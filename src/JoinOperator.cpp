#include "JoinOperator.h"
#include <algorithm>
#include <stdexcept>

JoinOperator::JoinOperator(
    std::unique_ptr<Operator> left,
    std::unique_ptr<Operator> right,
    const JoinCondition& condition,
    const std::string& left_alias,
    const std::string& right_alias,
    JoinType join_type
) : left_(std::move(left)),
    right_(std::move(right)),
    condition_(condition),
    left_alias_(left_alias),
    right_alias_(right_alias),
    join_type_(join_type),
    right_initialized_(false),
    right_pos_(0),
    left_had_match_(false),
    right_unmatched_pos_(0)
{
    build_output_schema();
}

void JoinOperator::build_output_schema() {
    output_schema_.clear();

    for (const auto& col : left_->GetOutputSchema()) {
        ColumnDefinition qualified_col = col;
        if (!left_alias_.empty()) {
            qualified_col.name = left_alias_ + "." + col.name;
        }
        output_schema_.push_back(std::move(qualified_col));
    }
    for (const auto& col : right_->GetOutputSchema()) {
        ColumnDefinition qualified_col = col;
        if (!right_alias_.empty()) {
            qualified_col.name = right_alias_ + "." + col.name;
        }
        output_schema_.push_back(std::move(qualified_col));
    }
}

const std::vector<ColumnDefinition>& JoinOperator::GetOutputSchema() const {
    return output_schema_;
}

void JoinOperator::Init() {
    left_->Init();
    right_->Init();

    right_buffer_.clear();
    right_pos_ = 0;
    while (auto row = right_->Next()) {
        right_buffer_.push_back(std::move(*row));
    }

    right_matched_.assign(right_buffer_.size(), false);
    right_unmatched_pos_ = 0;
    left_had_match_ = false;

    left_current_row_ = left_->Next();
    right_initialized_ = true;
}

std::optional<Row> JoinOperator::Next() {
    if (join_type_ == JoinType::INNER) {
        while (left_current_row_.has_value()) {
            while (right_pos_ < right_buffer_.size()) {
                const Row& right_row = right_buffer_[right_pos_];
                if (evaluate_condition(*left_current_row_, right_row)) {
                    Row merged = merge_rows(*left_current_row_, right_row);
                    ++right_pos_;
                    return merged;
                }
                ++right_pos_;
            }
            left_current_row_ = left_->Next();
            right_pos_ = 0;
        }
        return std::nullopt;
    }

    if (join_type_ == JoinType::LEFT) {
        while (left_current_row_.has_value()) {
            while (right_pos_ < right_buffer_.size()) {
                const Row& right_row = right_buffer_[right_pos_];
                if (evaluate_condition(*left_current_row_, right_row)) {
                    left_had_match_ = true;
                    Row merged = merge_rows(*left_current_row_, right_row);
                    ++right_pos_;
                    return merged;
                }
                ++right_pos_;
            }
            if (!left_had_match_) {
                Row null_right(right_->GetOutputSchema().size(), Value{std::monostate{}});
                Row result = merge_rows(*left_current_row_, null_right);
                left_current_row_ = left_->Next();
                right_pos_ = 0;
                left_had_match_ = false;
                return result;
            }
            left_current_row_ = left_->Next();
            right_pos_ = 0;
            left_had_match_ = false;
        }
        return std::nullopt;
    }

    if (join_type_ == JoinType::RIGHT) {
        while (left_current_row_.has_value()) {
            while (right_pos_ < right_buffer_.size()) {
                const Row& right_row = right_buffer_[right_pos_];
                if (evaluate_condition(*left_current_row_, right_row)) {
                    right_matched_[right_pos_] = true;
                    Row merged = merge_rows(*left_current_row_, right_row);
                    ++right_pos_;
                    return merged;
                }
                ++right_pos_;
            }
            left_current_row_ = left_->Next();
            right_pos_ = 0;
        }
        while (right_unmatched_pos_ < right_buffer_.size()) {
            size_t pos = right_unmatched_pos_++;
            if (!right_matched_[pos]) {
                Row null_left(left_->GetOutputSchema().size(), Value{std::monostate{}});
                return merge_rows(null_left, right_buffer_[pos]);
            }
        }
        return std::nullopt;
    }

    // FULL OUTER
    while (left_current_row_.has_value()) {
        while (right_pos_ < right_buffer_.size()) {
            const Row& right_row = right_buffer_[right_pos_];
            if (evaluate_condition(*left_current_row_, right_row)) {
                left_had_match_ = true;
                right_matched_[right_pos_] = true;
                Row merged = merge_rows(*left_current_row_, right_row);
                ++right_pos_;
                return merged;
            }
            ++right_pos_;
        }
        if (!left_had_match_) {
            Row null_right(right_->GetOutputSchema().size(), Value{std::monostate{}});
            Row result = merge_rows(*left_current_row_, null_right);
            left_current_row_ = left_->Next();
            right_pos_ = 0;
            left_had_match_ = false;
            return result;
        }
        left_current_row_ = left_->Next();
        right_pos_ = 0;
        left_had_match_ = false;
    }
    while (right_unmatched_pos_ < right_buffer_.size()) {
        size_t pos = right_unmatched_pos_++;
        if (!right_matched_[pos]) {
            Row null_left(left_->GetOutputSchema().size(), Value{std::monostate{}});
            return merge_rows(null_left, right_buffer_[pos]);
        }
    }
    return std::nullopt;
}

Row JoinOperator::merge_rows(const Row& left_row, const Row& right_row) const {
    Row merged = left_row;
    merged.insert(merged.end(), right_row.begin(), right_row.end());
    return merged;
}

static int find_col_index(const std::string& qualifier, const std::string& column, const std::vector<ColumnDefinition>& schema) {
   for(size_t i = 0; i < schema.size(); ++i){
        if (schema[i].name == column) return i;
   }
   auto bare_of = [](const std::string& name) {
        auto dot = name.rfind(".");
        return (dot == std::string::npos) ? name : name.substr(dot + 1);
   };

   std::string bare_col = bare_of(column);
   for(size_t i = 0; i < schema.size(); ++i){
        if (bare_of(schema[i].name) == bare_col) return i;
   }
   return -1;
}

bool JoinOperator::evaluate_condition(const Row& left_row, const Row& right_row) const {
    Row merged = merge_rows(left_row, right_row);

    int left_index = find_col_index("", condition_.left_column, output_schema_);
    int right_index = find_col_index("", condition_.right_column, output_schema_);

    if(left_index == -1 || right_index == -1) {
        throw std::runtime_error("Join condition references non-existent column(s)");
    }

    const Value& left_val = merged[left_index];
    const Value& right_val = merged[right_index];

    auto cmp = [&condition = condition_](const auto& a, const auto& b) -> bool {
        if (condition.op == "=")  return a == b;
        if (condition.op == "!=") return a != b;
        if (condition.op == "<")  return a <  b;
        if (condition.op == ">")  return a >  b;
        if (condition.op == "<=") return a <= b;
        if (condition.op == ">=") return a >= b;
        throw std::runtime_error("Unsupported operator in join condition: " + condition.op);
    };
    
    if(std::holds_alternative<int32_t>(left_val) && std::holds_alternative<int32_t>(right_val)) {
        return cmp(std::get<int32_t>(left_val), std::get<int32_t>(right_val));
    }
    if(std::holds_alternative<double>(left_val) && std::holds_alternative<double>(right_val)) {
        return cmp(std::get<double>(left_val), std::get<double>(right_val));
    }
    if(std::holds_alternative<std::string>(left_val) && std::holds_alternative<std::string>(right_val)) {
        return cmp(std::get<std::string>(left_val), std::get<std::string>(right_val));
    }
    if(std::holds_alternative<bool>(left_val) && std::holds_alternative<bool>(right_val)) {
        return cmp(std::get<bool>(left_val), std::get<bool>(right_val));
    }
    throw std::runtime_error("Incompatible value types in join condition");
}