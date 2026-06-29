#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "JoinTypes.h"

enum class JoinType;
struct JoinCondition;

enum class LogicalNodeType {
    SCAN,
    FILTER,
    PROJECT,
    INDEX_SCAN,
    SECONDARY_INDEX_SCAN,
    JOIN
};


class LogicalNode {
public:
    virtual ~LogicalNode() = default;
    virtual LogicalNodeType GetType() const = 0;
    
    std::vector<std::unique_ptr<LogicalNode>> children_;
};

class LogicalJoin : public LogicalNode {
public:
    JoinType join_type_;
    JoinCondition condition_;
    std::string left_alias_;
    std::string right_alias_;

    explicit LogicalJoin(
        JoinType type, JoinCondition condition,
        std::unique_ptr<LogicalNode> left,
        std::unique_ptr<LogicalNode> right,
        const std::string& left_alias  = "",
        const std::string& right_alias = ""
    ): join_type_(type), condition_(condition),
        left_alias_(left_alias), right_alias_(right_alias)
    {
        children_.push_back(std::move(left));
        children_.push_back(std::move(right));
    }
    LogicalNodeType GetType() const override { return LogicalNodeType::JOIN; }
};

class LogicalScan : public LogicalNode {
public:
    std::string table_name_;
    
    explicit LogicalScan(const std::string& table_name) : table_name_(table_name) {}
    LogicalNodeType GetType() const override { return LogicalNodeType::SCAN; }
};

class LogicalFilter : public LogicalNode {
public:
    std::optional<WhereClause> where_clause_;
    
    explicit LogicalFilter(std::optional<WhereClause> where, std::unique_ptr<LogicalNode> child) 
        : where_clause_(where) {
        children_.push_back(std::move(child));
    }
    LogicalNodeType GetType() const override { return LogicalNodeType::FILTER; }
};

class LogicalProject : public LogicalNode {
public:
    std::vector<std::string> columns_;
    
    explicit LogicalProject(std::vector<std::string> columns, std::unique_ptr<LogicalNode> child) 
        : columns_(columns) {
        children_.push_back(std::move(child));
    }
    LogicalNodeType GetType() const override { return LogicalNodeType::PROJECT; }
};

class LogicalIndexScan : public LogicalNode {
public:
    std::string table_name_;
    uint32_t primary_key_value_;

    LogicalIndexScan(const std::string& table_name, uint32_t pk_val)
        : table_name_(table_name), primary_key_value_(pk_val) {}

    LogicalNodeType GetType() const override { return LogicalNodeType::INDEX_SCAN; }
};

class LogicalSecondaryIndexScan : public LogicalNode {
public:
    std::string table_name_;
    std::string index_name_;
    int32_t     search_value_;

    LogicalSecondaryIndexScan(std::string table, std::string index, int32_t val)
        : table_name_(std::move(table)), index_name_(std::move(index)), search_value_(val) {}

    LogicalNodeType GetType() const override { return LogicalNodeType::SECONDARY_INDEX_SCAN; }
};