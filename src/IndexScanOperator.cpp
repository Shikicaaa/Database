#include "IndexScanOperator.h"

IndexScanOperator::IndexScanOperator(Table* table, uint32_t pk_value) : table_(table), pk_value_(pk_value), already_returned_(false) {}

void IndexScanOperator::Init() {
    already_returned_ = false;
}

const std::vector<ColumnDefinition>& IndexScanOperator::GetOutputSchema() const {
    return table_->get_columns();
}

std::optional<Row> IndexScanOperator::Next() {
    if (already_returned_) {
        return std::nullopt;
    }
    already_returned_ = true;
    return table_->find_row(pk_value_);
}