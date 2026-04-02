#include "Table.h"

bool Table::insert_row(const Row &row)
{
    if (row.size() != columns.size())
    {
        std::cerr << "ERROR: Row size doesn't match table schema!" << std::endl;
        return false;
    }

    uint32_t primary_key = extract_primary_key(row);

    for (size_t i = 0; i < columns.size(); i++) {
        if (columns[i].is_primary_key || columns[i].is_unique) {
            // TODO: Ovo mora da se kasnije promeni, nije dovoljno da se samo proveri primary, vec i ostali,
            // + mora da se ubaci za unique kolone, a ne samo primary, odnosno da imamo btree index i za unique kolone
            // trenutno je ovo samo da bi se testirao unique constraint, ali kasnije ce biti bitno i za ostale kolone koje nisu primary key
            
            if (columns[i].is_primary_key) {
                if (find_row(primary_key).has_value()) {
                    std::cerr << "ERROR: Duplicate value for PRIMARY KEY/UNIQUE column '" 
                              << columns[i].name << "' with value " << primary_key << std::endl;
                    return false;
                }
            }
        }
    }

    std::vector<uint8_t> serialized_data = serializer.serialize(columns, row);

    return btree.insert(primary_key, 0, serialized_data.data(), serialized_data.size());
}

std::optional<Row> Table::find_row(uint32_t primary_key)
{
    std::optional<std::vector<char>> result = btree.find(primary_key, 0);

    if (!result.has_value())
    {
        return std::nullopt;
    }

    const std::vector<char> &data = result.value();
    const uint8_t *byte_data = reinterpret_cast<const uint8_t *>(data.data());
    uint16_t size = static_cast<uint16_t>(data.size());

    Row row = serializer.deserialize(columns, byte_data, size);

    return row;
}

bool Table::update_row(uint32_t primary_key, const Row &row)
{
    if (row.size() != columns.size())
    {
        std::cerr << "ERROR: Row size doesn't match table schema!\n";
        return false;
    }

    std::vector<uint8_t> serialized_data = serializer.serialize(columns, row);
    return btree.update(primary_key, 0, serialized_data.data(), serialized_data.size());
}

bool Table::remove_row(uint32_t primary_key)
{
    if(!btree.remove(primary_key, 0)){
        std::cerr << "ERROR: Deletion not successfull, key not found";
        return false;
    }
    return true;
}

uint32_t Table::extract_primary_key(const Row &row)
{
    for (size_t i = 0; i < columns.size(); i++)
    {
        if (columns[i].is_primary_key)
        {
            return std::get<int32_t>(row[i]);
        }
    }
    return 0;
}
