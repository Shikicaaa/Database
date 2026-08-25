#pragma once
#include "Operator.h"
#include "Table.h"
#include "Cursor.h"
#include <vector>
#include <memory>

class SeqScanOperator : public Operator {
public:
    explicit SeqScanOperator(Table* table, const std::string& alias = "");

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    std::string alias_;
    std::unique_ptr<Cursor> cursor_;

    mutable std::vector<ColumnDefinition> aliased_schema_;
    mutable bool schema_built_ = false;
};