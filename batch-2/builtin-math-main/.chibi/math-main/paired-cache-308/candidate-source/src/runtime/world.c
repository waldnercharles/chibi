#include "runtime/world.h"

#include "execution/ecs_reader.h"
#include "runtime/behaviors/entities_source.h"
#include "runtime/behaviors/scene_flow.h"
#include "runtime/component_catalog.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct world
{
    ecs_world *ecs;
    bool owns_ecs;
    source_registry *sources;
    source_entities *host;
    eval_component_reader reader;
    execution_log_service log;
    engine_modules modules;
    scene_flow *scenes;
    url_requests *urls;
    engine *runtime;
};
static void boundary(void *context, double now, double delta)
{
    world *world = context;
    source_entities_time(world->host, now, delta, true);
}
static bool paused(void *context)
{
    world *world = context;
    return source_paused(world->sources);
}
static void bind(world *world, const char *name, void *value)
{
    if (!value) {
        return;
    }
    int status = source_bind_service(world->sources, name, value);
    assert(!status);
    (void)status;
}
static bool same_name(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool install(ecs_world *world, const ecs_component_desc *desc)
{
    if (!desc || !desc->id || !desc->type) {
        return false;
    }
    const ecs_component_desc *existing = ecs_world_component(world, desc->id);
    if (existing) {
        return desc->element_size == existing->element_size &&
               same_name(desc->key, existing->key) &&
               same_name(desc->name, existing->name) &&
               same_name(desc->type->name, existing->type->name) &&
               desc->index == existing->index &&
               ecs_layout_hash(desc->type) == ecs_layout_hash(existing->type) &&
               ecs_layout_hash(desc->type) == existing->layout_hash &&
               (!desc->layout_hash || desc->layout_hash == existing->layout_hash);
    }
    return ecs_world_register_component(world, desc, NULL) == ECS_OK;
}
static bool valid_controls(uint64_t controls)
{
    _Static_assert(CONTROL_COUNT < 64, "control bitset");
    const uint64_t all = ((UINT64_C(1) << CONTROL_COUNT) - 1) &
                         ~(UINT64_C(1) << CONTROL_NONE);
    return !(controls & ~all);
}
bool world_actions_valid(const input_action *actions, int count)
{
    if (count < 0 || count > INPUT_ACTION_MAX || (count && !actions)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const char *name = actions[i].name;
        uint32_t all_buttons = ((UINT32_C(1) << PAD_COUNT) - 1) & ~UINT32_C(1);
        if (!name || !name[0] || !valid_controls(actions[i].controls) ||
            (!actions[i].controls && !actions[i].buttons) ||
            actions[i].controller < 0 || actions[i].controller >= INPUT_CONTROLLER_MAX ||
            (actions[i].buttons & ~all_buttons)) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(actions[j].name, name)) {
                return false;
            }
        }
    }
    return true;
}
world *world_new(const world_options *options, char *error, size_t size)
{
    assert(options && (!size || error));
    if (options->modules & ~WORLD_MODULES_ALL ||
        !world_actions_valid(options->actions, options->action_count) ||
        (options->fields && !(options->modules & WORLD_MATERIALS)) ||
        options->components.component_count < 0 ||
        options->components.component_count > 1024 ||
        (options->components.component_count && !options->components.components)) {
        snprintf(error, size, "Invalid runtime world options");
        return NULL;
    }
    world *w = xcalloc(1, sizeof *w);
    w->owns_ecs = !options->ecs;
    w->ecs = options->ecs ? options->ecs : ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        if (!install(w->ecs, REGISTRY_COMPONENTS[i])) {
            goto fail;
        }
    }
    for (int i = 0; i < options->components.component_count; i++) {
        if (!install(w->ecs, options->components.components[i])) {
            goto fail;
        }
    }
    w->sources = source_registry_new(w->ecs);
    int event_status = source_set_events(w->sources, options->events);
    assert(!event_status);
    (void)event_status;
    w->host = source_entities_new(w->sources);
    w->modules.sources = w->sources;
    w->modules.entities = source_entities_host(w->host);
    w->modules.context = w;
    w->modules.boundary = boundary;
    w->modules.paused = paused;
    w->modules.input = (options->modules & WORLD_INPUT) != 0;
    w->modules.save = save_new();
    w->scenes = scene_flow_new();
    w->urls = url_requests_new();
    if (options->modules & WORLD_AUDIO) {
        w->modules.audio = audio_new();
    }
    if (options->modules & WORLD_UI) {
        w->modules.ui = ui_new();
    }
    if (options->modules & WORLD_PARTICLES) {
        w->modules.particles = particles_new();
    }
    if (options->modules & WORLD_MATERIALS) {
        w->modules.materials = materials_new_prepared(
            w->ecs,
            w->modules.entities,
            w->modules.particles,
            options->fields
        );
        entities_bind_materials(w->modules.entities, w->modules.materials);
        w->modules.terrain_storage = terrain_storage_new(
            w->modules.materials,
            w->modules.save
        );
    }
    if (options->modules & WORLD_PHYSICS) {
        w->modules.physics = physics_new(w->ecs, w->modules.entities);
    }
    if (options->modules & WORLD_PHYSICS3D) {
        w->modules.physics3d = physics3d_new(w->ecs, w->modules.entities);
    }
    if (options->modules & WORLD_VISIBILITY) {
        w->modules.visibility = visibility_new(w->ecs, w->modules.entities);
    }
    ecs_component_reader_init(&w->reader, w->ecs);
    bind(w, "ecs.world", w->ecs);
    bind(w, EVAL_COMPONENT_READER_SERVICE, &w->reader);
    bind(w, "game.entities", w->modules.entities);
    bind(w, "game.save", w->modules.save);
    bind(w, TERRAIN_STORAGE_SERVICE, w->modules.terrain_storage);
    bind(w, "game.scenes", w->scenes);
    bind(w, URL_SERVICE, w->urls);
    bind(w, "game.audio", w->modules.audio);
    bind(w, "game.ui", w->modules.ui);
    bind(w, "game.particles", w->modules.particles);
    bind(w, "game.visibility", w->modules.visibility);
    bind(w, "game.physics", w->modules.physics);
    bind(w, "game.physics3d", w->modules.physics3d);
    w->log = options->log;
    if (w->log.emit) {
        bind(w, EXECUTION_LOG_SERVICE, &w->log);
    }
    w->runtime = engine_new(w->ecs, &w->modules);
    assert(w->runtime);
    engine_set_input_actions(w->runtime, options->actions, options->action_count);
    return w;
fail:
    snprintf(error, size, "Runtime component registration failed");
    if (w->owns_ecs) {
        ecs_world_free(w->ecs);
    }
    free(w);
    return NULL;
}
void world_free(world *w)
{
    assert(w);
    engine_free(w->runtime);
    /* Cancel pending spawns and destroy their owned entities while callbacks
     * and registry are alive. Then cancel remaining source activations while
     * all borrowed services still exist. */
    entities_reset(w->modules.entities);
    source_registry_free(w->sources);
    if (w->modules.physics) {
        physics_free(w->modules.physics);
    }
    if (w->modules.physics3d) {
        physics3d_free(w->modules.physics3d);
    }
    if (w->modules.terrain_storage) {
        terrain_storage_free(w->modules.terrain_storage);
    }
    if (w->modules.materials) {
        materials_free(w->modules.materials);
    }
    if (w->modules.visibility) {
        visibility_free(w->modules.visibility);
    }
    source_entities_free(w->host);
    if (w->modules.particles) {
        particles_free(w->modules.particles);
    }
    if (w->modules.audio) {
        audio_free(w->modules.audio);
    }
    if (w->modules.ui) {
        ui_free(w->modules.ui);
    }
    save_free(w->modules.save);
    scene_flow_free(w->scenes);
    url_requests_free(w->urls);
    if (w->owns_ecs) {
        ecs_world_free(w->ecs);
    }
    free(w);
}
ecs_world *world_ecs(const world *w)
{
    return w->ecs;
}
source_registry *world_sources(const world *w)
{
    return w->sources;
}
entities *world_entities(const world *w)
{
    return w->modules.entities;
}
engine *world_engine(const world *w)
{
    return w->runtime;
}
materials *world_materials(const world *w)
{
    return w->modules.materials;
}
material_fields *world_fields(const world *w)
{
    return w->modules.materials ? materials_fields(w->modules.materials) : NULL;
}

scene_flow *world_scenes(const world *w)
{
    return w->scenes;
}

url_requests *world_urls(const world *w)
{
    return w->urls;
}

const engine_modules *world_modules(const world *w)
{
    return &w->modules;
}
