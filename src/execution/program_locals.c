#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/component.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
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

static const execution_module_function *context_function(behavior_function_context *ctx)
{
    if (!ctx->data) {
        return NULL;
    }
    const execution_module_plan *plan = &((execution_instance *)ctx->value_owner)
                                             ->program->module->plan;
    for (int i = 0; i < plan->function_count; i++) {
        if (plan->functions[i].body == ctx->data->function) {
            return &plan->functions[i];
        }
    }
    return NULL;
}
static const execution_module_function *event_function(const execution_module *module, uint64_t id)
{
    for (int i = 0; i < module->plan.function_count; i++) {
        const execution_module_function *function = &module->plan.functions[i];
        if (function->id == id && function->event) {
            return function;
        }
    }
    return NULL;
}
static const pin_value *context_event_argument(
    const behavior_function_context *ctx,
    const behavior_function *function,
    int ordinal
)
{
    if (!ctx || !ctx->data || ctx->data->function != function ||
        ordinal >= ctx->argument_count) {
        return NULL;
    }
    return &ctx->arguments[ordinal];
}
const pin_value *program_event_argument(behavior_function_context *ctx, uint64_t id, int ordinal)
{
    if (ordinal < 0) {
        return NULL;
    }
    execution_activation *activation = ctx->inspection_user;
    execution_program *program = activation->instance->program->owner_program;
    const execution_module_function *function = event_function(program->module, id);
    for (int i = 0; !function && i < acount(program->imports); i++) {
        function = event_function(program->imports[i]->module, id);
    }
    if (!function || ordinal >= function->body->argument_count) {
        return NULL;
    }
    const pin_value *value = context_event_argument(ctx, function->body, ordinal);
    if (value) {
        return value;
    }
    for (execution_activation *owner = activation; owner;
         owner = owner->continuation_owner) {
        value = context_event_argument(&owner->context, function->body, ordinal);
        if (!value) {
            value = context_event_argument(owner->continuation_context, function->body, ordinal);
        }
        if (value) {
            return value;
        }
    }
    return &function->body->argument_defaults[ordinal];
}
pin_value *program_capture_value(behavior_function_context *ctx, const execution_module_capture *capture)
{
    if (!ctx->data) {
        return NULL;
    }
    int value = capture->value;
    if (value < 0) {
        const execution_module_function *fn = context_function(ctx);
        if (!fn) {
            return NULL;
        }
        for (int i = 0; i < fn->local_count; i++) {
            if (fn->locals[i].id == capture->id) {
                if (capture->field < 0 || capture->field >= fn->locals[i].result_count) {
                    return NULL;
                }
                value = fn->locals[i].value + capture->field;
                break;
            }
        }
    }
    return value >= 0 && value < acount(ctx->data->function->data_values)
             ? &ctx->data->values[value]
             : NULL;
}
static int local_index(execution_activation *owner, behavior_function_context *ctx, int value)
{
    for (int i = 0; i < acount(owner->locals); i++) {
        if (owner->locals[i].context == ctx && owner->locals[i].value == value) {
            return i;
        }
    }
    int index = acount(owner->locals);
    apush(owner->locals, ((local_value){ .context = ctx, .value = value }));
    return index;
}
int program_store_local_group(behavior_function_context *ctx, pin_value *first, int count, bool stored)
{
    const execution_module_function *fn = context_function(ctx);
    if (!fn || count <= 0) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = 0; i < fn->local_count; i++) {
        const execution_module_local *plan = &fn->locals[i];
        if (plan->result_count == count && &ctx->data->values[plan->value] == first) {
            if (!stored) {
                return FLOW_OK;
            }
            execution_activation *owner = ctx->inspection_user;
            int index = local_index(owner, ctx, plan->value);
            if (owner->locals[index].initializing) {
                return FLOW_ERR_STATE;
            }
            owner->locals[index].ready = true;
            return FLOW_OK;
        }
    }
    return FLOW_ERR_ARGUMENT;
}
int program_initialize_local(behavior_function_context *ctx, pin_value *header, bool stored)
{
    const execution_module_function *fn = context_function(ctx);
    if (!fn) {
        return FLOW_OK;
    }
    const execution_module_local *plan = NULL;
    int field = -1;
    for (int i = 0; i < fn->local_count; i++) {
        for (int j = 0; j < fn->locals[i].result_count; j++) {
            if (&ctx->data->values[fn->locals[i].value + j] == header) {
                plan = &fn->locals[i];
                field = j;
                break;
            }
        }
        if (plan) {
            break;
        }
    }
    if (!plan) {
        return FLOW_OK;
    }
    execution_activation *owner = ctx->inspection_user;
    int index = local_index(owner, ctx, plan->value);
    if (stored && plan->result_count == 1) {
        owner->locals[index].ready = true;
        return FLOW_OK;
    }
    if (owner->locals[index].ready) {
        return FLOW_OK;
    }
    if (owner->locals[index].initializing) {
        ctx->error_message = "lazy local initialization is recursive";
        return FLOW_ERR_STATE;
    }
    owner->locals[index].initializing = true;
    pin_value replacement = { 0 };
    if (stored && pin_value_snapshot_to_arena(&owner->payload, &replacement, header)) {
        owner->locals[index].initializing = false;
        return FLOW_ERR_ARGUMENT;
    }
    pin_value *arguments = NULL;
    for (int i = 0; i < plan->capture_count; i++) {
        apush(arguments, ctx->arguments[i]);
    }
    for (int i = 0; i < plan->local_capture_count; i++) {
        const execution_module_capture *capture = &plan->captures[i];
        pin_value *value = program_capture_value(ctx, capture);
        if (!value) {
            afree(arguments);
            owner->locals[index].initializing = false;
            return FLOW_ERR_STATE;
        }
        apush(
            arguments,
            execution_reference_value(&owner->payload, ctx, value, capture->type)
        );
    }
    execution_program *p = owner->instance->program;
    execution_activation *a = program_activation_new(
        owner->instance,
        plan->initializer,
        arguments,
        acount(arguments),
        p->now,
        p->delta,
        p->budget
    );
    afree(arguments);
    int status = FLOW_ERR_ARGUMENT;
    if (a) {
        a->initializer = true;
        a->continuation_owner = owner;
        a->continuation_context = ctx;
        ctx->pending_commands++;
        status = program_activation_run(a, p->now, p->delta, p->budget);
        if (status == FLOW_OK) {
            pin_value *results = xcalloc((size_t)plan->result_count, sizeof *results);
            for (int i = 0; i < plan->result_count; i++) {
                if (pin_value_snapshot_to_arena(
                        &owner->payload,
                        &results[i],
                        &a->context.results[i]
                    )) {
                    status = FLOW_ERR_ARGUMENT;
                    break;
                }
            }
            if (status == FLOW_OK) {
                memcpy(&ctx->data->values[plan->value], results, (size_t)plan->result_count * sizeof *results);
                if (stored) {
                    ctx->data->values[plan->value + field] = replacement;
                }
            }
            free(results);
        } else if (status > 0) {
            status = FLOW_ERR_STATE;
        }
        program_activation_free(a);
    }
    owner->locals[index].initializing = false;
    owner->locals[index].ready = status == FLOW_OK;
    return status;
}
