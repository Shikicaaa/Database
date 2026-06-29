#include "Catalog.h"
#include "SlottedPage.h"
#include <iostream>
#include <optional>

static const uint8_t ENTRY_TYPE_TABLE = 0x01;
static const uint8_t ENTRY_TYPE_INDEX = 0x02;

Catalog::Catalog(Pager& p) : pager(p) {
    uint32_t catalog_root = p.get_catalog_root_page_id();
    catalog_btree = std::make_unique<BTree>(p, catalog_root, true);
}


uint32_t Catalog::hash_varchar(const std::string& s) {
    uint32_t h = 0;
    for (char c : s)
        h = h * 31 + static_cast<uint8_t>(c);
    return h;
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

    // type byte
    buffer.push_back(ENTRY_TYPE_TABLE);

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
    if (data.size() < 15) return false; // minimum: 1(type) + 4+4+4+2 bytes

    const uint8_t* base = reinterpret_cast<const uint8_t*>(data.data());
    const uint8_t* ptr  = base;
    const uint8_t* end  = base + data.size();

    // type byte — must be a table entry
    if (*ptr++ != ENTRY_TYPE_TABLE) return false;

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

std::vector<uint8_t> Catalog::serialize_index_entry(
    uint32_t root_page_id,
    const std::string& table_name,
    const std::string& column_name,
    const std::string& index_name)
{
    std::vector<uint8_t> buf;
    buf.push_back(ENTRY_TYPE_INDEX);

    buf.push_back((root_page_id >> 24) & 0xFF);
    buf.push_back((root_page_id >> 16) & 0xFF);
    buf.push_back((root_page_id >> 8)  & 0xFF);
    buf.push_back(root_page_id         & 0xFF);

    auto push_str = [&](const std::string& s) {
        buf.push_back(static_cast<uint8_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
    };
    push_str(table_name);
    push_str(column_name);
    push_str(index_name);
    return buf;
}

bool Catalog::deserialize_index_entry(
    const std::vector<char>& data,
    uint32_t& root_page_id,
    std::string& table_name,
    std::string& column_name,
    std::string& index_name)
{
    if (data.size() < 6) return false;
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());
    const uint8_t* end = ptr + data.size();

    if (*ptr++ != ENTRY_TYPE_INDEX) return false;

    root_page_id = (static_cast<uint32_t>(ptr[0]) << 24) |
                   (static_cast<uint32_t>(ptr[1]) << 16) |
                   (static_cast<uint32_t>(ptr[2]) << 8)  |
                   static_cast<uint32_t>(ptr[3]);
    ptr += 4;

    auto read_str = [&](std::string& out) -> bool {
        if (ptr >= end) return false;
        uint8_t len = *ptr++;
        if (ptr + len > end) return false;
        out.assign(reinterpret_cast<const char*>(ptr), len);
        ptr += len;
        return true;
    };
    return read_str(table_name) && read_str(column_name) && read_str(index_name);
}

void Catalog::ensure_indexes_loaded() {
    if (indexes_loaded_) return;
    indexes_loaded_ = true;

    uint32_t current_id = catalog_btree->find_first_leaf_node();

    while (current_id != 0) {
        Page* page = pager.get_page(current_id);
        PageHeader* ph = reinterpret_cast<PageHeader*>(page->data);
        SlottedPage sp(page->data);
        uint16_t* pointers = sp.get_cell_pointers();

        for (uint16_t i = 0; i < ph->num_cells; i++) {
            LeafCellHeader* lch = reinterpret_cast<LeafCellHeader*>(page->data + pointers[i]);
            if (lch->data_size == 0) continue;
            const uint8_t* first_byte = reinterpret_cast<const uint8_t*>(
                page->data + pointers[i] + LEAF_CELL_HEADER_SIZE);
            if (*first_byte != ENTRY_TYPE_INDEX) continue;

            std::vector<char> raw;
            if (lch->flags & CELL_FLAG_OVERFLOW) {
                uint32_t ovfl;
                std::memcpy(&ovfl, page->data + pointers[i] + LEAF_CELL_HEADER_SIZE, sizeof(uint32_t));
                raw = SlottedPage::read_from_overflow(ovfl, pager);
            } else {
                const char* dp = page->data + pointers[i] + LEAF_CELL_HEADER_SIZE;
                raw.assign(dp, dp + lch->data_size);
            }

            IndexInfo info;
            if (deserialize_index_entry(raw, info.root_page_id,
                                        info.table_name, info.column_name, info.index_name)) {
                loaded_indexes_.push_back(info);
            }
        }
        current_id = ph->right_child_page_id;
    }
}

bool Catalog::create_secondary_index(const std::string& index_name,
                                      const std::string& table_name,
                                      const std::string& column_name)
{
    Table* table = get_table(table_name);
    if (!table) {
        std::cerr << "CATALOG: Table '" << table_name << "' not found for index creation.\n";
        return false;
    }

    // Validate column exists and is INT
    const auto& cols = table->get_columns();
    int col_idx = -1;
    for (int i = 0; i < (int)cols.size(); i++) {
        if (cols[i].name == column_name) { col_idx = i; break; }
    }
    if (col_idx == -1) {
        std::cerr << "CATALOG: Column '" << column_name << "' not found in table '" << table_name << "'.\n";
        return false;
    }
    bool is_int = cols[col_idx].type == DataType::INT;
    bool is_varchar = cols[col_idx].type == DataType::VARCHAR;
    if (!is_int && !is_varchar) {
        std::cerr << "CATALOG: Secondary indexes only supported on INT and VARCHAR columns.\n";
        return false;
    }

    // Allocate and initialize a new BTree for the index
    uint32_t idx_root = pager.allocate_new_page();
    Page* idx_page = pager.get_page(idx_root);
    SlottedPage idx_sp(idx_page->data);
    idx_sp.init_as_leaf_node(true);
    idx_page->is_dirty = true;

    auto idx_btree = std::make_unique<BTree>(pager, idx_root, false);

    // Populate index from existing rows
    std::vector<Row> existing = table->scan_all();
    for (const Row& row : existing) {
        if ((int)row.size() <= col_idx) continue;
        uint32_t pk = table->extract_primary_key(row);
        if (is_int && std::holds_alternative<int32_t>(row[col_idx])) {
            uint32_t col_val = static_cast<uint32_t>(std::get<int32_t>(row[col_idx]));
            idx_btree->insert(col_val, pk, &pk, sizeof(uint32_t));
        } else if (is_varchar && std::holds_alternative<std::string>(row[col_idx])) {
            const std::string& s = std::get<std::string>(row[col_idx]);
            uint32_t col_key = hash_varchar(s);
            std::vector<uint8_t> chain_data;
            chain_data.push_back(static_cast<uint8_t>(s.size()));
            chain_data.insert(chain_data.end(), s.begin(), s.end());
            idx_btree->insert(col_key, pk, chain_data.data(), static_cast<uint16_t>(chain_data.size()));
        }
    }

    // Store the final root (may differ from idx_root after splits)
    uint32_t final_root = idx_btree->get_root_page_id();

    // Write index metadata to catalog BTree
    std::vector<uint8_t> entry = serialize_index_entry(final_root, table_name, column_name, index_name);
    uint32_t key = hash_table_name("__idx__" + index_name);
    if (!catalog_btree->insert(key, 0, entry.data(), static_cast<uint16_t>(entry.size()))) {
        std::cerr << "CATALOG: Failed to insert index '" << index_name << "' into catalog.\n";
        return false;
    }

    // Cache
    index_btrees_cache_[index_name] = std::move(idx_btree);
    loaded_indexes_.push_back({index_name, table_name, column_name, final_root});

    std::cout << "CATALOG: Created index '" << index_name << "' on " << table_name << "." << column_name << "\n";
    return true;
}

BTree* Catalog::get_index_btree(const std::string& index_name) {
    auto it = index_btrees_cache_.find(index_name);
    if (it != index_btrees_cache_.end()) return it->second.get();

    ensure_indexes_loaded();
    for (const auto& info : loaded_indexes_) {
        if (info.index_name == index_name) {
            auto btree = std::make_unique<BTree>(pager, info.root_page_id, false);
            BTree* ptr = btree.get();
            index_btrees_cache_[index_name] = std::move(btree);
            return ptr;
        }
    }
    return nullptr;
}

std::optional<IndexInfo> Catalog::find_index_for_column(const std::string& table_name,
                                                          const std::string& column_name)
{
    ensure_indexes_loaded();
    for (const auto& info : loaded_indexes_) {
        if (info.table_name == table_name && info.column_name == column_name)
            return info;
    }
    return std::nullopt;
}

std::vector<IndexInfo> Catalog::get_indexes_for_table(const std::string& table_name) {
    ensure_indexes_loaded();
    std::vector<IndexInfo> result;
    for (const auto& info : loaded_indexes_) {
        if (info.table_name == table_name) result.push_back(info);
    }
    return result;
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

bool Catalog::child_has_fk_value(const std::string& child_table,
                                    const std::string& fk_column_name,
                                    const Value& value)
{
    Table* child = get_table(child_table);
    if (!child) return false;

    const auto& cols = child->get_columns();

    // Find the index of the FK column in the child schema.
    int fk_col_idx = -1;
    for (int i = 0; i < (int)cols.size(); i++) {
        if (cols[i].name == fk_column_name) {
            fk_col_idx = i;
            break;
        }
    }
    if (fk_col_idx == -1) return false;

    // Scan all child rows and compare the FK column value.
    std::vector<Row> rows = child->scan_all();
    for (const Row& row : rows) {
        if ((int)row.size() > fk_col_idx && row[fk_col_idx] == value) {
            return true;
        }
    }
    return false;
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