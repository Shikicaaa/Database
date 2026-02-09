#include <iostream>
#include <cstdint>
#include <cstring>
#include "Pager.h"


struct PageHeader{
    uint16_t num_cells; // number of records
    uint16_t free_start; // where do pointers end
    uint16_t free_end; // where does the info start
};

struct Cell{
    uint32_t key;
    uint16_t value_size;
};

class SlottedPage{
    private:
        char* data;

        inline PageHeader* header() { return reinterpret_cast<PageHeader*>(data); }

        inline uint16_t* get_cell_pointers() { return reinterpret_cast<uint16_t*>(data + sizeof(PageHeader)); }

    public:
        SlottedPage(char* page_data);
        bool insert_record(uint32_t key, const void* data, uint16_t data_size);
};