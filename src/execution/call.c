#include "execution/definition.h"
#include "execution/runner.h"
#include "execution/service.h"
#include "execution/status.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/trace.h"

#include <stdlib.h>

#define CALL_CACHE_BYTES (64 * 1024)

typedef struct
{
    const behavior_function *function;
    behavior_function_invocation invocation;
    behavior_function_context context;
    behavior_function_activation activation;
    behavior_data_storage *data;
    arena storage;
    bool entered;
} invocation_frame;

struct behavior_function_calls
{
    /* Frames have stable addresses. A growing pointer array never invalidates
     * context.parent or arguments retained by a pending host operation. */
    invocation_frame **frames;
    /* Only a completed frame may be reused. It is never a live value root. */
    invocation_frame *spare;
    int result;
};

static void frame_leave(invocation_frame *frame, int result)
{
    if (frame->entered && frame->context.call_leave) {
        frame->context.call_leave(&frame->context, result);
    }
    frame->entered = false;
    behavior_function_release_host(&frame->context);
}

static void frame_free(invocation_frame *frame, int result)
{
    frame_leave(frame, result);
    behavior_function_free_data(frame->data);
    behavior_function_activation_free(&frame->activation);
    arena_free(&frame->storage);
    free(frame);
}

static bool cacheable(const invocation_frame *frame)
{
    const behavior_function *f = frame->function;
    const behavior_function_activation *a = &frame->activation;
    size_t remaining = CALL_CACHE_BYTES;
#define KEEP(bytes)                                                            \
    do {                                                                       \
        size_t size = (bytes);                                                 \
        if (size > remaining) {                                                \
            return false;                                                      \
        }                                                                      \
        remaining -= size;                                                     \
    } while (0)
#define VECTOR(values)                                                         \
    KEEP((values) ? sizeof(dyna_hdr) + (size_t)acap(values) * sizeof *(values) : 0)
    KEEP(sizeof *frame);
    KEEP(arena_storage_bytes(&frame->storage));
    size_t slots = (size_t)acount(f->slots) + acount(f->locals);
    if (a->values) {
        KEEP((slots + acount(f->frames)) * sizeof *a->values);
    }
    if (a->wake_times) {
        KEEP((size_t)acount(f->frames) * sizeof *a->wake_times);
    }
    if (a->vm) {
        KEEP(sizeof *a->vm);
        KEEP((slots + 1) * sizeof *a->vm->values);
        if (a->vm->parents) {
            KEEP((size_t)acount(f->frames) * sizeof *a->vm->parents);
        }
    }
    if (frame->data) {
        KEEP(sizeof *frame->data);
        VECTOR(frame->data->values);
        VECTOR(frame->data->buffers);
        VECTOR(frame->data->calls);
        for (int i = 0; i < acount(f->data_values); i++) {
            KEEP(f->data_values[i].size);
        }
    }
#undef VECTOR
#undef KEEP
    return true;
}

static void frame_retire(struct behavior_function_calls *calls, invocation_frame *frame)
{
    frame_leave(frame, FLOW_OK);
    /* VM scratch follows the selected body, which inspection may change. */
    if (frame->function->fast) {
        behavior_function_activation_free(&frame->activation);
    }
    if (!cacheable(frame)) {
        frame_free(frame, FLOW_OK);
        return;
    }
    if (calls->spare) {
        frame_free(calls->spare, FLOW_OK);
    }
    arena_reset(&frame->storage);
    frame->context = (behavior_function_context){ 0 };
    calls->spare = frame;
}

void behavior_function_calls_free(struct behavior_function_calls *calls)
{
    if (!calls) {
        return;
    }
    if (calls->spare) {
        frame_free(calls->spare, FLOW_OK);
    }
    for (int i = acount(calls->frames) - 1; i >= 0; i--) {
        frame_free(calls->frames[i], FLOW_ERR_STATE);
    }
    afree(calls->frames);
    free(calls);
}

void behavior_function_clear_calls(behavior_function_activation *activation)
{
    behavior_function_calls_free(activation->calls);
    activation->calls = NULL;
}

/* A failed stack never resumes. Unwinding it at once detaches its frames'
 * pending requests and continuations, whose host banks the failure released. */
static int settle_calls(struct behavior_function_calls *calls, int result)
{
    calls->result = result;
    if (result >= 0) {
        return result;
    }
    for (int i = acount(calls->frames) - 1; i >= 0; i--) {
        frame_free(calls->frames[i], result);
    }
    asetlen(calls->frames, 0);
    return result;
}

void behavior_function_visit_contexts(
    behavior_function_context *ctx,
    void (*visit)(behavior_function_context *)
)
{
    visit(ctx);
    struct behavior_function_calls *calls = ctx->activation ? ctx->activation->calls
                                                            : NULL;
    if (!calls) {
        return;
    }
    for (int i = 0; i < acount(calls->frames); i++) {
        behavior_function_visit_contexts(&calls->frames[i]->context, visit);
    }
}

behavior_function_context *behavior_function_active_context(
    behavior_function_context *context,
    const behavior_function **function
)
{
    struct behavior_function_calls *calls = context->activation
                                              ? context->activation->calls
                                              : NULL;
    if (!calls || !acount(calls->frames)) {
        return context;
    }
    invocation_frame *frame = calls->frames[acount(calls->frames) - 1];
    *function = frame->function;
    return &frame->context;
}

static int call_error(
    const behavior_function *function,
    behavior_function_context *context,
    behavior_function_failure failure,
    const char *message
)
{
    const behavior_function_instruction
        *i = &function->instructions[context->activation->pc - 1];
    context->failure = failure;
    context->error_message = message;
    context->node_id = i->node_id;
    context->error_frame = i->frame;
    context->error_source = i->source;
    context->error_stack = i->stack;
    context->error_pin = -1;
    context->error_steps = context->activation->steps;
    int status = failure == BEHAVIOR_FUNCTION_ARGUMENT ? FLOW_ERR_ARGUMENT
                                                       : FLOW_ERR_INVALID_GRAPH;
    if (context->failure_observer) {
        context->failure_observer(function, context, status);
    }
    return status;
}

static const behavior_function_invocation *invocation(
    const behavior_function *function,
    const behavior_function_context *parent
)
{
    const behavior_function_instruction
        *i = &function->instructions[parent->activation->pc - 1];
    assert(i->op == BF_INVOKE && i->left >= 0 && i->left < acount(function->calls));
    return &function->calls[i->left];
}
static void reset_activation(invocation_frame *frame)
{
    const behavior_function *f = frame->function;
    behavior_function_activation *a = &frame->activation;
    assert(!a->calls);
    int slots = acount(f->slots) + acount(f->locals);
    if (a->values) {
        memset(a->values, 0, (size_t)(slots + acount(f->frames)) * sizeof *a->values);
    }
    if (a->wake_times) {
        memset(a->wake_times, 0, (size_t)acount(f->frames) * sizeof *a->wake_times);
    }
    if (a->vm) {
        memset(a->vm->values, 0, (size_t)(slots + 1) * sizeof *a->vm->values);
        if (a->vm->parents) {
            memset(a->vm->parents, 0, (size_t)acount(f->frames) * sizeof *a->vm->parents);
        }
    }
    *a = (behavior_function_activation){ .values = a->values,
                                         .wake_times = a->wake_times,
                                         .vm = a->vm };
}

static invocation_frame *frame_new(
    struct behavior_function_calls *calls,
    const behavior_function *function,
    behavior_function_context *parent
)
{
    const behavior_function_instruction
        *i = &function->instructions[parent->activation->pc - 1];
    behavior_function_invocation call = *invocation(function, parent);
    assert(call.function && parent->data && parent->value_storage);
    unsigned depth = parent->call_depth +
                     (unsigned)function->frames[i->frame].depth + 1;
    if (call.function->depth_limit && depth >= call.function->depth_limit) {
        call_error(function, parent, BEHAVIOR_FUNCTION_DEPTH, "function recursion depth exceeded");
        return NULL;
    }
    invocation_frame *frame = calls->spare;
    calls->spare = NULL;
    if (!frame) {
        frame = xcalloc(1, sizeof *frame);
        arena_init(&frame->storage, 4096);
    }
    if (frame->function != call.function) {
        behavior_function_activation_free(&frame->activation);
        behavior_function_free_data(frame->data);
        frame->data = NULL;
    } else {
        reset_activation(frame);
        behavior_function_reset_data(frame->data);
    }
    frame->function = call.function;
    frame->invocation = call;
    behavior_function_prepare_data(call.function, parent->services, &frame->data);
    frame->context = (behavior_function_context){
        .parent = parent,
        .call_enter = parent->call_enter,
        .call_leave = parent->call_leave,
        .activation = &frame->activation,
        .data = frame->data,
        .budget = parent->budget,
        .call_depth = depth,
        .inspection_user = parent->inspection_user,
        .source_debug_user = parent->source_debug_user,
        .source_pause = parent->source_pause,
        .source_capture = parent->source_capture,
        .begin_command = parent->begin_command,
        .start_task = parent->start_task,
        .release_local = parent->release_local,
        .action_result = parent->action_result,
        .event_argument = parent->event_argument,
        .complete_action = parent->complete_action,
        .state_inputs = parent->state_inputs,
        .state_transition = parent->state_transition,
        .retrigger_action = parent->retrigger_action,
        .request_action_break = parent->request_action_break,
        .take_action_break = parent->take_action_break,
        .store_component = parent->store_component,
        .component_membership = parent->component_membership,
        .failure_observer = parent->failure_observer,
        .bind_instance = parent->bind_instance,
        .commit_instance = parent->commit_instance
    };
    pin_value *arguments = arena_alloc(&frame->storage, (size_t)(call.count + 1) * sizeof *arguments);
    for (int p = 0; p < call.count; p++) {
        if (pin_value_snapshot_to_arena(
                &frame->storage,
                &arguments[p],
                &parent->data->values[call.first + p]
            )) {
            frame_free(frame, FLOW_ERR_ARGUMENT);
            call_error(function, parent, BEHAVIOR_FUNCTION_ARGUMENT, "function argument cannot be retained");
            return NULL;
        }
    }
    frame->context.arguments = arguments;
    frame->context.argument_count = call.count;
    frame->context.results = arena_alloc(
        &frame->storage,
        (size_t)(call.result_count + 1) * sizeof(pin_value)
    );
    for (int p = 0; p < call.result_count; p++) {
        frame->context.results[p] = parent->data->values[call.result_first + p];
    }
    if (behavior_function_resumable(call.function) && !frame->activation.values) {
        behavior_function_activation_init(call.function, &frame->activation);
    }
    arena *payload = parent->value_storage;
    behavior_function_bind_storage(
        &frame->context,
        payload->collectable ? payload : &frame->storage
    );
    behavior_function_prepare_results(
        call.function,
        frame->context.results,
        &frame->storage
    );
    size_t size = behavior_function_watch_size(call.function);
    if (size) {
        frame->context.watch_storage = arena_alloc(&frame->storage, size);
    }
    return frame;
}

static void refresh(invocation_frame *frame, const behavior_function_context *root)
{
    behavior_function_context *context = &frame->context;
    context->program_name = root->program_name;
    context->now = root->now;
    context->delta_seconds = root->delta_seconds;
    context->instance_time = root->instance_time;
    context->variables = root->variables;
    context->states = root->states;
    context->select_state = root->select_state;
    context->retrigger = root->retrigger;
    context->variable_storage = root->variable_storage;
    context->value_owner = root->value_owner;
    context->store_value = root->store_value;
    context->add_set_value = root->add_set_value;
    context->own_type_name = root->own_type_name;
    context->inspect = root->inspect;
    context->source_debug_user = root->source_debug_user;
    context->source_pause = root->source_pause;
    context->source_capture = root->source_capture;
    context->watch = root->watch;
    context->trace = root->trace;
    context->owner_type_name = root->owner_type_name;
    context->world = root->world;
    context->binding_world = root->binding_world;
    bool bound = behavior_function_bind(frame->function, context, root->services, root->owner);
    assert(bound);
    (void)bound;
    if (context->bind_instance) {
        context->bind_instance(frame->function, context);
    }
}

static int return_values(invocation_frame *frame, const behavior_function *parent_function)
{
    behavior_function_context *context = &frame->context, *parent = context->parent;
    behavior_function_invocation call = frame->invocation;
    /* A function without results may end anywhere, as a void function does;
       one with results must reach Return to produce them. */
    if (!context->returned) {
        return call.result_count
                 ? call_error(parent_function, parent, BEHAVIOR_FUNCTION_NO_RETURN, "function completed without Return")
                 : FLOW_OK;
    }
    parent->returned_outcome = context->returned_outcome;
    for (int p = 0; p < call.result_count; p++) {
        if (pin_value_snapshot_to_arena(
                parent->value_storage,
                &parent->data->values[call.result_first + p],
                &context->results[p]
            )) {
            return call_error(parent_function, parent, BEHAVIOR_FUNCTION_DATA, "function result cannot be retained");
        }
    }
    return FLOW_OK;
}

int behavior_function_run_calls(
    const behavior_function *function,
    behavior_function_context *root,
    bool compiled
)
{
    assert(root->activation && root->activation->values);
    struct behavior_function_calls *calls = root->activation->calls;
    if (!calls) {
        calls = root->activation->calls = xcalloc(1, sizeof *calls);
    }
    if (calls->result < 0) {
        return calls->result;
    }
    for (;;) {
        int count = acount(calls->frames);
        const behavior_function *active = function;
        behavior_function_context *context = root;
        if (count) {
            invocation_frame *frame = calls->frames[count - 1];
            active = frame->function;
            context = &frame->context;
            refresh(frame, root);
        }
        int result = behavior_function_run_once(active, context, compiled);
        if (context->commit_instance) {
            context->commit_instance(context);
        }
        if (result == BEHAVIOR_FUNCTION_CALL_RESULT) {
            const behavior_function_invocation *call = invocation(active, context);
            if (call->task) {
                result = context->start_task(context, (int)(call - active->calls));
                if (!result) {
                    continue;
                }
                root->wake_time = context->wake_time;
                return settle_calls(calls, result);
            }
            invocation_frame *frame = frame_new(calls, active, context);
            if (!frame) {
                return settle_calls(
                    calls,
                    context->failure == BEHAVIOR_FUNCTION_DEPTH ? FLOW_ERR_INVALID_GRAPH
                                                                : FLOW_ERR_ARGUMENT
                );
            }
            apush(calls->frames, frame);
            frame->entered = true;
            if (frame->context.call_enter) {
                frame->context.call_enter(frame->function, &frame->context);
            }
            continue;
        }
        if (!result && count) {
            const behavior_function *parent = count > 1
                                                ? calls->frames[count - 2]->function
                                                : function;
            result = return_values(calls->frames[count - 1], parent);
            if (result) {
                frame_free(calls->frames[count - 1], result);
            } else {
                frame_retire(calls, calls->frames[count - 1]);
            }
            asetlen(calls->frames, count - 1);
            if (!result) {
                continue;
            }
            context = count > 1 ? &calls->frames[count - 2]->context : root;
        }
        root->wake_time = context->wake_time;
        return settle_calls(calls, result);
    }
}

static void trace_value(const pin_value *value, behavior_value_kind kind, arena_marker mark, void *user)
{
    pin_value_trace(value, mark, user);
    if (kind != BEHAVIOR_VALUE) {
        mark(user, (const void *)(uintptr_t)behavior_scalar_bits(*value));
    }
}
static void trace_values(
    const pin_value *values,
    const behavior_value_kind *kinds,
    int count,
    arena_marker mark,
    void *user
)
{
    for (int i = 0; i < count; i++) {
        trace_value(&values[i], kinds ? kinds[i] : BEHAVIOR_VALUE, mark, user);
    }
}
static void trace_context(
    const behavior_function *f,
    const behavior_function_context *ctx,
    arena_marker mark,
    void *user
)
{
    const behavior_function_activation *a = ctx->activation;
    if (a && a->values && a->pc > 0) {
        const behavior_function_suspension
            *s = &f->suspensions[f->instructions[a->pc - 1].target];
        int slots = acount(f->slots);
        for (int i = 0; i < acount(s->values); i++) {
            int value = s->values[i];
            pin_type type = value < slots ? f->slots[value]
                                          : f->locals[value - slots].initial.type;
            behavior_value_kind kind = value < slots ? f->slot_kinds[value]
                                                     : f->locals[value - slots].kind;
            if (kind != BEHAVIOR_VALUE || type == PIN_STR || type == PIN_NAME) {
                mark(user, (const void *)(uintptr_t)a->values[value]);
            }
        }
    }
    trace_values(ctx->arguments, f->argument_kinds, ctx->argument_count, mark, user);
    trace_values(ctx->results, f->result_kinds, f->result_count, mark, user);
    trace_values(ctx->variables, NULL, f->variable_count, mark, user);
    trace_values(ctx->command_arguments, f->command_argument_kinds, acount(f->command_arguments), mark, user);
    trace_values(ctx->command_results, f->command_result_kinds, acount(f->command_results), mark, user);
    if (!ctx->data) {
        return;
    }
    for (int i = 0; i < acount(f->data_values); i++) {
        trace_value(&ctx->data->values[i], f->data_values[i].kind, mark, user);
        if (f->data_values[i].size) {
            pin_value value = f->data_values[i].initial;
            value.ptr = ctx->data->buffers[i];
            pin_value_trace(&value, mark, user);
        }
    }
}
void behavior_function_trace_values(
    const behavior_function *f,
    const behavior_function_context *ctx,
    arena_marker mark,
    void *user
)
{
    trace_context(f, ctx, mark, user);
    const struct behavior_function_calls *calls = ctx->activation
                                                    ? ctx->activation->calls
                                                    : NULL;
    for (int i = 0; calls && i < acount(calls->frames); i++) {
        trace_context(calls->frames[i]->function, &calls->frames[i]->context, mark, user);
    }
}
typedef struct
{
    const arena *storage;
    bool retained;
} storage_roots;
static void retain_address(void *user, const void *address)
{
    storage_roots *roots = user;
    roots->retained |= arena_contains(roots->storage, address);
}
static bool context_retains(
    const behavior_function *f,
    const behavior_function_context *ctx,
    const arena *storage
)
{
    storage_roots roots = { .storage = storage };
    trace_context(f, ctx, retain_address, &roots);
    return roots.retained;
}
bool behavior_function_retains_storage(
    const behavior_function *f,
    const behavior_function_context *ctx,
    const arena *storage
)
{
    if (context_retains(f, ctx, storage)) {
        return true;
    }
    const struct behavior_function_calls *calls = ctx->activation
                                                    ? ctx->activation->calls
                                                    : NULL;
    if (!calls) {
        return false;
    }
    for (int i = 0; i < acount(calls->frames); i++) {
        const invocation_frame *frame = calls->frames[i];
        if (context_retains(frame->function, &frame->context, storage)) {
            return true;
        }
    }
    return false;
}

static void rebind_context(
    const behavior_function *f,
    behavior_function_context *ctx,
    const service_binding *services
)
{
    ctx->world = NULL;
    ctx->binding_world = NULL;
    bool bound = behavior_function_bind(f, ctx, services, ctx->owner);
    assert(bound);
    (void)bound;
    behavior_function_rebind_host(f, ctx, services);
}
void behavior_function_rebind_services(
    const behavior_function *f,
    behavior_function_context *ctx,
    const service_binding *services
)
{
    rebind_context(f, ctx, services);
    struct behavior_function_calls *calls = ctx->activation ? ctx->activation->calls
                                                            : NULL;
    if (!calls) {
        return;
    }
    for (int i = 0; i < acount(calls->frames); i++) {
        invocation_frame *frame = calls->frames[i];
        frame->context.program_name = ctx->program_name;
        rebind_context(frame->function, &frame->context, services);
    }
}
