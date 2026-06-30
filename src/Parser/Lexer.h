#pragma once
#include <string>
#include <vector>
#include <stdexcept>


enum class TokenType {
    // KEYWOWRD  
    SELECT, INSERT, UPDATE,
    DELETE, CREATE, DROP,
    JOIN, LEFT, RIGHT, FULL, OUTER, ON,
    FROM, INTO, WHERE, SET, VALUES, TABLE, INDEX,
    AND, OR, NOT, NULL_KW,
    PRIMARY, KEY, UNIQUE, NULLABLE,
    INT_KW, VARCHAR, NUMBER_KW, BOOLEAN_KW, DATE_KW,
    REFERENCES,

    // LITERALI
    NUMBER_LITERAL, STRING_LITERAL,
    BOOL_LITERAL, IDENTIFIER,

    // OPERATORS

    EQ, // =
    NEQ, // != OR <>
    LT, // <
    GT, // >
    LTE, // <=
    GTE, // >=

    // SEPARATORI
    LPAR, // (
    RPAR, // )
    COMMA, // ,
    SEMICOLON, // ;
    STAR, // *
    DOT, // .

    EOF_TOKEN
};


struct Token {
    TokenType type;
    std::string value; // For literals
    int line; // For error reporting

    Token(TokenType type, const std::string& value, int line)
        : type(type), value(std::move(value)), line(line) {}
};

class Lexer
{
    public:
        explicit Lexer(const std::string& input);

        std::vector<Token> tokenize();

    private:
        std::string input;
        size_t pos_ = 0;
        int line_ = 1;

        char peek() const;
        char peek_next() const;
        char advance();
        bool at_end() const;
        void skip_whitespace_and_comments();

        Token read_number();
        Token read_string();
        Token read_identifier_or_keyword();

        static TokenType keyword_to_identifier(const std::string& upper);
};