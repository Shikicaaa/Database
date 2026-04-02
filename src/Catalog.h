#pragma once

#include "Table.h"
#include "BTree.h"
#include <map>
#include <memory>
#include <ctime>

class Catalog {
private:
    Pager& pager;
    std::unique_ptr<BTree> catalog_btree;
    
    std::map<std::string, std::unique_ptr<BTree>> btrees_cache;
    std::map<std::string, std::unique_ptr<Table>> tables_cache;
    
    static uint32_t hash_table_name(const std::string& name);
    
    // 4B root_page_id][4B created_at][4B version][2B schema_size][schema_binary...]
    std::vector<uint8_t> serialize_catalog_entry(
        uint32_t root_page_id, uint32_t created_at, 
        uint32_t version, const std::vector<ColumnDefinition>& schema);

    bool deserialize_catalog_entry(
        const std::vector<char>& data, uint32_t& root_page_id,
        uint32_t& created_at, uint32_t& version,
        std::vector<ColumnDefinition>& schema);

public:
    Catalog(Pager& p);

    bool create_table(const std::string& name, const std::vector<ColumnDefinition>& cols);
    Table* get_table(const std::string& name);
    bool table_exists(const std::string& name);
};