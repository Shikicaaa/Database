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
    const std::vector<ColumnDefinition>& schema,
    const std::string& table_name)
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
    
    // schema [2B size][schema bytes]
    std::vector<uint8_t> schema_data = Serializer::serialize_schema(schema);
    uint16_t schema_size = static_cast<uint16_t>(schema_data.size());
    buffer.push_back((schema_size >> 8) & 0xFF);
    buffer.push_back(schema_size & 0xFF);
    buffer.insert(buffer.end(), schema_data.begin(), schema_data.end());

    // table name suffix [1B len][name bytes] appended AFTER schema so old
    // entries still deserialize correctly.
    uint8_t name_len = static_cast<uint8_t>(table_name.size());
    buffer.push_back(name_len);
    buffer.insert(buffer.end(), table_name.begin(), table_name.end());
    
    return buffer;
}

bool Catalog::deserialize_catalog_entry(
    const std::vector<char>& data, uint32_t& root_page_id,
    uint32_t& created_at, uint32_t& version,
    std::vector<ColumnDefinition>& schema,
    std::string& table_name)
{
    if (data.size() < 14) return false; // minimum: 4+4+4+2 bytes

    const uint8_t* base = reinterpret_cast<const uint8_t*>(data.data());
    const uint8_t* ptr  = base;
    const uint8_t* end  = base + data.size();

    root_page_id = (static_cast<uint32_t>(ptr[0]) << 24) |
                   (static_cast<uint32_t>(ptr[1]) << 16) |
                   (static_cast<uint32_t>(ptr[2]) << 8)  |
                   static_cast<uint32_t>(ptr[3]);
    ptr += 4;

    created_at = (static_cast<uint32_t>(ptr[0]) << 24) |
                 (static_cast<uint32_t>(ptr[1]) << 16) |
                 (static_cast<uint32_t>(ptr[2]) << 8)  |
                 static_cast<uint32_t>(ptr[3]);
    ptr += 4;

    version = (static_cast<uint32_t>(ptr[0]) << 24) |
              (static_cast<uint32_t>(ptr[1]) << 16) |
              (static_cast<uint32_t>(ptr[2]) << 8)  |
              static_cast<uint32_t>(ptr[3]);
    ptr += 4;

    uint16_t schema_size = (static_cast<uint16_t>(ptr[0]) << 8) | ptr[1];
    ptr += 2;

    if (ptr + schema_size > end) return false;

    schema = Serializer::deserialize_schema(ptr, schema_size);
    ptr += schema_size;

    // Optional table-name suffix [1B len][name bytes].
    // Present in entries written by current code;
    table_name.clear();
    if (ptr < end) {
        uint8_t name_len = *ptr++;
        if (ptr + name_len <= end) {
            table_name.assign(reinterpret_cast<const char*>(ptr), name_len);
        }
    }

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
    
    std::vector<uint8_t> entry_data = serialize_catalog_entry(table_root_page, created_at, version, cols, name);
    
    uint32_t key = hash_table_name(name);
    
    if (!catalog_btree->insert(key, 0, entry_data.data(), static_cast<uint16_t>(entry_data.size()))) {
        std::cerr << "CATALOG: Failed to insert table '" << name << "' into catalog!" << std::endl;
        pager.free_page(table_root_page);
        return false;
    }
    
    std::cout << "CATALOG: Created table '" << name << "' with root page " << table_root_page << std::endl;
    root_page_to_name_[table_root_page] = name;
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
    
    std::string recovered_name;
    if (!deserialize_catalog_entry(result.value(), root_page_id, created_at, version, schema, recovered_name)) {
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
    root_page_to_name_[root_page_id] = name;
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

bool Catalog::fk_value_exists(const std::string &table_name, const std::string &column_name, const Value &value)
{
    Table* parent = get_table(table_name);
    if (!parent) {
        std::cerr << "CATALOG: Parent table '" << table_name << "' does not exist!" << std::endl;
        return false;
    }

    // Foregin key must be an integer!
    if (!std::holds_alternative<int32_t>(value)) {
        std::cerr << "CATALOG: Foreign key value must be an integer (primary key)!" << std::endl;
        return false;
    }

    uint32_t pk = static_cast<uint32_t>(std::get<int32_t>(value));

    return parent->find_row(pk).has_value();
}

std::vector<FKReference> Catalog::get_referencing_tables(const std::string &parent_table_name)
{
    std::vector<FKReference> references;

    uint32_t current_id = catalog_btree->get_root_page_id();

    while(true){
        Page* page = pager.get_page(current_id);
        PageHeader* ph = reinterpret_cast<PageHeader*>(page->data);
        if(ph->node_type == LEAF) break;
        SlottedPage sp(page->data);
        
        uint16_t* pointers = sp.get_cell_pointers();
        if(ph->num_cells == 0) break;
        InternalNodeCell* cell = reinterpret_cast<InternalNodeCell*>(page->data + pointers[0]);

        current_id = cell->page_id;
    }

    // walk through leaf chain
    while(current_id != 0){
        Page* page = pager.get_page(current_id);
        PageHeader* ph = reinterpret_cast<PageHeader*>(page->data);
        SlottedPage sp(page->data);
        uint16_t* pointers = sp.get_cell_pointers();

        for(uint16_t i = 0; i < ph->num_cells; i++){
            LeafCellHeader* leaf_ch = reinterpret_cast<LeafCellHeader*>(page->data + pointers[i]);
            std::vector<char> raw_data;

            if(leaf_ch->flags & CELL_FLAG_OVERFLOW){
                uint32_t overflow_page_id;
                std::memcpy(&overflow_page_id, page->data + pointers[i] + LEAF_CELL_HEADER_SIZE, sizeof(uint32_t));
                raw_data = SlottedPage::read_from_overflow(overflow_page_id, pager);
            }else{
                const char* data_ptr = page->data + pointers[i] + LEAF_CELL_HEADER_SIZE;
                raw_data.assign(data_ptr, data_ptr + leaf_ch->data_size);
            }

            uint32_t root_page_id, created_at, version;
            std::vector<ColumnDefinition> schema;
            std::string table_name_from_entry;
            if(!deserialize_catalog_entry(raw_data, root_page_id, created_at, version, schema, table_name_from_entry)){
                std::cerr << "CATALOG: Failed to deserialize catalog entry!" << std::endl;
                continue;
            }
            // Resolve table name -prefer the name embedded in the entry,
            // fall back to the in-memory reverse map,
            // and as a last resort fall back to the tables_cache name scan.
            std::string table_name = table_name_from_entry;

            if (table_name.empty()) {
                auto name_it = root_page_to_name_.find(root_page_id);
                if (name_it != root_page_to_name_.end()) {
                    table_name = name_it->second;
                }
            }

            if (table_name.empty()) {
                std::cerr << "CATALOG: Could not resolve name for table with root page "
                          << root_page_id << "; skipping FK check.\n";
                continue;
            }

            for (const auto& col : schema) {
                if (col.fk_table == parent_table_name) {
                    // FKReference: { child_table, fk_column_name (child col), parent_column_name (parent col) }
                    references.push_back({table_name, col.name, col.fk_column});
                }
            }
        }
        current_id = ph->right_child_page_id;
    }

    return references;
}