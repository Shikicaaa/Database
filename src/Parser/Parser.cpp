#include "Parser.h"
#include <stdexcept>
#include <ctime>
 
const Token& Parser::peek() const
{
    return tokeni[pos];
}

SelectItem Parser::parse_select_item()
{
    SelectItem item = {"", "", false, false, ""};

    switch (peek().type) {
        case TokenType::COUNT:
        case TokenType::SUM:
        case TokenType::AVG:
        case TokenType::MIN:
        case TokenType::MAX: {
            item.aggregate_function = advance().value;
            expect(TokenType::LPAR, "expected '(' after aggregate function");
            if (match(TokenType::STAR)) {
                if (item.aggregate_function != "COUNT") {
                    throw std::runtime_error("'*' can only be used with COUNT, not " + item.aggregate_function);
                }
                item.is_star = true;
                item.column = "";
            } else {
                auto [table_alias, column_name] = parse_qualified_identifier();
                item.column = table_alias.empty() ? column_name : table_alias + "." + column_name;
            }
            expect(TokenType::RPAR, "expected ')' after aggregate function");
            break;
        }
        default: {
            auto [table_alias, column_name] = parse_qualified_identifier();
            item.column = table_alias.empty() ? column_name : table_alias + "." + column_name;
            break;
        }
    }
    if (check(TokenType::AS)) {
        advance(); // consume AS
        item.alias = expect(TokenType::IDENTIFIER, "expected alias after AS").value;
    }
    return item;
}

std::vector<std::string> Parser::parse_group_by()
{
    std::vector<std::string> cols;
    auto [q, c] = parse_qualified_identifier();
    cols.push_back(q.empty() ? c : q + "." + c);
    while (match(TokenType::COMMA)) {
        auto [q2, c2] = parse_qualified_identifier();
        cols.push_back(q2.empty() ? c2 : q2 + "." + c2);
    }
    return cols;
}


// (EXPRESSION) > NOT > AND > OR
std::shared_ptr<Condition> Parser::parse_or_expr() {
    auto left = parse_and_expr();
    while (check(TokenType::OR)) {
        advance();
        auto right = parse_and_expr();
        auto node = std::make_shared<Condition>();
        node->type = ConditionType::OR;
        node->children.push_back(left);
        node->children.push_back(right);
        left = node;
    }
    return left;
}

std::shared_ptr<Condition> Parser::parse_and_expr() {
    auto left = parse_not_expr();
    while (check(TokenType::AND)) {
        advance();
        auto right = parse_not_expr();
        auto node = std::make_shared<Condition>();
        node->type = ConditionType::AND;
        node->children.push_back(left);
        node->children.push_back(right);
        left = node;
    }
    return left;
}

std::shared_ptr<Condition> Parser::parse_not_expr() {
    if (match(TokenType::NOT)) {
        auto child = parse_not_expr();
        auto node = std::make_shared<Condition>();
        node->type = ConditionType::NOT;
        node->children.push_back(child);
        return node;
    }
    return parse_primary_condition();
}

std::shared_ptr<Condition> Parser::parse_primary_condition() {
    if (match(TokenType::LPAR)) {
        auto inner = parse_or_expr();
        expect(TokenType::RPAR, "expected ')' after expression");
        return inner;
    }

    if (match(TokenType::EXISTS)) {
        expect(TokenType::LPAR, "expected '(' after EXISTS");
        auto node = std::make_shared<Condition>();
        node->type = ConditionType::COMPARISON;
        node->op = "EXISTS";
        node->subquery = std::make_shared<SelectStatement>(parse_select());
        expect(TokenType::RPAR, "expected ')' after subquery");
        return node;
    }

    auto node = std::make_shared<Condition>();
    node->type = ConditionType::COMPARISON;

    auto [qualifier, column] = parse_qualified_identifier();
    node->table_qualifier = qualifier;
    node->column = column;

    if (check(TokenType::IS)) {
        advance();
        if (check(TokenType::NOT)) {
            advance();
            expect(TokenType::NULL_KW, "expected NULL after IS NOT");
            node->op = "IS NOT NULL";
        } else {
            expect(TokenType::NULL_KW, "expected NULL after IS");
            node->op = "IS NULL";
        }
        node->value = std::monostate{};
        return node;
    }
    if (check(TokenType::LIKE) || check(TokenType::ILIKE)) {
        node->op = (peek().type == TokenType::LIKE) ? "LIKE" : "ILIKE";
        advance();
        node->value = parse_value();
        return node;
    }

    if (check(TokenType::NOT) && peek_next().type == TokenType::IN) {
        advance(); // consume NOT
        advance(); // consume IN
        node->op = "NOT IN";
        expect(TokenType::LPAR, "expected '(' after NOT IN");
        node->subquery = std::make_shared<SelectStatement>(parse_select());
        expect(TokenType::RPAR, "expected ')' after subquery");
        return node;
    }

    if (check(TokenType::IN)) {
        advance();
        node->op = "IN";
        expect(TokenType::LPAR, "expected '(' after IN");
        node->subquery = std::make_shared<SelectStatement>(parse_select());
        expect(TokenType::RPAR, "expected ')' after subquery");
        return node;
    }

    node->op = parse_operator();
    if (check(TokenType::LPAR) && peek_next().type == TokenType::SELECT) {
        advance(); // consume '('
        node->subquery = std::make_shared<SelectStatement>(parse_select());
        expect(TokenType::RPAR, "expected ')' after subquery");
    } else if (check(TokenType::IDENTIFIER)) {
        auto [q, c] = parse_qualified_identifier();
        node->rhs_is_column = true;
        node->rhs_table_qualifier = q;
        node->rhs_column = c;
        }else {
        node->value = parse_value();
    }
    return node;
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
        case TokenType::CREATE: return parse_create();
        case TokenType::DROP:   return parse_drop();
        case TokenType::ALTER:    return parse_alter();
        case TokenType::JOIN:     return parse_join();
        case TokenType::BEGIN:    return parse_begin();
        case TokenType::COMMIT:   return parse_commit();
        case TokenType::ROLLBACK: return parse_rollback();
        default:
            throw std::runtime_error(
                "Syntax error at line " + std::to_string(peek().line) +
                ": expected statement keyword, got '" +
                peek().value + "'");
    }
}

inline int fast_parse_int(std::string_view& s, char sep1, char sep2 = '\0') {
    int val = 0;
    size_t i = 0;
    
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        val = val * 10 + (s[i] - '0');
        i++;
    }
    
    if (i < s.size() && (s[i] == sep1 || s[i] == sep2)) {
        s.remove_prefix(i + 1);
    } else {
        s.remove_prefix(i);
    }
    
    return val;
}
 
DateTime Parser::parse_date_literal(std::string_view s)
{
    return DateUtils::parse_date(s);
}
 
std::pair<std::string, std::string> Parser::parse_qualified_identifier()
{
    std::string first = expect(TokenType::IDENTIFIER, "expected identifier").value;
    if (match(TokenType::DOT)) {
        std::string second = expect(TokenType::IDENTIFIER, "expected identifier after '.'").value;
        return {first, second};
    }
    return {"", first}; // No table alias
}
 
JoinStatement Parser::parse_join()
{
    JoinStatement stmt;
 
    // [LEFT|RIGHT|FULL [OUTER]] JOIN  or JOIN (= INNER)
    if (match(TokenType::LEFT)) {
        match(TokenType::OUTER);
        stmt.type = JoinType::LEFT;
        expect(TokenType::JOIN, "expected JOIN after LEFT");
    } else if (match(TokenType::RIGHT)) {
        match(TokenType::OUTER);
        stmt.type = JoinType::RIGHT;
        expect(TokenType::JOIN, "expected JOIN after RIGHT");
    } else if (match(TokenType::FULL)) {
        match(TokenType::OUTER);
        stmt.type = JoinType::FULL_OUTER;
        expect(TokenType::JOIN, "expected JOIN after FULL");
    } else {
        expect(TokenType::JOIN, "expected JOIN");
        stmt.type = JoinType::INNER;
    }
 
    stmt.right_table = expect(TokenType::IDENTIFIER, "expected right table name").value;
 
    if (check(TokenType::IDENTIFIER)) {
        stmt.right_alias = advance().value;
    }
 
    expect(TokenType::ON, "expected ON");
 
    auto [lq, lc] = parse_qualified_identifier();
    stmt.condition.left_column = lq.empty() ? lc : lq + "." + lc; // if no qualifier, use column name as is
    stmt.condition.op              = parse_operator();
    auto [rq, rc] = parse_qualified_identifier();
    stmt.condition.right_column = rq.empty() ? rc : rq + "." + rc; // if no qualifier, use column name as is
 
    return stmt;
}

HavingClause Parser::parse_having()
{
    HavingClause clause;
    clause.item = parse_select_item();
    clause.op = parse_operator();
    clause.value = parse_value();
    return clause;
}
 
//  SELECT
//
//  Gramatika:
//    SELECT ( * | col [, col]* ) FROM identifier [WHERE condition]
//
//  Npr:
//    SELECT * FROM Employees
//    SELECT ID, Name FROM Employees WHERE ID = 42
// Ili
/*
    SELECT * FROM Employees e
    JOIN Departments d ON e.DepartmentID = d.ID
    WHERE e.Salary > 50000
*/
SelectStatement Parser::parse_select()
{
    SelectStatement stmt;
 
    expect(TokenType::SELECT, "expected SELECT");
 
    if (match(TokenType::STAR)) {
    } else {
        stmt.select_items.push_back(parse_select_item());
        while (match(TokenType::COMMA)) {
            stmt.select_items.push_back(parse_select_item());
        }
    }

    expect(TokenType::FROM, "expected FROM");
    
    stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;
    
    if(check(TokenType::IDENTIFIER))
    {
        stmt.table_alias = expect(TokenType::IDENTIFIER, "expected table alias").value;
    }
    while(check(TokenType::JOIN) || check(TokenType::LEFT) || check(TokenType::RIGHT) || check(TokenType::FULL))
    {
        stmt.joins.push_back(parse_join());
    }
 
    if (check(TokenType::WHERE)) {
        stmt.where_clause = parse_where();
    }
    
    if (check(TokenType::GROUP)) {
        advance(); // consume GROUP
        expect(TokenType::BY, "expected BY after GROUP");
        stmt.group_by = parse_group_by();

        if (check(TokenType::HAVING)) {
            advance();
            stmt.having_clause = parse_having();
        }
    }

    if (check(TokenType::ORDER)) {
        advance(); // consume ORDER
        expect(TokenType::BY, "expected BY after ORDER");
        OrderByClause obc;
        auto [q, c] = parse_qualified_identifier();
        obc.column = q.empty() ? c : q + "." + c;
        if (match(TokenType::ASC)) {
            obc.ascending = true;
        } else if (match(TokenType::DESC)) {
            obc.ascending = false;
        } else {
            obc.ascending = true;
        }
        stmt.order_by = std::vector<OrderByClause>{obc};
    }

    if (match(TokenType::LIMIT)) {
        stmt.limit = std::stoul(expect(TokenType::NUMBER_LITERAL, "expected number after LIMIT").value);
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
/*
    CREATE TABLE Orders (
        ID     INT PRIMARY KEY,
        UserID INT REFERENCES Users(ID),
        Total  NUMBER NULLABLE
    );
 
*/
Statement Parser::parse_create()
{
    expect(TokenType::CREATE, "expected CREATE");
 
    if (check(TokenType::INDEX)) {
        advance(); // consume INDEX
        CreateIndexStatement idx_stmt;
        idx_stmt.index_name = expect(TokenType::IDENTIFIER, "expected index name").value;
        expect(TokenType::ON, "expected ON");
        idx_stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;
        expect(TokenType::LPAR, "expected '('");
        idx_stmt.column_name = expect(TokenType::IDENTIFIER, "expected column name").value;
        expect(TokenType::RPAR, "expected ')'");
        return idx_stmt;
    }
 
    expect(TokenType::TABLE, "expected TABLE or INDEX after CREATE");
 
    CreateTableStatement stmt;
 
    stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;
    expect(TokenType::LPAR, "expected '('");
 
    stmt.columns.push_back(parse_column_def());
    while (match(TokenType::COMMA)) {
        if (check(TokenType::RPAR)) break;
        stmt.columns.push_back(parse_column_def());
    }
 
    expect(TokenType::RPAR, "expected ')'");
 
    return stmt;
}
 
//  WHERE
//
//  Gramatika:
//    WHERE identifier operator value
//
//  Operator: = | != | <> | < | > | <= | >= | AND | OR | NOT | IS NULL | IS NOT NULL | LIKE | ILIKE
WhereClause Parser::parse_where()
{
    expect(TokenType::WHERE, "expected WHERE");
    return parse_or_expr();
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
 
    if (t.type == TokenType::DATE_KW) {
        advance();
        const Token& date_str = expect(TokenType::STRING_LITERAL, "expected date literal as string");
        return Value(parse_date_literal(date_str.value));
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
 
ColumnDefinition Parser::parse_column_def()
{
    ColumnDefinition col;
    col.name = expect(TokenType::IDENTIFIER, "expected column name").value;
    col.type = parse_data_type();
 
    if (col.type == DataType::VARCHAR) {
        if (match(TokenType::LPAR)) {
            const Token& len_tok = expect(TokenType::NUMBER_LITERAL, "expected max length");
            col.max_length = static_cast<uint16_t>(std::stoi(len_tok.value));
            expect(TokenType::RPAR, "expected ')'");
        } else {
            col.max_length = 255;
        }
    }
 
    col.is_primary_key = false;
    col.is_nullable = false;
    col.is_unique = false;
 
    bool parsing_modifiers = true;
    while (parsing_modifiers) {
        if (check(TokenType::PRIMARY)) {
            advance();
            expect(TokenType::KEY, "expected KEY after PRIMARY");
            col.is_primary_key = true;
        } else if (match(TokenType::UNIQUE)) {
            col.is_unique = true;
        } else if (match(TokenType::NULLABLE)) {
            col.is_nullable = true;
        } else if (match(TokenType::REFERENCES)) {
            col.fk_table  = expect(TokenType::IDENTIFIER, "expected FK table name").value;
            expect(TokenType::LPAR, "expected '(' after FK table name");
            col.fk_column = expect(TokenType::IDENTIFIER, "expected FK column name").value;
            expect(TokenType::RPAR, "expected ')' after FK column name");
        } else {
            parsing_modifiers = false;
        }
    }
    return col;
}
 
Statement Parser::parse_drop()
{
    advance(); // consume DROP
    if (check(TokenType::TABLE)) {
        advance();
        DropTableStatement stmt;
        if (check(TokenType::IF)) {
            advance();
            expect(TokenType::EXISTS, "expected EXISTS after IF");
            stmt.if_exists = true;
        }
        stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;
        return stmt;
    }
    if (check(TokenType::INDEX)) {
        advance();
        DropIndexStatement stmt;
        stmt.index_name = expect(TokenType::IDENTIFIER, "expected index name").value;
        return stmt;
    }
    throw std::runtime_error(
        "Syntax error at line " + std::to_string(peek().line) +
        ": expected TABLE or INDEX after DROP, got '" + peek().value + "'");
}
 
Statement Parser::parse_alter()
{
    advance(); // consume ALTER
    expect(TokenType::TABLE, "expected TABLE after ALTER");
    AlterTableStatement stmt;
    stmt.table_name = expect(TokenType::IDENTIFIER, "expected table name").value;
 
    if (check(TokenType::ADD)) {
        advance();
        if (check(TokenType::COLUMN)) advance();
        stmt.action  = AlterTableStatement::Action::ADD_COLUMN;
        stmt.new_col = parse_column_def();
        return stmt;
    }
    if (check(TokenType::DROP)) {
        advance();
        if (check(TokenType::COLUMN)) advance();
        stmt.action        = AlterTableStatement::Action::DROP_COLUMN;
        stmt.target_column = expect(TokenType::IDENTIFIER, "expected column name").value;
        return stmt;
    }
    if (check(TokenType::RENAME)) {
        advance();
        if (check(TokenType::COLUMN)) advance();
        stmt.action          = AlterTableStatement::Action::RENAME_COLUMN;
        stmt.target_column   = expect(TokenType::IDENTIFIER, "expected old column name").value;
        expect(TokenType::TO, "expected TO");
        stmt.new_column_name = expect(TokenType::IDENTIFIER, "expected new column name").value;
        return stmt;
    }
    throw std::runtime_error(
        "Syntax error at line " + std::to_string(peek().line) +
        ": expected ADD, DROP, or RENAME after ALTER TABLE name, got '" + peek().value + "'");
}
 
BeginStatement Parser::parse_begin()
{
    advance(); // consume BEGIN
    match(TokenType::TRANSACTION);
    return BeginStatement{};
}
 
CommitStatement Parser::parse_commit()
{
    advance(); // consume COMMIT
    match(TokenType::TRANSACTION);
    return CommitStatement{};
}
 
RollbackStatement Parser::parse_rollback()
{
    advance(); // consume ROLLBACK
    match(TokenType::TRANSACTION);
    return RollbackStatement{};
}