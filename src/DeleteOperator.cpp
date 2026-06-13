#include "DeleteOperator.h"

DeleteOperator::DeleteOperator(std::unique_ptr<Operator> child, Table* table) : child_(std::move(child)), table_(table) {
    dummy_schema_.push_back(ColumnDefinition{"deleted_rows", DataType::INT, false, false, false, 4});
}

void DeleteOperator::Init() {
    child_->Init();
    has_executed_ = false;
}

const std::vector<ColumnDefinition>& DeleteOperator::GetOutputSchema() const {
    return dummy_schema_;
}

std::optional<Row> DeleteOperator::Next() {
    if (has_executed_) {
        return std::nullopt;
    }

    std::vector<uint32_t> pks;
    for (auto row = child_->Next(); row.has_value(); row = child_->Next()) {
        pks.push_back(table_->extract_primary_key(row.value()));
    }

    int delete_count = 0;
    for (uint32_t pk : pks) {
        if (!table_->remove_row(pk)) {
            std::cerr << "ERROR: Failed to delete row with primary key " << pk
                      << " from table '" << table_->get_columns()[0].name << "'\n";
        } else {
            delete_count++;
        }
    }

    has_executed_ = true;
    return Row{Value(delete_count)};
}