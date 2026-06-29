#pragma once
#include "Operator.h"
#include "Table.h"
#include "BTree.h"
#include <vector>

class SecondaryIndexScanOperator : public Operator {
public:
    SecondaryIndexScanOperator(
        Table* table, BTree* index_btree,
        uint32_t index_key,
        std::string original_string,
        int column_index
    );

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    BTree* index_btree_;
    uint32_t index_key_;
    std::string original_string_;  // empty = INT index 
    int column_index_;
    std::vector<uint32_t> matching_pks_;
    size_t pos_;
};
