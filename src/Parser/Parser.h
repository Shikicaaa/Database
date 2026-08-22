#pragma once
#include "Lexer.h"
#include "../Serializer.h"
#include "../JoinTypes.h"
#include "../JoinOperator.h"

#include <variant>
#include <optional>
#include <vector>
#include <string>


struct OrderByClause {
    std::string column;
    bool ascending; // true for ASC, false for DESC
};

struct SelectItem {
    std::string column;
    std::string aggregate_function; // "" for no aggregate, "COUNT", "SUM", "AVG", "MIN", "MAX"
    bool is_star; // true only for count(*), false otherwise
    bool is_distinct;
    std::string alias;
};

struct JoinStatement
{
    JoinType type;
    std::string left_table;
    std::string right_table;
    std::string left_alias;
    std::string right_alias;
    JoinCondition condition;
};

struct WhereClause
{
    std::string table_qualifier;
    std::string column;
    std::string op;
    Value value;
};

struct SelectStatement {
    std::string table_name;
    std::string table_alias;
    std::vector<SelectItem> select_items;
    std::vector<JoinStatement> joins;
    std::optional<WhereClause> where_clause;
    std::optional<uint32_t> limit;
    std::optional<std::vector<OrderByClause>> order_by;
    std::optional<std::vector<std::string>> group_by;
};

struct InsertStatement
{
    std::string table_name;
    std::vector<Value> values;
};

struct UpdateStatement
{
    std::string table_name;
    std::vector<std::pair<std::string, Value>> set_clauses;
    std::optional<WhereClause> where_clause;
};

struct DeleteStatement
{
    std::string table_name;
    std::optional<WhereClause> where_clause;
};

struct CreateTableStatement
{
    std::string table_name;
    std::vector<ColumnDefinition> columns; // column name and type
};

struct CreateIndexStatement
{
    std::string index_name;
    std::string table_name;
    std::string column_name;
};

struct DropTableStatement {
    std::string table_name;
    bool if_exists = false;
};

struct DropIndexStatement {
    std::string index_name;
};

struct AlterTableStatement {
    enum class Action { ADD_COLUMN, DROP_COLUMN, RENAME_COLUMN } action;
    std::string table_name;
    ColumnDefinition new_col;
    std::string target_column;
    std::string new_column_name;
};

struct TableReference {
    std::string table_name;
    std::string alias;
};

struct BeginStatement    {};
struct CommitStatement   {};
struct RollbackStatement {};

using Statement = std::variant<
    SelectStatement,
    InsertStatement,
    UpdateStatement,
    DeleteStatement,
    CreateTableStatement,
    CreateIndexStatement,
    JoinStatement,
    DropTableStatement,
    DropIndexStatement,
    AlterTableStatement,
    BeginStatement,
    CommitStatement,
    RollbackStatement
>;


class Parser
{
private:
    std::vector<Token> tokeni;
    size_t pos = 0;

    const Token& peek() const;
    const Token& peek_next() const;
    const Token& advance();

    bool check(TokenType type) const;
    bool match(TokenType type);
    
    const Token& expect(TokenType type, const std::string& msg);

    Statement parse_statement();

    SelectStatement parse_select();
    InsertStatement parse_insert();
    UpdateStatement parse_update();
    DeleteStatement parse_delete();
    Statement parse_create();
    Statement parse_drop();
    Statement parse_alter();
    JoinStatement parse_join();
    WhereClause parse_where();
    Value parse_value();
    DataType parse_data_type();
    ColumnDefinition parse_column_def();
    std::string parse_operator();
    std::pair<std::string, std::string> parse_qualified_identifier(); // returns {table_alias, column_name}
    DateTime parse_date_literal(const std::string& str);
    SelectItem parse_select_item();
    std::vector<std::string> parse_group_by();

    BeginStatement    parse_begin();
    CommitStatement   parse_commit();
    RollbackStatement parse_rollback();
public:
    explicit Parser(std::vector<Token> tokeni) : tokeni(std::move(tokeni)) {}

    Statement parse();
};