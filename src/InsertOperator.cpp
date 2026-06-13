#include "InsertOperator.h"
#include "TypeCoercion.h"

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
        }
        row = child_->Next();
    }
    has_executed_ = true;
    return Row{Value(row_count)};
}