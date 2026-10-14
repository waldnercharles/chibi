#include "runtime/player.h"

#include "runtime/audio.h"
#include "runtime/component_catalog.h"
#include "runtime/material_field_prepared.h"
#include "runtime/save.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct player
{
    world *world;
    scene_instance *scene;
    /* Owned names and retained scenes Load Scene can start, the start first. */
    player_named_scene *scenes;
    player_resource_host resources;
    declared_components *declared;
    void *generation;
    bool committed;
};
/* The active prepared instance owns authored object names independently of
 * global ECS naming, so multiple prefab instances may keep identical names. */
static const char *instance_entity_name(void *context, ecs_entity entity)
{
    player *p = context;
    if (!p->scene) return NULL;
    const scene *source = scene_instance_scene(p->scene);
    for (int i = 0; i < scene_object_count(source); i++) {
        if (ecs_entity_to_u64(scene_instance_entity(p->scene, i)) == ecs_entity_to_u64(entity)) {
            return scene_object_name(source, i);
        }
    }
    return NULL;
}

static bool fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
}
uint64_t player_asset_fingerprint(const void *data, size_t length)
{
    const unsigned char *bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}
bool player_relative_path(const char *path)
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
bool player_asset_valid(const player_asset *asset)
{
    if (!asset || !asset->name || !asset->name[0] || strnlen(asset->name, 1025) > 1024 ||
        asset->kind < 0 || asset->kind >= PLAYER_ASSET_COUNT ||
        !asset->length || asset->length > PLAYER_MAX_ASSET_BYTES ||
        (asset->path && !player_relative_path(asset->path))) {
        return false;
    }
    /* Sounds travel inside the artifact. */
    if (asset->external) {
        return asset->kind != PLAYER_ASSET_SOUND && !asset->data &&
               asset->path && asset->content_hash;
    }
    if (!asset->data || (asset->kind == PLAYER_ASSET_SOUND &&
                         !sound_format_of(asset->data, asset->length))) {
        return false;
    }
    return !asset->content_hash ||
           asset->content_hash == player_asset_fingerprint(asset->data, asset->length);
}
/* The object's global binding name, or NULL when it binds none. */
static const char *binding_name(const scene *scene, int object)
{
    for (int i = 0; i < scene_object_component_count(scene, object); i++) {
        scene_component component = scene_object_component_at(scene, object, i);
        if (component.desc->id == COMPONENT_GlobalBinding.id) {
            const char *name = ((const GlobalBinding *)component.data)->name;
            return name && name[0] ? name : NULL;
        }
    }
    return NULL;
}
static int compare_names(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
bool player_scene_bindings_valid(const scene *scene, char *error, size_t size)
{
    const char **names = NULL;
    for (int i = 0; i < scene_object_count(scene); i++) {
        const char *name = binding_name(scene, i);
        if (name) {
            apush(names, name);
        }
    }
    asort(names, compare_names);
    const char *shared = NULL;
    for (int i = 1; !shared && i < acount(names); i++) {
        shared = strcmp(names[i - 1], names[i]) ? NULL : names[i];
    }
    afree(names);
    if (shared && size) {
        snprintf(error, size, "Scene '%s' binds two entities to the global '%s'", scene_name(scene), shared);
    }
    return !shared;
}
static bool behaviors_registered(const source_registry *sources, const scene *scene)
{
    for (int i = 0; i < scene_object_count(scene); i++) {
        for (int j = 0; j < scene_object_behavior_count(scene, i); j++) {
            if (!source_registered(sources, scene_object_behavior(scene, i, j))) {
                return false;
            }
        }
    }
    return true;
}
static void keep(player *p, const char *name, scene *scene)
{
    scene_retain(scene);
    apush(p->scenes, ((player_named_scene){ xstrdup(name), scene }));
}
player *player_new(const player_spec *spec, char *error, size_t size)
{
    assert(spec && (!size || error));
    if (spec->world.ecs || !spec->scene || spec->module_count < 0 ||
        spec->module_count > PACKAGE_MAX_MODULES ||
        (spec->module_count && !spec->modules) || spec->prototype_count < 0 ||
        spec->prototype_count > 1024 ||
        (spec->prototype_count && !spec->prototypes) || spec->scene_count < 0 ||
        spec->scene_count > 1024 || (spec->scene_count && !spec->scenes) ||
        spec->asset_count < 0 || spec->asset_count > PLAYER_MAX_ASSETS ||
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
        const player_asset *asset = &spec->assets[i];
        if (!player_asset_valid(asset) ||
            asset->length > PLAYER_MAX_ASSET_BYTES - asset_bytes) {
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
    const char *start = spec->scene_name ? spec->scene_name : scene_name(spec->scene);
    if (!start[0]) {
        fail(error, size, "Prepared player scene names are missing or duplicated");
        return NULL;
    }
    if (!player_scene_bindings_valid(spec->scene, error, size)) {
        return NULL;
    }
    for (int i = 0; i < spec->scene_count; i++) {
        const player_named_scene *s = &spec->scenes[i];
        if (!s->name || !s->name[0] || !s->scene || !strcmp(s->name, start)) {
            fail(error, size, "Prepared player scene names are missing or duplicated");
            return NULL;
        }
        if (!player_scene_bindings_valid(s->scene, error, size)) {
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
        const player_prototype *p = &spec->prototypes[i];
        if (!p->name || !p->name[0] || !p->scene || !scene_is_tree(p->scene)) {
            fail(error, size, "Prepared player prototype requires a name and one root object");
            return NULL;
        }
        if (!player_scene_bindings_valid(p->scene, error, size)) {
            return NULL;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(p->name, spec->prototypes[j].name)) {
                fail(error, size, "Prepared player prototype names are duplicated");
                return NULL;
            }
        }
    }
    size_t local_budget = PLAYER_MAX_ASSET_BYTES;
    size_t *remaining = spec->decoded_remaining ? spec->decoded_remaining : &local_budget;
    size_t initial_budget = *remaining;
    if (!spec->decoded_remaining) {
        size_t bytes = scene_storage_bytes(spec->scene);
        if (spec->world.events) {
            bytes += event_catalog_storage_bytes(spec->world.events);
        }
        if (bytes > *remaining) {
            fail(error, size, "Prepared player retained storage budget exceeded");
            return NULL;
        }
        *remaining -= bytes;
        scene **charged = NULL;
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
            bytes = known ? 0 : scene_storage_bytes(charged[i]);
            if (bytes > *remaining) {
                afree(charged);
                fail(error, size, "Prepared player retained storage budget exceeded");
                return NULL;
            }
            *remaining -= bytes;
        }
        afree(charged);
    }
    world_options options = spec->world;
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
    player *p = xcalloc(1, sizeof *p);
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
    p->world = world_new(&options, error, size);
    if (!p->world) {
        if (options.fields) {
            material_fields_free(options.fields);
        }
        goto reject;
    }
    entities_bind_names(world_entities(p->world), p, instance_entity_name);
    ecs_world *ecs = world_ecs(p->world);
    if (spec->services.bind) {
        if (size) {
            error[0] = 0;
        }
        if (!spec->services.bind(spec->services.context, p->world, error, size)) {
            if (size && !error[0]) {
                fail(error, size, "Host service binding failed");
            }
            goto reject;
        }
    }
    if (scene_register_components(spec->scene, ecs, error, size) != ECS_OK) {
        goto reject;
    }
    for (int i = 0; i < spec->prototype_count; i++) {
        if (scene_register_components(spec->prototypes[i].scene, ecs, error, size) !=
            ECS_OK) {
            goto reject;
        }
    }
    for (int i = 0; i < spec->scene_count; i++) {
        if (scene_register_components(spec->scenes[i].scene, ecs, error, size) != ECS_OK) {
            goto reject;
        }
    }
    const engine_modules *services = world_modules(p->world);
    if (services->audio) {
        /* The library keeps records, names and its own copy of each file, all
         * charged before any file is copied. */
        assert(!audio_sound_count(services->audio));
        int sounds = 0;
        size_t bytes = 0;
        for (int i = 0; i < spec->asset_count; i++) {
            const player_asset *asset = &spec->assets[i];
            if (asset->kind == PLAYER_ASSET_SOUND) {
                sounds++;
                bytes += strlen(asset->name) + 1 + asset->length;
            }
        }
        audio_reserve_sounds(services->audio, sounds);
        bytes += audio_sound_storage_bytes(services->audio);
        if (bytes > *remaining) {
            fail(error, size, "Prepared player retained storage budget exceeded");
            goto reject;
        }
        *remaining -= bytes;
        for (int i = 0; i < spec->asset_count; i++) {
            const player_asset *asset = &spec->assets[i];
            if (asset->kind == PLAYER_ASSET_SOUND) {
                audio_add_sound(services->audio, asset->name, asset->data, asset->length);
            }
        }
        assert(audio_sound_storage_bytes(services->audio) == bytes);
    }
    if ((services->physics &&
         physics_configure(services->physics, scene_physics(spec->scene)) < 0) ||
        (services->physics3d &&
         physics3d_configure(services->physics3d, scene_physics(spec->scene)) < 0)) {
        fail(error, size, "Prepared scene physics configuration failed");
        goto reject;
    }
    source_staging *stage = package_stage_budget(
        world_sources(p->world),
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
    source_commit(stage);
    for (int i = 0; i < spec->scene_count; i++) {
        if (!behaviors_registered(world_sources(p->world), spec->scenes[i].scene)) {
            fail(error, size, "Prepared scene references an unregistered source module");
            goto reject;
        }
    }
    for (int i = 0; i < spec->prototype_count; i++) {
        if (!behaviors_registered(world_sources(p->world), spec->prototypes[i].scene)) {
            fail(error, size, "Prepared prototype references an unregistered source module");
            goto reject;
        }
        if (entities_register_prepared(
                world_entities(p->world),
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
    p->scene = scene_instantiate(spec->scene, ecs, world_sources(p->world), error, size);
    if (!p->scene) {
        goto reject;
    }
    if (entities_rebuild_global_bindings(world_entities(p->world)) != 0) {
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
    scene_flow *flow = world_scenes(p->world);
    scene_flow_set(flow, names, acount(names));
    scene_flow_enter(flow, start);
    afree(names);
    if (p->generation) {
        p->resources.commit(p->resources.context, p->generation, p->world);
        p->committed = true;
    }
    return p;
reject:
    player_free(p);
    *remaining = initial_budget;
    return NULL;
}
void player_free(player *p)
{
    assert(p);
    if (p->world) entities_bind_names(world_entities(p->world), NULL, NULL);
    if (p->scene) {
        scene_instance_free(p->scene);
    }
    if (p->world) {
        world_free(p->world);
    }
    for (int i = 0; i < acount(p->scenes); i++) {
        free((void *)p->scenes[i].name);
        scene_free(p->scenes[i].scene);
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
world *player_world(const player *p)
{
    return p->world;
}
const scene_instance *player_scene(const player *p)
{
    return p->scene;
}
bool player_load_scene(player *p, const char *name, char *error, size_t size)
{
    scene *scene = NULL;
    for (int i = 0; i < acount(p->scenes) && !scene; i++) {
        scene = !strcmp(p->scenes[i].name, name) ? p->scenes[i].scene : NULL;
    }
    if (!scene) {
        snprintf(error, size, "No scene named '%s'", name);
        return false;
    }
    const engine_modules *services = world_modules(p->world);
    engine_reset_scene(world_engine(p->world));
    scene_flow_enter(world_scenes(p->world), name);
    if (services->particles) {
        particles_clear(services->particles);
    }
    entities_reset(services->entities);
    scene_instance_free(p->scene);
    p->scene = NULL;
    if ((services->physics &&
         physics_configure(services->physics, scene_physics(scene)) < 0) ||
        (services->physics3d &&
         physics3d_configure(services->physics3d, scene_physics(scene)) < 0)) {
        return fail(error, size, "Prepared scene physics configuration failed");
    }
    p->scene = scene_instantiate(scene, world_ecs(p->world), world_sources(p->world), error, size);
    if (!p->scene) {
        return false;
    }
    if (entities_rebuild_global_bindings(services->entities) != 0) {
        return fail(error, size, "Prepared scene has conflicting global bindings");
    }
    /* Begin runs before held input reaches the new entities, as in scenarios. */
    source_registry *sources = world_sources(p->world);
    double now = engine_last_timing(world_engine(p->world))->simulation_time;
    if (source_begin(sources, now, ENGINE_FIXED_SECONDS) < 0) {
        return fail(error, size, source_error(sources));
    }
    return true;
}

bool player_restart_scene_valid(
    const player *p,
    const char *name,
    const player_restart_input *input,
    char *error,
    size_t size
)
{
    engine *e = p ? world_engine(p->world) : NULL;
    if (!e || !name || !input || input->record_limit < 0 || input->record_limit > 4096 ||
        input->replay_count < 0 || input->replay_count > 4096 ||
        ((input->replay_count > 0) != (input->replay != NULL)) ||
        (input->record_limit && input->replay_count) ||
        ((input->record_limit || input->replay_count) && !engine_has_input(e))) {
        return fail(error, size, "Fresh scene input options are invalid");
    }
    if (input->initial_save_json) {
        save *candidate_save = save_new();
        bool save_valid = save_import(candidate_save, input->initial_save_json);
        save_free(candidate_save);
        if (!save_valid) {
            return fail(error, size, "Fresh scene save baseline is invalid");
        }
    }
    bool found = false;
    for (int i = 0; i < acount(p->scenes); i++) {
        found |= !strcmp(p->scenes[i].name, name);
    }
    if (!found) {
        return fail(error, size, "Fresh scene name is unavailable");
    }
    for (int i = 0; i < input->replay_count; i++) {
        if (!input_keyframe_valid(&input->replay[i]) ||
            (!i && input->replay[i].step != 0) ||
            (i && input->replay[i].step < input->replay[i - 1].step)) {
            return fail(error, size, "Fresh replay keys must be finite and ordered from step0");
        }
    }
    return true;
}
bool player_restart_scene(
    player *p,
    const char *name,
    const player_restart_input *input,
    bool *admitted,
    char *error,
    size_t size
)
{
    if (admitted) {
        *admitted = false;
    }
    if (!admitted || !player_restart_scene_valid(p, name, input, error, size)) {
        return false;
    }
    engine *e = world_engine(p->world);
    char *save_json = input->initial_save_json ? xstrdup(input->initial_save_json)
                                               : NULL;
    /* Callers may pass the engine's own borrowed replay/recorded keys. */
    input_keyframe *keys = NULL;
    for (int i = 0; i < input->replay_count; i++) {
        apush(keys, input->replay[i]);
    }
    url_requests_cancel(world_urls(p->world));
    engine_cancel_input(e);
    engine_stop_replay(e);
    engine_set_playing(e, false);
    engine_reset(e);
    engine_seed_random(e, input->seed);
    if (save_json) {
        bool restored_save = save_import(world_modules(p->world)->save, save_json);
        assert(restored_save);
        (void)restored_save;
        free(save_json);
    }
    *admitted = true;
    if (input->record_limit) {
        bool armed = engine_record_input_bounded(e, input->seed, input->record_limit);
        assert(armed);
        (void)armed;
    }
    if (input->replay_count) {
        engine_replay_input(e, keys, input->replay_count, input->seed);
    }
    afree(keys);
    /* Detach old actors before rebuilding programs so prior spawn counts cannot
     * affect initializer draws or resurrect per-actor state. */
    entities_reset(world_modules(p->world)->entities);
    scene_instance_free(p->scene);
    p->scene = NULL;
    source_registry *sources = world_sources(p->world);
    /* Restart in stable registration order. Each replacement commits its RNG
     * fork before the next program's initializers, matching initial loading. */
    for (int i = 0; i < source_program_count(sources); i++) {
        const char *registration = source_program_name_at(sources, i);
        execution_module *module = (execution_module *)execution_program_module(
            source_program(sources, registration)
        );
        if (source_replace(sources, registration, module, true) < 0) {
            return fail(error, size, source_error(sources));
        }
    }
    return player_load_scene(p, name, error, size);
}

declared_components *player_declared_components(const player *p)
{
    return p->declared;
}
