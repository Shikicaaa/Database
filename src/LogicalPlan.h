#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "Parser/Parser.h"

enum class LogicalNodeType {
    SCAN,
    FILTER,
    PROJECT,
    INDEX_SCAN
};

class LogicalNode {
public:
    virtual ~LogicalNode() = default;
    virtual LogicalNodeType GetType() const = 0;
    
    std::vector<std::unique_ptr<LogicalNode>> children_;
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

