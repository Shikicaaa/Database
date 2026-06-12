#include "FilterOperator.h"
#include <algorithm>

FilterOperator::FilterOperator(std::unique_ptr<Operator> child, const std::optional<WhereClause>& where_clause)
    : child_(std::move(child)), where_clause_(where_clause) {}

void FilterOperator::Init() {
    child_->Init();
}

const std::vector<ColumnDefinition>& FilterOperator::GetOutputSchema() const {
    return child_->GetOutputSchema();
}

std::optional<Row> FilterOperator::Next(){
    std::optional<Row> current_row = child_->Next();
    
    while(current_row.has_value()){
        if(where_clause_.has_value()){
            std::string lookup = where_clause_->column;
            if(!where_clause_->table_qualifier.empty()){
                lookup = where_clause_->table_qualifier + "." + where_clause_->column;
            }
            uint32_t col_index = find_column_index(lookup, child_->GetOutputSchema());
            if (col_index == -1)
            {
                std::cerr << "ERROR: Column " << lookup << " not found in schema!\n";
                return std::nullopt;
            }
            auto val = current_row.value()[col_index];
            if(compare_values(val, where_clause_->op, where_clause_->value)){
                return current_row;
            }
        } else {
            return current_row;
        }
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