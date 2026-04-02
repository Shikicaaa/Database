#pragma once

#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <memory>
#include <cstdint>


const uint32_t PAGE_SIZE = 4096;


#pragma pack(push, 1)
struct DBHeader{
    char name[16];              // unique identifier for database
    uint32_t num_pages;         // num of pages in the db file 
    uint32_t root_page_id;      // where does B tree begin
    uint32_t page_size;         // basically PAGE_SIZE macro
    uint32_t first_free_page_id; // 0 if doesn't exist
    uint32_t catalog_root_page_id;
};
#pragma pack(pop)


struct Page{
    char data[PAGE_SIZE];
    bool is_dirty = false;
};

class Pager{
    private:
        std::fstream file;
        std::string fileName;

        DBHeader db_header;

        std::map<uint32_t, std::unique_ptr<Page>> page_cache;

    public:
        Pager(const std::string& fileName, const char* name);
        ~Pager();

        Page* get_page(uint32_t pageID);

        void flush(uint32_t pageID);

        inline uint32_t get_root_page_id() const { return db_header.root_page_id; }
        inline uint32_t get_num_pages() const { return db_header.num_pages; }
        inline uint32_t get_catalog_root_page_id() const { return db_header.catalog_root_page_id; }

        uint32_t allocate_new_page();
        void free_page(uint32_t page_id);

        void set_root_page_id(uint32_t new_root_id);
        void set_catalog_root_page_id(uint32_t new_root_id);
        
};