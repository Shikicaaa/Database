#include "Planner.h"
#include "IndexScanOperator.h"
#include "SecondaryIndexScanOperator.h"
#include "SeqScanOperator.h"
#include "FilterOperator.h"
#include "ProjectOperator.h"
#include "InsertOperator.h"
#include "ValuesOperator.h"
#include "UpdateOperator.h"
#include "DeleteOperator.h"
#include "LogicalPlan.h"
#include <variant>

std::unique_ptr<Operator> Planner::create_plan(const Statement& stmt) {
    return std::visit([this](const auto& s) -> std::unique_ptr<Operator> {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, SelectStatement>)
            return plan_select(s);
        else if constexpr (std::is_same_v<T, InsertStatement>)
            return plan_insert(s);
        else if constexpr (std::is_same_v<T, UpdateStatement>)
            return plan_update(s);
        else if constexpr (std::is_same_v<T, DeleteStatement>)
            return plan_delete(s);
        else
            throw std::runtime_error("Unknown statement type");
    }, stmt);
}

std::unique_ptr<Operator> Planner::plan_select(const SelectStatement& stmt) {
    Table* left_table = catalog_.get_table(stmt.table_name);
    if (!left_table) {
        throw std::runtime_error("Table '" + stmt.table_name + "' does not exist");
    }

    std::unique_ptr<Operator> current_op;
    bool where_handled = false;

    if (stmt.joins.empty() && stmt.where_clause.has_value() &&
        stmt.where_clause->op == "=" &&
        (std::holds_alternative<int32_t>(stmt.where_clause->value) ||
         std::holds_alternative<std::string>(stmt.where_clause->value)))
    {
        auto idx = catalog_.find_index_for_column(stmt.table_name, stmt.where_clause->column);
        if (idx.has_value()) {
            BTree* idx_btree = catalog_.get_index_btree(idx->index_name);
            if (idx_btree) {
                const auto& wc = stmt.where_clause.value();
                uint32_t key;
                std::string orig;
                if (std::holds_alternative<int32_t>(wc.value)) {
                    key  = static_cast<uint32_t>(std::get<int32_t>(wc.value));
                    orig = "";
                } else {
                    orig = std::get<std::string>(wc.value);
                    key  = Catalog::hash_varchar(orig);
                }
                int col_idx = -1;
                const auto& cols = left_table->get_columns();
                for (int i = 0; i < (int)cols.size(); i++)
                    if (cols[i].name == wc.column) { col_idx = i; break; }
                current_op = std::make_unique<SecondaryIndexScanOperator>(
                    left_table, idx_btree, key, orig, col_idx);
                where_handled = true;
            }
        }
    }

    if (!current_op) {
        current_op = std::make_unique<SeqScanOperator>(left_table);
    }

    std::string current_alias = stmt.table_alias;

    for (const auto& join : stmt.joins) {
        Table* right_table = catalog_.get_table(join.right_table);
        if (!right_table) {
            throw std::runtime_error("Table '" + join.right_table + "' does not exist");
        }

        std::unique_ptr<Operator> right_op = std::make_unique<SeqScanOperator>(right_table);
        current_op = std::make_unique<JoinOperator>(
            std::move(current_op), std::move(right_op), join.condition,
            current_alias, join.right_alias, join.type);

        current_alias = "";
    }

    if (!where_handled && stmt.where_clause.has_value()) {
        current_op = std::make_unique<FilterOperator>(std::move(current_op), stmt.where_clause);
    }

    if (!stmt.columns.empty() && !(stmt.columns.size() == 1 && stmt.columns[0] == "*")) {
        current_op = std::make_unique<ProjectOperator>(std::move(current_op), stmt.columns);
    }

    return current_op;
}

std::unique_ptr<LogicalNode> Planner::create_logical_plan_for_select(const SelectStatement& stmt) {
    std::unique_ptr<LogicalNode> current_node = std::make_unique<LogicalScan>(stmt.table_name);

    std::string current_alias = stmt.table_alias;
    for(const auto& join : stmt.joins) {
        std::unique_ptr<LogicalNode> right_node = std::make_unique<LogicalScan>(join.right_table);
        current_node = std::make_unique<LogicalJoin>(
            join.type, join.condition,
            std::move(current_node), std::move(right_node),
            current_alias, join.right_alias);
        current_alias = "";
    }
    
    std::unique_ptr<LogicalNode> filter = std::make_unique<LogicalFilter>(stmt.where_clause, std::move(current_node));

    std::unique_ptr<LogicalNode> project = std::make_unique<LogicalProject>(stmt.columns, std::move(filter));

    return project;
}

std::unique_ptr<LogicalNode> Planner::optimize_select(std::unique_ptr<LogicalNode> plan) {
    LogicalFilter* filter = nullptr;

    if (plan->GetType() == LogicalNodeType::PROJECT) {
        LogicalProject* project_node = static_cast<LogicalProject*>(plan.get());
        LogicalNode* filter_node = project_node->children_[0].get();
        if (filter_node->GetType() == LogicalNodeType::FILTER) {
            filter = static_cast<LogicalFilter*>(filter_node);
        }
    } else if (plan->GetType() == LogicalNodeType::FILTER) {
        filter = static_cast<LogicalFilter*>(plan.get());
    }

    if (!filter) return plan;

    LogicalNode* scan_node = filter->children_[0].get();
    if (scan_node->GetType() != LogicalNodeType::SCAN) return plan;

    LogicalScan* scan = static_cast<LogicalScan*>(scan_node);

    Table* table = catalog_.get_table(scan->table_name_);
    if (!table) {
        throw std::runtime_error("Table '" + scan->table_name_ + "' does not exist");
    }

    std::optional<uint32_t> pk = try_extract_pk_from_where(filter->where_clause_, table->get_columns());
    if (pk.has_value()) {
        filter->children_[0] = std::make_unique<LogicalIndexScan>(scan->table_name_, pk.value());
        filter->where_clause_ = std::nullopt;
    } else if (filter->where_clause_.has_value() &&
               filter->where_clause_->op == "=" &&
               (std::holds_alternative<int32_t>(filter->where_clause_->value) ||
                std::holds_alternative<std::string>(filter->where_clause_->value)))
    {
        auto idx = catalog_.find_index_for_column(scan->table_name_, filter->where_clause_->column);
        if (idx.has_value()) {
            const auto& wc = filter->where_clause_.value();
            uint32_t key;
            std::string orig;
            if (std::holds_alternative<int32_t>(wc.value)) {
                key  = static_cast<uint32_t>(std::get<int32_t>(wc.value));
                orig = "";
            } else {
                orig = std::get<std::string>(wc.value);
                key  = Catalog::hash_varchar(orig);
            }
            Table* t = catalog_.get_table(scan->table_name_);
            int col_idx = -1;
            if (t) {
                const auto& cols = t->get_columns();
                for (int i = 0; i < (int)cols.size(); i++)
                    if (cols[i].name == wc.column) { col_idx = i; break; }
            }
            filter->children_[0] = std::make_unique<LogicalSecondaryIndexScan>(
                scan->table_name_, idx->index_name, key, orig, col_idx);
            filter->where_clause_ = std::nullopt;
        }
    }

    return plan;
}

std::unique_ptr<Operator> Planner::plan_insert(const InsertStatement& stmt) {
    Table* table = catalog_.get_table(stmt.table_name);
    if(!table) {
        throw std::runtime_error("Table '" + stmt.table_name + "' does not exist");
    }

    std::vector<Row> rows;
    rows.push_back(stmt.values);

    auto values_op = std::make_unique<ValuesOperator>(rows, table->get_columns());
    return std::make_unique<InsertOperator>(table, std::move(values_op), &catalog_);
}

std::unique_ptr<Operator> Planner::plan_update(const UpdateStatement& stmt) {
    Table* table = catalog_.get_table(stmt.table_name);
    if(!table) {
        throw std::runtime_error("Table '" + stmt.table_name + "' does not exist");
    }

    std::unique_ptr<LogicalNode> scan = std::make_unique<LogicalScan>(stmt.table_name);
    std::unique_ptr<LogicalNode> filter = std::make_unique<LogicalFilter>(stmt.where_clause, std::move(scan));
    
    std::unique_ptr<LogicalNode> optimized_plan = optimize_select(std::move(filter));
    std::unique_ptr<Operator> child = create_physical_plan(std::move(optimized_plan));

    return std::make_unique<UpdateOperator>(std::move(child), table, stmt.set_clauses, &catalog_);
}

std::unique_ptr<Operator> Planner::plan_delete(const DeleteStatement& stmt) {
    Table* table = catalog_.get_table(stmt.table_name);
    if (!table) {
        throw std::runtime_error("Table '" + stmt.table_name + "' does not exist");
    }

    std::unique_ptr<LogicalNode> scan = std::make_unique<LogicalScan>(stmt.table_name);
    std::unique_ptr<LogicalNode> filter = std::make_unique<LogicalFilter>(stmt.where_clause, std::move(scan));
    
    std::unique_ptr<LogicalNode> optimized_plan = optimize_select(std::move(filter));
    std::unique_ptr<Operator> child = create_physical_plan(std::move(optimized_plan));

    return std::make_unique<DeleteOperator>(std::move(child), table, &catalog_);
}

std::optional<uint32_t> Planner::try_extract_pk_from_where(
    const std::optional<WhereClause>& where,
    const std::vector<ColumnDefinition>& schema) const
{
    if (!where.has_value()) return std::nullopt;
    const auto& clause = where.value();
    if (clause.op != "=") return std::nullopt;

    for (const auto& col_def : schema) {
        if (col_def.is_primary_key && col_def.name == clause.column) {
            if (std::holds_alternative<int32_t>(clause.value)) {
                return std::get<int32_t>(clause.value);
            }
        }
    }
    return std::nullopt;
}

std::unique_ptr<Operator> Planner::create_physical_plan(std::unique_ptr<LogicalNode> node) {
    switch (node->GetType()) {

        case LogicalNodeType::PROJECT: {
            auto* p = static_cast<LogicalProject*>(node.get());
            auto child = create_physical_plan(std::move(p->children_[0]));
            return std::make_unique<ProjectOperator>(std::move(child), p->columns_);
        }

        case LogicalNodeType::FILTER: {
            auto* f = static_cast<LogicalFilter*>(node.get());
            auto child = create_physical_plan(std::move(f->children_[0]));
            return std::make_unique<FilterOperator>(std::move(child), f->where_clause_);
        }

        case LogicalNodeType::SCAN: {
            auto* s = static_cast<LogicalScan*>(node.get());
            Table* table = catalog_.get_table(s->table_name_);
            return std::make_unique<SeqScanOperator>(table);
        }

        case LogicalNodeType::INDEX_SCAN: {
            auto* i = static_cast<LogicalIndexScan*>(node.get());
            Table* table = catalog_.get_table(i->table_name_);
            return std::make_unique<IndexScanOperator>(table, i->primary_key_value_);
        }

        case LogicalNodeType::SECONDARY_INDEX_SCAN: {
            auto* si = static_cast<LogicalSecondaryIndexScan*>(node.get());
            Table* table = catalog_.get_table(si->table_name_);
            BTree* idx_btree = catalog_.get_index_btree(si->index_name_);
            return std::make_unique<SecondaryIndexScanOperator>(
                table, idx_btree, si->index_key_, si->original_string_, si->column_index_);
        }

        case LogicalNodeType::JOIN: {
            auto* j = static_cast<LogicalJoin*>(node.get());
            auto left_child = create_physical_plan(std::move(j->children_[0]));
            auto right_child = create_physical_plan(std::move(j->children_[1]));
            return std::make_unique<JoinOperator>(
                std::move(left_child), std::move(right_child),
                j->condition_, j->left_alias_, j->right_alias_, j->join_type_);
        }

        default:
            throw std::runtime_error("Unknown logical node type");
    }
}