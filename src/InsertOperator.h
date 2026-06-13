#pragma once
#include "Operator.h"
#include "Table.h"
#include "Serializer.h"
#include "Catalog.h"
#include <memory>

class InsertOperator : public Operator {
public:
    InsertOperator(Table* table, std::unique_ptr<Operator> child, Catalog* catalog = nullptr);

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    std::unique_ptr<Operator> child_;
    Catalog* catalog_;

    std::vector<ColumnDefinition> dummy_schema_;

    bool has_executed_;
};