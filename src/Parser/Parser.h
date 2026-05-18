#pragma once
#include "Lexer.h"
#include "../Serializer.h"

#include <variant>
#include <optional>
#include <vector>
#include <string>

struct WhereClause
{
    std::string column;
    std::string op;
    Value value;
};

struct SelectStatement
{
    std::string table_name;
    std::vector<std::string> columns;
    std::optional<WhereClause> where_clause;
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


using Statement = std::variant<
    SelectStatement,
    InsertStatement,
    UpdateStatement,
    DeleteStatement,
    CreateTableStatement
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
    CreateTableStatement parse_create_table();

    WhereClause parse_where();
    Value parse_value();
    DataType parse_data_type();
    std::string parse_operator();
public:
    explicit Parser(std::vector<Token> tokeni) : tokeni(std::move(tokeni)) {}

    Statement parse();
};