//===----------------------------------------------------------------------===//
//                         DuckDB
//
// execution/operator/join/physical_join.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "execution/physical_operator.hpp"
#include "planner/operator/logical_join.hpp"

namespace duckdb {

//! PhysicalJoin represents the base class of the join operators
class PhysicalJoin : public PhysicalOperator {
public:
	PhysicalJoin(LogicalOperator &op, PhysicalOperatorType type, vector<JoinCondition> cond, JoinType join_type);

	string ExtraRenderInformation() override;

	vector<JoinCondition> conditions;
	JoinType type;
};
} // namespace duckdb