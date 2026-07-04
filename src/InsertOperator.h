#pragma once
#include "Operator.h"
#include "Table.h"
#include "Serializer.h"
#include "Catalog.h"
#include "TxnContext.h"
#include <memory>

class InsertOperator : public Operator {
public:
    InsertOperator(Table* table, std::unique_ptr<Operator> child,
                   Catalog* catalog = nullptr, TxnContext* txn_ctx = nullptr);

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    std::unique_ptr<Operator> child_;
    Catalog* catalog_;
    TxnContext* txn_ctx_ = nullptr;

    std::vector<ColumnDefinition> dummy_schema_;

    bool has_executed_;
};