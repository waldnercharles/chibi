#include "automation/prototype_instance.h"

#include "assets/prototype_instance.h"
#include "assets/scene_document.h"
#include "support/array.h"
#include "third_party/yyjson/yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The scene's entity named `name`, or NULL with why in `error`. */
static const game_scene_entity *entity_named(
    const game_scene_document *scene,
    const char *name,
    char *error,
    size_t size
)
{
    int index = scene ? game_scene_document_entity_index(scene, name) : -1;
    if (index < 0) {
        snprintf(error, size, "the scene has no entity %s", name);
        return NULL;
    }
    return &scene->entities[index];
}

int automation_instance_prototype(
    const char *scene_text,
    const char *entity,
    char *out,
    size_t size,
    char *error,
    size_t error_size
)
{
    game_scene_document *scene = game_scene_document_load(scene_text, error, error_size);
    const game_scene_entity *found = scene ? entity_named(scene, entity, error, error_size)
                                           : NULL;
    if (found) {
        snprintf(out, size, "%s", found->prototype);
    }
    game_scene_document_free(scene);
    return found ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_VALIDATION_ERROR;
}

/* The fields to apply: each `fields` names, which must be one the entity
   overrides, or all it overrides. NULL with why in `error`. */
static game_prototype_field *chosen(
    const game_prototype_field *overrides,
    yyjson_val *fields,
    bool *valid,
    char *error,
    size_t size
)
{
    game_prototype_field *picked = NULL;
    *valid = true;
    if (!fields) {
        for (int i = 0; i < acount(overrides); i++) {
            apush(picked, overrides[i]);
        }
        return picked;
    }
    yyjson_arr_iter iterator = yyjson_arr_iter_with(fields);
    yyjson_val *item;
    while (*valid && (item = yyjson_arr_iter_next(&iterator))) {
        const char *component = yyjson_get_str(yyjson_obj_get(item, "component"));
        const char *field = yyjson_get_str(yyjson_obj_get(item, "field"));
        int found = -1;
        for (int i = 0; component && field && i < acount(overrides); i++) {
            const ecs_component_desc *desc = overrides[i].component;
            bool named = !strcmp(desc->name, component) ||
                         (desc->key && !strcmp(desc->key, component));
            found = named && !strcmp(desc->type->fields[overrides[i].field].name, field)
                      ? i
                      : found;
        }
        if (found < 0) {
            snprintf(
                error,
                size,
                "%s.%s is not a field the entity overrides",
                component ? component : "?",
                field ? field : "?"
            );
            *valid = false;
            break;
        }
        apush(picked, overrides[found]);
    }
    return picked;
}

static void list_applied(yyjson_mut_doc *doc, yyjson_mut_val *result, const game_prototype_field *fields)
{
    yyjson_mut_val *applied = yyjson_mut_obj_add_arr(doc, result, "applied_fields");
    for (int i = 0; i < acount(fields); i++) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, applied);
        const ecs_component_desc *desc = fields[i].component;
        yyjson_mut_obj_add_strcpy(doc, item, "component", desc->name);
        yyjson_mut_obj_add_strcpy(
            doc,
            item,
            "field",
            desc->type->fields[fields[i].field].name
        );
    }
}

/* The entity spawned from its prototype with its overrides, in `world`. */
static int compose(
    ecs_world *world,
    const game_prototype *base,
    const game_scene_entity *entity,
    const game_prototype_registry *registry,
    ecs_entity *out,
    char *error,
    size_t size
)
{
    for (int i = 0; i < registry->component_count; i++) {
        ecs_world_register_component(world, registry->components[i], NULL);
    }
    if (game_prototype_spawn_components(base, world, out, error, size) != ECS_OK ||
        (entity->components &&
         game_prototype_apply_components_value(world, *out, entity->components, registry, error, size) !=
             0)) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    return CHIBI_AUTOMATION_OK;
}

int automation_prototype_from_instance(
    const automation_instance *instance,
    const game_prototype_registry *registry,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **text,
    char *error,
    size_t error_size
)
{
    *text = NULL;
    game_scene_document *scene = game_scene_document_load(instance->scene_text, error, error_size);
    const game_scene_entity *entity = scene
                                        ? entity_named(scene, instance->entity, error, error_size)
                                        : NULL;
    game_prototype *base = entity
                             ? game_prototype_load(instance->prototype_text, registry, error, error_size)
                             : NULL;
    ecs_world *world = ecs_world_new();
    ecs_entity spawned = { 0 };
    int status = base ? compose(world, base, entity, registry, &spawned, error, error_size)
                      : CHIBI_AUTOMATION_VALIDATION_ERROR;
    if (status == CHIBI_AUTOMATION_OK && instance->name) {
        game_prototype *made = game_prototype_from_instance(
            instance->name,
            base,
            world,
            spawned,
            (const char *const *)entity->behaviors,
            acount(entity->behaviors)
        );
        *text = made ? game_prototype_save(made) : NULL;
        game_prototype_free(made);
    } else if (status == CHIBI_AUTOMATION_OK) {
        game_prototype_field *overrides = game_prototype_instance_overrides(base, world, spawned);
        bool valid;
        game_prototype_field *fields = chosen(overrides, instance->fields, &valid, error, error_size);
        if (valid) {
            game_prototype_apply_instance(base, world, spawned, fields, acount(fields));
            list_applied(doc, result, fields);
            *text = game_prototype_save(base);
        }
        status = valid ? status : CHIBI_AUTOMATION_INVALID_REQUEST;
        afree(fields);
        afree(overrides);
    }
    if (status == CHIBI_AUTOMATION_OK && !*text) {
        snprintf(error, error_size, "the instance could not be saved as a prototype");
        status = CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    ecs_world_free(world);
    game_prototype_free(base);
    game_scene_document_free(scene);
    return status;
}
