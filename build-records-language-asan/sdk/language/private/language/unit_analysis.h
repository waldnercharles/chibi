#ifndef CHIBI_LANGUAGE_UNIT_ANALYSIS_H
#define CHIBI_LANGUAGE_UNIT_ANALYSIS_H

#include "language/semantic.h"
#include "language/unit_algebra.h"

typedef struct
{
    const script_catalog *catalog;
    const script_symbol *symbols;
    int symbol_count;
    const script_expression *expressions;
    int expression_count;
    const script_function *functions;
    int function_count;
    const script_statement *statements;
    int statement_count;
    const script_binding *bindings;
    int binding_count;
} script_unit_analysis_facts;
typedef struct
{
    const script_diagnostic *diagnostics;
    int diagnostic_count;
    const script_unit *units;
    int unit_count;
} script_unit_analysis_result;

/* Raw resolved IR/catalog rows obey analyzer index/span invariants. No lookup,
 * numeric classification, annotation parsing or unit decisions are supplied by
 * the adapter. Output owns added diagnostics and expression units in storage,
 * including every term name. Existing diagnostics and final positional sorting
 * belong to the caller. Equal-position diagnostic order has no new guarantee. */
script_unit_analysis_result script_unit_analysis(arena *, const script_unit_analysis_facts *);

#endif
