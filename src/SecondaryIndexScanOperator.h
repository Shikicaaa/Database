#pragma once
#include "Operator.h"
#include "Table.h"
#include "BTree.h"
#include <vector>

class SecondaryIndexScanOperator : public Operator {
public:
    SecondaryIndexScanOperator(Table* table, BTree* index_btree, int32_t search_value);

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table*   table_;
    BTree*   index_btree_;
    int32_t  search_value_;
    std::vector<uint32_t> matching_pks_;
    size_t   pos_;
};
