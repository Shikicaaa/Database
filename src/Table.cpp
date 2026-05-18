#include "Table.h"
#include "SlottedPage.h"

bool Table::insert_row(const Row &row)
{
    if (row.size() != columns.size())
    {
        std::cerr << "ERROR: Row size doesn't match table schema!" << std::endl;
        return false;
    }

    uint32_t primary_key = extract_primary_key(row);

    for (size_t i = 0; i < columns.size(); i++) {
        if (columns[i].is_primary_key || columns[i].is_unique) {
            // TODO: Ovo mora da se kasnije promeni, nije dovoljno da se samo proveri primary, vec i ostali,
            // + mora da se ubaci za unique kolone, a ne samo primary, odnosno da imamo btree index i za unique kolone
            // trenutno je ovo samo da bi se testirao unique constraint, ali kasnije ce biti bitno i za ostale kolone koje nisu primary key
            
            if (columns[i].is_primary_key) {
                if (find_row(primary_key).has_value()) {
                    std::cerr << "ERROR: Duplicate value for PRIMARY KEY/UNIQUE column '" 
                              << columns[i].name << "' with value " << primary_key << std::endl;
                    return false;
                }
            }
        }
    }

    std::vector<uint8_t> serialized_data = serializer.serialize(columns, row);

    return btree.insert(primary_key, 0, serialized_data.data(), serialized_data.size());
}

std::optional<Row> Table::find_row(uint32_t primary_key)
{
    std::optional<std::vector<char>> result = btree.find(primary_key, 0);

    if (!result.has_value())
    {
        return std::nullopt;
    }

    const std::vector<char> &data = result.value();
    const uint8_t *byte_data = reinterpret_cast<const uint8_t *>(data.data());
    uint16_t size = static_cast<uint16_t>(data.size());

    Row row = serializer.deserialize(columns, byte_data, size);

    return row;
}

bool Table::update_row(uint32_t primary_key, const Row &row)
{
    if (row.size() != columns.size())
    {
        std::cerr << "ERROR: Row size doesn't match table schema!\n";
        return false;
    }

    std::vector<uint8_t> serialized_data = serializer.serialize(columns, row);
    return btree.update(primary_key, 0, serialized_data.data(), serialized_data.size());
}

bool Table::remove_row(uint32_t primary_key)
{
    if(!btree.remove(primary_key, 0)){
        std::cerr << "ERROR: Deletion not successfull, key not found";
        return false;
    }
    return true;
}

uint32_t Table::extract_primary_key(const Row &row)
{
    for (size_t i = 0; i < columns.size(); i++)
    {
        if (columns[i].is_primary_key)
        {
            return std::get<int32_t>(row[i]);
        }
    }
    return 0;
}

const std::vector<ColumnDefinition>& Table::get_columns() const
{
    return columns;
}
 
std::vector<Row> Table::scan_all()
{
    std::vector<Row> result;
 
    uint32_t current_id = btree.get_root_page_id();
 
    while (true) {
        Page* page = btree.get_pager().get_page(current_id);
        PageHeader* h = reinterpret_cast<PageHeader*>(page->data);
 
        if (h->node_type == LEAF) break;
 
        SlottedPage sp(page->data);
        uint16_t* pointers = sp.get_cell_pointers();
 
        if (h->num_cells == 0) break;
 
        InternalNodeCell* cell = reinterpret_cast<InternalNodeCell*>(
            page->data + pointers[0]);
        current_id = cell->page_id;
    }
 
    while (current_id != 0) {
        Page* page = btree.get_pager().get_page(current_id);
        PageHeader* h = reinterpret_cast<PageHeader*>(page->data);
        SlottedPage sp(page->data);
 
        uint16_t* pointers = sp.get_cell_pointers();
 
        for (int i = 0; i < h->num_cells; i++) {
            LeafCellHeader* cell_header = reinterpret_cast<LeafCellHeader*>(
                page->data + pointers[i]);
 
            std::vector<char> raw_data;
 
            if (cell_header->flags & CELL_FLAG_OVERFLOW) {
                // Podaci su na overflow stranicama
                uint32_t overflow_page_id;
                std::memcpy(&overflow_page_id,
                            page->data + pointers[i] + LEAF_CELL_HEADER_SIZE,
                            sizeof(uint32_t));
                raw_data = SlottedPage::read_from_overflow(
                    overflow_page_id, btree.get_pager());
            } else {
                // Podaci su odmah iza headera
                const char* data_ptr = page->data + pointers[i] + LEAF_CELL_HEADER_SIZE;
                raw_data.assign(data_ptr, data_ptr + cell_header->data_size);
            }
 
            Row row = serializer.deserialize(
                columns,
                reinterpret_cast<const uint8_t*>(raw_data.data()),
                static_cast<uint16_t>(raw_data.size()));
 
            result.push_back(row);
        }
 
        current_id = h->right_child_page_id;
    }
 
    return result;
}