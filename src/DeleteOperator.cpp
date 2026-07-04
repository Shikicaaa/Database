#include "DeleteOperator.h"
#include "Logger.h"
#include <cstring>

DeleteOperator::DeleteOperator(std::unique_ptr<Operator> child, Table* table,
                               Catalog* catalog, TxnContext* txn_ctx)
    : child_(std::move(child)), table_(table), catalog_(catalog), txn_ctx_(txn_ctx)
{
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
                            LOG_ERROR("Delete", "FK constraint violation — row with PK " + std::to_string(pk) + " is still referenced by '" + ref.child_table + "." + ref.fk_column_name + "'");
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
            LOG_ERROR("Delete", "Failed to delete row with PK " + std::to_string(pk) + " from table '" + table_->get_name() + "'");
        } else {
            delete_count++;
            if (txn_ctx_) {
                Serializer sr;
                auto before_bytes = sr.serialize(table_->get_columns(), saved_row);
                txn_ctx_->wal->log_delete(
                    txn_ctx_->txn_id,
                    table_->get_root_page_id(), 0,
                    reinterpret_cast<const char*>(before_bytes.data()),
                    static_cast<uint16_t>(before_bytes.size()));
                txn_ctx_->undo_log->push_back({UndoOpType::DELETE_OP,
                    table_->get_root_page_id(), before_bytes, {}});
            }
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
                        } else if (std::holds_alternative<double>(saved_row[ci])) {
                            uint32_t col_key = Catalog::hash_number(std::get<double>(saved_row[ci]));
                            idx_btree->remove(col_key, pk);
                        } else if (std::holds_alternative<DateTime>(saved_row[ci])) {
                            uint32_t col_key = Catalog::hash_datetime(std::get<DateTime>(saved_row[ci]));
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