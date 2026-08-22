#ifndef ORDER_OPERATOR_H
#define ORDER_OPERATOR_H

#include <iostream>
#include <memory>
#include "Parser/Parser.h"
#include "Operator.h"

class OrderOperator : public Operator {
protected:
    std::unique_ptr<Operator> child_;
    std::vector<OrderByClause> order_by_;
    std::vector<Row> sorted_rows_;
    size_t current_index_ = 0;

    int find_column_index(const std::string& col_name, const std::vector<ColumnDefinition>& schema) const;

    bool value_less_than(const Value& a, const Value& b) const;

public:
    OrderOperator(std::unique_ptr<Operator> child, const std::vector<OrderByClause>& order_by)
        : child_(std::move(child)), order_by_(order_by) {}

    void Init() override;

    std::optional<Row> Next() override;

    const std::vector<ColumnDefinition>& GetOutputSchema() const override {
        return child_->GetOutputSchema();
    }
};

#endif