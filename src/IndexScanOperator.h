#pragma once
#include "Operator.h"
#include "Table.h"
#include "Catalog.h"

class IndexScanOperator : public Operator {
public:
    IndexScanOperator(Table* table, uint32_t pk_value, const std::string& alias = "");
    
    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    uint32_t pk_value_;
    bool already_returned_;
    std::string alias_;
    mutable std::vector<ColumnDefinition> aliased_schema_;
    mutable bool schema_built_ = false;
};