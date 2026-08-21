#include "LimitOperator.h"

LimitOperator::LimitOperator(uint32_t limit, std::unique_ptr<Operator> child) : limit_(limit), count_(0), child_(std::move(child)) {}

void LimitOperator::Init() {
    count_ = 0;
    child_->Init();
}

std::optional<Row> LimitOperator::Next() {
    if (count_ >= limit_) {
        return std::nullopt;
    }

    std::optional<Row> row = child_->Next();
    if (row.has_value()) {
        ++count_;
    }
    return row;
}