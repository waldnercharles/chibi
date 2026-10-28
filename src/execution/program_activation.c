#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/component.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/program_inspection.h"
#include "execution/program_internal.h"
#include "execution/request.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ACTIVATION_CACHE_BYTES ((size_t)64 * 1024)

static size_t cache_bytes(const execution_activation *a)
{
    return sizeof *a + arena_storage_bytes(&a->storage) +
           arena_storage_bytes(&a->payload) +
           behavior_function_host_storage_bytes(&a->context) +
           behavior_function_activation_storage_bytes(a->function, &a->activation);
}
size_t program_activation_cache_bytes(const execution_program *p)
{
    return p->spare_activation ? cache_bytes(p->spare_activation) : 0;
}
/* Only use after task ownership has been released, or before binding. */
static void discard(execution_activation *a)
{
    behavior_function_release_host(&a->context);
    behavior_function_activation_free(&a->activation);
    arena_free(&a->storage);
    arena_free(&a->payload);
    free(a);
}
void program_activation_cache_free(execution_program *p)
{
    execution_activation *a = p->spare_activation;
    p->spare_activation = NULL;
    if (a) {
        discard(a);
    }
}
static void unlink_continuation(execution_activation *a)
{
    if (!a->continuation_context) {
        return;
    }
    assert(a->continuation_context->pending_commands > 0);
    a->continuation_context->pending_commands--;
    a->continuation_context = NULL;
    a->continuation_owner = NULL;
}
static void cancel_continuations(behavior_function_context *ctx)
{
    execution_activation *owner = ctx->inspection_user;
    for (int i = 0; i < acount(owner->instance->activations); i++) {
        execution_activation *child = owner->instance->activations[i];
        if (child->continuation_context != ctx) {
            continue;
        }
        /* A running continuation stops when control returns to it. */
        if (child->running) {
            child->stopping = true;
            continue;
        }
        unlink_continuation(child);
        behavior_function_visit_contexts(&child->context, cancel_continuations);
        while (acount(child->requests)) {
            execution_request_detach(child->requests[0]->request);
        }
        child->finished = true;
        child->status = FLOW_ERR_STATE;
    }
}
void program_activation_cancel(execution_activation *a)
{
    a->cacheable = false;
    behavior_function_visit_contexts(&a->context, cancel_continuations);
    while (acount(a->requests)) {
        execution_request_detach(a->requests[0]->request);
    }
    a->finished = true;
    a->status = FLOW_OK;
}
void program_activation_free(execution_activation *activation)
{
    execution_program *p = activation->instance->program;
    bool keep = activation->cacheable && activation->has_run &&
                activation->finished && activation->status == FLOW_OK &&
                !activation->stopping && !activation->task &&
                !activation->origin_action && !activation->continuation_owner &&
                !activation->context.pending_commands &&
                !acount(activation->requests) && !p->owner_program->inspector &&
                !p->spare_activation;
    program_task_finished(activation, true);
    program_boxes_free(activation);
    program_inspection_release(activation);
    unlink_continuation(activation);
    behavior_function_visit_contexts(&activation->context, cancel_continuations);
    while (acount(activation->requests)) {
        execution_request_detach(activation->requests[0]->request);
    }
    afree(activation->requests);
    afree(activation->actions);
    if (keep) {
        behavior_function_reset_host(&activation->context);
        behavior_function_reset_activation(activation->function, &activation->activation);
        /* All escaping values already have their own storage. Retire payload
         * roots only after callbacks and host/call scratch have released them. */
        arena_reset(&activation->payload);
    } else {
        behavior_function_clear_calls(&activation->activation);
    }
    if (keep && !p->spare_activation &&
        cache_bytes(activation) <= ACTIVATION_CACHE_BYTES) {
        arena storage = activation->storage;
        arena payload = activation->payload;
        behavior_function_activation saved = activation->activation;
        behavior_function_context host = activation->context;
        const behavior_function *function = activation->function;
        arena_reset(&storage);
        *activation = (execution_activation){ .function = function,
                                              .storage = storage,
                                              .payload = payload,
                                              .activation = saved,
                                              .context = host };
        p->spare_activation = activation;
        return;
    }
    discard(activation);
}
static void leave_context(behavior_function_context *ctx, int result)
{
    (void)result;
    execution_activation *a = ctx->inspection_user;
    program_commit_instance(ctx);
    cancel_continuations(ctx);
    program_boxes_detach(a, ctx);
    for (int i = 0; i < acount(a->requests);) {
        command_wait *wait = a->requests[i];
        if (wait->context != ctx) {
            i++;
            continue;
        }
        /* A delivered request, such as a spawn still initializing its entity,
         * belongs to the activation until it finishes, not to the frame. */
        if (wait->delivered) {
            wait->context = ctx->parent;
            i++;
            continue;
        }
        execution_request_detach(wait->request);
    }
}
bool program_call_views_allowed(const behavior_function_context *ctx)
{
    return !ctx->inspect && !ctx->watch && !ctx->trace && !ctx->source_pause &&
           !ctx->source_capture && !ctx->call_enter &&
           (!ctx->call_leave || ctx->call_leave == leave_context) &&
           program_world_views_allowed(ctx);
}
execution_activation *program_activation_new(
    execution_instance *instance,
    const behavior_function *function,
    const pin_value *arguments,
    int count,
    double now,
    double delta,
    unsigned budget
)
{
    int declared = 0, result_count = 0;
    const pin_value *defaults = behavior_function_arguments(function, &declared);
    const pin_value *result_defaults = behavior_function_results(function, &result_count);
    if (count < 0 || count > declared) {
        return NULL;
    }
    execution_program *p = instance->program;
    if (p->spare_activation && (p->spare_activation->function != function ||
                                p->owner_program->inspector)) {
        program_activation_cache_free(p);
    }
    execution_activation *a = p->spare_activation;
    p->spare_activation = NULL; /* Reentrant work cannot borrow this live frame. */
    if (!a) {
        a = xcalloc(1, sizeof *a);
        arena_init_collectable(&a->payload);
    }
    a->instance = instance;
    a->function = function;
    a->cacheable = true;
    pin_value *args = arena_alloc(&a->storage, (size_t)(declared + 1) * sizeof *args);
    for (int i = 0; i < declared; i++) {
        const pin_value *value = i < count ? &arguments[i] : &defaults[i];
        if (!program_same_type(defaults[i], *value)) {
            discard(a);
            return NULL;
        }
        args[i] = *value;
    }
    for (int i = 0; i < declared; i++) {
        bool value_argument = i >= acount(function->argument_kinds) ||
                              function->argument_kinds[i] == BEHAVIOR_VALUE;
        pin_value owned = args[i];
        if (value_argument &&
            (program_validate_values(p, &args[i], 1) ||
             pin_value_collection_copy(&p->owner_program->managed->storage, &owned, &args[i]))) {
            discard(a);
            return NULL;
        }
        if (pin_value_snapshot_to_arena(&a->payload, &args[i], &owned)) {
            if (value_argument) {
                pin_value_collection_release(&owned);
            }
            discard(a);
            return NULL;
        }
        if (value_argument) {
            behavior_function_collection_retain(&a->context, &args[i]);
        }
    }
    pin_value *results = arena_alloc(&a->storage, (size_t)(result_count + 1) * sizeof *results);
    if (program_validate_values(p, result_defaults, result_count)) {
        discard(a);
        return NULL;
    }
    for (int i = 0; i < result_count; i++) {
        pin_value owned;
        if (pin_value_collection_copy(&p->owner_program->managed->storage, &owned, &result_defaults[i])) {
            for (int j = 0; j < i; j++) {
                pin_value_collection_release(&results[j]);
            }
            discard(a);
            return NULL;
        }
        if (pin_value_snapshot_to_arena(&a->payload, &results[i], &owned)) {
            pin_value_collection_release(&owned);
            for (int j = 0; j < i; j++) {
                pin_value_collection_release(&results[j]);
            }
            discard(a);
            return NULL;
        }
    }
    behavior_function_prepare_results(function, results, &a->storage);
    bool resumable = behavior_function_resumable(function);
    if (resumable && !a->activation.values) {
        behavior_function_activation_init(function, &a->activation);
    }
    a->activation.natives = p->natives;
    a->context = (behavior_function_context){
        .activation = resumable ? &a->activation : NULL,
        .data = a->context.data,
        .owned_data = a->context.owned_data,
        .queries = a->context.queries,
        .commands = a->context.commands,
        .command_arguments = a->context.command_arguments,
        .command_results = a->context.command_results,
        .collection_owners = a->context.collection_owners,
        .variables = instance->values,
        .states = instance->node_states,
        .variable_storage = instance->layouts,
        .value_owner = instance,
        .value_storage = &a->payload,
        .managed_heap = instance->program->owner_program->managed,
        .result_storage = &a->payload,

        .arguments = args,
        .argument_count = declared,
        .arguments_stabilized = true,
        .results = results,
        .now = now,
        .delta_seconds = delta,
        .instance_time = &instance->time,
        .budget = budget,
        .services = instance->program->services,
        .owner = instance->owner,
        .owner_type_name = "ecs.entity",

        .call_leave = leave_context,

        .inspection_user = a
    };
    for (int i = 0; i < result_count; i++) {
        behavior_function_collection_retain(&a->context, &results[i]);
    }
    program_bind_world_callbacks(&a->context);
    program_bind_commands(&a->context);
    program_bind_tasks(&a->context);
    a->context.program_name = instance->program->name;
    program_bind_instance(function, &a->context);
    behavior_function_bind_storage(&a->context, &a->payload);
    if (!behavior_function_bind(
            function,
            &a->context,
            instance->program->services,
            instance->owner
        )) {
        program_activation_free(a);
        return NULL;
    }
    behavior_function_prepare_host(function, &a->context);
    return a;
}
void program_continuation_finish(execution_activation *a)
{
    if (!a->continuation_owner || !a->finished) {
        return;
    }
    behavior_function_context *ctx = a->continuation_context;
    execution_activation *owner = a->continuation_owner;
    unlink_continuation(a);
    if (a->status < 0) {
        owner->status = a->status;
        owner->finished = true;
    } else if (a->context.returned && !ctx->exit_requested) {
        int count = acount(a->function->result_defaults);
        pin_value *results = arena_alloc(&owner->payload, (size_t)(count + 1) * sizeof *results);
        for (int i = 0; i < count; i++) {
            if (pin_value_snapshot_to_arena(
                    &owner->payload,
                    &results[i],
                    &a->context.results[i]
                )) {
                owner->status = FLOW_ERR_ARGUMENT;
                owner->finished = true;
                return;
            }
        }
        for (int i = 0; i < count; i++) {
            const pin_value *result = &results[i];
            if (ctx->results[i].ptr != result->ptr ||
                ctx->results[i].container != result->container ||
                ctx->results[i].type != result->type) {
                behavior_function_collection_drop(ctx, &ctx->results[i]);
            }
            behavior_function_collection_transfer(&a->context, ctx, result);
            ctx->results[i] = results[i];
        }
        ctx->returned_outcome = a->context.returned_outcome;
        ctx->exit_requested = a->context.exit_requested;
    }
}
int program_activation_run(execution_activation *a, double now, double delta, unsigned budget)
{
    a->cacheable &= !a->has_run && !a->instance->program->owner_program->inspector;
    if (program_task_due(a, now)) {
        program_tasks_tick(a->instance, now);
    }
    if (a->finished) {
        return a->status;
    }
    program_inspection_bind(a);
    if (program_inspection_paused(a)) {
        return FLOW_PAUSED_RESULT;
    }
    for (int i = 0; i < acount(a->requests);) {
        command_wait *wait = a->requests[i];
        if (program_wait_due(wait, now)) {
            bool completed = flow_host_complete(wait->request, -1, (pin_value){ 0 });
            assert(completed);
            (void)completed;
            wait->timer = false;
            flow_host_release(wait->request);
        }
        flow_host_state status = flow_host_status(wait->request);
        if (status == FLOW_HOST_CANCELLED) {
            a->finished = true;
            return a->status = FLOW_ERR_STATE;
        }
        if (status != FLOW_HOST_READY || wait->delivered) {
            i++;
            continue;
        }
        const behavior_function_command *command = &wait->function->commands[wait->command];
        int completed = program_complete_action(
            wait->context,
            command->node_id,
            command->result_count ? wait->context->command_results + command->result_first
                                  : NULL,
            command->result_count
        );
        wait->delivered = true;
        flow_host_request *request = wait->request;
        if (a->wait == wait) {
            a->wait = NULL;
        }
        execution_request_finish(request, FLOW_HOST_FINISHED);
        if (completed < 0) {
            a->finished = true;
            return a->status = completed;
        }
        if (i < acount(a->requests) && a->requests[i] == wait) {
            i++;
        }
    }
    double deadline = 0;
    for (int i = 0; i < acount(a->requests); i++) {
        command_wait *wait = a->requests[i];
        if (wait->timer && !wait->yield && (!deadline || wait->deadline < deadline)) {
            deadline = wait->deadline;
        }
    }
    if (deadline) {
        a->context.wake_time = deadline;
    }
    if (a->wait && flow_host_status(a->wait->request) == FLOW_HOST_PENDING) {
        return FLOW_SUSPENDED_RESULT;
    }
    const behavior_function *active = a->function;
    behavior_function_context *current = behavior_function_active_context(&a->context, &active);
    if (current->pending_commands) {
        return FLOW_SUSPENDED_RESULT;
    }
    execution_activation *before = a->instance->program->running;
    a->instance->program->running = a;
    a->running = true;
    program_bind_instance(a->function, &a->context);
    a->context.now = now;
    a->context.delta_seconds = delta;
    a->context.budget = budget ? budget : 10000;
    execution_program *observed = a->instance->program->owner_program;
    clock_t start = observed->observer ? clock() : (clock_t)-1;
    bool resumed = a->has_run;
    a->has_run = true;
    a->status = behavior_function_run(
        a->function,
        &a->context,
        a->instance->program->module->backend != BEHAVIOR_BYTECODE
    );
    a->cacheable &= a->status == FLOW_OK && !acount(a->requests);
    if (observed->observer) {
        clock_t end = clock();
        bool available = start != (clock_t)-1 && end != (clock_t)-1 && end >= start;
        execution_profile_sample sample = {
            .program = observed,
            .module = a->instance->program->module,
            .function = a->function,
            .owner = a->instance->owner,
            .resume = resumed,
            .cpu_available = available,
            .cpu_seconds = available ? (double)(end - start) / CLOCKS_PER_SEC : 0,
            .status = a->status
        };
        observed->observer(observed->observer_context, &sample);
    }
    a->running = false;
    a->instance->program->running = before;
    if (a->instance->running == a) {
        a->instance->running = before;
    }
    for (int i = 0; i < acount(a->instance->program->imports); i++) {
        execution_instance *instance = program_instance_row(
            a->instance->program->imports[i],
            a->instance->owner,
            a->instance->query_variables
        );
        if (instance && instance->running == a) {
            instance->running = before;
        }
    }
    program_commit_instance(&a->context);
    if (a->stopping) {
        program_activation_cancel(a);
    }
    a->finished = a->status != FLOW_SUSPENDED_RESULT && a->status != FLOW_PAUSED_RESULT;
    if (a->finished) {
        program_boxes_detach(a, NULL);
        program_task_finished(a, a->stopping);
    }
    program_inspection_finish(a);
    program_continuation_finish(a);
    /* Completed calls retire their payload values. Collect only calls
     * that can retain values across another resume or outstanding request. */
    if (!a->finished || acount(a->requests)) {
        program_collect_payloads(a->instance->program, a);
    }
    return a->status;
}
