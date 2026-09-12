#ifndef CHIBI_BEHAVIOR_FUNCTION_H
#define CHIBI_BEHAVIOR_FUNCTION_H
#include "graph/model/behavior_asset.h"

#include <stdio.h>

typedef struct behavior_function behavior_function;
typedef struct
{
    graph *graph;
    node *call;
    int parent;
} behavior_function_source;
typedef enum
{
    BEHAVIOR_FUNCTION_BUDGET,
    BEHAVIOR_FUNCTION_DEPTH,
    BEHAVIOR_FUNCTION_NO_RETURN,
} behavior_function_failure;
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
    /* The runtime owns distinct variable, argument and result storage. */
    pin_value *variables;
    const pin_value *arguments;
    int argument_count;
    /* Return values use the canonical Return node's input pin indices. */
    pin_value *results;
    uint64_t returned_node_id;
    double delta_seconds;
    uintptr_t steps;
    uintptr_t budget;
    uint64_t node_id;
    unsigned call_depth;
    int error_frame;
    uintptr_t error_steps;
    behavior_function_failure failure;
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
const behavior_function_source *behavior_function_frame_source(
    const behavior_function *function,
    int frame
);
#endif
