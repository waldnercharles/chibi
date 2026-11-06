#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/service.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/trace.h"

#include <stdlib.h>
#include <string.h>

static void *service(const service_binding *services, const char *name)
{
    if (!name) {
        return NULL;
    }
    for (int i = 0; i < acount(services); i++) {
        if (!strcmp(services[i].type_name, name)) {
            return services[i].value;
        }
    }
    return NULL;
}
static _Thread_local uint64_t data_allocations;
uint64_t behavior_function_data_allocations()
{
    return data_allocations;
}
void behavior_function_free_data(behavior_data_storage *data)
{
    if (!data) {
        return;
    }
    for (int i = 0; i < acount(data->buffers); i++) {
        free(data->buffers[i]);
    }
    afree(data->buffers);
    afree(data->buffered);
    afree(data->initial);
    afree(data->values);
    afree(data->calls);
    free(data);
}
void behavior_function_reset_data(behavior_data_storage *data)
{
    if (!data) {
        return;
    }
    const behavior_function *f = data->function;
    if (data->initial) {
        memcpy(
            data->values,
            data->initial,
            (size_t)acount(data->initial) * sizeof *data->values
        );
    }
    for (int i = 0; i < acount(data->buffered); i++) {
        int cell = data->buffered[i];
        memset(data->buffers[cell], 0, f->data_values[cell].size);
    }
}
size_t behavior_function_data_storage_bytes(const behavior_data_storage *data)
{
    if (!data) {
        return 0;
    }
    size_t bytes = sizeof *data;
#define VECTOR(values)                                                         \
    ((values) ? sizeof(dyna_hdr) + (size_t)acap(values) * sizeof *(values) : 0)
    bytes += VECTOR(data->values) + VECTOR(data->initial) +
             VECTOR(data->buffers) + VECTOR(data->buffered) + VECTOR(data->calls);
#undef VECTOR
    return bytes + data->buffer_bytes;
}
size_t behavior_function_host_storage_bytes(const behavior_function_context *ctx)
{
    size_t bytes = behavior_function_data_storage_bytes(ctx->data);
#define VECTOR(values)                                                         \
    ((values) ? sizeof(dyna_hdr) + (size_t)acap(values) * sizeof *(values) : 0)
    bytes += VECTOR(ctx->queries) + VECTOR(ctx->commands) +
             VECTOR(ctx->command_arguments) + VECTOR(ctx->command_results) +
             VECTOR(ctx->collection_owners);
#undef VECTOR
    for (int i = 0; i < acount(ctx->queries); i++) {
        bytes += (size_t)ctx->queries[i].snapshot.capacity * sizeof(pin_value);
    }
    return bytes;
}
void behavior_function_prepare_data(
    const behavior_function *f,
    const service_binding *services,
    behavior_data_storage **storage
)
{
    if (*storage && (*storage)->function != f) {
        behavior_function_free_data(*storage);
        *storage = NULL;
    }
    if (!acount(f->host_calls) && !acount(f->data_values) && !acount(f->calls)) {
        return;
    }
    if (!*storage) {
        data_allocations++;
        behavior_data_storage *data = xcalloc(1, sizeof *data);
        data->function = f;
        for (int i = 0; i < acount(f->host_calls); i++) {
            apush(
                data->calls,
                ((behavior_data_binding){ .capability_abi = f->host_calls[i].capability_abi,
                                          .invoke = f->host_calls[i].call->invoke,
                                          .value = f->host_calls[i].call->value,
                                          .context = f->host_calls[i].call->context,
                                          .action = f->host_calls[i].call->action })
            );
        }
        for (int i = 0; i < acount(f->data_values); i++) {
            behavior_function_data_value v = f->data_values[i];
            void *buffer = v.size ? xcalloc(1, v.size) : NULL;
            if (v.size) {
                v.initial.ptr = buffer;
                data->buffer_bytes += v.size;
                apush(data->buffered, i);
            }
            apush(data->values, v.initial);
            apush(data->initial, v.initial);
            apush(data->buffers, buffer);
        }
        /* Keep zero-argument, zero-result calls on a valid bank base. */
        if (!data->values) {
            apush(data->values, ((pin_value){ 0 }));
        }
        *storage = data;
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        (*storage)->calls[i].service = service(services, f->host_calls[i].call->service);
    }
}
void behavior_function_prepare_host(const behavior_function *f, behavior_function_context *ctx)
{
    assert(
        !acount(ctx->queries) && !acount(ctx->commands) &&
        !acount(ctx->command_arguments) && !acount(ctx->command_results)
    );
    ctx->points = f->points;
    ctx->watches = f->watches;
    ctx->edges = f->edges;
    if (ctx->data || acount(f->host_calls) || acount(f->data_values) ||
        acount(f->calls)) {
        bool owned = ctx->owned_data || !ctx->data;
        behavior_function_prepare_data(f, ctx->services, &ctx->data);
        ctx->owned_data = owned;
    }
    for (int i = 0; i < acount(f->queries); i++) {
        const behavior_function_query *q = &f->queries[i];
        apush(
            ctx->queries,
            ((behavior_query_binding){
                .read = q->read,
                .key = q->key,
                .service = service(ctx->services, q->service),
                .snapshot = { .element_type = PIN_HANDLE, .type_name = "ecs.entity" } })
        );
    }
    for (int i = 0; i < acount(f->commands); i++) {
        const behavior_function_command *c = &f->commands[i];
        apush(
            ctx->commands,
            ((behavior_command_binding){
                .invoke = c->command->invoke,
                .begin = c->command->begin,
                .service = service(ctx->services, c->command->service),
                .reference = c->reference,
                .node_id = c->node_id,
                .frame = c->frame,
                .first = c->first,
                .count = c->count,
                .result_first = c->result_first })
        );
    }
    for (int i = 0; i < acount(f->command_arguments); i++) {
        apush(ctx->command_arguments, f->command_arguments[i]);
    }
    for (int i = 0; i < acount(f->command_results); i++) {
        apush(ctx->command_results, f->command_results[i]);
    }
}
void behavior_function_release_host(behavior_function_context *ctx)
{
    behavior_function_collection_clear(ctx);
    behavior_function_restore_arguments(ctx);
    for (int i = 0; i < acount(ctx->queries); i++) {
        free(ctx->queries[i].snapshot.items);
    }
    afree(ctx->queries);
    afree(ctx->commands);
    afree(ctx->command_arguments);
    afree(ctx->command_results);
    if (ctx->owned_data) {
        behavior_function_free_data(ctx->data);
        ctx->data = NULL;
        ctx->owned_data = false;
    }
    ctx->owned_host = false;
}
void behavior_function_reset_host(behavior_function_context *ctx)
{
    behavior_function_collection_empty(ctx);
    for (int i = 0; i < acount(ctx->queries); i++) {
        free(ctx->queries[i].snapshot.items);
    }
#define CLEAR(values)                                                          \
    do {                                                                       \
        if (values) {                                                          \
            memset(values, 0, (size_t)acount(values) * sizeof *(values));      \
            asetlen(values, 0);                                                \
        }                                                                      \
    } while (0)
    CLEAR(ctx->queries);
    CLEAR(ctx->commands);
    CLEAR(ctx->command_arguments);
    CLEAR(ctx->command_results);
#undef CLEAR
    behavior_function_reset_data(ctx->data);
    if (ctx->data) {
        for (int i = 0; i < acount(ctx->data->calls); i++) {
            ctx->data->calls[i].service = NULL;
        }
    }
    *ctx = (behavior_function_context){ .data = ctx->data,
                                        .owned_data = ctx->owned_data,
                                        .queries = ctx->queries,
                                        .commands = ctx->commands,
                                        .command_arguments = ctx->command_arguments,
                                        .command_results = ctx->command_results,
                                        .collection_owners = ctx->collection_owners };
}

void behavior_function_rebind_host(
    const behavior_function *f,
    behavior_function_context *ctx,
    const service_binding *services
)
{
    ctx->services = services;
    if (ctx->data) {
        behavior_function_prepare_data(f, services, &ctx->data);
    }
    for (int i = 0; i < acount(f->queries); i++) {
        ctx->queries[i].service = service(services, f->queries[i].service);
    }
    for (int i = 0; i < acount(f->commands); i++) {
        ctx->commands[i].service = service(services, f->commands[i].command->service);
    }
}

static bool bound_to(const behavior_data_binding *binding, const node_host_call *call)
{
    return binding->invoke == call->invoke && binding->value == call->value &&
           binding->context == call->context && binding->action == call->action;
}
static bool language_values(const behavior_function_host_call *spec, const behavior_data_binding *binding)
{
    const node_host_call *call = spec->call;
    return call->language_values && call->value && !call->service &&
           spec->capability_abi && binding->capability_abi == spec->capability_abi &&
           bound_to(binding, call);
}
const char *behavior_function_host_arguments(behavior_function_context *ctx, int target)
{
    const behavior_function *f = ctx->data->function;
    assert(target >= 0 && target < acount(f->host_calls));
    const behavior_function_host_call *spec = &f->host_calls[target];
    const behavior_data_binding *binding = &ctx->data->calls[target];
    const node_host_call *canonical = execution_intrinsic_resolve(spec->capability_id);
    if (canonical &&
        spec->capability_abi == execution_intrinsic_abi(spec->capability_id) &&
        bound_to(binding, canonical)) {
        return NULL;
    }
    if (spec->call->borrows && bound_to(binding, spec->call)) {
        return NULL;
    }
    bool language = language_values(spec, binding);
    for (int i = spec->first; i < spec->result_first; i++) {
        const char
            *error = language
                       ? (ctx->managed_heap
                              ? behavior_managed_storage_owner_error(
                                    &ctx->managed_heap->storage,
                                    &ctx->data->values[i]
                                )
                              : "Language sequence requires a managed heap")
                       : pin_value_managed_boundary_error(&ctx->data->values[i]);
        if (error) {
            return error;
        }
    }
    return NULL;
}
const char *behavior_function_host_results(behavior_function_context *ctx, int target)
{
    const behavior_function *f = ctx->data->function;
    assert(target >= 0 && target < acount(f->host_calls));
    const behavior_function_host_call *spec = &f->host_calls[target];
    const behavior_data_binding *binding = &ctx->data->calls[target];
    const node_host_call *canonical = execution_intrinsic_resolve(spec->capability_id);
    bool intrinsic = canonical &&
                     spec->capability_abi ==
                         execution_intrinsic_abi(spec->capability_id) &&
                     binding->invoke == canonical->invoke &&
                     binding->value == canonical->value &&
                     binding->context == canonical->context &&
                     binding->action == canonical->action;
    pin_value *results = ctx->data->values + spec->result_first;
    bool language = language_values(spec, binding);
    if (!intrinsic && !language) {
        for (int i = 0; i < spec->result_count; i++) {
            const char *error = pin_value_managed_boundary_error(&results[i]);
            if (error) {
                memset(results, 0, (size_t)spec->result_count * sizeof *results);
                return error;
            }
        }
    }
    if (language) {
        for (int i = 0; i < spec->result_count; i++) {
            const char *error = ctx->managed_heap
                                  ? behavior_managed_storage_value_error(
                                        &ctx->managed_heap->storage,
                                        &results[i]
                                    )
                                  : "Language sequence requires a managed heap";
            if (error) {
                memset(results, 0, (size_t)spec->result_count * sizeof *results);
                return error;
            }
        }
    }
    if (!intrinsic) {
        for (int i = 0; i < spec->result_count; i++) {
            const char *error = behavior_function_validate_value(ctx, &results[i]);
            if (error) {
                memset(results, 0, (size_t)spec->result_count * sizeof *results);
                return error;
            }
        }
    }
    if (!intrinsic && !binding->value) {
        arena *storage = ctx->managed_heap ? &ctx->managed_heap->storage
                                           : ctx->value_storage;
        for (int i = 0; i < spec->result_count; i++) {
            pin_value copy;
            if (!pin_value_holds_storage(&results[i])) {
                continue;
            }
            if (!storage || pin_value_collection_copy(storage, &copy, &results[i])) {
                memset(results, 0, (size_t)spec->result_count * sizeof *results);
                return "Cannot copy native result bank";
            }
            results[i] = copy;
        }
    }
    if (!intrinsic) {
        for (int i = 0; i < spec->result_count; i++) {
            behavior_function_collection_retain(ctx, &results[i]);
        }
    }
    if (spec->owned_arguments) {
        for (int i = spec->first; i < spec->result_first; i++) {
            behavior_function_collection_drop(ctx, &ctx->data->values[i]);
        }
    }
    return NULL;
}
const char *behavior_function_host_action(behavior_function_context *ctx, int target, uint64_t node_id)
{
    const behavior_function *f = ctx->data->function;
    assert(target >= 0 && target < acount(f->host_calls));
    const behavior_function_host_call *spec = &f->host_calls[target];
    behavior_data_binding *binding = &ctx->data->calls[target];
    assert(binding->action && spec->outcome_count > 0);
    const char *argument_error = behavior_function_host_arguments(ctx, target);
    if (argument_error) {
        return argument_error;
    }
    int selected = -1;
    ctx->node_id = node_id;
    ctx->returned_outcome = 0;
    const char *error = binding->action(
        ctx,
        binding->service,
        ctx->data->values + spec->first,
        spec->result_first - spec->first,
        ctx->data->values + spec->result_first,
        spec->result_count,
        spec->outcomes,
        spec->outcome_count,
        &selected
    );
    if (error) {
        return error;
    }
    if (selected < 0 || selected >= spec->outcome_count) {
        return "Action selected an invalid outcome";
    }
    ctx->returned_outcome = spec->outcomes[selected];
    return NULL;
}
