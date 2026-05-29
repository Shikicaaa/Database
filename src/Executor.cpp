#include "Executor.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "FilterOperator.h"
#include "ProjectOperator.h"
#include "SeqScanOperator.h"
#include "ValuesOperator.h"
#include "InsertOperator.h"
#include "DeleteOperator.h"
#include "UpdateOperator.h"


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
        return {false, "Table '" + stmt.table_name + "' already exists"};
    }

    bool ok = catalog_.create_table(stmt.table_name, stmt.columns);
    if (!ok) {
        return {false, "Failed to create table '" + stmt.table_name + "'"};
    }

    return {true, "Table '" + stmt.table_name + "' created successfully"};
}

ExecutionResult Executor::execute_insert(const InsertStatement& stmt)
{
    Table* table = catalog_.get_table(stmt.table_name);
    if (!table) {
        return {false, "Table '" + stmt.table_name + "' does not exist"};
    }

    std::vector<Row> raw_data = {stmt.values};

    std::unique_ptr<Operator> plan = std::make_unique<ValuesOperator>(raw_data, table->get_columns());
    std::unique_ptr<Operator> insert_plan = std::make_unique<InsertOperator>(table, std::move(plan));

    insert_plan->Init();
    auto result = insert_plan->Next();

    int count = std::get<int32_t>(result.value()[0]);
    return {true, std::to_string(count) + " row(s) inserted"};

}

ExecutionResult Executor::execute_select(const SelectStatement& stmt)
{
    Table* table = catalog_.get_table(stmt.table_name);
    if (!table) {
        return {false, "Table '" + stmt.table_name + "' does not exist"};
    }

    std::unique_ptr<Operator> plan = std::make_unique<SeqScanOperator>(table);

    plan = std::make_unique<FilterOperator>(std::move(plan), stmt.where_clause);
    plan = std::make_unique<ProjectOperator>(std::move(plan), stmt.columns);

    std::vector<Row> result_rows;
    std::vector<std::string> result_col_names;

    plan->Init();

    for(const auto& col : plan->GetOutputSchema()){
        result_col_names.push_back(col.name);
    }

    while(auto row = plan->Next()){
        result_rows.push_back(*row);
    }

    return {true, std::to_string(result_rows.size()) + " row(s) returned", result_rows, result_col_names};
}

ExecutionResult Executor::execute_update(const UpdateStatement& stmt)
{
    Table* table = catalog_.get_table(stmt.table_name);
    if (!table) {
        return {false, "Table '" + stmt.table_name + "' does not exist"};
    }

    std::unique_ptr<Operator> scan = std::make_unique<SeqScanOperator>(table);
    std::unique_ptr<Operator> filter = std::make_unique<FilterOperator>(std::move(scan), stmt.where_clause);
    std::unique_ptr<Operator> update_op = std::make_unique<UpdateOperator>(std::move(filter), table, stmt.set_clauses);

    update_op->Init();
    auto result = update_op->Next();
    int updated = std::get<int32_t>(result.value()[0]);

    return {true, std::to_string(updated) + " row(s) updated"};
}

ExecutionResult Executor::execute_delete(const DeleteStatement& stmt)
{
    Table* table = catalog_.get_table(stmt.table_name);
    if (!table) {
        return {false, "Table '" + stmt.table_name + "' does not exist"};
    }

    std::unique_ptr<Operator> scan = std::make_unique<SeqScanOperator>(table);
    std::unique_ptr<Operator> filter = std::make_unique<FilterOperator>(std::move(scan), stmt.where_clause);
    std::unique_ptr<Operator> del_op = std::make_unique<DeleteOperator>(std::move(filter), table);

    del_op->Init();
    auto result = del_op->Next();
    int count = std::get<int32_t>(result.value()[0]);

    return {true, std::to_string(count) + " row(s) deleted"};
}

// Helperi

std::optional<uint32_t> Executor::try_extract_pk_from_where(
    const std::optional<WhereClause>& where,
    const std::vector<ColumnDefinition>& schema) const
{
    if (!where.has_value()) return std::nullopt;
    if (where->op != "=")  return std::nullopt;

    int idx = -1;
    for (int i = 0; i < (int)schema.size(); i++) {
        std::string a = where->column, b = schema[i].name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (a == b) { idx = i; break; }
    }

    if (idx == -1) return std::nullopt;
    if (!schema[idx].is_primary_key) return std::nullopt;

    if (const int32_t* v = std::get_if<int32_t>(&where->value)) {
        return static_cast<uint32_t>(*v);
    }

    return std::nullopt;
}