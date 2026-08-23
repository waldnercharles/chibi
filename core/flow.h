#ifndef CBLUEPRINT_FLOW_H
#define CBLUEPRINT_FLOW_H

#include "graph.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct flow_run flow_run;
    typedef struct blueprint_instance blueprint_instance;

    typedef struct
    {
        const pin_value *values; /* copied by flow_run_start; caller retains ownership */
        int count;
        const void *user_data; /* borrowed by the run; caller keeps it alive until free */
    } flow_inputs;

    typedef double (*flow_clock_fn)(void *user);
    typedef void (*flow_sink_fn)(const node *origin, const char *message, void *user);
    typedef void (*flow_parallel_for_fn)(int count, void (*worker)(int index, void *user), void *user);

    typedef struct
    {
        const graph *graph;
        uint64_t graph_id;
        uint64_t node_id;
    } flow_call_frame;

    typedef struct
    {
        uint64_t from_id;
        uint64_t from_pin_id;
        uint64_t to_id;
        uint64_t to_pin_id;
        graph_edge_kind kind;
        double timestamp;
        unsigned long activation;
    } flow_traversal;

    typedef struct
    {
        unsigned step_budget; /* zero selects the 10,000-step default. */
        flow_clock_fn now; /* host monotonic clock; NULL keeps the last supplied time */
        void *now_user; /* borrowed by the run */
        double initial_time;
        flow_sink_fn sink; /* optional host callback for side-effecting nodes */
        void *sink_user;   /* borrowed by the run */
        blueprint_instance *instance; /* owning instance, or NULL for a standalone run */
        flow_parallel_for_fn parallel_for; /* host-supplied, never owned */
        void *parallel_user;
        unsigned call_depth; /* internal function-call depth, zero at an entry */
    } flow_options;

    typedef enum
    {
        FLOW_IDLE = 0,
        FLOW_RUNNING,
        FLOW_SUSPENDED, /* a latent node is waiting for host time */
        FLOW_PAUSED,    /* the host/user stopped between activations */
        FLOW_COMPLETED,
        FLOW_FAILED,
    } flow_state;

    enum
    {
        FLOW_OK = 0,
        FLOW_PAUSED_RESULT = 1,
        FLOW_SUSPENDED_RESULT = 2,
        FLOW_ERR_ARGUMENT = -100,
        FLOW_ERR_UNKNOWN_EVENT = -101,
        FLOW_ERR_DUPLICATE_EVENT = -102,
        FLOW_ERR_INVALID_NODE = -103,
        FLOW_ERR_STEP_BUDGET = -104,
        FLOW_ERR_TOPOLOGY = -105,
        FLOW_ERR_INVALID_GRAPH = -106,
        FLOW_ERR_NODE = -107,
        FLOW_ERR_STATE = -108,
        FLOW_ERR_COMPILE = -109, /* RUN-12: refused to start, compile errors */
    };

    typedef struct
    {
        int code;
        const node *node;
        int pin;
        const char *message;
        unsigned long steps;
    } flow_error;

    /* Host-selected entry point. The run owns a copy of inputs and remains
     * valid until flow_run_free(). `error` receives failures that happen
     * before a run exists and may be NULL. */
    flow_run *flow_run_start(
        graph *g,
        node *entry,
        flow_inputs inputs,
        const flow_options *options,
        flow_error *error
    );
    flow_run *flow_run_start_reuse(
        flow_run *run,
        graph *g,
        node *entry,
        flow_inputs inputs,
        const flow_options *options,
        flow_error *error
    );
    int flow_run_recycle(flow_run *run);

    /* Public named dispatch. NULL means unknown or duplicate event; `error`
     * receives the structured dispatch error and may be NULL. */
    flow_run *graph_run_event(graph *g, const char *name, flow_inputs inputs, flow_error *error);

    /* Continue a run. tick() samples the configured clock; tick_at() lets a
     * host provide an already sampled monotonic value. */
    int flow_run_resume(flow_run *run);
    int flow_run_tick(flow_run *run);
    int flow_run_tick_at(flow_run *run, double monotonic_time);

    /* Request an external pause between activations, or enter persistent
     * single-step mode. Step mode survives latent suspension until resume. */
    int flow_run_pause(flow_run *run);
    int flow_run_step(flow_run *run);
    int flow_run_step_over(flow_run *run);

    /* Stop discards pending continuations and leaves the run idle. Reset also
     * clears graph-side evaluation state and releases run-owned storage. */
    int flow_run_stop(flow_run *run);
    int flow_run_reset(flow_run *run);

    flow_state flow_run_status(const flow_run *run);
    const flow_error *flow_run_error(const flow_run *run);
    unsigned long flow_run_steps(const flow_run *run);
    flow_inputs flow_run_inputs(const flow_run *run);
    const pin_value *flow_run_input_value(const flow_run *run, const node *n, int pin);
    /* Traversal records are ordered oldest to newest and remain available
     * after Stop until the run is reset or freed. The returned storage is
     * borrowed from the run. */
    const flow_traversal *flow_run_traversals(const flow_run *run, int *count);
    const node *flow_run_current_node(const flow_run *run);
    const flow_call_frame *flow_run_call_stack(const flow_run *run, int *count);
    unsigned flow_run_call_depth(const flow_run *run);
    unsigned flow_context_call_depth(const struct flow_context *ctx);
    int flow_context_input_pin(const struct flow_context *ctx);
    pin_value *flow_context_input_value(struct flow_context *ctx, int pin);
    pin_value *flow_context_output_value(struct flow_context *ctx, int pin);
    void *flow_context_service(const struct flow_context *ctx, const char *type_name);
    void flow_run_free(flow_run *run);

    /* Scheduler readiness (EXEC-17): RUNNING, or SUSPENDED with wake_time
     * already reached at `now`. Completed, failed, idle, and paused runs are
     * not ready. */
    int flow_run_ready(const flow_run *run, double now);

    /* The run records the latest DATA value for every activated input/output.
     * Phase 1 keys this table by node pointer and pin index. Phase 2 replaces
     * the pointer key with stable node/pin IDs. Scalar and string values are
     * returned directly; struct pointers remain valid until run reset/free. */
    int flow_run_watch_input(const flow_run *run, const node *n, int pin, pin_value *out);
    int flow_run_watch_output(const flow_run *run, const node *n, int pin, pin_value *out);

    /* Copy a recorded output into caller-owned storage. This is the safe path
     * when a struct watch must outlive the flow_run and its scratch arena. */
    int flow_run_watch_copy_output(
        const flow_run *run,
        const node *n,
        int pin,
        pin_value *out,
        void *struct_storage,
        size_t struct_storage_size
    );

    /* Helpers available to hand-authored node_desc.flow callbacks. */
    struct flow_context;
    double flow_context_now(const struct flow_context *ctx);
    int flow_context_eval_data(struct flow_context *ctx, int input_pin, pin_value *out);
    int flow_context_fail(struct flow_context *ctx, int code, const char *message);
    int flow_context_pause_until(struct flow_context *ctx, double monotonic_time);
    int flow_context_continue(struct flow_context *ctx, int output_pin);
    int flow_context_loop(struct flow_context *ctx, int first, int last);
    int flow_context_loop_range(
        struct flow_context *ctx,
        int first,
        int last,
        int body_output,
        int completed_output,
        int index_output
    );
    int flow_context_break(struct flow_context *ctx);
    int flow_context_foreach(
        struct flow_context *ctx,
        const pin_value *array,
        int body_output,
        int value_output,
        int index_output,
        int completed_output
    );
    int flow_context_emit(struct flow_context *ctx, const char *message);
    flow_inputs flow_context_inputs(const struct flow_context *ctx);
    blueprint_instance *flow_context_instance(const struct flow_context *ctx);
    /* Retain a completed nested run until this activation is reset/freed. */
    int flow_context_retain_run(struct flow_context *ctx, flow_run *child);
    /* Allocate from the run-owned arena (DATA-6). */
    void *flow_context_scratch_alloc(struct flow_context *ctx, size_t size);

#ifdef __cplusplus
}
#endif

#endif
