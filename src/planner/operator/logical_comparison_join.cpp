#include "duckdb/planner/operator/logical_comparison_join.hpp"
#include "duckdb/common/string_util.hpp"

using namespace duckdb;
using namespace std;

LogicalComparisonJoin::LogicalComparisonJoin(JoinType type, LogicalOperatorType logical_type)
    : LogicalJoin(type, logical_type) {
}

string LogicalComparisonJoin::ParamsToString() const {
	string result = "[" + JoinTypeToString(type);
	if (conditions.size() > 0) {
		result += " ";
		result += StringUtil::Join(conditions, conditions.size(), ", ", [](const JoinCondition& condition){
			return ExpressionTypeToString(condition.comparison) + "(" + condition.left->GetName() + ", " +
			          condition.right->GetName() + ")";
		});
		result += "]";
	}

	return result;
}
