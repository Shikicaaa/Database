#pragma once
#include "Operator.h"
#include <vector>

class ValuesOperator : public Operator {
public:
    ValuesOperator(const std::vector<Row>& rows, const std::vector<ColumnDefinition>& schema);

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    std::vector<Row> rows_;
    std::vector<ColumnDefinition> schema_;
    size_t current_index_;
};