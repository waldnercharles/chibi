#include "assets/prototype_instance.h"

#include "graph/model/registry.h"
#include "graph/model/type.h"
#include "graph/runtime/value_array.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <stdlib.h>
#include <string.h>

static bool plain(const type_field *field)
{
    switch (field->type) {
        case PIN_BOOL:
        case PIN_I8:
        case PIN_U8:
        case PIN_I16:
        case PIN_U16:
        case PIN_I32:
        case PIN_U32:
        case PIN_I64:
        case PIN_U64:
        case PIN_ENUM:
        case PIN_F32:
        case PIN_F64:
        case PIN_STR:
        case PIN_NAME: return true;
        case PIN_STRUCT: {
            const type_desc *type = registry_type_by_name(field->type_name);
            return type && type_is_plain(type);
        }
        default: return false;
    }
}

game_prototype_field *game_prototype_instance_overrides(
    const game_prototype *base,
    const ecs_world *world,
    ecs_entity entity
)
{
    game_prototype_field *found = NULL;
    for (int i = 0; i < acount(base->components); i++) {
        const game_prototype_component *component = &base->components[i];
        const ecs_component_desc *desc = component->desc;
        if (desc->id == GAME_COMPONENT_GameTransform.id ||
            !ecs_component_has(world, entity, desc->id)) {
            continue;
        }
        for (int f = 0; f < desc->type->field_count; f++) {
            const type_field *field = &desc->type->fields[f];
            pin_value value = { 0 }, inherited = { 0 };
            if (!plain(field) ||
                ecs_component_read_field(world, entity, desc->id, f, &value) != ECS_OK) {
                continue;
            }
            pin_read_field(component->data, field, &inherited);
            if (!pin_value_equal(&value, &inherited)) {
                apush(found, ((game_prototype_field){ desc, f }));
            }
        }
    }
    return found;
}

void game_prototype_apply_instance(
    game_prototype *prototype,
    const ecs_world *world,
    ecs_entity entity,
    const game_prototype_field *fields,
    int count
)
{
    for (int i = 0; i < count; i++) {
        const ecs_component_desc *desc = fields[i].component;
        pin_value value = { 0 };
        ecs_component_read_field(world, entity, desc->id, fields[i].field, &value);
        game_prototype_write_field(
            prototype,
            desc->name,
            desc->type->fields[fields[i].field].name,
            value
        );
    }
}

game_prototype *game_prototype_from_instance(
    const char *name,
    const game_prototype *base,
    const ecs_world *world,
    ecs_entity entity,
    const char *const *behaviors,
    int behavior_count
)
{
    game_prototype *prototype = game_prototype_capture_entity(name, world, entity, base->registry);
    const game_prototype_component *placement = NULL;
    for (int i = 0; i < acount(base->components); i++) {
        if (base->components[i].desc->id == GAME_COMPONENT_GameTransform.id) {
            placement = &base->components[i];
        }
    }
    for (int i = 0; placement && i < acount(prototype->components); i++) {
        if (prototype->components[i].desc->id == GAME_COMPONENT_GameTransform.id) {
            memcpy(
                prototype->components[i].data,
                placement->data,
                placement->desc->element_size
            );
        }
    }
    if (!placement) {
        game_prototype_remove_component(prototype, GAME_COMPONENT_GameTransform.name);
    }
    for (int i = 0; i < behavior_count; i++) {
        game_prototype_add_graph(prototype, behaviors[i]);
    }
    /* Its text still points into the world: a copy of its own owns it. */
    char *text = game_prototype_save(prototype);
    char error[256];
    game_prototype *owned = game_prototype_load(text, base->registry, error, sizeof error);
    free(text);
    game_prototype_free(prototype);
    return owned;
}
