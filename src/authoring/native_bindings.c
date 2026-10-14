#include "authoring/native_bindings.h"

#include <assert.h>

static bool host(void *user, uint64_t id, script_lower_host *out)
{
    const native_capability_binding *binding = native_capabilities_find(user, id, 0);
    if (!binding) {
        return false;
    }
    *out = (script_lower_host){ .call = binding->call,
                                .access = binding->access,
                                .command = binding->command,
                                .query = binding->query,
                                .query_service = binding->query_service,
                                .intrinsic = binding->intrinsic,
                                .result_pins = binding->result_pins,
                                .result_count = binding->result_count };
    return true;
}
static bool action(void *user, uint64_t id, uint64_t instance, int entry, script_lower_host *out)
{
    (void)instance;
    const native_capabilities *table = user;
    const script_catalog_capability *declaration = script_catalog_capability_by_id(
        native_capabilities_catalog(table),
        id
    );
    const native_capability_binding *binding = native_capabilities_find(table, id, 0);
    if (!declaration || !binding || declaration->kind != SCRIPT_CAPABILITY_ACTION ||
        declaration->entry_count != 1 || entry < -1 || entry > 0 ||
        (binding->instance_state && !binding->intrinsic)) {
        return false;
    }
    return host(user, id, out);
}
static bool global(void *user, uint64_t id, behavior_function_instruction *out)
{
    const native_global_binding *binding = native_capabilities_global(user, id);
    if (!binding) {
        return false;
    }
    *out = binding->instruction;
    return true;
}
script_lower_bindings authoring_native_bindings(const native_capabilities *table)
{
    assert(table);
    return (script_lower_bindings){ .user = (void *)table,
                                    .host = host,
                                    .action = action,
                                    .global = global,
                                    .generation = native_capabilities_generation(table) };
}
