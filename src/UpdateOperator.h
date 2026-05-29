#pragma once
#include "Operator.h"
#include "Table.h"
#include "Parser/Parser.h"
#include <memory>

class UpdateOperator : public Operator {
public:
    UpdateOperator(std::unique_ptr<Operator> child, Table* table, const std::vector<std::pair<std::string, Value>>& set_clauses);
    
    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

    private:
    Table* table_;
    std::unique_ptr<Operator> child_;
    std::vector<std::pair<std::string, Value>> set_clauses_;
    std::vector<ColumnDefinition> dummy_schema_;

    int find_column_index(const std::string& col_name) const;
};