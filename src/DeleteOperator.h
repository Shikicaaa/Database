#pragma once
#include "Operator.h"
#include "Table.h"
#include "Catalog.h"
#include "Parser/Parser.h"
#include <memory>

class DeleteOperator : public Operator {
public:
    DeleteOperator(std::unique_ptr<Operator> child, Table* table, Catalog* catalog = nullptr);

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    std::unique_ptr<Operator> child_;
    Catalog* catalog_ = nullptr;
    std::vector<ColumnDefinition> dummy_schema_;
    bool has_executed_ = false;
};