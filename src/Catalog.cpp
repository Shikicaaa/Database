#include "Catalog.h"
#include "SlottedPage.h"
#include "Logger.h"
#include <optional>
#include <algorithm>
#include <cstring>

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

uint32_t Catalog::hash_number(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return static_cast<uint32_t>(bits ^ (bits >> 32));
}

uint32_t Catalog::hash_datetime(const DateTime& dt) {
    uint32_t h = 0;
    auto feed = [&](uint8_t b) { h = h * 31 + b; };
    feed(static_cast<uint8_t>(dt.year & 0xFF));
    feed(static_cast<uint8_t>((dt.year >> 8) & 0xFF));
    feed(dt.month); feed(dt.day);
    feed(dt.hour);  feed(dt.minute); feed(dt.second);
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

void Catalog::ensure_indexes_loaded_locked() {
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

void Catalog::ensure_indexes_loaded() {
    std::lock_guard<std::mutex> lk(cache_mutex_);
    ensure_indexes_loaded_locked();
}

bool Catalog::create_secondary_index(const std::string& index_name,
                                      const std::string& table_name,
                                      const std::string& column_name)
{
    ensure_indexes_loaded();  // ensure existing indexes are in memory before we add a new one
    Table* table = get_table(table_name);
    if (!table) {
        LOG_WARN("Catalog", "Table '" + table_name + "' not found for index creation");
        return false;
    }

    const auto& cols = table->get_columns();
    int col_idx = -1;
    for (int i = 0; i < (int)cols.size(); i++) {
        if (cols[i].name == column_name) { col_idx = i; break; }
    }
    if (col_idx == -1) {
        LOG_WARN("Catalog", "Column '" + column_name + "' not found in table '" + table_name + "'");
        return false;
    }
    bool is_int = cols[col_idx].type == DataType::INT;
    bool is_varchar = cols[col_idx].type == DataType::VARCHAR;
    bool is_number = cols[col_idx].type == DataType::NUMBER;
    bool is_datetime = cols[col_idx].type == DataType::DATE;
    if (!is_int && !is_varchar && !is_number && !is_datetime) {
        LOG_WARN("Catalog", "Secondary indexes not supported on this column type");
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
        } else if (is_number && std::holds_alternative<double>(row[col_idx])) {
            double v = std::get<double>(row[col_idx]);
            uint32_t col_key = hash_number(v);
            std::vector<uint8_t> d(8);
            std::memcpy(d.data(), &v, 8);
            idx_btree->insert(col_key, pk, d.data(), 8);
        } else if (is_datetime && std::holds_alternative<DateTime>(row[col_idx])) {
            const DateTime& dt = std::get<DateTime>(row[col_idx]);
            uint32_t col_key = hash_datetime(dt);
            std::vector<uint8_t> d = {
                static_cast<uint8_t>(dt.year & 0xFF),
                static_cast<uint8_t>((dt.year >> 8) & 0xFF),
                dt.month, dt.day, dt.hour, dt.minute, dt.second
            };
            idx_btree->insert(col_key, pk, d.data(), static_cast<uint16_t>(d.size()));
        }
    }

    uint32_t final_root = idx_btree->get_root_page_id();

    std::vector<uint8_t> entry = serialize_index_entry(final_root, table_name, column_name, index_name);
    uint32_t key = hash_table_name("__idx__" + index_name);
    if (!catalog_btree->insert(key, 0, entry.data(), static_cast<uint16_t>(entry.size()))) {
        LOG_ERROR("Catalog", "Failed to insert index '" + index_name + "' into catalog");
        return false;
    }

    // Cache
    index_btrees_cache_[index_name] = std::move(idx_btree);
    loaded_indexes_.push_back({index_name, table_name, column_name, final_root});

    LOG_DEBUG("Catalog", "Created index '" + index_name + "' on " + table_name + "." + column_name);
    return true;
}

bool Catalog::drop_index(const std::string& index_name)
{
    ensure_indexes_loaded();

    bool found = false;
    for (const auto& info : loaded_indexes_) {
        if (info.index_name == index_name) { found = true; break; }
    }
    if (!found) {
        LOG_WARN("Catalog", "Index '" + index_name + "' not found");
        return false;
    }

    BTree* idx_btree = get_index_btree(index_name);
    if (idx_btree) {
        idx_btree->free_all_pages();
    }

    catalog_btree->remove(hash_table_name("__idx__" + index_name), 0);

    // In the end clear the cache and loaded indexes
    {
        std::lock_guard<std::mutex> lk(cache_mutex_);
        index_btrees_cache_.erase(index_name);
        loaded_indexes_.erase(
            std::remove_if(loaded_indexes_.begin(), loaded_indexes_.end(),
                           [&](const IndexInfo& i){ return i.index_name == index_name; }),
            loaded_indexes_.end());
    }

    LOG_DEBUG("Catalog", "Dropped index '" + index_name + "'");
    return true;
}

bool Catalog::drop_table(const std::string& name)
{
    if (!table_exists(name)) {
        LOG_WARN("Catalog", "Table '" + name + "' does not exist");
        return false;
    }

    // FK check
    auto refs = get_referencing_tables(name);
    if (!refs.empty()) {
        LOG_ERROR("Catalog", "Cannot drop table '" + name + "': referenced by '" + refs[0].child_table + "'");
        return false;
    }

    // Cascade-drop all secondary indexe
    auto indexes = get_indexes_for_table(name);
    for (const auto& idx : indexes) {
        drop_index(idx.index_name);
    }

    Table* tbl = get_table(name);
    if (tbl) {
        tbl->get_btree().free_all_pages();
    }

    catalog_btree->remove(hash_table_name(name), 0);

    uint32_t root_id = tbl ? tbl->get_btree().get_root_page_id() : 0;
    {
        std::lock_guard<std::mutex> lk(cache_mutex_);
        tables_cache.erase(name);
        btrees_cache.erase(name);
        if (root_id != 0) root_page_to_name_.erase(root_id);
    }

    LOG_DEBUG("Catalog", "Dropped table '" + name + "'");
    return true;
}

bool Catalog::alter_table_rename_column(
    const std::string& table_name,
    const std::string& old_name,
    const std::string& new_name
)
{
    Table* tbl = get_table(table_name);
    if (!tbl) {
        LOG_WARN("Catalog", "Table '" + table_name + "' not found");
        return false;
    }

    std::vector<ColumnDefinition> cols = tbl->get_columns();
    int col_idx = -1;
    for (int i = 0; i < (int)cols.size(); i++) {
        if (cols[i].name == old_name) { col_idx = i; break; }
    }
    if (col_idx == -1) {
        LOG_WARN("Catalog", "Column '" + old_name + "' not found in '" + table_name + "'");
        return false;
    }
    for (const auto& c : cols) {
        if (c.name == new_name) {
            LOG_WARN("Catalog", "Column '" + new_name + "' already exists in '" + table_name + "'");
            return false;
        }
    }

    cols[col_idx].name = new_name;

    uint32_t key = hash_table_name(table_name);
    auto old_data = catalog_btree->find(key, 0);
    if (!old_data.has_value()) return false;
    uint32_t root_page_id, created_at, version;
    std::vector<ColumnDefinition> dummy_schema;
    std::string recovered_name;
    if (!deserialize_catalog_entry(old_data.value(), root_page_id, created_at, version, dummy_schema, recovered_name))
        return false;
    catalog_btree->remove(key, 0);
    auto new_data = serialize_catalog_entry(root_page_id, created_at, version, cols, table_name);
    if (!catalog_btree->insert(key, 0, new_data.data(), static_cast<uint16_t>(new_data.size())))
        return false;

    // Evict table cache to force schema reload
    {
        std::lock_guard<std::mutex> lk(cache_mutex_);
        tables_cache.erase(table_name);
        btrees_cache.erase(table_name);
    }

    LOG_DEBUG("Catalog", "Renamed column '" + old_name + "' to '" + new_name + "' in '" + table_name + "'");
    return true;
}

bool Catalog::alter_table_add_column(const std::string& table_name, const ColumnDefinition& col)
{
    Table* tbl = get_table(table_name);
    if (!tbl) {
        LOG_WARN("Catalog", "Table '" + table_name + "' not found");
        return false;
    }

    // name conflict
    for (const auto& c : tbl->get_columns()) {
        if (c.name == col.name) {
            LOG_WARN("Catalog", "Column '" + col.name + "' already exists in '" + table_name + "'");
            return false;
        }
    }

    std::vector<Row> old_rows = tbl->scan_all();

    for (const Row& row : old_rows) {
        tbl->remove_row(tbl->extract_primary_key(row));
    }

    std::vector<ColumnDefinition> new_schema = tbl->get_columns();
    new_schema.push_back(col);

    uint32_t key = hash_table_name(table_name);
    auto old_data = catalog_btree->find(key, 0);
    if (!old_data.has_value()) return false;
    uint32_t root_page_id, created_at, version;
    std::vector<ColumnDefinition> dummy;
    std::string recovered_name;
    if (!deserialize_catalog_entry(old_data.value(), root_page_id, created_at, version, dummy, recovered_name))
        return false;
    catalog_btree->remove(key, 0);
    auto new_data = serialize_catalog_entry(root_page_id, created_at, version, new_schema, table_name);
    if (!catalog_btree->insert(key, 0, new_data.data(), static_cast<uint16_t>(new_data.size())))
        return false;

    {
        std::lock_guard<std::mutex> lk(cache_mutex_);
        tables_cache.erase(table_name);
        btrees_cache.erase(table_name);
    }

    Table* fresh = get_table(table_name);
    if (!fresh) return false;
    Value default_val;
    if (col.is_nullable) {
        default_val = std::monostate{};
    } else {
        switch (col.type) {
            case DataType::INT: default_val = int32_t{0}; break;
            case DataType::NUMBER: default_val = double{0.0}; break;
            case DataType::VARCHAR: default_val = std::string{}; break;
            case DataType::BOOLEAN: default_val = bool{false}; break;
            case DataType::DATE: default_val = DateTime{}; break;
        }
    }
    for (Row row : old_rows) {
        row.push_back(default_val);
        fresh->insert_row(row);
    }

    LOG_DEBUG("Catalog", "Added column '" + col.name + "' to '" + table_name + "'");
    return true;
}

bool Catalog::alter_table_drop_column(const std::string& table_name, const std::string& col_name)
{
    Table* tbl = get_table(table_name);
    if (!tbl) {
        LOG_WARN("Catalog", "Table '" + table_name + "' not found");
        return false;
    }

    const auto& old_cols = tbl->get_columns();
    int col_idx = -1;
    for (int i = 0; i < (int)old_cols.size(); i++) {
        if (old_cols[i].name == col_name) { col_idx = i; break; }
    }
    if (col_idx == -1) {
        LOG_WARN("Catalog", "Column '" + col_name + "' not found in '" + table_name + "'");
        return false;
    }
    if (old_cols[col_idx].is_primary_key) {
        LOG_ERROR("Catalog", "Cannot drop PRIMARY KEY column '" + col_name + "'");
        return false;
    }

    auto refs = get_referencing_tables(table_name);
    for (const auto& ref : refs) {
        if (ref.parent_column_name == col_name) {
            LOG_ERROR("Catalog", "Cannot drop column '" + col_name + "': referenced by '" +
                      ref.child_table + "." + ref.fk_column_name + "'");
            return false;
        }
    }

    auto idx_info = find_index_for_column(table_name, col_name);
    if (idx_info.has_value()) {
        drop_index(idx_info->index_name);
        tbl = get_table(table_name);
        if (!tbl) return false;
    }

    std::vector<Row> old_rows = tbl->scan_all();

    for (const Row& row : old_rows) {
        tbl->remove_row(tbl->extract_primary_key(row));
    }

    std::vector<ColumnDefinition> new_schema = tbl->get_columns();
    new_schema.erase(new_schema.begin() + col_idx);

    uint32_t key = hash_table_name(table_name);
    auto old_data = catalog_btree->find(key, 0);
    if (!old_data.has_value()) return false;
    uint32_t root_page_id, created_at, version;
    std::vector<ColumnDefinition> dummy;
    std::string recovered_name;
    if (!deserialize_catalog_entry(old_data.value(), root_page_id, created_at, version, dummy, recovered_name))
        return false;
    catalog_btree->remove(key, 0);
    auto new_data = serialize_catalog_entry(root_page_id, created_at, version, new_schema, table_name);
    if (!catalog_btree->insert(key, 0, new_data.data(), static_cast<uint16_t>(new_data.size())))
        return false;

    // Evict cache
    {
        std::lock_guard<std::mutex> lk(cache_mutex_);
        tables_cache.erase(table_name);
        btrees_cache.erase(table_name);
    }

    Table* fresh = get_table(table_name);
    if (!fresh) return false;
    for (Row row : old_rows) {
        row.erase(row.begin() + col_idx);
        fresh->insert_row(row);
    }

    LOG_DEBUG("Catalog", "Dropped column '" + col_name + "' from '" + table_name + "'");
    return true;
}

std::vector<std::string> Catalog::get_all_table_names() {
    std::vector<std::string> names;
    uint32_t page_id = catalog_btree->find_first_leaf_node();
    while (page_id != 0) {
        Page* page = pager.get_page(page_id);
        PageHeader* ph = reinterpret_cast<PageHeader*>(page->data);
        SlottedPage sp(page->data);
        uint16_t* ptrs = sp.get_cell_pointers();
        for (uint16_t i = 0; i < ph->num_cells; i++) {
            LeafCellHeader* lch = reinterpret_cast<LeafCellHeader*>(page->data + ptrs[i]);
            std::vector<char> raw;
            if (lch->flags & CELL_FLAG_OVERFLOW) {
                uint32_t ovfl;
                std::memcpy(&ovfl, page->data + ptrs[i] + LEAF_CELL_HEADER_SIZE, sizeof(uint32_t));
                raw = SlottedPage::read_from_overflow(ovfl, pager);
            } else {
                const char* dp = page->data + ptrs[i] + LEAF_CELL_HEADER_SIZE;
                raw.assign(dp, dp + lch->data_size);
            }
            if (raw.empty() || static_cast<uint8_t>(raw[0]) != ENTRY_TYPE_TABLE) continue;
            uint32_t root_page_id, created_at, version;
            std::vector<ColumnDefinition> schema;
            std::string name;
            if (deserialize_catalog_entry(raw, root_page_id, created_at, version, schema, name) && !name.empty())
                names.push_back(name);
        }
        page_id = ph->right_child_page_id;
    }
    return names;
}

std::vector<IndexInfo> Catalog::get_all_indexes() {
    ensure_indexes_loaded();
    return loaded_indexes_;
}

BTree* Catalog::get_index_btree(const std::string& index_name) {
    std::lock_guard<std::mutex> lk(cache_mutex_);

    auto it = index_btrees_cache_.find(index_name);
    if (it != index_btrees_cache_.end()) return it->second.get();

    ensure_indexes_loaded_locked();
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
        LOG_WARN("Catalog", "Table '" + name + "' already exists");
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
        LOG_ERROR("Catalog", "Failed to insert table '" + name + "' into catalog");
        pager.free_page(table_root_page);
        return false;
    }

    LOG_DEBUG("Catalog", "Created table '" + name + "' with root page " + std::to_string(table_root_page));
    root_page_to_name_[table_root_page] = name;
    return true;
}

Table* Catalog::get_table(const std::string& name) {
    std::lock_guard<std::mutex> lk(cache_mutex_);

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
        LOG_ERROR("Catalog", "Failed to deserialize table '" + name + "'");
        return nullptr;
    }
    
    auto btree = std::make_unique<BTree>(pager, root_page_id, false);
    auto table = std::make_unique<Table>(name, schema, *btree);
    
    // cache
    Table* table_ptr = table.get();
    
    btrees_cache[name] = std::move(btree);
    tables_cache[name] = std::move(table);
    
    LOG_DEBUG("Catalog", "Loaded table '" + name + "' from disk (root page " + std::to_string(root_page_id) + ")");
    root_page_to_name_[root_page_id] = name;
    return table_ptr;
}

Table* Catalog::get_table_by_root_page(uint32_t root_page_id)
{
    // Eagerly load every table so root_page_to_name_ is fully populated.
    for (const auto& name : get_all_table_names())
        get_table(name);
    auto it = root_page_to_name_.find(root_page_id);
    if (it == root_page_to_name_.end()) return nullptr;
    return get_table(it->second);
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
        LOG_WARN("Catalog", "Parent table '" + table_name + "' does not exist");
        return false;
    }

    if (!std::holds_alternative<int32_t>(value)) {
        LOG_WARN("Catalog", "Foreign key value must be an integer (primary key)");
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

            // Skip index entries (0x02) — they live in the same BTree but are not table entries
            if (!raw_data.empty() && static_cast<uint8_t>(raw_data[0]) != ENTRY_TYPE_TABLE) continue;

            uint32_t root_page_id, created_at, version;
            std::vector<ColumnDefinition> schema;
            std::string table_name_from_entry;
            if(!deserialize_catalog_entry(raw_data, root_page_id, created_at, version, schema, table_name_from_entry)){
                LOG_ERROR("Catalog", "Failed to deserialize catalog entry");
                continue;
            }
            // Resolve table name prefer the name embedded in the entry,
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
                LOG_WARN("Catalog", "Could not resolve name for table with root page " + std::to_string(root_page_id) + "; skipping FK check");
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

// table_locks_map_mutex_ is held only long enough to retrieve (or create)
// the per table shared_mutex pointer;
// the table mutex itself is then acquired
// outside the map lock so we never hold two locks simultaneously.

std::shared_mutex* Catalog::ensure_table_lock(const std::string& name)
{
    std::lock_guard<std::mutex> lk(table_locks_map_mutex_);
    auto it = table_locks_.find(name);
    if (it != table_locks_.end()) return it->second.get();
    table_locks_[name] = std::make_unique<std::shared_mutex>();
    return table_locks_[name].get();
}

void Catalog::lock_table_shared(const std::string& name)
{
    ensure_table_lock(name)->lock_shared();
}

void Catalog::lock_table_exclusive(const std::string& name)
{
    ensure_table_lock(name)->lock();
}

void Catalog::unlock_table_shared(const std::string& name)
{
    std::shared_mutex* m = nullptr;
    {
        std::lock_guard<std::mutex> lk(table_locks_map_mutex_);
        auto it = table_locks_.find(name);
        if (it != table_locks_.end()) m = it->second.get();
    }
    if (m) m->unlock_shared();
}

void Catalog::unlock_table_exclusive(const std::string& name)
{
    std::shared_mutex* m = nullptr;
    {
        std::lock_guard<std::mutex> lk(table_locks_map_mutex_);
        auto it = table_locks_.find(name);
        if (it != table_locks_.end()) m = it->second.get();
    }
    if (m) m->unlock();
}