#include "PageCache.h"
#include "Pager.h"
#include "Logger.h"
#include "WALManager.h"

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

    LOG_DEBUG("Cache", "Inserted page " + std::to_string(page_id) + " at index " + std::to_string(index));
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
            if (wal_manager_) {
                uint64_t next    = wal_manager_->get_next_lsn();
                uint64_t flushed = wal_manager_->get_flushed_lsn();
                if (next > 1 && flushed < next - 1)
                    wal_manager_->flush_to_lsn(next - 1);
            }
            on_evict(page_id, page->data);
            page->is_dirty = false;
        }
    }
}

void PageCache::evict_page()
{
    int iterations = 0;
    while(iterations < 2 * CACHE_SIZE){
        size_t index = clock_hand;
        if(cache_buffer[index]->reference_bit == false){
            if(cache_buffer[index]->is_dirty){
                // steal+no-force ==> flush all pending WAL records
                // before this dirty page reaches disk so the WAL can undo it on crash
                if (wal_manager_) {
                    uint64_t next = wal_manager_->get_next_lsn();
                    uint64_t flushed = wal_manager_->get_flushed_lsn();
                    if (next > 1 && flushed < next - 1)
                        wal_manager_->flush_to_lsn(next - 1);
                }
                on_evict(cache_buffer[index]->page_id, cache_buffer[index]->data);
                cache_buffer[index]->is_dirty = false;
                page_cache.erase(cache_buffer[index]->page_id);
                LOG_DEBUG("Cache", "Evicted dirty page " + std::to_string(cache_buffer[index]->page_id) + " from buffer pool");
                return;
            }else{
                page_cache.erase(cache_buffer[index]->page_id);
                LOG_DEBUG("Cache", "Evicted clean page " + std::to_string(cache_buffer[index]->page_id) + " from buffer pool");
                return;
            }
        }
        cache_buffer[index]->reference_bit = false;
        clock_hand = (clock_hand + 1) % CACHE_SIZE;
        iterations++;
    }
}

void PageCache::flush_all_dirty()
{
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (wal_manager_) {
        uint64_t next    = wal_manager_->get_next_lsn();
        uint64_t flushed = wal_manager_->get_flushed_lsn();
        if (next > 1 && flushed < next - 1)
            wal_manager_->flush_to_lsn(next - 1);
    }
    for (const auto& [page_id, index] : page_cache) {
        Page* page = cache_buffer[index].get();
        if (page->is_dirty) {
            on_evict(page_id, page->data);
            page->is_dirty = false;
        }
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
