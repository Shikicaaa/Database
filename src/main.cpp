#include "Pager.h"
#include "Catalog.h"
#include "Parser/Lexer.h"
#include "Parser/Parser.h"
#include "Executor.h"
#include <iostream>
#include <string>
#include <iomanip>
#include <cctype>
#include <readline/readline.h>
#include <readline/history.h>

static std::string trim_lower(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    std::string out = s.substr(a, b - a + 1);
    for (char& c : out) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return out;
}

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
        if (std::holds_alternative<DateTime>(v)) {
            const DateTime& dt = std::get<DateTime>(v);
            char buf[20];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
            return std::string(buf);
        }
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

    while (true) {
        const char* prompt = input.empty() ? "ShikiQL> " : "  -> ";
        char* raw = readline(prompt);

        if (!raw) break; // EOF (Ctrl-D)

        std::string line(raw);
        free(raw);

        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line == "exit" || line == "quit") {
            std::cout << "NA SPAVANJE!\n";
            break;
        }

        // Commands
        /*
        /showme or /sm or /gimme shows all tables and indexes in the catalog.
        /showme <filter> or /sm <filter> or /gimme <filter> shows filtered results. Filter can be "table", "tables", "idx", "index", or "indexes".
        */
        {
            std::string tl = trim_lower(line);
            bool is_meta = (tl == "/showme" || tl == "/sm" || tl == "/gimme" ||
                            tl.rfind("/showme ", 0) == 0 ||
                            tl.rfind("/sm ", 0) == 0 ||
                            tl.rfind("/gimme ", 0) == 0);
            if (is_meta) {
                std::string filter;
                size_t sp = tl.find(' ');
                if (sp != std::string::npos) filter = trim_lower(tl.substr(sp + 1));

                bool show_tables = filter.empty() || filter == "table" || filter == "tables";
                bool show_indexes = filter.empty() || filter == "idx" || filter == "index" || filter == "indexes";

                if (show_tables) {
                    auto names = catalog.get_all_table_names();
                    std::cout << "Tables (" << names.size() << "):\n";
                    for (const auto& n : names) std::cout << "  " << n << "\n";
                }
                if (show_tables && show_indexes) std::cout << "\n";
                if (show_indexes) {
                    auto idxs = catalog.get_all_indexes();
                    std::cout << "Indexes (" << idxs.size() << "):\n";
                    for (const auto& ix : idxs)
                        std::cout << "  " << ix.index_name << "  ->  "
                                  << ix.table_name << "(" << ix.column_name << ")\n";
                }
                std::cout << "\n";
                continue;
            }
        }

        // /schema <tablename>
        {
            std::string tl = trim_lower(line);
            if (tl.rfind("/schema", 0) == 0) {
                std::string arg = line.size() > 7 ? line.substr(7) : "";
                size_t a = arg.find_first_not_of(" \t'\"");
                size_t b = arg.find_last_not_of(" \t'\"");
                std::string tname = (a != std::string::npos) ? arg.substr(a, b - a + 1) : "";

                if (tname.empty()) {
                    std::cout << "Usage: /schema <table_name>\n\n";
                } else {
                    Table* t = catalog.get_table(tname);
                    if (!t) {
                        std::cout << "ERROR: Table '" << tname << "' not found.\n\n";
                    } else {
                        auto type_str = [](const ColumnDefinition& col) -> std::string {
                            switch (col.type) {
                                case DataType::INT: return "INT";
                                case DataType::VARCHAR: return "VARCHAR(" + std::to_string(col.max_length) + ")";
                                case DataType::NUMBER: "NUMBER";
                                case DataType::DATE: return "DATE";
                                case DataType::BOOLEAN: return "BOOLEAN";
                                default: return "?";
                            }
                        };
                        auto constraint_str = [](const ColumnDefinition& col) -> std::string {
                            std::string s;
                            if (col.is_primary_key) s += "PK ";
                            if (!col.is_nullable) s += "NOT NULL ";
                            if (col.is_unique) s += "UNIQUE ";
                            if (!col.fk_table.empty()) s += "FK->" + col.fk_table + "." + col.fk_column + " ";
                            if (!s.empty() && s.back() == ' ') s.pop_back();
                            return s;
                        };

                        const auto& cols = t->get_columns();
                        size_t w_name = 6, w_type = 4, w_cons = 11;
                        for (const auto& col : cols) {
                            w_name = std::max(w_name, col.name.size());
                            w_type = std::max(w_type, type_str(col).size());
                            w_cons = std::max(w_cons, constraint_str(col).size());
                        }
                        auto sep = [&]() {
                            std::cout << "+-" << std::string(w_name, '-')
                                      << "-+-" << std::string(w_type, '-')
                                      << "-+-" << std::string(w_cons, '-') << "-+\n";
                        };
                        std::cout << "Schema: " << tname << "\n";
                        sep();
                        std::cout << "| " << std::left << std::setw(w_name) << "column"
                                  << " | " << std::setw(w_type) << "type"
                                  << " | " << std::setw(w_cons) << "constraints" << " |\n";
                        sep();
                        for (const auto& col : cols) {
                            std::cout << "| " << std::left << std::setw(w_name) << col.name
                                      << " | " << std::setw(w_type) << type_str(col)
                                      << " | " << std::setw(w_cons) << constraint_str(col) << " |\n";
                        }
                        sep();
                        std::cout << "\n";
                    }
                }
                continue;
            }
        }

        if (!line.empty())
            add_history(line.c_str());

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