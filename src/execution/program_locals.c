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
/* A frame references the locals it owns. */
bool program_capture_reference(
    arena *storage,
    behavior_function_context *ctx,
    const execution_module_capture *capture,
    pin_value *out
)
{
    if (!ctx->data || capture->value < 0 ||
        capture->value >= acount(ctx->data->function->data_values)) {
        return false;
    }
    *out = execution_reference_value(
        storage,
        ctx,
        &ctx->data->values[capture->value],
        capture->type
    );
    return true;
}
