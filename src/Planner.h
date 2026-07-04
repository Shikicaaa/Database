#pragma once
#include "Parser/Parser.h"
#include "Catalog.h"
#include "LogicalPlan.h"
#include "Operator.h"
#include "TxnContext.h"
#include <memory>

class Planner {
public:
    explicit Planner(Catalog& catalog) : catalog_(catalog) {}

    void set_txn_context(TxnContext* ctx) { txn_ctx_ = ctx; }

    std::unique_ptr<Operator> create_plan(const Statement& stmt);

private:
    Catalog&    catalog_;
    TxnContext* txn_ctx_ = nullptr;
    
    std::unique_ptr<Operator> plan_select(const SelectStatement& stmt);
    std::unique_ptr<Operator> plan_insert(const InsertStatement& stmt);
    std::unique_ptr<Operator> plan_update(const UpdateStatement& stmt);
    std::unique_ptr<Operator> plan_delete(const DeleteStatement& stmt);

    std::unique_ptr<LogicalNode> create_logical_plan_for_select(const SelectStatement& stmt);
    std::unique_ptr<LogicalNode> optimize_select(std::unique_ptr<LogicalNode> plan);
    std::unique_ptr<Operator> create_physical_plan(std::unique_ptr<LogicalNode> logical_plan);
    
    std::optional<uint32_t> try_extract_pk_from_where(
        const std::optional<WhereClause>& where,
        const std::vector<ColumnDefinition>& schema) const;
};