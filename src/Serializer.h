#pragma once

#include <iostream>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <variant>

struct DateTime {
    int16_t year   = 0;
    uint8_t month  = 1;   // 1-12
    uint8_t day    = 1;   // 1-31
    uint8_t hour   = 0;   // 0-23
    uint8_t minute = 0;   // 0-59
    uint8_t second = 0;   // 0-59

    bool operator==(const DateTime& o) const {
        return year==o.year && month==o.month && day==o.day
            && hour==o.hour && minute==o.minute && second==o.second;
    }

    bool operator<(const DateTime& o) const {
        if (year   != o.year)   return year   < o.year;
        if (month  != o.month)  return month  < o.month;
        if (day    != o.day)    return day    < o.day;
        if (hour   != o.hour)   return hour   < o.hour;
        if (minute != o.minute) return minute < o.minute;
        return second < o.second;
    }
    bool operator!=(const DateTime& o) const { return !(*this == o); }
    bool operator>(const DateTime& o)  const { return o < *this; }
    bool operator<=(const DateTime& o) const { return !(o < *this); }
    bool operator>=(const DateTime& o) const { return !(*this < o); }
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
    std::string fk_table;
    std::string fk_column;
};

using Value = std::variant<std::monostate, int32_t, std::string, double, DateTime,  bool>;
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
