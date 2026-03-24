#include "SlottedPage.h"

SlottedPage::SlottedPage(char *page_data) : data(page_data) {}

/*
PAGE

Byte 0
||
\/
[HEADER(16B)][POINTER1][POINTER2]...FREE SPACE...[DATA2][DATA1]



Insertion works like this:

First we calculate how much space we need:
4B key + 4B row_id + 1B flags + 2B size + data = 11B + data
After that we add +2B for pointer at the beginning.
Now we check if there is some free space, if free space doesn't exist we cannot put something inside, therefore
we need to split the page. This will be implemented later on.
We now reserve space at the end and write a pointer at the beginning. Then we insert actual data, this is serialization
Then we copy key, flags, value size and value.
And at the end we update the header.
*/
bool SlottedPage::insert_record(uint32_t key, uint32_t row_id, const void *record_data, uint16_t data_size)
{
    PageHeader* h = header();

    // key(4) + row_id(4) + flags(1) + data_size(2) + data
    uint16_t size_needed = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t) + data_size;

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
    for(;insert_index < h->num_cells; insert_index++){
        uint16_t offset = pointers[insert_index];

        uint32_t existing_key;
        uint32_t existing_row_id;

        std::memcpy(&existing_key, data + offset, sizeof(uint32_t));
        std::memcpy(&existing_row_id, data + offset + sizeof(uint32_t), sizeof(uint32_t));

        if (compare_keys(key, row_id, existing_key, existing_row_id) < 0) {
            break;
        }
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

    std::memcpy(cell_ptr, &row_id, sizeof(uint32_t));
    cell_ptr += sizeof(uint32_t);

    uint8_t flags = CELL_FLAG_NORMAL;
    std::memcpy(cell_ptr, &flags, sizeof(uint8_t));
    cell_ptr += sizeof(uint8_t);

    std::memcpy(cell_ptr, &data_size, sizeof(uint16_t));
    cell_ptr += sizeof(uint16_t);

    std::memcpy(cell_ptr, record_data, data_size);

    h->num_cells++;
    h->free_start += sizeof(uint16_t);

    return true;

}

bool SlottedPage::insert_internal_cell(uint32_t key, uint32_t row_id, uint32_t page_id){
    PageHeader* h = header();

    uint16_t cell_size = sizeof(InternalNodeCell);
    uint16_t space_needed = cell_size + sizeof(uint16_t);

    if(space_needed > h->free_end - h->free_start){
        return false;
    }

    h->free_end -= cell_size;
    uint16_t offset = h->free_end;

    uint16_t* pointers = get_cell_pointers();

    int insert_index = 0;
    for(int i = 0; i<h->num_cells;i++){
        InternalNodeCell* existingcell = reinterpret_cast<InternalNodeCell*>(data + pointers[i]);
        if(compare_keys(key, row_id, existingcell->key, existingcell->row_id) < 0){
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

    pointers[insert_index] = offset;

    InternalNodeCell cell = {key, row_id, page_id};
    std::memcpy(data+offset, &cell, sizeof(InternalNodeCell));
    h->num_cells++;
    h->free_start += sizeof(uint16_t);

    return true;
}

bool SlottedPage::update_record(uint32_t key, uint32_t row_id, const void *new_data, uint16_t new_size, Pager &pager)
{
    PageHeader* h = header();
    uint16_t* pointers = get_cell_pointers();

    bool found = false;
    int cell_index = 0;
    
    uint32_t cell_key;
    uint32_t cell_row_id;
    uint8_t flags;
    uint16_t data_size;
    
    while(cell_index < h->num_cells){
        uint16_t offset = pointers[cell_index];
        char* cell_ptr = data + offset;


        std::memcpy(&cell_key, cell_ptr, sizeof(uint32_t));
        cell_ptr += sizeof(uint32_t);

        std::memcpy(&cell_row_id, cell_ptr, sizeof(uint32_t));
        cell_ptr += sizeof(uint32_t);

        std::memcpy(&flags, cell_ptr, sizeof(uint8_t));
        cell_ptr += sizeof(uint8_t);

        std::memcpy(&data_size, cell_ptr, sizeof(uint16_t));
        cell_ptr += sizeof(uint16_t);

        if(cell_key == key && cell_row_id == row_id){
            found = true;
            break;
        }
        cell_index++;
    }
    if(!found){
        return false;
    }

    if(flags == CELL_FLAG_OVERFLOW){
        uint32_t overflow_page_id;
        std::memcpy(&overflow_page_id, data + pointers[cell_index] + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t), sizeof(uint32_t));
        SlottedPage::free_overflow_pages(overflow_page_id, pager);
    }

    if(new_size <= data_size){
        std::memcpy(data + pointers[cell_index] + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t), new_data, new_size);
        uint8_t flags = CELL_FLAG_NORMAL;
        std::memcpy(data + pointers[cell_index] + sizeof(uint32_t) + sizeof(uint32_t), &flags, sizeof(uint8_t));
        uint16_t new_data_size = new_size;
        std::memcpy(data + pointers[cell_index] + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t), &new_data_size, sizeof(uint16_t));
    } else {
        uint32_t new_overflow_page_id = SlottedPage::write_to_overflow(new_data, new_size, pager);
        uint8_t flags = CELL_FLAG_OVERFLOW;
        std::memcpy(data + pointers[cell_index] + sizeof(uint32_t) + sizeof(uint32_t), &flags, sizeof(uint8_t));
        uint16_t overflow_data_size = sizeof(uint32_t);
        std::memcpy(data + pointers[cell_index] + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t), &overflow_data_size, sizeof(uint16_t));
        std::memcpy(data + pointers[cell_index] + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t), &new_overflow_page_id, sizeof(uint32_t));
    }

    return true;
}

uint32_t SlottedPage::write_to_overflow(const void *data, uint16_t size, Pager &pager)
{
    const char* src = static_cast<const char*>(data);
    uint16_t remaining = size;
    uint32_t first_overflow_page_id = 0;
    uint32_t prev_page_id = 0;

    while(remaining > 0){
        uint32_t overflow_page = pager.allocate_new_page();
        Page* page = pager.get_page(overflow_page);
        
        uint32_t chunk_size = std::min(uint32_t(remaining), uint32_t(OVERFLOW_PAGE_DATA_SIZE));

        if(first_overflow_page_id == 0){
            first_overflow_page_id = overflow_page;
        }

        char* page_data = page->data;
        OverflowPageHeader header = {size, chunk_size, 0};
        std::memcpy(page_data, &header, sizeof(OverflowPageHeader));
        std::memcpy(page_data + sizeof(OverflowPageHeader), src, chunk_size);

        if(prev_page_id != 0){
            Page* prev_page = pager.get_page(prev_page_id);
            OverflowPageHeader* prev_header = reinterpret_cast<OverflowPageHeader*>(prev_page->data);
            prev_header->next_page_id = overflow_page;
            prev_page->is_dirty = true;
        }

        src += chunk_size;

        page->is_dirty = true;
        remaining -= chunk_size;
        prev_page_id = overflow_page;
    }

    return first_overflow_page_id;
}

std::vector<char> SlottedPage::read_from_overflow(uint32_t overflow_page_id, Pager &pager)
{
    std::vector<char> result;
    uint32_t current_page_id = overflow_page_id;

    while(current_page_id != 0){
        Page* page = pager.get_page(current_page_id);
        OverflowPageHeader* header = reinterpret_cast<OverflowPageHeader*>(page->data);

        char* page_data = page->data + sizeof(OverflowPageHeader);
        result.insert(result.end(), page_data, page_data + header->chunk_size);

        current_page_id = header->next_page_id;
    }

    return result;
}

uint32_t SlottedPage::lookup_internal(uint32_t key, uint32_t row_id)
{
    PageHeader* h = header();
    uint16_t* pointers = get_cell_pointers();

    std::cout << "SP: Internal Lookup for key " << key << " inside Node with " << h->num_cells << " cells.\n";

    for(int i = 0; i < h->num_cells; i++){
        uint16_t offset = pointers[i];
        
       
        InternalNodeCell* cell = reinterpret_cast<InternalNodeCell*>(data + offset);

        
        /*
        We look for first key that is greater than the key in the argument.
        Internal node: [Ptr1] Key1 [Ptr2] Key2 [Ptr3]
        Our structure: {Key1, Ptr1}, {Key2, Ptr2} ... and Right Child at the end
        
        If we look for 10, but cell has key 30, key 10 belongs to the left child node
        in our case its Ptr1. If we go through every key and dont find the needed key we return the right child page id
        and then we can do lookup from there.
        */
        if (compare_keys(key, row_id, cell->key, cell->row_id) < 0) {
            return cell->page_id;
        }
    }

    return h->right_child_page_id;
}

std::optional<std::vector<char>> SlottedPage::get_record(uint32_t key, uint32_t row_id, Pager &pager)
{
    PageHeader* h = header();
    uint16_t* pointers = get_cell_pointers();

    for(int i = 0;i<h->num_cells;i++){
        uint16_t offset = pointers[i];

        char* cell_ptr = this->data + offset;

        uint32_t cell_key;
        uint32_t cell_row_id;
        uint8_t flags;
        uint16_t data_size;

        std::memcpy(&cell_key, cell_ptr, sizeof(uint32_t));
        cell_ptr += sizeof(uint32_t);

        std::memcpy(&cell_row_id, cell_ptr, sizeof(uint32_t));
        cell_ptr += sizeof(uint32_t);

        std::memcpy(&flags, cell_ptr, sizeof(uint8_t));
        cell_ptr += sizeof(uint8_t);

        std::memcpy(&data_size, cell_ptr, sizeof(uint16_t));
        cell_ptr += sizeof(uint16_t);

        if(cell_key == key && cell_row_id == row_id){
            if(flags == CELL_FLAG_NORMAL){
                std::vector<char> result(data_size);
                std::memcpy(result.data(), cell_ptr, data_size);
                return result;
            } else if (flags == CELL_FLAG_OVERFLOW) {
                uint32_t overflow_page_id;
                std::memcpy(&overflow_page_id, cell_ptr, sizeof(uint32_t));
                return read_from_overflow(overflow_page_id, pager);
            }
        }
    }
    return {};
}

std::optional<std::vector<char>> SlottedPage::get_record(uint32_t key, uint32_t row_id)
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
            uint32_t cell_row_id;
            uint16_t data_size;

            std::memcpy(&cell_row_id, cell_ptr, sizeof(uint32_t));
            cell_ptr += sizeof(uint32_t);

            uint8_t flags;
            std::memcpy(&flags, cell_ptr, sizeof(uint8_t));
            cell_ptr += sizeof(uint8_t);

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

        uint32_t row_id;
        std::memcpy(&row_id, cell_ptr + sizeof(uint32_t), sizeof(uint32_t));

        uint8_t flags;
        std::memcpy(&flags, cell_ptr + sizeof(uint32_t) + sizeof(uint32_t), sizeof(uint8_t));

        uint16_t data_len;
        std::memcpy(&data_len, cell_ptr+sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t), sizeof(uint16_t));

        char* record_data = cell_ptr + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t);
        
        if(is_first){
            separator_key = key;
            is_first = false;
        }

        dst->insert_record(key, row_id, record_data, data_len);
    }

    h->num_cells = split_cells;

    h->free_start = sizeof(PageHeader) + (split_cells * sizeof(uint16_t));

    recalculate_space();

    return separator_key;
}

void SlottedPage::recalculate_space()
{
    PageHeader* h = header();
    uint16_t* pointers = get_cell_pointers();
    if(h->num_cells == 0){
        h->free_end = PAGE_SIZE;
        return;
    }

    // shifting the offset to the right, so we can insert new record at the end of the page
    uint16_t min_offset = PAGE_SIZE;
    for(int i = 0;i<h->num_cells;i++){
        if(pointers[i] < min_offset){
            min_offset = pointers[i];
        }
    }

    h->free_end = min_offset;
}

void SlottedPage::compact()
{
    PageHeader* h = header();
    uint16_t* pointers = get_cell_pointers();

    if (h->num_cells == 0) {
        h->free_end = PAGE_SIZE;
        return;
    }

    struct CellData {
        uint32_t key;
        uint32_t row_id;
        uint16_t data_size;
        std::vector<char> record_data;
    };

    std::vector<CellData> cells;
    cells.reserve(h->num_cells);

    for (int i = 0; i < h->num_cells; i++) {
        uint16_t offset = pointers[i];
        char* cell_ptr = data + offset;

        CellData cell;
        std::memcpy(&cell.key, cell_ptr, sizeof(uint32_t));
        cell_ptr += sizeof(uint32_t);

        std::memcpy(&cell.row_id, cell_ptr, sizeof(uint32_t));
        cell_ptr += sizeof(uint32_t);

        uint8_t flags;
        std::memcpy(&flags, cell_ptr, sizeof(uint8_t));
        cell_ptr += sizeof(uint8_t);

        std::memcpy(&cell.data_size, cell_ptr, sizeof(uint16_t));
        cell_ptr += sizeof(uint16_t);

        cell.record_data.resize(cell.data_size);
        std::memcpy(cell.record_data.data(), cell_ptr, cell.data_size);

        cells.push_back(std::move(cell));
    }

    uint16_t write_offset = PAGE_SIZE;

    for (int i = 0; i < (int)cells.size(); i++) {
        const CellData& cell = cells[i];

        uint16_t cell_size = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t) + cell.data_size;

        write_offset -= cell_size;

        char* cell_ptr = data + write_offset;

        std::memcpy(cell_ptr, &cell.key, sizeof(uint32_t));
        cell_ptr += sizeof(uint32_t);

        std::memcpy(cell_ptr, &cell.row_id, sizeof(uint32_t));
        cell_ptr += sizeof(uint32_t);

        std::memcpy(cell_ptr, &cell.data_size, sizeof(uint16_t));
        cell_ptr += sizeof(uint16_t);

        std::memcpy(cell_ptr, cell.record_data.data(), cell.data_size);

        pointers[i] = write_offset;
    }

    h->free_end = write_offset;
}

void SlottedPage::init_as_leaf_node(bool is_root) {
    PageHeader* h = header();
    h->node_type = LEAF;
    h->is_root = is_root ? 1 : 0;
    h->num_cells = 0;
    h->parent_page_id = 0;
    h->right_child_page_id = 0;
    h->free_start = sizeof(PageHeader);
    h->free_end = PAGE_SIZE;
}

void SlottedPage::init_as_internal_node(bool is_root) {
    PageHeader* h = header();
    h->node_type = INTERNAL;
    h->is_root = is_root ? 1 : 0;
    h->num_cells = 0;
    h->parent_page_id = 0;
    h->right_child_page_id = 0;
    h->free_start = sizeof(PageHeader);
    h->free_end = PAGE_SIZE;
}

std::pair<uint32_t, uint32_t> SlottedPage::get_first_key_and_row_id() {
    PageHeader* h = header();
    if (h->num_cells == 0) return {0, 0};

    uint16_t* pointers = get_cell_pointers();
    uint16_t first_offset = pointers[0];

    uint32_t key;
    uint32_t row_id;
    std::memcpy(&key, data + first_offset, sizeof(uint32_t));
    std::memcpy(&row_id, data + first_offset + sizeof(uint32_t), sizeof(uint32_t));

    return {key, row_id};
}