#include "SeqScanOperator.h"

SeqScanOperator::SeqScanOperator(Table* table, const std::string& alias)
    : table_(table), alias_(alias) {}

void SeqScanOperator::Init() {
    cursor_ = std::make_unique<Cursor>(*table_);
}

std::optional<Row> SeqScanOperator::Next() {
    std::optional<Row> current = cursor_.get()->current_row();
    if (current.has_value()) {
        cursor_.get()->advance();
        return current;
    }
    return std::nullopt;
}

const std::vector<ColumnDefinition>& SeqScanOperator::GetOutputSchema() const {
    if (alias_.empty()) {
        return table_->get_columns();
    }
    if (!schema_built_) {
        aliased_schema_ = table_->get_columns();
        for (auto& col : aliased_schema_) {
            col.name = alias_ + "." + col.name;
        }
        schema_built_ = true;
    }
    return aliased_schema_;
}