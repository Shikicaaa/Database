#include "Table.h"
#include "SlottedPage.h"
#include "Logger.h"

bool Table::insert_row(const Row &row)
{
    if (row.size() != columns.size())
    {
        LOG_ERROR("Table", "Row size doesn't match table schema");
        return false;
    }

    uint32_t primary_key = extract_primary_key(row);

    for (size_t i = 0; i < columns.size(); i++) {
        if (columns[i].is_primary_key) {
            if (find_row(primary_key).has_value()) {
                LOG_ERROR("Table", "Duplicate PRIMARY KEY value " + std::to_string(primary_key) + " in column '" + columns[i].name + "'");
                return false;
            }
        } else if (columns[i].is_unique && i < row.size() && !std::holds_alternative<std::monostate>(row[i])) {
            for (const Row& existing : scan_all()) {
                if (i < existing.size() && existing[i] == row[i]) {
                    LOG_ERROR("Table", "Duplicate UNIQUE value in column '" + columns[i].name + "'");
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

bool Table::update_row(uint32_t old_primary_key, const Row &new_row)
{
    if (new_row.size() != columns.size())
    {
        LOG_ERROR("Table", "Row size doesn't match table schema");
        return false;
    }

    uint32_t new_primary_key = extract_primary_key(new_row);

    for (size_t i = 0; i < columns.size(); i++) {
        if (columns[i].is_unique && !columns[i].is_primary_key &&
            i < new_row.size() && !std::holds_alternative<std::monostate>(new_row[i])) {
            for (const Row& existing : scan_all()) {
                if (extract_primary_key(existing) == old_primary_key) continue;
                if (i < existing.size() && existing[i] == new_row[i]) {
                    LOG_ERROR("Table", "UNIQUE constraint violation on column '" + columns[i].name + "'");
                    return false;
                }
            }
        }
    }

    if (old_primary_key == new_primary_key) {
        std::vector<uint8_t> serialized_data = serializer.serialize(columns, new_row);
        return btree.update(old_primary_key, 0, serialized_data.data(), serialized_data.size());
    }

    if (find_row(new_primary_key).has_value()) {
        LOG_ERROR("Table", "Duplicate value for PRIMARY KEY " + std::to_string(new_primary_key) + " — cannot change PK to an existing value");
        return false;
    }

    if (!btree.remove(old_primary_key, 0)) {
        LOG_PANIC("Table", "update_row failed to remove old PK " + std::to_string(old_primary_key));
        return false;
    }

    std::vector<uint8_t> serialized_data = serializer.serialize(columns, new_row);
    if (!btree.insert(new_primary_key, 0, serialized_data.data(), serialized_data.size())) {
        LOG_PANIC("Table", "update_row failed to insert under new PK " + std::to_string(new_primary_key) + "; row is lost — data inconsistency");
        return false;
    }

    return true;
}

bool Table::remove_row(uint32_t primary_key)
{
    if(!btree.remove(primary_key, 0)){
        LOG_ERROR("Table", "Deletion not successful, key not found: " + std::to_string(primary_key));
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
                uint32_t overflow_page_id;
                std::memcpy(&overflow_page_id,
                            page->data + pointers[i] + LEAF_CELL_HEADER_SIZE,
                            sizeof(uint32_t));
                raw_data = SlottedPage::read_from_overflow(
                    overflow_page_id, btree.get_pager());
            } else {
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
