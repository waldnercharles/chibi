#include "authoring/prototype_preview.h"

#include "authoring/engine_preview.h"
#include "authoring/material_prepare.h"
#include "authoring/preview.h"
#include "authoring/prototype_prepare.h"
#include "execution/status.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/scenes/physics.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_prototype_preview
{
    ecs_world *world;
    game_preview_world *live;
    ecs_entity entity;
    ecs_entity *entities;
    engine *runtime;
    char **component_strings;
    bool started; /* launched; a scene it loads may have no entities */
};

game_entities *game_prototype_preview_entities(game_prototype_preview *preview)
{
    return preview->live->runtime->entities;
}

game_preview_world *game_prototype_preview_live(game_prototype_preview *preview)
{
    return preview->live;
}

static int attach_behavior(ecs_world *world, ecs_entity entity, const char *asset_name, void *user)
{
    game_prototype_preview *preview = user;
    if (world != preview->world ||
        !game_source_registered(preview->live->runtime->sources, asset_name)) {
        return -1;
    }
    return game_source_attach(
        preview->live->runtime->sources,
        entity,
        asset_name,
        preview->live->runtime->deferred,
        preview->live->runtime->now,
        preview->live->runtime->delta
    );
}

static void destroy_entities(game_prototype_preview *preview)
{
    game_particles_clear(preview->live->runtime->particles);
    game_entities_reset(preview->live->runtime->entities);
    for (int i = 0; i < acount(preview->entities); i++) {
        if (preview->entities[i].slot) {
            game_source_detach_entity(preview->live->runtime->sources, preview->entities[i]);
            ecs_world_destroy_entity(preview->world, preview->entities[i]);
        }
    }
    afree(preview->entities);
    game_prototype_owned_strings_free(preview->component_strings);
    preview->component_strings = NULL;
    preview->entity = (ecs_entity){ 0 };
}

static void clear_entity(game_prototype_preview *preview)
{
    engine_reset(preview->runtime);
    destroy_entities(preview);
    preview->started = false;
}

game_prototype_preview *game_prototype_preview_new(const game_prototype_registry *registry)
{
    if (!registry || registry->component_count < 0 ||
        (registry->component_count && !registry->components)) {
        return NULL;
    }
    game_prototype_preview *preview = xcalloc(1, sizeof *preview);
    preview->world = ecs_world_new();
    for (int i = 0; i < registry->component_count; i++) {
        if (registry->components[i] &&
            ecs_world_register_component(preview->world, registry->components[i], NULL) !=
                ECS_OK) {
            ecs_world_free(preview->world);
            free(preview);
            return NULL;
        }
    }
    preview->live = game_preview_world_new(preview->world);
    if (!preview->live) {
        ecs_world_free(preview->world);
        free(preview);
        return NULL;
    }
    preview->runtime = authoring_engine_new(preview->world, preview->live->runtime);
    engine_set_focus(preview->runtime, ENGINE_FOCUS_VIEWPORT);
    return preview;
}

void game_prototype_preview_free(game_prototype_preview *preview)
{
    if (!preview) {
        return;
    }
    clear_entity(preview);
    engine_free(preview->runtime);
    game_preview_world_free(preview->live);
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
    behavior_asset *copy = behavior_asset_clone(source);
    if (!copy) {
        fprintf(stderr, "Could not register preview behavior %s: %s\n", asset_name, graph_last_error());
        return -1;
    }
    game_preview_definition *definition = game_preview_definition_new(asset_name, copy);
    if (!definition) {
        fprintf(stderr, "Could not register preview behavior %s: %s\n", asset_name, graph_last_error());
        behavior_asset_free(copy);
        return -1;
    }
    if (game_preview_add_definition(preview->live, definition) != 0) {
        const compile_diagnostic *error = compile_report_first_error(&definition->diagnostics);
        fprintf(
            stderr,
            "Could not register preview behavior %s: %s\n",
            asset_name,
            error->message
        );
        game_preview_definition_free(definition);
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
    game_preview_definition *definition = game_preview_find_definition(preview->live, name);
    if (!definition) {
        if (game_prototype_preview_register_behavior(preview, name, source) != 0) {
            return -1;
        }
        definition = game_preview_find_definition(preview->live, name);
    }
    if (game_preview_sync_working(definition, source) != 0) {
        return -1;
    }
    return game_preview_can_update_library(preview->live, definition, message, size);
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
    game_preview_definition *definition = game_preview_find_definition(preview->live, asset_name);
    if (!definition) {
        int result = game_prototype_preview_register_behavior(preview, asset_name, source);
        if (result != 0) {
            return result;
        }
        definition = game_preview_find_definition(preview->live, asset_name);
        game_preview_mark_dirty(definition);
    }
    if (game_preview_sync_working(definition, source) != 0) {
        return -1;
    }
    return restart
             ? game_preview_apply(preview->live, definition, message, message_size)
             : game_preview_update(preview->live, definition, message, message_size);
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
    if (!authoring_prototype_parameters(preview->live->runtime->entities, prototype, error, error_size)) {
        return ECS_ERR_ARGUMENT;
    }
    clear_entity(preview);
    engine_set_playing(preview->runtime, false);
    preview->live->runtime->deferred = true;
    int result = game_prototype_spawn(
        prototype,
        preview->world,
        &(game_prototype_spawn_hooks){ attach_behavior, preview },
        &preview->entity,
        error,
        error_size
    );
    if (result != ECS_OK) {
        preview->live->runtime->deferred = false;
        engine_set_playing(preview->runtime, false);
    } else {
        apush(preview->entities, preview->entity);
        if (game_entities_rebuild_global_bindings(preview->live->runtime->entities) != 0) {
            snprintf(error, error_size, "global binding names must be unique");
            preview->live->runtime->deferred = false;
            clear_entity(preview);
            return ECS_ERR_COMPONENT;
        }
        preview->live->runtime->deferred = false;
        if (game_source_begin(
                preview->live->runtime->sources,
                preview->live->runtime->now,
                preview->live->runtime->delta
            ) != 0) {
            snprintf(error, error_size, "prototype behavior Begin failed");
            clear_entity(preview);
            return ECS_ERR_COMMAND;
        }
        preview->started = true;
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
    int result = game_prototype_preview_start_scene(preview, entities, entity_count, error, error_size);
    if (result == ECS_OK) {
        engine_set_playing(preview->runtime, true);
    }
    return result;
}

void game_prototype_preview_end_scene(game_prototype_preview *preview)
{
    engine_reset_scene(preview->runtime);
    destroy_entities(preview);
}

int game_prototype_preview_start_scene(
    game_prototype_preview *preview,
    const game_prototype_preview_scene_entity *entities,
    int entity_count,
    char *error,
    size_t error_size
)
{
    assert(entity_count >= 0 && (entities || !entity_count));
    preview->live->runtime->deferred = true;
    for (int i = 0; i < entity_count; i++) {
        if (!entities[i].prototype || entities[i].behavior_count < 0 ||
            (entities[i].behavior_count && !entities[i].behaviors)) {
            snprintf(error, error_size, "scene fixture entity %d is invalid", i);
            preview->live->runtime->deferred = false;
            clear_entity(preview);
            return ECS_ERR_ARGUMENT;
        }
        if (!authoring_prototype_parameters(
                preview->live->runtime->entities,
                entities[i].prototype,
                error,
                error_size
            )) {
            preview->live->runtime->deferred = false;
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
            preview->live->runtime->deferred = false;
            clear_entity(preview);
            return result;
        }
        /* Track the spawned entity before overrides allocate owned strings or
         * behavior attachment can fail, so clear_entity destroys it first. */
        apush(preview->entities, entity);
        if (!preview->entity.slot) {
            preview->entity = entity;
        }
        if (entities[i].components && game_prototype_apply_components_value(
                                          preview->world,
                                          entity,
                                          entities[i].components,
                                          entities[i].prototype->registry,
                                          &preview->component_strings,
                                          error,
                                          error_size
                                      ) != 0) {
            preview->live->runtime->deferred = false;
            clear_entity(preview);
            return ECS_ERR_COMPONENT;
        }
        game_source_detach_entity(preview->live->runtime->sources, entity);
        for (int behavior = 0; behavior < entities[i].behavior_count; behavior++) {
            if (game_source_attach(
                    preview->live->runtime->sources,
                    entity,
                    entities[i].behaviors[behavior],
                    true,
                    preview->live->runtime->now,
                    preview->live->runtime->delta
                ) != 0) {
                snprintf(
                    error,
                    error_size,
                    "scene behavior %s could not attach",
                    entities[i].behaviors[behavior]
                );
                preview->live->runtime->deferred = false;
                clear_entity(preview);
                return ECS_ERR_COMMAND;
            }
        }
    }
    if (game_entities_rebuild_global_bindings(preview->live->runtime->entities) != 0) {
        snprintf(error, error_size, "global binding names must be unique");
        preview->live->runtime->deferred = false;
        clear_entity(preview);
        return ECS_ERR_COMPONENT;
    }
    preview->live->runtime->deferred = false;
    if (game_source_begin(
            preview->live->runtime->sources,
            preview->live->runtime->now,
            preview->live->runtime->delta
        ) != 0) {
        snprintf(error, error_size, "scene behavior Begin failed");
        clear_entity(preview);
        return ECS_ERR_COMMAND;
    }
    preview->started = true;
    return ECS_OK;
}

int game_prototype_preview_register_prototype(game_prototype_preview *preview, const game_prototype *prototype)
{
    return game_entities_register(preview->live->runtime->entities, prototype);
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
    return game_physics_configure(preview->live->runtime->physics, settings);
}

int game_prototype_preview_input(
    game_prototype_preview *preview,
    const char *action,
    double value,
    int pressed,
    engine_input_failure *error
)
{
    if (!preview || !preview->started) {
        return FLOW_ERR_ARGUMENT;
    }
    return engine_dispatch_input(preview->runtime, action, value, pressed, error);
}

int game_prototype_preview_sample_movement(game_prototype_preview *preview, float x, float y)
{
    if (!preview || !preview->started) {
        return FLOW_ERR_ARGUMENT;
    }
    return engine_sample_movement(preview->runtime, x, y);
}

int game_prototype_preview_tick(game_prototype_preview *preview, double monotonic_time)
{
    if (!preview || !preview->started) {
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
    return preview ? preview->live->runtime->particles : NULL;
}

game_source_registry *game_prototype_preview_sources(game_prototype_preview *preview)
{
    return preview ? preview->live->runtime->sources : NULL;
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

execution_instance *game_prototype_preview_instance(game_prototype_preview *preview, const char *asset_name)
{
    if (!preview || !preview->entity.slot || !asset_name) {
        return NULL;
    }
    execution_program *program = game_source_program(preview->live->runtime->sources, asset_name);
    return program
             ? execution_program_instance(program, ecs_entity_to_u64(preview->entity))
             : NULL;
}
