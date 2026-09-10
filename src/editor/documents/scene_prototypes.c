#include "editor/ui/ui_shell.h"
#include "editor/workspace/scene_internal.h"
#include "runtime/generated.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int register_prototype_hosts(app_scene_authoring *scene, const game_prototype *prototype)
{
    if (game_entities_register(scene->live->entities, prototype) != 0 ||
        game_prototype_preview_register_prototype(scene->prototype_preview, prototype) !=
            0) {
        snprintf(
            scene->prototype_message,
            sizeof scene->prototype_message,
            "Could not update runtime prototype %s.",
            prototype->name
        );
        return -1;
    }
    return 0;
}

int app_scene_authoring_detach_prototype_behavior(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *asset_name
)
{
    if (!scene || !prototype_name || !asset_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset || !game_prototype_has_graph(asset->prototype, asset_name)) {
        return -1;
    }
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    if (game_prototype_remove_graph(asset->prototype, asset_name) != 0) {
        return -1;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    char error[256];
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        if (strcmp(actor->prototype_name, prototype_name) != 0) {
            continue;
        }
        game_behavior_detach_asset(behaviors, actor->entity, asset_name);
        game_prototype *base = game_prototype_clone(asset->prototype, error, sizeof error);
        if (!base) {
            snprintf(
                scene->prototype_message,
                sizeof scene->prototype_message,
                "Could not update %s: %s",
                actor->name,
                error
            );
            return -1;
        }
        game_prototype_free(actor->base);
        actor->base = base;
    }
    asset->dirty = 1;
    asset_index_invalidate(scene);
    if (register_prototype_hosts(scene, asset->prototype) != 0) {
        return -1;
    }
    snprintf(scene->prototype_message, sizeof scene->prototype_message, "Detached %s from %s.", asset_name, prototype_name);
    return 0;
}

const game_prototype_component *prototype_component_by_id(const game_prototype *prototype, ecs_component_id id)
{
    for (int i = 0; i < acount(prototype->components); i++) {
        if (prototype->components[i].desc->id == id) {
            return &prototype->components[i];
        }
    }
    return NULL;
}

bool prototype_has_component_id(const game_prototype *prototype, ecs_component_id id)
{
    return prototype_component_by_id(prototype, id) != NULL;
}

static int refresh_prototype_actor(
    app_scene_authoring *scene,
    const game_prototype *prototype,
    scene_actor *actor
)
{
    char error[256];
    char *overrides = game_prototype_capture_overrides_json(
        actor->base,
        scene->world,
        actor->entity
    );
    if (!overrides) {
        return -1;
    }
    for (int i = 0; i < acount(actor->base->components); i++) {
        const ecs_component_desc *desc = actor->base->components[i].desc;
        if (prototype_has_component_id(prototype, desc->id) ||
            !ecs_component_has(scene->world, actor->entity, desc->id)) {
            continue;
        }
        scene_retention_remove(&actor->retained, desc->name);
        scene_retained_component retained = {
            .name = xstrdup(desc->name),
            .values_json = game_prototype_entity_component_json(
                scene->world,
                actor->entity,
                desc,
                &scene->registry
            ),
            .defaults_json = game_prototype_component_json(actor->base, desc->name),
            .automatic = true,
        };
        assert(retained.values_json && retained.defaults_json);
        apush(actor->retained, retained);
        scene->scene_dirty = true;
    }
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (desc && prototype_has_component_id(actor->base, desc->id) &&
            !prototype_has_component_id(prototype, desc->id) &&
            ecs_component_has(scene->world, actor->entity, desc->id) &&
            ecs_world_record_remove(scene->world, actor->entity, desc->id, (uint64_t)i + 1) !=
                ECS_OK) {
            free(overrides);
            return -1;
        }
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        if (!ecs_component_has(scene->world, actor->entity, component->desc->id) &&
            ecs_world_record_add(scene->world, actor->entity, component->desc->id, NULL, (uint64_t)i + 1) !=
                ECS_OK) {
            free(overrides);
            return -1;
        }
    }
    if (ecs_world_apply_commands(scene->world) != ECS_OK) {
        free(overrides);
        return -1;
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        for (int field = 0; field < component->desc->type->field_count; field++) {
            pin_value value;
            pin_read_field(component->data, &component->desc->type->fields[field], &value);
            if (ecs_component_write_field(
                    scene->world,
                    actor->entity,
                    component->desc->id,
                    field,
                    value
                ) != ECS_OK) {
                free(overrides);
                return -1;
            }
        }
    }
    if (game_prototype_apply_components_json(
            scene->world,
            actor->entity,
            overrides,
            &scene->registry,
            error,
            sizeof error
        ) != 0) {
        free(overrides);
        return -1;
    }
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (desc && prototype_has_component_id(actor->base, desc->id) &&
            !prototype_has_component_id(prototype, desc->id) &&
            ecs_component_has(scene->world, actor->entity, desc->id) &&
            ecs_world_record_remove(scene->world, actor->entity, desc->id, (uint64_t)i + 1) !=
                ECS_OK) {
            free(overrides);
            return -1;
        }
    }
    if (ecs_world_apply_commands(scene->world) != ECS_OK) {
        free(overrides);
        return -1;
    }
    if (restore_retained_components(scene, actor, prototype) != 0) {
        free(overrides);
        return -1;
    }
    game_prototype *base = game_prototype_clone(prototype, error, sizeof error);
    if (!base) {
        free(overrides);
        return -1;
    }
    game_prototype_free(actor->base);
    actor->base = base;
    free(overrides);
    return 0;
}

int refresh_prototype_instances(app_scene_authoring *scene, scene_prototype_asset *asset)
{
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        if (strcmp(actor->prototype_name, asset->prototype->name) != 0) {
            continue;
        }
        if (refresh_prototype_actor(scene, asset->prototype, actor) != 0) {
            snprintf(
                scene->prototype_message,
                sizeof scene->prototype_message,
                "Could not update instance %s.",
                actor->name
            );
            return -1;
        }
    }
    return register_prototype_hosts(scene, asset->prototype);
}

int write_prototype_field(
    app_scene_authoring *scene,
    scene_prototype_asset *asset,
    const char *component,
    const char *field,
    pin_value value
)
{
    if (game_prototype_write_field(asset->prototype, component, field, value) != 0) {
        return -1;
    }
    asset->dirty = 1;
    asset_index_invalidate(scene);
    return refresh_prototype_instances(scene, asset);
}

int app_scene_authoring_read_prototype_field(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component,
    const char *field,
    pin_value *value
)
{
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    return asset ? game_prototype_read_field(asset->prototype, component, field, value)
                 : -1;
}

int app_scene_authoring_set_prototype_field(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component_name,
    const char *field_name,
    pin_value value
)
{
    if (!scene || !prototype_name || !component_name || !field_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    return write_prototype_field(scene, asset, component_name, field_name, value);
}

static const ecs_component_desc *component_descriptor(const app_scene_authoring *scene, const char *name)
{
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (desc && strcmp(desc->name, name) == 0) {
            return desc;
        }
    }
    return NULL;
}

int restore_retained_components(app_scene_authoring *scene, scene_actor *actor, const game_prototype *prototype)
{
    for (int i = 0; i < acount(actor->retained); i++) {
        scene_retained_component *retained = &actor->retained[i];
        const ecs_component_desc *desc = component_descriptor(scene, retained->name);
        assert(desc);
        if (!retained->automatic) {
            if (ecs_component_has(scene->world, actor->entity, desc->id) &&
                (ecs_world_record_remove(scene->world, actor->entity, desc->id, 0) != ECS_OK ||
                 ecs_world_apply_commands(scene->world) != ECS_OK)) {
                return -1;
            }
            continue;
        }
        if (!prototype_has_component_id(prototype, desc->id)) {
            continue;
        }
        char *defaults = game_prototype_component_json(prototype, desc->name);
        char *values = game_prototype_rebase_component_json(
            retained->values_json,
            retained->defaults_json,
            defaults
        );
        free(defaults);
        int result = values ? apply_components_json(scene, actor->entity, values)
                            : -1;
        free(values);
        if (result != 0) {
            return result;
        }
        scene_retention_remove(&actor->retained, desc->name);
        i--;
        scene->scene_dirty = true;
    }
    return 0;
}

int app_scene_authoring_set_instance_component(app_scene_authoring *scene, const char *name, bool present)
{
    if (!scene || !name) {
        return -1;
    }
    scene_actor *actor = selected_actor(scene);
    const ecs_component_desc *desc = component_descriptor(scene, name);
    if (!actor || !desc || scene->runtime_snapshot_valid) {
        return -1;
    }
    finish_transform_edit(scene, false);
    bool exists = ecs_component_has(scene->world, actor->entity, desc->id);
    if (exists == present) {
        return 0;
    }
    char *json = present ? NULL
                         : game_prototype_entity_component_json(
                               scene->world,
                               actor->entity,
                               desc,
                               &scene->registry
                           );
    if (!present && !json) {
        return -1;
    }
    char *retained_before = scene_retention_save(actor->retained);
    int result = present
                   ? ecs_world_record_add(scene->world, actor->entity, desc->id, NULL, 0)
                   : ecs_world_record_remove(scene->world, actor->entity, desc->id, 0);
    if (result == ECS_OK) {
        result = ecs_world_apply_commands(scene->world);
    }
    if (result == ECS_OK && present) {
        scene_retained_component *retained = scene_retention_find(actor->retained, desc->name);
        char *defaults = NULL;
        for (int i = 0; i < acount(actor->base->components); i++) {
            if (actor->base->components[i].desc->id == desc->id) {
                defaults = game_prototype_component_json(actor->base, desc->name);
                break;
            }
        }
        if (!defaults && desc->id == GAME_COMPONENT_GameVisualMotion.id) {
            game_prototype *context = game_prototype_capture_entity(
                actor->prototype_name,
                scene->world,
                actor->entity,
                &scene->registry
            );
            if (context && game_prototype_remove_component(context, desc->name) == 0 &&
                game_prototype_add_component(context, desc) == 0) {
                defaults = game_prototype_component_json(context, desc->name);
            }
            game_prototype_free(context);
        } else if (!defaults) {
            defaults = game_prototype_component_defaults_json(&scene->registry, desc);
        }
        char *restored = NULL;
        if (defaults) {
            restored = retained ? game_prototype_rebase_component_json(
                                      retained->values_json,
                                      retained->defaults_json,
                                      defaults
                                  )
                                : xstrdup(defaults);
        }
        free(defaults);
        result = restored ? apply_components_json(scene, actor->entity, restored)
                          : -1;
        free(restored);
        json = game_prototype_entity_component_json(
            scene->world,
            actor->entity,
            desc,
            &scene->registry
        );
    }
    if ((present && result != ECS_OK) || (result == ECS_OK && !json)) {
        if (present) {
            ecs_world_record_remove(scene->world, actor->entity, desc->id, 0);
            ecs_world_apply_commands(scene->world);
        }
        free(retained_before);
        free(json);
        return -1;
    }
    if (result == ECS_OK && json) {
        scene_retention_remove(&actor->retained, desc->name);
        if (!present) {
            scene_retained_component retained = {
                .name = xstrdup(desc->name),
                .values_json = xstrdup(json),
                .defaults_json = game_prototype_component_json(actor->base, desc->name),
            };
            apush(actor->retained, retained);
        }
        char *retained_after = scene_retention_save(actor->retained);
        scene_component_edit edit = { .component = desc->id,
                                      .state_json = json,
                                      .retained_before_json = retained_before,
                                      .retained_after_json = retained_after,
                                      .added = present };
        bool recorded = scene_history_push_component(scene->history, actor->entity, &edit);
        free(retained_after);
        assert(recorded);
        scene->scene_dirty = true;
        scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
    free(json);
    free(retained_before);
    return result;
}

int app_scene_authoring_add_prototype_component(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component_name
)
{
    if (!scene || !prototype_name || !component_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    const ecs_component_desc *desc = component_descriptor(scene, component_name);
    if (!asset || !desc) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    if (game_prototype_add_component(asset->prototype, desc) != 0) {
        return -1;
    }
    asset->dirty = 1;
    asset_index_invalidate(scene);
    return refresh_prototype_instances(scene, asset);
}

int app_scene_authoring_remove_prototype_component(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component_name
)
{
    if (!scene || !prototype_name || !component_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    if (game_prototype_detach_component(asset->prototype, component_name) != 0) {
        return -1;
    }
    asset->dirty = 1;
    asset_index_invalidate(scene);
    return refresh_prototype_instances(scene, asset);
}

static int restore_prototype(
    app_scene_authoring *scene,
    scene_prototype_asset *asset,
    char ***from,
    char ***to
)
{
    int last = acount(*from) - 1;
    if (last < 0) {
        return 0;
    }
    char error[256];
    char *current = game_prototype_save(asset->prototype);
    char *snapshot = (*from)[last];
    game_prototype *prototype = game_prototype_load(snapshot, &scene->registry, error, sizeof error);
    if (!prototype) {
        free(current);
        return -1;
    }
    adel(*from, last);
    apush(*to, current);
    game_prototype_free(asset->prototype);
    asset->prototype = prototype;
    asset->dirty = 1;
    asset_index_invalidate(scene);
    if (refresh_prototype_instances(scene, asset) != 0) {
        free(snapshot);
        return -1;
    }
    free(snapshot);
    return 0;
}

int prototype_undo(app_scene_authoring *scene)
{
    scene_prototype_asset *asset = selected_prototype(scene);
    return asset ? restore_prototype(scene, asset, &asset->undo, &asset->redo) : -1;
}

int prototype_redo(app_scene_authoring *scene)
{
    scene_prototype_asset *asset = selected_prototype(scene);
    return asset ? restore_prototype(scene, asset, &asset->redo, &asset->undo) : -1;
}

int append_prototype_asset(app_scene_authoring *scene, game_prototype *prototype, const char *path)
{
    if (!scene || !prototype || !prototype->name ||
        find_prototype(scene, prototype->name)) {
        game_prototype_free(prototype);
        return -1;
    }
    scene_prototype_asset asset = {
        .path = path ? xstrdup(path) : NULL,
        .prototype = prototype,
        .dirty = path ? 0 : 1,
    };
    if (register_prototype_hosts(scene, prototype) != 0) {
        free(asset.path);
        game_prototype_free(prototype);
        return -1;
    }
    apush(scene->prototypes, asset);
    if (scene->selected_prototype < 0) {
        select_prototype(scene, 0);
    }
    return 0;
}

int app_scene_authoring_register_prototype(app_scene_authoring *scene, const char *json, const char *path)
{
    if (!scene || !json) {
        return -1;
    }
    char error[256];
    game_prototype *prototype = game_prototype_load(json, &scene->registry, error, sizeof error);
    return append_prototype_asset(scene, prototype, path);
}

int app_scene_authoring_save_prototype(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *path
)
{
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset || !asset->prototype || !path) {
        return -1;
    }
    if (scene_tabs_path_in_use(scene, APP_EDITOR_DOCUMENT_PROTOTYPE, path)) {
        snprintf(scene->prototype_message, sizeof scene->prototype_message, "Prototype path is already open.");
        return -1;
    }
    char *json = game_prototype_save(asset->prototype);
    if (!json) {
        return -1;
    }
    int result = write_text_file_atomic(path, json);
    free(json);
    if (result == 0) {
        char *saved_path = xstrdup(path);
        free(asset->path);
        asset->path = saved_path;
        asset->dirty = 0;
        content_refresh(scene);
        scene_tabs_mark_document_saved(
            scene,
            APP_EDITOR_DOCUMENT_PROTOTYPE,
            asset->prototype->name
        );
    }
    return result;
}

int app_scene_authoring_attach_prototype_behavior(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *asset_name
)
{
    if (!scene || !prototype_name || !asset_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    game_behavior_world *behaviors = scene->live->behaviors;
    if (!asset || !asset->prototype) {
        snprintf(scene->prototype_message, sizeof scene->prototype_message, "Prototype not found.");
        return -1;
    }
    if (!game_behavior_asset_registered(behaviors, asset_name)) {
        snprintf(
            scene->prototype_message,
            sizeof scene->prototype_message,
            "Behavior asset %s is not registered.",
            asset_name
        );
        return -1;
    }
    if (game_prototype_has_graph(asset->prototype, asset_name)) {
        snprintf(
            scene->prototype_message,
            sizeof scene->prototype_message,
            "%s is already attached to %s.",
            asset_name,
            prototype_name
        );
        return 0;
    }
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    if (game_prototype_add_graph(asset->prototype, asset_name) != 0) {
        snprintf(
            scene->prototype_message,
            sizeof scene->prototype_message,
            "Could not attach %s to %s.",
            asset_name,
            prototype_name
        );
        return -1;
    }

    scene_actor **actors = NULL;
    game_prototype **snapshots = NULL;
    scene_actor **attached = NULL;
    char error[256];
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        if (strcmp(actor->prototype_name, prototype_name) != 0) {
            continue;
        }
        game_prototype *snapshot = game_prototype_clone(asset->prototype, error, sizeof error);
        if (!snapshot) {
            snprintf(
                scene->prototype_message,
                sizeof scene->prototype_message,
                "Could not update %s: %s",
                prototype_name,
                error
            );
            free_behavior_snapshots(snapshots);
            game_prototype_remove_graph(asset->prototype, asset_name);
            afree(actors);
            afree(attached);
            return -1;
        }
        apush(actors, actor);
        apush(snapshots, snapshot);
    }
    for (int i = 0; i < acount(actors); i++) {
        if (actor_has_behavior(behaviors, actors[i], asset_name)) {
            continue;
        }
        if (game_behavior_attach(behaviors, actors[i]->entity, asset_name) != 0) {
            snprintf(
                scene->prototype_message,
                sizeof scene->prototype_message,
                "Could not attach %s to spawned actors.",
                asset_name
            );
            for (int j = 0; j < acount(attached); j++) {
                game_behavior_detach_asset(behaviors, attached[j]->entity, asset_name);
            }
            free_behavior_snapshots(snapshots);
            game_prototype_remove_graph(asset->prototype, asset_name);
            afree(actors);
            afree(attached);
            return -1;
        }
        apush(attached, actors[i]);
    }
    for (int i = 0; i < acount(actors); i++) {
        game_prototype_free(actors[i]->base);
        actors[i]->base = snapshots[i];
        snapshots[i] = NULL;
    }
    afree(actors);
    afree(attached);
    afree(snapshots);
    asset->dirty = 1;
    asset_index_invalidate(scene);
    if (register_prototype_hosts(scene, asset->prototype) != 0) {
        return -1;
    }
    snprintf(scene->prototype_message, sizeof scene->prototype_message, "Attached %s to %s.", asset_name, prototype_name);
    return 0;
}
