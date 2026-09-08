#ifndef CBLUEPRINT_EVAL_H
#define CBLUEPRINT_EVAL_H

#include "graph/model/graph.h"
#include "graph/model/pin.h"
#include "graph/runtime/service.h"

/* Runtime state belongs to one pull-eval context or flow activation. */
typedef struct runtime_store runtime_store;

/* Deprecated compatibility symbol. A graph cannot own runtime state now;
 * reset the owning eval_context with eval_context_reset_runtime(). */
void graph_eval_reset(graph *g);

typedef struct
{
    int code;
    const node *node;
    int pin;
    const char *message;
} eval_status;

typedef struct behavior_instance behavior_instance;

typedef void (*eval_data_edge_fn)(
    const node *source,
    int source_pin,
    const node *target,
    int target_pin,
    void *user
);

typedef struct eval_context
{
    eval_status status;
    behavior_instance *instance; /* optional: variable/owner reads for Get/Self */
    const service_binding *services; /* borrowed activation service snapshot */
    eval_data_edge_fn on_data_edge; /* optional pull-evaluation edge observer */
    void *on_data_edge_user;
    runtime_store *runtime; /* borrowed when attached by a flow run */
    int runtime_owned;
    unsigned call_depth;
    double delta_seconds;
} eval_context;

void eval_context_init(eval_context *ctx);
void eval_context_free(eval_context *ctx);
void eval_context_reset(eval_context *ctx);
/* Reset one context's memo, diagnostics, and runtime values. */
void eval_context_reset_runtime(eval_context *ctx);
void eval_context_attach_runtime(eval_context *ctx, runtime_store *runtime);
void eval_context_detach_runtime(eval_context *ctx);
const eval_status *eval_context_status(const eval_context *ctx);
void *eval_context_service(const eval_context *ctx, const char *type_name);
int eval_context_input(const eval_context *ctx, const node *n, int pin, pin_value *out);
int eval_context_output(const eval_context *ctx, const node *n, int pin, pin_value *out);

/* Pull-eval one output pin of n; writes the value to *out. Returns 0 on
 * success, -1 on a bad pin index, -2 on a cycle, -3 on allocation failure,
 * and -4 on invalid graph data. Status is stored on the supplied context. */
int graph_eval_output(graph *g, node *n, int out_pin, arena *a, pin_value *out, eval_context *ctx);

#endif
