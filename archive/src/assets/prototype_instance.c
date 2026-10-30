#include "assets/prototype_instance.h"

#include "runtime/component_catalog.h"
#include "support/arena.h"
#include "support/array.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/type.h"

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
            const type_desc *type = type_catalog_find(field->type_name);
            return type && type_is_plain(type);
        }
        default: return false;
    }
}

prototype_field *prototype_instance_overrides(const prototype *base, const ecs_world *world, ecs_entity entity)
{
    arena storage = { 0 };
    prototype_field *found = NULL;
    for (int i = 0; i < acount(base->components); i++) {
        const prototype_component *component = &base->components[i];
        const ecs_component_desc *desc = component->desc;
        if (desc->id == COMPONENT_Transform.id ||
            !ecs_component_has(world, entity, desc->id)) {
            continue;
        }
        for (int f = 0; f < desc->type->field_count; f++) {
            const type_field *field = &desc->type->fields[f];
            pin_value value = { 0 }, inherited = { 0 };
            if (!plain(field) ||
                ecs_component_read_field(&storage, world, entity, desc->id, f, &value) !=
                    ECS_OK) {
                continue;
            }
            pin_read_field(&storage, component->data, field, &inherited);
            if (!pin_value_equal(&value, &inherited)) {
                apush(found, ((prototype_field){ desc, f }));
            }
        }
    }
    arena_free(&storage);
    return found;
}

void prototype_apply_instance(
    prototype *prototype,
    const ecs_world *world,
    ecs_entity entity,
    const prototype_field *fields,
    int count
)
{
    arena storage = { 0 };
    for (int i = 0; i < count; i++) {
        const ecs_component_desc *desc = fields[i].component;
        pin_value value = { 0 };
        ecs_component_read_field(&storage, world, entity, desc->id, fields[i].field, &value);
        prototype_write_field(
            prototype,
            desc->name,
            desc->type->fields[fields[i].field].name,
            value
        );
    }
    arena_free(&storage);
}

prototype *prototype_from_instance(
    const char *name,
    const prototype *base,
    const ecs_world *world,
    ecs_entity entity,
    const char *const *behaviors,
    int behavior_count
)
{
    prototype *captured = prototype_capture_entity(name, world, entity, base->registry);
    const prototype_component *placement = NULL;
    for (int i = 0; i < acount(base->components); i++) {
        if (base->components[i].desc->id == COMPONENT_Transform.id) {
            placement = &base->components[i];
        }
    }
    for (int i = 0; placement && i < acount(captured->components); i++) {
        if (captured->components[i].desc->id == COMPONENT_Transform.id) {
            memcpy(
                captured->components[i].data,
                placement->data,
                placement->desc->element_size
            );
        }
    }
    if (!placement) {
        prototype_remove_component(captured, COMPONENT_Transform.name);
    }
    for (int i = 0; i < behavior_count; i++) {
        prototype_add_graph(captured, behaviors[i]);
    }
    /* Its text still points into the world: a copy of its own owns it. */
    char *text = prototype_save(captured);
    char error[256];
    prototype *owned = prototype_load(text, base->registry, error, sizeof error);
    free(text);
    prototype_free(captured);
    return owned;
}
