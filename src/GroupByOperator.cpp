#include "GroupByOperator.h"

int GroupByOperator::find_column_index(const std::string &col_name, const std::vector<ColumnDefinition> &schema) const
{
    for (int i = 0; i < (int)schema.size(); i++) {
        std::string a = col_name, b = schema[i].name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (a == b) return i;
    }

    for (int i = 0; i < (int)schema.size(); i++) {
        std::string b = schema[i].name;
        auto dot = b.rfind(".");
        if (dot != std::string::npos) {
            b = b.substr(dot + 1);
        }
        std::string a = col_name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (a == b) return i;
    }

    return -1;
}

void GroupByOperator::build_output_schema(const std::vector<ColumnDefinition> &child_schema)
{
    output_schema_.clear();
    for (const auto& item : select_items_) {
        if (item.aggregate_function.empty()){
            int idx = find_column_index(item.column, child_schema);
            if (idx == -1) {
                throw std::runtime_error("Column '" + item.column + "' not found in child schema");
            }
            ColumnDefinition col_def = child_schema[idx];
            if (!item.alias.empty()) {
                col_def.name = item.alias;
            }
            output_schema_.push_back(col_def);
        }else {
            ColumnDefinition col_def;
            col_def.is_primary_key = false;
            col_def.is_nullable = true;
            col_def.is_unique = false;
            col_def.max_length = 0;

            std::string default_name = item.aggregate_function + "OF" + (item.is_star ? "*" : item.column);

            col_def.name = item.alias.empty() ? default_name : item.alias;
            
            if (item.aggregate_function == "COUNT") {
                col_def.type = DataType::INT;
            } else if (item.aggregate_function == "SUM" || item.aggregate_function == "AVG") {
                col_def.type = DataType::NUMBER;
            } else { // MIN / MAX
                if (item.is_star) {
                    throw std::runtime_error("'*' can only be used with COUNT");
                }
                int idx = find_column_index(item.column, child_schema);
                if (idx == -1) {
                    throw std::runtime_error("Column '" + item.column + "' not found in schema");
                }
                col_def.type = child_schema[idx].type;
            }
            output_schema_.push_back(col_def);
        }
    }
}

bool GroupByOperator::value_less_than(const Value& a, const Value& b) const
{
    bool a_null = std::holds_alternative<std::monostate>(a);
    bool b_null = std::holds_alternative<std::monostate>(b);
    if (a_null || b_null) return a_null && !b_null;

    if (std::holds_alternative<int32_t>(a) && std::holds_alternative<int32_t>(b))
        return std::get<int32_t>(a) < std::get<int32_t>(b);
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b))
        return std::get<double>(a) < std::get<double>(b);
    if (std::holds_alternative<int32_t>(a) && std::holds_alternative<double>(b))
        return (double)std::get<int32_t>(a) < std::get<double>(b);
    if (std::holds_alternative<double>(a) && std::holds_alternative<int32_t>(b))
        return std::get<double>(a) < (double)std::get<int32_t>(b);
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b))
        return std::get<std::string>(a) < std::get<std::string>(b);
    if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b))
        return std::get<bool>(a) < std::get<bool>(b);
    if (std::holds_alternative<DateTime>(a) && std::holds_alternative<DateTime>(b))
        return std::get<DateTime>(a) < std::get<DateTime>(b);

    return false;
}

bool GroupByOperator::compare_having(const Value& row_val, const std::string& op, const Value& target) const
{
    auto cmp = [&op](const auto& a, const auto& b) -> bool {
        if (op == "=")  return a == b;
        if (op == "!=") return a != b;
        if (op == "<")  return a <  b;
        if (op == ">")  return a >  b;
        if (op == "<=") return a <= b;
        if (op == ">=") return a >= b;
        return false;
    };

    if (std::holds_alternative<int32_t>(row_val) && std::holds_alternative<int32_t>(target))
        return cmp(std::get<int32_t>(row_val), std::get<int32_t>(target));
    if (std::holds_alternative<double>(row_val) && std::holds_alternative<double>(target))
        return cmp(std::get<double>(row_val), std::get<double>(target));
    if (std::holds_alternative<int32_t>(row_val) && std::holds_alternative<double>(target))
        return cmp((double)std::get<int32_t>(row_val), std::get<double>(target));
    if (std::holds_alternative<double>(row_val) && std::holds_alternative<int32_t>(target))
        return cmp(std::get<double>(row_val), (double)std::get<int32_t>(target));
    if (std::holds_alternative<std::string>(row_val) && std::holds_alternative<std::string>(target))
        return cmp(std::get<std::string>(row_val), std::get<std::string>(target));
    if (std::holds_alternative<bool>(row_val) && std::holds_alternative<bool>(target))
        return cmp(std::get<bool>(row_val), std::get<bool>(target));
    if (std::holds_alternative<DateTime>(row_val) && std::holds_alternative<DateTime>(target))
        return cmp(std::get<DateTime>(row_val), std::get<DateTime>(target));

    return false;
}

void GroupByOperator::update_agg_state(AggregateState& state, const Value& val) const
{
    bool is_null = std::holds_alternative<std::monostate>(val);

    if (!is_null) {
        state.count++;

        if (std::holds_alternative<int32_t>(val)) {
            state.sum += std::get<int32_t>(val);
        } else if (std::holds_alternative<double>(val)) {
            state.sum += std::get<double>(val);
        }

        if (!state.has_min_max) {
            state.min_value = val;
            state.max_value = val;
            state.has_min_max = true;
        } else {
            if (value_less_than(val, state.min_value)) state.min_value = val;
            if (value_less_than(state.max_value, val)) state.max_value = val;
        }
    }
}

Value GroupByOperator::finalize_agg(const SelectItem &item, const AggregateState &state, DataType source_type) const
{
    if (item.aggregate_function == "COUNT") {
        return Value(static_cast<int32_t>(state.count));
    } else if (item.aggregate_function == "SUM") {
        return Value(state.sum);
    } else if (item.aggregate_function == "AVG") {
        if (state.count == 0) return Value(0.0);
        return Value(state.sum / state.count);
    } else if (item.aggregate_function == "MIN") {
        return Value(state.has_min_max ? state.min_value : std::monostate{});
    } else if (item.aggregate_function == "MAX") {
        return Value(state.has_min_max ? state.max_value : std::monostate{});
    }

    throw std::runtime_error("Unknown aggregate function: " + item.aggregate_function);
}

void GroupByOperator::Init()
{
    child_->Init();
    result_rows_.clear();
    current_index_ = 0;

    const auto& child_schema = child_->GetOutputSchema();
    
    auto is_in_group_by = [&](const std::string& col_name) {
        for (const auto& gc : group_by_columns_) {
            std::string a = col_name, b = gc;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (a == b) return true;
        }
        return false;
    };

    for (const auto& item : select_items_) {
        if (item.aggregate_function.empty() && !is_in_group_by(item.column)) {
            throw std::runtime_error("Column '" + item.column + "' must appear in the GROUP BY clause or be used in an aggregate function");
        }
        if (having_clause_.has_value()) {
            const auto& hi = having_clause_->item;
            if (hi.aggregate_function.empty() && !is_in_group_by(hi.column)) {
                throw std::runtime_error(
                    "HAVING column '" + hi.column + "' must appear in GROUP BY clause or be an aggregate");
            }
        }
    }

    build_output_schema(child_schema);

    std::vector<SelectItem> tracked_items = select_items_;
    int having_agg_index = -1;
    if (having_clause_.has_value() && !having_clause_->item.aggregate_function.empty()) {
        tracked_items.push_back(having_clause_->item);
        having_agg_index = (int)tracked_items.size() - 1;
    }

    std::vector<int> group_by_indices;
    for (const auto& col : group_by_columns_) {
        int idx = find_column_index(col, child_schema);
        if (idx == -1) {
            throw std::runtime_error("GROUP BY column '" + col + "' not found in schema");
        }
        group_by_indices.push_back(idx);
    }

    std::vector<int> agg_col_indices(tracked_items.size(), -1);
    for (size_t i = 0; i < tracked_items.size(); i++) {
        const auto& item = tracked_items[i];
        if (!item.aggregate_function.empty() && !item.is_star) {
            int idx = find_column_index(item.column, child_schema);
            if (idx == -1) {
                throw std::runtime_error("Column '" + item.column + "' not found in schema");
            }
            agg_col_indices[i] = idx;
        }
    }

    std::map<std::vector<Value>, std::vector<AggregateState>> groups;
    std::map<std::vector<Value>, Row> representative_rows;

    std::optional<Row> row;
    while ((row = child_->Next())) {
        std::vector<Value> key;
        for (int idx : group_by_indices) {
            key.push_back((*row)[idx]);
        }

        auto it = groups.find(key);
        if (it == groups.end()) {
            groups[key] = std::vector<AggregateState>(tracked_items.size());
            representative_rows[key] = *row;
            it = groups.find(key);
        }

        for (size_t i = 0; i < tracked_items.size(); i++) {
            const auto& item = tracked_items[i];
            if (item.aggregate_function.empty()) continue;

            if (item.aggregate_function == "COUNT" && item.is_star) {
                it->second[i].count++;
            } else {
                update_agg_state(it->second[i], (*row)[agg_col_indices[i]]);
            }
        }
    }

    // fallback if there are no group by columns and no rows, we still need to produce a single row with aggregate results
    if (groups.empty() && group_by_columns_.empty()) {
        groups[{}] = std::vector<AggregateState>(select_items_.size());
        representative_rows[{}] = Row(child_schema.size(), Value(std::monostate{}));
    }

    for (const auto& [key, states] : groups) {
        const Row& rep_row = representative_rows[key];

        if (having_clause_.has_value()) {
            Value having_val;
            const auto& hi = having_clause_->item;
            if (!hi.aggregate_function.empty()) {
                DataType src_type = DataType::INT;
                if (!hi.is_star) src_type = child_schema[agg_col_indices[having_agg_index]].type;
                having_val = finalize_agg(tracked_items[having_agg_index], states[having_agg_index], src_type);
            } else {
                int idx = find_column_index(hi.column, child_schema);
                having_val = rep_row[idx];
            }
            if (!compare_having(having_val, having_clause_->op, having_clause_->value)) {
                continue;
            }
        }

        Row out_row;
        for (size_t i = 0; i < select_items_.size(); i++) {
            const auto& item = select_items_[i];
            if (item.aggregate_function.empty()) {
                int idx = find_column_index(item.column, child_schema);
                out_row.push_back(rep_row[idx]);
            } else {
                DataType source_type = DataType::INT;
                if (!item.is_star) source_type = child_schema[agg_col_indices[i]].type;
                out_row.push_back(finalize_agg(item, states[i], source_type));
            }
        }
        result_rows_.push_back(std::move(out_row));
    }
}

std::optional<Row> GroupByOperator::Next() {
    if (current_index_ < result_rows_.size()) {
        return result_rows_[current_index_++];
    }
    return std::nullopt;
}