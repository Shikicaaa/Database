#include "OrderOperator.h"
#include <algorithm>

int OrderOperator::find_column_index(const std::string& col_name,
                                      const std::vector<ColumnDefinition>& schema) const
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

bool OrderOperator::value_less_than(const Value& a, const Value& b) const
{
    bool a_null = std::holds_alternative<std::monostate>(a);
    bool b_null = std::holds_alternative<std::monostate>(b);
    if (a_null || b_null) {
        return a_null && !b_null;
    }

    // int vs int
    if (std::holds_alternative<int32_t>(a) && std::holds_alternative<int32_t>(b)) {
        return std::get<int32_t>(a) < std::get<int32_t>(b);
    }

    // double vs double
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        return std::get<double>(a) < std::get<double>(b);
    }

    // int vs double and double vs int
    if (std::holds_alternative<int32_t>(a) && std::holds_alternative<double>(b)) {
        return (double)std::get<int32_t>(a) < std::get<double>(b);
    }
    if (std::holds_alternative<double>(a) && std::holds_alternative<int32_t>(b)) {
        return std::get<double>(a) < (double)std::get<int32_t>(b);
    }

    // string vs string
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a) < std::get<std::string>(b);
    }

    // bool vs bool
    if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b)) {
        return std::get<bool>(a) < std::get<bool>(b);
    }

    // DateTime vs DateTime
    if (std::holds_alternative<DateTime>(a) && std::holds_alternative<DateTime>(b)) {
        return std::get<DateTime>(a) < std::get<DateTime>(b);
    }

    return false;
}

void OrderOperator::Init() {
    child_->Init();
    sorted_rows_.clear();
    current_index_ = 0;

    std::optional<Row> row;
    while ((row = child_->Next())) {
        sorted_rows_.push_back(*row);
    }

    const auto& schema = child_->GetOutputSchema();

    std::sort(sorted_rows_.begin(), sorted_rows_.end(), [this, &schema](const Row& a, const Row& b) {
        for (const auto& order_clause : order_by_) {
            int col_index = find_column_index(order_clause.column, schema);
            if (col_index == -1) {
                throw std::runtime_error("Column '" + order_clause.column + "' not found in schema");
            }
            const Value& val_a = a[col_index];
            const Value& val_b = b[col_index];

            bool a_lt_b = value_less_than(val_a, val_b);
            bool b_lt_a = value_less_than(val_b, val_a);

            if (a_lt_b || b_lt_a) {
                return order_clause.ascending ? a_lt_b : b_lt_a;
            }
        }
        return false;
    });
}

std::optional<Row> OrderOperator::Next() {
    if (current_index_ >= sorted_rows_.size()) {
        return std::nullopt;
    }
    return sorted_rows_[current_index_++];
}