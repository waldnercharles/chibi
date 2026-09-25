#include "assets/scene_document.h"

#include "assets/behavior_loader.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "runtime/generated.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char *scene_authoring_scene_json(app_scene_authoring *scene)
{
    finish_transform_edit(scene, false);
    /* The document is named after its file, so saving never renames it. */
    char name[256] = "Scene";
    if (scene->scene_path && scene->scene_path[0]) {
        snprintf(name, sizeof name, "%s", content_basename(scene->scene_path));
        char *extension = strrchr(name, '.');
        if (extension && extension != name) {
            *extension = '\0';
        }
    }
    game_scene_document *document = game_scene_document_new(name);
    if (!document) {
        return NULL;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    document->perspective = scene->perspective;
    document->physics = game_physics_configuration(scene->live->physics);
    for (int i = 0; i < acount(scene->actors); i++) {
        const scene_actor *actor = &scene->actors[i];
        const game_prototype *snapshot = scene->runtime_snapshot_valid
                                           ? actor->runtime_snapshot
                                           : NULL;
        const char **names = NULL;
        if (snapshot) {
            for (int j = 0; j < acount(snapshot->graphs); j++) {
                apush(names, snapshot->graphs[j]);
            }
        }
        int count = snapshot ? 0
                             : game_behavior_attachment_count_for_entity(
                                   behaviors,
                                   actor->entity
                               );
        for (int j = 0; j < count; j++) {
            game_behavior_attachment_info info;
            if (game_behavior_attachment_info_at(behaviors, actor->entity, j, &info) != 0) {
                afree(names);
                game_scene_document_free(document);
                return NULL;
            }
            apush(names, info.asset_name);
        }
        int result = game_scene_document_add_entity(
            document,
            actor->name,
            actor->prototype_name,
            names,
            acount(names)
        );
        if (result == 0) {
            char *components = snapshot
                                 ? game_prototype_overrides_json(actor->base, snapshot)
                                 : game_prototype_capture_overrides_json(
                                       actor->base,
                                       scene->world,
                                       actor->entity
                                   );
            result = game_scene_document_set_entity_components(document, i, components);
            free(components);
        }
        if (result == 0) {
            char *retained = scene_retention_save(actor->retained);
            result = game_scene_document_set_entity_retained(document, i, retained);
            free(retained);
        }
        afree(names);
        if (result != 0) {
            game_scene_document_free(document);
            return NULL;
        }
    }
    char *json = game_scene_document_save(document);
    game_scene_document_free(document);
    if (!json) {
        return NULL;
    }
    return json;
}

int app_scene_authoring_save(app_scene_authoring *scene, const char *path)
{
    if (!scene || !path) {
        return -1;
    }
    if (scene_tabs_path_in_use(scene, APP_EDITOR_DOCUMENT_SCENE, path)) {
        snprintf(scene->message, sizeof scene->message, "Scene path is already open.");
        return -1;
    }
    char *json = scene_authoring_scene_json(scene);
    if (!json) {
        return -1;
    }
    int result = write_text_file_atomic(path, json);
    free(json);
    if (result == 0) {
        char *saved_path = xstrdup(path);
        free(scene->scene_path);
        scene->scene_path = saved_path;
        scene->scene_dirty = false;
        scene_tabs_sync_current(scene);
    }
    return result;
}

char *read_scene_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) || ftell(file) < 0) {
        if (file) {
            fclose(file);
        }
        return NULL;
    }
    long size = ftell(file);
    if (fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    char *json = (char *)malloc((size_t)size + 1);
    if (!json) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(json, 1, (size_t)size, file);
    fclose(file);
    json[read] = '\0';
    return json;
}

int app_scene_authoring_load_prototype(app_scene_authoring *scene, const char *path)
{
    if (!scene || !path) {
        return -1;
    }
    char *json = read_scene_file(path);
    if (!json) {
        return -1;
    }
    int result = app_scene_authoring_register_prototype(scene, json, path);
    free(json);
    return result;
}

static int load_content_behavior(app_scene_authoring *scene, const char *path)
{
    char *json = read_scene_file(path);
    if (!json) {
        return 0;
    }
    behavior_asset *working = behavior_asset_load_json(json);
    free(json);
    if (!working || !working->name ||
        game_live_find_definition(scene->live, working->name)) {
        behavior_asset_free(working);
        return 0;
    }
    game_live_definition *definition = game_live_definition_new(working->name, working);
    if (!definition) {
        behavior_asset_free(working);
        return 0;
    }
    if (game_live_definition_set_path(definition, path) != 0) {
        game_live_definition_free(definition);
        return 0;
    }
    if (game_live_add_definition(scene->live, definition) != 0) {
        game_live_definition_free(definition);
        return 0;
    }
    if (game_prototype_preview_register_behavior(
            scene->prototype_preview,
            working->name,
            working
        ) != 0) {
        game_live_remove_definition(scene->live, working->name);
        return 0;
    }
    return 1;
}

static int load_content_prototype(app_scene_authoring *scene, const char *path)
{
    char *json = read_scene_file(path);
    if (!json) {
        return 0;
    }
    int result = app_scene_authoring_register_prototype(scene, json, path) == 0;
    free(json);
    return result;
}

int app_scene_authoring_load_content(app_scene_authoring *scene)
{
    if (!scene) {
        return -1;
    }
    content_refresh(scene);
    int loaded = app_behavior_fixtures_load(
        scene->live,
        scene->prototype_preview,
        scene->content_root
    );
    if (loaded < 0) {
        loaded = 0;
    }
    bool has_assets = false;
    for (int i = 0; i < acount(scene->content); i++) {
        if (scene->content[i].directory ||
            content_path_in_folder(scene, scene->content[i].path, "scenes")) {
            continue;
        }
        has_assets = true;
        if (content_path_in_folder(scene, scene->content[i].path, "prototypes")) {
            loaded += load_content_prototype(scene, scene->content[i].path);
        }
    }
    return loaded || !has_assets ? 0 : -1;
}

static scene_prototype_asset *ensure_prototype(app_scene_authoring *scene, const char *name)
{
    scene_prototype_asset *asset = find_prototype(scene, name);
    if (asset || !scene || !scene->resolve_prototype) {
        return asset;
    }
    const char *json = scene->resolve_prototype(name, scene->resolve_user);
    if (!json || app_scene_authoring_register_prototype(scene, json, NULL) != 0) {
        return NULL;
    }
    return find_prototype(scene, name);
}

int apply_components_json(app_scene_authoring *scene, ecs_entity entity, const char *components_json)
{
    if (!components_json || !components_json[0]) {
        return 0;
    }
    char error[256];
    game_prototype_registry registry = app_component_registry();
    if (game_prototype_apply_components_json(scene->world, entity, components_json, &registry, error, sizeof error) !=
        0) {
        snprintf(scene->message, sizeof scene->message, "Could not apply overrides: %s", error);
        return -1;
    }
    return 0;
}

static int validate_scene_behaviors(app_scene_authoring *scene, char **names)
{
    game_behavior_world *world = scene->live->behaviors;
    for (int i = 0; i < acount(names); i++) {
        const behavior_asset *definition = NULL;
        for (int j = 0; j < game_behavior_asset_count(world); j++) {
            if (strcmp(names[i], game_behavior_asset_name_at(world, j)) == 0) {
                definition = game_behavior_asset_definition_at(world, j);
                break;
            }
        }
        if (!definition) {
            snprintf(scene->message, sizeof scene->message, "Unknown behavior asset %s.", names[i]);
            return -1;
        }
        if (definition->owner_type_name &&
            strcmp(definition->owner_type_name, "ecs.entity") != 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Behavior %s cannot attach to a scene entity.",
                names[i]
            );
            return -1;
        }
    }
    return 0;
}

static int validate_scene_entity(app_scene_authoring *scene, const game_scene_entity *entity)
{
    scene_retained_component *retained = NULL;
    bool valid = scene_retention_load(entity->retained_json, &retained) == 0 &&
                 scene_retention_validate(retained, &scene->registry);
    scene_retention_free(retained);
    if (!valid) {
        snprintf(
            scene->message,
            sizeof scene->message,
            "Invalid retained components for %s.",
            entity->name
        );
        return -1;
    }
    scene_prototype_asset *asset = ensure_prototype(scene, entity->prototype);
    if (!asset) {
        snprintf(
            scene->message,
            sizeof scene->message,
            "Unknown scene prototype %s.",
            entity->prototype
        );
        return -1;
    }
    if (validate_scene_behaviors(scene, asset->prototype->graphs) != 0 ||
        validate_scene_behaviors(scene, entity->behaviors) != 0) {
        return -1;
    }
    /* Exercise the same component parser without mutating the open Scene. */
    ecs_world *world = ecs_world_new();
    int result = 0;
    char error[256] = { 0 };
    for (int i = 0; i < scene->registry.component_count; i++) {
        if (ecs_world_register_component(world, scene->registry.components[i], NULL) !=
            ECS_OK) {
            snprintf(error, sizeof error, "could not register scene components");
            result = -1;
            break;
        }
    }
    ecs_entity actor;
    if (result == 0) {
        result = game_prototype_spawn_components(asset->prototype, world, &actor, error, sizeof error);
    }
    if (result == 0 && entity->components_json && entity->components_json[0]) {
        result = game_prototype_apply_components_json(
            world,
            actor,
            entity->components_json,
            &scene->registry,
            error,
            sizeof error
        );
    }
    ecs_world_free(world);
    if (result != 0) {
        snprintf(
            scene->message,
            sizeof scene->message,
            "Could not load scene entity %s: %s",
            entity->name,
            error
        );
    }
    return result;
}

int scene_authoring_load_json(app_scene_authoring *scene, const char *json, const char *path)
{
    char error[256];
    game_scene_document *document = game_scene_document_load(json, error, sizeof error);
    if (!document) {
        snprintf(scene->message, sizeof scene->message, "Could not load scene: %s", error);
        return -1;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    for (int i = 0; i < acount(document->entities); i++) {
        if (validate_scene_entity(scene, &document->entities[i]) != 0) {
            game_scene_document_free(document);
            return -1;
        }
    }
    scene->debug_sessions[APP_RUNTIME_SCENE]++;
    editor_set_inspection_blocked(scene->graph_editor, true);
    engine_set_playing(scene->runtime, false);
    game_behavior_set_deferred(scene->live->behaviors, true);
    scene->runtime_playing = false;
    scene->runtime_step = false;
    editor_ui_set_scene_play_expanded(false);
    app_scene_authoring_stop_prototype_preview(scene);
    invalidate_runtime_snapshot(scene);
    game_entities_reset(scene->live->entities);
    game_physics_reset(scene->live->physics);
    int configured = game_physics_configure(scene->live->physics, document->physics);
    assert(configured == 0);
    game_particles_clear(scene->live->particles);
    app_scene_renderer_reset(scene->renderer);
    while (acount(scene->actors)) {
        scene->selected = acount(scene->actors) - 1;
        remove_selected(scene);
    }
    for (int i = 0; i < acount(document->entities); i++) {
        game_scene_entity *entity = &document->entities[i];
        if (add_actor(scene, entity->name, entity->prototype) != 0) {
            game_scene_document_free(document);
            return -1;
        }
        scene_actor *actor = selected_actor(scene);
        if (scene_retention_load(entity->retained_json, &actor->retained) != 0) {
            game_scene_document_free(document);
            return -1;
        }
        if (apply_components_json(scene, actor->entity, entity->components_json) != 0) {
            game_scene_document_free(document);
            return -1;
        }
        if (restore_retained_components(scene, actor, actor->base) != 0) {
            game_scene_document_free(document);
            return -1;
        }
        game_behavior_detach_entity(behaviors, actor->entity);
        for (int j = 0; j < acount(entity->behaviors); j++) {
            if (game_behavior_attach(behaviors, actor->entity, entity->behaviors[j]) != 0) {
                game_scene_document_free(document);
                snprintf(scene->message, sizeof scene->message, "Could not attach scene behavior.");
                return -1;
            }
        }
    }
    if (game_entities_rebuild_global_bindings(scene->live->entities) != 0) {
        game_scene_document_free(document);
        snprintf(scene->message, sizeof scene->message, "Global binding names must be unique.");
        return -1;
    }
    scene->perspective = document->perspective;
    game_scene_document_free(document);
    char *saved_path = path ? xstrdup(path) : NULL;
    free(scene->scene_path);
    scene->scene_path = saved_path;
    scene->scene_dirty = false;
    scene_tabs_document_selected(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    snprintf(
        scene->message,
        sizeof scene->message,
        "Loaded scene from %s.",
        scene->scene_path ? scene->scene_path : "checkpoint"
    );
    return 0;
}

int app_scene_authoring_load(app_scene_authoring *scene, const char *path)
{
    if (!scene || !path) {
        return -1;
    }
    char *json = read_scene_file(path);
    if (!json) {
        snprintf(scene->message, sizeof scene->message, "Could not read %s.", path);
        return -1;
    }
    int result = scene_authoring_load_json(scene, json, path);
    free(json);
    if (result == 0) {
        scene->camera.saved = false;
        scene->camera_target = -1;
    }
    return result;
}

const char *app_scene_authoring_message(const app_scene_authoring *scene)
{
    return scene ? scene->message : NULL;
}

const char *app_scene_authoring_selected_content(const app_scene_authoring *scene)
{
    return scene ? scene->content_selected_path : NULL;
}

app_editor_document_context app_scene_authoring_document(const app_scene_authoring *scene)
{
    return scene ? scene->document
                 : (app_editor_document_context){ APP_EDITOR_DOCUMENT_SCENE, NULL };
}
