#pragma once
#include <cstdint>
#include "WALManager.h"
#include "Transaction.h"

struct TxnContext {
    uint32_t txn_id;
    WALManager* wal;
    std::vector<UndoEntry>* undo_log;
};
