#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

extern void stat_statements_parser_init(void);
extern void stat_statements_parser_deinit(void);

#ifdef __cplusplus
}
#endif

uint64 gen_plan_id(QueryDesc *queryDesc);
StringInfo gen_normplan(const char *executionPlan);
char *gen_normquery(const char *query);
