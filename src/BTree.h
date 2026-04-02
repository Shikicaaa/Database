#pragma once
#include "Pager.h"
#include <optional>

class BTree {
private:
    Pager& pager;
    uint32_t root_page_id;
    bool use_catalog_root; // true = updates catalog_root_page_id, false = updates root_page_id

    void split_leaf_node(uint32_t old_node_id);
    void split_internal_node(uint32_t old_node_id);
    bool insert_into_leaf(uint32_t leaf_id, uint32_t key, uint32_t row_id, const void* data, uint16_t size);
    void insert_into_parent(Page* old_node, uint32_t old_node_id, uint32_t key, uint32_t row_id, uint32_t new_node_id);
    uint32_t find_leaf_node(uint32_t leaf_key, uint32_t row_id);
public:
    BTree(Pager& p);
    BTree(Pager& p, uint32_t explicit_root_page_id, bool is_catalog = false);

    bool insert(uint32_t key, uint32_t row_id, const void* data, uint16_t size);
    std::optional<std::vector<char>> find(uint32_t key, uint32_t row_id);
    bool update(uint32_t key, uint32_t row_id, const void* data, uint16_t size);
    bool remove(uint32_t key, uint32_t row_id);

    // bool unique_check(uint32_t key, uint32_t row_id);
    
    uint32_t get_root_page_id() const { return root_page_id; }
};