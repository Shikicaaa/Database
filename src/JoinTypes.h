#ifndef JOIN_TYPES_H
#define JOIN_TYPES_H

#include <string>

enum class JoinType {
    INNER,
    LEFT,
    RIGHT,
    FULL_OUTER
};

struct JoinCondition {
    std::string left_column;  // e.g., "e.DepartmentID"
    std::string right_column; // e.g., "d.ID"
    std::string op; // = < > etc...
};

#endif // JOIN_TYPES_H