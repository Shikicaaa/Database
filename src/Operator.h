#pragma once

#include <optional>
#include <vector>
#include "Serializer.h"

class Operator {
public:
    virtual ~Operator() = default;

    virtual void Init() = 0;

    virtual std::optional<Row> Next() = 0;

    virtual const std::vector<ColumnDefinition>& GetOutputSchema() const = 0;
};
