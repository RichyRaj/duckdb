#include "duckdb/transaction/local_storage.hpp"
#include "duckdb/execution/index/art/art.hpp"
#include "duckdb/storage/table/append_state.hpp"
#include "duckdb/storage/write_ahead_log.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"

using namespace duckdb;
using namespace std;

LocalTableStorage::LocalTableStorage(DataTable &table) : max_row(0) {
	for (auto &index : table.indexes) {
		assert(index->type == IndexType::ART);
		auto &art = (ART &)*index;
		if (art.is_unique) {
			// unique index: create a local ART index that maintains the same unique constraint
			vector<unique_ptr<Expression>> unbound_expressions;
			for (auto &expr : art.unbound_expressions) {
				unbound_expressions.push_back(expr->Copy());
			}
			indexes.push_back(make_unique<ART>(table, art.column_ids, move(unbound_expressions), true));
		}
	}
}

LocalTableStorage::~LocalTableStorage() {
}

void LocalTableStorage::InitializeScan(LocalScanState &state) {
	state.storage = this;

	state.chunk_index = 0;
	state.max_index = collection.chunks.size() - 1;
	state.last_chunk_count = collection.chunks.back()->size();
}

void LocalTableStorage::Clear() {
	collection.chunks.clear();
	indexes.clear();
	deleted_entries.clear();
	state = nullptr;
}

void LocalStorage::InitializeScan(DataTable *table, LocalScanState &state) {
	auto entry = table_storage.find(table);
	if (entry == table_storage.end()) {
		// no local storage for table: set scan to nullptr
		state.storage = nullptr;
		return;
	}
	state.storage = entry->second.get();
	state.storage->InitializeScan(state);
}

void LocalStorage::Scan(LocalScanState &state, const vector<column_t> &column_ids, DataChunk &result) {
	if (!state.storage || state.chunk_index > state.max_index) {
		// nothing left to scan
		result.Reset();
		return;
	}
	auto &chunk = *state.storage->collection.chunks[state.chunk_index];
	index_t chunk_count = state.chunk_index == state.max_index ? state.last_chunk_count : chunk.size();
	index_t count = chunk_count;

	// first create a selection vector from the deleted entries (if any)
	sel_t *sel_vector = nullptr;
	auto entry = state.storage->deleted_entries.find(state.chunk_index);
	if (entry != state.storage->deleted_entries.end()) {
		// deleted entries! create a selection vector
		auto deleted = entry->second.get();
		sel_vector = state.sel_vector_data;
		index_t new_count = 0;
		for (index_t i = 0; i < count; i++) {
			if (!deleted[i]) {
				sel_vector[new_count++] = i;
			}
		}
		count = new_count;
	}

	// now scan the vectors of the chunk
	for (index_t i = 0; i < column_ids.size(); i++) {
		auto id = column_ids[i];
		if (id == COLUMN_IDENTIFIER_ROW_ID) {
			// row identifier: return MAX_ROW_ID plus the row offset in the chunk
			result.data[i].count = chunk_count;
			VectorOperations::GenerateSequence(result.data[i], MAX_ROW_ID + state.chunk_index * STANDARD_VECTOR_SIZE);
		} else {
			result.data[i].Reference(chunk.data[id]);
		}
		result.data[i].sel_vector = sel_vector;
		result.data[i].count = count;
	}
	result.sel_vector = sel_vector;
	state.chunk_index++;
}

void LocalStorage::Append(DataTable *table, DataChunk &chunk) {
	auto entry = table_storage.find(table);
	LocalTableStorage *storage;
	if (entry == table_storage.end()) {
		auto new_storage = make_unique<LocalTableStorage>(*table);
		storage = new_storage.get();
		table_storage.insert(make_pair(table, move(new_storage)));
	} else {
		storage = entry->second.get();
	}
	// append to unique indices (if any)
	if (storage->indexes.size() > 0) {
		index_t base_id = MAX_ROW_ID + storage->collection.count;

		// first generate the vector of row identifiers
		StaticVector<row_t> row_identifiers;
		row_identifiers.sel_vector = chunk.sel_vector;
		row_identifiers.count = chunk.size();
		VectorOperations::GenerateSequence(row_identifiers, base_id);

		// now append the entries to the indices
		for (auto &index : storage->indexes) {
			if (!index->Append(chunk, row_identifiers)) {
				throw ConstraintException("PRIMARY KEY or UNIQUE constraint violated: duplicated key");
			}
		}
	}

	//! Append to the chunk
	storage->collection.Append(chunk);
}

LocalTableStorage *LocalStorage::GetStorage(DataTable *table) {
	auto entry = table_storage.find(table);
	assert(entry != table_storage.end());
	return entry->second.get();
}

static index_t GetChunk(Vector &row_identifiers) {
	auto ids = (row_t *)row_identifiers.data;
	auto first_id = ids[0] - MAX_ROW_ID;

	index_t chunk_idx = first_id / STANDARD_VECTOR_SIZE;
	// verify that all row ids belong to the same chunk
#ifdef DEBUG
	VectorOperations::Exec(row_identifiers, [&](index_t i, index_t k) {
		index_t idx = (ids[i] - MAX_ROW_ID) / STANDARD_VECTOR_SIZE;
		assert(idx == chunk_idx);
	});
#endif
	return chunk_idx;
}

void LocalStorage::Delete(DataTable *table, Vector &row_identifiers) {
	auto storage = GetStorage(table);
	// figure out the chunk from which these row ids came
	index_t chunk_idx = GetChunk(row_identifiers);
	assert(chunk_idx < storage->collection.chunks.size());

	// get a pointer to the deleted entries for this chunk
	bool *deleted;
	auto entry = storage->deleted_entries.find(chunk_idx);
	if (entry == storage->deleted_entries.end()) {
		// nothing deleted yet, add the deleted entries
		auto del_entries = unique_ptr<bool[]>(new bool[STANDARD_VECTOR_SIZE]);
		memset(del_entries.get(), 0, sizeof(bool) * STANDARD_VECTOR_SIZE);
		deleted = del_entries.get();
		storage->deleted_entries.insert(make_pair(chunk_idx, move(del_entries)));
	} else {
		deleted = entry->second.get();
	}

	// now actually mark the entries as deleted in the deleted vector
	index_t base_index = MAX_ROW_ID + chunk_idx * STANDARD_VECTOR_SIZE;

	auto ids = (row_t *)row_identifiers.data;
	VectorOperations::Exec(row_identifiers, [&](index_t i, index_t k) {
		auto id = ids[i] - base_index;
		deleted[id] = true;
	});
}

template <class T>
static void update_data(Vector &data_vector, Vector &update_vector, Vector &row_identifiers, index_t base_index) {
	auto target = (T *)data_vector.data;
	auto updates = (T *)update_vector.data;
	auto ids = (row_t *)row_identifiers.data;
	VectorOperations::Exec(row_identifiers, [&](index_t i, index_t k) {
		auto id = ids[i] - base_index;
		target[id] = updates[i];
	});
}

static void update_chunk(Vector &data, Vector &updates, Vector &row_identifiers, index_t base_index) {
	assert(data.type == updates.type);
	assert(row_identifiers.type == ROW_TYPE);
	assert(updates.sel_vector == row_identifiers.sel_vector);

	switch (data.type) {
	case TypeId::TINYINT:
		update_data<int8_t>(data, updates, row_identifiers, base_index);
		break;
	case TypeId::SMALLINT:
		update_data<int16_t>(data, updates, row_identifiers, base_index);
		break;
	case TypeId::INTEGER:
		update_data<int32_t>(data, updates, row_identifiers, base_index);
		break;
	case TypeId::BIGINT:
		update_data<int64_t>(data, updates, row_identifiers, base_index);
		break;
	case TypeId::FLOAT:
		update_data<float>(data, updates, row_identifiers, base_index);
		break;
	case TypeId::DOUBLE:
		update_data<double>(data, updates, row_identifiers, base_index);
		break;
	default:
		throw Exception("Unsupported type for in-place update");
	}
}

void LocalStorage::Update(DataTable *table, Vector &row_identifiers, vector<column_t> &column_ids, DataChunk &data) {
	auto storage = GetStorage(table);
	// figure out the chunk from which these row ids came
	index_t chunk_idx = GetChunk(row_identifiers);
	assert(chunk_idx < storage->collection.chunks.size());

	index_t base_index = MAX_ROW_ID + chunk_idx * STANDARD_VECTOR_SIZE;

	// now perform the actual update
	auto &chunk = *storage->collection.chunks[chunk_idx];
	for (index_t i = 0; i < column_ids.size(); i++) {
		auto col_idx = column_ids[i];
		update_chunk(chunk.data[col_idx], data.data[i], row_identifiers, base_index);
	}
}

template <class T> bool LocalStorage::ScanTableStorage(DataTable *table, LocalTableStorage *storage, T &&fun) {
	vector<column_t> column_ids;
	for (index_t i = 0; i < table->types.size(); i++) {
		column_ids.push_back(i);
	}

	DataChunk chunk;
	chunk.Initialize(table->types);

	// initialize the scan
	LocalScanState state;
	storage->InitializeScan(state);

	while (true) {
		Scan(state, column_ids, chunk);
		if (chunk.size() == 0) {
			return true;
		}
		if (!fun(chunk)) {
			return false;
		}
	}
}

void LocalStorage::CheckCommit() {
	// check whether a commit can be made, we do this check by appending to all indices
	bool success = true;
	for (auto &entry : table_storage) {
		auto table = entry.first;
		auto storage = entry.second.get();

		storage->state = make_unique<TableAppendState>();
		table->InitializeAppend(*storage->state);

		if (table->indexes.size() == 0) {
			continue;
		}

		row_t current_row = storage->state->row_start;
		ScanTableStorage(table, storage, [&](DataChunk &chunk) -> bool {
			// append this chunk to the indexes of the table
			if (!table->AppendToIndexes(*storage->state, chunk, current_row)) {
				success = false;
				return false;
			}
			current_row += chunk.size();
			return true;
		});

		storage->max_row = current_row;
		if (!success) {
			break;
		}
	}
	if (!success) {
		// failed to insert in one of the tables: delete already inserted entries
		for (auto &entry : table_storage) {
			auto table = entry.first;
			auto storage = entry.second.get();

			if (table->indexes.size() == 0 || storage->max_row == 0) {
				continue;
			}

			row_t current_row = storage->state->row_start;
			ScanTableStorage(table, storage, [&](DataChunk &chunk) -> bool {
				if (current_row >= storage->max_row) {
					// done
					return false;
				}
				table->RemoveFromIndexes(chunk, current_row);
				current_row += chunk.size();
				return true;
			});
		}
		// reset the storage state for each of the entries
		for (auto &entry : table_storage) {
			auto storage = entry.second.get();
			storage->state = nullptr;
		}
		// throw a constraint violation
		throw ConstraintException("PRIMARY KEY or UNIQUE constraint violated: duplicated key");
	}
}

void LocalStorage::Commit(Transaction &transaction, WriteAheadLog *log, transaction_t commit_id) noexcept {
	// commit local storage, iterate over all entries in the table storage map
	for (auto &entry : table_storage) {
		auto table = entry.first;
		auto storage = entry.second.get();

		if (log && !table->IsTemporary()) {
			log->WriteSetTable(table->schema, table->table);
		}

		// scan all chunks in this storage
		ScanTableStorage(table, storage, [&](DataChunk &chunk) -> bool {
			// append to base table
			table->Append(transaction, commit_id, chunk, *storage->state);
			// if there is a WAL, write the chunk to there as well
			if (log && !table->IsTemporary()) {
				log->WriteInsert(chunk);
			}
			return true;
		});
		storage->Clear();
	}
	// finished commit: clear local storage
	table_storage.clear();
}
