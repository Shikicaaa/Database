#include "InsertOperator.h"
#include "TypeCoercion.h"
#include <cstring>

InsertOperator::InsertOperator(Table* table, std::unique_ptr<Operator> child, Catalog* catalog)
    : table_(table), child_(std::move(child)), catalog_(catalog), has_executed_(false) 
{
    dummy_schema_ = table_->get_columns();
}

void InsertOperator::Init() {
    child_->Init();
    has_executed_ = false;
}

const std::vector<ColumnDefinition>& InsertOperator::GetOutputSchema() const {
    return dummy_schema_;
}

std::optional<Row> InsertOperator::Next() {
    if (has_executed_){
        return std::nullopt;
    }
    std::optional<Row> row = child_->Next();
    int row_count = 0;
    while(row.has_value()){
        Row coerced = coerce_row(row.value(), table_->get_columns());
        bool fk_violation = false;
        // FK check
        if(catalog_){
            const auto& cols = table_->get_columns();
            for(size_t i = 0; i < cols.size(); i++){
                if(cols[i].fk_table.empty()) continue;

                const Value& fk_val = coerced[i];
                if(std::holds_alternative<std::monostate>(fk_val)) continue; // NULL are allowed for nullable FK columns
                if(!catalog_->fk_value_exists(cols[i].fk_table, cols[i].fk_column, fk_val)){
                    std::cerr << "ERROR: FK constraint violation on INSERT - value for column '"
                              << cols[i].name << "' does not exist in '"
                              << cols[i].fk_table << "." << cols[i].fk_column << "'.\n";
                    fk_violation = true;
                    break;
                }
            }
            if(fk_violation) {
                row = child_->Next();
                continue;
            }
        }
        if(!table_->insert_row(coerced)){
            std::cerr << "ERROR: Failed to insert row into table '" << table_->get_columns()[0].name << "'\n";
        } else {
            row_count++;
            if (catalog_) {
                auto indexes = catalog_->get_indexes_for_table(table_->get_name());
                const auto& cols = table_->get_columns();
                for (const auto& idx : indexes) {
                    for (size_t ci = 0; ci < cols.size(); ci++) {
                        if (cols[ci].name != idx.column_name) continue;
                        BTree* idx_btree = catalog_->get_index_btree(idx.index_name);
                        if (!idx_btree) break;
                        uint32_t pk = table_->extract_primary_key(coerced);
                           // INT
                        if (std::holds_alternative<int32_t>(coerced[ci])) {
                            uint32_t col_val = static_cast<uint32_t>(std::get<int32_t>(coerced[ci]));
                            idx_btree->insert(col_val, pk, &pk, sizeof(uint32_t));
                            // VARCHAR
                        } else if (std::holds_alternative<std::string>(coerced[ci])) {
                            const std::string& s = std::get<std::string>(coerced[ci]);
                            uint32_t col_key = Catalog::hash_varchar(s);
                            std::vector<uint8_t> d;
                            d.push_back(static_cast<uint8_t>(s.size()));
                            d.insert(d.end(), s.begin(), s.end());
                            idx_btree->insert(col_key, pk, d.data(), static_cast<uint16_t>(d.size()));
                            // NUMBER
                        } else if (std::holds_alternative<double>(coerced[ci])) {
                            double v = std::get<double>(coerced[ci]);
                            uint32_t col_key = Catalog::hash_number(v);
                            std::vector<uint8_t> d(8);
                            std::memcpy(d.data(), &v, 8);
                            idx_btree->insert(col_key, pk, d.data(), 8);
                        
                            // DATETIME
                        } else if (std::holds_alternative<DateTime>(coerced[ci])) {
                            const DateTime& dt = std::get<DateTime>(coerced[ci]);
                            uint32_t col_key = Catalog::hash_datetime(dt);
                            std::vector<uint8_t> d = {
                                static_cast<uint8_t>(dt.year & 0xFF),
                                static_cast<uint8_t>((dt.year >> 8) & 0xFF),
                                dt.month, dt.day, dt.hour, dt.minute, dt.second
                            };
                            idx_btree->insert(col_key, pk, d.data(), static_cast<uint16_t>(d.size()));
                        }
                        break;
                    }
                }
            }
        }
        row = child_->Next();
    }
    has_executed_ = true;
    return Row{Value(row_count)};
}