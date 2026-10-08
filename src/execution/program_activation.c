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
        assert(!child->running);
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
    behavior_function_visit_contexts(&a->context, cancel_continuations);
    while (acount(a->requests)) {
        execution_request_detach(a->requests[0]->request);
    }
    a->finished = true;
    a->status = FLOW_OK;
}
void program_activation_free(execution_activation *activation)
{
    program_task_finished(activation, true);
    program_inspection_release(activation);
    unlink_continuation(activation);
    behavior_function_visit_contexts(&activation->context, cancel_continuations);
    while (acount(activation->requests)) {
        execution_request_detach(activation->requests[0]->request);
    }
    afree(activation->requests);
    afree(activation->actions);
    afree(activation->locals);
    behavior_function_activation_free(&activation->activation);
    behavior_function_release_host(&activation->context);
    arena_free(&activation->storage);
    arena_free(&activation->payload);
    free(activation);
}
static void leave_context(behavior_function_context *ctx, int result)
{
    (void)result;
    execution_activation *a = ctx->inspection_user;
    program_commit_instance(ctx);
    cancel_continuations(ctx);
    for (int i = 0; i < acount(a->locals);) {
        if (a->locals[i].context == ctx) {
            adel(a->locals, i);
        } else {
            i++;
        }
    }
    for (int i = 0; i < acount(a->requests);) {
        if (a->requests[i]->context != ctx) {
            i++;
            continue;
        }
        execution_request_detach(a->requests[i]->request);
    }
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
    execution_activation *a = xcalloc(1, sizeof *a);
    a->instance = instance;
    a->function = function;
    int declared = 0, result_count = 0;
    const pin_value *defaults = behavior_function_arguments(function, &declared);
    const pin_value *result_defaults = behavior_function_results(function, &result_count);
    if (count < 0 || count > declared) {
        free(a);
        return NULL;
    }
    arena_init_collectable(&a->payload);
    pin_value *args = arena_alloc(&a->storage, (size_t)(declared + 1) * sizeof *args);
    for (int i = 0; i < declared; i++) {
        const pin_value *value = i < count ? &arguments[i] : &defaults[i];
        if (!program_same_type(defaults[i], *value) ||
            pin_value_snapshot_to_arena(&a->payload, &args[i], value)) {
            arena_free(&a->storage);
            arena_free(&a->payload);
            free(a);
            return NULL;
        }
    }
    pin_value *results = arena_alloc(&a->storage, (size_t)(result_count + 1) * sizeof *results);
    for (int i = 0; i < result_count; i++) {
        int status = pin_value_snapshot_to_arena(&a->payload, &results[i], &result_defaults[i]);
        assert(!status);
        (void)status;
    }
    behavior_function_prepare_results(function, results, &a->storage);
    bool resumable = behavior_function_resumable(function);
    if (resumable) {
        behavior_function_activation_init(function, &a->activation);
    }
    a->context = (behavior_function_context){
        .activation = resumable ? &a->activation : NULL,
        .variables = instance->values,
        .states = instance->node_states,
        .variable_storage = instance->layouts,
        .value_owner = instance,
        .value_storage = &a->payload,

        .arguments = args,
        .argument_count = declared,
        .results = results,
        .now = now,
        .delta_seconds = delta,
        .instance_time = &instance->time,
        .budget = budget,
        .services = instance->program->services,
        .owner = instance->owner,
        .owner_type_name = "ecs.entity",

        .call_leave = leave_context,

        .initialize_local = program_initialize_local,
        .store_local_group = program_store_local_group,

        .inspection_user = a
    };
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
    if (a->initializer || !a->continuation_owner || !a->finished) {
        return;
    }
    behavior_function_context *ctx = a->continuation_context;
    execution_activation *owner = a->continuation_owner;
    unlink_continuation(a);
    if (a->status < 0) {
        owner->status = a->status;
        owner->finished = true;
    } else if (a->context.returned && !ctx->exit_requested) {
        for (int i = 0; i < acount(a->function->result_defaults); i++) {
            int status = pin_value_snapshot_to_arena(
                &owner->payload,
                &ctx->results[i],
                &a->context.results[i]
            );
            assert(!status);
            (void)status;
        }
        ctx->returned_outcome = a->context.returned_outcome;
        ctx->exit_requested = a->context.exit_requested;
    }
}
int program_activation_run(execution_activation *a, double now, double delta, unsigned budget)
{
    if (a->finished) {
        return a->status;
    }
    program_inspection_bind(a);
    if (program_inspection_paused(a)) {
        return FLOW_PAUSED_RESULT;
    }
    for (int i = 0; i < acount(a->requests);) {
        command_wait *wait = a->requests[i];
        if (wait->timer && wait->deadline <= now) {
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
            wait->context->command_results + command->result_first,
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
        if (wait->timer && (!deadline || wait->deadline < deadline)) {
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
        a->instance->program->module->backend == BEHAVIOR_SLJIT
    );
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
        execution_instance *instance = execution_program_instance(
            a->instance->program->imports[i],
            a->instance->owner
        );
        if (instance && instance->running == a) {
            instance->running = before;
        }
    }
    program_commit_instance(&a->context);
    a->finished = a->status != FLOW_SUSPENDED_RESULT && a->status != FLOW_PAUSED_RESULT;
    if (a->finished) {
        program_task_finished(a, false);
    }
    program_inspection_finish(a);
    program_continuation_finish(a);
    program_collect_payloads(a->instance->program, a);
    return a->status;
}
