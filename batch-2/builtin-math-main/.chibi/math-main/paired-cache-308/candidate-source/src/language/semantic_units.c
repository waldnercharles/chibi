#include "language/semantic_internal.h"
#include "language/unit_analysis.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>

script_type script_analyzer_signature_type(analyzer *a, TSNode node, char **unit_text)
{
    return script_analyzer_read_type(a, node, true, unit_text);
}
void script_analyzer_signature_unit(analyzer *a, TSNode node, const char *expected, const char *actual)
{
    script_unit x = script_unit_parse(expected), y = script_unit_parse(actual);
    if (x.kind == SCRIPT_UNIT_KNOWN && y.kind == SCRIPT_UNIT_KNOWN &&
        !script_unit_same(x, y)) {
        char left[SCRIPT_UNIT_TEXT], right[SCRIPT_UNIT_TEXT];
        script_unit_format(x, left, sizeof left);
        script_unit_format(y, right, sizeof right);
        script_analyzer_diagnostic(a, node, "Units differ: %s annotation %s", left, right);
    }
}
static int by_position(const void *a, const void *b)
{
    const script_diagnostic *x = a, *y = b;
    if (x->range.document != y->range.document) {
        return (x->range.document > y->range.document) -
               (x->range.document < y->range.document);
    }
    return (x->range.start > y->range.start) - (x->range.start < y->range.start);
}
void script_analyzer_units(analyzer *a)
{
    script_analysis *out = a->out;
    script_unit_analysis_facts facts = {
        .catalog = a->catalog,
        .symbols = out->symbols,
        .symbol_count = out->symbol_count,
        .expressions = out->expressions,
        .expression_count = acount(out->expressions),
        .functions = out->functions,
        .function_count = acount(out->functions),
        .statements = out->statements,
        .statement_count = acount(out->statements),
        .bindings = out->bindings,
        .binding_count = acount(out->bindings),
    };
    arena storage = { 0 };
    script_unit_analysis_result result = script_unit_analysis(&storage, &facts);
    for (int i = 0; i < result.diagnostic_count; i++) {
        apush(out->diagnostics, result.diagnostics[i]);
    }
    arena_free(&storage);
    if (acount(out->diagnostics) > 1) {
        qsort(out->diagnostics, (size_t)acount(out->diagnostics), sizeof *out->diagnostics, by_position);
    }
}
