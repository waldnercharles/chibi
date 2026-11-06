#ifndef CHIBI_LANGUAGE_EFFECT_ANALYSIS_H
#define CHIBI_LANGUAGE_EFFECT_ANALYSIS_H
#include "language/semantic.h"
typedef struct
{
    const script_catalog *catalog;
    const script_expression *expressions;
    int expression_count;
    const script_statement *statements;
    int statement_count;
    const script_function *functions;
    int function_count;
    const script_symbol *symbols;
    int symbol_count;
    const script_scope *scopes;
    int scope_count;
    const script_query *queries;
    int query_count;
    const script_binding *bindings;
    int binding_count;
    /* Ordered stages and ownership distinguish row locals from invocation reads. */
    const script_query_description *descriptions;
    int description_count;
} script_effect_facts;
typedef struct
{
    const unsigned *expression_effects;
    int expression_count;
    const unsigned *statement_effects;
    int statement_count;
    const unsigned *function_effects;
    int function_count;
    const script_diagnostic *diagnostics;
    int diagnostic_count;
} script_effect_result;
typedef struct
{
    const int *permutation;
    int count;
    const script_diagnostic *diagnostics;
    int diagnostic_count;
} script_binding_order;
/* Resolved indices and counted spans obey analyzer invariants. Outputs own all
 * storage in the supplied arena. Existing diagnostics belong to the caller.
 * A cycle returns the identity permutation and the original first range. */
script_effect_result script_effect_analyze(arena *, const script_effect_facts *);
script_binding_order script_effect_order(arena *, const script_effect_facts *);
#endif
