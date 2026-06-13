#include "UpdateOperator.h"
#include "TypeCoercion.h"
#include <algorithm>

UpdateOperator::UpdateOperator(std::unique_ptr<Operator> child, Table* table, const std::vector<std::pair<std::string, Value>>& set_clauses) 
    : child_(std::move(child)), table_(table), set_clauses_(set_clauses) {
    dummy_schema_.push_back(ColumnDefinition{"updated_rows", DataType::INT, false, false, false, 4});
}

void UpdateOperator::Init() {
    child_->Init();
    has_executed_ = false;
}

int UpdateOperator::find_column_index(const std::string& col_name) const {
    const auto& schema = table_->get_columns();
    for (int i = 0; i < (int)schema.size(); i++) {
        std::string a = col_name, b = schema[i].name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (a == b) return i;
    }
    return -1;
}

const std::vector<ColumnDefinition>& UpdateOperator::GetOutputSchema() const {
    return dummy_schema_;
}

std::optional<Row> UpdateOperator::Next() {
    if (has_executed_) {
        return std::nullopt;
    }

    std::vector<Row> target_rows;
    for (auto row = child_->Next(); row.has_value(); row = child_->Next()) {
        target_rows.push_back(row.value());
    }

    int update_count = 0;
    for (const Row& old_row : target_rows) {
        uint32_t pk = table_->extract_primary_key(old_row);

        Row new_row = old_row;
        for (const auto& [col_name, new_val] : set_clauses_) {
            int idx = find_column_index(col_name);
            if (idx != -1) {
                new_row[idx] = coerce_value(new_val, table_->get_columns()[idx]);
            }
        }

        if (!table_->update_row(pk, new_row)) {
            std::cerr << "ERROR: Failed to update row with primary key " << pk
                      << " in table '" << table_->get_columns()[0].name << "'\n";
        } else {
            update_count++;
        }
    }

    has_executed_ = true;
    return Row{Value(update_count)};
}