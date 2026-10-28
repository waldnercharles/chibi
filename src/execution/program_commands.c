#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/component.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/program_internal.h"
#include "execution/request.h"
#include "execution/state.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const execution_module_action *action_plan(const execution_module *m, uint64_t id)
{
    for (int i = 0; i < m->plan.action_count; i++) {
        if (m->plan.actions[i].id == id) {
            return &m->plan.actions[i];
        }
    }
    return NULL;
}
static action_values *action_bank(behavior_function_context *ctx, uint64_t action)
{
    execution_activation *a = ctx->inspection_user;
    for (int i = 0; i < acount(a->actions); i++) {
        if (a->actions[i].context == ctx && a->actions[i].action == action) {
            return &a->actions[i];
        }
    }
    for (execution_activation *child = a; child->continuation_owner;
         child = child->continuation_owner) {
        execution_activation *parent = child->continuation_owner;
        behavior_function_context *origin = child->continuation_context;
        for (int i = 0; i < acount(parent->actions); i++) {
            if (parent->actions[i].context == origin &&
                parent->actions[i].action == action) {
                return &parent->actions[i];
            }
        }
        a = parent;
    }
    a = ctx->inspection_user;
    const execution_module_action *plan = action_plan(
        ((execution_instance *)ctx->value_owner)->program->module,
        action
    );
    if (!plan) {
        return NULL;
    }
    action_values bank = { .context = ctx, .action = action, .count = plan->result_count };
    bank.values = arena_alloc(
        &a->storage,
        (size_t)(bank.count + 1) * sizeof *bank.values
    );
    for (int i = 0; i < bank.count; i++) {
        int status = pin_value_snapshot_to_arena(&a->payload, &bank.values[i], &plan->defaults[i]);
        assert(!status);
        (void)status;
    }
    apush(a->actions, bank);
    return &a->actions[acount(a->actions) - 1];
}
static const pin_value *action_result(behavior_function_context *ctx, uint64_t action, int ordinal)
{
    action_values *bank = action_bank(ctx, action);
    return bank && ordinal >= 0 && ordinal < bank->count ? &bank->values[ordinal]
                                                         : NULL;
}
static int request_action_break(behavior_function_context *ctx, uint64_t action)
{
    if (!action) {
        execution_activation *child = ctx->inspection_user;
        for (; child->continuation_owner; child = child->continuation_owner) {
            behavior_function_context *origin = child->continuation_context;
            const execution_module_action *plan = action_plan(
                ((execution_instance *)origin->value_owner)->program->module,
                child->origin_action
            );
            if (plan && plan->loop) {
                return request_action_break(origin, child->origin_action);
            }
        }
        return FLOW_ERR_STATE;
    }
    action_values *bank = action_bank(ctx, action);
    if (!bank) {
        return FLOW_ERR_ARGUMENT;
    }
    bank->break_requested = true;
    return FLOW_OK;
}
static int take_action_break(behavior_function_context *ctx, uint64_t action, bool *requested)
{
    action_values *bank = action_bank(ctx, action);
    if (!bank) {
        return FLOW_ERR_ARGUMENT;
    }
    *requested = bank->break_requested;
    bank->break_requested = false;
    return FLOW_OK;
}
static bool wait_complete(void *owner, int pin, pin_value value)
{
    command_wait *wait = owner;
    const behavior_function *active = wait->function;
    const behavior_function_command *command = &active->commands[wait->command];
    if (pin < -1) {
        return false;
    }
    if (pin >= 0) {
        int ordinal = pin;
        if (command->result_pins) {
            ordinal = -1;
            for (int i = 0; i < command->result_count; i++) {
                if (command->result_pins[i] == pin) {
                    ordinal = i;
                }
            }
        }
        int index = command->result_first + ordinal;
        if (ordinal < 0 || ordinal >= command->result_count || index < 0 ||
            index >= acount(active->command_results) ||
            !program_same_type(active->command_results[index], value)) {
            return false;
        }
        if (pin_value_snapshot_to_arena(
                &wait->activation->payload,
                &wait->context->command_results[index],
                &value
            )) {
            return false;
        }
    }
    return true;
}
static void wait_ready(void *owner)
{
    command_wait *wait = owner;
    if (wait->pending) {
        assert(wait->context->pending_commands > 0);
        wait->context->pending_commands--;
        wait->pending = false;
    }
    wait->context->error_status = 0;
    wait->activation->context.wake_time = 0;
}
static void wait_cancel(void *owner)
{
    command_wait *wait = owner;
    if (wait->delivered || !wait->pending) {
        return;
    }
    assert(wait->context->pending_commands > 0);
    wait->context->pending_commands--;
    wait->pending = false;
    wait->context->error_status = FLOW_ERR_STATE;
    wait->activation->status = FLOW_ERR_STATE;
    wait->activation->finished = true;
}
static int wait_resume(void *owner)
{
    command_wait *wait = owner;
    if (!wait->begun) {
        return FLOW_SUSPENDED_RESULT;
    }
    execution_program *p = wait->activation->instance->program;
    return program_activation_run(wait->activation, p->now, p->delta, p->budget);
}
static void wait_detach(void *owner)
{
    command_wait *wait = owner;
    execution_activation *a = wait->activation;
    if (wait->pending) {
        assert(wait->context->pending_commands > 0);
        wait->context->pending_commands--;
        wait->pending = false;
    }
    if (a->wait == wait) {
        a->wait = NULL;
    }
    for (int i = 0; i < acount(a->requests); i++) {
        if (a->requests[i] == wait) {
            adel(a->requests, i);
            break;
        }
    }
    if (wait->timer) {
        flow_host_release(wait->request);
    }
    free(wait);
}
static const void *wait_identity(void *owner)
{
    return ((command_wait *)owner)->activation;
}
static execution_owner_state wait_state(void *owner)
{
    execution_activation *a = ((command_wait *)owner)->activation;
    if (a->running) {
        return EXECUTION_OWNER_RUNNING;
    }
    if (a->finished) {
        return a->status < 0 ? EXECUTION_OWNER_FAILED : EXECUTION_OWNER_COMPLETED;
    }
    return a->status == FLOW_PAUSED_RESULT ? EXECUTION_OWNER_PAUSED
                                           : EXECUTION_OWNER_SUSPENDED;
}
static bool wait_valid(void *owner)
{
    execution_instance *instance = ((command_wait *)owner)->activation->instance;
    return !instance->owner || !instance->program->world ||
           ecs_entity_is_alive(
               instance->program->world,
               ecs_entity_from_u64(instance->owner)
           );
}
static bool wait_runnable(void *owner, double now)
{
    execution_activation *a = ((command_wait *)owner)->activation;
    if (!a->finished && !a->running && program_task_due(a, now)) {
        return true;
    }
    if (a->finished || a->running || a->context.wake_time > now) {
        return false;
    }
    for (int i = 0; i < acount(a->requests); i++) {
        command_wait *wait = a->requests[i];
        if (program_wait_due(wait, now) ||
            (flow_host_status(wait->request) == FLOW_HOST_READY && !wait->delivered)) {
            return true;
        }
    }
    return !a->wait || flow_host_status(a->wait->request) == FLOW_HOST_READY;
}
static int wait_tick(void *owner, double now)
{
    execution_activation *a = ((command_wait *)owner)->activation;
    execution_program *p = a->instance->program;
    return program_activation_run(a, now, p->delta, p->budget);
}
static const execution_request_sink wait_sink = { .complete = wait_complete,
                                                  .ready = wait_ready,
                                                  .cancel = wait_cancel,
                                                  .resume = wait_resume,
                                                  .detach = wait_detach,
                                                  .identity = wait_identity,
                                                  .state = wait_state,
                                                  .valid = wait_valid,
                                                  .runnable = wait_runnable,
                                                  .tick = wait_tick };
static int begin_command(behavior_function_context *ctx, int command)
{
    execution_activation *activation = ctx->inspection_user;
    command_wait *wait = xcalloc(1, sizeof *wait);
    const behavior_function *function = activation->function;
    behavior_function_active_context(&activation->context, &function);
    *wait = (command_wait){ .activation = activation,
                            .context = ctx,
                            .command = command,
                            .function = function,
                            .pending = true };
    ctx->pending_commands++;
    flow_host_request
        *request = execution_request_new(&wait_sink, wait, activation->instance->owner);
    wait->request = request;
    activation->wait = wait;
    apush(activation->requests, wait);
    behavior_command_binding *binding = &ctx->commands[command];
    const node_command *descriptor = function->commands[command].command;
    if (descriptor->intrinsic == NODE_COMMAND_DELAY ||
        descriptor->intrinsic == NODE_COMMAND_YIELD) {
        wait->timer = true;
        if (descriptor->intrinsic == NODE_COMMAND_YIELD) {
            wait->yield = true;
            wait->update = activation->instance->program->updates;
            wait->begun = true;
            return 1;
        } else {
            assert(binding->count == 1);
            pin_value duration = ctx->command_arguments[binding->first];
            assert(duration.type == PIN_F64 && duration.container == PIN_CONTAINER_SINGLE);
            wait->deadline = ctx->now + duration.d;
        }
        wait->begun = true;
        if (wait->deadline > ctx->now) {
            if (!activation->context.wake_time ||
                wait->deadline < activation->context.wake_time) {
                activation->context.wake_time = wait->deadline;
            }
            return 1;
        }
        bool completed = flow_host_complete(request, -1, (pin_value){ 0 });
        assert(completed);
        (void)completed;
        wait->timer = false;
        flow_host_release(request);
    } else if (descriptor->intrinsic != NODE_COMMAND_NATIVE) {
        int status = program_task_command(
            ctx,
            &function->commands[command],
            descriptor->intrinsic,
            request
        );
        if (status < 0) {
            flow_host_release(request);
            execution_request_detach(request);
            return status;
        }
    } else if (!binding->begin) {
        flow_host_release(request);
        execution_request_detach(request);
        ctx->error_message = "asynchronous command has no begin implementation";
        return FLOW_ERR_NODE;
    }
    const char *error = NULL;
    int result = descriptor->intrinsic != NODE_COMMAND_NATIVE
                   ? 0
                   : binding->begin(
                         binding->service,
                         binding->reference,
                         binding->count ? ctx->command_arguments + binding->first : NULL,
                         binding->count,
                         request,
                         &error
                     );
    if (result < 0) {
        ctx->error_message = error;
        flow_host_release(request);
        execution_request_detach(request);
        return result;
    }
    /* A native begin may remove the target and complete synchronously. Stop
     * before consuming that result or invoking an authored continuation. */
    if (!program_task_check(activation, ctx->now)) {
        return 1;
    }
    wait->begun = true;
    if (flow_host_status(request) == FLOW_HOST_READY) {
        activation->wait = NULL;
        const behavior_function_command *c = &wait->function->commands[wait->command];
        int completed = program_complete_action(
            ctx,
            c->node_id,
            c->result_count ? ctx->command_results + c->result_first : NULL,
            c->result_count
        );
        if (activation->stopping || !program_wait_attached(activation, wait)) {
            return 1;
        }
        wait->delivered = true;
        execution_request_finish(request, FLOW_HOST_FINISHED);
        if (completed < 0) {
            return completed;
        }
        return 0;
    }
    return 1;
}
bool program_request_deliver(flow_host_request *request, const pin_value *values, int count)
{
    command_wait *wait = execution_request_context(request, &wait_sink);
    if (!wait) {
        return false;
    }
    const behavior_function_command *c = &wait->function->commands[wait->command];
    for (int i = 0; i < c->result_count; i++) {
        int index = c->result_first + i;
        const pin_value *value = i < count
                                   ? &values[i]
                                   : &wait->function->command_results[index];
        if (!program_same_type(wait->function->command_results[index], *value) ||
            pin_value_snapshot_to_arena(
                &wait->activation->payload,
                &wait->context->command_results[index],
                value
            )) {
            return false;
        }
    }
    return flow_host_complete(request, -1, (pin_value){ 0 });
}
int program_complete_action(behavior_function_context *ctx, uint64_t action, const pin_value *values, int count)
{
    execution_activation *owner = ctx->inspection_user;
    if (!program_task_check(owner, ctx->now)) {
        return FLOW_OK;
    }
    execution_program *p = owner->instance->program;
    action_values *bank = action_bank(ctx, action);
    const execution_module *scope = ((execution_instance *)ctx->value_owner)
                                        ->program->module;
    const execution_module_action *plan = action_plan(scope, action);
    if (!plan) {
        return FLOW_OK; /* An awaited anonymous native operation has no authored outcome handlers. */
    }
    if (!bank || count != bank->count) {
        return FLOW_ERR_ARGUMENT;
    }
    program_commit_instance(ctx);
    for (int i = 0; i < count; i++) {
        if (!program_same_type(plan->defaults[i], values[i]) ||
            pin_value_snapshot_to_arena(&owner->payload, &bank->values[i], &values[i])) {
            return FLOW_ERR_ARGUMENT;
        }
    }
    const behavior_function *current_function = owner->function;
    behavior_function_active_context(&owner->context, &current_function);
    uint64_t parent = 0;
    for (int i = 0; i < scope->plan.function_count; i++) {
        if (scope->plan.functions[i].body == current_function) {
            parent = scope->plan.functions[i].id;
        }
    }
    uint64_t outcome = ctx->returned_outcome ? ctx->returned_outcome : plan->completion;
    ctx->returned_outcome = 0;
    for (int f = 0; f < scope->plan.function_count; f++) {
        const execution_module_function *fn = &scope->plan.functions[f];
        if (fn->kind != EXECUTION_FUNCTION_CONTINUATION || fn->action != action ||
            fn->outcome != outcome || (fn->parent && fn->parent != parent)) {
            continue;
        }
        pin_value *arguments = NULL;
        if (fn->capture_count > ctx->argument_count) {
            return FLOW_ERR_ARGUMENT;
        }
        for (int i = 0; i < fn->capture_count; i++) {
            apush(arguments, ctx->arguments[i]);
        }
        for (int i = 0; i < fn->local_capture_count; i++) {
            pin_value reference;
            if (!program_capture_reference(&owner->payload, ctx, &fn->captures[i], &reference)) {
                afree(arguments);
                return FLOW_ERR_STATE;
            }
            apush(arguments, reference);
        }
        for (int i = 0; i < count; i++) {
            apush(arguments, bank->values[i]);
        }
        execution_activation *a = program_activation_new(
            owner->instance,
            fn->body,
            arguments,
            acount(arguments),
            p->now,
            p->delta,
            p->budget
        );
        afree(arguments);
        if (!a) {
            return FLOW_ERR_ARGUMENT;
        }
        a->continuation_owner = owner;
        a->continuation_context = ctx;
        a->origin_action = action;
        ctx->pending_commands++;
        int status = program_activation_run(a, p->now, p->delta, p->budget);
        program_continuation_finish(a);
        if (!a->finished || acount(a->requests)) {
            apush(owner->instance->activations, a);
        } else {
            program_activation_free(a);
        }
        if (status < 0) {
            return status;
        }
        if (owner->finished || owner->stopping) {
            return FLOW_OK;
        }
    }
    return FLOW_OK;
}
static const execution_module_action *state_plan(
    behavior_function_context *ctx,
    uint64_t id,
    pin_value **state
)
{
    execution_instance *instance = ctx->value_owner;
    const execution_module_action *plan = action_plan(instance->program->module, id);
    if (!plan || plan->state_kind == NATIVE_STATE_NONE) {
        return NULL;
    }
    int index = (int)(plan - instance->program->module->plan.actions);
    assert(index >= 0 && index < acount(instance->node_states));
    *state = &instance->node_states[index];
    return plan;
}
static int state_inputs(behavior_function_context *ctx, uint64_t id, int entry, int *demand)
{
    pin_value *state;
    const execution_module_action *plan = state_plan(ctx, id, &state);
    if (!plan) {
        return FLOW_ERR_STATE;
    }
    *demand = native_state_inputs(plan->state_kind, entry, state);
    return *demand < 0 ? FLOW_ERR_STATE : FLOW_OK;
}
static int state_transition(
    behavior_function_context *ctx,
    uint64_t id,
    int entry,
    const pin_value *inputs,
    int count,
    int *selected
)
{
    pin_value *state;
    const execution_module_action *plan = state_plan(ctx, id, &state);
    if (!plan || count != native_state_input_count(plan->state_kind) ||
        native_state_inputs(plan->state_kind, entry, state) < 0) {
        return FLOW_ERR_STATE;
    }
    if (plan->state_kind == NATIVE_STATE_MULTI && !entry && state->type == PIN_FLOW) {
        execution_instance *instance = ctx->value_owner;
        char *mask = arena_alloc(&instance->selections, (size_t)plan->outcome_count + 1);
        memset(mask, '0', (size_t)plan->outcome_count);
        mask[plan->outcome_count] = 0;
        const char *error = pin_value_text(
            &instance->selections,
            state,
            mask,
            (size_t)plan->outcome_count
        );
        assert(!error);
        (void)error;
    }
    pin_value result = { 0 };
    *selected = native_state_transition(plan->state_kind, entry, state, inputs, plan->outcome_count, &result);
    if (*selected == NATIVE_STATE_INVALID) {
        return FLOW_ERR_STATE;
    }
    if (result.type != PIN_FLOW) {
        action_values *bank = action_bank(ctx, id);
        if (!bank || bank->count != 1) {
            return FLOW_ERR_STATE;
        }
        result.type_name = plan->defaults[0].type_name;
        if (!program_same_type(plan->defaults[0], result)) {
            return FLOW_ERR_STATE;
        }
        execution_activation *activation = ctx->inspection_user;
        if (pin_value_snapshot_to_arena(&activation->payload, &bank->values[0], &result)) {
            return FLOW_ERR_STATE;
        }
    }
    return FLOW_OK;
}
static int retrigger_action(behavior_function_context *ctx, uint64_t id, double duration, bool *found)
{
    pin_value *state;
    const execution_module_action *plan = state_plan(ctx, id, &state);
    if (!plan || plan->state_kind != NATIVE_STATE_RETRIGGER) {
        return FLOW_ERR_STATE;
    }
    execution_instance *instance = ctx->value_owner;
    execution_activation *current = ctx->inspection_user;
    execution_instance *origin = current->instance;
    *found = false;
    for (int i = 0; i < acount(origin->activations); i++) {
        execution_activation *activation = origin->activations[i];
        for (int j = 0; j < acount(activation->requests); j++) {
            command_wait *wait = activation->requests[j];
            if (wait->timer && wait->pending && !wait->delivered &&
                wait->context->value_owner == instance &&
                wait->function->commands[wait->command].node_id == id) {
                wait->deadline = ctx->now + duration;
                if (!activation->context.wake_time ||
                    wait->deadline < activation->context.wake_time) {
                    activation->context.wake_time = wait->deadline;
                }
                *found = true;
                return FLOW_OK;
            }
        }
    }
    return FLOW_OK;
}
void program_bind_commands(behavior_function_context *ctx)
{
    ctx->begin_command = begin_command;
    ctx->action_result = action_result;
    ctx->complete_action = program_complete_action;
    ctx->state_inputs = state_inputs;
    ctx->state_transition = state_transition;
    ctx->retrigger_action = retrigger_action;
    ctx->request_action_break = request_action_break;
    ctx->take_action_break = take_action_break;
}
