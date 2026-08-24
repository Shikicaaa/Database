#include "FilterOperator.h"
#include "LikeOperator.h"
#include "Logger.h"
#include <algorithm>

FilterOperator::FilterOperator(std::unique_ptr<Operator> child, const std::optional<WhereClause>& where_clause)
    : child_(std::move(child)), where_clause_(where_clause) {}

void FilterOperator::Init() {
    child_->Init();
}

const std::vector<ColumnDefinition>& FilterOperator::GetOutputSchema() const {
    return child_->GetOutputSchema();
}

std::optional<Row> FilterOperator::Next() {
    std::optional<Row> current_row = child_->Next();

    while (current_row.has_value()) {
        if (!where_clause_.has_value() || !where_clause_.value()) return current_row;
        if (evaluate(*where_clause_.value(), *current_row, child_->GetOutputSchema()))
            return current_row;
        current_row = child_->Next();
    }
    return std::nullopt;
}

int FilterOperator::find_column_index(const std::string& col_name,
                                const std::vector<ColumnDefinition>& schema) const
{
    for (int i = 0; i < (int)schema.size(); i++) {
        std::string a = col_name, b = schema[i].name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (a == b) return i;
    }
    for(int i = 0; i < (int)schema.size(); i++){
        std::string b = schema[i].name;
        auto dot = b.rfind(".");
        if(dot != std::string::npos){
            b = b.substr(dot + 1);
        }
        std::string a = col_name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if(a == b) return i;
    }
    return -1;
}

bool FilterOperator::compare_values(const Value& row_val,
                              const std::string& op,
                              const Value& where_val) const
{
    if (op == "IS NULL")     return std::holds_alternative<std::monostate>(row_val);
    if (op == "IS NOT NULL") return !std::holds_alternative<std::monostate>(row_val);

    if (op == "LIKE" || op == "ILIKE") {
        if (!std::holds_alternative<std::string>(row_val) || !std::holds_alternative<std::string>(where_val)) {
            return false; // LIKE can only be applied to strings
        }
        return like_match(std::get<std::string>(where_val), std::get<std::string>(row_val), op == "ILIKE");
    }

    auto cmp = [&op](const auto& a, const auto& b) -> bool {
        if (op == "=")  return a == b;
        if (op == "!=") return a != b;
        if (op == "<")  return a <  b;
        if (op == ">")  return a >  b;
        if (op == "<=") return a <= b;
        if (op == ">=") return a >= b;
        return false;
    };

    // int == int
    if (std::holds_alternative<int32_t>(row_val) &&
        std::holds_alternative<int32_t>(where_val)) {
        return cmp(std::get<int32_t>(row_val), std::get<int32_t>(where_val));
    }

    // double == double
    if (std::holds_alternative<double>(row_val) &&
        std::holds_alternative<double>(where_val)) {
        return cmp(std::get<double>(row_val), std::get<double>(where_val));
    }

    // int i doubleo onda konvertujemo int u double
    if (std::holds_alternative<int32_t>(row_val) &&
        std::holds_alternative<double>(where_val)) {
        return cmp((double)std::get<int32_t>(row_val), std::get<double>(where_val));
    }
    if (std::holds_alternative<double>(row_val) &&
        std::holds_alternative<int32_t>(where_val)) {
        return cmp(std::get<double>(row_val), (double)std::get<int32_t>(where_val));
    }

    // string == string
    if (std::holds_alternative<std::string>(row_val) &&
        std::holds_alternative<std::string>(where_val)) {
        return cmp(std::get<std::string>(row_val), std::get<std::string>(where_val));
    }

    // bool == bool
    if (std::holds_alternative<bool>(row_val) &&
        std::holds_alternative<bool>(where_val)) {
        return cmp(std::get<bool>(row_val), std::get<bool>(where_val));
    }

    // NULL = NULL => true, sve ostalo false
    if (std::holds_alternative<std::monostate>(row_val) &&
        std::holds_alternative<std::monostate>(where_val)) {
        return op == "=";
    }

    return false;
}

bool FilterOperator::evaluate(const Condition& cond, const Row& row,
                               const std::vector<ColumnDefinition>& schema) const
{
    switch (cond.type) {
        case ConditionType::AND:
            return evaluate(*cond.children[0], row, schema) &&
                   evaluate(*cond.children[1], row, schema);

        case ConditionType::OR:
            return evaluate(*cond.children[0], row, schema) ||
                   evaluate(*cond.children[1], row, schema);

        case ConditionType::NOT:
            return !evaluate(*cond.children[0], row, schema);

        case ConditionType::COMPARISON: {
            std::string lookup = cond.column;
            if (!cond.table_qualifier.empty())
                lookup = cond.table_qualifier + "." + cond.column;

            int col_index = find_column_index(lookup, schema);
            if (col_index == -1) {
                LOG_ERROR("Filter", "Column '" + lookup + "' not found in schema");
                throw std::runtime_error("Column '" + lookup + "' not found in schema");
            }
            return compare_values(row[col_index], cond.op, cond.value);
        }
    }
    return false;
}
