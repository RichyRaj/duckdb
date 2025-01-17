#include "duckdb/planner/pragma_handler.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/storage_manager.hpp"
#include "duckdb/storage/buffer_manager.hpp"
#include "duckdb/common/operator/cast_operators.hpp"
#include "duckdb/parser/statement/pragma_statement.hpp"

#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/star_expression.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/parser/expression/function_expression.hpp"

#include <cctype>

using namespace duckdb;
using namespace std;

PragmaHandler::PragmaHandler(ClientContext &context) : context(context) {
}

unique_ptr<SQLStatement> PragmaHandler::HandlePragma(PragmaStatement &pragma) {
	string keyword = StringUtil::Lower(pragma.name);
	if (keyword == "table_info") {
		if (pragma.pragma_type != PragmaType::CALL) {
			throw ParserException("Invalid PRAGMA table_info: expected table name");
		}
		if (pragma.parameters.size() != 1) {
			throw ParserException("Invalid PRAGMA table_info: table_info takes exactly one argument");
		}
		// generate a SelectStatement that selects from the pragma_table_info function
		// i.e. SELECT * FROM pragma_table_info('table_name')
		auto select_statement = make_unique<SelectStatement>();
		auto select_node = make_unique<SelectNode>();
		select_node->select_list.push_back(make_unique<StarExpression>());

		vector<unique_ptr<ParsedExpression>> children;
		children.push_back(make_unique<ConstantExpression>(SQLTypeId::VARCHAR, pragma.parameters[0]));
		auto table_function = make_unique<TableFunctionRef>();
		table_function->function = make_unique<FunctionExpression>(DEFAULT_SCHEMA, "pragma_table_info", children);
		select_node->from_table = move(table_function);
		select_statement->node = move(select_node);
		return move(select_statement);
	} else if (keyword == "enable_profile" || keyword == "enable_profiling") {
		// enable profiling
		if (pragma.pragma_type == PragmaType::ASSIGNMENT) {
			// enable_profiling with assignment
			// this is either enable_profiling = json, or enable_profiling = query_tree
			string assignment = pragma.parameters[0].ToString();
			if (assignment == "json") {
				context.profiler.automatic_print_format = ProfilerPrintFormat::JSON;
			} else if (assignment == "query_tree") {
				context.profiler.automatic_print_format = ProfilerPrintFormat::QUERY_TREE;
			} else {
				throw ParserException("Unrecognized print format %s, supported formats: [json, query_tree]", assignment.c_str());
			}
		} else if (pragma.pragma_type == PragmaType::NOTHING) {
			context.profiler.automatic_print_format = ProfilerPrintFormat::QUERY_TREE;
		} else {
			throw ParserException("Cannot call PRAGMA enable_profiling");
		}
		context.profiler.Enable();
	} else if (keyword == "disable_profile" || keyword == "disable_profiling") {
		if (pragma.pragma_type != PragmaType::NOTHING) {
			throw ParserException("disable_profiling cannot take parameters!");
		}
		// enable profiling
		context.profiler.Disable();
		context.profiler.automatic_print_format = ProfilerPrintFormat::NONE;
	} else if (keyword == "profiling_output" || keyword == "profile_output") {
		// set file location of where to save profiling output
		if (pragma.pragma_type != PragmaType::ASSIGNMENT || pragma.parameters[0].type != TypeId::VARCHAR) {
			throw ParserException("Profiling output must be an assignment (e.g. PRAGMA profile_output='/tmp/test.json')");
		}
		context.profiler.save_location = pragma.parameters[0].str_value;
	} else if (keyword == "memory_limit") {
		if (pragma.pragma_type != PragmaType::ASSIGNMENT) {
			throw ParserException("Memory limit must be an assignment (e.g. PRAGMA memory_limit='1GB')");
		}
		if (pragma.parameters[0].type == TypeId::VARCHAR) {
			ParseMemoryLimit(pragma.parameters[0].str_value);
		} else {
			int64_t value = pragma.parameters[0].GetNumericValue();
			if (value < 0) {
				// limit < 0, set limit to infinite
				context.db.storage->buffer_manager->SetLimit();
			} else {
				throw ParserException("Memory limit must be an assignment with a memory unit (e.g. PRAGMA memory_limit='1GB')");
			}
		}
	} else {
		throw ParserException("Unrecognized PRAGMA keyword: %s", keyword.c_str());
	}
	return nullptr;
}

void PragmaHandler::ParseMemoryLimit(string arg) {
	// split based on the number/non-number
	index_t idx = 0;
	while (std::isspace(arg[idx])) {
		idx++;
	}
	index_t num_start = idx;
	while ((arg[idx] >= '0' && arg[idx] <= '9') || arg[idx] == '.' || arg[idx] == 'e' || arg[idx] == 'E' ||
	       arg[idx] == '-') {
		idx++;
	}
	if (idx == num_start) {
		throw ParserException("Memory limit must have a number (e.g. PRAGMA memory_limit=1GB");
	}
	string number = arg.substr(num_start, idx - num_start);

	// try to parse the number
	double limit = Cast::Operation<const char *, double>(number.c_str());

	// now parse the memory limit unit (e.g. bytes, gb, etc)
	while (std::isspace(arg[idx])) {
		idx++;
	}
	index_t start = idx;
	while (idx < arg.size() && !std::isspace(arg[idx])) {
		idx++;
	}
	if (limit < 0) {
		// limit < 0, set limit to infinite
		context.db.storage->buffer_manager->SetLimit();
		return;
	}
	string unit = StringUtil::Lower(arg.substr(start, idx - start));
	index_t multiplier;
	if (unit == "byte" || unit == "bytes" || unit == "b") {
		multiplier = 1;
	} else if (unit == "kilobyte" || unit == "kilobytes" || unit == "kb" || unit == "k") {
		multiplier = 1000LL;
	} else if (unit == "megabyte" || unit == "megabytes" || unit == "mb" || unit == "m") {
		multiplier = 1000LL * 1000LL;
	} else if (unit == "gigabyte" || unit == "gigabytes" || unit == "gb" || unit == "g") {
		multiplier = 1000LL * 1000LL * 1000LL;
	} else if (unit == "terabyte" || unit == "terabytes" || unit == "tb" || unit == "t") {
		multiplier = 1000LL * 1000LL * 1000LL * 1000LL;
	} else {
		throw ParserException("Unknown unit for memory_limit: %s (expected: b, mb, gb or tb)", unit.c_str());
	}
	// set the new limit in the buffer manager
	context.db.storage->buffer_manager->SetLimit((index_t)(multiplier * limit));
}
