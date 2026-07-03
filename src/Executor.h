#pragma once
#include "Parser/Parser.h"
#include "Catalog.h"
#include "Table.h"
#include <string>
#include <vector>

struct ExecutionResult {
    bool success  = false;
    std::string message;
    std::vector<Row> rows;
    std::vector<std::string> columns;
};

class Executor {
public:
    explicit Executor(Catalog& catalog);

    ExecutionResult execute(const Statement& stmt);

private:
    Catalog& catalog_;

    ExecutionResult execute_select(const SelectStatement& stmt);
    ExecutionResult execute_insert(const InsertStatement& stmt);
    ExecutionResult execute_update(const UpdateStatement& stmt);
    ExecutionResult execute_delete(const DeleteStatement& stmt);
    ExecutionResult execute_create(const CreateTableStatement& stmt);
    ExecutionResult execute_create_index(const CreateIndexStatement& stmt);
    ExecutionResult execute_drop_table(const DropTableStatement& stmt);
    ExecutionResult execute_drop_index(const DropIndexStatement& stmt);
    ExecutionResult execute_alter_table(const AlterTableStatement& stmt);

    std::optional<uint32_t> try_extract_pk_from_where(
        const std::optional<WhereClause>& where,
        const std::vector<ColumnDefinition>& schema) const;
};