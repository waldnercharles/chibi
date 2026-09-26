#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>

typedef struct
{
    const behavior_function *function;
    behavior_function_invocation invocation;
    behavior_function_context context;
    behavior_function_activation activation;
    arena storage;
    bool entered;
} invocation_frame;

struct behavior_function_calls
{
    /* Frames have stable addresses. A growing pointer array never invalidates
     * context.parent or arguments retained by a pending host operation. */
    invocation_frame **frames;
    int result;
};

static void frame_free(invocation_frame *frame, int result)
{
    if (frame->entered && frame->context.call_leave) {
        frame->context.call_leave(&frame->context, result);
    }
    behavior_function_release_host(&frame->context);
    behavior_function_activation_free(&frame->activation);
    arena_free(&frame->storage);
    free(frame);
}

void behavior_function_calls_free(struct behavior_function_calls *calls)
{
    if (!calls) {
        return;
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
    return failure == BEHAVIOR_FUNCTION_ARGUMENT ? FLOW_ERR_ARGUMENT
                                                 : FLOW_ERR_INVALID_GRAPH;
}

static invocation_frame *frame_new(const behavior_function *function, behavior_function_context *parent)
{
    const behavior_function_instruction
        *i = &function->instructions[parent->activation->pc - 1];
    assert(i->op == BF_INVOKE && i->left >= 0 && i->left < acount(function->calls));
    behavior_function_invocation call = function->calls[i->left];
    assert(call.function && parent->data && parent->value_storage);
    invocation_frame *frame = xcalloc(1, sizeof *frame);
    frame->function = call.function;
    frame->invocation = call;
    frame->context = (behavior_function_context){
        .parent = parent,
        .call_enter = parent->call_enter,
        .call_leave = parent->call_leave,
        .activation = &frame->activation,
        .budget = parent->budget,
        .call_depth = parent->call_depth + (unsigned)function->frames[i->frame].depth + 1,
        .inspection_user = parent->inspection_user,
        .begin_command = parent->begin_command
    };
    arena_init(&frame->storage, 4096);
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
    if (behavior_function_resumable(call.function)) {
        behavior_function_activation_init(call.function, &frame->activation);
    }
    behavior_function_bind_storage(&frame->context, &frame->storage);
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
    context->own_type_name = root->own_type_name;
    context->inspect = root->inspect;
    context->watch = root->watch;
    context->trace = root->trace;
    context->owner_type_name = root->owner_type_name;
    context->world = root->world;
    context->binding_world = root->binding_world;
    bool bound = behavior_function_bind(frame->function, context, root->services, root->owner);
    assert(bound);
    (void)bound;
}

static int return_values(invocation_frame *frame, const behavior_function *parent_function)
{
    behavior_function_context *context = &frame->context, *parent = context->parent;
    behavior_function_invocation call = frame->invocation;
    /* A function without results may end anywhere, as a void function does;
       one with results must reach Return to produce them. */
    if (!context->returned_node_id) {
        return call.result_count
                 ? call_error(parent_function, parent, BEHAVIOR_FUNCTION_NO_RETURN, "function completed without Return")
                 : FLOW_OK;
    }
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
    bool native
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
        int result = behavior_function_run_once(active, context, native);
        if (result == BEHAVIOR_FUNCTION_CALL_RESULT) {
            invocation_frame *frame = frame_new(active, context);
            if (!frame) {
                calls->result = FLOW_ERR_ARGUMENT;
                return calls->result;
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
            frame_free(calls->frames[count - 1], result);
            asetlen(calls->frames, count - 1);
            if (!result) {
                continue;
            }
            context = count > 1 ? &calls->frames[count - 2]->context : root;
        }
        root->wake_time = context->wake_time;
        calls->result = result;
        return result;
    }
}
