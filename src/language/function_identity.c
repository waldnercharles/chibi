#include "language/function_identity.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint64_t script_function_identity(const script_analysis *analysis, int index)
{
    assert(analysis && index >= 0 && index < analysis->function_count);
    const script_function *function = &analysis->functions[index];
    if (function->symbol >= 0) {
        return analysis->symbols[function->symbol].id;
    }
    size_t capacity = strlen(analysis->identity) + 64;
    char *identity = xmalloc(capacity);
    snprintf(
        identity,
        capacity,
        "%s:%d:%d",
        analysis->identity,
        function->range.start,
        function->kind
    );
    uint64_t id = script_catalog_id("function", identity);
    free(identity);
    return id;
}
