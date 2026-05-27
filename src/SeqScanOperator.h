#pragma once
#include "Operator.h"
#include "Table.h"
#include "Cursor.h"
#include <vector>
#include <memory>

class SeqScanOperator : public Operator {
public:
    explicit SeqScanOperator(Table* table);

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Table* table_;
    
    std::unique_ptr<Cursor> cursor_;
};