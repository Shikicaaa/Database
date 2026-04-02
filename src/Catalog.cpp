#include "Catalog.h"
#include "SlottedPage.h"
#include <iostream>

Catalog::Catalog(Pager& p) : pager(p) {
    uint32_t catalog_root = p.get_catalog_root_page_id();
    catalog_btree = std::make_unique<BTree>(p, catalog_root, true);
}


uint32_t Catalog::hash_table_name(const std::string& name) {
    uint32_t hash = 5381;
    for (char c : name) {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
    }
    return hash;
}

std::vector<uint8_t> Catalog::serialize_catalog_entry(
    uint32_t root_page_id, uint32_t created_at, uint32_t version,
    const std::vector<ColumnDefinition>& schema) 
{
    std::vector<uint8_t> buffer;
    
    // root_page_id 4B
    buffer.push_back((root_page_id >> 24) & 0xFF);
    buffer.push_back((root_page_id >> 16) & 0xFF);
    buffer.push_back((root_page_id >> 8) & 0xFF);
    buffer.push_back(root_page_id & 0xFF);
    
    // created_at 4B
    buffer.push_back((created_at >> 24) & 0xFF);
    buffer.push_back((created_at >> 16) & 0xFF);
    buffer.push_back((created_at >> 8) & 0xFF);
    buffer.push_back(created_at & 0xFF);
    
    // version 4B
    buffer.push_back((version >> 24) & 0xFF);
    buffer.push_back((version >> 16) & 0xFF);
    buffer.push_back((version >> 8) & 0xFF);
    buffer.push_back(version & 0xFF);
    
    // schema
    std::vector<uint8_t> schema_data = Serializer::serialize_schema(schema);
    uint16_t schema_size = static_cast<uint16_t>(schema_data.size());
    
    buffer.push_back((schema_size >> 8) & 0xFF);
    buffer.push_back(schema_size & 0xFF);
    buffer.insert(buffer.end(), schema_data.begin(), schema_data.end());
    
    return buffer;
}

bool Catalog::deserialize_catalog_entry(
    const std::vector<char>& data, uint32_t& root_page_id,
    uint32_t& created_at, uint32_t& version,
    std::vector<ColumnDefinition>& schema)
{
    if (data.size() < 14) return false; // minimum: 4+4+4+2 bytes
    
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());
    
    root_page_id = (static_cast<uint32_t>(ptr[0]) << 24) |
                   (static_cast<uint32_t>(ptr[1]) << 16) |
                   (static_cast<uint32_t>(ptr[2]) << 8) |
                   static_cast<uint32_t>(ptr[3]);
    ptr += 4;
    
    created_at = (static_cast<uint32_t>(ptr[0]) << 24) |
                 (static_cast<uint32_t>(ptr[1]) << 16) |
                 (static_cast<uint32_t>(ptr[2]) << 8) |
                 static_cast<uint32_t>(ptr[3]);
    ptr += 4;
    
    version = (static_cast<uint32_t>(ptr[0]) << 24) |
              (static_cast<uint32_t>(ptr[1]) << 16) |
              (static_cast<uint32_t>(ptr[2]) << 8) |
              static_cast<uint32_t>(ptr[3]);
    ptr += 4;
    
    uint16_t schema_size = (static_cast<uint16_t>(ptr[0]) << 8) | ptr[1];
    ptr += 2;
    
    if (data.size() < static_cast<size_t>(14 + schema_size)) return false;
    
    schema = Serializer::deserialize_schema(ptr, schema_size);
    return true;
}

bool Catalog::create_table(const std::string& name, const std::vector<ColumnDefinition>& cols) {
    // if table already exists
    if (table_exists(name)) {
        std::cerr << "CATALOG: Table '" << name << "' already exists!" << std::endl;
        return false;
    }
    
    uint32_t table_root_page = pager.allocate_new_page();
    
    Page* page = pager.get_page(table_root_page);
    SlottedPage sp(page->data);
    sp.init_as_leaf_node(true);
    page->is_dirty = true;
    
    uint32_t created_at = static_cast<uint32_t>(std::time(nullptr));
    uint32_t version = 1;
    
    std::vector<uint8_t> entry_data = serialize_catalog_entry(table_root_page, created_at, version, cols);
    
    uint32_t key = hash_table_name(name);
    
    if (!catalog_btree->insert(key, 0, entry_data.data(), static_cast<uint16_t>(entry_data.size()))) {
        std::cerr << "CATALOG: Failed to insert table '" << name << "' into catalog!" << std::endl;
        pager.free_page(table_root_page);
        return false;
    }
    
    std::cout << "CATALOG: Created table '" << name << "' with root page " << table_root_page << std::endl;
    return true;
}

Table* Catalog::get_table(const std::string& name) {
    auto it = tables_cache.find(name);
    if (it != tables_cache.end()) {
        return it->second.get();
    }
    
    uint32_t key = hash_table_name(name);
    auto result = catalog_btree->find(key, 0);
    
    if (!result.has_value()) {
        return nullptr;
    }
    
    uint32_t root_page_id, created_at, version;
    std::vector<ColumnDefinition> schema;
    
    if (!deserialize_catalog_entry(result.value(), root_page_id, created_at, version, schema)) {
        std::cerr << "CATALOG: Failed to deserialize table '" << name << "'" << std::endl;
        return nullptr;
    }
    
    auto btree = std::make_unique<BTree>(pager, root_page_id, false);
    auto table = std::make_unique<Table>(name, schema, *btree);
    
    // cache
    Table* table_ptr = table.get();
    
    btrees_cache[name] = std::move(btree);
    tables_cache[name] = std::move(table);
    
    std::cout << "CATALOG: Loaded table '" << name << "' from disk (root page " << root_page_id << ")" << std::endl;
    return table_ptr;
}

bool Catalog::table_exists(const std::string& name) {
    // Check cache
    if (tables_cache.find(name) != tables_cache.end()) {
        return true;
    }
    
    uint32_t key = hash_table_name(name);
    return catalog_btree->find(key, 0).has_value();
}
