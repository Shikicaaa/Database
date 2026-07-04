#pragma once
#include "Parser/Parser.h"
#include "Catalog.h"
#include "Table.h"
#include "WALManager.h"
#include "Transaction.h"
#include "TxnContext.h"
#include "Pager.h"
#include <atomic>
#include <optional>
#include <string>
#include <vector>

struct ExecutionResult {
    bool success  = false;
    std::string message;
    std::vector<Row> rows;
    std::vector<std::string> columns;
};

class Executor {
public:
    Executor(Catalog& catalog, WALManager& wal, Pager& pager);

    ExecutionResult execute(const Statement& stmt);

private:
    Catalog& catalog_;
    WALManager& wal_;
    Pager& pager_;

    std::atomic<uint32_t> next_txn_id_{1};
    std::optional<Transaction> active_txn_;

    // Acquires a shared (exclusive=false) or exclusive (exclusive=true) table lock
    // for the active transaction. No op if no transaction is active ==> Auto commit.
    // Handles upgrade from shared into exclusive if the table is already locked shared.
    void acquire_table_lock(const std::string& table_name, bool exclusive);

    // LSN of the active transaction's BEGIN record, or 0 if none is open.
    // Pass to WALManager::checkpoint() so a DDL statement run while a transaction
    // is still open can't seal a checkpoint past that transaction's own records.
    uint64_t checkpoint_floor() const {
        return active_txn_.has_value() ? active_txn_->wal_lsns.front() : 0;
    }

    // Returns a TxnContext for the statement about to run: the active transaction's
    // if one is open, otherwise logs an implicit BEGIN into local_storage and sets
    // autocommit=true so the caller knows to log COMMIT + flush + checkpoint itself
    // once the statement finishes. Every DML statement goes through the WAL this way,
    // whether or not the user wrapped it in an explicit BEGIN/COMMIT.
    TxnContext begin_statement_txn(Transaction& local_storage, bool& autocommit);
    void end_statement_txn(uint32_t txn_id, bool autocommit);

    ExecutionResult execute_select(const SelectStatement& stmt);
    ExecutionResult execute_insert(const InsertStatement& stmt);
    ExecutionResult execute_update(const UpdateStatement& stmt);
    ExecutionResult execute_delete(const DeleteStatement& stmt);
    ExecutionResult execute_create(const CreateTableStatement& stmt);
    ExecutionResult execute_create_index(const CreateIndexStatement& stmt);
    ExecutionResult execute_drop_table(const DropTableStatement& stmt);
    ExecutionResult execute_drop_index(const DropIndexStatement& stmt);
    ExecutionResult execute_alter_table(const AlterTableStatement& stmt);
    ExecutionResult execute_begin(const BeginStatement& stmt);
    ExecutionResult execute_commit(const CommitStatement& stmt);
    ExecutionResult execute_rollback(const RollbackStatement& stmt);

    std::optional<uint32_t> try_extract_pk_from_where(
        const std::optional<WhereClause>& where,
        const std::vector<ColumnDefinition>& schema) const;
};
