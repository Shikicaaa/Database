#include "ValuesOperator.h"

ValuesOperator::ValuesOperator(const std::vector<Row>& rows, const std::vector<ColumnDefinition>& schema)
    : rows_(rows), schema_(schema), current_index_(0) {}

void ValuesOperator::Init() {
    current_index_ = 0;
}

std::optional<Row> ValuesOperator::Next() {
    if (current_index_ < rows_.size()) {
        return rows_[current_index_++];
    }
    return std::nullopt;
}

const std::vector<ColumnDefinition>& ValuesOperator::GetOutputSchema() const {
    return schema_;
}