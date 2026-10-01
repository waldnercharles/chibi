#include "runtime/behaviors/live.h"

#include "assets/system_document.h"
#include "graph/document/events.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_message(char *message, size_t size, const char *format, ...)
{
    if (!message || !size) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(message, size, format, args);
    va_end(args);
}

static game_live_definition *find_definition(game_live_world *world, const char *name)
{
    for (int i = 0; i < acount(world->definitions); i++) {
        if (strcmp(world->definitions[i]->name, name) == 0) {
            return world->definitions[i];
        }
    }
    return NULL;
}

game_live_definition *game_live_definition_new(const char *asset_name, behavior_asset *working)
{
    if (!asset_name || !asset_name[0] || !working) {
        return NULL;
    }
    behavior_asset *applied = behavior_asset_clone(working);
    if (!applied) {
        return NULL;
    }
    game_live_definition *definition = (game_live_definition *)xcalloc(1, sizeof *definition);
    definition->name = xstrdup(asset_name);
    definition->working = working;
    definition->applied = applied;
    compile_report_init(&definition->diagnostics);
    return definition;
}

void game_live_definition_free(game_live_definition *definition)
{
    if (!definition) {
        return;
    }
    behavior_asset_free(definition->working);
    behavior_asset_free(definition->applied);
    compile_report_free(&definition->diagnostics);
    free(definition->name);
    free(definition->path);
    free(definition);
}

game_live_definition *game_live_find_definition(game_live_world *world, const char *asset_name)
{
    return asset_name ? find_definition(world, asset_name) : NULL;
}

const char *game_live_definition_path(const game_live_definition *definition)
{
    return definition ? definition->path : NULL;
}

int game_live_definition_set_path(game_live_definition *definition, const char *path)
{
    if (!definition || !path || !path[0]) {
        return -1;
    }
    char *copy = xstrdup(path);
    free(definition->path);
    definition->path = copy;
    return 0;
}

int game_live_definition_count(const game_live_world *world)
{
    return world ? acount(world->definitions) : 0;
}

game_live_definition *game_live_definition_at(game_live_world *world, int index)
{
    if (!world || index < 0 || index >= acount(world->definitions)) {
        return NULL;
    }
    return world->definitions[index];
}

/* Automatic node arrangement does not change authored behavior. */
static void omit_automatic_positions(av_value *value)
{
    if (av_is_obj(value)) {
        av_value *placed = av_obj_get(value, "placed");
        if (av_is_bool(placed) && !av_get_bool(placed)) {
            av_obj_remove_key(value, "x");
            av_obj_remove_key(value, "y");
            av_obj_remove_key(value, "placed");
        }
        for (size_t i = 0; i < av_obj_size(value); i++) {
            omit_automatic_positions(av_obj_value_at(value, i));
        }
    }
    if (av_is_arr(value)) {
        for (size_t i = 0; i < av_arr_size(value); i++) {
            omit_automatic_positions(av_arr_get(value, i));
        }
    }
}

static bool same_document(const behavior_asset *a, const behavior_asset *b)
{
    av_doc *doc = av_doc_new();
    av_value *left = behavior_asset_to_value(doc, a);
    av_value *right = behavior_asset_to_value(doc, b);
    omit_automatic_positions(left);
    omit_automatic_positions(right);
    bool same = av_equals(left, right);
    av_doc_free(doc);
    return same;
}

int game_live_sync_working(game_live_definition *definition, const behavior_asset *source)
{
    if (!definition || !source) {
        return -1;
    }
    bool unchanged = behavior_asset_equal(source, definition->working);
    bool edited = !unchanged && !same_document(source, definition->working);
    if (unchanged) {
        return 0;
    }
    behavior_asset *copy = behavior_asset_clone(source);
    if (!copy) {
        return -1;
    }
    behavior_asset_free(definition->working);
    definition->working = copy;
    definition->dirty |= edited;
    definition->unsaved |= edited;
    return 0;
}

int game_live_mark_dirty(game_live_definition *definition)
{
    if (!definition || !behavior_asset_mutable(definition->working)) {
        return -1;
    }
    definition->dirty = 1;
    definition->unsaved = true;
    return 0;
}

game_live_world *game_live_world_new(ecs_world *ecs)
{
    if (!ecs) {
        return NULL;
    }
    game_live_world *world = (game_live_world *)xcalloc(1, sizeof *world);
    world->ecs = ecs;
    world->behaviors = game_behavior_world_new(ecs);
    if (!world->behaviors) {
        free(world);
        return NULL;
    }
    world->entities = game_entities_new(ecs, world->behaviors);
    game_behavior_bind_entities(world->behaviors, world->entities);
    world->physics = game_physics_new(ecs, world->entities);
    world->particles = game_particles_new();
    world->materials = game_materials_new(ecs, world->entities, world->particles);
    game_entities_bind_materials(world->entities, world->materials);
    game_behavior_bind_particles(world->behaviors, world->particles);
    world->visibility = game_visibility_new(ecs, world->entities);
    game_behavior_bind_visibility(world->behaviors, world->visibility);
    return world;
}

void game_live_world_free(game_live_world *world)
{
    if (!world) {
        return;
    }
    render_shader_clear_world(world->ecs);
    game_physics_free(world->physics);
    game_materials_free(world->materials);
    game_visibility_free(world->visibility);
    game_entities_free(world->entities);
    game_behavior_world_free(world->behaviors);
    game_particles_free(world->particles);
    for (int i = 0; i < acount(world->definitions); i++) {
        game_live_definition *definition = world->definitions[i];
        definition->world = NULL;
        definition->registered = 0;
        game_live_definition_free(definition);
    }
    for (int i = 0; i < acount(world->systems); i++) {
        free(world->systems[i].name);
        free(world->systems[i].json);
    }
    afree(world->systems);
    afree(world->definitions);
    free(world);
}

static behavior_asset *linked_snapshot(
    game_live_world *world,
    const behavior_asset *source,
    const game_live_definition *changed,
    char *message,
    size_t size
)
{
    const behavior_asset **catalog = NULL;
    for (int i = 0; i < acount(world->definitions); i++) {
        game_live_definition *d = world->definitions[i];
        apush(catalog, d == changed ? d->working : d->applied);
    }
    behavior_asset *result = behavior_asset_link_libraries(source, catalog, acount(catalog), message, size);
    afree(catalog);
    return result;
}

static bool shader_domain(behavior_domain domain)
{
    return domain == BEHAVIOR_DOMAIN_MATERIAL || domain == BEHAVIOR_DOMAIN_SHAPE;
}

static bool validate_shader(const behavior_asset *asset, char *message, size_t size)
{
    if (asset->domain == BEHAVIOR_DOMAIN_SHAPE &&
        (!(asset->bounds[0] > 0) || !(asset->bounds[1] > 0) ||
         !isfinite(asset->bounds[0]) || !isfinite(asset->bounds[1]))) {
        set_message(message, size, "Shape bounds must be finite positive half extents.");
        return false;
    }
    shader_graph graph;
    shader_graph_init(&graph);
    shader_error error = { 0 };
    if (!shader_graph_from_graph(asset->graph, &graph, &error)) {
        set_message(message, size, "%s", error.message);
        return false;
    }
    char *glsl = shader_graph_glsl_named(&graph, "validate_library", &error);
    shader_graph_free(&graph);
    if (!glsl) {
        set_message(message, size, "%s", error.message);
        return false;
    }
    free(glsl);
    return true;
}

static int registration_error(game_live_definition *definition, const char *message)
{
    compile_diagnostic diagnostic = { .severity = CMPL_ERROR };
    snprintf(diagnostic.message, sizeof diagnostic.message, "%s", message);
    apush(definition->diagnostics.items, diagnostic);
    definition->diagnostics.error_count++;
    return -1;
}

int game_live_add_definition(game_live_world *world, game_live_definition *definition)
{
    if (!world || !definition) {
        return -1;
    }
    compile_report_free(&definition->diagnostics);
    compile_report_init(&definition->diagnostics);
    if (definition->world || find_definition(world, definition->name)) {
        return registration_error(definition, "A definition with this name is already registered.");
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION &&
        graph_event_host_name(definition->working->name)) {
        return registration_error(definition, "This event name is reserved by the host.");
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_EVENT) {
        char message[256];
        behavior_asset *linked = linked_snapshot(world, definition->working, NULL, message, sizeof message);
        if (!linked) {
            return registration_error(definition, message[0] ? message : graph_last_error());
        }
        if (game_behavior_register_asset(world->behaviors, definition->name, linked) != 0) {
            behavior_asset_free(linked);
            return registration_error(definition, "A runtime behavior with this name is already registered.");
        }
        behavior_asset_free(definition->applied);
        definition->applied = linked;
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_FIELD) {
        char message[256];
        behavior_asset *linked = linked_snapshot(world, definition->working, NULL, message, sizeof message);
        if (!linked ||
            !material_field_compile(game_materials_fields(world->materials), linked, message, sizeof message)) {
            behavior_asset_free(linked);
            return registration_error(definition, message);
        }
        behavior_asset_free(definition->applied);
        definition->applied = linked;
    }
    if (shader_domain(definition->working->domain)) {
        char message[256];
        behavior_asset *linked = linked_snapshot(world, definition->working, NULL, message, sizeof message);
        if (!linked) {
            return registration_error(definition, message[0] ? message : graph_last_error());
        }
        shader_error error = { 0 };
        if (!render_shader_upsert(linked, &error)) {
            behavior_asset_free(linked);
            return registration_error(definition, error.message);
        }
        behavior_asset_free(definition->applied);
        definition->applied = linked;
    }
    definition->world = world;
    definition->registered = 1;
    apush(world->definitions, definition);
    return 0;
}

int game_live_rename_definition(game_live_world *world, const char *from, const char *to)
{
    game_live_definition *definition = find_definition(world, from);
    if (!definition || find_definition(world, to) ||
        (definition->working->domain == BEHAVIOR_DOMAIN_EVENT &&
         game_behavior_rename_asset(world->behaviors, from, to) != 0)) {
        return -1;
    }
    free(definition->name);
    definition->name = xstrdup(to);
    return 0;
}

int game_live_remove_definition(game_live_world *world, const char *asset_name)
{
    if (!world || !asset_name) {
        return -1;
    }
    for (int i = 0; i < acount(world->definitions); i++) {
        game_live_definition *definition = world->definitions[i];
        if (strcmp(definition->name, asset_name) != 0 ||
            (definition->working->domain == BEHAVIOR_DOMAIN_EVENT &&
             game_behavior_unregister_asset(
                 world->behaviors,
                 asset_name,
                 &definition->applied
             ) != 0)) {
            continue;
        }
        definition->world = NULL;
        definition->registered = 0;
        adel(world->definitions, i);
        game_live_definition_free(definition);
        return 0;
    }
    return -1;
}

static int update_library(
    game_live_world *world,
    game_live_definition *definition,
    char *message,
    size_t size,
    bool commit
)
{
    const char *error = behavior_asset_validate_library(definition->working);
    if (error) {
        set_message(message, size, "%s", error);
        return -1;
    }
    behavior_asset *library = linked_snapshot(world, definition->working, definition, message, size);
    if (!library) {
        return -1;
    }
    compile_report report;
    compile_report_init(&report);
    behavior_asset_compile(library, &report);
    bool blocked = compile_report_blocked(&report);
    if (blocked) {
        set_message(message, size, "%s", compile_report_first_error(&report)->message);
    }
    compile_report_free(&report);
    behavior_asset_free(library);
    if (blocked) {
        return -1;
    }
    game_live_definition **dependents = NULL;
    behavior_asset **snapshots = NULL;
    int status = -1;
    for (int i = 0; i < acount(world->definitions); i++) {
        game_live_definition *d = world->definitions[i];
        if (d->working->domain != BEHAVIOR_DOMAIN_EVENT &&
            !shader_domain(d->working->domain) &&
            d->working->domain != BEHAVIOR_DOMAIN_FIELD) {
            continue;
        }
        bool uses = false;
        for (int g = 0; g < acount(d->applied->graphs); g++) {
            const char *name = d->applied->graphs[g]->source_asset;
            uses |= name && strcmp(name, definition->name) == 0;
        }
        if (!uses) {
            continue;
        }
        behavior_asset *snapshot = linked_snapshot(world, d->working, definition, message, size);
        if (!snapshot) {
            goto cleanup;
        }
        apush(dependents, d);
        apush(snapshots, snapshot);
        if (shader_domain(snapshot->domain)) {
            if (!validate_shader(snapshot, message, size)) {
                goto cleanup;
            }
            continue;
        }
        if (snapshot->domain == BEHAVIOR_DOMAIN_FIELD) {
            field_program *program = field_program_compile(snapshot, message, size);
            if (!program) {
                goto cleanup;
            }
            field_program_free(program);
            continue;
        }
        if (!snapshot->owner_type_name) {
            behavior_asset_set_owner_type(snapshot, "ecs.entity");
        }
        compile_report_init(&report);
        behavior_asset_compile(snapshot, &report);
        blocked = compile_report_blocked(&report);
        if (blocked) {
            set_message(message, size, "%s", compile_report_first_error(&report)->message);
        }
        compile_report_free(&report);
        if (blocked) {
            goto cleanup;
        }
        int ready = game_behavior_can_preview_asset(world->behaviors, d->name, snapshot);
        if (ready != INSTANCE_UPDATE_READY) {
            status = ready;
            set_message(message, size, "Library update waits for dependent behavior executions or a restart.");
            goto cleanup;
        }
    }
    if (!commit) {
        status = 0;
        goto cleanup;
    }
    for (int i = 0; i < acount(dependents); i++) {
        behavior_asset *old = NULL;
        if (snapshots[i]->domain == BEHAVIOR_DOMAIN_FIELD) {
            bool applied = material_field_compile(
                game_materials_fields(world->materials),
                snapshots[i],
                message,
                size
            );
            assert(applied);
            old = dependents[i]->applied;
        } else if (shader_domain(snapshots[i]->domain)) {
            shader_error error = { 0 };
            bool applied = render_shader_upsert(snapshots[i], &error);
            assert(applied);
            old = dependents[i]->applied;
        } else {
            int result = game_behavior_preview_asset(
                world->behaviors,
                dependents[i]->name,
                snapshots[i],
                &old
            );
            assert(result == INSTANCE_UPDATE_READY);
        }
        dependents[i]->applied = snapshots[i];
        snapshots[i] = NULL;
        dependents[i]->dirty = 0;
        behavior_asset_free(old);
    }
    library = behavior_asset_clone(definition->working);
    assert(library);
    behavior_asset_free(definition->applied);
    definition->applied = library;
    definition->dirty = 0;
    status = 0;
    set_message(message, size, "Updated library and %d dependent behaviors.", acount(dependents));
cleanup:
    for (int i = 0; i < acount(snapshots); i++) {
        behavior_asset_free(snapshots[i]);
    }
    afree(snapshots);
    afree(dependents);
    return status;
}

static int update_definition(
    game_live_world *world,
    game_live_definition *definition,
    char *message,
    size_t message_size,
    bool restart
)
{
    if (!world || !definition || definition->world != world || !definition->registered) {
        set_message(message, message_size, "definition is not registered with this world");
        return -1;
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION &&
        graph_event_host_name(definition->working->name)) {
        set_message(message, message_size, "Event name is reserved for host dispatch");
        return -1;
    }
    if (!definition->dirty) {
        set_message(message, message_size, "definition has no unapplied edits");
        return 0;
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_LIBRARY ||
        definition->working->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        return update_library(world, definition, message, message_size, true);
    }
    behavior_asset *snapshot = linked_snapshot(world, definition->working, NULL, message, message_size);
    if (!snapshot) {
        set_message(message, message_size, "working definition could not be loaded: %s", graph_last_error());
        return -1;
    }
    compile_report_free(&definition->diagnostics);
    compile_report_init(&definition->diagnostics);
    if (behavior_asset_compile(snapshot, &definition->diagnostics) != 0 &&
        compile_report_blocked(&definition->diagnostics)) {
        const compile_diagnostic *diagnostic = compile_report_first_error(
            &definition->diagnostics
        );
        set_message(message, message_size, "apply blocked: %s", diagnostic->message);
        behavior_asset_free(snapshot);
        return -1;
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_FIELD &&
        !material_field_compile(game_materials_fields(world->materials), snapshot, message, message_size)) {
        behavior_asset_free(snapshot);
        return -1;
    }
    if (definition->working->domain != BEHAVIOR_DOMAIN_EVENT) {
        if (definition->working->domain == BEHAVIOR_DOMAIN_MATERIAL ||
            definition->working->domain == BEHAVIOR_DOMAIN_SHAPE) {
            shader_error error = { 0 };
            if (!render_shader_upsert(snapshot, &error)) {
                set_message(message, message_size, "%s", error.message);
                behavior_asset_free(snapshot);
                return -1;
            }
        }
        behavior_asset_free(definition->applied);
        definition->applied = snapshot;
        definition->dirty = 0;
        set_message(message, message_size, "Updated %s.", definition->name);
        return 0;
    }
    const compile_diagnostic *backend_error = behavior_program_diagnostic(
        behavior_program_publish(snapshot)
    );
    if (backend_error) {
        apush(definition->diagnostics.items, *backend_error);
        definition->diagnostics.error_count++;
        set_message(message, message_size, "apply blocked: %s", backend_error->message);
        behavior_asset_free(snapshot);
        return -1;
    }
    behavior_asset *old = NULL;
    int result = restart
                   ? game_behavior_replace_asset(world->behaviors, definition->name, snapshot, &old)
                   : game_behavior_preview_asset(world->behaviors, definition->name, snapshot, &old);
    if (!restart && result > 0) {
        set_message(
            message,
            message_size,
            result == INSTANCE_UPDATE_BUSY
                ? "Waiting for active behavior executions to finish."
                : "Restart required to apply changed behavior state."
        );
        behavior_asset_free(snapshot);
        return result;
    }
    if (result != 0) {
        set_message(message, message_size, "definition replacement failed");
        behavior_asset_free(snapshot);
        return -1;
    }
    definition->applied = snapshot;
    definition->dirty = 0;
    behavior_asset_free(old);
    set_message(
        message,
        message_size,
        restart ? "applied %s: destroyed and recreated its instances" : "Preview updated: %s",
        definition->name
    );
    return 0;
}

int game_live_can_preview_library(
    game_live_world *world,
    game_live_definition *definition,
    char *message,
    size_t size
)
{
    return update_library(world, definition, message, size, false);
}

int game_live_apply(game_live_world *world, game_live_definition *definition, char *message, size_t message_size)
{
    return update_definition(world, definition, message, message_size, true);
}

int game_live_preview(game_live_world *world, game_live_definition *definition, char *message, size_t message_size)
{
    return update_definition(world, definition, message, message_size, false);
}

behavior_asset *game_live_link(game_live_world *world, const behavior_asset *source)
{
    char message[256];
    return linked_snapshot(world, source, NULL, message, sizeof message);
}

static const ecs_component_desc *resolve_system_component(const char *name, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *c = GAME_REGISTRY_COMPONENTS[i];
        if (!strcmp(c->name, name) || !strcmp(c->key, name)) {
            return c;
        }
    }
    return NULL;
}
const game_live_system_asset *game_live_find_system(const game_live_world *world, const char *name)
{
    for (int i = 0; name && i < acount(world->systems); i++) {
        if (!strcmp(world->systems[i].name, name)) {
            return &world->systems[i];
        }
    }
    return NULL;
}
int game_live_register_system(
    game_live_world *world,
    const char *name,
    const char *json,
    char *error,
    size_t size
)
{
    game_system_document *
        document = game_system_document_load(json, resolve_system_component, NULL, error, size);
    if (!document) {
        return -1;
    }
    compile_report report;
    compile_report_init(&report);
    int invalid = behavior_asset_compile_for_system(
        game_system_document_behavior_asset(document),
        &report
    );
    if (invalid) {
        snprintf(error, size, "%s", compile_report_first_error(&report)->message);
    }
    compile_report_free(&report);
    game_system_document_free(document);
    if (invalid) {
        return -1;
    }
    for (int i = 0; i < acount(world->systems); i++) {
        game_live_system_asset *asset = &world->systems[i];
        if (strcmp(asset->name, name)) {
            continue;
        }
        if (!strcmp(asset->json, json)) {
            return 0;
        }
        free(asset->json);
        asset->json = xstrdup(json);
        asset->revision++;
        return 0;
    }
    apush(world->systems, ((game_live_system_asset){ xstrdup(name), xstrdup(json), 1 }));
    return 0;
}
