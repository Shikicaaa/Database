#pragma once
#include <iostream>
#include <cstdint>
#include <cstring>
#include <vector>
#include <optional>
#include "Pager.h"


enum NodeType { INTERNAL = 0, LEAF = 1};
/*
[NODE_TYPE 1B][IS_ROOT 1B][NUM_CELLS 2B][PARENT_PAGE_ID 4B][FREE_START 2B][FREE_END 2B] = TOTAL 12B
*/
#pragma pack(push, 1)
struct PageHeader{
    uint8_t node_type;
    uint8_t is_root;            
    uint16_t num_cells;         // number of records
    uint32_t parent_page_id;
    uint32_t right_child_page_id; // ONLY IF NODE_TYPE IS INTERNAL OTHERWISE (IF LEAF) IT IS SET TO EITHER 0 OR NEXT LEAF ID    
    uint16_t free_start;        // where do pointers end
    uint16_t free_end;          // where does the info start

};
#pragma pack(pop)

struct Cell{
    uint32_t key;
    uint16_t value_size;
};

struct InternalNodeCell {
    uint32_t key;
    uint32_t page_id;
};



class SlottedPage{
    private:
        char* data;

        inline PageHeader* header() { return reinterpret_cast<PageHeader*>(data); }

        inline uint16_t* get_cell_pointers() { return reinterpret_cast<uint16_t*>(data + sizeof(PageHeader)); }

    public:
        SlottedPage(char* page_data);
        bool insert_record(uint32_t key, const void* data, uint16_t data_size);
        void init_as_leaf_node(bool is_root);
        void init_as_internal_node(bool is_root);


        std::optional<std::vector<char>> get_record(uint32_t key);

        uint32_t move_half(SlottedPage* dst);
};