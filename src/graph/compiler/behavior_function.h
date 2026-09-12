#ifndef CHIBI_BEHAVIOR_FUNCTION_H
#define CHIBI_BEHAVIOR_FUNCTION_H
#include "graph/model/behavior_asset.h"

#include <stdio.h>

typedef struct behavior_function behavior_function;
typedef struct
{
    const graph *graph;
    const node *node;
    int pin;
    bool output;
    const char *reason;
} behavior_function_diagnostic;
typedef struct
{
    pin_value *variables;
    const pin_value *arguments;
    int argument_count;
    double delta_seconds;
    unsigned long steps;
    unsigned long budget;
    uint64_t node_id;
} behavior_function_context;
/* Complete synchronous scalar entry, or NULL if an operation requires the
 * general compiled runtime. No partial effects occur during eligibility checks. */
behavior_function *behavior_function_lower(
    const behavior_asset *asset,
    graph *g,
    node *entry,
    behavior_function_diagnostic *diagnostic
);
void behavior_function_free(behavior_function *function);
bool behavior_function_compile(behavior_function *function);
int behavior_function_run(const behavior_function *function, behavior_function_context *ctx, bool native);
/* The exported C function has the same ABI as the SLJIT entry. */
void behavior_function_write_c(const behavior_function *function, FILE *out, const char *name);
void *behavior_function_entry(const behavior_function *function);
#endif
