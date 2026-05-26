#include "PageCache.h"
#include "Pager.h"

Page* PageCache::find_page(uint32_t page_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    auto it = page_cache.find(page_id);
    if (it == page_cache.end()) return nullptr;

    hits++;
    size_t index = it->second;
    cache_buffer[index]->reference_bit = 1;
    cache_buffer[index]->access_count++;
    return cache_buffer[index].get();
}

Page* PageCache::insert_page(uint32_t page_id, const char* data){
    std::lock_guard<std::mutex> lock(cache_mutex);
    misses++;

    size_t index;

    if(num_allocated_pages < CACHE_SIZE){
        index = num_allocated_pages++;
        cache_buffer.emplace_back(std::make_unique<Page>());
    } else {
        evict_page();
        evictions++;
        index = clock_hand;
    }

    Page* page = cache_buffer[index].get();
    std::memcpy(page->data, data, PAGE_SIZE);
    page->page_id = page_id;
    page->is_dirty = false;
    page->reference_bit = 1;
    page->access_count = 1;
    page_cache[page_id] = index;

    std::cout << "[CACHE] Inserted page " << page_id << " at index " << index << "." << std::endl;
    return page;
}

void PageCache::mark_dirty(uint32_t page_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = page_cache.find(page_id);
    if (it != page_cache.end()) {
        cache_buffer[it->second]->is_dirty = true;
    }
}

void PageCache::flush_page(uint32_t page_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = page_cache.find(page_id);
    if (it != page_cache.end()) {
        Page* page = cache_buffer[it->second].get();
        if (page->is_dirty) {
            on_evict(page_id, page->data);
            page->is_dirty = false;
        }
    }
}

/*
Start from beginning.
If ref = 0 evict
If ref = 1, set ref = 0 and move on
Do until we find a page to evict or we loop through all pages
*/
void PageCache::evict_page()
{
    int iterations = 0;
    while(iterations < 2 * CACHE_SIZE){
        size_t index = clock_hand;
        if(cache_buffer[index]->reference_bit == false){
            if(cache_buffer[index]->is_dirty){
                on_evict(cache_buffer[index]->page_id, cache_buffer[index]->data);
                cache_buffer[index]->is_dirty = false;
                page_cache.erase(cache_buffer[index]->page_id);
                std::cout << "[CACHE] Evicted page " << cache_buffer[index]->page_id << " from cache." << std::endl;
                return;
            }else{
                page_cache.erase(cache_buffer[index]->page_id);
                std::cout << "[CACHE] Evicted clean page " << cache_buffer[index]->page_id << " from cache." << std::endl;
                return;
            }
        }
        cache_buffer[index]->reference_bit = false;
        clock_hand = (clock_hand + 1) % CACHE_SIZE;
        iterations++;
    }
}

void PageCache::shutdown()
{
    std::lock_guard<std::mutex> lock(cache_mutex);
    for (const auto& [page_id, index] : page_cache) {
        Page* page = cache_buffer[index].get();
        if (page->is_dirty) {
            on_evict(page_id, page->data);
            page->is_dirty = false;
        }
    }
}