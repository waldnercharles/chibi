#include "runtime/player.h"

#include "runtime/material_field_prepared.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_player
{
    game_world *world;
    game_scene_instance *scene;
    /* Owned names and retained scenes Load Scene can start, the start first. */
    game_player_named_scene *scenes;
    game_player_resource_host resources;
    declared_components *declared;
    void *generation;
    bool committed;
};
static bool fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
}
uint64_t game_player_asset_fingerprint(const void *data, size_t length)
{
    const unsigned char *bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}
bool game_player_relative_path(const char *path)
{
    if (!path || !path[0] || strnlen(path, 1025) > 1024 || path[0] == '/' ||
        strchr(path, '\\') || strchr(path, ':')) {
        return false;
    }
    const char *part = path;
    for (const char *p = path;; p++) {
        if (*p != '/' && *p) {
            continue;
        }
        size_t length = (size_t)(p - part);
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.')) {
            return false;
        }
        if (!*p) {
            return true;
        }
        part = p + 1;
    }
}
bool game_player_asset_valid(const game_player_asset *asset)
{
    if (!asset || !asset->name || !asset->name[0] || strnlen(asset->name, 1025) > 1024 ||
        asset->kind < 0 || asset->kind >= GAME_PLAYER_ASSET_COUNT ||
        !asset->length || asset->length > GAME_PLAYER_MAX_ASSET_BYTES ||
        (asset->path && !game_player_relative_path(asset->path))) {
        return false;
    }
    if (asset->external) {
        return !asset->data && asset->path && asset->content_hash;
    }
    return asset->data &&
           (!asset->content_hash ||
            asset->content_hash ==
                game_player_asset_fingerprint(asset->data, asset->length));
}
static bool behaviors_registered(const game_source_registry *sources, const game_scene *scene)
{
    for (int i = 0; i < game_scene_object_count(scene); i++) {
        for (int j = 0; j < game_scene_object_behavior_count(scene, i); j++) {
            if (!game_source_registered(sources, game_scene_object_behavior(scene, i, j))) {
                return false;
            }
        }
    }
    return true;
}
static void keep(game_player *p, const char *name, game_scene *scene)
{
    game_scene_retain(scene);
    apush(p->scenes, ((game_player_named_scene){ xstrdup(name), scene }));
}
game_player *game_player_new(const game_player_spec *spec, char *error, size_t size)
{
    assert(spec && (!size || error));
    if (spec->world.ecs || !spec->scene || spec->module_count < 0 ||
        spec->module_count > GAME_PACKAGE_MAX_MODULES ||
        (spec->module_count && !spec->modules) || spec->prototype_count < 0 ||
        spec->prototype_count > 1024 ||
        (spec->prototype_count && !spec->prototypes) || spec->scene_count < 0 ||
        spec->scene_count > 1024 || (spec->scene_count && !spec->scenes) ||
        spec->asset_count < 0 || spec->asset_count > GAME_PLAYER_MAX_ASSETS ||
        (spec->asset_count && !spec->assets) ||
        ((spec->asset_count || spec->resources.stage || spec->resources.scene ||
          spec->resources.commit || spec->resources.discard || spec->resources.retire) &&
         (!spec->resources.stage || !spec->resources.scene || !spec->resources.commit ||
          !spec->resources.discard || !spec->resources.retire))) {
        fail(error, size, "Invalid prepared player metadata");
        return NULL;
    }
    size_t asset_bytes = 0;
    for (int i = 0; i < spec->asset_count; i++) {
        const game_player_asset *asset = &spec->assets[i];
        if (!game_player_asset_valid(asset) ||
            asset->length > GAME_PLAYER_MAX_ASSET_BYTES - asset_bytes) {
            fail(error, size, "Invalid prepared player resource record");
            return NULL;
        }
        asset_bytes += asset->length;
        for (int j = 0; j < i; j++) {
            if (asset->kind == spec->assets[j].kind &&
                !strcmp(asset->name, spec->assets[j].name)) {
                fail(error, size, "Prepared player resource identity is duplicated");
                return NULL;
            }
        }
    }
    const char *start = spec->scene_name ? spec->scene_name
                                         : game_scene_name(spec->scene);
    if (!start[0]) {
        fail(error, size, "Prepared player scene names are missing or duplicated");
        return NULL;
    }
    for (int i = 0; i < spec->scene_count; i++) {
        const game_player_named_scene *s = &spec->scenes[i];
        if (!s->name || !s->name[0] || !s->scene || !strcmp(s->name, start)) {
            fail(error, size, "Prepared player scene names are missing or duplicated");
            return NULL;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(s->name, spec->scenes[j].name)) {
                fail(error, size, "Prepared player scene names are missing or duplicated");
                return NULL;
            }
        }
    }
    for (int i = 0; i < spec->prototype_count; i++) {
        const game_player_prototype *p = &spec->prototypes[i];
        if (!p->name || !p->name[0] || !p->scene ||
            game_scene_object_count(p->scene) != 1) {
            fail(error, size, "Prepared player prototype requires a named single object");
            return NULL;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(p->name, spec->prototypes[j].name)) {
                fail(error, size, "Prepared player prototype names are duplicated");
                return NULL;
            }
        }
    }
    size_t local_budget = GAME_PLAYER_MAX_ASSET_BYTES;
    size_t *remaining = spec->decoded_remaining ? spec->decoded_remaining : &local_budget;
    size_t initial_budget = *remaining;
    if (!spec->decoded_remaining) {
        size_t bytes = game_scene_storage_bytes(spec->scene);
        if (spec->world.events) {
            bytes += game_event_catalog_storage_bytes(spec->world.events);
        }
        if (bytes > *remaining) {
            fail(error, size, "Prepared player retained storage budget exceeded");
            return NULL;
        }
        *remaining -= bytes;
        game_scene **charged = NULL;
        apush(charged, spec->scene);
        for (int i = 0; i < spec->prototype_count; i++) {
            apush(charged, spec->prototypes[i].scene);
        }
        for (int i = 0; i < spec->scene_count; i++) {
            apush(charged, spec->scenes[i].scene);
        }
        for (int i = 1; i < acount(charged); i++) {
            bool known = false;
            for (int j = 0; j < i; j++) {
                known |= charged[i] == charged[j];
            }
            bytes = known ? 0 : game_scene_storage_bytes(charged[i]);
            if (bytes > *remaining) {
                afree(charged);
                fail(error, size, "Prepared player retained storage budget exceeded");
                return NULL;
            }
            *remaining -= bytes;
        }
        afree(charged);
    }
    game_world_options options = spec->world;
    options.fields = NULL;
    if (spec->world.fields) {
        unsigned char *bytes;
        size_t length;
        if (!material_fields_prepared_write(spec->world.fields, &bytes, &length, error, size)) {
            return NULL;
        }
        size_t field_budget = *remaining;
        if (spec->decoded_remaining) {
            size_t old_bytes = material_fields_storage_bytes(spec->world.fields);
            if (old_bytes > SIZE_MAX - field_budget) {
                free(bytes);
                fail(error, size, "Prepared player field budget is invalid");
                return NULL;
            }
            field_budget += old_bytes;
        }
        options.fields = material_fields_prepared_read_budget(bytes, length, &field_budget, error, size);
        free(bytes);
        if (!options.fields) {
            *remaining = initial_budget;
            return NULL;
        }
        *remaining = field_budget;
    }
    game_player *p = xcalloc(1, sizeof *p);
    p->declared = spec->declared ? declared_components_retain(spec->declared) : NULL;
    p->resources = spec->resources;
    if (p->resources.stage) {
        p->generation = p->resources.stage(
            p->resources.context,
            spec->assets,
            spec->asset_count,
            remaining,
            error,
            size
        );
        if (!p->generation) {
            if (options.fields) {
                material_fields_free(options.fields);
            }
            declared_components_free(p->declared);
            free(p);
            *remaining = initial_budget;
            return NULL;
        }
    }
    p->world = game_world_new(&options, error, size);
    if (!p->world) {
        if (options.fields) {
            material_fields_free(options.fields);
        }
        goto reject;
    }
    ecs_world *ecs = game_world_ecs(p->world);
    if (game_scene_register_components(spec->scene, ecs, error, size) != ECS_OK) {
        goto reject;
    }
    for (int i = 0; i < spec->prototype_count; i++) {
        if (game_scene_register_components(spec->prototypes[i].scene, ecs, error, size) !=
            ECS_OK) {
            goto reject;
        }
    }
    for (int i = 0; i < spec->scene_count; i++) {
        if (game_scene_register_components(spec->scenes[i].scene, ecs, error, size) !=
            ECS_OK) {
            goto reject;
        }
    }
    const engine_modules *services = game_world_modules(p->world);
    if (services->physics &&
        game_physics_configure(services->physics, game_scene_physics(spec->scene)) < 0) {
        fail(error, size, "Prepared scene physics configuration failed");
        goto reject;
    }
    game_source_staging *stage = game_package_stage_budget(
        game_world_sources(p->world),
        spec->modules,
        spec->module_count,
        spec->linker,
        NULL,
        spec->backend,
        remaining,
        error,
        size
    );
    if (!stage) {
        goto reject;
    }
    /* No dispatch until all registrations exist. A later scene/schema failure
     * destroys the unpublished-to-host player as one owned unit. */
    game_source_commit(stage);
    for (int i = 0; i < spec->scene_count; i++) {
        if (!behaviors_registered(game_world_sources(p->world), spec->scenes[i].scene)) {
            fail(error, size, "Prepared scene references an unregistered source module");
            goto reject;
        }
    }
    for (int i = 0; i < spec->prototype_count; i++) {
        if (!behaviors_registered(
                game_world_sources(p->world),
                spec->prototypes[i].scene
            )) {
            fail(error, size, "Prepared prototype references an unregistered source module");
            goto reject;
        }
        if (game_entities_register_prepared(
                game_world_entities(p->world),
                spec->prototypes[i].name,
                spec->prototypes[i].scene
            ) != 0) {
            fail(error, size, "Prepared prototype registration failed");
            goto reject;
        }
    }
    if (p->generation) {
        if (!p->resources.scene(p->resources.context, p->generation, spec->scene, error, size)) {
            goto reject;
        }
        for (int i = 0; i < spec->prototype_count; i++) {
            if (!p->resources.scene(
                    p->resources.context,
                    p->generation,
                    spec->prototypes[i].scene,
                    error,
                    size
                )) {
                goto reject;
            }
        }
        for (int i = 0; i < spec->scene_count; i++) {
            if (!p->resources
                     .scene(p->resources.context, p->generation, spec->scenes[i].scene, error, size)) {
                goto reject;
            }
        }
    }
    p->scene = game_scene_instantiate(spec->scene, ecs, game_world_sources(p->world), error, size);
    if (!p->scene) {
        goto reject;
    }
    if (game_entities_rebuild_global_bindings(game_world_entities(p->world)) != 0) {
        fail(error, size, "Prepared scene has conflicting global bindings");
        goto reject;
    }
    keep(p, start, spec->scene);
    for (int i = 0; i < spec->scene_count; i++) {
        keep(p, spec->scenes[i].name, spec->scenes[i].scene);
    }
    const char **names = NULL;
    for (int i = 0; i < acount(p->scenes); i++) {
        apush(names, p->scenes[i].name);
    }
    game_scene_flow *flow = game_world_scenes(p->world);
    game_scene_flow_set(flow, names, acount(names));
    game_scene_flow_enter(flow, start);
    afree(names);
    if (p->generation) {
        p->resources.commit(p->resources.context, p->generation, p->world);
        p->committed = true;
    }
    return p;
reject:
    game_player_free(p);
    *remaining = initial_budget;
    return NULL;
}
void game_player_free(game_player *p)
{
    assert(p);
    if (p->scene) {
        game_scene_instance_free(p->scene);
    }
    if (p->world) {
        game_world_free(p->world);
    }
    for (int i = 0; i < acount(p->scenes); i++) {
        free((void *)p->scenes[i].name);
        game_scene_free(p->scenes[i].scene);
    }
    afree(p->scenes);
    if (p->generation) {
        if (p->committed) {
            p->resources.retire(p->resources.context, p->generation);
        } else {
            p->resources.discard(p->resources.context, p->generation);
        }
    }
    declared_components_free(p->declared);
    free(p);
}
game_world *game_player_world(const game_player *p)
{
    return p->world;
}
const game_scene_instance *game_player_scene(const game_player *p)
{
    return p->scene;
}
bool game_player_load_scene(game_player *p, const char *name, char *error, size_t size)
{
    game_scene *scene = NULL;
    for (int i = 0; i < acount(p->scenes) && !scene; i++) {
        scene = !strcmp(p->scenes[i].name, name) ? p->scenes[i].scene : NULL;
    }
    if (!scene) {
        snprintf(error, size, "No scene named '%s'", name);
        return false;
    }
    const engine_modules *services = game_world_modules(p->world);
    engine_reset_scene(game_world_engine(p->world));
    game_scene_flow_enter(game_world_scenes(p->world), name);
    if (services->particles) {
        game_particles_clear(services->particles);
    }
    game_entities_reset(services->entities);
    game_scene_instance_free(p->scene);
    p->scene = NULL;
    if (services->physics &&
        game_physics_configure(services->physics, game_scene_physics(scene)) < 0) {
        return fail(error, size, "Prepared scene physics configuration failed");
    }
    p->scene = game_scene_instantiate(
        scene,
        game_world_ecs(p->world),
        game_world_sources(p->world),
        error,
        size
    );
    if (!p->scene) {
        return false;
    }
    if (game_entities_rebuild_global_bindings(services->entities) != 0) {
        return fail(error, size, "Prepared scene has conflicting global bindings");
    }
    /* Begin runs before held input reaches the new entities, as in scenarios. */
    game_source_registry *sources = game_world_sources(p->world);
    double now = engine_last_timing(game_world_engine(p->world))->simulation_time;
    if (game_source_begin(sources, now, ENGINE_FIXED_SECONDS) < 0) {
        return fail(error, size, game_source_error(sources));
    }
    return true;
}
