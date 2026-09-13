#ifndef CHIBI_BEHAVIOR_PROGRAM_H
#define CHIBI_BEHAVIOR_PROGRAM_H

#include "graph/compiler/behavior_function.h"
#include "graph/compiler/compile.h"
#include "graph/compiler/graph_ir.h"
#include "graph/runtime/eval.h"

typedef enum
{
    BEHAVIOR_UNAVAILABLE,
    BEHAVIOR_SLJIT,
    BEHAVIOR_BYTECODE,
    BEHAVIOR_ORACLE,
} behavior_backend;

typedef struct behavior_program behavior_program;
typedef struct behavior_graph_program behavior_graph_program;

typedef struct
{
    const behavior_graph_program *program;
    eval_context *eval;
    arena *arena;
    pin_value *inputs;
    pin_value *outputs;
} behavior_data_context;

/* One immutable linked asset generation owns the program. Instances guard the
 * asset for their lifetime; publication happens before parallel dispatch. */
uint64_t behavior_asset_revision(const behavior_asset *asset);
behavior_program *behavior_program_publish(behavior_asset *asset);
void behavior_program_free(behavior_program *program);
behavior_backend behavior_program_backend(const behavior_program *program);
uint64_t behavior_program_revision(const behavior_program *program);
/* Publication order is also the fixed state-slot order in compiled bodies. */
int behavior_asset_state_index(const behavior_asset *asset, uint64_t node_id);
const uint64_t *behavior_program_states(const behavior_program *program, int *count);
const char *behavior_backend_name(behavior_backend backend);
const compile_diagnostic *behavior_program_diagnostic(const behavior_program *program);
/* Named root event lookup. A duplicate returns the second entry for diagnostics. */
node *behavior_program_event(const behavior_program *program, const char *name, bool *duplicate);
/* Publication indexes only unique, eligible synchronous events. Sets entry on success. */
const behavior_function *behavior_program_batch_event(
    const behavior_program *program,
    const char *name,
    node **entry
);
const behavior_graph_program *behavior_program_graph(const behavior_program *program, const graph *g);
const graph_ir *behavior_graph_ir(const behavior_graph_program *program);

/* True when this input has a helper-free native numeric region. */
bool behavior_program_input_is_direct(const behavior_graph_program *program, const node *n, int pin);

int behavior_program_input(
    const behavior_graph_program *program,
    node *n,
    int pin,
    arena *a,
    pin_value *out,
    eval_context *ctx,
    behavior_backend backend
);
int behavior_program_output(
    const behavior_graph_program *program,
    node *n,
    int pin,
    arena *a,
    pin_value *out,
    eval_context *ctx,
    behavior_backend backend
);
int behavior_program_flow(
    const behavior_graph_program *program,
    node *n,
    flow_context *ctx,
    behavior_backend backend
);
const behavior_function *behavior_program_function(
    const behavior_graph_program *program,
    const node *entry
);

/* An optimization exclusion, separate from publication errors. */
const behavior_function_diagnostic *behavior_program_function_diagnostic(
    const behavior_graph_program *program,
    const node *entry
);

int behavior_program_block(const behavior_graph_program *program, const node *n);
node *behavior_program_block_node(const behavior_graph_program *program, int block);
int behavior_program_target(
    const behavior_graph_program *program,
    const node *n,
    int output,
    node **target,
    int *input
);

#endif
