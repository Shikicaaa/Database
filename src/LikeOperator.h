#pragma once
#include <string>
#include <vector>

bool like_match(const std::string& pattern, const std::string& text, bool case_insensitive = false);

enum class PatTokenType { LITERAL, ANY_CHAR, ANY_SEQ };

struct PatToken {
    PatTokenType type;
    char ch = 0;
};

std::vector<PatToken> normalize_pattern(const std::string& pattern, char escape = '\\');