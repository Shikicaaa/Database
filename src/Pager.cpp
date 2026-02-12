#include "Pager.h"

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
        std::memcpy(db_header.name, name, 16);
        db_header.num_pages = 1;
        db_header.page_size = PAGE_SIZE;
        db_header.root_page_id = 0;

        char page_zero[PAGE_SIZE] = {0};
        std::memcpy(page_zero, &db_header, sizeof(DBHeader));

        file.write(page_zero, PAGE_SIZE);
    } else{
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));
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
    uint32_t new_page_id = db_header.num_pages;
    db_header.num_pages++;

    // update the header
    file.seekp(0);
    file.write(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));

    char empty_page[PAGE_SIZE] = {0};

    // move to the next page
    file.seekp(new_page_id * PAGE_SIZE);
    file.write(empty_page, PAGE_SIZE);
    file.flush();
    return new_page_id;
}

void Pager::set_root_page_id(uint32_t new_root_id)
{
    db_header.root_page_id = new_root_id;
    
    file.seekp(0);
    file.write(reinterpret_cast<char*>(&db_header), sizeof(DBHeader));
    file.flush();

}
