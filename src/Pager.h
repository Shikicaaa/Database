#pragma once

#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <memory>


const uint32_t PAGE_SIZE = 4096;

struct Page{
    char data[PAGE_SIZE];
    bool is_dirty = false;
};

class Pager{
    private:
        std::fstream file;
        std::string fileName;

        std::map<uint32_t, std::unique_ptr<Page>> page_cache;

    public:
        Pager(const std::string& fileName);
        ~Pager();

        Page* get_page(uint32_t pageID);

        void flush(uint32_t pageID);
};