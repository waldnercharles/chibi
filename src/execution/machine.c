#include "execution/machine.h"

#include "ecs/execution.h"
#include "execution/borrow.h"
#include "execution/component_write.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/random.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/pointer.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local uint64_t vm_allocations;
uint64_t behavior_function_vm_allocations()
{
    return vm_allocations;
}

static void enter(const behavior_function *f, pin_value *locals, int frame, behavior_function_activation *activation)
{
    if (f->waits) {
        activation->wake_times[frame] = 0;
    }
    for (int i = 0; i < acount(f->locals); i++) {
        if (f->locals[i].frame == frame) {
            locals[i] = f->locals[i].initial;
        }
    }
}
static int fail(
    behavior_function_context *ctx,
    const behavior_function_instruction *i,
    uintptr_t steps,
    const uintptr_t *parents,
    behavior_function_failure reason
)
{
    ctx->node_id = i->node_id;
    ctx->error_frame = i->frame;
    ctx->error_source = i->source;
    ctx->error_stack = i->stack;
    ctx->error_pin = reason >= BEHAVIOR_FUNCTION_DATA ? i->pin : -1;
    ctx->error_steps = steps;
    ctx->steps = i->frame ? parents[0] : steps;
    ctx->failure = reason;
    if (i->op != BF_QUERY && i->op != BF_COMMAND_BEGIN && i->op != BF_HOST_CALL &&
        i->op != BF_COMPONENT_WRITE && i->op != BF_INTERN_STRING) {
        ctx->error_message = i->op == BF_QUERY_VARIABLE
                               ? "Query variable is not bound"
                           : i->op == BF_COMPONENT_READ
                               ? "Referenced storage is unavailable"
                               : i->message;
    }
    return reason == BEHAVIOR_FUNCTION_COMMAND   ? ctx->error_status
         : reason == BEHAVIOR_FUNCTION_ARGUMENT  ? FLOW_ERR_ARGUMENT
         : reason == BEHAVIOR_FUNCTION_STATE     ? FLOW_ERR_STATE
         : reason == BEHAVIOR_FUNCTION_HOST_FLOW ? FLOW_ERR_NODE
         : reason >= BEHAVIOR_FUNCTION_NO_RETURN ? FLOW_ERR_INVALID_GRAPH
                                                 : FLOW_ERR_STEP_BUDGET;
}

void execution_machine_begin(
    execution_machine *m,
    const behavior_function *f,
    behavior_function_context *ctx,
    bool compact
)
{
    int value_count = acount(f->slots) + acount(f->locals) + 1;
    int frame_count = acount(f->frames);
    if (f->fast) {
        int fast_values = acount(f->fast->slots) + acount(f->fast->locals) + 1;
        if (fast_values > value_count) {
            value_count = fast_values;
        }
        if (acount(f->fast->frames) > frame_count) {
            frame_count = acount(f->fast->frames);
        }
    }
    bool canonical = behavior_execution_scope_active() ||
                     behavior_function_inspection_required(f, ctx);
    if (compact && !canonical && behavior_function_fast_allowed(f, ctx)) {
        f = f->fast;
    }
    behavior_function_activation *activation = ctx->activation;
    behavior_function_vm_storage transient = { 0 };
    if (activation && !activation->vm) {
        activation->vm = xcalloc(1, sizeof *activation->vm);
    }
    behavior_function_vm_storage *storage = activation ? activation->vm : &transient;
    if (!storage->values) {
        vm_allocations++;
        storage->values = xcalloc((size_t)value_count, sizeof *storage->values);
        storage->parents = frame_count > 1
                             ? xcalloc((size_t)frame_count, sizeof *storage->parents)
                             : NULL;
    }
    pin_value *slots = storage->values, *locals = slots + acount(f->slots) + 1;
    uintptr_t *parents = storage->parents;
    uintptr_t steps = ctx->steps;
    int begin = 0;
    if (!activation || !activation->pc) {
        enter(f, locals, 0, activation);
    }
    if (activation && activation->pc) {
        begin = activation->pc;
        steps = activation->steps;
        int slot_count = acount(f->slots), base = slot_count + acount(f->locals);
        const behavior_function_suspension
            *s = &f->suspensions[f->instructions[begin - 1].target];
        assert(s->pc + 1 == begin);
        for (int k = 0; k < s->value_count; k++) {
            int v = s->values[k];
            pin_value *to = v < slot_count ? &slots[v] : &locals[v - slot_count];
            to->type = v < slot_count ? f->slots[v]
                                      : f->locals[v - slot_count].initial.type;
            memcpy(&to->d, &activation->values[v], sizeof(double));
        }
        for (int frame = f->frames[activation->frame].source.parent; frame >= 0;
             frame = f->frames[frame].source.parent) {
            parents[frame] = activation->values[base + frame];
        }
        activation->pc = 0;
    }
    const behavior_function_instruction *instructions = f->instructions;
    if (compact && !canonical && f->runtime_instructions) {
        instructions = f->runtime_instructions;
        begin = f->runtime_map[begin];
    }
    *m = (execution_machine){ .function = f,
                              .context = ctx,
                              .values = slots,
                              .parents = parents,
                              .steps = steps,
                              .pc = begin,
                              .instructions = instructions };
}

int execution_machine_step(execution_machine *m, int pc)
{
    const behavior_function *f = m->function;
    behavior_function_context *ctx = m->context;
    behavior_function_activation *activation = ctx->activation;
    pin_value *slots = m->values, *locals = slots + acount(f->slots) + 1;
    uintptr_t *parents = m->parents, steps = m->steps;
    const behavior_function_instruction *instructions = m->instructions;
    int next = pc + 1, result = EXECUTION_CONTINUE;
    const behavior_function_instruction *i = &instructions[pc];
    pin_value *out = &slots[i->output];
    pin_value a = i->left_slot ? slots[i->left] : (pin_value){ 0 };
    pin_value b = i->right_slot ? slots[i->right] : (pin_value){ 0 };
    if (i->op < BF_STORE) {
        out->type = f->slots[i->output];
    }
    if (i->indirect) {
        if (i->op == BF_STORE) {
            memcpy(
                (char *)ctx->variable_storage[i->target].data + i->offset,
                &a.d,
                behavior_scalar_size(i->type)
            );
            goto done;
        }
        if (i->op == BF_ARGUMENT && (i->left >= ctx->argument_count ||
                                     ctx->arguments[i->left].type != PIN_STRUCT)) {
            *out = i->value;
            goto done;
        }
        const pin_value *base = i->op == BF_ARGUMENT ? &ctx->arguments[i->left]
                              : i->op == BF_VARIABLE ? &ctx->variables[i->left]
                                                     : &ctx->results[i->target];
        size_t size = behavior_scalar_size(i->type);
        if (i->op == BF_RESULT) {
            if (base->ptr) {
                memcpy((char *)base->ptr + i->offset, &a.d, size);
            }
        } else {
            memset(&out->d, 0, sizeof out->d);
            if (base->ptr) {
                memcpy(&out->d, (const char *)base->ptr + i->offset, size);
            }
        }
        goto done;
    }
    switch (i->op) {
        case BF_WATCH_BEGIN:
            if (ctx->watch) {
                ctx->watched = ctx->watches[i->target].initial;
            }
            break;
        case BF_WATCH_VALUE:
            if (ctx->watch) {
                memcpy(
                    (i->right ? (char *)ctx->watch_storage : (char *)&ctx->watched) +
                        i->offset,
                    &a.d,
                    behavior_scalar_size(i->type)
                );
            }
            break;
        case BF_WATCH_PRESENT:
            if (ctx->watch) {
                ctx->watched.ptr = a.b ? ctx->watch_storage : NULL;
            }
            break;
        case BF_WATCH_RESET:
            if (ctx->watch) {
                ctx->watch(ctx->inspection_user, &ctx->watches[i->target], NULL);
            }
            break;
        case BF_WATCH_END:
            if (ctx->watch && a.b) {
                ctx->watch(ctx->inspection_user, &ctx->watches[i->target], &ctx->watched);
            }
            break;
        case BF_TRACE:
            if (ctx->trace) {
                ctx->trace(ctx->inspection_user, &ctx->edges[i->target], steps);
            }
            break;
        case BF_POINT:
            if (!ctx->inspect ||
                !ctx->inspect(ctx->inspection_user, &ctx->points[i->left])) {
                break;
            }
            goto suspend;
        case BF_SOURCE_WATCH:
            ctx->watched = a;
            behavior_source_capture(ctx, i->target);
            break;
        case BF_SOURCE_POINT:
            if (!behavior_source_pause(ctx, i->left)) {
                break;
            }
            goto suspend;
        case BF_DELAY:
            ctx->wake_time = ctx->now + a.d;
            if (ctx->wake_time <= ctx->now) {
                break;
            }
            activation->wake_times[i->frame] = ctx->wake_time;
            goto suspend;
        case BF_INVOKE:
        case BF_SUSPEND: {
        suspend:
            assert(activation && activation->values);
            int slot_count = acount(f->slots), base = slot_count + acount(f->locals);
            const behavior_function_suspension *s = &f->suspensions[i->target];
            for (int k = 0; k < s->value_count; k++) {
                int v = s->values[k];
                const pin_value *from = v < slot_count ? &slots[v]
                                                       : &locals[v - slot_count];
                memcpy(&activation->values[v], &from->d, sizeof(double));
            }
            for (int frame = f->frames[i->frame].source.parent; frame >= 0;
                 frame = f->frames[frame].source.parent) {
                activation->values[base + frame] = parents[frame];
            }
            activation->pc = s->pc + 1;
            activation->steps = steps;
            activation->frame = i->frame;
            activation->source = i->source;
            activation->stack = i->stack;
            activation->node_id = i->node_id;
            ctx->steps = i->frame ? parents[0] : steps;
            result = (i->op == BF_POINT || i->op == BF_SOURCE_POINT) ? FLOW_PAUSED_RESULT
                   : i->op == BF_INVOKE ? BEHAVIOR_FUNCTION_CALL_RESULT
                                        : FLOW_SUSPENDED_RESULT;
            goto done;
        }
        case BF_COMMAND_SERVICE: {
            uintptr_t pointer = (uintptr_t)ctx->commands[i->target].service;
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = pointer;
            } else {
                out->u = (uint32_t)pointer;
            }
            break;
        }
        case BF_COMMAND_RESULT:
            memcpy(
                &out->d,
                (const char *)&ctx->command_results[i->left] + i->offset,
                behavior_scalar_size(i->type)
            );
            break;
        case BF_COMMAND_ARGUMENT:
        case BF_COMMAND_OUTPUT:
            memcpy(
                (char *)&(
                    i->op == BF_COMMAND_OUTPUT ? ctx->command_results : ctx->command_arguments
                )[i->target] +
                    i->offset,
                &a.d,
                behavior_scalar_size(i->type)
            );
            break;
        case BF_COMMAND:
        case BF_COMMAND_BEGIN: {
            const behavior_function_command *spec = &f->commands[i->target];
            behavior_command_binding *c = &ctx->commands[i->target];
            ctx->error_status = i->op == BF_COMMAND_BEGIN
                                  ? ctx->begin_command(ctx, i->target)
                                  : c->invoke(
                                        c->service,
                                        c->reference,
                                        ctx->command_arguments + spec->first,
                                        spec->count
                                    );
            if (ctx->error_status < 0) {
                result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_COMMAND);
                goto done;
            }
            break;
        }
        case BF_STRING_EQUAL:
            out->b = f->slots[i->left] == PIN_STR
                       ? behavior_text_equal(a.text, b.text)
                       : behavior_string_equal(a.str, b.str);
            break;
        case BF_INDEX: {
            uintptr_t pointer = (uintptr_t)behavior_scalar_bits(a) +
                                (uintptr_t)(intptr_t)b.i * i->offset;
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = pointer;
            } else {
                out->u = (uint32_t)pointer;
            }
            break;
        }
        case BF_COPY_STRING: {
            if (i->type == PIN_STR) {
                const pin_string *text = a.text ? a.text : i->value.text;
                out->text = text ? ctx->copy_text(ctx->value_storage, text) : NULL;
            } else {
                const char *text = a.str ? a.str : i->value.str;
                out->str = text ? ctx->copy_string(ctx->value_storage, text) : NULL;
            }
            break;
        }
        case BF_COPY_ARRAY: {
            uintptr_t pointer = (uintptr_t)ctx->copy_array(
                ctx->value_storage,
                (const pin_array *)(uintptr_t)behavior_scalar_bits(a)
            );
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = pointer;
            } else {
                out->u = (uint32_t)pointer;
            }
            break;
        }
        case BF_STATE_SELECT:
            out->i = ctx->select_state(ctx->value_owner, &f->state_selections[i->target], a.i, b.i);
            break;
        case BF_RETRIGGER:
            out->b = ctx->retrigger(ctx, i->frame, i->target, a.d) != 0;
            break;
        case BF_INTERN_STRING: {
            const ecs_execution_view *world = (const ecs_execution_view *)(uintptr_t)
                behavior_scalar_bits(b);
            if (i->type == PIN_STR) {
                const pin_string *text = behavior_function_intern_text(ctx, world, a.text);
                if (!text) {
                    result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_HOST_DATA);
                    goto done;
                }
                out->text = text;
            } else {
                out->str = world->intern_string(world, a.str);
            }
            break;
        }
        case BF_STORE_VALUE:
            if (i->offset == BF_STORE_VALUE_SET_ADD) {
                ctx->add_set_value(ctx->value_owner, i->target, &ctx->data->values[i->left]);
            } else {
                ctx->store_value(ctx->value_owner, i->target, &ctx->data->values[i->left]);
            }
            break;
        case BF_HOST_SERVICE: {
            uintptr_t pointer = (uintptr_t)ctx->data->calls[i->target].service;
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = pointer;
            } else {
                out->u = (uint32_t)pointer;
            }
            break;
        }
        case BF_HOST_SLOT: {
            uintptr_t pointer = i->target ? (uintptr_t)ctx->data->buffers[i->left]
                                          : (uintptr_t)&ctx->data->values[i->left];
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = pointer;
            } else {
                out->u = (uint32_t)pointer;
            }
            break;
        }
        case BF_HOST_CALL: {
            const behavior_function_host_call *spec = &f->host_calls[i->target];
            behavior_data_binding *call = &ctx->data->calls[i->target];
            ctx->error_message = behavior_function_host_arguments(ctx, i->target);
            if (ctx->error_message) {
                result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_DATA);
                goto done;
            }
            if (call->action) {
                ctx->error_message = behavior_function_host_action(ctx, i->target, i->node_id);
                if (ctx->error_message) {
                    result = fail(ctx, i, steps, parents, i->left);
                    goto done;
                }
                break;
            }
            if (call->context) {
                ctx->node_id = i->node_id;
                ctx->error_message = call->context(
                    ctx,
                    call->service,
                    ctx->data->values + spec->first,
                    ctx->data->values + spec->result_first
                );
                if (ctx->error_message) {
                    result = fail(ctx, i, steps, parents, i->left);
                    goto done;
                }
                break;
            }
            ctx->error_message = i->right == BF_HOST_BORROW_ARRAY
                                   ? behavior_function_borrow_snapshot(ctx, i->target)
                               : call->value
                                   ? call->value(
                                         execution_intrinsic_in_result(spec->capability_id)
                                             ? ctx->result_storage
                                             : ctx->value_storage,
                                         ctx->data->values + spec->first,
                                         spec->result_first - spec->first,
                                         ctx->data->values + spec->result_first
                                     )
                                   : call->invoke(
                                         call->service,
                                         ctx->data->values + spec->first,
                                         ctx->data->values + spec->result_first
                                     );
            if (ctx->error_message) {
                result = fail(ctx, i, steps, parents, i->left);
                goto done;
            }
            break;
        }
        case BF_QUERY: {
            behavior_query_binding *query = &ctx->queries[i->target];
            ctx->error_message = query->read(query->service, query->key, &query->snapshot);
            if (ctx->error_message) {
                result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_HOST_DATA);
                goto done;
            }
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = (uintptr_t)&query->snapshot;
            } else {
                out->u = (uint32_t)(uintptr_t)&query->snapshot;
            }
            break;
        }
        case BF_ARRAY: {
            const pin_value *value = &i->value;
            if (i->target == BF_VARIABLE) {
                value = &ctx->variables[i->left];
            } else if (
                i->target == BF_ARGUMENT && i->left < ctx->argument_count &&
                ctx->arguments[i->left].type == i->value.type
            ) {
                value = &ctx->arguments[i->left];
            }
            uintptr_t pointer = value->container == i->value.container
                                  ? (uintptr_t)value->ptr
                                  : 0;
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = pointer;
            } else {
                out->u = (uint32_t)pointer;
            }
            break;
        }
        case BF_STORAGE_NAME:
        case BF_OWN_NAME: {
            const char *name = i->op == BF_STORAGE_NAME
                                 ? ctx->variable_storage[i->left].type_name
                             : !behavior_scalar_bits(a)
                                 ? NULL
                                 : ctx->own_type_name(
                                       ctx->value_owner,
                                       (const char *)(uintptr_t)behavior_scalar_bits(a)
                                   );
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = (uintptr_t)name;
            } else {
                out->u = (uint32_t)(uintptr_t)name;
            }
            break;
        }
        case BF_STORE_TYPE_NAME:
            ctx->variables[i->target].record = NULL;
            ctx->variables[i->target].type_name = (const char *)(uintptr_t)
                behavior_scalar_bits(a);
            break;
        case BF_STORE_HEADER:
            ctx->variables[i->target] = (pin_value){
                .type = PIN_STRUCT,
                .type_name = (const char *)(uintptr_t)behavior_scalar_bits(a),
                .ptr = b.b ? ctx->variable_storage[i->target].data : NULL
            };
            break;
        case BF_CONTEXT:
            if (i->target) {
                if (BF_POINTER_TYPE == PIN_U64) {
                    out->u64 = (uintptr_t)ctx;
                } else {
                    out->u = (uint32_t)(uintptr_t)ctx;
                }
            } else {
                memcpy(&out->d, (const char *)ctx + i->offset, behavior_scalar_size(i->type));
            }
            break;
        case BF_OWNER: out->u64 = ctx->owner; break;
        case BF_QUERY_VARIABLE:
            if (!ctx->query_variables) {
                result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_HOST_DATA);
                goto done;
            }
            out->u64 = ctx->query_variables[i->target - 1];
            ctx->error_message = NULL;
            break;
        case BF_WORLD:
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = (uintptr_t)(i->target ? ctx->binding_world : ctx->world);
            } else {
                out->u = (uint32_t)(uintptr_t)(i->target ? ctx->binding_world
                                                         : ctx->world);
            }
            break;
        case BF_ENTITY:
            out->u = ecs_execution_entity(
                (const ecs_execution_view *)(uintptr_t)behavior_scalar_bits(a),
                b.u64
            );
            break;
        case BF_COMPONENT_WRITE:
            ctx->error_message = behavior_component_write(ctx, i, &a.u64, &b.d);
            if (ctx->error_message) {
                result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_HOST_DATA);
                goto done;
            }
            break;
        case BF_COMPONENT_READ: {
            uint32_t row = ctx->world ? ecs_execution_entity(ctx->world, a.u64) : 0;
            const ecs_execution_column
                *column = ecs_execution_component(ctx->world, row, i->value.u64);
            if (!column || column->layout != i->layout || column->stride != i->offset) {
                result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_HOST_DATA);
                goto done;
            }
            memcpy(
                &out->d,
                (const char *)column->data + (size_t)row * column->stride + i->target,
                behavior_scalar_size(i->type)
            );
            ctx->error_message = NULL;
            break;
        }
        case BF_COLUMN:
        case BF_COLUMN_TRY:
        case BF_COMPONENT: {
            const ecs_execution_column *column = ctx->columns
                                                   ? ctx->columns[i->component]
                                                   : ecs_execution_find_column(
                                                         (const ecs_execution_view *)(uintptr_t)
                                                             behavior_scalar_bits(a),
                                                         i->value.u64
                                                     );
            if (i->op == BF_COMPONENT) {
                column = ecs_execution_present(column, b.u);
            }
            if (i->type == PIN_BOOL) {
                out->b = column != NULL;
                break;
            }
            if (column && (column->layout != i->layout || column->stride != i->offset)) {
                if (i->op == BF_COLUMN_TRY) {
                    out->u64 = 0;
                    break;
                }
                result = fail(ctx, i, steps, parents, i->target);
                goto done;
            }
            uintptr_t row = i->op != BF_COMPONENT ? (uintptr_t)column
                          : column ? (uintptr_t)column->data + b.u * column->stride
                                   : 0;
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = row;
            } else {
                out->u = (uint32_t)row;
            }
            break;
        }
        case BF_ROW: {
            const ecs_execution_column *column = ecs_execution_present(
                (const ecs_execution_column *)(uintptr_t)behavior_scalar_bits(a),
                b.u
            );
            uintptr_t row = column ? (uintptr_t)column->data + (size_t)b.u * i->offset
                                   : 0;
            if (BF_POINTER_TYPE == PIN_U64) {
                out->u64 = row;
            } else {
                out->u = (uint32_t)row;
            }
            break;
        }
        case BF_LOAD:
        case BF_READ:
            if (i->op == BF_READ && !behavior_scalar_bits(a)) {
                memset(&out->d, 0, sizeof out->d);
                break;
            }
            memcpy(
                &out->d,
                (const char *)(uintptr_t)behavior_scalar_bits(a) + i->offset,
                behavior_scalar_size(i->type)
            );
            break;
        case BF_WRITE:
            memcpy(
                (char *)(uintptr_t)behavior_scalar_bits(b) + i->offset,
                &a.d,
                behavior_scalar_size(i->type)
            );
            break;
        case BF_ASSERT:
            if (!behavior_scalar_bits(a)) {
                result = fail(ctx, i, steps, parents, i->target);
                goto done;
            }
            break;
        case BF_TYPE_NAME: {
            const char *name = i->value.str;
            if (i->target == BF_OWNER) {
                name = ctx->owner_type_name;
            } else if (i->target == BF_VARIABLE) {
                name = ctx->variables[i->left].type_name;
            } else if (
                i->target == BF_ARGUMENT && i->left < ctx->argument_count &&
                ctx->arguments[i->left].type == i->value.type
            ) {
                name = ctx->arguments[i->left].type_name;
            }
            if (i->type == PIN_U64) {
                out->u64 = (uintptr_t)name;
            } else {
                out->u = (uint32_t)(uintptr_t)name;
            }
            break;
        }
        case BF_RESULT_TYPE_NAME:
            ctx->results[i->target].record = NULL;
            ctx->results[i->target]
                .type_name = (const char *)(uintptr_t)(BF_POINTER_TYPE == PIN_U64
                                                           ? slots[i->left].u64
                                                           : slots[i->left].u);
            break;
        case BF_RESULT_ARRAY:
            ctx->results[i->target] = (pin_value){
                .type = i->type,
                .container = i->value.container,
                .type_name = (const char *)(uintptr_t)behavior_scalar_bits(b),
                .ptr = (void *)(uintptr_t)behavior_scalar_bits(a)
            };
            break;
        case BF_PRESENT:
            if (i->target == BF_ARGUMENT &&
                (i->left >= ctx->argument_count ||
                 ctx->arguments[i->left].type != PIN_STRUCT)) {
                *out = i->value;
            } else {
                out->b = (i->target == BF_ARGUMENT
                              ? ctx->arguments[i->left].ptr
                              : ctx->variables[i->left].ptr) != NULL;
            }
            break;
        case BF_SELECT:
            *out = slots[slots[i->left].b ? i->right : i->target];
            break;
        case BF_RESULT_PRESENT:
            if (!slots[i->left].b) {
                ctx->results[i->target].ptr = NULL;
            }
            break;
        case BF_REQUIRE:
            if (!slots[i->left].b) {
                result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_DATA);
                goto done;
            }
            break;
#define MATH_UNARY(op, name, fn)                                               \
    case BF_##op: out->d = fn(a.d); break;
            BEHAVIOR_MATH_UNARY(MATH_UNARY)
#undef MATH_UNARY
#define MATH_BINARY(op, name, fn)                                              \
    case BF_##op: out->d = fn(a.d, b.d); break;
            BEHAVIOR_MATH_BINARY(MATH_BINARY)
#undef MATH_BINARY
        case BF_MOD_F32: out->f = fmodf(a.f, b.f); break;
        case BF_MOD_F64: out->d = fmod(a.d, b.d); break;
        case BF_HYPOT: out->d = hypot(a.d, b.d); break;
        case BF_FMA: out->d = fma(a.d, b.d, slots[i->target].d); break;
        case BF_RANDOM: out->i = rand(); break;
        case BF_RANDOM_RANGE: out->d = behavior_random_range(a.d, b.d); break;
        case BF_CONSTANT: *out = i->value; break;
        case BF_VARIABLE: *out = ctx->variables[i->left]; break;
        case BF_LOCAL: *out = locals[i->left]; break;
        case BF_DELTA: out->f = (float)ctx->delta_seconds; break;
        case BF_ARGUMENT:
            *out = behavior_function_argument(ctx->arguments, ctx->argument_count, i->left, i->value);
            break;
        case BF_CAST:
            behavior_scalar_cast(a, i->type, (behavior_cast_mode)i->target, out);
            if (f->integer_reports) {
                behavior_integer_report(f, i, a, b);
            }
            break;
        case BF_FLAGS: out->b = (a.u & b.u) != 0; break;
        case BF_CONVERT:
            if (i->type == PIN_F32) {
                out->f = (float)a.d;
            } else {
                out->d = (double)a.f;
            }
            break;
        case BF_SAVE:
            locals[i->target] = a;
            if (behavior_function_inspectable(f) && f->locals[i->target].parameter) {
                memcpy(
                    &activation->values[acount(f->slots) + i->target],
                    &a.d,
                    behavior_scalar_size(i->type)
                );
            }
            break;
        case BF_ENTER:
            if (behavior_function_inspectable(f)) {
                activation->values[acount(f->slots) + acount(f->locals) + i->frame] = steps;
            }
            parents[i->frame] = steps;
            steps = 0;
            enter(f, locals, i->target, activation);
            break;
        case BF_LEAVE:
            if (behavior_function_inspectable(f)) {
                activation->values[acount(f->slots) + acount(f->locals) + i->target] = steps;
            }
            steps = parents[f->frames[i->target].source.parent];
            break;
        case BF_CALL_CHECK: {
            unsigned depth = (unsigned)f->frames[i->frame].depth;
            if (depth >= (unsigned)i->left ||
                ctx->call_depth >= (unsigned)i->left - depth) {
                result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_DEPTH);
                goto done;
            }
            break;
        }
        case BF_FAIL: result = fail(ctx, i, steps, parents, i->left); goto done;
        case BF_RESULT:
        case BF_STORE: {
            pin_value *target = i->op == BF_STORE ? ctx->variables : ctx->results;
            switch (i->type) {
#define STORE(pin, type, field)                                                \
    case pin: target[i->target].field = a.field; break;
                BEHAVIOR_SCALARS(STORE)
#undef STORE
                default: assert(false);
            }
            break;
        }
        case BF_CHECK:
            ctx->node_id = i->node_id;
            if (steps >= ctx->budget) {
                result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_BUDGET);
                goto done;
            }
            steps++;
            break;
        case BF_JUMP: next = i->target; break;
        case BF_BRANCH:
            if (a.b) {
                next = i->target;
            }
            break;
        case BF_END: result = FLOW_OK; goto done;
        case BF_RETURN:
            ctx->returned = true;
            ctx->returned_node_id = i->node_id;
            result = FLOW_OK;
            goto done;
        default: {
            if (behavior_function_bitwise(i->op)) {
                *out = behavior_scalar_from_bits(
                    i->type,
                    behavior_scalar_bitwise_bits(
                        i->op,
                        i->type,
                        behavior_scalar_bits(a),
                        i->op == BF_BIT_NOT ? 0 : behavior_scalar_bits(b)
                    )
                );
                break;
            }
            if ((i->op == BF_EQ || i->op == BF_NE) && a.type != PIN_F32 &&
                a.type != PIN_F64) {
                bool equal = a.type == PIN_POINTER
                               ? pin_pointer_equal(a.ptr, b.ptr)
                               : behavior_scalar_bits(a) == behavior_scalar_bits(b);
                out->b = i->op == BF_EQ ? equal : !equal;
                break;
            }
            if (a.type >= PIN_I8 && a.type <= PIN_U64) {
                pin_value *other = (i->op == BF_DIV || i->op == BF_MOD) && i->target >= 0
                                     ? &slots[i->target]
                                     : NULL;
                behavior_scalar_integer_op(i->op, a, b, out, other);
                if (f->integer_reports) {
                    behavior_integer_report(f, i, a, b);
                }
                break;
            }
            double x = a.type == PIN_F32 ? a.f : a.d,
                   y = b.type == PIN_F32 ? b.f : b.d, r = 0;
            switch (i->op) {
                case BF_ADD:
                    r = i->type == PIN_F32 ? (float)(a.f + b.f) : x + y;
                    break;
                case BF_SUB:
                    r = i->type == PIN_F32 ? (float)(a.f - b.f) : x - y;
                    break;
                case BF_MUL:
                    r = i->type == PIN_F32 ? (float)(a.f * b.f) : x * y;
                    break;
                case BF_DIV:
                    r = i->type == PIN_F32 ? (float)(a.f / b.f) : x / y;
                    break;
                case BF_NEG: r = -x; break;
                case BF_ABS: r = fabs(x); break;
                case BF_MIN: r = x < y ? x : y; break;
                case BF_MAX: r = x > y ? x : y; break;
                case BF_LT: out->b = x < y; break;
                case BF_LE: out->b = x <= y; break;
                case BF_GT: out->b = x > y; break;
                case BF_GE: out->b = x >= y; break;
                case BF_EQ: out->b = x == y; break;
                case BF_NE: out->b = x != y; break;
                default: assert(false);
            }
            if (out->type == PIN_F32) {
                out->f = (float)r;
            }
            if (out->type == PIN_F64) {
                out->d = r;
            }
        }
    }

done:
    m->steps = steps;
    m->pc = next;
    return result;
}

int execution_machine_fail(execution_machine *m, const char *message)
{
    int result = fail(m->context, &m->instructions[m->pc], m->steps, m->parents, BEHAVIOR_FUNCTION_BUDGET);
    m->context->error_message = message;
    return result;
}

void execution_machine_end(execution_machine *m, int result)
{
    if (!result) {
        m->context->steps = m->steps + m->function->root_steps;
    }
    if (!m->context->activation) {
        free(m->parents);
        free(m->values);
    }
}
