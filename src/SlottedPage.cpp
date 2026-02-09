#include "SlottedPage.h"

SlottedPage::SlottedPage(char *page_data) : data(page_data)
{
    PageHeader* h = header();
    h->num_cells = 0;
    h->free_start = sizeof(PageHeader);
    h->free_end = PAGE_SIZE;
}

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
    pointers[h->num_cells] = data_offset;

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
