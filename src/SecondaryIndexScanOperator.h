#pragma once
#include "Operator.h"
#include "Table.h"
#include "BTree.h"
#include "Serializer.h"
#include <vector>
#include <cstring>

class SecondaryIndexScanOperator : public Operator {
public:
    SecondaryIndexScanOperator(
        Table* table, BTree* index_btree,
        uint32_t index_key,
        DataType col_type,
        std::string original_string,
        std::vector<uint8_t> original_bytes,
        int column_index
    );

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    BTree* index_btree_;
    uint32_t index_key_;
    DataType col_type_;
    std::string original_string_;       // FOR VARCHAR ONLY 
    std::vector<uint8_t> original_bytes_; // FOR NUMBER / DATETIME
    int column_index_;
    std::vector<uint32_t> matching_pks_;
    size_t pos_;
};
