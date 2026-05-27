#pragma once

#include <optional>
#include <cstdint>
#include <vector>
#include "Serializer.h"
#include "Table.h"
#include "Pager.h"
#include "SlottedPage.h"

class Table;
class Pager;

class Cursor {
public:
    explicit Cursor(Table& table);

    std::optional<Row> current_row();

    bool advance();

private:
    Table& table_;
    Pager& pager_;
    
    uint32_t current_page_id_;
    uint16_t cell_index_;
    bool eof_; 

    std::optional<std::vector<char>> get_raw_current_record();
};