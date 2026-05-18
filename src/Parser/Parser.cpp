#include "Parser.h"
#include <stdexcept>

const Token& Parser::peek() const
{
    return tokeni[pos];
}

const Token& Parser::peek_next() const
{
    if (pos+ 1 < tokeni.size()) return tokeni[pos+ 1];
    return tokeni.back();  // EOF
}

const Token& Parser::advance()
{
    const Token& t = tokeni[pos];
    if (pos + 1 < tokeni.size()) ++pos;
    return t;
}

bool Parser::check(TokenType t) const
{
    return peek().type == t;
}


bool Parser::match(TokenType t)
{
    if (check(t)) { advance(); return true; }
    return false;
}

const Token& Parser::expect(TokenType t, const std::string& msg)
{
    if (!check(t)) {
        throw std::runtime_error(
            "Syntax error at line " + std::to_string(peek().line) +
            ": " + msg +
            " (got '" + peek().value + "')");
    }
    return advance();
}


Statement Parser::parse()
{
    Statement stmt = parse_statement();

    // ; at the end of statement is optional
    match(TokenType::SEMICOLON);

    if (!check(TokenType::EOF_TOKEN)) {
        throw std::runtime_error(
            "Syntax error at line " + std::to_string(peek().line) +
            ": unexpected token '" + peek().value + "' after statement");
    }

    return stmt;
}

Statement Parser::parse_statement()
{
    switch (peek().type) {
        case TokenType::SELECT: return parse_select();
        case TokenType::INSERT: return parse_insert();
        case TokenType::UPDATE: return parse_update();
        case TokenType::DELETE: return parse_delete();
        case TokenType::CREATE: return parse_create_table();
        default:
            throw std::runtime_error(
                "Syntax error at line " + std::to_string(peek().line) +
                ": expected SELECT/INSERT/UPDATE/DELETE/CREATE, got '" +
                peek().value + "'");
    }
}

//  SELECT
//
//  Gramatika:
//    SELECT ( * | col [, col]* ) FROM identifier [WHERE condition]
//
//  Npr:
//    SELECT * FROM Employees
//    SELECT ID, Name FROM Employees WHERE ID = 42
SelectStatement Parser::parse_select()
{
    SelectStatement stmt;

    expect(TokenType::SELECT, "expected SELECT");

    if (match(TokenType::STAR)) {
    } else {
        stmt.columns.push_back(expect(TokenType::IDENTIFIER, "expected column name").value);
        while (match(TokenType::COMMA)) {
            stmt.columns.push_back(expect(TokenType::IDENTIFIER, "expected column name").value);
        }
    }

    expect(TokenType::FROM, "expected FROM");
    stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;

    if (check(TokenType::WHERE)) {
        stmt.where_clause = parse_where();
    }

    return stmt;
}

//  INSERT
//
//  Gramatika:
//    INSERT INTO identifier VALUES ( value [, value]* )
//
//  Npr:
//    INSERT INTO Employees VALUES (42, 'Andrija', 120000)
InsertStatement Parser::parse_insert()
{
    InsertStatement stmt;

    expect(TokenType::INSERT, "expected INSERT");
    expect(TokenType::INTO,   "expected INTO");

    stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;

    expect(TokenType::VALUES, "expected VALUES");
    expect(TokenType::LPAR, "expected '('");

    stmt.values.push_back(parse_value());
    while (match(TokenType::COMMA)) {
        stmt.values.push_back(parse_value());
    }

    expect(TokenType::RPAR, "expected ')'");

    return stmt;
}

//  UPDATE
//
//  Gramatika:
//    UPDATE identifier SET col = val [, col = val]* [WHERE condition]
//
//  Npr:
//    UPDATE Employees SET Salary = 130000 WHERE ID = 5
UpdateStatement Parser::parse_update()
{
    UpdateStatement stmt;

    expect(TokenType::UPDATE, "expected UPDATE");
    stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;
    expect(TokenType::SET, "expected SET");

    auto parse_assignment = [&]() {
        std::string col = expect(TokenType::IDENTIFIER, "expected column name").value;
        expect(TokenType::EQ, "expected '='");
        Value val = parse_value();
        stmt.set_clauses.emplace_back(col, val);
    };

    parse_assignment();
    while (match(TokenType::COMMA)) {
        parse_assignment();
    }

    if (check(TokenType::WHERE)) {
        stmt.where_clause = parse_where();
    }

    return stmt;
}

//  DELETE
//
//  Gramatika:
//    DELETE FROM identifier [WHERE condition]
//
//  Npr:
//    DELETE FROM Employees WHERE ID = 13
DeleteStatement Parser::parse_delete()
{
    DeleteStatement stmt;

    expect(TokenType::DELETE, "expected DELETE");
    expect(TokenType::FROM,   "expected FROM");
    stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;

    if (check(TokenType::WHERE)) {
        stmt.where_clause = parse_where();
    }

    return stmt;
}

//  CREATE TABLE
//
//  Gramatika:
//    CREATE TABLE identifier ( col_def [, col_def]* )
//
//  col_def:
//    identifier type [PRIMARY KEY] [UNIQUE] [NULLABLE]
//
//  type:
//    INT | NUMBER | BOOLEAN | DATE | VARCHAR ( number )
//
//  Npr:
//    CREATE TABLE Employees (
//        ID     INT     PRIMARY KEY,
//        Name   VARCHAR(255),
//        Salary NUMBER  NULLABLE
//    )
CreateTableStatement Parser::parse_create_table()
{
    CreateTableStatement stmt;

    expect(TokenType::CREATE, "expected CREATE");
    expect(TokenType::TABLE,  "expected TABLE");

    stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;
    expect(TokenType::LPAR, "expected '('");

    // Ovde se cita definicija kolone do )
    auto parse_column_def = [&]() {
        ColumnDefinition col;

        col.name = expect(TokenType::IDENTIFIER, "expected column name").value;
        col.type = parse_data_type();

        if (col.type == DataType::VARCHAR) {
            if (match(TokenType::LPAR)) {
                const Token& len_tok = expect(TokenType::NUMBER_LITERAL, "expected max length");
                col.max_length = static_cast<uint16_t>(std::stoi(len_tok.value));
                expect(TokenType::RPAR, "expected ')'");
            } else {
                col.max_length = 255; // default
            }
        }

        col.is_primary_key = false;
        col.is_nullable    = false;
        col.is_unique      = false;

        bool parsing_modifiers = true;
        while (parsing_modifiers) {
            if (check(TokenType::PRIMARY)) {
                advance(); // PRIMARY
                expect(TokenType::KEY, "expected KEY after PRIMARY");
                col.is_primary_key = true;
            } else if (match(TokenType::UNIQUE)) {
                col.is_unique = true;
            } else if (match(TokenType::NULLABLE)) {
                col.is_nullable = true;
            } else {
                parsing_modifiers = false;
            }
        }

        stmt.columns.push_back(col);
    };

    parse_column_def();
    while (match(TokenType::COMMA)) {
        if (check(TokenType::RPAR)) break;
        parse_column_def();
    }

    expect(TokenType::RPAR, "expected ')'");

    return stmt;
}

//  WHERE
//
//  Gramatika:
//    WHERE identifier operator value
//
//  Operator: = | != | <> | < | > | <= | >=
WhereClause Parser::parse_where()
{
    WhereClause wc;

    expect(TokenType::WHERE, "expected WHERE");
    wc.column = expect(TokenType::IDENTIFIER, "expected column name").value;
    wc.op     = parse_operator();
    wc.value  = parse_value();

    return wc;
}

//   std::variant<std::monostate, int32_t, double, std::string, bool, DateUnix>
Value Parser::parse_value()
{
    const Token& t = peek();

    if (t.type == TokenType::NUMBER_LITERAL) {
        advance();
        // Ako ima br ima . onda je NUMBER inace je INTEGER
        if (t.value.find('.') != std::string::npos) {
            return Value(std::stod(t.value));
        } else {
            return Value(static_cast<int32_t>(std::stoi(t.value)));
        }
    }

    if (t.type == TokenType::STRING_LITERAL) {
        advance();
        return Value(t.value);
    }

    if (t.type == TokenType::BOOL_LITERAL) {
        advance();
        return Value(t.value == "TRUE");
    }

    if (t.type == TokenType::NULL_KW) {
        advance();
        return Value(std::monostate{});
    }

    throw std::runtime_error(
        "Syntax error at line " + std::to_string(t.line) +
        ": expected a value (number, string, TRUE/FALSE/NULL), got '" + t.value + "'");
}

DataType Parser::parse_data_type()
{
    const Token& t = peek();

    switch (t.type) {
        case TokenType::INT_KW:      advance(); return DataType::INT;
        case TokenType::NUMBER_KW:   advance(); return DataType::NUMBER;
        case TokenType::VARCHAR:     advance(); return DataType::VARCHAR;
        case TokenType::BOOLEAN_KW:  advance(); return DataType::BOOLEAN;
        case TokenType::DATE_KW:     advance(); return DataType::DATE;
        default:
            throw std::runtime_error(
                "Syntax error at line " + std::to_string(t.line) +
                ": expected data type (INT/NUMBER/VARCHAR/BOOLEAN/DATE), got '" +
                t.value + "'");
    }
}

std::string Parser::parse_operator()
{
    const Token& t = peek();

    switch (t.type) {
        case TokenType::EQ:  advance(); return "=";
        case TokenType::NEQ: advance(); return "!=";
        case TokenType::LT:  advance(); return "<";
        case TokenType::GT:  advance(); return ">";
        case TokenType::LTE: advance(); return "<=";
        case TokenType::GTE: advance(); return ">=";
        default:
            throw std::runtime_error(
                "Syntax error at line " + std::to_string(t.line) +
                ": expected comparison operator, got '" + t.value + "'");
    }
}