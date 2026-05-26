#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <memory>
#include <cstring>
#include <mutex>

struct Page;

const uint32_t CACHE_SIZE = 128;

class PageCache
{
    std::vector<std::unique_ptr<Page>> cache_buffer;
    std::unordered_map<uint32_t, size_t> page_cache;

    size_t clock_hand;
    size_t num_allocated_pages;

    mutable std::mutex cache_mutex;

    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;

public:
    std::function<void(uint32_t, const char*)> on_evict;

    PageCache() : clock_hand(0), num_allocated_pages(0), hits(0), misses(0), evictions(0) {
        cache_buffer.reserve(CACHE_SIZE);
        std::cout << "[CACHE] Initialized cache with capacity of " << CACHE_SIZE << " pages." << std::endl;
    }

    Page* find_page(uint32_t page_id);

    Page* insert_page(uint32_t page_id, const char* data);

    void mark_dirty(uint32_t page_id);
    void flush_page(uint32_t page_id);
    void shutdown();

    void print_stats() const {
        std::lock_guard<std::mutex> lock(cache_mutex);
        std::cout << "[CACHE] Hits: " << hits << ", Misses: " << misses << ", Evictions: " << evictions << std::endl;
    }

    size_t get_used_pages() const;

private:
    void evict_page();
};