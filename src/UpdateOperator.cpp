#include "UpdateOperator.h"
#include "TypeCoercion.h"
#include <algorithm>

UpdateOperator::UpdateOperator(std::unique_ptr<Operator> child, Table* table, const std::vector<std::pair<std::string, Value>>& set_clauses, Catalog* catalog) 
    : child_(std::move(child)), table_(table),  set_clauses_(set_clauses), catalog_(catalog) {
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
        bool fk_violation = false;
        for (const auto& [col_name, new_val] : set_clauses_) {
            int idx = find_column_index(col_name);
            if (idx == -1) {
                std::cerr << "ERROR: Column '" << col_name << "' not found in table schema.\n";
                fk_violation = true;
                break;
            }

            Value coerced = coerce_value(new_val, table_->get_columns()[idx]);
            new_row[idx] = coerced;

            // the new value must exist as a PK in the referenced parent table
            if (catalog_ && !table_->get_columns()[idx].fk_table.empty()) {
                if (!std::holds_alternative<std::monostate>(coerced) &&
                    !catalog_->fk_value_exists(table_->get_columns()[idx].fk_table,
                                               table_->get_columns()[idx].fk_column,
                                               coerced)) {
                    std::cerr << "ERROR: FK constraint violation on UPDATE - value for '"
                              << col_name << "' does not exist in '"
                              << table_->get_columns()[idx].fk_table << "."
                              << table_->get_columns()[idx].fk_column << "'.\n";
                    fk_violation = true;
                    break;
                }
            }

            if (!fk_violation && catalog_ && table_->get_columns()[idx].is_primary_key) {
                Value old_pk_value = Value(static_cast<int32_t>(table_->extract_primary_key(old_row)));
                Value new_pk_value = coerced;

                if (old_pk_value != new_pk_value) {
                    auto refs = catalog_->get_referencing_tables(table_->get_name());
                    for (const auto& ref : refs) {
                        if (catalog_->child_has_fk_value(ref.child_table, ref.fk_column_name, old_pk_value)) {
                            std::cerr << "ERROR: Cannot update primary key value for column '" << col_name
                                      << "' because it is referenced by child table '" << ref.child_table
                                      << "' on column '" << ref.fk_column_name << "'.\n";
                            fk_violation = true;
                            break;
                        }
                    }
                }
            }
        }

        if(fk_violation) continue;

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