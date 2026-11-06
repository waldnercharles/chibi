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

struct prototype_preview
{
    ecs_world *world;
    preview_world *live;
    ecs_entity entity;
    ecs_entity *entities;
    engine *runtime;
    char **component_strings;
    bool started; /* launched; a scene it loads may have no entities */
};

entities *prototype_preview_entities(prototype_preview *preview)
{
    return preview->live->runtime->entities;
}

preview_world *prototype_preview_live(prototype_preview *preview)
{
    return preview->live;
}

static int attach_behavior(ecs_world *world, ecs_entity entity, const char *asset_name, void *user)
{
    prototype_preview *preview = user;
    if (world != preview->world ||
        !source_registered(preview->live->runtime->sources, asset_name)) {
        return -1;
    }
    return source_attach(
        preview->live->runtime->sources,
        entity,
        asset_name,
        preview->live->runtime->deferred,
        preview->live->runtime->now,
        preview->live->runtime->delta
    );
}

static void destroy_entities(prototype_preview *preview)
{
    particles_clear(preview->live->runtime->particles);
    entities_reset(preview->live->runtime->entities);
    /* Children go before their parents, whose destruction would take them. */
    for (int i = acount(preview->entities) - 1; i >= 0; i--) {
        if (preview->entities[i].slot) {
            source_detach_entity(preview->live->runtime->sources, preview->entities[i]);
            ecs_world_destroy_entity(preview->world, preview->entities[i]);
        }
    }
    afree(preview->entities);
    prototype_owned_strings_free(preview->component_strings);
    preview->component_strings = NULL;
    preview->entity = (ecs_entity){ 0 };
}

static void clear_entity(prototype_preview *preview)
{
    engine_reset(preview->runtime);
    destroy_entities(preview);
    preview->started = false;
}

prototype_preview *prototype_preview_new(const prototype_registry *registry)
{
    if (!registry || registry->component_count < 0 ||
        (registry->component_count && !registry->components)) {
        return NULL;
    }
    prototype_preview *preview = xcalloc(1, sizeof *preview);
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
    preview->live = preview_world_new(preview->world);
    if (!preview->live) {
        ecs_world_free(preview->world);
        free(preview);
        return NULL;
    }
    preview->runtime = authoring_engine_new(preview->world, preview->live->runtime);
    engine_set_focus(preview->runtime, ENGINE_FOCUS_VIEWPORT);
    return preview;
}

void prototype_preview_free(prototype_preview *preview)
{
    if (!preview) {
        return;
    }
    clear_entity(preview);
    engine_free(preview->runtime);
    preview_world_free(preview->live);
    ecs_world_free(preview->world);
    free(preview);
}

int prototype_preview_register_behavior(
    prototype_preview *preview,
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
    preview_definition *definition = preview_definition_new(asset_name, copy);
    if (!definition) {
        fprintf(stderr, "Could not register preview behavior %s: %s\n", asset_name, graph_last_error());
        behavior_asset_free(copy);
        return -1;
    }
    if (preview_add_definition(preview->live, definition) != 0) {
        const compile_diagnostic *error = compile_report_first_error(&definition->diagnostics);
        fprintf(
            stderr,
            "Could not register preview behavior %s: %s\n",
            asset_name,
            error->message
        );
        preview_definition_free(definition);
        return -1;
    }
    return 0;
}

int prototype_preview_prepare_library(
    prototype_preview *preview,
    const char *name,
    const behavior_asset *source,
    char *message,
    size_t size
)
{
    preview_definition *definition = preview_find_definition(preview->live, name);
    if (!definition) {
        if (prototype_preview_register_behavior(preview, name, source) != 0) {
            return -1;
        }
        definition = preview_find_definition(preview->live, name);
    }
    if (preview_sync_working(definition, source) != 0) {
        return -1;
    }
    return preview_can_update_library(preview->live, definition, message, size);
}

int prototype_preview_update_behavior(
    prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source,
    bool restart,
    char *message,
    size_t message_size
)
{
    preview_definition *definition = preview_find_definition(preview->live, asset_name);
    if (!definition) {
        int result = prototype_preview_register_behavior(preview, asset_name, source);
        if (result != 0) {
            return result;
        }
        definition = preview_find_definition(preview->live, asset_name);
        preview_mark_dirty(definition);
    }
    if (preview_sync_working(definition, source) != 0) {
        return -1;
    }
    return restart
             ? preview_apply(preview->live, definition, message, message_size)
             : preview_update(preview->live, definition, message, message_size);
}

int prototype_preview_launch(prototype_preview *preview, const prototype *prototype, char *error, size_t error_size)
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
    int result = prototype_spawn(
        prototype,
        preview->world,
        &(prototype_spawn_hooks){ attach_behavior, preview },
        &preview->entity,
        error,
        error_size
    );
    if (result != ECS_OK) {
        preview->live->runtime->deferred = false;
        engine_set_playing(preview->runtime, false);
    } else {
        apush(preview->entities, preview->entity);
        if (entities_rebuild_global_bindings(preview->live->runtime->entities) != 0) {
            snprintf(error, error_size, "global binding names must be unique");
            preview->live->runtime->deferred = false;
            clear_entity(preview);
            return ECS_ERR_COMPONENT;
        }
        preview->live->runtime->deferred = false;
        if (source_begin(
                preview->live->runtime->sources,
                preview->live->runtime->now,
                preview->live->runtime->delta
            ) != 0) {
            snprintf(
                error,
                error_size,
                "prototype behavior Begin failed: %s",
                source_error(preview->live->runtime->sources)
            );
            clear_entity(preview);
            return ECS_ERR_COMMAND;
        }
        preview->started = true;
        engine_set_playing(preview->runtime, true);
    }
    return result;
}

int prototype_preview_launch_scene(
    prototype_preview *preview,
    const prototype_preview_scene_entity *entities,
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
    int result = prototype_preview_start_scene(preview, entities, entity_count, error, error_size);
    if (result == ECS_OK) {
        engine_set_playing(preview->runtime, true);
    }
    return result;
}

void prototype_preview_end_scene(prototype_preview *preview)
{
    engine_reset_scene(preview->runtime);
    destroy_entities(preview);
}

/* The entity a '.'-separated path names from the scene's roots, or -1. */
static int entity_at_path(const prototype_preview_scene_entity *entities, int count, const char *path)
{
    int found = -1;
    for (const char *segment = path;; segment = strchr(segment, '.') + 1) {
        const char *dot = strchr(segment, '.');
        size_t length = dot ? (size_t)(dot - segment) : strlen(segment);
        int parent = found;
        found = -1;
        for (int i = parent + 1; i < count && found < 0; i++) {
            if (entities[i].parent == parent &&
                !strncmp(entities[i].name, segment, length) &&
                !entities[i].name[length]) {
                found = i;
            }
        }
        if (found < 0 || !dot) {
            return found;
        }
    }
}

/* Writes into each scene entity the entities its component values name by
 * path; `spawned` holds the scene's entities in order. */
static int link_scene(
    ecs_world *world,
    const prototype_preview_scene_entity *entities,
    int count,
    const ecs_entity *spawned,
    char *error,
    size_t error_size
)
{
    int result = ECS_OK;
    for (int i = 0; !result && i < count; i++) {
        prototype_link *links = entities[i].components
                                  ? prototype_components_links(
                                        entities[i].prototype->registry,
                                        entities[i].components
                                    )
                                  : NULL;
        for (int j = 0; !result && j < acount(links); j++) {
            const prototype_link *link = &links[j];
            const type_field *field = &link->component->type->fields[link->field];
            int target = entity_at_path(entities, count, link->path);
            if (target < 0) {
                snprintf(
                    error,
                    error_size,
                    "scene entity %s: %s %s names %s, but no entity has that "
                    "path",
                    entities[i].name,
                    link->component->name,
                    field->name,
                    link->path
                );
                result = ECS_ERR_COMPONENT;
                continue;
            }
            pin_value value = { .type = PIN_HANDLE,
                                .type_name = field->type_name,
                                .u64 = ecs_entity_to_u64(spawned[target]) };
            result = ecs_component_write_field(
                world,
                spawned[i],
                link->component->id,
                link->field,
                value
            );
            if (result) {
                snprintf(
                    error,
                    error_size,
                    "scene entity %s: %s %s could not be written",
                    entities[i].name,
                    link->component->name,
                    field->name
                );
            }
        }
        prototype_links_free(links);
    }
    return result;
}

/* Prepares what the entity's overrides name, such as a sky or shader
 * parameters, as preparing its prototype prepared the prototype's own. */
static bool prepare_overrides(const prototype_preview_scene_entity *entity, char *error, size_t size)
{
    if (!entity->components) {
        return true;
    }
    prototype *copy = prototype_clone(entity->prototype, error, size);
    bool ok = copy &&
              !prototype_apply_entity_components(copy, entity->components, error, size) &&
              authoring_prototype_resources(copy, error, size);
    prototype_free(copy);
    return ok;
}

int prototype_preview_start_scene(
    prototype_preview *preview,
    const prototype_preview_scene_entity *entities,
    int entity_count,
    char *error,
    size_t error_size
)
{
    assert(entity_count >= 0 && (entities || !entity_count));
    preview->live->runtime->deferred = true;
    int base = acount(preview->entities);
    for (int i = 0; i < entity_count; i++) {
        if (!entities[i].prototype || entities[i].parent < -1 ||
            entities[i].parent >= i || entities[i].behavior_count < 0 ||
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
            ) ||
            !prepare_overrides(&entities[i], error, error_size)) {
            preview->live->runtime->deferred = false;
            clear_entity(preview);
            return ECS_ERR_ARGUMENT;
        }
        ecs_entity entity = { 0 };
        int result = prototype_spawn(
            entities[i].prototype,
            preview->world,
            &(prototype_spawn_hooks){ attach_behavior, preview },
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
        int parent = entities[i].parent;
        if (parent >= 0 && (ecs_world_record_add_pair(
                                preview->world,
                                entity,
                                ECS_CHILD_OF,
                                preview->entities[base + parent],
                                0
                            ) ||
                            ecs_world_apply_commands(preview->world))) {
            snprintf(error, error_size, "scene fixture entity %d could not join its parent", i);
            preview->live->runtime->deferred = false;
            clear_entity(preview);
            return ECS_ERR_COMMAND;
        }
        if (entities[i].components && prototype_apply_components_value(
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
        if (!entities[i].own_behaviors) {
            continue;
        }
        source_detach_entity(preview->live->runtime->sources, entity);
        for (int behavior = 0; behavior < entities[i].behavior_count; behavior++) {
            if (source_attach(
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
    int linked = link_scene(preview->world, entities, entity_count, preview->entities + base, error, error_size);
    if (linked != ECS_OK) {
        preview->live->runtime->deferred = false;
        clear_entity(preview);
        return linked;
    }
    if (entities_rebuild_global_bindings(preview->live->runtime->entities) != 0) {
        snprintf(error, error_size, "global binding names must be unique");
        preview->live->runtime->deferred = false;
        clear_entity(preview);
        return ECS_ERR_COMPONENT;
    }
    preview->live->runtime->deferred = false;
    if (source_begin(
            preview->live->runtime->sources,
            preview->live->runtime->now,
            preview->live->runtime->delta
        ) != 0) {
        snprintf(
            error,
            error_size,
            "scene behavior Begin failed: %s",
            source_error(preview->live->runtime->sources)
        );
        clear_entity(preview);
        return ECS_ERR_COMMAND;
    }
    preview->started = true;
    return ECS_OK;
}

int prototype_preview_register_prototype(prototype_preview *preview, const prototype *prototype)
{
    return entities_register(preview->live->runtime->entities, prototype);
}

void prototype_preview_stop(prototype_preview *preview)
{
    if (preview) {
        clear_entity(preview);
        engine_set_playing(preview->runtime, false);
    }
}

engine *prototype_preview_runtime(prototype_preview *preview)
{
    return preview->runtime;
}

int prototype_preview_configure_physics(prototype_preview *preview, physics_settings settings)
{
    if (preview->entity.slot) {
        return FLOW_ERR_ARGUMENT;
    }
    preview_runtime *runtime = preview->live->runtime;
    int result = physics_configure(runtime->physics, settings);
    return result < 0 ? result : physics3d_configure(runtime->physics3d, settings);
}

int prototype_preview_input(
    prototype_preview *preview,
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

int prototype_preview_sample_movement(prototype_preview *preview, float x, float y)
{
    if (!preview || !preview->started) {
        return FLOW_ERR_ARGUMENT;
    }
    return engine_sample_movement(preview->runtime, x, y);
}

int prototype_preview_tick(prototype_preview *preview, double monotonic_time)
{
    if (!preview || !preview->started) {
        return FLOW_ERR_ARGUMENT;
    }
    return engine_frame(preview->runtime, monotonic_time);
}

ecs_world *prototype_preview_world(prototype_preview *preview)
{
    return preview ? preview->world : NULL;
}

particles *prototype_preview_particles(prototype_preview *preview)
{
    return preview ? preview->live->runtime->particles : NULL;
}

source_registry *prototype_preview_sources(prototype_preview *preview)
{
    return preview ? preview->live->runtime->sources : NULL;
}

ecs_entity prototype_preview_entity(const prototype_preview *preview)
{
    return preview ? preview->entity : (ecs_entity){ 0 };
}

int prototype_preview_entity_count(const prototype_preview *preview)
{
    return preview ? acount(preview->entities) : 0;
}

ecs_entity prototype_preview_entity_at(const prototype_preview *preview, int index)
{
    return preview && index >= 0 && index < acount(preview->entities)
             ? preview->entities[index]
             : (ecs_entity){ 0 };
}

execution_instance *prototype_preview_instance(prototype_preview *preview, const char *asset_name)
{
    if (!preview || !preview->entity.slot || !asset_name) {
        return NULL;
    }
    execution_program *program = source_program(preview->live->runtime->sources, asset_name);
    return program
             ? execution_program_instance(program, ecs_entity_to_u64(preview->entity))
             : NULL;
}
