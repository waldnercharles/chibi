#include "execution/definition.h"
#include "execution/service.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

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
    for (int i = 0; i < acount(f->data_values); i++) {
        behavior_function_data_value v = f->data_values[i];
        data->values[i] = v.initial;
        if (v.size) {
            memset(data->buffers[i], 0, v.size);
            data->values[i].ptr = data->buffers[i];
        }
    }
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
                ((behavior_data_binding){ .invoke = f->host_calls[i].call->invoke,
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
            }
            apush(data->values, v.initial);
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
    assert(!ctx->queries && !ctx->commands && !ctx->command_arguments && !ctx->command_results);
    ctx->points = f->points;
    ctx->watches = f->watches;
    ctx->edges = f->edges;
    if (!ctx->data &&
        (acount(f->host_calls) || acount(f->data_values) || acount(f->calls))) {
        behavior_function_prepare_data(f, ctx->services, &ctx->data);
        ctx->owned_data = true;
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

const char *behavior_function_host_action(behavior_function_context *ctx, int target, uint64_t node_id)
{
    const behavior_function *f = ctx->data->function;
    assert(target >= 0 && target < acount(f->host_calls));
    const behavior_function_host_call *spec = &f->host_calls[target];
    behavior_data_binding *binding = &ctx->data->calls[target];
    assert(binding->action && spec->outcome_count > 0);
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
