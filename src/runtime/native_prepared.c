#include "runtime/native_prepared.h"

#include "runtime/input_action.h"

#include <assert.h>

static bool host(void *user, uint64_t id, execution_prepared_host *out)
{
    const native_prepared_context *context = user;
    const native_capability_binding *binding = native_capabilities_find(context->native, id, 0);
    if (!binding || !binding->call) {
        return false;
    }
    *out = (execution_prepared_host){ .call = binding->call,
                                      .access = binding->access,
                                      .abi = binding->abi };
    return true;
}
static bool command(void *user, uint64_t id, const char *, execution_prepared_command *out)
{
    const native_prepared_context *context = user;
    const native_capability_binding *binding = native_capabilities_find(context->native, id, 0);
    if (!binding || !binding->command) {
        return false;
    }
    *out = (execution_prepared_command){ .command = binding->command,
                                         .abi = binding->abi };
    return true;
}
static bool query(void *user, uint64_t id, execution_prepared_query *out)
{
    const native_prepared_context *context = user;
    const native_capability_binding *binding = native_capabilities_find(context->native, id, 0);
    if (!binding || !binding->query || !binding->query_service) {
        return false;
    }
    *out = (execution_prepared_query){ .read = binding->query,
                                       .service = binding->query_service,
                                       .abi = binding->abi };
    return true;
}
static bool event(void *user, uint64_t id, uint64_t abi)
{
    const native_prepared_context *context = user;
    const script_catalog *catalog = native_capabilities_catalog(context->native);
    const script_catalog_capability *declaration = script_catalog_capability_by_id(catalog, id);
    if (declaration && declaration->kind == SCRIPT_CAPABILITY_EVENT) {
        return abi == script_catalog_capability_abi(catalog, declaration);
    }
    for (int i = 0; context->events && i < game_event_catalog_count(context->events);
         i++) {
        execution_module_event_info declaration = game_event_catalog_at(context->events, i);
        if (declaration.id == id) {
            return declaration.abi == abi;
        }
    }
    return false;
}
static bool type(void *user, uint64_t id, execution_prepared_type *out)
{
    const char *input[] = { INPUT_MAP_TYPE, INPUT_ACTION_TYPE };
    for (int i = 0; i < 2; i++) {
        if (id == script_catalog_id("type", input[i])) {
            *out = (execution_prepared_type){ .name = input[i], .storage = PIN_STR };
            return true;
        }
    }
    const native_prepared_context *context = user;
    const script_catalog *catalog = native_capabilities_catalog(context->native);
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *t = &catalog->types[i];
        if (t->id != id) {
            continue;
        }
        const type_desc *layout = native_capabilities_layout(context->native, id);
        if (t->type.storage == PIN_STRUCT && !layout) {
            return false;
        }
        *out = (execution_prepared_type){ .name = t->name,
                                          .type = layout,
                                          .storage = t->type.storage,
                                          .size = t->size,
                                          .layout = t->layout };
        return true;
    }
    return false;
}
execution_prepared_linker native_capabilities_prepared_linker(const native_prepared_context *context)
{
    assert(context && context->native);
    return (execution_prepared_linker){ .user = (void *)context,
                                        .host = host,
                                        .command = command,
                                        .query = query,
                                        .event = event,
                                        .type = type };
}
