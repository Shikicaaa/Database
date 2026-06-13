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

    std::vector<uint32_t> pks;
    for (auto row = child_->Next(); row.has_value(); row = child_->Next()) {
        pks.push_back(table_->extract_primary_key(row.value()));
    }

    int delete_count = 0;
    for (uint32_t pk : pks) {
        // reject delete if any child table still references this PK
        if (catalog_) {
            Value pk_val = Value(static_cast<int32_t>(pk));
            auto refs = catalog_->get_referencing_tables(table_->get_name());
            bool blocked = false;
            for (const auto& ref : refs) {
                Table* child_tbl = catalog_->get_table(ref.child_table);
                if (!child_tbl) continue;
                // Scan child table rows and check if any rows FK column matches pk
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
        }
    }

    has_executed_ = true;
    return Row{Value(delete_count)};
}