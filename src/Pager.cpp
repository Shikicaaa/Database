#include "Pager.h"
#include "SlottedPage.h"
#include <cstring>

Pager::Pager(const std::string &fileName, const char* name){
    this->fileName = fileName;

    file.open(fileName, std::ios::binary | std::ios::out | std::ios::in);
    if(!file){
        file.open(fileName, std::ios::out | std::ios::binary);
        file.close();
        file.open(fileName, std::ios::binary | std::ios::out | std::ios::in);
    }

    file.seekg(0, std::ios::end);
    // WE CHECK IF THE FILE IS EMPTY
    if(file.tellg() == 0){
        std::memset(&db_header, 0, sizeof(DBHeader));
        std::strncpy(db_header.name, name, sizeof(db_header.name) - 1);
        db_header.num_pages = 2; //page 0 header, page 1 catalog root
        db_header.page_size = PAGE_SIZE;
        db_header.root_page_id = 0;
        db_header.first_free_page_id = 0;
        db_header.catalog_root_page_id = 1;

        // PAGE 0
        char page_zero[PAGE_SIZE] = {0};
        std::memcpy(page_zero, &db_header, sizeof(DBHeader));

        file.write(page_zero, PAGE_SIZE);

        // PAGE 1
        char page_one[PAGE_SIZE] = {0};
        SlottedPage catalog_page(page_one);
        catalog_page.init_as_leaf_node(true);
        file.write(page_one, PAGE_SIZE);

        file.flush();

    } else{
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));

        if (file.gcount() < static_cast<std::streamsize>(sizeof(DBHeader))) {
            file.clear();
            db_header.first_free_page_id = 0;
            file.seekp(0);
            file.write(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));
            file.flush();
        }

        char normalized_name[sizeof(db_header.name)] = {0};
        std::strncpy(normalized_name, name, sizeof(normalized_name) - 1);
        if (std::memcmp(db_header.name, normalized_name, sizeof(db_header.name)) != 0) {
            std::memcpy(db_header.name, normalized_name, sizeof(db_header.name));
            file.seekp(0);
            file.write(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));
            file.flush();
        }
    }
}

Pager::~Pager()
{
    for(auto const& [id,page_ptr] : page_cache){
        flush(id);
    }
    file.close();
}

Page* Pager::get_page(uint32_t pageID){
    if(page_cache.find(pageID) != page_cache.end()){
        return page_cache[pageID].get();
    }

    auto new_page = std::make_unique<Page>();

    int offset = pageID * PAGE_SIZE;
    file.seekg(offset);
    file.read(new_page->data, PAGE_SIZE);

    if(!file){
        file.clear();
    }

    Page* raw_page = new_page.get();

    page_cache[pageID] = std::move(new_page);

    return raw_page;
}

void Pager::flush(uint32_t pageID)
{
    auto it = page_cache.find(pageID);

    if (it != page_cache.end()) {
        Page* page = it->second.get();
        
        if (page->is_dirty) {
            file.seekp(pageID * PAGE_SIZE);
            file.write(page->data, PAGE_SIZE);
            page->is_dirty = false;
        }
    }
}

uint32_t Pager::allocate_new_page()
{
    uint32_t allocated_page_id;

    if(db_header.first_free_page_id != 0){
        allocated_page_id = db_header.first_free_page_id;

        Page* recycled_page = get_page(allocated_page_id);
        uint32_t next_free_page_id;
        std::memcpy(&next_free_page_id, recycled_page->data, sizeof(uint32_t));

        db_header.first_free_page_id = next_free_page_id;

        std::memset(recycled_page->data, 0, PAGE_SIZE);
        recycled_page->is_dirty = true;
    }else{
        // if there is no free page, we initialize zeroed page on disk
        allocated_page_id = db_header.num_pages;
        db_header.num_pages++;

        char empty_page[PAGE_SIZE] = {0};
        file.seekp(allocated_page_id * PAGE_SIZE);
        file.write(empty_page, PAGE_SIZE);
    }

    file.seekp(0);
    file.write(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));
    file.flush();

    return allocated_page_id;
}

void Pager::free_page(uint32_t page_id)
{
    Page* page = get_page(page_id);

    std::memcpy(page->data, &db_header.first_free_page_id, sizeof(uint32_t));

    db_header.first_free_page_id = page_id;

    page->is_dirty = true;

    file.seekp(0);
    file.write(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));
}

void Pager::set_root_page_id(uint32_t new_root_id)
{
    db_header.root_page_id = new_root_id;
    
    file.seekp(0);
    file.write(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));
    file.flush();

}

void Pager::set_catalog_root_page_id(uint32_t new_root_id)
{
    db_header.catalog_root_page_id = new_root_id;
    
    file.seekp(0);
    file.write(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));
    file.flush();
}
