#include "execution/ecs_reader.h"

#include "types/text.h"

#include <string.h>

static int read_field(
    arena *storage,
    void *user,
    uint64_t owner,
    const char *component_key,
    const char *field_name,
    pin_value *out
)
{
    ecs_world *world = user;
    ecs_entity entity = ecs_entity_from_u64(owner);
    if (!world || !out || !ecs_entity_is_alive(world, entity)) {
        return EVAL_COMPONENT_READ_INVALID_OWNER;
    }
    const ecs_component_desc *component = NULL;
    for (int i = 0; i < ecs_world_component_count(world); i++) {
        const ecs_component_desc *candidate = ecs_world_component_at(world, i);
        if (candidate && candidate->key && component_key &&
            strcmp(candidate->key, component_key) == 0) {
            component = candidate;
            break;
        }
    }
    if (!component || !ecs_component_has(world, entity, component->id)) {
        return EVAL_COMPONENT_READ_MISSING_COMPONENT;
    }
    const void *data = ecs_component_data_const(world, entity, component->id);
    if (!component->type || !data) {
        return EVAL_COMPONENT_READ_MISSING_COMPONENT;
    }
    if (!field_name) {
        *out = (pin_value){
            .type = PIN_STRUCT,
            .container = PIN_CONTAINER_SINGLE,
            .type_name = component->type->name,
            .ptr = (void *)data,
        };
        return EVAL_COMPONENT_READ_OK;
    }
    return pin_read_field_path(storage, data, component->type, field_name, out) == 0
             ? EVAL_COMPONENT_READ_OK
             : EVAL_COMPONENT_READ_MISSING_FIELD;
}

void ecs_component_reader_init(eval_component_reader *reader, ecs_world *world)
{
    if (reader) {
        *reader = (eval_component_reader){ .read = read_field,
                                           .user = world,
                                           .execution = ecs_world_execution(world) };
    }
}
