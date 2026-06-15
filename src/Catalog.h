#pragma once

#include "Table.h"
#include "BTree.h"
#include <map>
#include <memory>
#include <ctime>

struct FKReference {
    std::string child_table;
    std::string fk_column_name;
    std::string parent_column_name;
};

class Catalog {
private:
    Pager& pager;
    std::unique_ptr<BTree> catalog_btree;
    
    std::map<std::string, std::unique_ptr<BTree>> btrees_cache;
    std::map<std::string, std::unique_ptr<Table>> tables_cache;
    std::map<uint32_t, std::string> root_page_to_name_;
    
    static uint32_t hash_table_name(const std::string& name);
    
    // Entry format: [4B root_page_id][4B created_at][4B version][2B schema_size][schema...][1B name_len][name]
    // The table-name suffix is optional for backward compatibility with old entries.
    std::vector<uint8_t> serialize_catalog_entry(
        uint32_t root_page_id, uint32_t created_at,
        uint32_t version, const std::vector<ColumnDefinition>& schema,
        const std::string& table_name);

    bool deserialize_catalog_entry(
        const std::vector<char>& data, uint32_t& root_page_id,
        uint32_t& created_at, uint32_t& version,
        std::vector<ColumnDefinition>& schema,
        std::string& table_name);

public:
    Catalog(Pager& p);

    bool create_table(const std::string& name, const std::vector<ColumnDefinition>& cols);
    Table* get_table(const std::string& name);
    bool table_exists(const std::string& name);

    // FK enforcement
    bool fk_value_exists(const std::string& table_name, const std::string& column_name, const Value& value);

    bool child_has_fk_value(const std::string& child_table,
                            const std::string& fk_column_name,
                            const Value& value);

    std::vector<FKReference> get_referencing_tables(const std::string& parent_table_name);

};