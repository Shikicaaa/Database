#pragma once
#include "Parser/Parser.h"
#include "Catalog.h"
#include "Table.h"
#include <string>
#include <vector>

//  success  - naredba uspela
//  message  - poruka greska ili uspeh, npr 3 tables affected
//  rows     - rezultati za SELECT
//  columns  - ako je select da znamo koja imena da vratimo
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


    bool matches_where(const Row& row,
                       const std::vector<ColumnDefinition>& schema,
                       const std::optional<WhereClause>& where) const;

    // ovo sluzi da optimizujemo pretragu umesto full scan mi trazimo po PK ako moze
    std::optional<uint32_t> try_extract_pk_from_where(
        const std::optional<WhereClause>& where,
        const std::vector<ColumnDefinition>& schema) const;

    int find_column_index(const std::string& col_name,
                          const std::vector<ColumnDefinition>& schema) const;

    bool compare_values(const Value& row_val,
                        const std::string& op,
                        const Value& where_val) const;
};