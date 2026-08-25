#pragma once
#include "Operator.h"
#include "Parser/Parser.h"
#include "Catalog.h"
#include <memory>

class FilterOperator : public Operator {
public:
    FilterOperator(std::unique_ptr<Operator> child,
                   const std::optional<WhereClause>& where_clause,
                   Catalog* catalog = nullptr,
                   const std::string& outer_alias = "");

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    Catalog* catalog_ = nullptr;
    std::string outer_alias_;

    std::unique_ptr<Operator> child_;
    std::optional<WhereClause> where_clause_;

    int find_column_index(const std::string& col_name, const std::vector<ColumnDefinition>& schema) const;
    bool compare_values(const Value& row_val, const std::string& op, const Value& where_val) const;

    bool evaluate(const Condition& cond, const Row& row, const std::vector<ColumnDefinition>& schema) const;

    bool evaluate_correlated_subquery(const Condition& cond, const Row& outer_row, const std::vector<ColumnDefinition>& outer_schema) const;

    Value resolve_outer_value(const std::string& qualifier, const std::string& column, const Row& outer_row, const std::vector<ColumnDefinition>& outer_schema) const;

    std::shared_ptr<Condition> substitute_outer_refs(const std::shared_ptr<Condition>& cond, const Row& outer_row, const std::vector<ColumnDefinition>& outer_schema) const;
};