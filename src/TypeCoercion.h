#include "Serializer.h"

static DateTime coerce_string_to_date(const std::string& s) {
    // Supported formats (with optional time part):
    //   YYYY-MM-DD HH:MM:SS
    //   YYYY-MM-DD
    //   DD-MM-YYYY HH:MM:SS
    //   DD-MM-YYYY
    //   DD-MM-YY HH:MM:SS
    //   DD-MM-YY
    // Separator can be '-' or '/'
    DateTime dt;

    std::string norm = s;
    for (char& c : norm) if (c == '/') c = '-';

     int a, b, c_val;
    int hh = 0, mm = 0, ss = 0;
 
    if (std::sscanf(norm.c_str(), "%d-%d-%d %d:%d:%d", &a, &b, &c_val, &hh, &mm, &ss) < 3 &&
        std::sscanf(norm.c_str(), "%d-%d-%d", &a, &b, &c_val) != 3) {
        throw std::runtime_error(
            "Cannot convert '" + s + "' to DATE. "
            "Supported formats: YYYY-MM-DD, DD-MM-YYYY, DD-MM-YY, with optional HH:MM:SS");
    }
 
    int year, month, day;
    if (a > 31) { 
        year = a;
        month = b;
        day = c_val;
    }
    else if (c_val > 31) {
        year = c_val;
        month = b;
        day = a;
    }
    else { 
        year = 2000 + c_val;
        month = b;
        day = a;
    }
 
    if (month < 1 || month > 12) throw std::runtime_error("Invalid month in date '" + s + "'");
    if (day   < 1 || day   > 31 || (month == 2 && day > 29 && year % 4 == 0) || (month == 2 && day == 29 && year % 4 != 0)) throw std::runtime_error("Invalid day in date '" + s + "'");
    if (hh < 0 || hh > 23) throw std::runtime_error("Invalid hour in date '" + s + "'");
    if (mm < 0 || mm > 59) throw std::runtime_error("Invalid minute in date '" + s + "'");
    if (ss < 0 || ss > 59) throw std::runtime_error("Invalid second in date '" + s + "'");
 
    dt.year = static_cast<int16_t>(year);
    dt.month = static_cast<uint8_t>(month);
    dt.day = static_cast<uint8_t>(day);
    dt.hour = static_cast<uint8_t>(hh);
    dt.minute = static_cast<uint8_t>(mm);
    dt.second = static_cast<uint8_t>(ss);
    return dt;
}

static Row coerce_row(const Row& row, const std::vector<ColumnDefinition>& schema) {
    if (row.size() != schema.size()) {
        throw std::runtime_error(
            "Column count mismatch: got " + std::to_string(row.size()) +
            " value(s), expected " + std::to_string(schema.size()));
    }
    Row out;
    out.reserve(row.size());
    for (size_t i = 0; i < schema.size(); i++) {
        const Value& v  = row[i];
        const DataType t = schema[i].type;
 
        if (std::holds_alternative<std::monostate>(v)) {
            if (!schema[i].is_nullable)
                throw std::runtime_error("NULL not allowed for column '" + schema[i].name + "'");
            out.push_back(v);
            continue;
        }
 
        if (t == DataType::DATE && std::holds_alternative<std::string>(v)) {
            out.push_back(Value(coerce_string_to_date(std::get<std::string>(v))));
            continue;
        }
 
        if (t == DataType::INT && std::holds_alternative<double>(v)) {
            out.push_back(Value(static_cast<int32_t>(std::get<double>(v))));
            continue;
        }
 
        if (t == DataType::NUMBER && std::holds_alternative<int32_t>(v)) {
            out.push_back(Value(static_cast<double>(std::get<int32_t>(v))));
            continue;
        }
 
        out.push_back(v);
    }
    return out;
}

inline Value coerce_value(const Value& v, const ColumnDefinition& col) {
    const DataType t = col.type;
 
    if (std::holds_alternative<std::monostate>(v)) {
        if (!col.is_nullable)
            throw std::runtime_error("NULL not allowed for column '" + col.name + "'");
        return v;
    }
 
    if (t == DataType::DATE && std::holds_alternative<std::string>(v))
        return Value(coerce_string_to_date(std::get<std::string>(v)));
 
    if (t == DataType::INT && std::holds_alternative<double>(v))
        return Value(static_cast<int32_t>(std::get<double>(v)));
 
    if (t == DataType::NUMBER && std::holds_alternative<int32_t>(v))
        return Value(static_cast<double>(std::get<int32_t>(v)));
 
    return v;
}