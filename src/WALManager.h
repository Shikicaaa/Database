#pragma once

#include <fstream>
#include <string>
#include <mutex>
#include <cstdint>

const uint32_t WAL_MAGIC   = 0x53484B57; // "SHKW" 
const uint32_t WAL_VERSION = 1;

enum class WALRecordType : uint8_t {
    BEGIN = 1,
    INSERT = 2,
    UPDATE = 3,
    DELETE_REC = 4,
    COMMIT = 5,
    ROLLBACK = 6,
    CHECKPOINT_BEGIN = 7,
    CHECKPOINT_END = 8,
};

#pragma pack(push, 1)
struct WALFileHeader {
    uint32_t magic; // WAL_MAGIC first sanity check on open
    uint32_t version; // WAL_VERSION
    uint32_t page_size; // must match database PAGE_SIZE
    uint8_t  padding[4]; // align next fields to 8-byte boundary
    uint64_t next_lsn; // next LSN to assign (rebuilt from scan on open)
    uint64_t flushed_lsn; // highest LSN durably fsynced to this file
    uint64_t begin_checkpoint_lsn; // LSN of last BEGIN_CHECKPOINT record (0 = never)
    uint64_t end_checkpoint_lsn; // LSN of last END_CHECKPOINT record   (0 = incomplete)
};                                  // 48 bytes total

struct WALRecordHeader {
    uint64_t lsn;
    uint32_t txn_id;
    WALRecordType type;
    uint32_t payload_size;     // bytes that follow this header
};                                  // 17 bytes total
#pragma pack(pop)

class Catalog;

class WALManager {
public:
    WALManager(const std::string& path, uint32_t page_size);
    ~WALManager();

    uint64_t log_begin(uint32_t txn_id);
    uint64_t log_commit(uint32_t txn_id);
    uint64_t log_rollback(uint32_t txn_id);

    // [page_id:4][offset:2][before_size:2][after_size:2][before...][after...]
    uint64_t log_insert(uint32_t txn_id, uint32_t page_id, uint16_t offset,
                        const char* after_image,  uint16_t after_size);
    uint64_t log_update(uint32_t txn_id, uint32_t page_id, uint16_t offset,
                        const char* before_image, uint16_t before_size,
                        const char* after_image,  uint16_t after_size);
    uint64_t log_delete(uint32_t txn_id, uint32_t page_id, uint16_t offset,
                        const char* before_image, uint16_t before_size);

    void flush_to_lsn(uint64_t lsn);
    uint64_t get_flushed_lsn() const;
    uint64_t get_next_lsn() const;

    // Call after pager_.flush_all_pages() to record that all data pages are on disk
    // Recovery will skip redo/undo for every WAL record written before this point
    // floor_lsn, if nonzero, caps how far the checkpoint can advance: it will never
    // pass (floor_lsn - 1), so an in-flight transaction's own BEGIN lsn keeps its
    // records visible to recovery even if some unrelated statement checkpoints
    // while that transaction is still open
    void checkpoint(uint64_t floor_lsn = 0);

    void recover(Catalog& catalog);

private:
    std::fstream file_;
    WALFileHeader header_;
    mutable std::mutex wal_mutex_;

    uint64_t append_record(uint32_t txn_id, WALRecordType type,
                           const char* payload, uint32_t payload_size);
    void write_header();
    void scan_to_end();
};
