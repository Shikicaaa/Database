#pragma once
#include "Operator.h"
#include "Table.h"
#include <memory>

class InsertOperator : public Operator {
public:
    InsertOperator(Table* table, std::unique_ptr<Operator> child);

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    std::unique_ptr<Operator> child_;

    std::vector<ColumnDefinition> dummy_schema_;

    bool has_executed_;
};