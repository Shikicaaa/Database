#include "Pager.h"

Pager::Pager(const std::string &fileName){
    this->fileName = fileName;

    file.open(fileName, std::ios::binary | std::ios::out | std::ios::in);
    if(!file){
        file.open(fileName, std::ios::out | std::ios::binary);
        file.close();
        file.open(fileName, std::ios::binary | std::ios::out | std::ios::in);
        
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
