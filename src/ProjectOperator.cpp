#include "ProjectOperator.h"
#include <algorithm>

ProjectOperator::ProjectOperator(std::unique_ptr<Operator> child, const std::vector<std::string>& selected_columns)
    : child_(std::move(child)), selected_columns_(selected_columns) 
{
    build_schema_and_mapping();
}

void ProjectOperator::Init() {
    child_->Init();
}

void ProjectOperator::build_schema_and_mapping() {
    const auto& child_schema = child_->GetOutputSchema();
    
    if(selected_columns_.empty()){
        output_schema_ = child_schema;
        column_mapping_.resize(child_schema.size());
        for(int i = 0; i < (int)child_schema.size(); i++){
            column_mapping_[i] = i;
        }
        return;
    }

    for(const auto& col_name : selected_columns_){
        auto it = std::find_if(child_schema.begin(), child_schema.end(),
            [&](const ColumnDefinition& col_def){
                return col_def.name == col_name;
            });

        if(it == child_schema.end()){
            it = std::find_if(child_schema.begin(), child_schema.end(),
                [&](const ColumnDefinition& col_def){
                    const std::string& n = col_def.name;
                    auto dot = n.rfind('.');
                    std::string bare = (dot == std::string::npos) ? n : n.substr(dot + 1);
                    auto cdot = col_name.rfind('.');
                    std::string col_bare = (cdot == std::string::npos) ? col_name : col_name.substr(cdot + 1);
                    return bare == col_bare && (cdot == std::string::npos || col_def.name == col_name);
                });
        }

        if(it == child_schema.end()){
            throw std::runtime_error("Column '" + col_name + "' not found in child schema");
        }

        ColumnDefinition out_col = *it;
        auto dot = col_name.rfind('.');
        if (dot != std::string::npos) {
            out_col.name = col_name.substr(dot + 1);
        }
        output_schema_.push_back(out_col);
        column_mapping_.push_back(std::distance(child_schema.begin(), it));
    }
}

const std::vector<ColumnDefinition>& ProjectOperator::GetOutputSchema() const {
    return output_schema_;
}

std::optional<Row> ProjectOperator::Next() {
    auto row = child_->Next();
    if (!row) return std::nullopt;
    Row projected_row;
    for(int idx : column_mapping_){
        projected_row.push_back(row.value()[idx]);
    }
    return projected_row;
}