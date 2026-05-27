#include "SeqScanOperator.h"

SeqScanOperator::SeqScanOperator(Table* table) : table_(table) {}

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
    return table_->get_columns();
}