#include "Executor.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "Planner.h"

Executor::Executor(Catalog& catalog) : catalog_(catalog) {}

ExecutionResult Executor::execute(const Statement& stmt)
{
    Catalog::ReadLock  rl;
    Catalog::WriteLock wl;
    if (std::holds_alternative<SelectStatement>(stmt))
        rl = catalog_.acquire_read();
    else
        wl = catalog_.acquire_write();

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
        else if constexpr (std::is_same_v<T, CreateIndexStatement>)
            return execute_create_index(s);
        else if constexpr (std::is_same_v<T, DropTableStatement>)
            return execute_drop_table(s);
        else if constexpr (std::is_same_v<T, DropIndexStatement>)
            return execute_drop_index(s);
        else if constexpr (std::is_same_v<T, AlterTableStatement>)
            return execute_alter_table(s);
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

ExecutionResult Executor::execute_create_index(const CreateIndexStatement& stmt)
{
    bool ok = catalog_.create_secondary_index(stmt.index_name, stmt.table_name, stmt.column_name);
    if (!ok) {
        return {false, "Failed to create index '" + stmt.index_name + "'", {}, {}};
    }
    return {true, "Index '" + stmt.index_name + "' created on " + stmt.table_name + "(" + stmt.column_name + ")", {}, {}};
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

ExecutionResult Executor::execute_drop_table(const DropTableStatement& stmt)
{
    if (!catalog_.table_exists(stmt.table_name)) {
        if (stmt.if_exists)
            return {true, "Table '" + stmt.table_name + "' does not exist, skipped", {}, {}};
        return {false, "Table '" + stmt.table_name + "' does not exist", {}, {}};
    }
    if (!catalog_.drop_table(stmt.table_name))
        return {false, "Cannot drop table '" + stmt.table_name + "' (FK references exist)", {}, {}};
    return {true, "Table '" + stmt.table_name + "' dropped", {}, {}};
}

ExecutionResult Executor::execute_drop_index(const DropIndexStatement& stmt)
{
    if (!catalog_.drop_index(stmt.index_name))
        return {false, "Index '" + stmt.index_name + "' does not exist or could not be dropped", {}, {}};
    return {true, "Index '" + stmt.index_name + "' dropped", {}, {}};
}

ExecutionResult Executor::execute_alter_table(const AlterTableStatement& stmt)
{
    using Action = AlterTableStatement::Action;
    bool ok = false;
    std::string msg;
    switch (stmt.action) {
        case Action::ADD_COLUMN:
            ok  = catalog_.alter_table_add_column(stmt.table_name, stmt.new_col);
            msg = ok ? "Column '" + stmt.new_col.name + "' added to '" + stmt.table_name + "'"
                     : "Failed to add column";
            break;
        case Action::DROP_COLUMN:
            ok  = catalog_.alter_table_drop_column(stmt.table_name, stmt.target_column);
            msg = ok ? "Column '" + stmt.target_column + "' dropped from '" + stmt.table_name + "'"
                     : "Failed to drop column (PK, FK reference, or not found)";
            break;
        case Action::RENAME_COLUMN:
            ok  = catalog_.alter_table_rename_column(stmt.table_name, stmt.target_column, stmt.new_column_name);
            msg = ok ? "Column '" + stmt.target_column + "' renamed to '" + stmt.new_column_name + "'"
                     : "Failed to rename column (not found or name conflict)";
            break;
    }
    return {ok, msg, {}, {}};
}
