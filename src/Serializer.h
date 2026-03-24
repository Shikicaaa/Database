#pragma once

#include <iostream>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <variant>

struct DateUnix {
    int32_t timestamp;
};

enum class DataType {
    INT,
    VARCHAR,
    NUMBER,
    DATE,
    BOOLEAN
};

struct ColumnDefinition {
    std::string name;
    DataType type;
    bool is_primary_key;
    bool is_nullable;
    bool is_unique;
    uint16_t max_length; 
};

using Value = std::variant<std::monostate, int32_t, std::string, double, DateUnix,  bool>;
using Row = std::vector<Value>;

class Serializer {
public:
    std::vector<uint8_t> serialize(const std::vector<ColumnDefinition>& schema, const Row& row);

    Row deserialize(const std::vector<ColumnDefinition>& schema, const uint8_t* data, uint16_t size);

};
