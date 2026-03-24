#pragma once
#include <iostream>
#include <cstdint>
#include <cstring>
#include <vector>
#include <optional>
#include "BTree.h"
#include "Serializer.h"

class Table {
    private:
        std::string name;
        std::vector<ColumnDefinition> columns;

        BTree& btree;

        Serializer serializer;
    public:
        Table(std::string name, std::vector<ColumnDefinition> table_schema, BTree& btree)
        : name(name), columns(table_schema), btree(btree), serializer() {}

        bool insert_row(const Row& row);
        std::optional<Row> find_row(uint32_t primary_key);


    private:
        uint32_t extract_primary_key(const Row& row);
};
