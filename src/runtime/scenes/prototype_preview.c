#include "runtime/scenes/prototype_preview.h"

#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/physics.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_prototype_preview
{
    ecs_world *world;
    game_live_world *live;
    ecs_entity entity;
    ecs_entity *entities;
    engine *runtime;
};

game_entities *game_prototype_preview_entities(game_prototype_preview *preview)
{
    return preview->live->entities;
}

static int attach_behavior(ecs_world *world, ecs_entity entity, const char *asset_name, void *user)
{
    game_prototype_preview *preview = user;
    if (world != preview->world ||
        !game_behavior_asset_registered(preview->live->behaviors, asset_name)) {
        return -1;
    }
    return game_behavior_attach(preview->live->behaviors, entity, asset_name);
}

static void clear_entity(game_prototype_preview *preview)
{
    engine_reset(preview->runtime);
    game_particles_clear(preview->live->particles);
    game_entities_reset(preview->live->entities);
    for (int i = 0; i < acount(preview->entities); i++) {
        if (preview->entities[i].slot) {
            game_behavior_destroy_entity(preview->live->behaviors, preview->entities[i]);
        }
    }
    afree(preview->entities);
    preview->entity = (ecs_entity){ 0 };
}

game_prototype_preview *game_prototype_preview_new(const game_prototype_registry *registry)
{
    if (!registry || registry->component_count < 0 ||
        (registry->component_count && !registry->components)) {
        return NULL;
    }
    game_prototype_preview *preview = xcalloc(1, sizeof *preview);
    preview->world = ecs_world_new();
    preview->live = game_live_world_new(preview->world);
    if (!preview->live) {
        ecs_world_free(preview->world);
        free(preview);
        return NULL;
    }
    preview->runtime = engine_new(preview->world, preview->live);
    engine_set_focus(preview->runtime, ENGINE_FOCUS_VIEWPORT);
    for (int i = 0; i < registry->component_count; i++) {
        if (registry->components[i] &&
            ecs_world_register_component(preview->world, registry->components[i], NULL) !=
                ECS_OK) {
            game_prototype_preview_free(preview);
            return NULL;
        }
    }
    return preview;
}

void game_prototype_preview_free(game_prototype_preview *preview)
{
    if (!preview) {
        return;
    }
    clear_entity(preview);
    engine_free(preview->runtime);
    game_live_world_free(preview->live);
    ecs_world_free(preview->world);
    free(preview);
}

int game_prototype_preview_register_behavior(
    game_prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source
)
{
    if (!preview || !asset_name || !asset_name[0] || !source) {
        return -1;
    }
    char *json = behavior_asset_save_json(source);
    behavior_asset *copy = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (!copy) {
        fprintf(stderr, "Could not register preview behavior %s: %s\n", asset_name, graph_last_error());
        return -1;
    }
    game_live_definition *definition = game_live_definition_new(asset_name, copy);
    if (!definition) {
        fprintf(stderr, "Could not register preview behavior %s: %s\n", asset_name, graph_last_error());
        behavior_asset_free(copy);
        return -1;
    }
    if (game_live_add_definition(preview->live, definition) != 0) {
        const compile_diagnostic *error = compile_report_first_error(&definition->diagnostics);
        fprintf(
            stderr,
            "Could not register preview behavior %s: %s\n",
            asset_name,
            error->message
        );
        game_live_definition_free(definition);
        return -1;
    }
    return 0;
}

int game_prototype_preview_prepare_library(
    game_prototype_preview *preview,
    const char *name,
    const behavior_asset *source,
    char *message,
    size_t size
)
{
    game_live_definition *definition = game_live_find_definition(preview->live, name);
    if (!definition) {
        if (game_prototype_preview_register_behavior(preview, name, source) != 0) {
            return -1;
        }
        definition = game_live_find_definition(preview->live, name);
    }
    if (game_live_sync_working(definition, source) != 0) {
        return -1;
    }
    return game_live_can_preview_library(preview->live, definition, message, size);
}

int game_prototype_preview_update_behavior(
    game_prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source,
    bool restart,
    char *message,
    size_t message_size
)
{
    game_live_definition *definition = game_live_find_definition(preview->live, asset_name);
    if (!definition) {
        int result = game_prototype_preview_register_behavior(preview, asset_name, source);
        if (result != 0) {
            return result;
        }
        definition = game_live_find_definition(preview->live, asset_name);
        game_live_mark_dirty(definition);
    }
    if (game_live_sync_working(definition, source) != 0) {
        return -1;
    }
    return restart
             ? game_live_apply(preview->live, definition, message, message_size)
             : game_live_preview(preview->live, definition, message, message_size);
}

int game_prototype_preview_launch(
    game_prototype_preview *preview,
    const game_prototype *prototype,
    char *error,
    size_t error_size
)
{
    if (!preview || !prototype) {
        return ECS_ERR_ARGUMENT;
    }
    clear_entity(preview);
    engine_set_playing(preview->runtime, false);
    game_behavior_set_deferred(preview->live->behaviors, true);
    int result = game_prototype_spawn(
        prototype,
        preview->world,
        &(game_prototype_spawn_hooks){ attach_behavior, preview },
        &preview->entity,
        error,
        error_size
    );
    if (result != ECS_OK) {
        game_behavior_set_deferred(preview->live->behaviors, false);
        engine_set_playing(preview->runtime, false);
    } else {
        apush(preview->entities, preview->entity);
        if (game_entities_rebuild_global_bindings(preview->live->entities) != 0) {
            snprintf(error, error_size, "global binding names must be unique");
            game_behavior_set_deferred(preview->live->behaviors, false);
            clear_entity(preview);
            return ECS_ERR_COMPONENT;
        }
        game_behavior_set_deferred(preview->live->behaviors, false);
        if (game_behavior_begin_pending(preview->live->behaviors) != 0) {
            snprintf(error, error_size, "prototype behavior Begin failed");
            clear_entity(preview);
            return ECS_ERR_COMMAND;
        }
        engine_set_playing(preview->runtime, true);
    }
    return result;
}

int game_prototype_preview_launch_scene(
    game_prototype_preview *preview,
    const game_prototype_preview_scene_entity *entities,
    int entity_count,
    char *error,
    size_t error_size
)
{
    if (!preview || !entities || entity_count <= 0) {
        return ECS_ERR_ARGUMENT;
    }
    clear_entity(preview);
    engine_set_playing(preview->runtime, false);
    game_behavior_set_deferred(preview->live->behaviors, true);
    for (int i = 0; i < entity_count; i++) {
        if (!entities[i].prototype || entities[i].behavior_count < 0 ||
            (entities[i].behavior_count && !entities[i].behaviors)) {
            snprintf(error, error_size, "scene fixture entity %d is invalid", i);
            game_behavior_set_deferred(preview->live->behaviors, false);
            clear_entity(preview);
            return ECS_ERR_ARGUMENT;
        }
        ecs_entity entity = { 0 };
        int result = game_prototype_spawn(
            entities[i].prototype,
            preview->world,
            &(game_prototype_spawn_hooks){ attach_behavior, preview },
            &entity,
            error,
            error_size
        );
        if (result != ECS_OK) {
            game_behavior_set_deferred(preview->live->behaviors, false);
            clear_entity(preview);
            return result;
        }
        if (entities[i].components_json && game_prototype_apply_components_json(
                                               preview->world,
                                               entity,
                                               entities[i].components_json,
                                               entities[i].prototype->registry,
                                               error,
                                               error_size
                                           ) != 0) {
            game_behavior_set_deferred(preview->live->behaviors, false);
            clear_entity(preview);
            return ECS_ERR_COMPONENT;
        }
        game_behavior_detach_entity(preview->live->behaviors, entity);
        for (int behavior = 0; behavior < entities[i].behavior_count; behavior++) {
            if (game_behavior_attach(
                    preview->live->behaviors,
                    entity,
                    entities[i].behaviors[behavior]
                ) != 0) {
                snprintf(
                    error,
                    error_size,
                    "scene behavior %s could not attach",
                    entities[i].behaviors[behavior]
                );
                game_behavior_set_deferred(preview->live->behaviors, false);
                clear_entity(preview);
                return ECS_ERR_COMMAND;
            }
        }
        apush(preview->entities, entity);
        if (!preview->entity.slot) {
            preview->entity = entity;
        }
    }
    if (game_entities_rebuild_global_bindings(preview->live->entities) != 0) {
        snprintf(error, error_size, "global binding names must be unique");
        game_behavior_set_deferred(preview->live->behaviors, false);
        clear_entity(preview);
        return ECS_ERR_COMPONENT;
    }
    game_behavior_set_deferred(preview->live->behaviors, false);
    if (game_behavior_begin_pending(preview->live->behaviors) != 0) {
        snprintf(error, error_size, "scene behavior Begin failed");
        clear_entity(preview);
        return ECS_ERR_COMMAND;
    }
    engine_set_playing(preview->runtime, true);
    return ECS_OK;
}

int game_prototype_preview_register_prototype(game_prototype_preview *preview, const game_prototype *prototype)
{
    return game_entities_register(preview->live->entities, prototype);
}

void game_prototype_preview_stop(game_prototype_preview *preview)
{
    if (preview) {
        clear_entity(preview);
        engine_set_playing(preview->runtime, false);
    }
}

engine *game_prototype_preview_runtime(game_prototype_preview *preview)
{
    return preview->runtime;
}

int game_prototype_preview_configure_physics(game_prototype_preview *preview, game_physics_settings settings)
{
    if (preview->entity.slot) {
        return FLOW_ERR_ARGUMENT;
    }
    return game_physics_configure(preview->live->physics, settings);
}

int game_prototype_preview_input(
    game_prototype_preview *preview,
    const char *action,
    double value,
    int pressed,
    flow_error *error
)
{
    if (!preview || !preview->entity.slot) {
        return FLOW_ERR_ARGUMENT;
    }
    return engine_dispatch_input(preview->runtime, action, value, pressed, error);
}

int game_prototype_preview_sample_movement(game_prototype_preview *preview, float x, float y)
{
    if (!preview || !preview->entity.slot) {
        return FLOW_ERR_ARGUMENT;
    }
    return engine_sample_movement(preview->runtime, x, y);
}

int game_prototype_preview_tick(game_prototype_preview *preview, double monotonic_time)
{
    if (!preview || !preview->entity.slot) {
        return FLOW_ERR_ARGUMENT;
    }
    return engine_frame(preview->runtime, monotonic_time);
}

ecs_world *game_prototype_preview_world(game_prototype_preview *preview)
{
    return preview ? preview->world : NULL;
}

game_particles *game_prototype_preview_particles(game_prototype_preview *preview)
{
    return preview ? preview->live->particles : NULL;
}

game_behavior_world *game_prototype_preview_behaviors(game_prototype_preview *preview)
{
    return preview ? preview->live->behaviors : NULL;
}

game_live_world *game_prototype_preview_live(game_prototype_preview *preview)
{
    return preview->live;
}

ecs_entity game_prototype_preview_entity(const game_prototype_preview *preview)
{
    return preview ? preview->entity : (ecs_entity){ 0 };
}

int game_prototype_preview_entity_count(const game_prototype_preview *preview)
{
    return preview ? acount(preview->entities) : 0;
}

ecs_entity game_prototype_preview_entity_at(const game_prototype_preview *preview, int index)
{
    return preview && index >= 0 && index < acount(preview->entities)
             ? preview->entities[index]
             : (ecs_entity){ 0 };
}

behavior_instance *game_prototype_preview_instance(game_prototype_preview *preview, const char *asset_name)
{
    if (!preview || !preview->entity.slot || !asset_name) {
        return NULL;
    }
    return game_behavior_instance(preview->live->behaviors, preview->entity, asset_name);
}

int game_prototype_preview_register_system(
    game_prototype_preview *preview,
    const char *name,
    const char *json,
    char *error,
    size_t size
)
{
    return game_live_register_system(preview->live, name, json, error, size);
}
