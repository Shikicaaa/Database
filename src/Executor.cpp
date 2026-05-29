#include "Executor.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "Planner.h"

Executor::Executor(Catalog& catalog) : catalog_(catalog) {}

ExecutionResult Executor::execute(const Statement& stmt)
{
    return std::visit([this](const auto& s) -> ExecutionResult {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, SelectStatement>)
            return execute_select(s);
        else if constexpr (std::is_same_v<T, InsertStatement>)
            return execute_insert(s);
        else if constexpr (std::is_same_v<T, UpdateStatement>)
            return execute_update(s);
        else if constexpr (std::is_same_v<T, DeleteStatement>)
            return execute_delete(s);
        else if constexpr (std::is_same_v<T, CreateTableStatement>)
            return execute_create(s);
        else
            return {false, "Unknown statement type"};
    }, stmt);
}

ExecutionResult Executor::execute_create(const CreateTableStatement& stmt)
{
    if (catalog_.table_exists(stmt.table_name)) {
        return {false, "Table '" + stmt.table_name + "' already exists", {}, {}};
    }

    bool ok = catalog_.create_table(stmt.table_name, stmt.columns);
    if (!ok) {
        return {false, "Failed to create table '" + stmt.table_name + "'", {}, {}};
    }

    return {true, "Table '" + stmt.table_name + "' created successfully", {}, {}};
}

ExecutionResult Executor::execute_insert(const InsertStatement& stmt)
{
    Planner planner(catalog_);
    auto plan = planner.create_plan(stmt);
    plan->Init();
    auto result = plan->Next();
    int count = std::get<int32_t>(result.value()[0]);
    return {true, std::to_string(count) + " row(s) inserted", {}, {}};
}

ExecutionResult Executor::execute_select(const SelectStatement& stmt)
{
    Planner planner(catalog_);

    auto plan = planner.create_plan(stmt);
    plan->Init();
    std::vector<Row> result_rows;
    std::vector<ColumnDefinition> result_cols = plan->GetOutputSchema();
    std::vector<std::string> result_col_names;
    for(const auto& col_def : result_cols){
        result_col_names.push_back(col_def.name);
    }
    auto row = plan->Next();
    while (row.has_value()) {
        result_rows.push_back(row.value());
        row = plan->Next();
    }

    return {true, std::to_string(result_rows.size()) + " row(s) returned", result_rows, result_col_names};
}

ExecutionResult Executor::execute_update(const UpdateStatement& stmt)
{
    Planner planner(catalog_);
    auto plan = planner.create_plan(stmt);
    plan->Init();
    auto result = plan->Next();
    int updated = std::get<int32_t>(result.value()[0]);

    return {true, std::to_string(updated) + " row(s) updated", {}, {}};
}

ExecutionResult Executor::execute_delete(const DeleteStatement& stmt)
{
    Planner planner(catalog_);
    auto plan = planner.create_plan(stmt);
    plan->Init();
    auto result = plan->Next();
    int deleted = std::get<int32_t>(result.value()[0]);

    return {true, std::to_string(deleted) + " row(s) deleted", {}, {}};
}
