//===----------------------------------------------------------------------===//
//                         DuckDB
//
// execution/operator/filter/physical_filter.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/execution/physical_operator.hpp"

namespace duckdb {

//! PhysicalFilter represents a filter operator. It removes non-matching tupels
//! from the result. Note that it does not physically change the data, it only
//! adds a selection vector to the chunk.
class PhysicalFilter : public PhysicalOperator {
public:
	PhysicalFilter(vector<TypeId> types, vector<unique_ptr<Expression>> select_list)
	    : PhysicalOperator(PhysicalOperatorType::FILTER, types), expressions(std::move(select_list)) {
	}

	vector<unique_ptr<Expression>> expressions;

public:
	void GetChunkInternal(ClientContext &context, DataChunk &chunk, PhysicalOperatorState *state) override;

	string ExtraRenderInformation() const override;
};
} // namespace duckdb
