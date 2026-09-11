#ifndef CHIBI_BEHAVIOR_PROGRAM_H
#define CHIBI_BEHAVIOR_PROGRAM_H

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
const char *behavior_backend_name(behavior_backend backend);
const compile_diagnostic *behavior_program_diagnostic(const behavior_program *program);
const behavior_graph_program *behavior_program_graph(const behavior_program *program, const graph *g);
const graph_ir *behavior_graph_ir(const behavior_graph_program *program);

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
