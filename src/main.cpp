#include "Pager.h"
#include "Catalog.h"
#include "Parser/Lexer.h"
#include "Parser/Parser.h"
#include "Executor.h"
#include <iostream>
#include <string>
#include <iomanip>

static void print_result(const ExecutionResult& result)
{
    if (!result.success) {
        std::cout << "ERROR: " << result.message << "\n";
        return;
    }

    if (result.rows.empty() && result.columns.empty()) {
        std::cout << result.message << "\n";
        return;
    }

    if (result.rows.empty()) {
        std::cout << "Empty set. " << result.message << "\n";
        return;
    }

    std::vector<size_t> widths(result.columns.size());
    for (size_t i = 0; i < result.columns.size(); i++) {
        widths[i] = result.columns[i].size();
    }

    auto value_to_str = [](const Value& v) -> std::string {
        if (std::holds_alternative<std::monostate>(v)) return "NULL";
        if (std::holds_alternative<int32_t>(v))
            return std::to_string(std::get<int32_t>(v));
        if (std::holds_alternative<double>(v)) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << std::get<double>(v);
            return oss.str();
        }
        if (std::holds_alternative<std::string>(v))
            return std::get<std::string>(v);
        if (std::holds_alternative<bool>(v))
            return std::get<bool>(v) ? "TRUE" : "FALSE";
        return "?";
    };

    std::vector<std::vector<std::string>> cells(result.rows.size(),
        std::vector<std::string>(result.columns.size()));

    for (size_t r = 0; r < result.rows.size(); r++) {
        for (size_t c = 0; c < result.columns.size(); c++) {
            cells[r][c] = (c < result.rows[r].size())
                          ? value_to_str(result.rows[r][c])
                          : "NULL";
            widths[c] = std::max(widths[c], cells[r][c].size());
        }
    }

    auto print_separator = [&](char left, char mid, char right, char fill) {
        std::cout << left;
        for (size_t i = 0; i < widths.size(); i++) {
            std::cout << std::string(widths[i] + 2, fill);
            std::cout << (i + 1 < widths.size() ? mid : right);
        }
        std::cout << "\n";
    };

    print_separator('+', '+', '+', '-');
    std::cout << "|";
    for (size_t i = 0; i < result.columns.size(); i++) {
        std::cout << " " << std::left << std::setw(widths[i])
                  << result.columns[i] << " |";
    }
    std::cout << "\n";
    print_separator('+', '+', '+', '-');

    for (const auto& row_cells : cells) {
        std::cout << "|";
        for (size_t i = 0; i < row_cells.size(); i++) {
            std::cout << " " << std::left << std::setw(widths[i])
                      << row_cells[i] << " |";
        }
        std::cout << "\n";
    }

    print_separator('+', '+', '+', '-');
    std::cout << result.message << "\n";
}

int main()
{
    Pager   pager("database.db", "MyDB");
    Catalog catalog(pager);
    Executor executor(catalog);

    std::cout << "ShikiDB v1.0\n";
    std::cout << "Type 'exit' or 'quit' to close.\n\n";

    std::string input;
    std::string line;

    while (true) {
        std::cout << (input.empty() ? "ShikiQL> " : "  -> ");
        
        if (!std::getline(std::cin, line)) break; //EOF

        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line == "exit" || line == "quit") {
            std::cout << "NA SPAVANJE!\n";
            break;
        }

        input += " " + line;

        if (input.find(';') == std::string::npos) continue;

        std::string remaining = input;
        input.clear();

        while (!remaining.empty()) {
            size_t semi = remaining.find(';');
            if (semi == std::string::npos) {
                input = remaining;
                break;
            }

            std::string sql = remaining.substr(0, semi);
            remaining = remaining.substr(semi + 1);

            if (sql.find_first_not_of(" \t\n\r") == std::string::npos) continue;

            // Lexer -> Parser -> Executor
            try {
                Lexer  lexer(sql);
                auto   tokens = lexer.tokenize();

                Parser parser(std::move(tokens));
                auto   stmt = parser.parse();

                ExecutionResult result = executor.execute(stmt);
                print_result(result);

            } catch (const std::exception& e) {
                std::cout << "ERROR: " << e.what() << "\n";
            }

            std::cout << "\n";
        }
    }

    return 0;
}