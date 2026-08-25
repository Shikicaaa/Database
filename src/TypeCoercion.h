#include "DateUtils.h"

static DateTime coerce_string_to_date(const std::string& s) {
    return DateUtils::parse_date(s);
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