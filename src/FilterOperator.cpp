#include "FilterOperator.h"
#include "LikeOperator.h"
#include "Planner.h"
#include "Logger.h"
#include <algorithm>

FilterOperator::FilterOperator(std::unique_ptr<Operator> child, const std::optional<WhereClause>& where_clause, Catalog* catalog, const std::string& outer_alias)
    : catalog_(catalog), outer_alias_(outer_alias),
      child_(std::move(child)), where_clause_(where_clause) {}

void FilterOperator::Init() {
    child_->Init();
}

const std::vector<ColumnDefinition>& FilterOperator::GetOutputSchema() const {
    return child_->GetOutputSchema();
}

std::optional<Row> FilterOperator::Next() {
    std::optional<Row> current_row = child_->Next();

    while (current_row.has_value()) {
        if (!where_clause_.has_value() || !where_clause_.value()) return current_row;
        if (evaluate(*where_clause_.value(), *current_row, child_->GetOutputSchema()))
            return current_row;
        current_row = child_->Next();
    }
    return std::nullopt;
}

int FilterOperator::find_column_index(const std::string& col_name,
                                const std::vector<ColumnDefinition>& schema) const
{
    for (int i = 0; i < (int)schema.size(); i++) {
        std::string a = col_name, b = schema[i].name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (a == b) return i;
    }
    for(int i = 0; i < (int)schema.size(); i++){
        std::string b = schema[i].name;
        auto dot = b.rfind(".");
        if(dot != std::string::npos){
            b = b.substr(dot + 1);
        }
        std::string a = col_name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if(a == b) return i;
    }
    return -1;
}

bool FilterOperator::compare_values(const Value& row_val,
                              const std::string& op,
                              const Value& where_val) const
{
    if (op == "IS NULL")     return std::holds_alternative<std::monostate>(row_val);
    if (op == "IS NOT NULL") return !std::holds_alternative<std::monostate>(row_val);

    if (op == "LIKE" || op == "ILIKE") {
        if (!std::holds_alternative<std::string>(row_val) || !std::holds_alternative<std::string>(where_val)) {
            return false; // LIKE can only be applied to strings
        }
        return like_match(std::get<std::string>(where_val), std::get<std::string>(row_val), op == "ILIKE");
    }

    auto cmp = [&op](const auto& a, const auto& b) -> bool {
        if (op == "=")  return a == b;
        if (op == "!=") return a != b;
        if (op == "<")  return a <  b;
        if (op == ">")  return a >  b;
        if (op == "<=") return a <= b;
        if (op == ">=") return a >= b;
        return false;
    };

    // int == int
    if (std::holds_alternative<int32_t>(row_val) &&
        std::holds_alternative<int32_t>(where_val)) {
        return cmp(std::get<int32_t>(row_val), std::get<int32_t>(where_val));
    }

    // double == double
    if (std::holds_alternative<double>(row_val) &&
        std::holds_alternative<double>(where_val)) {
        return cmp(std::get<double>(row_val), std::get<double>(where_val));
    }

    // int i doubleo onda konvertujemo int u double
    if (std::holds_alternative<int32_t>(row_val) &&
        std::holds_alternative<double>(where_val)) {
        return cmp((double)std::get<int32_t>(row_val), std::get<double>(where_val));
    }
    if (std::holds_alternative<double>(row_val) &&
        std::holds_alternative<int32_t>(where_val)) {
        return cmp(std::get<double>(row_val), (double)std::get<int32_t>(where_val));
    }

    // string == string
    if (std::holds_alternative<std::string>(row_val) &&
        std::holds_alternative<std::string>(where_val)) {
        return cmp(std::get<std::string>(row_val), std::get<std::string>(where_val));
    }

    // bool == bool
    if (std::holds_alternative<bool>(row_val) &&
        std::holds_alternative<bool>(where_val)) {
        return cmp(std::get<bool>(row_val), std::get<bool>(where_val));
    }

    // NULL = NULL => true, sve ostalo false
    if (std::holds_alternative<std::monostate>(row_val) &&
        std::holds_alternative<std::monostate>(where_val)) {
        return op == "=";
    }

    return false;
}


Value FilterOperator::resolve_outer_value(const std::string& qualifier, const std::string& column,
                                           const Row& outer_row, const std::vector<ColumnDefinition>& outer_schema) const
{
    std::string lookup = qualifier.empty() ? column : qualifier + "." + column;
    int idx = find_column_index(lookup, outer_schema);
    if (idx == -1) {
        throw std::runtime_error("Correlated column '" + lookup + "' not found in outer row");
    }
    return outer_row[idx];
}

std::shared_ptr<Condition> FilterOperator::substitute_outer_refs(
    const std::shared_ptr<Condition>& cond,
    const Row& outer_row,
    const std::vector<ColumnDefinition>& outer_schema) const
{
    if (!cond) return nullptr;

    auto copy = std::make_shared<Condition>(*cond);

    if (copy->type == ConditionType::AND || copy->type == ConditionType::OR) {
        copy->children[0] = substitute_outer_refs(cond->children[0], outer_row, outer_schema);
        copy->children[1] = substitute_outer_refs(cond->children[1], outer_row, outer_schema);
        return copy;
    }
    if (copy->type == ConditionType::NOT) {
        copy->children[0] = substitute_outer_refs(cond->children[0], outer_row, outer_schema);
        return copy;
    }

    if (copy->table_qualifier == outer_alias_) {
        // TODO: Implement LHS substitution for correlated subqueries if needed
    }

    if (copy->rhs_is_column && copy->rhs_table_qualifier == outer_alias_) {
        Value v = resolve_outer_value(copy->rhs_table_qualifier, copy->rhs_column, outer_row, outer_schema);
        copy->rhs_is_column = false;
        copy->value = v;
    }

    return copy;
}

bool FilterOperator::evaluate_correlated_subquery(const Condition& cond, const Row& outer_row, const std::vector<ColumnDefinition>& outer_schema) const
{
    if (!catalog_) {
        throw std::runtime_error("Correlated subquery requires Catalog access but none was provided");
    }

    SelectStatement sub_copy = *cond.subquery;
    if (sub_copy.where_clause.has_value() && sub_copy.where_clause.value()) {
        sub_copy.where_clause = substitute_outer_refs(sub_copy.where_clause.value(), outer_row, outer_schema);
    }

    Planner sub_planner(*catalog_);
    auto plan = sub_planner.create_plan(Statement{sub_copy});
    plan->Init();
    bool has_row = plan->Next().has_value();

    if (cond.op == "EXISTS") return has_row;
    if (cond.op == "NOT EXISTS") return !has_row;

    throw std::runtime_error("Unsupported correlated subquery operator: " + cond.op);
}

bool FilterOperator::evaluate(const Condition& cond, const Row& row,
                               const std::vector<ColumnDefinition>& schema) const
{
    switch (cond.type) {
        case ConditionType::AND:
            return evaluate(*cond.children[0], row, schema) &&
                   evaluate(*cond.children[1], row, schema);

        case ConditionType::OR:
            return evaluate(*cond.children[0], row, schema) ||
                   evaluate(*cond.children[1], row, schema);

        case ConditionType::NOT:
            return !evaluate(*cond.children[0], row, schema);

        case ConditionType::COMPARISON: {
            if (cond.subquery && (cond.op == "EXISTS" || cond.op == "NOT EXISTS")) {
                return evaluate_correlated_subquery(cond, row, schema);
            }

            std::string lookup = cond.column;
            if (!cond.table_qualifier.empty())
                lookup = cond.table_qualifier + "." + cond.column;

            int col_index = find_column_index(lookup, schema);
            if (col_index == -1) {
                LOG_ERROR("Filter", "Column '" + lookup + "' not found in schema");
                throw std::runtime_error("Column '" + lookup + "' not found in schema");
            }

            if (cond.op == "IN" || cond.op == "NOT IN") {
                bool found = false;
                for (const auto& v : cond.value_list) {
                    if (compare_values(row[col_index], "=", v)) { found = true; break; }
                }
                return cond.op == "IN" ? found : !found;
            }

            if (cond.rhs_is_column) {
                std::string rhs_lookup = cond.rhs_table_qualifier.empty()
                    ? cond.rhs_column : cond.rhs_table_qualifier + "." + cond.rhs_column;
                int rhs_idx = find_column_index(rhs_lookup, schema);
                if (rhs_idx == -1) {
                    throw std::runtime_error("Column '" + rhs_lookup + "' not found in schema");
                }
                return compare_values(row[col_index], cond.op, row[rhs_idx]);
            }

            return compare_values(row[col_index], cond.op, cond.value);
        }

        case ConditionType::LITERAL_BOOL:
            return cond.literal_value;
    }
    return false;
}