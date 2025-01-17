//===----------------------------------------------------------------------===//
//                         DuckDB
//
// storage/column_data.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/storage/table/append_state.hpp"
#include "duckdb/storage/table/scan_state.hpp"
#include "duckdb/storage/table/persistent_segment.hpp"

namespace duckdb {
class DataTable;
class PersistentSegment;
class Transaction;

class ColumnData {
public:
	ColumnData();
	//! Set up the column data with the set of persistent segments, returns the amount of rows
	void Initialize(vector<unique_ptr<PersistentSegment>> &segments);

	//! The type of the column
	TypeId type;
	//! The table of the column
	DataTable *table;
	//! The column index of the column
	index_t column_idx;
	//! The segments holding the data of the column
	SegmentTree data;
	//! The amount of persistent rows
	index_t persistent_rows;

public:
	//! Initialize a scan of the column
	void InitializeScan(ColumnScanState &state);
	//! Scan the next vector from the column
	void Scan(Transaction &transaction, ColumnScanState &state, Vector &result);
	//! Scan the next vector from the column, throwing an exception if there are any outstanding updates
	void IndexScan(ColumnScanState &state, Vector &result);

	//! Initialize an appending phase for this column
	void InitializeAppend(ColumnAppendState &state);
	//! Append a vector of type [type] to the end of the column
	void Append(ColumnAppendState &state, Vector &vector);

	//! Update the specified row identifiers
	void Update(Transaction &transaction, Vector &updates, row_t *ids);

	//! Fetch the vector from the column data that belongs to this specific row
	void Fetch(ColumnScanState &state, row_t row_id, Vector &result);
	//! Fetch a specific row id and append it to the vector
	void FetchRow(ColumnFetchState &state, Transaction &transaction, row_t row_id, Vector &result);

private:
	//! Append a transient segment
	void AppendTransientSegment(index_t start_row);
};

} // namespace duckdb
