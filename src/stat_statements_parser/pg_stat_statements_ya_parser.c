// NOTE: this file is just a bunch of code borrowed from pg_stat_statements for PG 9.4
// and from our own inhouse implementation of pg_stat_statements for managed PG

#include "postgres.h"

#include <sys/stat.h>
#include <unistd.h>

#include "access/hash.h"
#include "commands/explain.h"
#include "executor/instrument.h"
#include "executor/execdesc.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "parser/analyze.h"
#include "parser/parsetree.h"
#include "parser/scanner.h"
#include "parser/gram.h"
#include "pgstat.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/spin.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"

#include "pg_stat_statements_ya_parser.h"

static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;

#define JUMBLE_SIZE 1024 /* query serialization buffer size */

/*
 * Struct for tracking locations/lengths of constants during normalization
 */
typedef struct pgssLocationLen
{
	int location; /* start offset in query text */
	int length;	  /* length in bytes, or -1 to ignore */
} pgssLocationLen;

/*
 * Working state for computing a query jumble and producing a normalized
 * query string
 */
typedef struct pgssJumbleState
{
	/* Jumble of current query tree */
	unsigned char *jumble;

	/* Number of bytes used in jumble[] */
	Size jumble_len;

	/* Array of locations of constants that should be removed */
	pgssLocationLen *clocations;

	/* Allocated length of clocations array */
	int clocations_buf_size;

	/* Current number of valid entries in clocations array */
	int clocations_count;

	/* highest Param id we've seen, in order to start normalization correctly */
	int highest_extern_param_id;
} pgssJumbleState;

static void AppendJumble(pgssJumbleState *jstate,
						 const unsigned char *item, Size size);
static void JumbleQuery(pgssJumbleState *jstate, Query *query);
static void JumbleRangeTable(pgssJumbleState *jstate, List *rtable);
static void JumbleExpr(pgssJumbleState *jstate, Node *node);
static void RecordConstLocation(pgssJumbleState *jstate, int location);
static void fill_in_constant_lengths(pgssJumbleState *jstate, const char *query);
static int comp_location(const void *a, const void *b);
StringInfo gen_normplan(const char *execution_plan);
static bool need_replace(int token);
void pgss_post_parse_analyze(ParseState *pstate, Query *query);
static char *generate_normalized_query(pgssJumbleState *jstate, const char *query,
									   int *query_len_p, int encoding);

	void stat_statements_parser_init()
{
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = pgss_post_parse_analyze;
}

void stat_statements_parser_deinit()
{
	post_parse_analyze_hook = prev_post_parse_analyze_hook;
}

/*
 * AppendJumble: Append a value that is substantive in a given query to
 * the current jumble.
 */
static void
AppendJumble(pgssJumbleState *jstate, const unsigned char *item, Size size)
{
	unsigned char *jumble = jstate->jumble;
	Size jumble_len = jstate->jumble_len;

	/*
	 * Whenever the jumble buffer is full, we hash the current contents and
	 * reset the buffer to contain just that hash value, thus relying on the
	 * hash to summarize everything so far.
	 */
	while (size > 0)
	{
		Size part_size;

		if (jumble_len >= JUMBLE_SIZE)
		{
			uint32 start_hash = hash_any(jumble, JUMBLE_SIZE);

			memcpy(jumble, &start_hash, sizeof(start_hash));
			jumble_len = sizeof(start_hash);
		}
		part_size = Min(size, JUMBLE_SIZE - jumble_len);
		memcpy(jumble + jumble_len, item, part_size);
		jumble_len += part_size;
		item += part_size;
		size -= part_size;
	}
	jstate->jumble_len = jumble_len;
}

/*
 * Wrappers around AppendJumble to encapsulate details of serialization
 * of individual local variable elements.
 */
#define APP_JUMB(item) \
	AppendJumble(jstate, (const unsigned char *)&(item), sizeof(item))
#define APP_JUMB_STRING(str) \
	AppendJumble(jstate, (const unsigned char *)(str), strlen(str) + 1)

/*
 * JumbleQuery: Selectively serialize the query tree, appending significant
 * data to the "query jumble" while ignoring nonsignificant data.
 *
 * Rule of thumb for what to include is that we should ignore anything not
 * semantically significant (such as alias names) as well as anything that can
 * be deduced from child nodes (else we'd just be double-hashing that piece
 * of information).
 */
void JumbleQuery(pgssJumbleState *jstate, Query *query)
{
	Assert(IsA(query, Query));
	Assert(query->utilityStmt == NULL);

	APP_JUMB(query->commandType);
	/* resultRelation is usually predictable from commandType */
	JumbleExpr(jstate, (Node *)query->cteList);
	JumbleRangeTable(jstate, query->rtable);
	JumbleExpr(jstate, (Node *)query->jointree);
	JumbleExpr(jstate, (Node *)query->targetList);
	JumbleExpr(jstate, (Node *)query->returningList);
	JumbleExpr(jstate, (Node *)query->groupClause);
	JumbleExpr(jstate, query->havingQual);
	JumbleExpr(jstate, (Node *)query->windowClause);
	JumbleExpr(jstate, (Node *)query->distinctClause);
	JumbleExpr(jstate, (Node *)query->sortClause);
	JumbleExpr(jstate, query->limitOffset);
	JumbleExpr(jstate, query->limitCount);
	/* we ignore rowMarks */
	JumbleExpr(jstate, query->setOperations);
}

/*
 * Jumble a range table
 */
static void
JumbleRangeTable(pgssJumbleState *jstate, List *rtable)
{
	ListCell *lc;

	foreach (lc, rtable)
	{
		RangeTblEntry *rte = (RangeTblEntry *)lfirst(lc);

		Assert(IsA(rte, RangeTblEntry));
		APP_JUMB(rte->rtekind);
		switch (rte->rtekind)
		{
		case RTE_RELATION:
			APP_JUMB(rte->relid);
			break;
		case RTE_SUBQUERY:
			JumbleQuery(jstate, rte->subquery);
			break;
		case RTE_JOIN:
			APP_JUMB(rte->jointype);
			break;
		case RTE_FUNCTION:
			JumbleExpr(jstate, (Node *)rte->functions);
			break;
		case RTE_VALUES:
			JumbleExpr(jstate, (Node *)rte->values_lists);
			break;
		case RTE_CTE:

			/*
			 * Depending on the CTE name here isn't ideal, but it's the
			 * only info we have to identify the referenced WITH item.
			 */
			APP_JUMB_STRING(rte->ctename);
			APP_JUMB(rte->ctelevelsup);
			break;
		/* GPDB RTEs */
		case RTE_VOID:
			break;
		case RTE_TABLEFUNCTION:
			JumbleQuery(jstate, rte->subquery);
			JumbleExpr(jstate, (Node *)rte->functions);
			break;
		default:
			ereport(ERROR, (errmsg("unrecognized RTE kind: %d", (int)rte->rtekind)));
			break;
		}
	}
}

/*
 * Jumble an expression tree
 *
 * In general this function should handle all the same node types that
 * expression_tree_walker() does, and therefore it's coded to be as parallel
 * to that function as possible.  However, since we are only invoked on
 * queries immediately post-parse-analysis, we need not handle node types
 * that only appear in planning.
 *
 * Note: the reason we don't simply use expression_tree_walker() is that the
 * point of that function is to support tree walkers that don't care about
 * most tree node types, but here we care about all types.  We should complain
 * about any unrecognized node type.
 */
static void
JumbleExpr(pgssJumbleState *jstate, Node *node)
{
	ListCell *temp;

	if (node == NULL)
		return;

	/* Guard against stack overflow due to overly complex expressions */
	check_stack_depth();

	/*
	 * We always emit the node's NodeTag, then any additional fields that are
	 * considered significant, and then we recurse to any child nodes.
	 */
	APP_JUMB(node->type);

	switch (nodeTag(node))
	{
	case T_Var:
	{
		Var *var = (Var *)node;

		APP_JUMB(var->varno);
		APP_JUMB(var->varattno);
		APP_JUMB(var->varlevelsup);
	}
	break;
	case T_Const:
	{
		Const *c = (Const *)node;

		/* We jumble only the constant's type, not its value */
		APP_JUMB(c->consttype);
		/* Also, record its parse location for query normalization */
		RecordConstLocation(jstate, c->location);
	}
	break;
	case T_Param:
	{
		Param *p = (Param *)node;

		APP_JUMB(p->paramkind);
		APP_JUMB(p->paramid);
		APP_JUMB(p->paramtype);
	}
	break;
	case T_Aggref:
	{
		Aggref *expr = (Aggref *)node;

		APP_JUMB(expr->aggfnoid);
		JumbleExpr(jstate, (Node *)expr->aggdirectargs);
		JumbleExpr(jstate, (Node *)expr->args);
		JumbleExpr(jstate, (Node *)expr->aggorder);
		JumbleExpr(jstate, (Node *)expr->aggdistinct);
		JumbleExpr(jstate, (Node *)expr->aggfilter);
	}
	break;
	case T_WindowFunc:
	{
		WindowFunc *expr = (WindowFunc *)node;

		APP_JUMB(expr->winfnoid);
		APP_JUMB(expr->winref);
		JumbleExpr(jstate, (Node *)expr->args);
		JumbleExpr(jstate, (Node *)expr->aggfilter);
	}
	break;
	case T_ArrayRef:
	{
		ArrayRef *aref = (ArrayRef *)node;

		JumbleExpr(jstate, (Node *)aref->refupperindexpr);
		JumbleExpr(jstate, (Node *)aref->reflowerindexpr);
		JumbleExpr(jstate, (Node *)aref->refexpr);
		JumbleExpr(jstate, (Node *)aref->refassgnexpr);
	}
	break;
	case T_FuncExpr:
	{
		FuncExpr *expr = (FuncExpr *)node;

		APP_JUMB(expr->funcid);
		JumbleExpr(jstate, (Node *)expr->args);
	}
	break;
	case T_NamedArgExpr:
	{
		NamedArgExpr *nae = (NamedArgExpr *)node;

		APP_JUMB(nae->argnumber);
		JumbleExpr(jstate, (Node *)nae->arg);
	}
	break;
	case T_OpExpr:
	case T_DistinctExpr: /* struct-equivalent to OpExpr */
	case T_NullIfExpr:	 /* struct-equivalent to OpExpr */
	{
		OpExpr *expr = (OpExpr *)node;

		APP_JUMB(expr->opno);
		JumbleExpr(jstate, (Node *)expr->args);
	}
	break;
	case T_ScalarArrayOpExpr:
	{
		ScalarArrayOpExpr *expr = (ScalarArrayOpExpr *)node;

		APP_JUMB(expr->opno);
		APP_JUMB(expr->useOr);
		JumbleExpr(jstate, (Node *)expr->args);
	}
	break;
	case T_BoolExpr:
	{
		BoolExpr *expr = (BoolExpr *)node;

		APP_JUMB(expr->boolop);
		JumbleExpr(jstate, (Node *)expr->args);
	}
	break;
	case T_SubLink:
	{
		SubLink *sublink = (SubLink *)node;

		APP_JUMB(sublink->subLinkType);
		JumbleExpr(jstate, (Node *)sublink->testexpr);
		JumbleQuery(jstate, (Query *)sublink->subselect);
	}
	break;
	case T_FieldSelect:
	{
		FieldSelect *fs = (FieldSelect *)node;

		APP_JUMB(fs->fieldnum);
		JumbleExpr(jstate, (Node *)fs->arg);
	}
	break;
	case T_FieldStore:
	{
		FieldStore *fstore = (FieldStore *)node;

		JumbleExpr(jstate, (Node *)fstore->arg);
		JumbleExpr(jstate, (Node *)fstore->newvals);
	}
	break;
	case T_RelabelType:
	{
		RelabelType *rt = (RelabelType *)node;

		APP_JUMB(rt->resulttype);
		JumbleExpr(jstate, (Node *)rt->arg);
	}
	break;
	case T_CoerceViaIO:
	{
		CoerceViaIO *cio = (CoerceViaIO *)node;

		APP_JUMB(cio->resulttype);
		JumbleExpr(jstate, (Node *)cio->arg);
	}
	break;
	case T_ArrayCoerceExpr:
	{
		ArrayCoerceExpr *acexpr = (ArrayCoerceExpr *)node;

		APP_JUMB(acexpr->resulttype);
		JumbleExpr(jstate, (Node *)acexpr->arg);
	}
	break;
	case T_ConvertRowtypeExpr:
	{
		ConvertRowtypeExpr *crexpr = (ConvertRowtypeExpr *)node;

		APP_JUMB(crexpr->resulttype);
		JumbleExpr(jstate, (Node *)crexpr->arg);
	}
	break;
	case T_CollateExpr:
	{
		CollateExpr *ce = (CollateExpr *)node;

		APP_JUMB(ce->collOid);
		JumbleExpr(jstate, (Node *)ce->arg);
	}
	break;
	case T_CaseExpr:
	{
		CaseExpr *caseexpr = (CaseExpr *)node;

		JumbleExpr(jstate, (Node *)caseexpr->arg);
		foreach (temp, caseexpr->args)
		{
			CaseWhen *when = (CaseWhen *)lfirst(temp);

			Assert(IsA(when, CaseWhen));
			JumbleExpr(jstate, (Node *)when->expr);
			JumbleExpr(jstate, (Node *)when->result);
		}
		JumbleExpr(jstate, (Node *)caseexpr->defresult);
	}
	break;
	case T_CaseTestExpr:
	{
		CaseTestExpr *ct = (CaseTestExpr *)node;

		APP_JUMB(ct->typeId);
	}
	break;
	case T_ArrayExpr:
		JumbleExpr(jstate, (Node *)((ArrayExpr *)node)->elements);
		break;
	case T_RowExpr:
		JumbleExpr(jstate, (Node *)((RowExpr *)node)->args);
		break;
	case T_RowCompareExpr:
	{
		RowCompareExpr *rcexpr = (RowCompareExpr *)node;

		APP_JUMB(rcexpr->rctype);
		JumbleExpr(jstate, (Node *)rcexpr->largs);
		JumbleExpr(jstate, (Node *)rcexpr->rargs);
	}
	break;
	case T_CoalesceExpr:
		JumbleExpr(jstate, (Node *)((CoalesceExpr *)node)->args);
		break;
	case T_MinMaxExpr:
	{
		MinMaxExpr *mmexpr = (MinMaxExpr *)node;

		APP_JUMB(mmexpr->op);
		JumbleExpr(jstate, (Node *)mmexpr->args);
	}
	break;
	case T_XmlExpr:
	{
		XmlExpr *xexpr = (XmlExpr *)node;

		APP_JUMB(xexpr->op);
		JumbleExpr(jstate, (Node *)xexpr->named_args);
		JumbleExpr(jstate, (Node *)xexpr->args);
	}
	break;
	case T_NullTest:
	{
		NullTest *nt = (NullTest *)node;

		APP_JUMB(nt->nulltesttype);
		JumbleExpr(jstate, (Node *)nt->arg);
	}
	break;
	case T_BooleanTest:
	{
		BooleanTest *bt = (BooleanTest *)node;

		APP_JUMB(bt->booltesttype);
		JumbleExpr(jstate, (Node *)bt->arg);
	}
	break;
	case T_CoerceToDomain:
	{
		CoerceToDomain *cd = (CoerceToDomain *)node;

		APP_JUMB(cd->resulttype);
		JumbleExpr(jstate, (Node *)cd->arg);
	}
	break;
	case T_CoerceToDomainValue:
	{
		CoerceToDomainValue *cdv = (CoerceToDomainValue *)node;

		APP_JUMB(cdv->typeId);
	}
	break;
	case T_SetToDefault:
	{
		SetToDefault *sd = (SetToDefault *)node;

		APP_JUMB(sd->typeId);
	}
	break;
	case T_CurrentOfExpr:
	{
		CurrentOfExpr *ce = (CurrentOfExpr *)node;

		APP_JUMB(ce->cvarno);
		if (ce->cursor_name)
			APP_JUMB_STRING(ce->cursor_name);
		APP_JUMB(ce->cursor_param);
	}
	break;
	case T_TargetEntry:
	{
		TargetEntry *tle = (TargetEntry *)node;

		APP_JUMB(tle->resno);
		APP_JUMB(tle->ressortgroupref);
		JumbleExpr(jstate, (Node *)tle->expr);
	}
	break;
	case T_RangeTblRef:
	{
		RangeTblRef *rtr = (RangeTblRef *)node;

		APP_JUMB(rtr->rtindex);
	}
	break;
	case T_JoinExpr:
	{
		JoinExpr *join = (JoinExpr *)node;

		APP_JUMB(join->jointype);
		APP_JUMB(join->isNatural);
		APP_JUMB(join->rtindex);
		JumbleExpr(jstate, join->larg);
		JumbleExpr(jstate, join->rarg);
		JumbleExpr(jstate, join->quals);
	}
	break;
	case T_FromExpr:
	{
		FromExpr *from = (FromExpr *)node;

		JumbleExpr(jstate, (Node *)from->fromlist);
		JumbleExpr(jstate, from->quals);
	}
	break;
	case T_List:
		foreach (temp, (List *)node)
		{
			JumbleExpr(jstate, (Node *)lfirst(temp));
		}
		break;
	case T_SortGroupClause:
	{
		SortGroupClause *sgc = (SortGroupClause *)node;

		APP_JUMB(sgc->tleSortGroupRef);
		APP_JUMB(sgc->eqop);
		APP_JUMB(sgc->sortop);
		APP_JUMB(sgc->nulls_first);
	}
	break;
	case T_WindowClause:
	{
		WindowClause *wc = (WindowClause *)node;

		APP_JUMB(wc->winref);
		APP_JUMB(wc->frameOptions);
		JumbleExpr(jstate, (Node *)wc->partitionClause);
		JumbleExpr(jstate, (Node *)wc->orderClause);
		JumbleExpr(jstate, wc->startOffset);
		JumbleExpr(jstate, wc->endOffset);
	}
	break;
	case T_CommonTableExpr:
	{
		CommonTableExpr *cte = (CommonTableExpr *)node;

		/* we store the string name because RTE_CTE RTEs need it */
		APP_JUMB_STRING(cte->ctename);
		JumbleQuery(jstate, (Query *)cte->ctequery);
	}
	break;
	case T_SetOperationStmt:
	{
		SetOperationStmt *setop = (SetOperationStmt *)node;

		APP_JUMB(setop->op);
		APP_JUMB(setop->all);
		JumbleExpr(jstate, setop->larg);
		JumbleExpr(jstate, setop->rarg);
	}
	break;
	case T_RangeTblFunction:
	{
		RangeTblFunction *rtfunc = (RangeTblFunction *)node;

		JumbleExpr(jstate, rtfunc->funcexpr);
	}
	break;
	/* GPDB nodes */
	case T_GroupingClause:
	{
		GroupingClause *grpnode = (GroupingClause *)node;

		JumbleExpr(jstate, (Node *)grpnode->groupsets);
	}
	break;
	case T_GroupingFunc:
	{
		GroupingFunc *grpnode = (GroupingFunc *)node;

		JumbleExpr(jstate, (Node *)grpnode->args);
	}
	break;
	case T_Grouping:
	case T_GroupId:
	case T_Integer:
	case T_Value:
		// TODO:seems like nothing to do with it
		break;
	/* GPDB-only additions, nothing to do */
	case T_PartitionBy:
	case T_PartitionElem:
	case T_PartitionRangeItem:
	case T_PartitionBoundSpec:
	case T_PartitionSpec:
	case T_PartitionValuesSpec:
	case T_AlterPartitionId:
	case T_AlterPartitionCmd:
	case T_InheritPartitionCmd:
	case T_CreateFileSpaceStmt:
	case T_FileSpaceEntry:
	case T_DropFileSpaceStmt:
	case T_TableValueExpr:
	case T_DenyLoginInterval:
	case T_DenyLoginPoint:
	case T_AlterTypeStmt:
	case T_SetDistributionCmd:
	case T_ExpandStmtSpec:
		break;
	default:
		/* Only a warning, since we can stumble along anyway */
		ereport(WARNING, (errmsg("unrecognized node type: %d",
			 (int)nodeTag(node))));
		break;
	}
}

/*
 * Record location of constant within query string of query tree
 * that is currently being walked.
 */
static void
RecordConstLocation(pgssJumbleState *jstate, int location)
{
	/* -1 indicates unknown or undefined location */
	if (location >= 0)
	{
		/* enlarge array if needed */
		if (jstate->clocations_count >= jstate->clocations_buf_size)
		{
			jstate->clocations_buf_size *= 2;
			jstate->clocations = (pgssLocationLen *)
				repalloc(jstate->clocations,
						 jstate->clocations_buf_size *
							 sizeof(pgssLocationLen));
		}
		jstate->clocations[jstate->clocations_count].location = location;
		/* initialize lengths to -1 to simplify fill_in_constant_lengths */
		jstate->clocations[jstate->clocations_count].length = -1;
		jstate->clocations_count++;
	}
}

/* check if token should be replaced by substitute varable */
static bool
need_replace(int token)
{
	return (token == FCONST) || (token == ICONST) || (token == SCONST) || (token == BCONST) || (token == XCONST);
}

/*
 * gen_normplan - parse execution plan using flex and replace all CONST to
 * substitute variables.
 */
StringInfo
gen_normplan(const char *execution_plan)
{
	core_yyscan_t yyscanner;
	core_yy_extra_type yyextra;
	core_YYSTYPE yylval;
	YYLTYPE yylloc;
	int tok;
	int bind_prefix = 1;
	char *tmp_str;
	YYLTYPE last_yylloc = 0;
	int last_tok = 0;
	StringInfo plan_out = makeStringInfo();
	;

	yyscanner = scanner_init(execution_plan,
							 &yyextra,
#if PG_VERSION_NUM >= 120000
							 &ScanKeywords,
							 ScanKeywordTokens
#else
							 ScanKeywords,
							 NumScanKeywords
#endif
	);

	for (;;)
	{
		/* get the next lexem */
		tok = core_yylex(&yylval, &yylloc, yyscanner);

		/* now we store end previsous lexem in yylloc - so could prcess it */
		if (need_replace(last_tok))
		{
			/* substitute variable instead of CONST */
			int s_len = asprintf(&tmp_str, "$%i", bind_prefix++);
			if (s_len > 0)
			{
				appendStringInfoString(plan_out, tmp_str);
				free(tmp_str);
			}
			else
			{
				appendStringInfoString(plan_out, "??");
			}
		}
		else
		{
			/* do not change - just copy as-is */
			tmp_str = strndup((char *)execution_plan + last_yylloc, yylloc - last_yylloc);
			appendStringInfoString(plan_out, tmp_str);
			free(tmp_str);
		}
		/* check if further parsing not needed */
		if (tok == 0)
			break;
		last_tok = tok;
		last_yylloc = yylloc;
	}

	scanner_finish(yyscanner);

	return plan_out;
}

/*
 * Post-parse-analysis hook: mark query with a queryId
 */
void pgss_post_parse_analyze(ParseState *pstate, Query *query)
{
	pgssJumbleState jstate;

	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook(pstate, query);

	/* Assert we didn't do this already */
	Assert(query->queryId == 0);

	/*
	 * Utility statements get queryId zero.  We do this even in cases where
	 * the statement contains an optimizable statement for which a queryId
	 * could be derived (such as EXPLAIN or DECLARE CURSOR).  For such cases,
	 * runtime control will first go through ProcessUtility and then the
	 * executor, and we don't want the executor hooks to do anything, since we
	 * are already measuring the statement's costs at the utility level.
	 */
	if (query->utilityStmt)
	{
		query->queryId = 0;
		return;
	}

	/* Set up workspace for query jumbling */
	jstate.jumble = (unsigned char *)palloc(JUMBLE_SIZE);
	jstate.jumble_len = 0;
	jstate.clocations_buf_size = 32;
	jstate.clocations = (pgssLocationLen *)
		palloc(jstate.clocations_buf_size * sizeof(pgssLocationLen));
	jstate.clocations_count = 0;

	/* Compute query ID and mark the Query node with it */
	JumbleQuery(&jstate, query);
	query->queryId = hash_any(jstate.jumble, jstate.jumble_len);

	/*
	 * If we are unlucky enough to get a hash of zero, use 1 instead, to
	 * prevent confusion with the utility-statement case.
	 */
	if (query->queryId == 0)
		query->queryId = 1;
}

/*
 * comp_location: comparator for qsorting pgssLocationLen structs by location
 */
static int
comp_location(const void *a, const void *b)
{
	int			l = ((const pgssLocationLen *) a)->location;
	int			r = ((const pgssLocationLen *) b)->location;

	if (l < r)
		return -1;
	else if (l > r)
		return +1;
	else
		return 0;
}

/*
 * Given a valid SQL string and an array of constant-location records,
 * fill in the textual lengths of those constants.
 *
 * The constants may use any allowed constant syntax, such as float literals,
 * bit-strings, single-quoted strings and dollar-quoted strings.  This is
 * accomplished by using the public API for the core scanner.
 *
 * It is the caller's job to ensure that the string is a valid SQL statement
 * with constants at the indicated locations.  Since in practice the string
 * has already been parsed, and the locations that the caller provides will
 * have originated from within the authoritative parser, this should not be
 * a problem.
 *
 * Duplicate constant pointers are possible, and will have their lengths
 * marked as '-1', so that they are later ignored.  (Actually, we assume the
 * lengths were initialized as -1 to start with, and don't change them here.)
 *
 * N.B. There is an assumption that a '-' character at a Const location begins
 * a negative numeric constant.  This precludes there ever being another
 * reason for a constant to start with a '-'.
 */
static void
fill_in_constant_lengths(pgssJumbleState *jstate, const char *query)
{
	pgssLocationLen *locs;
	core_yyscan_t yyscanner;
	core_yy_extra_type yyextra;
	core_YYSTYPE yylval;
	YYLTYPE		yylloc;
	int			last_loc = -1;
	int			i;

	/*
	 * Sort the records by location so that we can process them in order while
	 * scanning the query text.
	 */
	if (jstate->clocations_count > 1)
		qsort(jstate->clocations, jstate->clocations_count,
			  sizeof(pgssLocationLen), comp_location);
	locs = jstate->clocations;

	/* initialize the flex scanner --- should match raw_parser() */
	yyscanner = scanner_init(query,
							 &yyextra,
							 ScanKeywords,
							 NumScanKeywords);

	/* Search for each constant, in sequence */
	for (i = 0; i < jstate->clocations_count; i++)
	{
		int			loc = locs[i].location;
		int			tok;

		Assert(loc >= 0);

		if (loc <= last_loc)
			continue;			/* Duplicate constant, ignore */

		/* Lex tokens until we find the desired constant */
		for (;;)
		{
			tok = core_yylex(&yylval, &yylloc, yyscanner);

			/* We should not hit end-of-string, but if we do, behave sanely */
			if (tok == 0)
				break;			/* out of inner for-loop */

			/*
			 * We should find the token position exactly, but if we somehow
			 * run past it, work with that.
			 */
			if (yylloc >= loc)
			{
				if (query[loc] == '-')
				{
					/*
					 * It's a negative value - this is the one and only case
					 * where we replace more than a single token.
					 *
					 * Do not compensate for the core system's special-case
					 * adjustment of location to that of the leading '-'
					 * operator in the event of a negative constant.  It is
					 * also useful for our purposes to start from the minus
					 * symbol.  In this way, queries like "select * from foo
					 * where bar = 1" and "select * from foo where bar = -2"
					 * will have identical normalized query strings.
					 */
					tok = core_yylex(&yylval, &yylloc, yyscanner);
					if (tok == 0)
						break;	/* out of inner for-loop */
				}

				/*
				 * We now rely on the assumption that flex has placed a zero
				 * byte after the text of the current token in scanbuf.
				 */
				locs[i].length = strlen(yyextra.scanbuf + loc);
				break;			/* out of inner for-loop */
			}
		}

		/* If we hit end-of-string, give up, leaving remaining lengths -1 */
		if (tok == 0)
			break;

		last_loc = loc;
	}

	scanner_finish(yyscanner);
}

/*
 * Generate a normalized version of the query string that will be used to
 * represent all similar queries.
 *
 * Note that the normalized representation may well vary depending on
 * just which "equivalent" query is used to create the hashtable entry.
 * We assume this is OK.
 *
 * *query_len_p contains the input string length, and is updated with
 * the result string length (which cannot be longer) on exit.
 *
 * Returns a palloc'd string.
 */
static char *
generate_normalized_query(pgssJumbleState *jstate, const char *query,
						  int *query_len_p, int encoding)
{
	char	   *norm_query;
	int			query_len = *query_len_p;
	int			i,
				len_to_wrt,		/* Length (in bytes) to write */
				quer_loc = 0,	/* Source query byte location */
				n_quer_loc = 0, /* Normalized query byte location */
				last_off = 0,	/* Offset from start for previous tok */
				last_tok_len = 0;		/* Length (in bytes) of that tok */

	/*
	 * Get constants' lengths (core system only gives us locations).  Note
	 * this also ensures the items are sorted by location.
	 */
	fill_in_constant_lengths(jstate, query);

	/* Allocate result buffer */
	norm_query = palloc(query_len + 1);

	for (i = 0; i < jstate->clocations_count; i++)
	{
		int			off,		/* Offset from start for cur tok */
					tok_len;	/* Length (in bytes) of that tok */

		off = jstate->clocations[i].location;
		tok_len = jstate->clocations[i].length;

		if (tok_len < 0)
			continue;			/* ignore any duplicates */

		/* Copy next chunk (what precedes the next constant) */
		len_to_wrt = off - last_off;
		len_to_wrt -= last_tok_len;

		Assert(len_to_wrt >= 0);
		memcpy(norm_query + n_quer_loc, query + quer_loc, len_to_wrt);
		n_quer_loc += len_to_wrt;

		/* And insert a '?' in place of the constant token */
		norm_query[n_quer_loc++] = '?';

		quer_loc = off + tok_len;
		last_off = off;
		last_tok_len = tok_len;
	}

	/*
	 * We've copied up until the last ignorable constant.  Copy over the
	 * remaining bytes of the original query string.
	 */
	len_to_wrt = query_len - quer_loc;

	Assert(len_to_wrt >= 0);
	memcpy(norm_query + n_quer_loc, query + quer_loc, len_to_wrt);
	n_quer_loc += len_to_wrt;

	Assert(n_quer_loc <= query_len);
	norm_query[n_quer_loc] = '\0';

	*query_len_p = n_quer_loc;
	return norm_query;
}

char *gen_normquery(const char *query)
{
	if (!query) {
		return NULL;
	}
	pgssJumbleState jstate;
	jstate.jumble = (unsigned char *)palloc(JUMBLE_SIZE);
	jstate.jumble_len = 0;
	jstate.clocations_buf_size = 32;
	jstate.clocations = (pgssLocationLen *)
		palloc(jstate.clocations_buf_size * sizeof(pgssLocationLen));
	jstate.clocations_count = 0;
	int query_len = strlen(query);
	return generate_normalized_query(&jstate, query, &query_len, GetDatabaseEncoding());
}

/* prime numbers */

#define nNumbers 1001

static int16 primeNumbers[nNumbers] = {1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107,
	109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229,
	233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359,
	367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491,
	499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607, 613, 617, 619, 631, 641,
	643, 647, 653, 659, 661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739, 743, 751, 757, 761, 769, 773, 787,
	797, 809, 811, 821, 823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919, 929, 937, 941,
	947, 953, 967, 971, 977, 983, 991, 997, 1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049, 1051, 1061, 1063, 1069,
	1087, 1091, 1093, 1097, 1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163, 1171, 1181, 1187, 1193, 1201, 1213, 1217,
	1223, 1229, 1231, 1237, 1249, 1259, 1277, 1279, 1283, 1289, 1291, 1297, 1301, 1303, 1307, 1319, 1321, 1327, 1361,
	1367, 1373, 1381, 1399, 1409, 1423, 1427, 1429, 1433, 1439, 1447, 1451, 1453, 1459, 1471, 1481, 1483, 1487, 1489,
	1493, 1499, 1511, 1523, 1531, 1543, 1549, 1553, 1559, 1567, 1571, 1579, 1583, 1597, 1601, 1607, 1609, 1613, 1619,
	1621, 1627, 1637, 1657, 1663, 1667, 1669, 1693, 1697, 1699, 1709, 1721, 1723, 1733, 1741, 1747, 1753, 1759, 1777,
	1783, 1787, 1789, 1801, 1811, 1823, 1831, 1847, 1861, 1867, 1871, 1873, 1877, 1879, 1889, 1901, 1907, 1913, 1931,
	1933, 1949, 1951, 1973, 1979, 1987, 1993, 1997, 1999, 2003, 2011, 2017, 2027, 2029, 2039, 2053, 2063, 2069, 2081,
	2083, 2087, 2089, 2099, 2111, 2113, 2129, 2131, 2137, 2141, 2143, 2153, 2161, 2179, 2203, 2207, 2213, 2221, 2237,
	2239, 2243, 2251, 2267, 2269, 2273, 2281, 2287, 2293, 2297, 2309, 2311, 2333, 2339, 2341, 2347, 2351, 2357, 2371,
	2377, 2381, 2383, 2389, 2393, 2399, 2411, 2417, 2423, 2437, 2441, 2447, 2459, 2467, 2473, 2477, 2503, 2521, 2531,
	2539, 2543, 2549, 2551, 2557, 2579, 2591, 2593, 2609, 2617, 2621, 2633, 2647, 2657, 2659, 2663, 2671, 2677, 2683,
	2687, 2689, 2693, 2699, 2707, 2711, 2713, 2719, 2729, 2731, 2741, 2749, 2753, 2767, 2777, 2789, 2791, 2797, 2801,
	2803, 2819, 2833, 2837, 2843, 2851, 2857, 2861, 2879, 2887, 2897, 2903, 2909, 2917, 2927, 2939, 2953, 2957, 2963,
	2969, 2971, 2999, 3001, 3011, 3019, 3023, 3037, 3041, 3049, 3061, 3067, 3079, 3083, 3089, 3109, 3119, 3121, 3137,
	3163, 3167, 3169, 3181, 3187, 3191, 3203, 3209, 3217, 3221, 3229, 3251, 3253, 3257, 3259, 3271, 3299, 3301, 3307,
	3313, 3319, 3323, 3329, 3331, 3343, 3347, 3359, 3361, 3371, 3373, 3389, 3391, 3407, 3413, 3433, 3449, 3457, 3461,
	3463, 3467, 3469, 3491, 3499, 3511, 3517, 3527, 3529, 3533, 3539, 3541, 3547, 3557, 3559, 3571, 3581, 3583, 3593,
	3607, 3613, 3617, 3623, 3631, 3637, 3643, 3659, 3671, 3673, 3677, 3691, 3697, 3701, 3709, 3719, 3727, 3733, 3739,
	3761, 3767, 3769, 3779, 3793, 3797, 3803, 3821, 3823, 3833, 3847, 3851, 3853, 3863, 3877, 3881, 3889, 3907, 3911,
	3917, 3919, 3923, 3929, 3931, 3943, 3947, 3967, 3989, 4001, 4003, 4007, 4013, 4019, 4021, 4027, 4049, 4051, 4057,
	4073, 4079, 4091, 4093, 4099, 4111, 4127, 4129, 4133, 4139, 4153, 4157, 4159, 4177, 4201, 4211, 4217, 4219, 4229,
	4231, 4241, 4243, 4253, 4259, 4261, 4271, 4273, 4283, 4289, 4297, 4327, 4337, 4339, 4349, 4357, 4363, 4373, 4391,
	4397, 4409, 4421, 4423, 4441, 4447, 4451, 4457, 4463, 4481, 4483, 4493, 4507, 4513, 4517, 4519, 4523, 4547, 4549,
	4561, 4567, 4583, 4591, 4597, 4603, 4621, 4637, 4639, 4643, 4649, 4651, 4657, 4663, 4673, 4679, 4691, 4703, 4721,
	4723, 4729, 4733, 4751, 4759, 4783, 4787, 4789, 4793, 4799, 4801, 4813, 4817, 4831, 4861, 4871, 4877, 4889, 4903,
	4909, 4919, 4931, 4933, 4937, 4943, 4951, 4957, 4967, 4969, 4973, 4987, 4993, 4999, 5003, 5009, 5011, 5021, 5023,
	5039, 5051, 5059, 5077, 5081, 5087, 5099, 5101, 5107, 5113, 5119, 5147, 5153, 5167, 5171, 5179, 5189, 5197, 5209,
	5227, 5231, 5233, 5237, 5261, 5273, 5279, 5281, 5297, 5303, 5309, 5323, 5333, 5347, 5351, 5381, 5387, 5393, 5399,
	5407, 5413, 5417, 5419, 5431, 5437, 5441, 5443, 5449, 5471, 5477, 5479, 5483, 5501, 5503, 5507, 5519, 5521, 5527,
	5531, 5557, 5563, 5569, 5573, 5581, 5591, 5623, 5639, 5641, 5647, 5651, 5653, 5657, 5659, 5669, 5683, 5689, 5693,
	5701, 5711, 5717, 5737, 5741, 5743, 5749, 5779, 5783, 5791, 5801, 5807, 5813, 5821, 5827, 5839, 5843, 5849, 5851,
	5857, 5861, 5867, 5869, 5879, 5881, 5897, 5903, 5923, 5927, 5939, 5953, 5981, 5987, 6007, 6011, 6029, 6037, 6043,
	6047, 6053, 6067, 6073, 6079, 6089, 6091, 6101, 6113, 6121, 6131, 6133, 6143, 6151, 6163, 6173, 6197, 6199, 6203,
	6211, 6217, 6221, 6229, 6247, 6257, 6263, 6269, 6271, 6277, 6287, 6299, 6301, 6311, 6317, 6323, 6329, 6337, 6343,
	6353, 6359, 6361, 6367, 6373, 6379, 6389, 6397, 6421, 6427, 6449, 6451, 6469, 6473, 6481, 6491, 6521, 6529, 6547,
	6551, 6553, 6563, 6569, 6571, 6577, 6581, 6599, 6607, 6619, 6637, 6653, 6659, 6661, 6673, 6679, 6689, 6691, 6701,
	6703, 6709, 6719, 6733, 6737, 6761, 6763, 6779, 6781, 6791, 6793, 6803, 6823, 6827, 6829, 6833, 6841, 6857, 6863,
	6869, 6871, 6883, 6899, 6907, 6911, 6917, 6947, 6949, 6959, 6961, 6967, 6971, 6977, 6983, 6991, 6997, 7001, 7013,
	7019, 7027, 7039, 7043, 7057, 7069, 7079, 7103, 7109, 7121, 7127, 7129, 7151, 7159, 7177, 7187, 7193, 7207, 7211,
	7213, 7219, 7229, 7237, 7243, 7247, 7253, 7283, 7297, 7307, 7309, 7321, 7331, 7333, 7349, 7351, 7369, 7393, 7411,
	7417, 7433, 7451, 7457, 7459, 7477, 7481, 7487, 7489, 7499, 7507, 7517, 7523, 7529, 7537, 7541, 7547, 7549, 7559,
	7561, 7573, 7577, 7583, 7589, 7591, 7603, 7607, 7621, 7639, 7643, 7649, 7669, 7673, 7681, 7687, 7691, 7699, 7703,
	7717, 7723, 7727, 7741, 7753, 7757, 7759, 7789, 7793, 7817, 7823, 7829, 7841, 7853, 7867, 7873, 7877, 7879, 7883,
	7901, 7907, 7919};

static uint64 genIdOneNode(PlanState *planstate, ExplainState *es, int level);

static uint64 hashUInt64(uint64 id, int level) {
	int multiplier = level % nNumbers;
	return id * primeNumbers[multiplier];
}

static uint64 hashString(char *str) {
	if (str == NULL)
		return 0;
	return (uint64) hash_any((const unsigned char *) str, strlen(str));
}

static uint64 hashJoinType(Join *join, int level) {
	if (join == NULL)
		return 0;
	return hashUInt64(join->jointype, level);
}

static uint64 hashIndex(IndexScan *index, int level) {
	if (index == NULL)
		return 0;
	return hashUInt64(index->indexid, level);
}

/*
 * Show the target relation of a scan or modify node
 */
static uint64
genIdTargetRel(Plan *plan, Index rti, ExplainState *es)
{
	if (plan == NULL)
		return 0;

        RangeTblEntry *rte;
        uint64      id = 0;

        rte = rt_fetch(rti, es->rtable);

		switch (nodeTag(plan))
        {
                case T_SeqScan:
                case T_DynamicSeqScan:
                case T_IndexScan:
                case T_DynamicIndexScan:
                case T_IndexOnlyScan:
                case T_BitmapHeapScan:
                case T_DynamicBitmapHeapScan:
                case T_TidScan:
                case T_ForeignScan:
                case T_ModifyTable:
                case T_ExternalScan:
                        /* Assert it's on a real relation */
                        Assert(rte->rtekind == RTE_RELATION);
                        id += hashUInt64(rte->relid, 2);
                        id += hashUInt64(get_rel_namespace(rte->relid), 2);
                        break;
                case T_FunctionScan:
                        {
                                FunctionScan *fscan = (FunctionScan *) plan;

                                /* Assert it's on a RangeFunction */
                                Assert(rte->rtekind == RTE_FUNCTION);
                                if (list_length(fscan->functions) == 1)
                                {
                                        RangeTblFunction *rtfunc = (RangeTblFunction *) linitial(fscan->functions);

                                        if (IsA(rtfunc->funcexpr, FuncExpr))
                                        {
                                                FuncExpr   *funcexpr = (FuncExpr *) rtfunc->funcexpr;
                                                Oid                     funcid = funcexpr->funcid;

                                                id += hashUInt64(funcid, 2);
                                                id += hashUInt64(get_func_namespace(funcid), 2);
                                        }
                                }
                        }
                        break;
                case T_TableFunctionScan:
                        {
                                TableFunctionScan *fscan = (TableFunctionScan *) plan;

                                /* Assert it's on a RangeFunction */
                                Assert(rte->rtekind == RTE_TABLEFUNCTION);

                                /*
                                 * Unlike in a FunctionScan, in a TableFunctionScan the call
                                 * should always be a function call of a single function.
                                 * Get the real name of the function.
                                 */
                                {
                                        RangeTblFunction *rtfunc = fscan->function;

                                        if (IsA(rtfunc->funcexpr, FuncExpr))
                                        {
                                                FuncExpr   *funcexpr = (FuncExpr *) rtfunc->funcexpr;
                                                Oid                     funcid = funcexpr->funcid;

                                                id += hashUInt64(funcid, 2);
                                                id += hashUInt64(get_func_namespace(funcid), 2);
                                        }
                                }
                        }
                        break;
                case T_ValuesScan:
                        Assert(rte->rtekind == RTE_VALUES);
                        break;
                case T_CteScan:
                        /* Assert it's on a non-self-reference CTE */
                        Assert(rte->rtekind == RTE_CTE);
                        id += hashString(rte->ctename);
                        break;
                case T_WorkTableScan:
                        /* Assert it's on a self-reference CTE */
                        Assert(rte->rtekind == RTE_CTE);
                        id += hashString(rte->ctename);
                        break;
                default:
                        break;
        }

        return hashUInt64(id, 0);
}

static uint64
genIdScanTarget(Scan *plan, ExplainState *es)
{
	if (plan == NULL)
		return 0;
        return genIdTargetRel((Plan *) plan, plan->scanrelid, es);
}

/*
 * Show the target of a ModifyTable node
 */
static uint64
genIdModifyTarget(ModifyTable *plan, ExplainState *es)
{
	if (plan == NULL)
		return 0;
        Index           rti;

        /*
         * We show the name of the first target relation.  In multi-target-table
         * cases this should always be the parent of the inheritance tree.
         */
        Assert(plan->resultRelations != NIL);
        rti = linitial_int(plan->resultRelations);

        return genIdTargetRel((Plan *) plan, rti, es);
}

static uint64
genIdMemberNodes(List *plans, PlanState **planstates, ExplainState *es, int level)
{
	if (planstates == NULL)
		return 0;
        int                     nplans = list_length(plans);
        int                     j;
        uint64                  planId = 0;

        for (j = 0; j < nplans; j++)
                planId += genIdOneNode(planstates[j], es, level);

        return planId;
}

static uint64
genIdSubPlans(List *plans, ExplainState *es, SliceTable *sliceTable, int level)
{
        ListCell   *lst;
        Slice      *saved_slice = es->currentSlice;
        uint64      planId = 0;

        foreach(lst, plans)
        {
                SubPlanState *sps = (SubPlanState *) lfirst(lst);
                SubPlan    *sp = (SubPlan *) sps->xprstate.expr;

                /* Subplan might have its own root slice */
                if (sliceTable && sp->qDispSliceId > 0)
                {
                        es->currentSlice = (Slice *)list_nth(sliceTable->slices,
                                                                                                 sp->qDispSliceId);
                }

                planId += genIdOneNode(sps->planstate, es, level);
        }

        es->currentSlice = saved_slice;

        return planId;
}

static uint64
genIdDispatchInfo(Slice *slice) {
	if (slice == NULL)
		return 1;
	return hashUInt64(slice->gangType, 2);
}

static uint64
genIdIndexScanDetails(Oid indexid, ScanDirection indexorderdir,
                                                ExplainState *es)
{
        uint64 id = hashUInt64(indexid, 2);
        id += hashUInt64(indexorderdir, 1);
        return id;
}

static uint64 genIdOneNode(PlanState *planstate, ExplainState *es, int level)
{
	if (planstate == NULL)
		return 0;
	Plan	   *plan = planstate->plan;
	uint64     planId = 0;

	planId += hashUInt64(nodeTag(plan), 0);

	switch (nodeTag(plan))
	{
		case T_SeqScan:
		case T_DynamicSeqScan:
		case T_ExternalScan:
		case T_BitmapHeapScan:
		case T_DynamicBitmapHeapScan:
		case T_TidScan:
		case T_SubqueryScan:
		case T_FunctionScan:
		case T_TableFunctionScan:
		case T_ValuesScan:
		case T_CteScan:
		case T_ForeignScan:
		case T_WorkTableScan:
			{
				planId += genIdScanTarget((Scan *) plan, es);
			}
			break;
		case T_IndexScan:
		case T_IndexOnlyScan:
			{
				planId += hashIndex((IndexScan *) plan, 2);
				IndexScan  *indexscan = (IndexScan *) plan;
				planId += genIdIndexScanDetails(indexscan->indexid,
										indexscan->indexorderdir,
										es);
				planId += genIdScanTarget((Scan *) indexscan, es);
			}
			break;
		case T_BitmapIndexScan:
		case T_DynamicBitmapIndexScan:
			planId += hashIndex((IndexScan *) plan, 2);
			break;
		case T_DynamicIndexScan:
			{
				planId += genIdScanTarget((Scan *) plan, es);
				planId += hashIndex((IndexScan *) plan, 2);
			}
			break;
		case T_ModifyTable:
			{
				planId += hashUInt64(((ModifyTable *) plan)->operation, 2);
				planId += genIdModifyTarget((ModifyTable *) plan, es);
			}
			break;
		case T_PartitionSelector:
			planId += hashUInt64(((PartitionSelector *)plan)->relid, 2);
			break;
		case T_NestLoop:
			{
				planId += hashJoinType((Join *) plan, 2);
				if (((NestLoop *)plan)->shared_outer)
				{
					planId += hashUInt64(1, 2);
				}
			}
			break;
		case T_MergeJoin:
		case T_HashJoin:
			planId += hashJoinType((Join *) plan, 2);
			break;
		case T_Agg:
			planId += hashUInt64(((Agg *) plan)->aggstrategy, 2);
			break;
		case T_SetOp:
			{
				planId += hashUInt64(((SetOp *) plan)->strategy, 2);
				planId += hashUInt64(((SetOp *) plan)->cmd, 2);
			}
			break;
		case T_Motion:
			{
				Motion		*pMotion = (Motion *) plan;
				planId += hashUInt64(pMotion->motionType, 2);
				switch (pMotion->motionType)
				{
					case MOTIONTYPE_FIXED:
						if (pMotion->isBroadcast)
						{
							planId += hashUInt64(1, 3);
						}
						else if (plan->lefttree->flow->locustype == CdbLocusType_Replicated)
						{
							planId += hashUInt64(2, 3);
						}
						else
						{
							planId += hashUInt64(3, 3);
						}
						break;
					default:
						break;
				}

			}
			break;
		default:
			break;
	}

	planId += genIdDispatchInfo(es->currentSlice);

	/* initPlan-s */
	if (plan->initPlan)
		planId += genIdSubPlans(planstate->initPlan, es, planstate->state->es_sliceTable, level + 1);

	/* lefttree */
	if (outerPlan(plan))
	{
		planId += genIdOneNode(outerPlanState(planstate), es, level + 1);
	}

		/* righttree */
	if (innerPlanState(planstate))
		planId += genIdOneNode(innerPlanState(planstate), es, level + 1);

        /* special child plans */
        switch (nodeTag(plan))
        {
                case T_ModifyTable:
                        planId += genIdMemberNodes(((ModifyTable *) plan)->plans,
                                                           ((ModifyTableState *) planstate)->mt_plans,
                                                        	es, level + 1);
                        break;
                case T_Append:
                        planId += genIdMemberNodes(((Append *) plan)->appendplans,
                                                           ((AppendState *) planstate)->appendplans,
                                                        	es, level + 1);
                        break;
                case T_MergeAppend:
                        planId += genIdMemberNodes(((MergeAppend *) plan)->mergeplans,
                                                           ((MergeAppendState *) planstate)->mergeplans,
                                                           es, level + 1);
                        break;
                case T_Sequence:
                        planId += genIdMemberNodes(((Sequence *) plan)->subplans,
                                                           ((SequenceState *) planstate)->subplans,
                                                           es, level + 1);
                        break;
                case T_BitmapAnd:
                        planId += genIdMemberNodes(((BitmapAnd *) plan)->bitmapplans,
                                                           ((BitmapAndState *) planstate)->bitmapplans,
                                                           es, level + 1);
                        break;
                case T_BitmapOr:
                        planId += genIdMemberNodes(((BitmapOr *) plan)->bitmapplans,
                                                           ((BitmapOrState *) planstate)->bitmapplans,
                                                           es, level + 1);
                        break;
                case T_SubqueryScan:
                        planId += genIdOneNode(((SubqueryScanState *) planstate)->subplan, es, level + 1);
                        break;
                default:
                        break;
        }

	if (planstate->subPlan)
		planId += genIdSubPlans(planstate->subPlan, es, NULL, level + 1);

	return hashUInt64(planId, level);

}

uint64 gen_plan_id(QueryDesc *queryDesc)
{
	if (queryDesc == NULL)
		return 0;
	Assert(queryDesc->plannedstmt != NULL);
	ExplainState es;
	uint64 id;
        MemoryContext oldcxt = MemoryContextSwitchTo(queryDesc->estate->es_query_cxt);
        ExplainInitState(&es);
	es.rtable = queryDesc->plannedstmt->rtable;
	id = genIdOneNode(queryDesc->planstate, &es, 9);
	pfree(es.str->data);
        MemoryContextSwitchTo(oldcxt);
	return id;
}
