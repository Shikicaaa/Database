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

    // Schema serialization for catalog persistence
    // Format: [2B num_columns] per column: [1B name_len][name][1B type][1B flags][2B max_len]
    // flags = is_primary_key | (is_nullable << 1) | (is_unique << 2)
    static std::vector<uint8_t> serialize_schema(const std::vector<ColumnDefinition>& schema);
    static std::vector<ColumnDefinition> deserialize_schema(const uint8_t* data, uint16_t size);
};
