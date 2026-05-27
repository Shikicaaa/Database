#include "Cursor.h"

Cursor::Cursor(Table& table) 
    : table_(table), 
      pager_(table.get_btree().get_pager()),
      cell_index_(0), 
      eof_(false) 
{
    current_page_id_ = table_.get_btree().find_first_leaf_node();
    
    Page* page = pager_.get_page(current_page_id_);
    SlottedPage sp(page->data);
    if (sp.header()->num_cells == 0) {
        eof_ = true;
    }
}

bool Cursor::advance() {
    if (eof_) return false;

    Page* page = pager_.get_page(current_page_id_);
    SlottedPage sp(page->data);
    PageHeader* h = sp.header();
    if(cell_index_ + 1 < h->num_cells){
        cell_index_++;
        return true;
    } else if(cell_index_ + 1 == h->num_cells){
        uint32_t next_page_id = h->right_child_page_id;
        if(next_page_id != 0){
            current_page_id_ = next_page_id;
            cell_index_ = 0;
            return true;
        } else {
            eof_ = true;
            return false;
        }
    } else {
        eof_ = true;
        return false;
    }
}

std::optional<std::vector<char>> Cursor::get_raw_current_record() {
    if (eof_) return std::nullopt;

    std::vector<char> raw_data;
    
    Page* page = pager_.get_page(current_page_id_);
    SlottedPage sp(page->data);

    uint16_t* pointers = sp.get_cell_pointers();
    LeafCellHeader* cell_header = reinterpret_cast<LeafCellHeader*>(page->data + pointers[cell_index_]);
    
    if (cell_header->flags == CELL_FLAG_OVERFLOW) {
        uint32_t overflow_page_id;
        std::memcpy(&overflow_page_id, page->data + pointers[cell_index_] + sizeof(LeafCellHeader), sizeof(uint32_t));
        return SlottedPage::read_from_overflow(overflow_page_id, pager_);
    } else {
        raw_data.resize(cell_header->data_size);
        std::memcpy(raw_data.data(), page->data + pointers[cell_index_] + sizeof(LeafCellHeader), cell_header->data_size);
        return raw_data;
    }
}

std::optional<Row> Cursor::current_row() {
    auto raw_record = get_raw_current_record();
    if (!raw_record) return std::nullopt;
    return table_.get_serializer().deserialize(
        table_.get_columns(), 
        reinterpret_cast<const uint8_t*>(raw_record.value().data()),
        raw_record.value().size()
    );
}