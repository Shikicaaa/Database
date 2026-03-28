#pragma once
#include <iostream>
#include <cstdint>
#include <cstring>
#include <vector>
#include <optional>
#include <utility>
#include "Pager.h"

const uint8_t CELL_FLAG_NORMAL   = 0x00;
const uint8_t CELL_FLAG_OVERFLOW = 0x01;


const uint16_t OVERFLOW_THRESHOLD = 2048;
#pragma pack(push, 1)
struct OverflowPageHeader {
    uint32_t total_data_size;
    uint32_t chunk_size;
    uint32_t next_page_id;
};
#pragma pack(pop)

const uint16_t OVERFLOW_PAGE_DATA_SIZE = PAGE_SIZE - sizeof(OverflowPageHeader);

enum NodeType { INTERNAL = 0, LEAF = 1};
/*
[NODE_TYPE 1B][IS_ROOT 1B][NUM_CELLS 2B][PARENT_PAGE_ID 4B][RIGHT_CHILD_ID 4B][FREE_START 2B][FREE_END 2B] = TOTAL 16B
*/
#pragma pack(push, 1)
struct PageHeader{
    uint8_t node_type;
    uint8_t is_root;            
    uint16_t num_cells;         // number of records
    uint32_t parent_page_id;
    uint32_t right_child_page_id; // pointer to the next page to the right of the cell   
    uint16_t free_start;        // where do pointers end
    uint16_t free_end;          // where does the info start
};
#pragma pack(pop)

// [key (4B)][row_id (4B)][flags (1B)][data_size (2B)][overflow_page_id (4B)][data]  = 11B + data
//
// flags & CELL_FLAG_OVERFLOW:
//   data = [overflow_page_id (4B)]


#pragma pack(push, 1)
struct LeafCellHeader {
    uint32_t key;
    uint32_t row_id;
    uint8_t flags;
    uint16_t data_size;
};
#pragma pack(pop)

const uint16_t LEAF_CELL_HEADER_SIZE = sizeof(LeafCellHeader);  // 11 bytes

struct InternalNodeCell {
    uint32_t key;
    uint32_t row_id;
    uint32_t page_id;
};



class SlottedPage{
    private:
        char* data;



    public:

        inline PageHeader* header() { return reinterpret_cast<PageHeader*>(data); }
        inline uint16_t* get_cell_pointers() { return reinterpret_cast<uint16_t*>(data + sizeof(PageHeader)); }
        inline int compare_keys(uint32_t key1, uint32_t row_1, uint32_t key2, uint32_t row_2) {
            if (key1 < key2) return -1;
            if (key1 > key2) return 1;
            if (row_1 < row_2) return -1;
            if (row_1 > row_2) return 1;
            return 0;
        }
        SlottedPage(char* page_data);

        bool insert_record(uint32_t key, uint32_t row_id, const void* data, uint16_t data_size);
        bool insert_internal_cell(uint32_t key, uint32_t row_id, uint32_t page_id);

        bool update_record(uint32_t key, uint32_t row_id,
                          const void* new_data, uint16_t new_size,
                          Pager& pager);

        bool delete_record(uint32_t key, uint32_t row_id, Pager& pager);

        static uint32_t write_to_overflow(const void* data, uint16_t size, Pager& pager);

        static std::vector<char> read_from_overflow(uint32_t overflow_page_id, Pager& pager);
        static void free_overflow_pages(uint32_t first_overflow_page_id, Pager& pager);

        std::optional<std::vector<char>> get_record(uint32_t key, uint32_t row_id, Pager& pager);

        std::optional<std::vector<char>> get_record(uint32_t key, uint32_t row_id);


        void init_as_leaf_node(bool is_root);
        std::pair<uint32_t, uint32_t> get_first_key_and_row_id();
        void init_as_internal_node(bool is_root);

        uint32_t lookup_internal(uint32_t target_key, uint32_t target_row_id);

        uint32_t move_half(SlottedPage* dst);
        void recalculate_space();
        void compact();
};