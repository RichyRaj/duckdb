#include "duckdb.hpp"

using namespace duckdb;

int main() {
	DuckDB db(nullptr);
	Connection con(db);

	con.Query("CREATE TABLE sensors(id INTEGER PRIMARY KEY NOT NULL, type VARCHAR(20), location VARCHAR(30))");
	con.Query("CREATE TABLE sensor_data(s_id INTEGER NOT NULL, temp DOUBLE, cpu DOUBLE)");
	con.Query("INSERT INTO sensors VALUES (1, 'a', 'floor')");
	con.Query("INSERT INTO sensors VALUES (2, 'b', 'ceiling')");
	con.Query("INSERT INTO sensors VALUES (3, 'a', 'floor')");
	con.Query("INSERT INTO sensor_data VALUES (1, 92.23, 0.84422)");
	con.Query("INSERT INTO sensor_data VALUES (2, 52.23, 0.22422)");

	// End of Schema Def
	
	// Querying
	auto result = con.Query("SELECT * FROM sensors");
	result->Print();
	
	con.Query("SET T1 = sensors");
	con.Query("SET T2 = sensor_data");
	result = con.Query("SHOW META");
	result->Print();
	result = con.Query("SHOW DATA");
	result->Print();


	// From the example
	/**
	con.Query("CREATE TABLE integers(i INTEGER)");
	con.Query("INSERT INTO integers VALUES (3)");
	auto result = con.Query("SELECT * FROM integers");
	// auto result = con.Query("FIND MAX temp");	
	result->Print();
	**/
}
