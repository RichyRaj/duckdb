#include "catch.hpp"
#include "duckdb/common/file_system.hpp"
#include "dbgen.hpp"
#include "test_helpers.hpp"
#include <cfloat>

using namespace duckdb;
using namespace std;

TEST_CASE("Test index creation statements with multiple connections", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db), con2(db);

	// create a table
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER, j INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));
	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (1, 3)"));
	for (int i = 0; i < 3000; i++) {
		int key = i + 10;
		REQUIRE_NO_FAIL(
		    con.Query("INSERT INTO integers VALUES (" + to_string(i + 10) + ", " + to_string(i + 12) + ")"));
		result = con.Query("SELECT i FROM integers WHERE i=" + to_string(i + 10));
		REQUIRE(CHECK_COLUMN(result, 0, {Value(key)}));
	}

	// both con and con2 start a transaction
	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
	REQUIRE_NO_FAIL(con2.Query("BEGIN TRANSACTION"));

	// con2 updates the integers array before index creation
	REQUIRE_NO_FAIL(con2.Query("UPDATE integers SET i=4 WHERE i=1"));

	// con should see the old state
	result = con.Query("SELECT j FROM integers WHERE i=1");
	REQUIRE(CHECK_COLUMN(result, 0, {3}));

	// con2 should see the updated state
	result = con2.Query("SELECT j FROM integers WHERE i=4");
	REQUIRE(CHECK_COLUMN(result, 0, {3}));

	// now we commit con
	REQUIRE_NO_FAIL(con.Query("COMMIT"));

	// con should still see the old state
	result = con.Query("SELECT j FROM integers WHERE i=1");
	REQUIRE(CHECK_COLUMN(result, 0, {3}));

	REQUIRE_NO_FAIL(con2.Query("COMMIT"));

	// after commit of con2 - con should see the old state
	result = con.Query("SELECT j FROM integers WHERE i=4");
	REQUIRE(CHECK_COLUMN(result, 0, {3}));

	// now we update the index again, this time after index creation
	REQUIRE_NO_FAIL(con2.Query("UPDATE integers SET i=7 WHERE i=4"));
	// the new state should be visible
	result = con.Query("SELECT j FROM integers WHERE i=7");
	REQUIRE(CHECK_COLUMN(result, 0, {3}));
}

TEST_CASE("Test ART index on table with multiple columns", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i BIGINT, j INTEGER, k VARCHAR)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(j)"));

	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (10, 1, 'hello'), (11, 2, 'world')"));

	// condition on "i"
	result = con.Query("SELECT i FROM integers WHERE i=10");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	result = con.Query("SELECT * FROM integers WHERE i=10");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"hello"}));

	// condition on "j"
	result = con.Query("SELECT j FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	result = con.Query("SELECT * FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"hello"}));

	// condition on "k"
	result = con.Query("SELECT k FROM integers WHERE k='hello'");
	REQUIRE(CHECK_COLUMN(result, 0, {"hello"}));
	result = con.Query("SELECT i, k FROM integers WHERE k='hello'");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	REQUIRE(CHECK_COLUMN(result, 1, {"hello"}));
	result = con.Query("SELECT * FROM integers WHERE k='hello'");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"hello"}));
}

TEST_CASE("Test ART index on table with updates to other columns", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db), con2(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i BIGINT, j INTEGER, k VARCHAR)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(j)"));

	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (10, 1, 'hello'), (11, 2, 'world')"));

	// condition on "j"
	result = con.Query("SELECT * FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"hello"}));

	// now update the column
	REQUIRE_NO_FAIL(con2.Query("BEGIN TRANSACTION"));
	REQUIRE_NO_FAIL(con2.Query("UPDATE integers SET i=100, k='update' WHERE j=1"));

	// con sees the old state, con2 sees the new state
	result = con.Query("SELECT * FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"hello"}));
	result = con2.Query("SELECT * FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {100}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"update"}));

	// after a commit, both see the new state
	REQUIRE_NO_FAIL(con2.Query("COMMIT"));

	result = con.Query("SELECT * FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {100}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"update"}));
	result = con2.Query("SELECT * FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {100}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"update"}));

	// now do the same but with two outstanding updates
	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
	REQUIRE_NO_FAIL(con2.Query("BEGIN TRANSACTION"));
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=20, k='t1' WHERE j=1"));
	REQUIRE_NO_FAIL(con2.Query("UPDATE integers SET i=21, k='t2' WHERE j=2"));

	// con1 sees the updated state for the first tuple, but the old state for the new tuple
	result = con.Query("SELECT * FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {20}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"t1"}));
	result = con.Query("SELECT * FROM integers WHERE j=2");
	REQUIRE(CHECK_COLUMN(result, 0, {11}));
	REQUIRE(CHECK_COLUMN(result, 1, {2}));
	REQUIRE(CHECK_COLUMN(result, 2, {"world"}));
	result = con.Query("SELECT * FROM integers ORDER BY j");
	REQUIRE(CHECK_COLUMN(result, 0, {20, 11}));
	REQUIRE(CHECK_COLUMN(result, 1, {1, 2}));
	REQUIRE(CHECK_COLUMN(result, 2, {"t1", "world"}));
	// con2 sees the updated state for the second tuple, but the old state for the new tuple
	result = con2.Query("SELECT * FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {100}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"update"}));
	result = con2.Query("SELECT * FROM integers WHERE j=2");
	REQUIRE(CHECK_COLUMN(result, 0, {21}));
	REQUIRE(CHECK_COLUMN(result, 1, {2}));
	REQUIRE(CHECK_COLUMN(result, 2, {"t2"}));
	result = con2.Query("SELECT * FROM integers ORDER BY j");
	REQUIRE(CHECK_COLUMN(result, 0, {100, 21}));
	REQUIRE(CHECK_COLUMN(result, 1, {1, 2}));
	REQUIRE(CHECK_COLUMN(result, 2, {"update", "t2"}));

	// after commit, both see the updated state
	REQUIRE_NO_FAIL(con.Query("COMMIT"));
	REQUIRE_NO_FAIL(con2.Query("COMMIT"));

	result = con.Query("SELECT * FROM integers WHERE j=1");
	REQUIRE(CHECK_COLUMN(result, 0, {20}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"t1"}));
	result = con.Query("SELECT * FROM integers WHERE j=2");
	REQUIRE(CHECK_COLUMN(result, 0, {21}));
	REQUIRE(CHECK_COLUMN(result, 1, {2}));
	REQUIRE(CHECK_COLUMN(result, 2, {"t2"}));
	result = con.Query("SELECT * FROM integers ORDER BY j");
	REQUIRE(CHECK_COLUMN(result, 0, {20, 21}));
	REQUIRE(CHECK_COLUMN(result, 1, {1, 2}));
	REQUIRE(CHECK_COLUMN(result, 2, {"t1", "t2"}));
	result = con2.Query("SELECT * FROM integers ORDER BY j");
	REQUIRE(CHECK_COLUMN(result, 0, {20, 21}));
	REQUIRE(CHECK_COLUMN(result, 1, {1, 2}));
	REQUIRE(CHECK_COLUMN(result, 2, {"t1", "t2"}));
}

TEST_CASE("Test ART index that requires multiple columns for expression", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	// FIXME: this should work, not a multidimensional index
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i BIGINT, j INTEGER, k VARCHAR, l BIGINT)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art((j+l))"));

	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (10, 1, 'hello', 4), (11, 2, 'world', 6)"));
	result = con.Query("SELECT * FROM integers WHERE j+l=5");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"hello"}));
	REQUIRE(CHECK_COLUMN(result, 3, {4}));

	result = con.Query("SELECT * FROM integers WHERE k='hello'");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	REQUIRE(CHECK_COLUMN(result, 1, {1}));
	REQUIRE(CHECK_COLUMN(result, 2, {"hello"}));
	REQUIRE(CHECK_COLUMN(result, 3, {4}));

	// update that uses both columns in the index
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET j=5, l=l WHERE j=1"));
	// update that only uses one of the columns
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET j=5 WHERE j=5"));

	result = con.Query("SELECT * FROM integers WHERE j+l=9");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	REQUIRE(CHECK_COLUMN(result, 1, {5}));
	REQUIRE(CHECK_COLUMN(result, 2, {"hello"}));
	REQUIRE(CHECK_COLUMN(result, 3, {4}));

	REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE j+l=8"));
	REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE j+l=9"));

	result = con.Query("SELECT COUNT(*) FROM integers");
	REQUIRE(CHECK_COLUMN(result, 0, {0}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE j+l>0");
	REQUIRE(CHECK_COLUMN(result, 0, {0}));
}

TEST_CASE("Test updates on ART index", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER, j INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(j)"));

	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (1, 2), (2, 2)"));
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET j=10 WHERE i=1"));
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET j=10 WHERE rowid=0"));
	REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE rowid=1"));

	result = con.Query("SELECT * FROM integers WHERE j>5");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	REQUIRE(CHECK_COLUMN(result, 1, {10}));
}

TEST_CASE("Test ART index with single value", "[art-s]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));

	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (1)"));

	result = con.Query("SELECT * FROM integers WHERE i < 3");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	result = con.Query("SELECT * FROM integers WHERE i <= 1");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	result = con.Query("SELECT * FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	result = con.Query("SELECT * FROM integers WHERE i >= 1");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	result = con.Query("SELECT * FROM integers WHERE i = 1");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));

	result = con.Query("SELECT * FROM integers WHERE i < 1");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT * FROM integers WHERE i <= 0");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT * FROM integers WHERE i > 1");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT * FROM integers WHERE i >= 2");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT * FROM integers WHERE i = 2");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
}

TEST_CASE("Test ART index with selection vector", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE source(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("INSERT INTO source VALUES (1), (2), (3), (4), (5), (6)"));

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));

	// insert with selection vector
	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers SELECT * FROM source WHERE i % 2 = 0"));

	result = con.Query("SELECT * FROM integers WHERE i<3 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {2}));
	result = con.Query("SELECT * FROM integers ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {2, 4, 6}));
	result = con.Query("SELECT * FROM integers WHERE i>3 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {4, 6}));
	result = con.Query("SELECT * FROM integers WHERE i<=3 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {2}));
	result = con.Query("SELECT * FROM integers WHERE i>=3 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {4, 6}));

	// update with selection vector
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=3 WHERE i=4"));

	result = con.Query("SELECT * FROM integers WHERE i<3 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {2}));
	result = con.Query("SELECT * FROM integers WHERE i<=3 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {2, 3}));
	result = con.Query("SELECT * FROM integers WHERE i>3 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {6}));
	result = con.Query("SELECT * FROM integers WHERE i>=3 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {3, 6}));

	// delete with selection vector
	REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i>3"));

	result = con.Query("SELECT * FROM integers WHERE i > 0 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {2, 3}));
	result = con.Query("SELECT * FROM integers WHERE i < 3 ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {2}));
}

TEST_CASE("Test ART index with multiple predicates", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER, j INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));

	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (1, 2), (1, 3)"));

	result = con.Query("SELECT * FROM integers WHERE i = 1 AND j = 2");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	REQUIRE(CHECK_COLUMN(result, 1, {2}));
}

TEST_CASE("Test ART index with simple updates", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db), con2(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));

	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (1)"));

	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=10 WHERE i=1"));
	// con sees the new state
	result = con.Query("SELECT * FROM integers WHERE i < 5");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT * FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {10}));
	// con2 sees the old state
	result = con2.Query("SELECT * FROM integers WHERE i < 5");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	result = con2.Query("SELECT * FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	REQUIRE_NO_FAIL(con.Query("ROLLBACK"));
}

TEST_CASE("Test ART index with multiple updates on the same value", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));

	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (1)"));

	result = con.Query("SELECT * FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));

	// update the same tuple a bunch of times in the same transaction and then rollback
	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
	for (int32_t i = 0; i < 10; i++) {
		REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=" + to_string(i + 2) + " WHERE i=" + to_string(i + 1)));

		result = con.Query("SELECT * FROM integers WHERE i > 0");
		REQUIRE(CHECK_COLUMN(result, 0, {Value::INTEGER(i + 2)}));
	}
	REQUIRE_NO_FAIL(con.Query("ROLLBACK"));

	result = con.Query("SELECT * FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));

	// now update the same tuple a bunch of times in the same transaction and then commit
	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
	for (int32_t i = 0; i < 10; i++) {
		REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=$1 WHERE i=$2", i + 2, i + 1));

		result = con.Query("SELECT * FROM integers WHERE i > 0");
		REQUIRE(CHECK_COLUMN(result, 0, {Value::INTEGER(i + 2)}));
	}
	REQUIRE_NO_FAIL(con.Query("COMMIT"));

	result = con.Query("SELECT * FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {11}));
}

TEST_CASE("Test ART index with prefixes", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i BIGINT)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));
	// insert a bunch of values with different prefixes
	vector<int64_t> values = {9312908412824241,
	                          -2092042498432234,
	                          1,
	                          -100,
	                          0,
	                          -598538523852390852,
	                          4298421,
	                          -498249,
	                          9312908412824240,
	                          -2092042498432235,
	                          2,
	                          -101,
	                          -598538523852390853,
	                          4298422,
	                          -498261};
	index_t gt_count = 0, lt_count = 0;
	index_t count = 0;
	for (index_t val_index = 0; val_index < values.size(); val_index++) {
		auto &value = values[val_index];
		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", value));
		if (value >= 0) {
			gt_count++;
		} else {
			lt_count++;
		}
		count++;
		for (index_t i = 0; i <= val_index; i++) {
			result = con.Query("SELECT COUNT(*) FROM integers WHERE i = " + to_string(values[i]));
			REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(1)}));
		}
		result = con.Query("SELECT COUNT(*) FROM integers");
		REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(count)}));
		result = con.Query("SELECT COUNT(*) FROM integers WHERE i < 9223372036854775808");
		REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(count)}));
		result = con.Query("SELECT COUNT(*) FROM integers WHERE i >= 0");
		REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(gt_count)}));
		result = con.Query("SELECT COUNT(*) FROM integers WHERE i < 0");
		REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(lt_count)}));
	}
}

TEST_CASE("Test ART index with linear insertions and deletes", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	vector<index_t> insertion_count = {4, 16, 48, 256, 1024};
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));
	for (auto &insert_count : insertion_count) {
		// insert the data
		vector<index_t> elements;
		index_t table_count = 0;
		for (index_t i = 0; i < insert_count; i++) {
			index_t element = i;
			REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", (int32_t)element));
			elements.push_back(element);
			table_count++;
			// test that the insert worked
			result = con.Query("SELECT COUNT(*) FROM integers WHERE i < 100000000");
			bool checked = CHECK_COLUMN(result, 0, {Value::BIGINT(table_count)});
			REQUIRE(checked);
			result = con.Query("SELECT COUNT(*) FROM integers WHERE i >= 0");
			REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(table_count)}));
		}
		// test that it worked
		result = con.Query("SELECT COUNT(*) FROM integers WHERE i < 100000000");
		REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(table_count)}));
		result = con.Query("SELECT COUNT(*) FROM integers WHERE i >= 0");
		REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(table_count)}));

		// delete random data
		for (auto &element : elements) {
			// delete the element
			REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i=$1", (int32_t)element));
			table_count--;
			// verify that the deletion worked
			result = con.Query("SELECT COUNT(*) FROM integers WHERE i >= 0");
			bool check = CHECK_COLUMN(result, 0, {Value::BIGINT(table_count)});
			REQUIRE(check);
		}
	}
}

TEST_CASE("Test ART index with random insertions and deletes", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	vector<index_t> insertion_count = {1024, 2048};
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));
	for (auto &insert_count : insertion_count) {
		// insert the data
		vector<index_t> elements;
		index_t table_count = 0;
		for (index_t i = 0; i < insert_count; i++) {
			index_t element = i * i;
			REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", (int32_t)element));
			elements.push_back(element);
			table_count++;
		}
		// test that it worked
		result = con.Query("SELECT COUNT(*) FROM integers WHERE i >= 0");
		REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(table_count)}));

		// delete random data
		std::random_shuffle(elements.begin(), elements.end());
		for (auto &element : elements) {
			// delete the element
			REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i=$1", (int32_t)element));
			table_count--;
			// verify that the deletion worked
			result = con.Query("SELECT COUNT(*) FROM integers");
			REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(table_count)}));
			result = con.Query("SELECT COUNT(*) FROM integers WHERE i >= 0");
			REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(table_count)}));
		}
	}
}

TEST_CASE("Test ART index creation with many versions", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);
	Connection r1(db), r2(db), r3(db);
	int64_t expected_sum_r1 = 0, expected_sum_r2 = 0, expected_sum_r3 = 0, total_sum = 0;

	// insert the values [0...20000]
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	for (index_t i = 0; i < 20000; i++) {
		int32_t val = i + 1;
		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", val));
		expected_sum_r1 += val;
		expected_sum_r2 += val + 1;
		expected_sum_r3 += val + 2;
		total_sum += val + 3;
	}
	// now start a transaction in r1
	REQUIRE_NO_FAIL(r1.Query("BEGIN TRANSACTION"));
	// increment values by 1
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=i+1"));
	// now start a transaction in r2
	REQUIRE_NO_FAIL(r2.Query("BEGIN TRANSACTION"));
	// increment values by 1 again
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=i+1"));
	// now start a transaction in r3
	REQUIRE_NO_FAIL(r3.Query("BEGIN TRANSACTION"));
	// increment values by 1 again
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=i+1"));
	// create an index, this fails because we have outstanding updates
	REQUIRE_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));

	// r1
	result = r1.Query("SELECT SUM(i) FROM integers");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(expected_sum_r1)}));
	result = r1.Query("SELECT SUM(i) FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(expected_sum_r1)}));
	// r2
	result = r2.Query("SELECT SUM(i) FROM integers");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(expected_sum_r2)}));
	result = r2.Query("SELECT SUM(i) FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(expected_sum_r2)}));
	// r3
	result = r3.Query("SELECT SUM(i) FROM integers");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(expected_sum_r3)}));
	result = r3.Query("SELECT SUM(i) FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(expected_sum_r3)}));
	// total sum
	result = con.Query("SELECT SUM(i) FROM integers");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(total_sum)}));
	result = con.Query("SELECT SUM(i) FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(total_sum)}));
}

TEST_CASE("Test ART index with many matches", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	for (index_t i = 0; i < 1024; i++) {
		for (index_t val = 0; val < 2; val++) {
			REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", (int32_t)val));
		}
	}
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));

	result = con.Query("SELECT COUNT(*) FROM integers WHERE i<1");
	REQUIRE(CHECK_COLUMN(result, 0, {1024}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i<=1");
	REQUIRE(CHECK_COLUMN(result, 0, {2048}));

	result = con.Query("SELECT COUNT(*) FROM integers WHERE i=0");
	REQUIRE(CHECK_COLUMN(result, 0, {1024}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i=1");
	REQUIRE(CHECK_COLUMN(result, 0, {1024}));

	result = con.Query("SELECT COUNT(*) FROM integers WHERE i>0");
	REQUIRE(CHECK_COLUMN(result, 0, {1024}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i>=0");
	REQUIRE(CHECK_COLUMN(result, 0, {2048}));

	REQUIRE_NO_FAIL(con.Query("ROLLBACK"));

	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	for (index_t i = 0; i < 2048; i++) {
		for (index_t val = 0; val < 2; val++) {
			REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", (int32_t)val));
		}
	}

	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));

	result = con.Query("SELECT COUNT(*) FROM integers WHERE i<1");
	REQUIRE(CHECK_COLUMN(result, 0, {2048}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i<=1");
	REQUIRE(CHECK_COLUMN(result, 0, {4096}));

	result = con.Query("SELECT COUNT(*) FROM integers WHERE i=0");
	REQUIRE(CHECK_COLUMN(result, 0, {2048}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i=1");
	REQUIRE(CHECK_COLUMN(result, 0, {2048}));

	result = con.Query("SELECT COUNT(*) FROM integers WHERE i>0");
	REQUIRE(CHECK_COLUMN(result, 0, {2048}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i>=0");
	REQUIRE(CHECK_COLUMN(result, 0, {4096}));

	REQUIRE_NO_FAIL(con.Query("ROLLBACK"));
}

TEST_CASE("Test ART index with non-linear insertion", "[art][.]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));
	index_t count = 0;
	for (int32_t it = 0; it < 10; it++) {
		for (int32_t val = 0; val < 1000; val++) {
			if (it + val % 2) {
				count++;
				REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", val));
			}
		}
	}
	result = con.Query("SELECT COUNT(*) FROM integers");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(count)}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i < 1000000");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(count)}));
}

TEST_CASE("Test ART index with rollbacks", "[art][.]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));
	index_t count = 0;
	for (int32_t it = 0; it < 10; it++) {
		for (int32_t val = 0; val < 1000; val++) {
			REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
			REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", val));
			if (it + val % 2) {
				count++;
				REQUIRE_NO_FAIL(con.Query("COMMIT"));
			} else {
				REQUIRE_NO_FAIL(con.Query("ROLLBACK"));
			}
		}
	}
	result = con.Query("SELECT COUNT(*) FROM integers");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(count)}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i < 1000000");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(count)}));
}

TEST_CASE("Test ART index with the same value multiple times", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));
	for (int32_t val = 0; val < 100; val++) {
		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", val));
	}
	for (int32_t val = 0; val < 100; val++) {
		result = con.Query("SELECT COUNT(*) FROM integers WHERE i = " + to_string(val));
		REQUIRE(CHECK_COLUMN(result, 0, {1}));
	}
	for (int32_t it = 0; it < 10; it++) {
		for (int32_t val = 0; val < 100; val++) {
			result = con.Query("SELECT COUNT(*) FROM integers WHERE i = " + to_string(val));
			REQUIRE(CHECK_COLUMN(result, 0, {it + 1}));
		}
		for (int32_t val = 0; val < 100; val++) {
			REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", val));
			result = con.Query("SELECT COUNT(*) FROM integers WHERE i = " + to_string(val));
			REQUIRE(CHECK_COLUMN(result, 0, {it + 2}));
		}
		for (int32_t val = 0; val < 100; val++) {
			result = con.Query("SELECT COUNT(*) FROM integers WHERE i = " + to_string(val));
			REQUIRE(CHECK_COLUMN(result, 0, {it + 2}));
		}
	}
}

TEST_CASE("Test ART index with negative values and big values", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i BIGINT)"));
	vector<int64_t> values = {-4611686018427387906, -4611686018427387904, -2305843009213693952, 0,
	                          2305843009213693952,  4611686018427387904,  4611686018427387906};
	for (auto val : values) {
		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", val));
	}

	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers using art(i)"));

	result = con.Query("SELECT COUNT(*) FROM integers WHERE i > $1", 0);
	REQUIRE(CHECK_COLUMN(result, 0, {3}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i < $1", 0);
	REQUIRE(CHECK_COLUMN(result, 0, {3}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i < $1", (int64_t)4611686018427387906);
	REQUIRE(CHECK_COLUMN(result, 0, {6}));
	result = con.Query("SELECT COUNT(*) FROM integers WHERE i <= $1", (int64_t)4611686018427387906);
	REQUIRE(CHECK_COLUMN(result, 0, {7}));
}

TEST_CASE("Test ART with different Integer Types", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);

	Connection con(db);
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i TINYINT, j SMALLINT, k INTEGER, l BIGINT)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index1 ON integers(i)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index2 ON integers(j)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index3 ON integers(k)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index4 ON integers(l)"));

	// query the empty indices first
	result = con.Query("SELECT i FROM integers WHERE i > 0");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT j FROM integers WHERE j < 0");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT k FROM integers WHERE k >= 0");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT l FROM integers WHERE l <= 0");
	REQUIRE(CHECK_COLUMN(result, 0, {}));

	// now insert the values [1..5] in all columns
	auto prepare = con.Prepare("INSERT INTO integers VALUES ($1, $2, $3, $4)");
	for (int32_t i = 1; i <= 5; i++) {
		REQUIRE_NO_FAIL(prepare->Execute(i, i, i, i));
	}
	prepare.reset();

	result = con.Query("SELECT * FROM integers ORDER BY i");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 3, 4, 5}));
	REQUIRE(CHECK_COLUMN(result, 1, {1, 2, 3, 4, 5}));
	REQUIRE(CHECK_COLUMN(result, 2, {1, 2, 3, 4, 5}));
	REQUIRE(CHECK_COLUMN(result, 3, {1, 2, 3, 4, 5}));

	result = con.Query("SELECT i FROM integers WHERE i > 0::TINYINT ORDER BY i");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 3, 4, 5}));
	result = con.Query("SELECT j FROM integers WHERE j <= 2::SMALLINT ORDER BY j");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2}));
	result = con.Query("SELECT k FROM integers WHERE k >= -100000::INTEGER ORDER BY k");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 3, 4, 5}));
	result = con.Query("SELECT k FROM integers WHERE k >= 100000::INTEGER ORDER BY k");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT k FROM integers WHERE k >= 100000::INTEGER AND k <= 100001::INTEGER ORDER BY k");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT l FROM integers WHERE l <= 1000000000::BIGINT ORDER BY i");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 3, 4, 5}));
	result = con.Query("SELECT l FROM integers WHERE l <= -1000000000::BIGINT ORDER BY i");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
}

TEST_CASE("ART Integer Types", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);

	Connection con(db);

	string int_types[4] = {"tinyint", "smallint", "integer", "bigint"};
	index_t n_sizes[4] = {100, 1000, 1000, 1000};
	for (index_t idx = 0; idx < 4; idx++) {
		REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i " + int_types[idx] + ")"));
		REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers(i)"));

		index_t n = n_sizes[idx];
		auto keys = unique_ptr<int32_t[]>(new int32_t[n]);
		auto key_pointer = keys.get();
		for (index_t i = 0; i < n; i++) {
			keys[i] = i + 1;
		}
		std::random_shuffle(key_pointer, key_pointer + n);

		for (index_t i = 0; i < n; i++) {
			REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", keys[i]));
			result =
			    con.Query("SELECT i FROM integers WHERE i=CAST(" + to_string(keys[i]) + " AS " + int_types[idx] + ")");
			REQUIRE(CHECK_COLUMN(result, 0, {Value(keys[i])}));
		}
		//! Checking non-existing values
		result = con.Query("SELECT i FROM integers WHERE i=CAST(" + to_string(-1) + " AS " + int_types[idx] + ")");
		REQUIRE(CHECK_COLUMN(result, 0, {}));
		result = con.Query("SELECT i FROM integers WHERE i=CAST(" + to_string(n_sizes[idx] + 1) + " AS " +
		                   int_types[idx] + ")");
		REQUIRE(CHECK_COLUMN(result, 0, {}));

		//! Checking if all elements are still there
		for (index_t i = 0; i < n; i++) {
			result =
			    con.Query("SELECT i FROM integers WHERE i=CAST(" + to_string(keys[i]) + " AS " + int_types[idx] + ")");
			REQUIRE(CHECK_COLUMN(result, 0, {Value(keys[i])}));
		}

		//! Checking Multiple Range Queries
		int32_t up_range_result = n_sizes[idx] * 2 - 1;
		result = con.Query("SELECT sum(i) FROM integers WHERE i >= " + to_string(n_sizes[idx] - 1));
		REQUIRE(CHECK_COLUMN(result, 0, {Value(up_range_result)}));

		result = con.Query("SELECT sum(i) FROM integers WHERE i > " + to_string(n_sizes[idx] - 2));
		REQUIRE(CHECK_COLUMN(result, 0, {Value(up_range_result)}));

		result = con.Query("SELECT sum(i) FROM integers WHERE i > 2 AND i < 5");
		REQUIRE(CHECK_COLUMN(result, 0, {Value(7)}));

		result = con.Query("SELECT sum(i) FROM integers WHERE i >=2 AND i <5");
		REQUIRE(CHECK_COLUMN(result, 0, {Value(9)}));

		result = con.Query("SELECT sum(i) FROM integers WHERE i >2 AND i <=5");
		REQUIRE(CHECK_COLUMN(result, 0, {Value(12)}));

		result = con.Query("SELECT sum(i) FROM integers WHERE i >=2 AND i <=5");
		REQUIRE(CHECK_COLUMN(result, 0, {Value(14)}));
		result = con.Query("SELECT sum(i) FROM integers WHERE i <=2");
		REQUIRE(CHECK_COLUMN(result, 0, {Value(3)}));

		result = con.Query("SELECT sum(i) FROM integers WHERE i <0");
		REQUIRE(CHECK_COLUMN(result, 0, {Value()}));

		result = con.Query("SELECT sum(i) FROM integers WHERE i >10000000");
		REQUIRE(CHECK_COLUMN(result, 0, {Value()}));

		//! Checking Duplicates
		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (1)"));
		result = con.Query("SELECT SUM(i) FROM integers WHERE i=1");
		REQUIRE(CHECK_COLUMN(result, 0, {Value(2)}));

		//! Successful update
		REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=14 WHERE i=13"));
		result = con.Query("SELECT * FROM integers WHERE i=14");
		REQUIRE(CHECK_COLUMN(result, 0, {14, 14}));

		// Testing rollbacks and commits
		// rolled back update
		REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
		// update the value
		REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=14 WHERE i=12"));
		// now there are three values with 14
		result = con.Query("SELECT * FROM integers WHERE i=14");
		REQUIRE(CHECK_COLUMN(result, 0, {14, 14, 14}));
		// rollback the value
		REQUIRE_NO_FAIL(con.Query("ROLLBACK"));
		// after the rollback
		result = con.Query("SELECT * FROM integers WHERE i=14");
		REQUIRE(CHECK_COLUMN(result, 0, {14, 14}));
		// roll back insert
		REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
		// update the value
		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (14)"));
		// now there are three values with 14
		result = con.Query("SELECT * FROM integers WHERE i=14");
		REQUIRE(CHECK_COLUMN(result, 0, {14, 14, 14}));
		// rollback the value
		REQUIRE_NO_FAIL(con.Query("ROLLBACK"));
		// after the rol
		result = con.Query("SELECT * FROM integers WHERE i=14");
		REQUIRE(CHECK_COLUMN(result, 0, {14, 14}));

		//! Testing deletes
		// Delete non-existing element
		REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i=0"));
		// Now Deleting all elements
		for (index_t i = 0; i < n; i++) {
			REQUIRE_NO_FAIL(
			    con.Query("DELETE FROM integers WHERE i=CAST(" + to_string(keys[i]) + " AS " + int_types[idx] + ")"));
			// check the value does not exist
			result =
			    con.Query("SELECT * FROM integers WHERE i=CAST(" + to_string(keys[i]) + " AS " + int_types[idx] + ")");
			REQUIRE(CHECK_COLUMN(result, 0, {}));
		}
		// Delete from empty tree
		REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i=0"));

		REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
		REQUIRE_NO_FAIL(con.Query("DROP TABLE integers"));
	}
}

TEST_CASE("ART Simple Big Range", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i integer)"));
	index_t n = 4;
	auto keys = unique_ptr<int32_t[]>(new int32_t[n + 1]);
	for (index_t i = 0; i < n + 1; i++) {
		keys[i] = i + 1;
	}

	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
	for (index_t i = 0; i < n; i++) {
		for (index_t j = 0; j < 1500; j++) {
			REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", keys[i]));
		}
	}
	REQUIRE_NO_FAIL(con.Query("COMMIT"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers(i)"));

	result = con.Query("SELECT count(i) FROM integers WHERE i > 1 AND i < 3");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(1500)}));
	result = con.Query("SELECT count(i) FROM integers WHERE i >= 1 AND i < 3");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(3000)}));
	result = con.Query("SELECT count(i) FROM integers WHERE i > 1");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(4500)}));
	result = con.Query("SELECT count(i) FROM integers WHERE i < 4");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(4500)}));
	result = con.Query("SELECT count(i) FROM integers WHERE i < 5");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(6000)}));
	REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
	REQUIRE_NO_FAIL(con.Query("DROP TABLE integers"));
}

TEST_CASE("ART Big Range with deletions", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	index_t n = 4;
	auto keys = unique_ptr<int32_t[]>(new int32_t[n + 1]);
	for (index_t i = 0; i < n + 1; i++) {
		keys[i] = i + 1;
	}

	// now perform a an index creation and scan with deletions with a second transaction
	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i integer)"));
	for (index_t j = 0; j < 1500; j++) {
		for (index_t i = 0; i < n + 1; i++) {
			REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", keys[i]));
		}
	}
	REQUIRE_NO_FAIL(con.Query("COMMIT"));

	// second transaction: begin and verify counts
	Connection con2(db);
	REQUIRE_NO_FAIL(con2.Query("BEGIN TRANSACTION"));
	for (index_t i = 0; i < n + 1; i++) {
		result = con2.Query("SELECT FIRST(i), COUNT(i) FROM integers WHERE i=" + to_string(keys[i]));
		REQUIRE(CHECK_COLUMN(result, 0, {Value(keys[i])}));
		REQUIRE(CHECK_COLUMN(result, 1, {Value(1500)}));
	}
	result = con2.Query("SELECT COUNT(i) FROM integers WHERE i < 10");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(7500)}));

	// now delete entries in the first transaction
	REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i = 5"));
	// verify that the counts are still correct in the second transaction
	for (index_t i = 0; i < n + 1; i++) {
		result = con2.Query("SELECT FIRST(i), COUNT(i) FROM integers WHERE i=" + to_string(keys[i]));
		REQUIRE(CHECK_COLUMN(result, 0, {Value(keys[i])}));
		REQUIRE(CHECK_COLUMN(result, 1, {Value(1500)}));
	}
	result = con2.Query("SELECT COUNT(i) FROM integers WHERE i < 10");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(7500)}));

	// create an index in the first transaction now
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers(i)"));
	// verify that the counts are still correct for con2
	for (index_t i = 0; i < n + 1; i++) {
		result = con2.Query("SELECT FIRST(i), COUNT(i) FROM integers WHERE i=" + to_string(keys[i]));
		REQUIRE(CHECK_COLUMN(result, 0, {Value(keys[i])}));
		REQUIRE(CHECK_COLUMN(result, 1, {Value(1500)}));
	}
	result = con2.Query("SELECT COUNT(i) FROM integers WHERE i<10");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(7500)}));

	// do a bunch of queries in the first transaction
	result = con.Query("SELECT count(i) FROM integers WHERE i > 1 AND i < 3");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(1500)}));
	result = con.Query("SELECT count(i) FROM integers WHERE i >= 1 AND i < 3");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(3000)}));
	result = con.Query("SELECT count(i) FROM integers WHERE i > 1");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(4500)}));
	result = con.Query("SELECT count(i) FROM integers WHERE i < 4");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(4500)}));
	result = con.Query("SELECT count(i) FROM integers WHERE i < 5");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(6000)}));

	// verify that the counts are still correct in the second transaction
	result = con2.Query("SELECT COUNT(i) FROM integers WHERE i<10");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(7500)}));
	result = con2.Query("SELECT COUNT(i) FROM integers WHERE i=5");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(1500)}));
}

TEST_CASE("ART Negative Range", "[art-neg]") {
    unique_ptr<QueryResult> result;
    DuckDB db(nullptr);

    Connection con(db);

    REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i integer)"));
    index_t n = 1000;
    auto keys = unique_ptr<int32_t[]>(new int32_t[n]);
    for (index_t i = 0; i < n; i++) {
        keys[i] = i -500 ;
    }

    REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
    for (index_t i = 0; i < n; i++) {
        REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", keys[i]));
    }
    REQUIRE_NO_FAIL(con.Query("COMMIT"));
    REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers(i)"));

    result = con.Query("SELECT sum(i) FROM integers WHERE i >= -500 AND i <= -498");
    REQUIRE(CHECK_COLUMN(result, 0, {Value(-1497)}));
    result = con.Query("SELECT sum(i) FROM integers WHERE i >= -10 AND i <= 5");
    REQUIRE(CHECK_COLUMN(result, 0, {Value(-40)}));
    result = con.Query("SELECT sum(i) FROM integers WHERE i >= 10 AND i <= 15");
    REQUIRE(CHECK_COLUMN(result, 0, {Value(75)}));
    REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
    REQUIRE_NO_FAIL(con.Query("DROP TABLE integers"));
}

float generate_small_float(){
    return static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
}

float generate_float(float min_float, float max_float){
    return min_float + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(max_float-min_float)));
}

double generate_small_double(){
    return static_cast <double> (rand()) / static_cast <double> (RAND_MAX);
}

double generate_double(double min_double, double max_double){
    return min_double + static_cast <double> (rand()) /( static_cast <double> (RAND_MAX/(max_double-min_double)));
}

int full_scan_float(float* keys, index_t size, float low, float high){
    int sum = 0;
    for (index_t i = 0; i < size; i ++){
        if (keys[i] >= low && keys[i]  <= high) {
            sum += 1;
        }
    }
    return sum;
}

int full_scan_double(double* keys, index_t size, double low, double high){
    int sum = 0;
    for (index_t i = 0; i < size; i ++){
        if (keys[i] >= low && keys[i]  <= high) {
            sum += 1;
        }
    }
    return sum;
}

TEST_CASE("ART Floating Point Small", "[art-float-small]") {
    unique_ptr<QueryResult> result;
    DuckDB db(nullptr);
    float low,high, aux;
    int answer;

    Connection con(db);
    //! Will use 100 keys
    auto keys = unique_ptr<float[]>(new float[100]);
    index_t n  = 100;
    REQUIRE_NO_FAIL(con.Query("CREATE TABLE numbers(i real)"));
    //! Generate 100 small floats (0.0 - 1.0)
    for (index_t i = 0; i < 10; i ++){
        keys[i] = generate_small_float();
    }
    //! Generate 40 floats (-50/50)
    for (index_t i = 10; i < 50; i ++){
        keys[i] = generate_float(-50,50);
    }
    //!Generate 50 floats (min/max)
    for (index_t i = 50; i < 100; i ++){
        keys[i] = generate_float(FLT_MIN,FLT_MAX);
    }
    //! Insert values and create index
    REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
    for (index_t i = 0; i < n; i++) {
        REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST("+ to_string(keys[i])+ " AS REAL))"));
    }
    REQUIRE_NO_FAIL(con.Query("COMMIT"));
    REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON numbers(i)"));
    //! Generate 5 small-small range queries
    for (index_t i = 0; i < 5; i++) {
        aux = generate_small_float();
        high = generate_small_float();
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_float(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS REAL) and i <= CAST("+ to_string(high)+ " AS REAL)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }
    //! Generate 5 normal-normal range queries
    for (index_t i = 0; i < 5; i++) {
        aux = generate_float(-50,50);
        high = generate_float(-50,50);
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_float(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS REAL) and i <= CAST("+ to_string(high)+ " AS REAL)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }
    //! Generate 5 big-big range queries
    for (index_t i = 0; i < 5; i++) {
        aux = generate_float(FLT_MIN,FLT_MAX);
        high = generate_float(FLT_MIN,FLT_MAX);
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_float(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS REAL) and i <= CAST("+ to_string(high)+ " AS REAL)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }

    REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
    REQUIRE_NO_FAIL(con.Query("DROP TABLE numbers"));
}

TEST_CASE("ART Floating Point Double Small", "[art-double-small]") {
    double low,high, aux;
    int answer;
    unique_ptr<QueryResult> result;
    DuckDB db(nullptr);

    Connection con(db);
    //! Will use 10k keys
    auto keys = unique_ptr<double[]>(new double[100]);
    index_t n  = 100;
    REQUIRE_NO_FAIL(con.Query("CREATE TABLE numbers(i double)"));
    //! Generate 10 small double (0.0 - 1.0)
    for (index_t i = 0; i < 10; i ++){
        keys[i] = generate_small_double();
    }
    //! Generate 40 double (-50/50)
    for (index_t i = 10; i < 50; i ++){
        keys[i] = generate_double(-50,50);
    }
    //!Generate 50 double (min/max)
    for (index_t i = 50; i < 100; i ++){
        keys[i] = generate_double(FLT_MIN,FLT_MAX);
    }
    //! Insert values and create index
    REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
    for (index_t i = 0; i < n; i++) {
        REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST("+ to_string(keys[i])+ " AS DOUBLE))"));
    }
    REQUIRE_NO_FAIL(con.Query("COMMIT"));
    REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON numbers(i)"));
    //! Generate 5 small-small range queries

    for (index_t i = 0; i < 5; i++) {
        aux = generate_small_double();
        high = generate_small_double();
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_double(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS DOUBLE) and i <= CAST("+ to_string(high)+ " AS DOUBLE)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }
    //! Generate 5 small-normal range queries
    for (index_t i = 0; i < 5; i++) {
        aux = generate_double(-50,50);
        high = generate_double(-50,50);
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_double(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS DOUBLE) and i <= CAST("+ to_string(high)+ " AS DOUBLE)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }
    //! Generate 5 normal-normal range queries
    for (index_t i = 0; i < 5; i++) {
        aux = generate_double(FLT_MIN,FLT_MAX);
        high = generate_double(FLT_MIN,FLT_MAX);
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_double(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS DOUBLE) and i <= CAST("+ to_string(high)+ " AS DOUBLE)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }

    REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
    REQUIRE_NO_FAIL(con.Query("DROP TABLE numbers"));
}

TEST_CASE("ART Floating Point", "[art-float][.]") {
    srand(1);
    unique_ptr<QueryResult> result;
    DuckDB db(nullptr);
    float low,high, aux;
    int answer;

    Connection con(db);
    //! Will use 10k keys
    auto keys = unique_ptr<float[]>(new float[10000]);
    index_t n  = 10000;
    REQUIRE_NO_FAIL(con.Query("CREATE TABLE numbers(i real)"));
    //! Generate 1000 small floats (0.0 - 1.0)
    for (index_t i = 0; i < 1000; i ++){
        keys[i] = generate_small_float();
    }

    //! Generate 4000 floats (-50/50)
    for (index_t i = 1000; i < 5000; i ++){
        keys[i] = generate_float(-50,50);
    }
    //!Generate 5000 floats (min/max)
    for (index_t i = 5000; i < 10000; i ++){
        keys[i] = generate_float(FLT_MIN,FLT_MAX);
    }
    //! Insert values and create index
    REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
    for (index_t i = 0; i < n; i++) {
        REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST("+ to_string(keys[i])+ " AS REAL))"));
    }
    REQUIRE_NO_FAIL(con.Query("COMMIT"));
    REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON numbers(i)"));
    //! Generate 500 small-small range queries
    for (index_t i = 0; i < 500; i++) {
        aux = generate_small_float();
        high = generate_small_float();
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_float(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS REAL) and i <= CAST("+ to_string(high)+ " AS REAL)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }
    //! Generate 500 normal-normal range queries
    for (index_t i = 0; i < 500; i++) {
        aux = generate_float(-50,50);
        high = generate_float(-50,50);
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_float(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS REAL) and i <= CAST("+ to_string(high)+ " AS REAL)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }
    //! Generate 500 big-big range queries
    for (index_t i = 0; i < 500; i++) {
        aux = generate_float(FLT_MIN,FLT_MAX);
        high = generate_float(FLT_MIN,FLT_MAX);
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_float(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS REAL) and i <= CAST("+ to_string(high)+ " AS REAL)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }

    REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
    REQUIRE_NO_FAIL(con.Query("DROP TABLE numbers"));
}

TEST_CASE("ART Floating Point Double", "[art-double][.]") {
    srand(1);
    unique_ptr<QueryResult> result;
    DuckDB db(nullptr);
    double low,high, aux;
    int answer;

    Connection con(db);
    //! Will use 10k keys
    auto keys = unique_ptr<double[]>(new double[10000]);
    index_t n  = 10000;
    REQUIRE_NO_FAIL(con.Query("CREATE TABLE numbers(i double)"));
    //! Generate 1000 small double (0.0 - 1.0)
    for (index_t i = 0; i < 1000; i ++){
        keys[i] = generate_small_double();
    }
    //! Generate 4000 double (-50/50)
    for (index_t i = 1000; i < 5000; i ++){
        keys[i] = generate_double(-50,50);
    }
    //!Generate 5000 double (min/max)
    for (index_t i = 5000; i < 10000; i ++){
        keys[i] = generate_double(FLT_MIN,FLT_MAX);
    }
    //! Insert values and create index
    REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
    for (index_t i = 0; i < n; i++) {
        REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST("+ to_string(keys[i])+ " AS DOUBLE))"));
    }
    REQUIRE_NO_FAIL(con.Query("COMMIT"));
    REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON numbers(i)"));

    //! Generate 500 small-small range queries
    for (index_t i = 0; i < 500; i++) {
        aux = generate_small_double();
        high = generate_small_double();
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_double(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS DOUBLE) and i <= CAST("+ to_string(high)+ " AS DOUBLE)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }
    //! Generate 500 small-normal range queries
    for (index_t i = 0; i < 500; i++) {
        aux = generate_double(-50,50);
        high = generate_double(-50,50);
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_double(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS DOUBLE) and i <= CAST("+ to_string(high)+ " AS DOUBLE)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }
    //! Generate 500 normal-normal range queries
    for (index_t i = 0; i < 500; i++) {
        aux = generate_double(FLT_MIN,FLT_MAX);
        high = generate_double(FLT_MIN,FLT_MAX);
        low = min(aux,high);
        high = max(aux,high);
        answer = full_scan_double(keys.get(),n,low,high);
        result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST("+ to_string(low)+ " AS DOUBLE) and i <= CAST("+ to_string(high)+ " AS DOUBLE)");
        REQUIRE(CHECK_COLUMN(result, 0, {answer}));
    }

    REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
    REQUIRE_NO_FAIL(con.Query("DROP TABLE numbers"));
}

TEST_CASE("ART FP Unique Constraint", "[art-float-unique]") {
    unique_ptr<QueryResult> result;
    DuckDB db(nullptr);
    Connection con(db);

    REQUIRE_NO_FAIL(con.Query("CREATE TABLE numbers(i REAL PRIMARY KEY, j INTEGER)"));

    //! insert two conflicting pairs at the same time
    REQUIRE_FAIL(con.Query("INSERT INTO numbers VALUES (3.45, 4), (3.45, 5)"));

    //! insert unique values
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (3.45, 4), (2.2, 5)"));

    result = con.Query("SELECT * FROM numbers");
    REQUIRE(CHECK_COLUMN(result, 0, {Value::FLOAT(3.45), Value::FLOAT(2.2)}));
    REQUIRE(CHECK_COLUMN(result, 1, {4, 5}));

    //! insert a duplicate value as part of a chain of values
    REQUIRE_FAIL(con.Query("INSERT INTO numbers VALUES (6, 6), (3.45, 4);"));

    //! now insert just the first value
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (6, 6);"));

    result = con.Query("SELECT * FROM numbers");
    REQUIRE(CHECK_COLUMN(result, 0, {Value::FLOAT(3.45), Value::FLOAT(2.2), Value::FLOAT(6)}));
    REQUIRE(CHECK_COLUMN(result, 1, {4, 5, 6}));

    //! insert NULL value in PRIMARY KEY is not allowed
    REQUIRE_FAIL(con.Query("INSERT INTO numbers VALUES (NULL, 4);"));

    //! update NULL is also not allowed
    REQUIRE_FAIL(con.Query("UPDATE numbers SET i=NULL;"));
}

TEST_CASE("ART FP Special Cases", "[art-fp-special]") {
    unique_ptr<QueryResult> result;
    DuckDB db(nullptr);
    Connection con(db);

    REQUIRE_NO_FAIL(con.Query("CREATE TABLE numbers(i REAL)"));

    //! INF
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(POWER(1000,10000) AS REAL))"));
    //! -INF
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(-POWER(1000,10000) AS REAL))"));
    //! NaN
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(POWER(1000,10000)*0 AS REAL))"));
    //! +0
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(0 AS REAL))"));
    //! -0
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(-0 AS REAL))"));


    REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON numbers(i)"));
    //! INF
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(POWER(1000,10000) AS REAL)");
    REQUIRE(CHECK_COLUMN(result, 0, {1}));
    //! -INF
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(-POWER(1000,10000) AS REAL)");
    REQUIRE(CHECK_COLUMN(result, 0, {1}));
    //! NaN
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(POWER(1000,10000)*0 AS REAL)");
    REQUIRE(CHECK_COLUMN(result, 0, {0}));
    //! +0
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(0 AS REAL)");
    REQUIRE(CHECK_COLUMN(result, 0, {2}));
    //! -0
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(-0 AS REAL)");
    REQUIRE(CHECK_COLUMN(result, 0, {2}));
    //! -INF till INF
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST(-POWER(1000,10000) AS REAL) and i <= CAST(POWER(1000,10000) AS REAL)");
    REQUIRE(CHECK_COLUMN(result, 0, {4}));
}

TEST_CASE("ART Double Special Cases", "[art-double-special]") {
    unique_ptr<QueryResult> result;
    DuckDB db(nullptr);
    Connection con(db);

    REQUIRE_NO_FAIL(con.Query("CREATE TABLE numbers(i DOUBLE)"));
    //! INF
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(POWER(1000,10000) AS DOUBLE))"));
    //! -INF
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(-POWER(1000,10000) AS DOUBLE))"));
    //! NaN
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(POWER(1000,10000)*0 AS DOUBLE))"));
    //! +0
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(0 AS DOUBLE))"));
    //! -0
    REQUIRE_NO_FAIL(con.Query("INSERT INTO numbers VALUES (CAST(-0 AS DOUBLE))"));


    REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON numbers(i)"));
    //! INF
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(POWER(1000,10000) AS DOUBLE)");
    REQUIRE(CHECK_COLUMN(result, 0, {1}));
    //! -INF
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(-POWER(1000,10000) AS DOUBLE)");
    REQUIRE(CHECK_COLUMN(result, 0, {1}));
    //! NaN
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(POWER(1000,10000)*0 AS DOUBLE)");
    REQUIRE(CHECK_COLUMN(result, 0, {0}));
    //! +0
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(0 AS DOUBLE)");
    REQUIRE(CHECK_COLUMN(result, 0, {2}));
    //! -0
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i = CAST(-0 AS DOUBLE)");
    REQUIRE(CHECK_COLUMN(result, 0, {2}));
    //! -INF till INF
    result = con.Query("SELECT COUNT(i) FROM numbers WHERE i >= CAST(-POWER(1000,10000) AS REAL) and i <= CAST(POWER(1000,10000) AS DOUBLE)");
    REQUIRE(CHECK_COLUMN(result, 0, {4}));
}

TEST_CASE("Test updates resulting from big index scans", "[art][.]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	int64_t sum = 0;
	int64_t count = 0;

	REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i integer)"));
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers(i)"));
	for (index_t i = 0; i < 25000; i++) {
		int32_t value = i + 1;

		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", value));

		sum += value;
		count++;
	}
	REQUIRE_NO_FAIL(con.Query("COMMIT"));

	// check the sum and the count
	result = con.Query("SELECT SUM(i), COUNT(i) FROM integers WHERE i>0");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(sum)}));
	REQUIRE(CHECK_COLUMN(result, 1, {Value::BIGINT(count)}));

	// update the data with an index scan
	REQUIRE_NO_FAIL(con.Query("UPDATE integers SET i=i+1 WHERE i>0"));
	sum += count;

	// now check the sum and the count again
	result = con.Query("SELECT SUM(i), COUNT(i) FROM integers WHERE i>0");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::BIGINT(sum)}));
	REQUIRE(CHECK_COLUMN(result, 1, {Value::BIGINT(count)}));

	// now delete from the table with an index scan
	REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i>0"));

	result = con.Query("SELECT SUM(i), COUNT(i) FROM integers WHERE i>0");
	REQUIRE(CHECK_COLUMN(result, 0, {Value()}));
	REQUIRE(CHECK_COLUMN(result, 1, {Value::BIGINT(0)}));
}

TEST_CASE("ART Node 4", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);

	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i integer)"));
	index_t n = 4;
	auto keys = unique_ptr<int32_t[]>(new int32_t[n]);
	for (index_t i = 0; i < n; i++) {
		keys[i] = i + 1;
	}

	for (index_t i = 0; i < n; i++) {
		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", keys[i]));
	}

	for (index_t i = 0; i < n; i++) {
		result = con.Query("SELECT i FROM integers WHERE i=$1", keys[i]);
		REQUIRE(CHECK_COLUMN(result, 0, {Value(keys[i])}));
	}
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers(i)"));
	result = con.Query("SELECT sum(i) FROM integers WHERE i <= 2");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(3)}));
	result = con.Query("SELECT sum(i) FROM integers WHERE i > 1");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(2 + 3 + 4)}));
	// Now Deleting all elements
	for (index_t i = 0; i < n; i++) {
		REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i=$1", keys[i]));
	}
	REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i = 0"));
	REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
	REQUIRE_NO_FAIL(con.Query("DROP TABLE integers"));
}

TEST_CASE("ART Node 16", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);

	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i integer)"));
	index_t n = 6;
	auto keys = unique_ptr<int32_t[]>(new int32_t[n]);
	for (index_t i = 0; i < n; i++) {
		keys[i] = i + 1;
	}

	for (index_t i = 0; i < n; i++) {
		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", keys[i]));
	}
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers(i)"));
	for (index_t i = 0; i < n; i++) {
		result = con.Query("SELECT i FROM integers WHERE i=$1", keys[i]);
		REQUIRE(CHECK_COLUMN(result, 0, {Value(keys[i])}));
	}
	result = con.Query("SELECT sum(i) FROM integers WHERE i <=2");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(3)}));
	result = con.Query("SELECT sum(i) FROM integers WHERE i > 4");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(5 + 6)}));
	// Now Deleting all elements
	for (index_t i = 0; i < n; i++) {
		REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i=$1", keys[i]));
	}
	REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
	REQUIRE_NO_FAIL(con.Query("DROP TABLE integers"));
}

TEST_CASE("ART Node 48", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);

	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i integer)"));
	index_t n = 20;
	auto keys = unique_ptr<int32_t[]>(new int32_t[n]);
	for (index_t i = 0; i < n; i++) {
		keys[i] = i + 1;
	}
	int64_t expected_sum = 0;
	for (index_t i = 0; i < n; i++) {
		REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES ($1)", keys[i]));
		expected_sum += keys[i];
	}
	REQUIRE_NO_FAIL(con.Query("CREATE INDEX i_index ON integers(i)"));
	for (index_t i = 0; i < n; i++) {
		result = con.Query("SELECT i FROM integers WHERE i=$1", keys[i]);
		REQUIRE(CHECK_COLUMN(result, 0, {Value(keys[i])}));
	}
	result = con.Query("SELECT sum(i) FROM integers WHERE i <=2");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(3)}));
	result = con.Query("SELECT sum(i) FROM integers WHERE i > 15");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(16 + 17 + 18 + 19 + 20)}));

	// delete an element and reinsert it
	REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i=16"));
	REQUIRE_NO_FAIL(con.Query("INSERT INTO integers VALUES (16)"));

	// query again
	result = con.Query("SELECT sum(i) FROM integers WHERE i <=2");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(3)}));
	result = con.Query("SELECT sum(i) FROM integers WHERE i > 15");
	REQUIRE(CHECK_COLUMN(result, 0, {Value(16 + 17 + 18 + 19 + 20)}));

	// Now delete all elements
	for (index_t i = 0; i < n; i++) {
		REQUIRE_NO_FAIL(con.Query("DELETE FROM integers WHERE i=$1", keys[i]));
		expected_sum -= keys[i];
		// verify the sum
		result = con.Query("SELECT sum(i) FROM integers WHERE i > 0");
		REQUIRE(CHECK_COLUMN(result, 0, {expected_sum == 0 ? Value() : Value::BIGINT(expected_sum)}));
	}
	REQUIRE_NO_FAIL(con.Query("DROP INDEX i_index"));
	REQUIRE_NO_FAIL(con.Query("DROP TABLE integers"));
}

TEST_CASE("Index Exceptions", "[art]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);

	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE integers(i integer, j integer, k BOOLEAN)"));

	REQUIRE_FAIL(con.Query("CREATE INDEX ON integers(i)"));

	REQUIRE_FAIL(con.Query("CREATE INDEX i_index ON integers(i COLLATE \"de_DE\")"));

	REQUIRE_FAIL(con.Query("CREATE INDEX i_index ON integers using blabla(i)"));

	REQUIRE_FAIL(con.Query("CREATE INDEX i_index ON integers(i,j)"));

	REQUIRE_FAIL(con.Query("CREATE INDEX i_index ON integers(k)"));

	REQUIRE_FAIL(con.Query("CREATE INDEX i_index ON integers(f)"));
}
