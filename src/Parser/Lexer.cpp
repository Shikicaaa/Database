#include "Lexer.h"
#include <cctype>
#include <algorithm>
#include <stdexcept>

Lexer::Lexer(const std::string& input) : input(input) {}

std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;
    while(true)
    {
        skip_whitespace_and_comments();
        if(at_end())
        {
            tokens.emplace_back(TokenType::EOF_TOKEN, "", line_);
            break;
        }

        char c = peek();

        if(c == '\'')
        {
            tokens.push_back(read_string());
            continue;
        }
        if(std::isdigit(c))
        {
            tokens.push_back(read_number());
            continue;
        }
        if(std::isalpha(c) || c == '_')
        {
            tokens.push_back(read_identifier_or_keyword());
            continue;
        }
        advance(); //consume the char we're about to classify

        switch(c)
        {
            case '(': tokens.emplace_back(TokenType::LPAR, "(", line_); break;
            case ')': tokens.emplace_back(TokenType::RPAR, ")", line_); break;
            case ',': tokens.emplace_back(TokenType::COMMA, ",", line_); break;
            case ';': tokens.emplace_back(TokenType::SEMICOLON, ";", line_); break;
            case '*': tokens.emplace_back(TokenType::STAR, "*", line_); break;
            case '=': tokens.emplace_back(TokenType::EQ, "=", line_); break;
            case '!':
                if(!at_end() && peek() == '=')
                {
                    advance();
                    tokens.emplace_back(TokenType::NEQ, "!=", line_);
                }
                else
                {
                    throw std::runtime_error("Unexpected character '!' at line " + std::to_string(line_));
                }
                break;
            case '<':
                if(!at_end() && peek() == '=')
                {
                    advance();
                    tokens.emplace_back(TokenType::LTE, "<=", line_);
                }
                else if(!at_end() && peek() == '>')
                {
                    advance();
                    tokens.emplace_back(TokenType::NEQ, "<>", line_);
                }
                else
                {
                    tokens.emplace_back(TokenType::LT, "<", line_);
                }
                break;
            case '>':
                if(!at_end() && peek() == '=')
                {
                    advance();
                    tokens.emplace_back(TokenType::GTE, ">=", line_);
                }
                else
                {
                    tokens.emplace_back(TokenType::GT, ">", line_);
                }
                break;
            case '.':
                tokens.emplace_back(TokenType::DOT, ".", line_);
                break;
            default:
                throw std::runtime_error(std::string("Unexpected character '") + c + "' at line " + std::to_string(line_));
        }
    }
    return tokens;
}

char Lexer::peek() const
{
    return at_end() ? '\0' : input[pos_];
}

char Lexer::peek_next() const
{
    return (pos_ + 1 < input.size()) ? input[pos_ + 1] : '\0';
}

char Lexer::advance() 
{
    char c = input[pos_++];
    if(c=='\n') ++line_;
    return c;
}

bool Lexer::at_end() const
{
    return pos_ >= input.size();
}

void Lexer::skip_whitespace_and_comments()
{
    while(!at_end())
    {
        char c = peek();
        
        if(std::isspace(c)) 
        {
            advance();
            continue;
        }

        if(c == '-' && peek_next() == '-')
        {
            while(!at_end() && peek() != '\n') advance();
            continue;
        }

        break;
    }
}


Token Lexer::read_number()
{
    int start_line = line_;
    std::string num;

    if(peek() == '-') num += advance(); // Negative number
    while(!at_end() && (std::isdigit(peek())))
    {
        num += advance();
    }
    if(!at_end() && peek() == '.')
    {
        num += advance(); // Decimal point
        while(!at_end() && std::isdigit(peek()))
        {
            num += advance();
        }
    }
    return Token(TokenType::NUMBER_LITERAL, num, start_line);
}

Token Lexer::read_string()
{
    int start_line = line_;
    advance();

    std::string str;
    while(!at_end() && peek() != '\'')
    {
        if(peek() == '\''  && peek_next() == '\'')
        {
            advance(); advance();
            str += '\'';
        }else
        {
            str += advance();
        }
    }

    if(at_end())
    {
        throw std::runtime_error("Unterminated string literal at line " + std::to_string(start_line));
    }
    advance(); // Consume the closing quote
    return Token(TokenType::STRING_LITERAL, str, start_line);
}

Token Lexer::read_identifier_or_keyword()
{
    int start_line = line_;
    std::string word;
    while(!at_end() && (std::isalnum(peek()) || peek() == '_'))
    {
        word += advance();
    }
    std::string upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    TokenType type = keyword_to_identifier(upper);
    if(upper == "TRUE" || upper == "FALSE")
    {
        return Token(TokenType::BOOL_LITERAL, upper, start_line);
    }
    std::string stored = (type == TokenType::IDENTIFIER) ? word : upper;
    return Token(type, stored, start_line);
}

TokenType Lexer::keyword_to_identifier(const std::string& upper)
{
    if(upper == "SELECT") return TokenType::SELECT;
    if(upper == "INSERT") return TokenType::INSERT;
    if(upper == "UPDATE") return TokenType::UPDATE;
    if(upper == "DELETE") return TokenType::DELETE;
    if(upper == "CREATE") return TokenType::CREATE;
    if(upper == "DROP") return TokenType::DROP;
    if(upper == "FROM") return TokenType::FROM;
    if(upper == "INTO") return TokenType::INTO;
    if(upper == "WHERE") return TokenType::WHERE;
    if(upper == "SET") return TokenType::SET;
    if(upper == "VALUES") return TokenType::VALUES;
    if(upper == "TABLE") return TokenType::TABLE;
    if(upper == "AND") return TokenType::AND;
    if(upper == "OR") return TokenType::OR;
    if(upper == "NOT") return TokenType::NOT;
    if(upper == "NULL") return TokenType::NULL_KW;
    if(upper == "PRIMARY") return TokenType::PRIMARY;
    if(upper == "KEY") return TokenType::KEY;
    if(upper == "UNIQUE") return TokenType::UNIQUE;
    if(upper == "NULLABLE") return TokenType::NULLABLE;
    if(upper == "INT") return TokenType::INT_KW;
    if(upper == "VARCHAR") return TokenType::VARCHAR;
    if(upper == "NUMBER") return TokenType::NUMBER_KW;
    if(upper == "BOOLEAN") return TokenType::BOOLEAN_KW;
    if(upper == "DATE") return TokenType::DATE_KW;
    if(upper == "JOIN") return TokenType::JOIN;
    if(upper == "LEFT") return TokenType::LEFT;
    if(upper == "RIGHT") return TokenType::RIGHT;
    if(upper == "FULL") return TokenType::FULL;
    if(upper == "OUTER") return TokenType::OUTER;
    if(upper == "ON") return TokenType::ON;

    return TokenType::IDENTIFIER;
}