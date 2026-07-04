#include "WALManager.h"
#include "Catalog.h"
#include "Logger.h"
#include <cstring>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <unordered_set>

WALManager::WALManager(const std::string& path, uint32_t page_size)
{
    file_.open(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file_) {
        file_.open(path, std::ios::binary | std::ios::out);
        file_.close();
        file_.open(path, std::ios::binary | std::ios::in | std::ios::out);

        std::memset(&header_, 0, sizeof(WALFileHeader));
        header_.magic = WAL_MAGIC;
        header_.version = WAL_VERSION;
        header_.page_size = page_size;
        header_.next_lsn = 1;
        header_.flushed_lsn = 0;
        header_.begin_checkpoint_lsn = 0;
        header_.end_checkpoint_lsn = 0;
        write_header();
        LOG_DEBUG("WAL", "Created new WAL file: " + path);
    } else {
        file_.seekg(0);
        file_.read(reinterpret_cast<char*>(&header_), sizeof(WALFileHeader));
        if (file_.gcount() < static_cast<std::streamsize>(sizeof(WALFileHeader)))
            throw std::runtime_error("WAL file too short to contain a valid header");
        if (header_.magic != WAL_MAGIC)
            throw std::runtime_error("WAL file corrupt: invalid magic number");
        if (header_.page_size != page_size)
            throw std::runtime_error("WAL page_size mismatch with database");
        scan_to_end();
        LOG_DEBUG("WAL", "Opened existing WAL file: " + path +
                  " (next_lsn=" + std::to_string(header_.next_lsn) + ")");
    }
}

WALManager::~WALManager()
{
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void WALManager::write_header()
{
    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(&header_), sizeof(WALFileHeader));
    file_.flush();
}

void WALManager::scan_to_end()
{
    file_.seekg(sizeof(WALFileHeader));
    uint64_t max_lsn = header_.next_lsn > 0 ? header_.next_lsn - 1 : 0;

    WALRecordHeader rec;
    while (true) {
        file_.read(reinterpret_cast<char*>(&rec), sizeof(WALRecordHeader));
        if (file_.gcount() < static_cast<std::streamsize>(sizeof(WALRecordHeader)))
            break;
        if (rec.lsn > max_lsn)
            max_lsn = rec.lsn;
        file_.seekg(rec.payload_size, std::ios::cur);
        if (!file_) break;
    }

    file_.clear();
    header_.next_lsn = max_lsn + 1;
    file_.seekp(0, std::ios::end);
}

uint64_t WALManager::append_record(uint32_t txn_id, WALRecordType type,
                                   const char* payload, uint32_t payload_size)
{
    std::lock_guard<std::mutex> lock(wal_mutex_);

    uint64_t lsn = header_.next_lsn++;

    WALRecordHeader rec;
    rec.lsn = lsn;
    rec.txn_id = txn_id;
    rec.type = type;
    rec.payload_size = payload_size;

    file_.seekp(0, std::ios::end);
    file_.write(reinterpret_cast<const char*>(&rec), sizeof(WALRecordHeader));
    if (payload_size > 0 && payload != nullptr)
        file_.write(payload, payload_size);

    LOG_DEBUG("WAL", "LSN=" + std::to_string(lsn) +
              " txn=" + std::to_string(txn_id) +
              " type=" + std::to_string(static_cast<int>(type)));
    return lsn;
}

uint64_t WALManager::log_begin(uint32_t txn_id)
{
    return append_record(txn_id, WALRecordType::BEGIN, nullptr, 0);
}

uint64_t WALManager::log_commit(uint32_t txn_id)
{
    return append_record(txn_id, WALRecordType::COMMIT, nullptr, 0);
}

uint64_t WALManager::log_rollback(uint32_t txn_id)
{
    return append_record(txn_id, WALRecordType::ROLLBACK, nullptr, 0);
}

uint64_t WALManager::log_insert(uint32_t txn_id, uint32_t page_id, uint16_t offset,
                                const char* after_image, uint16_t after_size)
{
    uint16_t before_size  = 0;
    uint32_t payload_size = 4 + 2 + 2 + 2 + after_size;
    std::vector<char> buf(payload_size);
    char* p = buf.data();
    std::memcpy(p, &page_id, 4); p += 4;
    std::memcpy(p, &offset, 2); p += 2;
    std::memcpy(p, &before_size, 2); p += 2;
    std::memcpy(p, &after_size, 2); p += 2;
    if (after_size > 0) std::memcpy(p, after_image, after_size);
    return append_record(txn_id, WALRecordType::INSERT, buf.data(), payload_size);
}

uint64_t WALManager::log_update(uint32_t txn_id, uint32_t page_id, uint16_t offset,
                                const char* before_image, uint16_t before_size,
                                const char* after_image,  uint16_t after_size)
{
    uint32_t payload_size = 4 + 2 + 2 + 2 + before_size + after_size;
    std::vector<char> buf(payload_size);
    char* p = buf.data();
    std::memcpy(p, &page_id, 4); p += 4;
    std::memcpy(p, &offset, 2); p += 2;
    std::memcpy(p, &before_size, 2); p += 2;
    std::memcpy(p, &after_size, 2); p += 2;
    if (before_size > 0) std::memcpy(p, before_image, before_size);
    p += before_size;
    if (after_size  > 0) std::memcpy(p, after_image,  after_size);
    return append_record(txn_id, WALRecordType::UPDATE, buf.data(), payload_size);
}

uint64_t WALManager::log_delete(uint32_t txn_id, uint32_t page_id, uint16_t offset,
                                const char* before_image, uint16_t before_size)
{
    uint16_t after_size = 0;
    uint32_t payload_size = 4 + 2 + 2 + 2 + before_size;
    std::vector<char> buf(payload_size);
    char* p = buf.data();
    std::memcpy(p, &page_id, 4); p += 4;
    std::memcpy(p, &offset, 2); p += 2;
    std::memcpy(p, &before_size, 2); p += 2;
    std::memcpy(p, &after_size, 2); p += 2;
    if (before_size > 0) std::memcpy(p, before_image, before_size);
    return append_record(txn_id, WALRecordType::DELETE_REC, buf.data(), payload_size);
}

void WALManager::flush_to_lsn(uint64_t lsn)
{
    std::lock_guard<std::mutex> lock(wal_mutex_);
    file_.flush();
    header_.flushed_lsn = lsn;
    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(&header_), sizeof(WALFileHeader));
    file_.flush();
    LOG_DEBUG("WAL", "Flushed to LSN=" + std::to_string(lsn));
}

uint64_t WALManager::get_flushed_lsn() const
{
    std::lock_guard<std::mutex> lock(wal_mutex_);
    return header_.flushed_lsn;
}

uint64_t WALManager::get_next_lsn() const
{
    std::lock_guard<std::mutex> lock(wal_mutex_);
    return header_.next_lsn;
}

void WALManager::checkpoint(uint64_t floor_lsn)
{
    std::lock_guard<std::mutex> lk(wal_mutex_);
    file_.flush();
    uint64_t candidate = header_.next_lsn > 0 ? header_.next_lsn - 1 : 0;
    if (floor_lsn > 0 && floor_lsn - 1 < candidate)
        candidate = floor_lsn - 1;
    header_.end_checkpoint_lsn = candidate;
    write_header();
    LOG_DEBUG("WAL", "Checkpoint at LSN=" + std::to_string(header_.end_checkpoint_lsn));
}

void WALManager::recover(Catalog& catalog)
{
    struct DataRec {
        uint64_t lsn;
        uint32_t txn_id;
        WALRecordType type;
        uint32_t page_id;
        std::vector<uint8_t> before;
        std::vector<uint8_t> after;
    };

    // committed[txn_id] = true  ==> COMMIT seen = winner
    // committed[txn_id] = false ==> ROLLBACK seen = already handled, undo is idempotent
    // absent from map ==> crash loser
    std::unordered_map<uint32_t, bool> committed;
    std::vector<DataRec> recs;
    uint64_t checkpoint_lsn = 0;

    {
        std::lock_guard<std::mutex> lk(wal_mutex_);
        checkpoint_lsn = header_.end_checkpoint_lsn;
        file_.clear();
        file_.seekg(sizeof(WALFileHeader));

        WALRecordHeader hdr;
        while (file_.read(reinterpret_cast<char*>(&hdr), sizeof(WALRecordHeader)),
               file_.gcount() == static_cast<std::streamsize>(sizeof(WALRecordHeader))) {
            std::vector<char> payload(hdr.payload_size);
            if (hdr.payload_size > 0) {
                file_.read(payload.data(), hdr.payload_size);
                if (file_.gcount() < static_cast<std::streamsize>(hdr.payload_size)) break;
            }

            if (hdr.type == WALRecordType::COMMIT) {
                committed[hdr.txn_id] = true;
            } else if (hdr.type == WALRecordType::ROLLBACK) {
                // emplace: don't overwrite a prior COMMIT (defensive)
                committed.emplace(hdr.txn_id, false);
            } else if (hdr.lsn > checkpoint_lsn &&   // data before checkpoint is already on disk
                       (hdr.type == WALRecordType::INSERT  ||
                        hdr.type == WALRecordType::UPDATE  ||
                        hdr.type == WALRecordType::DELETE_REC) &&
                       payload.size() >= 10) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(payload.data());
                DataRec rec;
                rec.lsn = hdr.lsn;
                rec.txn_id = hdr.txn_id;
                rec.type = hdr.type;
                std::memcpy(&rec.page_id, p, 4); p += 4;
                uint16_t off = 0, bsz = 0, asz = 0;
                std::memcpy(&off, p, 2); p += 2;
                std::memcpy(&bsz, p, 2); p += 2;
                std::memcpy(&asz, p, 2); p += 2;
                rec.before.assign(p, p + bsz);
                rec.after .assign(p + bsz, p + bsz + asz);
                recs.push_back(std::move(rec));
            }
        }
        file_.clear();
    }

    if (recs.empty()) {
        LOG_DEBUG("WAL", "Recovery: no data records — nothing to redo/undo");
        return;
    }

    // Redo pass
    int redo_ops = 0, undo_ops = 0;
    for (const auto& rec : recs) {
        auto it = committed.find(rec.txn_id);
        if (it == committed.end() || !it->second) continue; // skip losers

        Table* table = catalog.get_table_by_root_page(rec.page_id);
        if (!table) continue;
        Serializer sr;

        if (rec.type == WALRecordType::INSERT) {
            Row row = sr.deserialize(table->get_columns(),
                                     rec.after.data(), static_cast<uint16_t>(rec.after.size()));
            table->insert_row(row);
            ++redo_ops;
        } else if (rec.type == WALRecordType::UPDATE) {
            Row new_row = sr.deserialize(table->get_columns(),
                                          rec.after.data(), static_cast<uint16_t>(rec.after.size()));
            uint32_t pk = table->extract_primary_key(new_row);
            table->update_row(pk, new_row);
            ++redo_ops;
        } else if (rec.type == WALRecordType::DELETE_REC) {
            Row old_row = sr.deserialize(table->get_columns(),
                                          rec.before.data(), static_cast<uint16_t>(rec.before.size()));
            uint32_t pk = table->extract_primary_key(old_row);
            table->remove_row(pk);
            ++redo_ops;
        }
    }

    // Undo pass
    for (int i = static_cast<int>(recs.size()) - 1; i >= 0; --i) {
        const auto& rec = recs[i];
        auto it = committed.find(rec.txn_id);
        if (it != committed.end()) continue; // skip winners AND already-rolled-back

        Table* table = catalog.get_table_by_root_page(rec.page_id);
        if (!table) continue;
        Serializer sr;

        if (rec.type == WALRecordType::INSERT) {
            Row row = sr.deserialize(table->get_columns(),
                                      rec.after.data(), static_cast<uint16_t>(rec.after.size()));
            uint32_t pk = table->extract_primary_key(row);
            table->remove_row(pk);
            ++undo_ops;
        } else if (rec.type == WALRecordType::UPDATE) {
            Row old_row = sr.deserialize(table->get_columns(),
                                          rec.before.data(), static_cast<uint16_t>(rec.before.size()));
            uint32_t pk = table->extract_primary_key(old_row);
            table->update_row(pk, old_row);
            ++undo_ops;
        } else if (rec.type == WALRecordType::DELETE_REC) {
            Row row = sr.deserialize(table->get_columns(),
                                      rec.before.data(), static_cast<uint16_t>(rec.before.size()));
            table->insert_row(row);
            ++undo_ops;
        }
    }

    std::unordered_set<uint32_t> crash_losers;
    for (const auto& rec : recs) {
        if (committed.find(rec.txn_id) == committed.end())
            crash_losers.insert(rec.txn_id);
    }
    for (uint32_t txn_id : crash_losers)
        append_record(txn_id, WALRecordType::ROLLBACK, nullptr, 0);
    if (!crash_losers.empty())
        flush_to_lsn(header_.next_lsn - 1);

    LOG_DEBUG("WAL", "Recovery complete: redo=" + std::to_string(redo_ops) +
              " undo=" + std::to_string(undo_ops) +
              " crash_losers=" + std::to_string(crash_losers.size()));
}
