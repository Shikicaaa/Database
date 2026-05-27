#pragma once
#include "Operator.h"
#include "Parser/Parser.h"
#include <memory>

class FilterOperator : public Operator {
public:
    FilterOperator(std::unique_ptr<Operator> child, const std::optional<WhereClause>& where_clause);

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    std::unique_ptr<Operator> child_;
    std::optional<WhereClause> where_clause_;

    int find_column_index(const std::string& col_name, const std::vector<ColumnDefinition>& schema) const;
    
    bool compare_values(const Value& row_val, const std::string& op, const Value& where_val) const;
};