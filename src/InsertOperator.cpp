#include "InsertOperator.h"
#include "TypeCoercion.h"

InsertOperator::InsertOperator(Table* table, std::unique_ptr<Operator> child)
    : table_(table), child_(std::move(child)), has_executed_(false) 
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