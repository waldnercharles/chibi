#include "execution/borrow.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/managed.h"
#include "execution/native.h"
#include "execution/runner.h"
#include "execution/service.h"
#include "execution/status.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/trace.h"

#include <stdlib.h>

/* Spare frames a settled stack keeps for its next run, and the larger set a
 * running stack keeps so its hot callees reuse their frames. */
#define CALL_CACHE_BYTES (64 * 1024)
#define CALL_RUN_CACHE_BYTES (8 * 1024 * 1024)

typedef struct invocation_frame
{
    const behavior_function *function;
    behavior_function_invocation invocation;
    behavior_function_context context;
    behavior_function_activation activation;
    behavior_data_storage *data;
    arena storage;
    bool entered;
    /* What a spare frame retains, measured when it retired. */
    size_t bytes;
    struct invocation_frame *next;
} invocation_frame;

struct behavior_function_calls
{
    /* Frames have stable addresses. A growing pointer array never invalidates
     * context.parent or arguments retained by a pending host operation. */
    invocation_frame **frames;
    /* Completed frames, matched by immutable function generation, from most to
     * least recently retired. Their whole retained allocation set,
     * `spare_bytes`, is bounded and never forms a live value root. */
    invocation_frame *spare;
    size_t spare_bytes;
    int result;
};

static void frame_leave(invocation_frame *frame, int result)
{
    if (frame->entered && frame->context.call_leave) {
        frame->context.call_leave(&frame->context, result);
    }
    frame->entered = false;
}

static void frame_free(invocation_frame *frame, int result)
{
    frame_leave(frame, result);
    behavior_function_release_host(&frame->context);
    behavior_function_free_data(frame->data);
    behavior_function_activation_free(&frame->activation);
    arena_free(&frame->storage);
    free(frame);
}

static size_t frame_bytes(const invocation_frame *frame)
{
    return sizeof *frame + arena_storage_bytes(&frame->storage) +
           behavior_function_host_storage_bytes(&frame->context) +
           behavior_function_activation_storage_bytes(frame->function, &frame->activation);
}
size_t behavior_function_calls_storage_bytes(const struct behavior_function_calls *calls)
{
    if (!calls) {
        return 0;
    }
    size_t bytes = sizeof *calls;
    if (calls->frames) {
        bytes += sizeof(dyna_hdr) + (size_t)acap(calls->frames) * sizeof *calls->frames;
    }
    for (int i = 0; i < acount(calls->frames); i++) {
        bytes += frame_bytes(calls->frames[i]);
    }
    return bytes + calls->spare_bytes;
}
void behavior_function_calls_reset(struct behavior_function_calls *calls)
{
    if (calls) {
        assert(!acount(calls->frames) && calls->result == FLOW_OK);
        /* A deeper live call can grow this vector after older spare frames
         * were retained. Trim against their combined idle allocation size. */
        if (behavior_function_calls_storage_bytes(calls) > CALL_CACHE_BYTES) {
            afree(calls->frames);
        } else if (calls->frames) {
            memset(calls->frames, 0, (size_t)acap(calls->frames) * sizeof *calls->frames);
        }
        calls->result = FLOW_OK;
    }
}

/* The stack's own allocation: its header and frame vector. */
static size_t calls_fixed_bytes(const struct behavior_function_calls *calls)
{
    return sizeof *calls + sizeof(dyna_hdr) +
           (size_t)acap(calls->frames) * sizeof *calls->frames;
}
/* Keeps the most recently retired spares that fit `budget` with the stack
 * and frees the rest, in one pass over the list. */
static void trim_spares(struct behavior_function_calls *calls, size_t budget)
{
    size_t used = calls_fixed_bytes(calls);
    invocation_frame **kept = &calls->spare;
    while (*kept && used + (*kept)->bytes <= budget) {
        used += (*kept)->bytes;
        kept = &(*kept)->next;
    }
    while (*kept) {
        invocation_frame *frame = *kept;
        *kept = frame->next;
        calls->spare_bytes -= frame->bytes;
        frame_free(frame, FLOW_OK);
    }
}

static void frame_retire(struct behavior_function_calls *calls, invocation_frame *frame)
{
    frame_leave(frame, FLOW_OK);
    behavior_function_reset_host(&frame->context);
    behavior_function_reset_activation(frame->function, &frame->activation);
    arena_reset(&frame->storage);
    frame->invocation = (behavior_function_invocation){ 0 };
    /* Live frames are an execution requirement, not an idle-retention budget.
     * A frame that cannot fit beside the stack's vector is freed at once. A
     * full list drops to half the budget, so trims stay rare. */
    frame->bytes = frame_bytes(frame);
    if (calls_fixed_bytes(calls) + frame->bytes > CALL_RUN_CACHE_BYTES) {
        frame_free(frame, FLOW_OK);
        return;
    }
    frame->next = calls->spare;
    calls->spare = frame;
    calls->spare_bytes += frame->bytes;
    if (calls_fixed_bytes(calls) + calls->spare_bytes > CALL_RUN_CACHE_BYTES) {
        trim_spares(calls, CALL_RUN_CACHE_BYTES / 2);
    }
}

void behavior_function_calls_free(struct behavior_function_calls *calls)
{
    if (!calls) {
        return;
    }
    while (calls->spare) {
        invocation_frame *frame = calls->spare;
        calls->spare = frame->next;
        frame_free(frame, FLOW_OK);
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
    trim_spares(calls, CALL_CACHE_BYTES);
    if (result == FLOW_OK) {
        behavior_function_calls_reset(calls);
    }
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
/* Where a copy shares its source's payload until either side writes it: the
 * managed heap that holds collections. */
static arena *sharing_storage(behavior_function_context *context)
{
    return context->managed_heap ? &context->managed_heap->storage
                                 : context->value_storage;
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
    invocation_frame **available = &calls->spare;
    while (*available && (*available)->function != call.function) {
        available = &(*available)->next;
    }
    invocation_frame *frame = *available;
    if (frame) {
        *available = frame->next;
        frame->next = NULL;
        calls->spare_bytes -= frame->bytes;
    }
    if (!frame) {
        frame = xcalloc(1, sizeof *frame);
        arena_init(&frame->storage, 512);
    }
    frame->function = call.function;
    frame->invocation = call;
    behavior_function_prepare_data(call.function, parent->services, &frame->data);
    frame->context = (behavior_function_context){
        .parent = parent,
        .managed_heap = parent->managed_heap,
        .call_enter = parent->call_enter,
        .call_leave = parent->call_leave,
        .activation = &frame->activation,
        .variables = parent->variables,
        .data = frame->data,
        .queries = frame->context.queries,
        .commands = frame->context.commands,
        .command_arguments = frame->context.command_arguments,
        .command_results = frame->context.command_results,
        .services = parent->services,
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
        .complete_action = parent->complete_action,
        .state_inputs = parent->state_inputs,
        .state_transition = parent->state_transition,
        .retrigger_action = parent->retrigger_action,
        .request_action_break = parent->request_action_break,
        .take_action_break = parent->take_action_break,
        .store_component = parent->store_component,
        .reduce_component = parent->reduce_component,
        .component_membership = parent->component_membership,
        .failure_observer = parent->failure_observer,
        .bind_instance = parent->bind_instance,
        .commit_instance = parent->commit_instance
    };
    pin_value *arguments = arena_alloc(&frame->storage, (size_t)(call.count + 1) * sizeof *arguments);
    /* A call lends its caller's values: the callee copies a parameter it
     * writes. Owned arguments are copies the frame takes over. A callee that
     * may write through a reference to a lent value gets its own copy, which
     * keeps the value it was passed. */
    bool lend = call.function->argument_sharing == BEHAVIOR_SHARING_SAFE;
    arena *payload = parent->value_storage;
    arena *values = payload->collectable ? payload : &frame->storage;
    for (int p = 0; p < call.count; p++) {
        const pin_value *argument = &parent->data->values[call.first + p];
        arguments[p] = *argument;
        if (call.owned_arguments) {
            behavior_function_collection_transfer(parent, &frame->context, &arguments[p]);
            continue;
        }
        if (lend || !pin_value_holds_storage(argument)) {
            continue;
        }
        if (pin_value_collection_copy(sharing_storage(parent), &arguments[p], argument)) {
            frame_free(frame, FLOW_ERR_ARGUMENT);
            call_error(function, parent, BEHAVIOR_FUNCTION_ARGUMENT, "function argument cannot be retained");
            return NULL;
        }
        behavior_function_collection_retain(&frame->context, &arguments[p]);
    }
    frame->context.arguments_stabilized = true;
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
    behavior_function_bind_storage(&frame->context, values);
    frame->context.result_storage = payload;
    behavior_function_prepare_results(
        call.function,
        frame->context.results,
        &frame->storage
    );
    size_t size = behavior_function_watch_size(call.function);
    if (size) {
        frame->context.watch_storage = arena_alloc(&frame->storage, size);
    }
    /* Supplied host banks live with the frame, so run_once must not release
     * their allocations before successful retirement can reset them. */
    behavior_function_prepare_host(call.function, &frame->context);
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
    context->query_variables = root->query_variables;
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

static bool same_header(const pin_value *value, const pin_value *declared)
{
    return value->type == declared->type && value->container == declared->container;
}
static int return_values(invocation_frame *frame, const behavior_function *parent_function)
{
    behavior_function_context *context = &frame->context, *parent = context->parent;
    behavior_function_invocation call = frame->invocation;
    /* A function without results may end anywhere, as a void function does;
       one with results must reach Return to produce them. */
    if (!context->returned) {
        if (!call.result_count && call.owned_arguments) {
            for (int p = 0; p < call.count; p++) {
                behavior_function_collection_drop(parent, &parent->data->values[call.first + p]);
            }
        }
        return call.result_count
                 ? call_error(parent_function, parent, BEHAVIOR_FUNCTION_NO_RETURN, "function completed without Return")
                 : FLOW_OK;
    }
    parent->returned_outcome = context->returned_outcome;
    for (int p = 0; p < call.result_count; p++) {
        /* The copy follows the result's header, so a header that left the
         * declared type could return a pointer's bits instead of a copy. */
        if (p < frame->function->result_count &&
            !same_header(&context->results[p], &frame->function->result_defaults[p])) {
            return call_error(parent_function, parent, BEHAVIOR_FUNCTION_DATA, "function result differs from its declared type");
        }
        /* A function that builds its result in the caller's storage returns it
         * there without a copy. The intrinsics that build there copy into it
         * everything they store, and builds_results says nothing else changes
         * what they build and no result returns the caller's own data, so a
         * value there was built for this call and holds nothing of the
         * callee's own storage. A callee that runs in the caller's storage, as
         * with a collectable payload, makes everything there, so it copies. */
        pin_value *out = &parent->data->values[call.result_first + p];
        const pin_value *result = &context->results[p];
        bool pointer = result->container != PIN_CONTAINER_SINGLE ||
                       result->type == PIN_STRUCT ||
                       result->type == PIN_UNION || result->type == PIN_STR;
        if (frame->function->builds_results && pointer && result->ptr &&
            context->value_storage != parent->value_storage &&
            arena_contains(parent->value_storage, result->ptr)) {
            if (out->ptr != result->ptr) {
                behavior_function_collection_drop(parent, out);
            }
            out->ptr = result->ptr;
            behavior_function_collection_transfer(context, parent, out);
            continue;
        }
        pin_value retained;
        if (pin_value_snapshot_to_arena(
                parent->value_storage,
                &retained,
                &context->results[p]
            )) {
            return call_error(parent_function, parent, BEHAVIOR_FUNCTION_DATA, "function result cannot be retained");
        }
        /* A result that shares a collection the caller holds, such as an
         * argument it lent or an earlier result, gets its own copy, so that
         * writing one leaves the other. */
        pin_value shared = retained;
        if (behavior_function_collection_holds(parent, &shared) &&
            pin_value_collection_copy(sharing_storage(parent), &retained, &shared)) {
            return call_error(parent_function, parent, BEHAVIOR_FUNCTION_DATA, "function result cannot be retained");
        }
        if (out->ptr != retained.ptr) {
            behavior_function_collection_drop(parent, out);
        }
        *out = retained;
        behavior_function_collection_transfer(context, parent, out);
    }
    if (call.owned_arguments) {
        for (int p = 0; p < call.count; p++) {
            behavior_function_collection_drop(parent, &parent->data->values[call.first + p]);
        }
    }
    return FLOW_OK;
}

/* The native entry that runs `call` in place of a frame: one bound to its
 * callee when the call would pass its arguments directly, with nothing
 * inspecting it. A task and an inspected call keep their VM frame. */
static const execution_native_export *native_call(
    const behavior_function_context *root,
    const behavior_function *function,
    const behavior_function_context *context,
    const behavior_function_invocation *call
)
{
    const execution_natives *natives = root->activation->natives;
    const execution_native_export *native = natives
                                              ? execution_natives_find(natives, call->function)
                                              : NULL;
    return native && !behavior_function_inspection_required(function, context) &&
                   program_call_views_allowed(context)
             ? native
             : NULL;
}
static void copy_failure(behavior_function_context *root, const behavior_function_context *context)
{
    root->error_message = context->error_message;
    root->failure = context->failure;
    root->source_function = context->source_function;
    root->node_id = context->node_id;
    root->error_source = context->error_source;
    root->error_frame = context->error_frame;
    root->error_stack = context->error_stack;
    root->error_pin = context->error_pin;
    root->error_steps = context->error_steps;
    root->error_status = context->error_status;
}

static int run_calls(const behavior_function *function, behavior_function_context *root, bool compiled)
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
                    if (call->owned_arguments) {
                        for (int p = 0; p < call->count; p++) {
                            behavior_function_collection_drop(
                                context,
                                &context->data->values[call->first + p]
                            );
                        }
                    }
                    continue;
                }
                root->wake_time = context->wake_time;
                return settle_calls(calls, result);
            }
            const execution_native_export *native = native_call(root, active, context, call);
            if (native) {
                pin_value *values = context->data->values;
                execution_native_call(
                    root->activation->natives,
                    native,
                    call->function,
                    &values[call->first],
                    call->count,
                    &values[call->result_first],
                    call->result_count
                );
                /* A frame's callee without outcomes returns none. */
                context->returned_outcome = 0;
                behavior_run_record(EXECUTION_RUN_NATIVE);
                continue;
            }
            invocation_frame *frame = frame_new(calls, active, context);
            if (!frame) {
                if (context != root) {
                    copy_failure(root, context);
                }
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
            behavior_managed_heap_safepoint(root->managed_heap, function, root);
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
                behavior_managed_heap_safepoint(root->managed_heap, function, root);
                continue;
            }
            context = count > 1 ? &calls->frames[count - 2]->context : root;
        }
        root->wake_time = context->wake_time;
        if (result < 0 && context != root) {
            /* Preserve the diagnostic before failure unwinds the owned frames.
             * Source observers already received the precise callee location. */
            copy_failure(root, context);
        }
        return settle_calls(calls, result);
    }
}
int behavior_function_run_calls(
    const behavior_function *function,
    behavior_function_context *root,
    bool compiled
)
{
    behavior_managed_heap *heap = root->managed_heap;
    if (heap) {
        heap->running_calls++;
    }
    int result = run_calls(function, root, compiled);
    if (heap) {
        heap->running_calls--;
    }
    return result;
}

static void trace_value(const pin_value *value, behavior_value_kind kind, arena_marker mark, void *user)
{
    behavior_managed_heap_trace_value(value, mark, user);
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
        for (int i = 0; i < s->value_count; i++) {
            int value = s->values[i];
            pin_type type = value < slots ? f->slots[value]
                                          : f->locals[value - slots].initial.type;
            behavior_value_kind kind = value < slots ? f->slot_kinds[value]
                                                     : f->locals[value - slots].kind;
            if (kind != BEHAVIOR_VALUE || type == PIN_STR || type == PIN_NAME ||
                type == PIN_UNION || type == PIN_POINTER) {
                mark(user, (const void *)(uintptr_t)a->values[value]);
            }
        }
    }
    /* Imported argument banks may themselves live in the managed heap. */
    mark(user, ctx->arguments);
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
            behavior_managed_heap_trace_value(&value, mark, user);
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
    behavior_function_collection_trace(ctx, mark, user);
    const struct behavior_function_calls *calls = ctx->activation
                                                    ? ctx->activation->calls
                                                    : NULL;
    for (int i = 0; calls && i < acount(calls->frames); i++) {
        trace_context(calls->frames[i]->function, &calls->frames[i]->context, mark, user);
        behavior_function_collection_trace(&calls->frames[i]->context, mark, user);
    }
}
static void visit_bank(
    const pin_value *values,
    int count,
    void (*visit)(void *, const pin_value *),
    void *user
)
{
    for (int i = 0; i < count; i++) {
        visit(user, &values[i]);
    }
}
static void visit_reference_bank(
    const pin_value *values,
    const behavior_value_kind *kinds,
    int count,
    void (*visit)(void *, const pin_value *),
    void *user
)
{
    for (int i = 0; kinds && i < count; i++) {
        pin_value pointer;
        if (kinds[i] == BEHAVIOR_REFERENCE &&
            execution_reference_managed_pointer(values[i], &pointer)) {
            visit(user, &pointer);
        }
    }
}

static void visit_context(
    const behavior_function *f,
    const behavior_function_context *ctx,
    void (*visit)(void *, const pin_value *),
    void *user
)
{
    const behavior_function_activation *a = ctx->activation;
    if (a && a->values && a->pc > 0) {
        const behavior_function_suspension
            *s = &f->suspensions[f->instructions[a->pc - 1].target];
        int slots = acount(f->slots);
        for (int i = 0; i < s->root_count; i++) {
            pin_value value = s->roots[i].shape;
            value.ptr = (void *)(uintptr_t)a->values[s->roots[i].index];
            visit(user, &value);
        }
        for (int i = 0; i < s->value_count; i++) {
            int index = s->values[i];
            pin_type type = index < slots ? f->slots[index]
                                          : f->locals[index - slots].initial.type;
            behavior_value_kind kind = index < slots ? f->slot_kinds[index]
                                                     : f->locals[index - slots].kind;
            if (kind == BEHAVIOR_REFERENCE) {
                pin_value reference = { .type = BF_POINTER_TYPE,
                                        .u64 = a->values[index] },
                          pointer;
                if (execution_reference_managed_pointer(reference, &pointer)) {
                    visit(user, &pointer);
                }
            }
            if (type == PIN_TASK || type == PIN_UNION || type == PIN_POINTER) {
                pin_value value = { .type = type, .u64 = a->values[index] };
                if (type == PIN_UNION || type == PIN_POINTER) {
                    value.ptr = (void *)(uintptr_t)a->values[index];
                }
                visit(user, &value);
            }
        }
    }
    visit_bank(ctx->arguments, ctx->argument_count, visit, user);
    visit_reference_bank(ctx->arguments, f->argument_kinds, ctx->argument_count, visit, user);
    visit_reference_bank(ctx->results, f->result_kinds, f->result_count, visit, user);
    visit_reference_bank(
        ctx->command_arguments,
        f->command_argument_kinds,
        acount(f->command_arguments),
        visit,
        user
    );
    visit_reference_bank(ctx->command_results, f->command_result_kinds, acount(f->command_results), visit, user);

    visit_bank(ctx->results, f->result_count, visit, user);
    visit_bank(ctx->variables, f->variable_count, visit, user);
    visit_bank(ctx->command_arguments, acount(f->command_arguments), visit, user);
    visit_bank(ctx->command_results, acount(f->command_results), visit, user);
    if (!ctx->data) {
        return;
    }
    visit_bank(ctx->data->values, acount(f->data_values), visit, user);
    for (int i = 0; i < acount(f->data_values); i++) {
        pin_value pointer;
        if (f->data_values[i].kind == BEHAVIOR_REFERENCE &&
            execution_reference_managed_pointer(ctx->data->values[i], &pointer)) {
            visit(user, &pointer);
        }
    }

    for (int i = 0; i < acount(f->data_values); i++) {
        if (f->data_values[i].size) {
            pin_value value = f->data_values[i].initial;
            value.ptr = ctx->data->buffers[i];
            visit(user, &value);
        }
    }
}
void behavior_function_visit_values(
    const behavior_function *f,
    const behavior_function_context *ctx,
    void (*visit)(void *, const pin_value *),
    void *user
)
{
    visit_context(f, ctx, visit, user);
    const struct behavior_function_calls *calls = ctx->activation
                                                    ? ctx->activation->calls
                                                    : NULL;
    for (int i = 0; calls && i < acount(calls->frames); i++) {
        visit_context(calls->frames[i]->function, &calls->frames[i]->context, visit, user);
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
