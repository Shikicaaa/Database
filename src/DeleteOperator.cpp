#include "DeleteOperator.h"

DeleteOperator::DeleteOperator(std::unique_ptr<Operator> child, Table* table, Catalog* catalog)
    : child_(std::move(child)), table_(table), catalog_(catalog) {
    dummy_schema_.push_back(ColumnDefinition{"deleted_rows", DataType::INT, false, false, false, 4});
}

void DeleteOperator::Init() {
    child_->Init();
    has_executed_ = false;
}

const std::vector<ColumnDefinition>& DeleteOperator::GetOutputSchema() const {
    return dummy_schema_;
}

std::optional<Row> DeleteOperator::Next() {
    if (has_executed_) {
        return std::nullopt;
    }

    std::vector<std::pair<uint32_t, Row>> pk_rows;
    for (auto row = child_->Next(); row.has_value(); row = child_->Next()) {
        uint32_t pk = table_->extract_primary_key(row.value());
        pk_rows.push_back({pk, row.value()});
    }

    int delete_count = 0;
    for (auto& [pk, saved_row] : pk_rows) {
        // reject delete if any child table still references this PK
        if (catalog_) {
            Value pk_val = Value(static_cast<int32_t>(pk));
            auto refs = catalog_->get_referencing_tables(table_->get_name());
            bool blocked = false;
            for (const auto& ref : refs) {
                Table* child_tbl = catalog_->get_table(ref.child_table);
                if (!child_tbl) continue;
                std::vector<Row> child_rows = child_tbl->scan_all();
                const auto& child_cols = child_tbl->get_columns();
                for (const auto& crow : child_rows) {
                    for (size_t ci = 0; ci < child_cols.size(); ci++) {
                        if (child_cols[ci].fk_table == table_->get_name() &&
                            child_cols[ci].name == ref.fk_column_name &&
                            crow[ci] == pk_val) {
                            std::cerr << "ERROR: FK constraint violation on DELETE - "
                                      << "row with PK " << pk << " is still referenced by '"
                                      << ref.child_table << "." << ref.fk_column_name << "'.\n";
                            blocked = true;
                            break;
                        }
                    }
                    if (blocked) break;
                }
                if (blocked) break;
            }
            if (blocked) continue;
        }

        if (!table_->remove_row(pk)) {
            std::cerr << "ERROR: Failed to delete row with primary key " << pk
                      << " from table '" << table_->get_name() << "'\n";
        } else {
            delete_count++;
            // Remove from secondary indexes
            if (catalog_) {
                auto indexes = catalog_->get_indexes_for_table(table_->get_name());
                const auto& cols = table_->get_columns();
                for (const auto& idx : indexes) {
                    for (size_t ci = 0; ci < cols.size(); ci++) {
                        if (cols[ci].name != idx.column_name || ci >= saved_row.size()) continue;
                        BTree* idx_btree = catalog_->get_index_btree(idx.index_name);
                        if (!idx_btree) break;
                        if (std::holds_alternative<int32_t>(saved_row[ci])) {
                            uint32_t col_val = static_cast<uint32_t>(std::get<int32_t>(saved_row[ci]));
                            idx_btree->remove(col_val, pk);
                        } else if (std::holds_alternative<std::string>(saved_row[ci])) {
                            uint32_t col_key = Catalog::hash_varchar(std::get<std::string>(saved_row[ci]));
                            idx_btree->remove(col_key, pk);
                        }
                        break;
                    }
                }
            }
        }
    }

    has_executed_ = true;
    return Row{Value(delete_count)};
}