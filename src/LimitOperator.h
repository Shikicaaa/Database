#ifndef LIMIT_OPERATOR_H
#define LIMIT_OPERATOR_H

#include <iostream>
#include <memory>
#include "Operator.h"

class LimitOperator : public Operator {
protected:
    uint32_t limit_;
    uint32_t count_;

public:
    LimitOperator(uint32_t limit, std::unique_ptr<Operator> child);
    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override {
        return child_->GetOutputSchema();
    }

private:
    std::unique_ptr<Operator> child_;
};

#endif