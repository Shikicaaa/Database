#include "SlottedPage.h"

SlottedPage::SlottedPage(char *page_data) : data(page_data) {}

/*
PAGE

Byte 0
||
\/
[HEADER(6B)][POINTER1][POINTER2]...FREE SPACE...[DATA2][DATA1]



Insertion works like this:

First we calculate how much space we need, 4B for key, 2B for size and rest for actual data.
After that we add +2B for pointer at the beginning.
Now we check if there is some free space, if free space doesn't exist we cannot put something inside, therefore
we need to split the page. This will be implemented later on.
We now reserve space at the end and write a pointer at the beginning. Then we insert actual data, this is serialization
Then we copy key, value size and value.
And at the end we update the header.
*/
bool SlottedPage::insert_record(uint32_t key, const void *record_data, uint16_t data_size)
{
    PageHeader* h = header();

    uint16_t size_needed = sizeof(uint32_t) + sizeof(uint16_t) + data_size;

    uint16_t total_space_needed = size_needed + sizeof(uint16_t);


    // IF We dont have enough space we need to split the page
    uint16_t free_space = h->free_end - h->free_start;
    if(total_space_needed > free_space){
        return false;
    }

    h->free_end -= size_needed;
    uint16_t data_offset = h->free_end;

    uint16_t* pointers = get_cell_pointers();
    
    int insert_index = 0;

    for(int i = 0;i < h->num_cells;i++){
        uint32_t cell_key;
        char* cell_ptr = data + pointers[i];
        std::memcpy(&cell_key, cell_ptr, sizeof(uint32_t));

        if(cell_key > key){
            insert_index = i;
            break;
        }
        insert_index = i+1;
    }

    if(insert_index < h->num_cells){
        std::memmove(
            &pointers[insert_index+1],
            &pointers[insert_index],
            (h->num_cells - insert_index) * sizeof(uint16_t)
        );
    }

    pointers[insert_index] = data_offset;

    char* cell_ptr = this->data + data_offset;

    std::memcpy(cell_ptr, &key, sizeof(uint32_t));
    cell_ptr += sizeof(uint32_t);

    std::memcpy(cell_ptr, &data_size, sizeof(uint16_t));
    cell_ptr += sizeof(uint16_t);

    std::memcpy(cell_ptr, record_data, data_size);

    h->num_cells++;
    h->free_start += sizeof(uint16_t);

    return true;

}

std::optional<std::vector<char>> SlottedPage::get_record(uint32_t key)
{
    PageHeader* h = header();
    uint16_t* pointers = get_cell_pointers();

    for(int i = 0;i<h->num_cells;i++){
        uint16_t offset = pointers[i];

        char* cell_ptr = this->data + offset;

        uint32_t cell_key;
        std::memcpy(&cell_key, cell_ptr, sizeof(uint32_t));

        if(cell_key == key){
            cell_ptr += sizeof(uint32_t);
            uint16_t data_size;

            std::memcpy(&data_size, cell_ptr, sizeof(uint16_t));

            cell_ptr += sizeof(uint16_t);

            std::vector<char> result(data_size);
            std::memcpy(result.data(), cell_ptr, data_size);

            return result;
        }
    }
    return {};
}

uint32_t SlottedPage::move_half(SlottedPage *dst)
{
    PageHeader* h = header();
    uint16_t* p = get_cell_pointers();

    int total_cells = h->num_cells;
    int split_cells = total_cells / 2;

    uint32_t separator_key = 0;
    bool is_first = true;
    for(int i = split_cells;i < total_cells;i++){
        uint16_t offset = p[i];
        char* cell_ptr = data + offset;

        //key & datasize
        uint32_t key;
        std::memcpy(&key, cell_ptr, sizeof(uint32_t));

        uint16_t data_len;
        std::memcpy(&data_len, cell_ptr+sizeof(uint32_t), sizeof(uint16_t));

        char* record_data = cell_ptr + sizeof(uint32_t) + sizeof(uint16_t);
        
        if(is_first){
            separator_key = key;
            is_first = false;
        }

        dst->insert_record(separator_key, record_data, data_len);
    }

    h->num_cells = split_cells;

    h->free_start = sizeof(PageHeader) + sizeof(split_cells * sizeof(uint16_t));

    return separator_key;
}

void SlottedPage::init_as_leaf_node(bool is_root) {
    PageHeader* h = header();
    h->node_type = LEAF;
    h->is_root = is_root ? 1 : 0;
    h->num_cells = 0;
    h->parent_page_id = 0;
    h->free_start = sizeof(PageHeader);
    h->free_end = PAGE_SIZE;
}