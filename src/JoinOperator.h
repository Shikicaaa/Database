#ifndef JOIN_OPERATOR_H
#define JOIN_OPERATOR_H

#include <iostream>
#include <memory>
#include "Operator.h"
#include "JoinTypes.h"

class JoinOperator : public Operator {
public:
    JoinOperator(
        std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
        const JoinCondition& condition,
        const std::string& left_alias  = "",
        const std::string& right_alias = ""
    );

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

protected:
    std::unique_ptr<Operator> left_;
    std::unique_ptr<Operator> right_;
    JoinCondition condition_;
    std::string left_alias_;
    std::string right_alias_;
    std::vector<ColumnDefinition> output_schema_;

    Row merge_rows(const Row& left_row, const Row& right_row) const;
    bool evaluate_condition(const Row& left_row, const Row& right_row) const;
    void build_output_schema();

private:
    std::optional<Row> left_current_row_;
    bool right_initialized_;
    std::vector<Row> right_buffer_;
    size_t right_pos_;
};

#endif // JOIN_OPERATOR_H