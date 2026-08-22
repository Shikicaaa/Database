#ifndef GROUP_BY_OPERATOR_H
#define GROUP_BY_OPERATOR_H

#include <algorithm>
#include <memory>
#include <map>
#include <limits>
#include "Parser/Parser.h"
#include "Operator.h"

class GroupByOperator : public Operator {
protected:
    std::unique_ptr<Operator> child_;
    std::vector<std::string> group_by_columns_;
    std::vector<SelectItem> select_items_;
    std::optional<HavingClause> having_clause_;

    std::vector<Row> result_rows_;
    std::vector<ColumnDefinition> output_schema_;
    size_t current_index_ = 0;

    int find_column_index(const std::string& col_name, const std::vector<ColumnDefinition>& schema) const;
    void build_output_schema(const std::vector<ColumnDefinition>& child_schema);


    struct AggregateState {
        int32_t count = 0;
        double sum = 0.0;
        bool has_min_max = false;
        Value min_value;
        Value max_value;
    };

    bool value_less_than(const Value& a, const Value& b) const;
    void update_agg_state(AggregateState& state, const Value& value) const;
    Value finalize_agg(const SelectItem& item, const AggregateState& state, DataType source_type) const;
    bool compare_having(const Value& row_val, const std::string& op, const Value& target) const;

public:
    GroupByOperator(std::unique_ptr<Operator> child, const std::vector<std::string>& group_by_columns, const std::vector<SelectItem>& select_items, const std::optional<HavingClause>& having_clause = std::nullopt)
    : child_(std::move(child)), group_by_columns_(group_by_columns), select_items_(select_items), having_clause_(having_clause) {}

    void Init() override;
    std::optional<Row> Next() override;

    const std::vector<ColumnDefinition>& GetOutputSchema() const override {
        return output_schema_;
    }

};

#endif