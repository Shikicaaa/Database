#include "SecondaryIndexScanOperator.h"

SecondaryIndexScanOperator::SecondaryIndexScanOperator(
    Table* table, BTree* index_btree, int32_t search_value)
    : table_(table), index_btree_(index_btree),
      search_value_(search_value), pos_(0)
{}

void SecondaryIndexScanOperator::Init() {
    matching_pks_ = index_btree_->find_range(static_cast<uint32_t>(search_value_));
    pos_ = 0;
}

std::optional<Row> SecondaryIndexScanOperator::Next() {
    while (pos_ < matching_pks_.size()) {
        uint32_t pk = matching_pks_[pos_++];
        auto row = table_->find_row(pk);
        if (row.has_value()) return row;
    }
    return std::nullopt;
}

const std::vector<ColumnDefinition>& SecondaryIndexScanOperator::GetOutputSchema() const {
    return table_->get_columns();
}
