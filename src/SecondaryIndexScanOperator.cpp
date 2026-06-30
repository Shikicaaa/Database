#include "SecondaryIndexScanOperator.h"

SecondaryIndexScanOperator::SecondaryIndexScanOperator(
    Table* table, BTree* index_btree,
    uint32_t index_key, DataType col_type,
    std::string original_string,
    std::vector<uint8_t> original_bytes,
    int column_index)
    : table_(table), index_btree_(index_btree),
      index_key_(index_key), col_type_(col_type),
      original_string_(std::move(original_string)),
      original_bytes_(std::move(original_bytes)),
      column_index_(column_index), pos_(0)
{}

void SecondaryIndexScanOperator::Init() {
    matching_pks_.clear();
    pos_ = 0;

    if (col_type_ == DataType::INT) {
        matching_pks_ = index_btree_->find_range(index_key_);
    } else if (col_type_ == DataType::VARCHAR) {
        auto chain = index_btree_->find_range_with_data(index_key_);
        for (auto& entry : chain) {
            if (entry.data.empty()) continue;
            uint8_t stored_len = static_cast<uint8_t>(entry.data[0]);
            if (stored_len != static_cast<uint8_t>(original_string_.size())) continue;
            bool match = true;
            for (uint8_t j = 0; j < stored_len; j++) {
                if (entry.data[1 + j] != original_string_[j]) { match = false; break; }
            }
            if (match) matching_pks_.push_back(entry.pk);
        }
    } else {
        // NUMBER and DATETIME we compare here byte by byte
        auto chain = index_btree_->find_range_with_data(index_key_);
        for (auto& entry : chain) {
            if (entry.data.size() != original_bytes_.size()) continue;
            if (std::memcmp(entry.data.data(), original_bytes_.data(), original_bytes_.size()) == 0)
                matching_pks_.push_back(entry.pk);
        }
    }
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
