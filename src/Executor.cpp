#include "Executor.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "FilterOperator.h"
#include "ProjectOperator.h"
#include "SeqScanOperator.h"


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

    const auto& schema = table->get_columns();

    if (stmt.values.size() != schema.size()) {
        return {false,
            "Column count mismatch: table has " +
            std::to_string(schema.size()) + " columns, got " +
            std::to_string(stmt.values.size()) + " values"};
    }

    Row row(stmt.values.begin(), stmt.values.end());

    if (!table->insert_row(row)) {
        return {false, "Insert failed (duplicate key?)"};
    }

    return {true, "1 row inserted"};
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

    const auto& schema = table->get_columns();

    for (const auto& [col_name, _] : stmt.set_clauses) {
        if (find_column_index(col_name, schema) == -1) {
            return {false, "Column '" + col_name + "' does not exist"};
        }
    }

    std::vector<Row> rows_to_update;
    std::optional<uint32_t> pk_val = try_extract_pk_from_where(stmt.where_clause, schema);

    if (pk_val.has_value()) {
        auto found = table->find_row(pk_val.value());
        if (found.has_value() && matches_where(found.value(), schema, stmt.where_clause)) {
            rows_to_update.push_back(found.value());
        }
    } else {
        std::vector<Row> all_rows = table->scan_all();
        for (const auto& row : all_rows) {
            if (matches_where(row, schema, stmt.where_clause)) {
                rows_to_update.push_back(row);
            }
        }
    }

    int updated = 0;
    for (const auto& old_row : rows_to_update) {
        Row new_row = old_row;

        for (const auto& [col_name, new_val] : stmt.set_clauses) {
            int idx = find_column_index(col_name, schema);
            new_row[idx] = new_val;
        }

        uint32_t pk = table->extract_primary_key(old_row);

        if (table->update_row(pk, new_row)) {
            updated++;
        }
    }

    return {true, std::to_string(updated) + " row(s) updated"};
}

ExecutionResult Executor::execute_delete(const DeleteStatement& stmt)
{
    Table* table = catalog_.get_table(stmt.table_name);
    if (!table) {
        return {false, "Table '" + stmt.table_name + "' does not exist"};
    }

    const auto& schema = table->get_columns();

    std::vector<uint32_t> pks_to_delete;
    std::optional<uint32_t> pk_val = try_extract_pk_from_where(stmt.where_clause, schema);

    if (pk_val.has_value()) {
        auto found = table->find_row(pk_val.value());
        if (found.has_value() && matches_where(found.value(), schema, stmt.where_clause)) {
            pks_to_delete.push_back(pk_val.value());
        }
    } else {
        std::vector<Row> all_rows = table->scan_all();
        for (const auto& row : all_rows) {
            if (matches_where(row, schema, stmt.where_clause)) {
                pks_to_delete.push_back(table->extract_primary_key(row));
            }
        }
    }

    int deleted = 0;
    for (uint32_t pk : pks_to_delete) {
        if (table->remove_row(pk)) {
            deleted++;
        }
    }

    return {true, std::to_string(deleted) + " row(s) deleted"};
}

// Helperi

std::optional<uint32_t> Executor::try_extract_pk_from_where(
    const std::optional<WhereClause>& where,
    const std::vector<ColumnDefinition>& schema) const
{
    if (!where.has_value()) return std::nullopt;
    if (where->op != "=")  return std::nullopt;

    int idx = find_column_index(where->column, schema);
    if (idx == -1) return std::nullopt;
    if (!schema[idx].is_primary_key) return std::nullopt;

    if (const int32_t* v = std::get_if<int32_t>(&where->value)) {
        return static_cast<uint32_t>(*v);
    }

    return std::nullopt;
}

int Executor::find_column_index(const std::string& col_name,
                                const std::vector<ColumnDefinition>& schema) const
{
    for (int i = 0; i < (int)schema.size(); i++) {
        std::string a = col_name, b = schema[i].name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (a == b) return i;
    }
    return -1;
}

bool Executor::matches_where(const Row& row,
                             const std::vector<ColumnDefinition>& schema,
                             const std::optional<WhereClause>& where) const
{
    if (!where.has_value()) return true;

    int idx = find_column_index(where->column, schema);
    if (idx == -1 || idx >= (int)row.size()) return false;

    return compare_values(row[idx], where->op, where->value);
}

bool Executor::compare_values(const Value& row_val,
                              const std::string& op,
                              const Value& where_val) const
{
    auto cmp = [&op](const auto& a, const auto& b) -> bool {
        if (op == "=")  return a == b;
        if (op == "!=") return a != b;
        if (op == "<")  return a <  b;
        if (op == ">")  return a >  b;
        if (op == "<=") return a <= b;
        if (op == ">=") return a >= b;
        return false;
    };

    // int == int
    if (std::holds_alternative<int32_t>(row_val) &&
        std::holds_alternative<int32_t>(where_val)) {
        return cmp(std::get<int32_t>(row_val), std::get<int32_t>(where_val));
    }

    // double == double
    if (std::holds_alternative<double>(row_val) &&
        std::holds_alternative<double>(where_val)) {
        return cmp(std::get<double>(row_val), std::get<double>(where_val));
    }

    // int i doubleo onda konvertujemo int u double
    if (std::holds_alternative<int32_t>(row_val) &&
        std::holds_alternative<double>(where_val)) {
        return cmp((double)std::get<int32_t>(row_val), std::get<double>(where_val));
    }
    if (std::holds_alternative<double>(row_val) &&
        std::holds_alternative<int32_t>(where_val)) {
        return cmp(std::get<double>(row_val), (double)std::get<int32_t>(where_val));
    }

    // string == string
    if (std::holds_alternative<std::string>(row_val) &&
        std::holds_alternative<std::string>(where_val)) {
        return cmp(std::get<std::string>(row_val), std::get<std::string>(where_val));
    }

    // bool == bool
    if (std::holds_alternative<bool>(row_val) &&
        std::holds_alternative<bool>(where_val)) {
        return cmp(std::get<bool>(row_val), std::get<bool>(where_val));
    }

    // NULL vrednosti samo NULL = NULL je true, sve ostalo false
    if (std::holds_alternative<std::monostate>(row_val) &&
        std::holds_alternative<std::monostate>(where_val)) {
        return op == "=";
    }

    return false;
}