#pragma once
#include "Operator.h"
#include "Table.h"
#include "Catalog.h"

class IndexScanOperator : public Operator {
public:
    IndexScanOperator(Table* table, uint32_t pk_value);
    
    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    uint32_t pk_value_;
    bool already_returned_;
};