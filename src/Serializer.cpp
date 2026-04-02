#include "Serializer.h"

std::vector<uint8_t> Serializer::serialize(
    const std::vector<ColumnDefinition>& schema,
    const Row& row)
{
    std::vector<uint8_t> buffer;

    size_t n = schema.size();
    size_t null_bitmap_size = (n + 7) / 8;

    std::vector<uint8_t> bitmap(null_bitmap_size, 0);
    for(size_t i = 0;i<n;i++){
        bool is_null = std::holds_alternative<std::monostate>(row[i]) && schema[i].is_nullable;
        if (is_null) {
            size_t byte_index = i / 8;
            size_t bit_index = i % 8;
            bitmap[byte_index] |= (1 << bit_index);
        }
    }

    buffer.insert(buffer.end(), bitmap.begin(), bitmap.end());

    for(size_t i = 0;i<n;i++){
        if(std::holds_alternative<std::monostate>(row[i]) && schema[i].is_nullable){
            continue;
        }
        switch (schema[i].type){
            case DataType::INT: {
                int32_t value = std::get<int32_t>(row[i]);

                uint8_t bytes[4];
                bytes[0] = (value >> 24) & 0xFF;
                bytes[1] = (value >> 16) & 0xFF;
                bytes[2] = (value >> 8) & 0xFF;
                bytes[3] = value & 0xFF;

                buffer.insert(buffer.end(), bytes, bytes + 4);
                break;
            }
            case DataType::VARCHAR:{
                const std::string& str = std::get<std::string>(row[i]);
                uint16_t length = static_cast<uint16_t>(str.size());

                uint8_t length_bytes[2];
                length_bytes[0] = (length >> 8) & 0xFF;
                length_bytes[1] = length & 0xFF;

                buffer.insert(buffer.end(), length_bytes, length_bytes + 2);
                buffer.insert(buffer.end(), str.begin(), str.end());
                break;
            }
            case DataType::NUMBER:{
                double value = std::get<double>(row[i]);
                uint8_t bytes[8];
                std::memcpy(bytes, &value, 8);
                buffer.insert(buffer.end(), bytes, bytes + 8);
                break;
            }

            case DataType::BOOLEAN: {
                bool value = std::get<bool>(row[i]);
                buffer.push_back(value ? 0x01 : 0x00);
                break;
            }

            case DataType::DATE: {
                //Unix timestamp
                DateUnix date = std::get<DateUnix>(row[i]);
                int32_t timestamp = date.timestamp;

                uint8_t bytes[4];
                bytes[0] = (timestamp >> 24) & 0xFF;
                bytes[1] = (timestamp >> 16) & 0xFF;
                bytes[2] = (timestamp >> 8) & 0xFF;
                bytes[3] = timestamp & 0xFF;

                buffer.insert(buffer.end(), bytes, bytes + 4);
                break;
            }
        }
    }

    return buffer;
}

Row Serializer::deserialize(
    const std::vector<ColumnDefinition>& schema,
    const uint8_t* data,
    uint16_t size)
{
    Row row;
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;

    size_t n = schema.size();
    size_t null_bitmap_size = (n + 7) / 8;

    // TODO: Implement bitmap optimization
    if (ptr + null_bitmap_size > end) return row;
    ptr += null_bitmap_size;

    for (size_t i = 0; i < n; i++) {
        size_t byte_index = i / 8;
        size_t bit_index = i % 8;
        bool is_null = (data[byte_index] & (1 << bit_index)) != 0;
        if (is_null) {
            row.push_back(std::monostate{});
            continue;
        }
        switch (schema[i].type) {
            case DataType::INT: {
                if (ptr + 4 > end) return row;

                int32_t value = (static_cast<int32_t>(ptr[0]) << 24) |
                                (static_cast<int32_t>(ptr[1]) << 16) |
                                (static_cast<int32_t>(ptr[2]) << 8)  |
                                (static_cast<int32_t>(ptr[3]));
                ptr += 4;

                row.push_back(Value(value));
                break;
            }

            case DataType::VARCHAR: {
                if (ptr + 2 > end) return row;

                uint16_t length = (static_cast<uint16_t>(ptr[0]) << 8) |
                                  (static_cast<uint16_t>(ptr[1]));
                ptr += 2;

                if (ptr + length > end) return row;

                std::string value(reinterpret_cast<const char*>(ptr), length);
                ptr += length;

                row.push_back(Value(value));
                break;
            }

            case DataType::NUMBER: {
                if (ptr + 8 > end) return row;

                double value;
                std::memcpy(&value, ptr, 8);
                ptr += 8;

                row.push_back(Value(value));
                break;
            }

            case DataType::BOOLEAN: {
                if (ptr + 1 > end) return row;

                bool value = (*ptr != 0);
                ptr += 1;

                row.push_back(Value(value));
                break;
            }

            case DataType::DATE: {
                if (ptr + 4 > end) return row;

                int32_t timestamp = (static_cast<int32_t>(ptr[0]) << 24) |
                                    (static_cast<int32_t>(ptr[1]) << 16) |
                                    (static_cast<int32_t>(ptr[2]) << 8)  |
                                    (static_cast<int32_t>(ptr[3]));
                ptr += 4;

                DateUnix date;
                date.timestamp = timestamp;
                row.push_back(Value(date));
                break;
            }
        }
    }

    return row;
}

std::vector<uint8_t> Serializer::serialize_schema(const std::vector<ColumnDefinition>& schema) {
    std::vector<uint8_t> buffer;
    
    // Number of columns (2 bytes)
    uint16_t num_cols = static_cast<uint16_t>(schema.size());
    buffer.push_back((num_cols >> 8) & 0xFF);
    buffer.push_back(num_cols & 0xFF);
    
    for (const auto& col : schema) {
        // Name length (1 byte) + name
        uint8_t name_len = static_cast<uint8_t>(col.name.size());
        buffer.push_back(name_len);
        buffer.insert(buffer.end(), col.name.begin(), col.name.end());
        
        // Type (1 byte)
        buffer.push_back(static_cast<uint8_t>(col.type));
        
        // Flags (1 byte): is_pk | (is_nullable << 1) | (is_unique << 2)
        uint8_t flags = (col.is_primary_key ? 1 : 0) |
                        (col.is_nullable ? 2 : 0) |
                        (col.is_unique ? 4 : 0);
        buffer.push_back(flags);
        
        // Max length (2 bytes)
        buffer.push_back((col.max_length >> 8) & 0xFF);
        buffer.push_back(col.max_length & 0xFF);
    }
    
    return buffer;
}

std::vector<ColumnDefinition> Serializer::deserialize_schema(const uint8_t* data, uint16_t size) {
    std::vector<ColumnDefinition> schema;
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    
    if (ptr + 2 > end) return schema;
    
    uint16_t num_cols = (static_cast<uint16_t>(ptr[0]) << 8) | ptr[1];
    ptr += 2;
    
    for (uint16_t i = 0; i < num_cols; i++) {
        if (ptr + 1 > end) return schema;
        
        // Name
        uint8_t name_len = *ptr++;
        if (ptr + name_len > end) return schema;
        std::string name(reinterpret_cast<const char*>(ptr), name_len);
        ptr += name_len;
        
        if (ptr + 4 > end) return schema;
        
        // Type
        DataType type = static_cast<DataType>(*ptr++);
        
        // Flags
        uint8_t flags = *ptr++;
        bool is_pk = (flags & 1) != 0;
        bool is_nullable = (flags & 2) != 0;
        bool is_unique = (flags & 4) != 0;
        
        // Max length
        uint16_t max_len = (static_cast<uint16_t>(ptr[0]) << 8) | ptr[1];
        ptr += 2;
        
        schema.push_back({name, type, is_pk, is_nullable, is_unique, max_len});
    }
    
    return schema;
}
