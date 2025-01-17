//===----------------------------------------------------------------------===//
//                         DuckDB
//
// parser/transformer.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/constants.hpp"
#include "duckdb/common/enums/expression_type.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/parser/tokens.hpp"

#include "pg_definitions.hpp"
#include "nodes/parsenodes.hpp"


namespace duckdb {

class ColumnDefinition;
struct OrderByNode;

//! The transformer class is responsible for transforming the internal Postgres
//! parser representation into the DuckDB representation
class Transformer {
public:
	//! Transforms a Postgres parse tree into a set of SQL Statements
	bool TransformParseTree(PGList *tree, vector<unique_ptr<SQLStatement>> &statements);
	string NodetypeToString(PGNodeTag type);

	index_t prepared_statement_parameter_index = 0;

private:
	//! Transforms a Postgres statement into a single SQL statement
	unique_ptr<SQLStatement> TransformStatement(PGNode *stmt);
	//===--------------------------------------------------------------------===//
	// Statement transformation
	//===--------------------------------------------------------------------===//
	//! Transform a Postgres T_PGSelectStmt node into a SelectStatement
	unique_ptr<SelectStatement> TransformSelect(PGNode *node);
	//! Transform a Postgres T_AlterStmt node into a AlterTableStatement
	unique_ptr<AlterTableStatement> TransformAlter(PGNode *node);
	//! Transform a Postgres T_PGRenameStmt node into a RenameStatement
	unique_ptr<AlterTableStatement> TransformRename(PGNode *node);
	//! Transform a Postgres T_PGCreateStmt node into a CreateTableStatement
	unique_ptr<CreateTableStatement> TransformCreateTable(PGNode *node);
	//! Transform a Postgres T_PGCreateStmt node into a CreateTableStatement
	unique_ptr<CreateTableStatement> TransformCreateTableAs(PGNode *node);
	//! Transform a Postgres node into a CreateSchemaStatement
	unique_ptr<CreateSchemaStatement> TransformCreateSchema(PGNode *node);
	//! Transform a Postgres T_PGCreateSeqStmt node into a CreateSequenceStatement
	unique_ptr<CreateSequenceStatement> TransformCreateSequence(PGNode *node);
	//! Transform a Postgres T_PGViewStmt node into a CreateViewStatement
	unique_ptr<CreateViewStatement> TransformCreateView(PGNode *node);
	//! Transform a Postgres T_PGDropStmt node into a Drop[Table,Schema]Statement
	unique_ptr<SQLStatement> TransformDrop(PGNode *node);
	//! Transform a Postgres T_PGInsertStmt node into a InsertStatement
	unique_ptr<InsertStatement> TransformInsert(PGNode *node);
	//! Transform a Postgres T_PGIndexStmt node into CreateIndexStatement
	unique_ptr<CreateIndexStatement> TransformCreateIndex(PGNode *node);
	//! Transform a Postgres T_PGCopyStmt node into a CopyStatement
	unique_ptr<CopyStatement> TransformCopy(PGNode *node);
	//! Transform a Postgres T_PGTransactionStmt node into a TransactionStatement
	unique_ptr<TransactionStatement> TransformTransaction(PGNode *node);
	//! Transform a Postgres T_DeleteStatement node into a DeleteStatement
	unique_ptr<DeleteStatement> TransformDelete(PGNode *node);
	//! Transform a Postgres T_PGUpdateStmt node into a UpdateStatement
	unique_ptr<UpdateStatement> TransformUpdate(PGNode *node);
	//! Transform a Postgres T_PGPragmaStmt node into a PragmaStatement
	unique_ptr<PragmaStatement> TransformPragma(PGNode *node);

	unique_ptr<PrepareStatement> TransformPrepare(PGNode *node);
	unique_ptr<ExecuteStatement> TransformExecute(PGNode *node);
	unique_ptr<DeallocateStatement> TransformDeallocate(PGNode *node);

	//===--------------------------------------------------------------------===//
	// Query Node Transform
	//===--------------------------------------------------------------------===//
	//! Transform a Postgres T_PGSelectStmt node into a QueryNode
	unique_ptr<QueryNode> TransformSelectNode(PGSelectStmt *node);

	//===--------------------------------------------------------------------===//
	// Expression Transform
	//===--------------------------------------------------------------------===//
	//! Transform a Postgres boolean expression into an Expression
	unique_ptr<ParsedExpression> TransformBoolExpr(PGBoolExpr *root);
	//! Transform a Postgres case expression into an Expression
	unique_ptr<ParsedExpression> TransformCase(PGCaseExpr *root);
	//! Transform a Postgres type cast into an Expression
	unique_ptr<ParsedExpression> TransformTypeCast(PGTypeCast *root);
	//! Transform a Postgres coalesce into an Expression
	unique_ptr<ParsedExpression> TransformCoalesce(PGAExpr *root);
	//! Transform a Postgres column reference into an Expression
	unique_ptr<ParsedExpression> TransformColumnRef(PGColumnRef *root);
	//! Transform a Postgres constant value into an Expression
	unique_ptr<ParsedExpression> TransformValue(PGValue val);
	//! Transform a Postgres operator into an Expression
	unique_ptr<ParsedExpression> TransformAExpr(PGAExpr *root);
	//! Transform a Postgres abstract expression into an Expression
	unique_ptr<ParsedExpression> TransformExpression(PGNode *node);
	//! Transform a Postgres function call into an Expression
	unique_ptr<ParsedExpression> TransformFuncCall(PGFuncCall *root);

	//! Transform a Postgres constant value into an Expression
	unique_ptr<ParsedExpression> TransformConstant(PGAConst *c);

	unique_ptr<ParsedExpression> TransformResTarget(PGResTarget *root);
	unique_ptr<ParsedExpression> TransformNullTest(PGNullTest *root);
	unique_ptr<ParsedExpression> TransformParamRef(PGParamRef *node);
	unique_ptr<ParsedExpression> TransformSQLValueFunction(PGSQLValueFunction *node);

	unique_ptr<ParsedExpression> TransformSubquery(PGSubLink *root);
	//===--------------------------------------------------------------------===//
	// Constraints transform
	//===--------------------------------------------------------------------===//
	unique_ptr<Constraint> TransformConstraint(PGListCell *cell);

	unique_ptr<Constraint> TransformConstraint(PGListCell *cell, ColumnDefinition &column, index_t index);

	//===--------------------------------------------------------------------===//
	// Helpers
	//===--------------------------------------------------------------------===//
	string TransformAlias(PGAlias *root);
	void TransformCTE(PGWithClause *de_with_clause, SelectStatement &select);
	// Operator String to ExpressionType (e.g. + => OPERATOR_ADD)
	ExpressionType OperatorToExpressionType(string &op);

	unique_ptr<ParsedExpression> TransformUnaryOperator(string op, unique_ptr<ParsedExpression> child);
	unique_ptr<ParsedExpression> TransformBinaryOperator(string op, unique_ptr<ParsedExpression> left,
	                                                     unique_ptr<ParsedExpression> right);
	//===--------------------------------------------------------------------===//
	// TableRef transform
	//===--------------------------------------------------------------------===//
	//! Transform a Postgres node into a TableRef
	unique_ptr<TableRef> TransformTableRefNode(PGNode *node);
	//! Transform a Postgres FROM clause into a TableRef
	unique_ptr<TableRef> TransformFrom(PGList *root);
	//! Transform a Postgres table reference into a TableRef
	unique_ptr<TableRef> TransformRangeVar(PGRangeVar *root);
	//! Transform a Postgres table-producing function into a TableRef
	unique_ptr<TableRef> TransformRangeFunction(PGRangeFunction *root);
	//! Transform a Postgres join node into a TableRef
	unique_ptr<TableRef> TransformJoin(PGJoinExpr *root);
	//! Transform a table producing subquery into a TableRef
	unique_ptr<TableRef> TransformRangeSubselect(PGRangeSubselect *root);

	//! Transform a Postgres TypeName string into a SQLType
	SQLType TransformTypeName(PGTypeName *name);

	//! Transform a Postgres GROUP BY expression into a list of Expression
	bool TransformGroupBy(PGList *group, vector<unique_ptr<ParsedExpression>> &result);
	//! Transform a Postgres ORDER BY expression into an OrderByDescription
	bool TransformOrderBy(PGList *order, vector<OrderByNode> &result);

	//! Transform a Postgres SELECT clause into a list of Expressions
	bool TransformExpressionList(PGList *list, vector<unique_ptr<ParsedExpression>> &result);
	//! Transform a VALUES list into a set of expressions
	void TransformValuesList(PGList *list, vector<vector<unique_ptr<ParsedExpression>>> &values);

	void TransformWindowDef(PGWindowDef *window_spec, WindowExpression *expr);

	//! Holds window expressions defined by name. We need those when transforming the expressions referring to them.
	unordered_map<string, PGWindowDef *> window_clauses;
};

} // namespace duckdb
