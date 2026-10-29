#include "execution/component.h"

#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/service.h"
#include "support/array.h"

#include <assert.h>
#include <string.h>

bool behavior_function_bind(
    const behavior_function *f,
    behavior_function_context *ctx,
    const service_binding *services,
    uint64_t owner
)
{
    assert(f->frozen);
    ctx->owner = owner;
    ctx->services = services;
    /* A reference argument may name a component the caller resolved, and a
     * task keeps it after the caller's world binding ends. */
    bool world = f->binds_world;
    if (!world && !f->component_bindings) {
        return true;
    }
    for (int i = 0; i < acount(services); i++) {
        if (world && !strcmp(services[i].type_name, "ecs.world")) {
            ctx->world = ecs_world_execution(services[i].value);
        }
        if (f->component_bindings &&
            !strcmp(services[i].type_name, EVAL_COMPONENT_READER_SERVICE)) {
            const eval_component_reader *reader = services[i].value;
            if (reader) {
                if (f->component_bindings && !reader->execution) {
                    return false;
                }
                ctx->binding_world = reader->execution;
            }
        }
    }
    return true;
}
