#include "IndexScanOperator.h"

IndexScanOperator::IndexScanOperator(Table* table, uint32_t pk_value, const std::string& alias)
    : table_(table), pk_value_(pk_value), already_returned_(false), alias_(alias) {}
    
    void IndexScanOperator::Init() {
        already_returned_ = false;
    }
    
const std::vector<ColumnDefinition>& IndexScanOperator::GetOutputSchema() const {
    if (alias_.empty()) return table_->get_columns();
    if (!schema_built_) {
        aliased_schema_ = table_->get_columns();
        for (auto& col : aliased_schema_) col.name = alias_ + "." + col.name;
        schema_built_ = true;
    }
    return aliased_schema_;
}

std::optional<Row> IndexScanOperator::Next() {
    if (already_returned_) {
        return std::nullopt;
    }
    already_returned_ = true;
    return table_->find_row(pk_value_);
}