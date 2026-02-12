#pragma once
#include "Pager.h"

class BTree {
private:
    Pager& pager;
    uint32_t root_page_id;

    void split_leaf_node(uint32_t old_node_id);
    void split_internal_node(uint32_t old_node_id); 
    void insert_into_leaf(uint32_t leaf_id, uint32_t key, const void* data, uint16_t size);

public:
    BTree(Pager& p);

    bool insert(uint32_t key, const void* data, uint16_t size);
    // std::vector<char> find(uint32_t key);
};