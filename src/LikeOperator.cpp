#include "LikeOperator.h"
#include <iostream>

static char normalize_char(char c, bool case_insensitive)
{
    return case_insensitive ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : c;
}

std::vector<PatToken> normalize_pattern(const std::string &pattern, char escape)
{
    std::vector<PatToken> tokens;
    for (size_t i = 0; i < pattern.size(); i++) {
        char c = pattern[i];
        if (c == escape && i + 1 < pattern.size()) {
            tokens.push_back({PatTokenType::LITERAL, pattern[i + 1]});
            i++;
        } else if (c == '%') {
            tokens.push_back({PatTokenType::ANY_SEQ, 0});
        } else if (c == '_') {
            tokens.push_back({PatTokenType::ANY_CHAR, 0});
        } else {
            tokens.push_back({PatTokenType::LITERAL, c});
        }
    }
    return tokens;
}

bool like_match(const std::string &pattern, const std::string &text, bool case_insensitive)
{
    auto tokens = normalize_pattern(pattern, '\\');
    size_t n = text.size();
    size_t m = tokens.size();

    std::vector<std::vector<bool>> dp(n + 1, std::vector<bool>(m + 1, false));
    dp[0][0] = true;

    for (size_t j = 1; j <= m; j++) {
        if (tokens[j - 1].type == PatTokenType::ANY_SEQ) {
            dp[0][j] = dp[0][j - 1];
        }
    }

    for (size_t i = 1; i <= n; i++) {
        for (size_t j = 1; j <= m; j++) {
            const PatToken& t = tokens[j - 1];
            if (t.type == PatTokenType::ANY_SEQ) {
                dp[i][j] = dp[i - 1][j] || dp[i][j - 1];
            } else if (t.type == PatTokenType::ANY_CHAR) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = dp[i - 1][j - 1] &&
                    (normalize_char(text[i - 1], case_insensitive) ==
                     normalize_char(t.ch, case_insensitive));
            }
        }
    }

    return dp[n][m];
}
