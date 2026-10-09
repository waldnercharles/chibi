#include "runtime/world.h"

#include "execution/ecs_reader.h"
#include "runtime/behaviors/entities_source.h"
#include "runtime/component_catalog.h"
#include "runtime/declared_components.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_world
{
    ecs_world *ecs;
    bool owns_ecs;
    game_source_registry *sources;
    game_source_entities *host;
    eval_component_reader reader;
    execution_log_service log;
    engine_modules modules;
    engine *runtime;
};
static void boundary(void *context, double now, double delta)
{
    game_world *world = context;
    game_source_entities_time(world->host, now, delta, true);
}
static bool paused(void *context)
{
    game_world *world = context;
    return game_source_paused(world->sources);
}
static void bind(game_world *world, const char *name, void *value)
{
    if (!value) {
        return;
    }
    int status = game_source_bind_service(world->sources, name, value);
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
static bool valid_actions(const game_world_options *options)
{
    if (options->action_count < 0 || (options->action_count && !options->actions)) {
        return false;
    }
    for (int i = 0; i < options->action_count; i++) {
        const char *name = options->actions[i].name;
        if (!name) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(options->actions[j].name, name)) {
                return false;
            }
        }
    }
    return true;
}
game_world *game_world_new(const game_world_options *options, char *error, size_t size)
{
    assert(options && (!size || error));
    if (options->modules & ~GAME_WORLD_MODULES_ALL || !valid_actions(options) ||
        (options->fields && !(options->modules & GAME_WORLD_MATERIALS)) ||
        options->components.component_count < 0 ||
        options->components.component_count > 1024 ||
        (options->components.component_count && !options->components.components)) {
        snprintf(error, size, "Invalid runtime world options");
        return NULL;
    }
    game_world *w = xcalloc(1, sizeof *w);
    w->owns_ecs = !options->ecs;
    w->ecs = options->ecs ? options->ecs : ecs_world_new();
    ecs_component_catalog project = project_components();
    for (int i = 0; i < project.component_count; i++) {
        if (!install(w->ecs, project.components[i])) {
            goto fail;
        }
    }
    for (int i = 0; i < options->components.component_count; i++) {
        if (!install(w->ecs, options->components.components[i])) {
            goto fail;
        }
    }
    w->sources = game_source_registry_new(w->ecs);
    int event_status = game_source_set_events(w->sources, options->events);
    assert(!event_status);
    (void)event_status;
    w->host = game_source_entities_new(w->sources);
    w->modules.sources = w->sources;
    w->modules.entities = game_source_entities_host(w->host);
    w->modules.context = w;
    w->modules.boundary = boundary;
    w->modules.paused = paused;
    w->modules.input = (options->modules & GAME_WORLD_INPUT) != 0;
    w->modules.rendering = (options->modules & GAME_WORLD_VISUAL_MOTION) != 0;
    w->modules.sdf_motion = options->sdf_motion;
    w->modules.save = game_save_new();
    if (options->modules & GAME_WORLD_AUDIO) {
        w->modules.audio = game_audio_new();
    }
    if (options->modules & GAME_WORLD_UI) {
        w->modules.ui = game_ui_new();
    }
    if (options->modules & GAME_WORLD_PARTICLES) {
        w->modules.particles = game_particles_new();
    }
    if (options->modules & GAME_WORLD_MATERIALS) {
        w->modules.materials = game_materials_new_prepared(
            w->ecs,
            w->modules.entities,
            w->modules.particles,
            options->fields
        );
        game_entities_bind_materials(w->modules.entities, w->modules.materials);
    }
    if (options->modules & GAME_WORLD_PHYSICS) {
        w->modules.physics = game_physics_new(w->ecs, w->modules.entities);
    }
    if (options->modules & GAME_WORLD_VISIBILITY) {
        w->modules.visibility = game_visibility_new(w->ecs, w->modules.entities);
    }
    ecs_component_reader_init(&w->reader, w->ecs);
    bind(w, "ecs.world", w->ecs);
    bind(w, EVAL_COMPONENT_READER_SERVICE, &w->reader);
    bind(w, "game.entities", w->modules.entities);
    bind(w, "game.save", w->modules.save);
    bind(w, "game.audio", w->modules.audio);
    bind(w, "game.ui", w->modules.ui);
    bind(w, "game.particles", w->modules.particles);
    bind(w, "game.visibility", w->modules.visibility);
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
void game_world_free(game_world *w)
{
    assert(w);
    engine_free(w->runtime);
    /* Cancel pending spawns and destroy their owned entities while callbacks
     * and registry are alive. Then cancel remaining source activations while
     * all borrowed services still exist. */
    game_entities_reset(w->modules.entities);
    game_source_registry_free(w->sources);
    if (w->modules.physics) {
        game_physics_free(w->modules.physics);
    }
    if (w->modules.materials) {
        game_materials_free(w->modules.materials);
    }
    if (w->modules.visibility) {
        game_visibility_free(w->modules.visibility);
    }
    game_source_entities_free(w->host);
    if (w->modules.particles) {
        game_particles_free(w->modules.particles);
    }
    if (w->modules.audio) {
        game_audio_free(w->modules.audio);
    }
    if (w->modules.ui) {
        game_ui_free(w->modules.ui);
    }
    game_save_free(w->modules.save);
    if (w->owns_ecs) {
        ecs_world_free(w->ecs);
    }
    free(w);
}
ecs_world *game_world_ecs(const game_world *w)
{
    return w->ecs;
}
game_source_registry *game_world_sources(const game_world *w)
{
    return w->sources;
}
game_entities *game_world_entities(const game_world *w)
{
    return w->modules.entities;
}
engine *game_world_engine(const game_world *w)
{
    return w->runtime;
}
game_materials *game_world_materials(const game_world *w)
{
    return w->modules.materials;
}
material_fields *game_world_fields(const game_world *w)
{
    return w->modules.materials ? game_materials_fields(w->modules.materials) : NULL;
}

const engine_modules *game_world_modules(const game_world *w)
{
    return &w->modules;
}
