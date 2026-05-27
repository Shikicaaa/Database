#pragma once
#include "Operator.h"
#include <memory>
#include <string>
#include <vector>

class ProjectOperator : public Operator {
public:
    ProjectOperator(std::unique_ptr<Operator> child, const std::vector<std::string>& selected_columns);

    void Init() override;
    std::optional<Row> Next() override;
    const std::vector<ColumnDefinition>& GetOutputSchema() const override;

private:
    std::unique_ptr<Operator> child_;
    std::vector<std::string> selected_columns_;

    std::vector<ColumnDefinition> output_schema_;
    
    std::vector<int> column_mapping_;

    void build_schema_and_mapping();
};