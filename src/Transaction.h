#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class UndoOpType : uint8_t { INSERT = 1, UPDATE = 2, DELETE_OP = 3 };

struct UndoEntry {
    UndoOpType op;
    uint32_t page_id;
    std::vector<uint8_t> before;  // before image bytes empty for insert
    std::vector<uint8_t> after;   // after image bytes empty for delete
};

struct Transaction {
    uint32_t txn_id;
    bool active = true;
    std::vector<uint64_t> wal_lsns;
    std::vector<std::string> shared_locks;
    std::vector<std::string> exclusive_locks;
    std::vector<UndoEntry> undo_log; //applied in reverse on ROLLBACK
};
