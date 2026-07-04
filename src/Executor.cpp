#include "Executor.h"
#include "Serializer.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "Planner.h"

Executor::Executor(Catalog& catalog, WALManager& wal, Pager& pager)
    : catalog_(catalog), wal_(wal), pager_(pager) {}

ExecutionResult Executor::execute(const Statement& stmt)
{
    // BEGIN/COMMIT/ROLLBACK don't touch the catalog skip its lock entirely.
    const bool is_txn_ctrl = std::holds_alternative<BeginStatement>(stmt)
                          || std::holds_alternative<CommitStatement>(stmt)
                          || std::holds_alternative<RollbackStatement>(stmt);

    Catalog::ReadLock  rl;
    Catalog::WriteLock wl;
    if (!is_txn_ctrl) {
        if (std::holds_alternative<SelectStatement>(stmt))
            rl = catalog_.acquire_read();
        else
            wl = catalog_.acquire_write();
    }

    return std::visit([this](const auto& s) -> ExecutionResult {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, SelectStatement>)
            return execute_select(s);
        else if constexpr (std::is_same_v<T, InsertStatement>)
            return execute_insert(s);
        else if constexpr (std::is_same_v<T, UpdateStatement>)
            return execute_update(s);
        else if constexpr (std::is_same_v<T, DeleteStatement>)
            return execute_delete(s);
        else if constexpr (std::is_same_v<T, CreateTableStatement>)
            return execute_create(s);
        else if constexpr (std::is_same_v<T, CreateIndexStatement>)
            return execute_create_index(s);
        else if constexpr (std::is_same_v<T, DropTableStatement>)
            return execute_drop_table(s);
        else if constexpr (std::is_same_v<T, DropIndexStatement>)
            return execute_drop_index(s);
        else if constexpr (std::is_same_v<T, AlterTableStatement>)
            return execute_alter_table(s);
        else if constexpr (std::is_same_v<T, BeginStatement>)
            return execute_begin(s);
        else if constexpr (std::is_same_v<T, CommitStatement>)
            return execute_commit(s);
        else if constexpr (std::is_same_v<T, RollbackStatement>)
            return execute_rollback(s);
        else
            return {false, "Unknown statement type"};
    }, stmt);
}


void Executor::acquire_table_lock(const std::string& table_name, bool exclusive)
{
    if (!active_txn_.has_value()) return; // aif no transaction is active, no locks are needed - auto commit

    Transaction& txn = *active_txn_;

    if (std::find(txn.exclusive_locks.begin(), txn.exclusive_locks.end(), table_name)
        != txn.exclusive_locks.end())
        return;

    if (exclusive) {
        // upgrade shared to exclusive if already held
        auto it = std::find(txn.shared_locks.begin(), txn.shared_locks.end(), table_name);
        if (it != txn.shared_locks.end()) {
            catalog_.unlock_table_shared(table_name);
            txn.shared_locks.erase(it);
        }
        catalog_.lock_table_exclusive(table_name);
        txn.exclusive_locks.push_back(table_name);
    } else {
        if (std::find(txn.shared_locks.begin(), txn.shared_locks.end(), table_name)
            != txn.shared_locks.end())
            return;
        catalog_.lock_table_shared(table_name);
        txn.shared_locks.push_back(table_name);
    }
}


TxnContext Executor::begin_statement_txn(Transaction& local_storage, bool& autocommit)
{
    autocommit = !active_txn_.has_value();
    if (autocommit) {
        local_storage.txn_id = next_txn_id_++;
        local_storage.wal_lsns.push_back(wal_.log_begin(local_storage.txn_id));
        return TxnContext{local_storage.txn_id, &wal_, &local_storage.undo_log};
    }
    return TxnContext{active_txn_->txn_id, &wal_, &active_txn_->undo_log};
}

void Executor::end_statement_txn(uint32_t txn_id, bool autocommit)
{
    if (!autocommit) return; // part of an explicit transaction COMMIT/ROLLBACK handles it
    uint64_t commit_lsn = wal_.log_commit(txn_id);
    wal_.flush_to_lsn(commit_lsn);   // durability: WAL on disk before declaring success
    pager_.flush_all_pages();        // force all data pages to disk
    wal_.checkpoint();                // seal checkpoint so recovery skips this record
}

ExecutionResult Executor::execute_create(const CreateTableStatement& stmt)
{
    if (catalog_.table_exists(stmt.table_name)) {
        return {false, "Table '" + stmt.table_name + "' already exists", {}, {}};
    }

    bool ok = catalog_.create_table(stmt.table_name, stmt.columns);
    if (!ok) {
        return {false, "Failed to create table '" + stmt.table_name + "'", {}, {}};
    }

    pager_.flush_all_pages();
    wal_.checkpoint(checkpoint_floor());
    return {true, "Table '" + stmt.table_name + "' created successfully", {}, {}};
}

ExecutionResult Executor::execute_create_index(const CreateIndexStatement& stmt)
{
    bool ok = catalog_.create_secondary_index(stmt.index_name, stmt.table_name, stmt.column_name);
    if (!ok) {
        return {false, "Failed to create index '" + stmt.index_name + "'", {}, {}};
    }
    pager_.flush_all_pages();
    wal_.checkpoint(checkpoint_floor());
    return {true, "Index '" + stmt.index_name + "' created on " + stmt.table_name + "(" + stmt.column_name + ")", {}, {}};
}

ExecutionResult Executor::execute_drop_table(const DropTableStatement& stmt)
{
    if (!catalog_.table_exists(stmt.table_name)) {
        if (stmt.if_exists)
            return {true, "Table '" + stmt.table_name + "' does not exist, skipped", {}, {}};
        return {false, "Table '" + stmt.table_name + "' does not exist", {}, {}};
    }
    if (!catalog_.drop_table(stmt.table_name))
        return {false, "Cannot drop table '" + stmt.table_name + "' (FK references exist)", {}, {}};
    pager_.flush_all_pages();
    wal_.checkpoint(checkpoint_floor());
    return {true, "Table '" + stmt.table_name + "' dropped", {}, {}};
}

ExecutionResult Executor::execute_drop_index(const DropIndexStatement& stmt)
{
    if (!catalog_.drop_index(stmt.index_name))
        return {false, "Index '" + stmt.index_name + "' does not exist or could not be dropped", {}, {}};
    pager_.flush_all_pages();
    wal_.checkpoint(checkpoint_floor());
    return {true, "Index '" + stmt.index_name + "' dropped", {}, {}};
}

ExecutionResult Executor::execute_alter_table(const AlterTableStatement& stmt)
{
    using Action = AlterTableStatement::Action;
    bool ok = false;
    std::string msg;
    switch (stmt.action) {
        case Action::ADD_COLUMN:
            ok  = catalog_.alter_table_add_column(stmt.table_name, stmt.new_col);
            msg = ok ? "Column '" + stmt.new_col.name + "' added to '" + stmt.table_name + "'"
                     : "Failed to add column";
            break;
        case Action::DROP_COLUMN:
            ok  = catalog_.alter_table_drop_column(stmt.table_name, stmt.target_column);
            msg = ok ? "Column '" + stmt.target_column + "' dropped from '" + stmt.table_name + "'"
                     : "Failed to drop column (PK, FK reference, or not found)";
            break;
        case Action::RENAME_COLUMN:
            ok  = catalog_.alter_table_rename_column(stmt.table_name, stmt.target_column, stmt.new_column_name);
            msg = ok ? "Column '" + stmt.target_column + "' renamed to '" + stmt.new_column_name + "'"
                     : "Failed to rename column (not found or name conflict)";
            break;
    }
    if (ok) {
        pager_.flush_all_pages();
        wal_.checkpoint(checkpoint_floor());
    }
    return {ok, msg, {}, {}};
}


ExecutionResult Executor::execute_insert(const InsertStatement& stmt)
{
    acquire_table_lock(stmt.table_name, true);

    Transaction local_txn;
    bool autocommit;
    TxnContext ctx = begin_statement_txn(local_txn, autocommit);

    Planner planner(catalog_);
    planner.set_txn_context(&ctx);
    auto plan = planner.create_plan(stmt);
    plan->Init();
    auto result = plan->Next();
    int count = std::get<int32_t>(result.value()[0]);

    end_statement_txn(ctx.txn_id, autocommit);
    return {true, std::to_string(count) + " row(s) inserted", {}, {}};
}

ExecutionResult Executor::execute_select(const SelectStatement& stmt)
{
    acquire_table_lock(stmt.table_name, false);

    Planner planner(catalog_);

    auto plan = planner.create_plan(stmt);
    plan->Init();
    std::vector<Row> result_rows;
    std::vector<ColumnDefinition> result_cols = plan->GetOutputSchema();
    std::vector<std::string> result_col_names;
    for(const auto& col_def : result_cols){
        result_col_names.push_back(col_def.name);
    }
    auto row = plan->Next();
    while (row.has_value()) {
        result_rows.push_back(row.value());
        row = plan->Next();
    }

    return {true, std::to_string(result_rows.size()) + " row(s) returned", result_rows, result_col_names};
}

ExecutionResult Executor::execute_update(const UpdateStatement& stmt)
{
    acquire_table_lock(stmt.table_name, true);

    Transaction local_txn;
    bool autocommit;
    TxnContext ctx = begin_statement_txn(local_txn, autocommit);

    Planner planner(catalog_);
    planner.set_txn_context(&ctx);
    auto plan = planner.create_plan(stmt);
    plan->Init();
    auto result = plan->Next();
    int updated = std::get<int32_t>(result.value()[0]);

    end_statement_txn(ctx.txn_id, autocommit);
    return {true, std::to_string(updated) + " row(s) updated", {}, {}};
}

ExecutionResult Executor::execute_delete(const DeleteStatement& stmt)
{
    acquire_table_lock(stmt.table_name, true);

    Transaction local_txn;
    bool autocommit;
    TxnContext ctx = begin_statement_txn(local_txn, autocommit);

    Planner planner(catalog_);
    planner.set_txn_context(&ctx);
    auto plan = planner.create_plan(stmt);
    plan->Init();
    auto result = plan->Next();
    const Row& row = result.value();
    int deleted = std::get<int32_t>(row[0]);
    int blocked = row.size() > 1 ? std::get<int32_t>(row[1]) : 0;

    end_statement_txn(ctx.txn_id, autocommit);

    std::string msg = std::to_string(deleted) + " row(s) deleted";
    if (blocked > 0) {
        const std::string& reason = std::get<std::string>(row[2]);
        msg += " (" + std::to_string(blocked) + " row(s) skipped - still referenced by " + reason + ")";
    }
    return {true, msg, {}, {}};
}


ExecutionResult Executor::execute_begin(const BeginStatement&)
{
    if (active_txn_.has_value())
        return {false, "Transaction " + std::to_string(active_txn_->txn_id)
                + " is already active. COMMIT or ROLLBACK first", {}, {}};

    uint32_t txn_id = next_txn_id_++;
    Transaction txn;
    txn.txn_id = txn_id;

    uint64_t lsn = wal_.log_begin(txn_id);
    txn.wal_lsns.push_back(lsn);

    active_txn_ = std::move(txn);
    return {true, "Transaction " + std::to_string(txn_id) + " started", {}, {}};
}

ExecutionResult Executor::execute_commit(const CommitStatement&)
{
    if (!active_txn_.has_value())
        return {false, "No active transaction to commit", {}, {}};

    Transaction& txn = *active_txn_;

    uint64_t commit_lsn = wal_.log_commit(txn.txn_id);
    txn.wal_lsns.push_back(commit_lsn);
    wal_.flush_to_lsn(commit_lsn); // durability: WAL on disk before declaring success
    pager_.flush_all_pages();       // force all data pages to disk
    wal_.checkpoint();              // seal checkpoint so recovery skips pre-commit records

    for (const auto& t : txn.exclusive_locks) catalog_.unlock_table_exclusive(t);
    for (const auto& t : txn.shared_locks)    catalog_.unlock_table_shared(t);

    uint32_t txn_id = txn.txn_id;
    active_txn_.reset();
    return {true, "Transaction " + std::to_string(txn_id) + " committed", {}, {}};
}

ExecutionResult Executor::execute_rollback(const RollbackStatement&)
{
    if (!active_txn_.has_value())
        return {false, "No active transaction to roll back", {}, {}};

    Transaction& txn = *active_txn_;

    // Apply undo log in reverse order to reverse every data change made by this txn.
    Serializer sr;
    for (int i = static_cast<int>(txn.undo_log.size()) - 1; i >= 0; --i) {
        const UndoEntry& e = txn.undo_log[i];
        Table* table = catalog_.get_table_by_root_page(e.page_id);
        if (!table) continue;

        if (e.op == UndoOpType::INSERT) {
            Row row = sr.deserialize(table->get_columns(),
                                     e.after.data(), static_cast<uint16_t>(e.after.size()));
            uint32_t pk = table->extract_primary_key(row);
            table->remove_row(pk);
        } else if (e.op == UndoOpType::UPDATE) {
            Row old_row = sr.deserialize(table->get_columns(),
                                          e.before.data(), static_cast<uint16_t>(e.before.size()));
            uint32_t pk = table->extract_primary_key(old_row);
            table->update_row(pk, old_row);
        } else if (e.op == UndoOpType::DELETE_OP) {
            Row row = sr.deserialize(table->get_columns(),
                                      e.before.data(), static_cast<uint16_t>(e.before.size()));
            table->insert_row(row);
        }
    }

    uint64_t rollback_lsn = wal_.log_rollback(txn.txn_id);
    txn.wal_lsns.push_back(rollback_lsn);
    wal_.flush_to_lsn(rollback_lsn);

    for (const auto& t : txn.exclusive_locks) catalog_.unlock_table_exclusive(t);
    for (const auto& t : txn.shared_locks) catalog_.unlock_table_shared(t);

    uint32_t txn_id = txn.txn_id;
    active_txn_.reset();
    return {true, "Transaction " + std::to_string(txn_id) + " rolled back", {}, {}};
}
