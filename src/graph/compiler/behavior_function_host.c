#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/eval.h"
#include "support/array.h"

#include <stdlib.h>
#include <string.h>

static void *service(const service_binding *services, const char *name)
{
    for (int i = 0; i < acount(services); i++) {
        if (!strcmp(services[i].type_name, name)) {
            return services[i].value;
        }
    }
    return NULL;
}
void behavior_function_prepare_host(const behavior_function *f, behavior_function_context *ctx)
{
    assert(!ctx->queries && !ctx->commands && !ctx->command_arguments);
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
                .service = service(ctx->services, c->command->service),
                .reference = c->reference })
        );
    }
    for (int i = 0; i < acount(f->command_arguments); i++) {
        apush(ctx->command_arguments, f->command_arguments[i]);
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
}
