#pragma once

#include "Table.h"
#include "BTree.h"
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <ctime>
#include <vector>

struct FKReference {
    std::string child_table;
    std::string fk_column_name;
    std::string parent_column_name;
};

struct IndexInfo {
    std::string index_name;
    std::string table_name;
    std::string column_name;
    uint32_t    root_page_id;
};

class Catalog {
private:
    Pager& pager;
    std::unique_ptr<BTree> catalog_btree;

    std::map<std::string, std::unique_ptr<BTree>> btrees_cache;
    std::map<std::string, std::unique_ptr<Table>> tables_cache;
    std::map<uint32_t, std::string> root_page_to_name_;

    // Secondary indexes
    std::map<std::string, std::unique_ptr<BTree>> index_btrees_cache_;
    std::vector<IndexInfo> loaded_indexes_;
    bool indexes_loaded_ = false;

    mutable std::shared_mutex rw_mutex_;
    mutable std::mutex cache_mutex_;

    mutable std::mutex table_locks_map_mutex_;
    std::unordered_map<std::string, std::unique_ptr<std::shared_mutex>> table_locks_;
    std::shared_mutex* ensure_table_lock(const std::string& name);

    static uint32_t hash_table_name(const std::string& name);
    void ensure_indexes_loaded_locked();
public:
    static uint32_t hash_varchar(const std::string& s);
    static uint32_t hash_number(double v);
    static uint32_t hash_datetime(const DateTime& dt);
private:

    // Catalog entry format:
    //   Table: [1B type=0x01][4B root_page_id][4B created_at][4B version][2B schema_size][schema][1B name_len][name]
    //   Index: [1B type=0x02][4B root_page_id][1B table_name_len][table_name][1B col_name_len][col_name][1B index_name_len][index_name]
    std::vector<uint8_t> serialize_catalog_entry(
        uint32_t root_page_id, uint32_t created_at,
        uint32_t version, const std::vector<ColumnDefinition>& schema,
        const std::string& table_name);

    bool deserialize_catalog_entry(
        const std::vector<char>& data, uint32_t& root_page_id,
        uint32_t& created_at, uint32_t& version,
        std::vector<ColumnDefinition>& schema,
        std::string& table_name);

    std::vector<uint8_t> serialize_index_entry(
        uint32_t root_page_id,
        const std::string& table_name,
        const std::string& column_name,
        const std::string& index_name);

    bool deserialize_index_entry(
        const std::vector<char>& data,
        uint32_t& root_page_id,
        std::string& table_name,
        std::string& column_name,
        std::string& index_name);

    void ensure_indexes_loaded();

public:
    using ReadLock  = std::shared_lock<std::shared_mutex>;
    using WriteLock = std::unique_lock<std::shared_mutex>;
    ReadLock  acquire_read()  const { return ReadLock(rw_mutex_);  }
    WriteLock acquire_write()       { return WriteLock(rw_mutex_); }

    // Locks are held until the transaction COMMIT or ROLLBACK
    void lock_table_shared(const std::string& name);
    void lock_table_exclusive(const std::string& name);
    void unlock_table_shared(const std::string& name);
    void unlock_table_exclusive(const std::string& name);

    Catalog(Pager& p);

    bool create_table(const std::string& name, const std::vector<ColumnDefinition>& cols);
    Table* get_table(const std::string& name);
    Table* get_table_by_root_page(uint32_t root_page_id); // used by recovery + ROLLBACK undo
    bool table_exists(const std::string& name);

    // Secondary indexes
    bool create_secondary_index(const std::string& index_name,
                                const std::string& table_name,
                                const std::string& column_name);
    BTree* get_index_btree(const std::string& index_name);
    std::optional<IndexInfo> find_index_for_column(const std::string& table_name,
                                                    const std::string& column_name);
    std::vector<IndexInfo> get_indexes_for_table(const std::string& table_name);
    std::vector<std::string> get_all_table_names();
    std::vector<IndexInfo>   get_all_indexes();

    bool drop_table(const std::string& name);
    bool drop_index(const std::string& index_name);
    bool alter_table_add_column(const std::string& table_name, const ColumnDefinition& col);
    bool alter_table_drop_column(const std::string& table_name, const std::string& col_name);
    bool alter_table_rename_column(const std::string& table_name,
                                   const std::string& old_name,
                                   const std::string& new_name);

    // FK enforcement
    bool fk_value_exists(const std::string& table_name, const std::string& column_name, const Value& value);

    bool child_has_fk_value(const std::string& child_table,
                            const std::string& fk_column_name,
                            const Value& value);

    std::vector<FKReference> get_referencing_tables(const std::string& parent_table_name);

};