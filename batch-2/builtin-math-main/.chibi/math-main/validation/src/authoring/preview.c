#include "authoring/preview.h"

#include "authoring/preview_source_internal.h"
#include "authoring/shader_prepare.h"
#include "authoring/source_graph_library.h"
#include "authoring/source_presentation.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/material_field.h"
#include "graph/compiler/validation.h"
#include "graph/document/events.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/value_copy.h"
#include "graph/modeling/model_compile.h"
#include "graph/shader/shader_graph.h"
#include "render/shader_catalog.h"
#include "runtime/component_catalog.h"
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

static preview_definition *find_definition(preview_world *world, const char *name)
{
    for (int i = 0; i < acount(world->definitions); i++) {
        if (strcmp(world->definitions[i]->name, name) == 0) {
            return world->definitions[i];
        }
    }
    return NULL;
}

preview_definition *preview_definition_new(const char *asset_name, behavior_asset *working)
{
    if (!asset_name || !asset_name[0] || !working) {
        return NULL;
    }
    behavior_asset *applied = behavior_asset_clone(working);
    if (!applied) {
        return NULL;
    }
    preview_definition *definition = (preview_definition *)xcalloc(1, sizeof *definition);
    definition->name = xstrdup(asset_name);
    definition->working = working;
    definition->applied = applied;
    compile_report_init(&definition->diagnostics);
    return definition;
}

preview_definition *preview_projection_new(const char *name, authoring_source_presentation *presentation)
{
    assert(name && name[0] && presentation);
    preview_definition *definition = xcalloc(1, sizeof *definition);
    definition->name = xstrdup(name);
    definition->working_presentation = authoring_source_presentation_retain(presentation);
    definition->applied_presentation = authoring_source_presentation_clone(presentation);
    definition->working = authoring_source_presentation_view(definition->working_presentation);
    definition->applied = authoring_source_presentation_view(definition->applied_presentation);
    compile_report_init(&definition->diagnostics);
    return definition;
}
int preview_projection_set(
    preview_definition *definition,
    authoring_source_presentation *presentation,
    bool applied
)
{
    assert(definition && presentation && definition->working_presentation);
    if (applied &&
        (!definition->world ||
         !source_registered(definition->world->runtime->sources, definition->name))) {
        return -1;
    }
    authoring_source_presentation **slot = applied
                                             ? &definition->applied_presentation
                                             : &definition->working_presentation;
    authoring_source_presentation *next = authoring_source_presentation_retain(presentation);
    authoring_source_presentation_free(*slot);
    *slot = next;
    if (applied) {
        definition->applied = authoring_source_presentation_view(next);
    } else {
        definition->working = authoring_source_presentation_view(next);
    }
    definition->dirty = strcmp(
                            authoring_source_presentation_text(definition->working_presentation),
                            authoring_source_presentation_text(definition->applied_presentation)
                        ) != 0;
    return 0;
}
void preview_definition_free(preview_definition *definition)
{
    if (!definition) {
        return;
    }
    assert(!definition->registered);
    if (definition->working_presentation) {
        authoring_source_presentation_free(definition->working_presentation);
        authoring_source_presentation_free(definition->applied_presentation);
    } else {
        behavior_asset_free(definition->working);
        behavior_asset_free(definition->applied);
    }
    behavior_asset_free(definition->field_source);
    compile_report_free(&definition->diagnostics);
    free(definition->name);
    free(definition->path);
    free(definition);
}

preview_definition *preview_find_definition(preview_world *world, const char *asset_name)
{
    return asset_name ? find_definition(world, asset_name) : NULL;
}

const char *preview_definition_path(const preview_definition *definition)
{
    return definition ? definition->path : NULL;
}

int preview_definition_set_path(preview_definition *definition, const char *path)
{
    if (!definition || !path || !path[0]) {
        return -1;
    }
    char *copy = xstrdup(path);
    free(definition->path);
    definition->path = copy;
    return 0;
}

int preview_definition_count(const preview_world *world)
{
    return world ? acount(world->definitions) : 0;
}

preview_definition *preview_definition_at(preview_world *world, int index)
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

int preview_sync_working(preview_definition *definition, const behavior_asset *source)
{
    if (!definition || !source) {
        return -1;
    }
    if (definition->working_presentation) {
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

int preview_mark_dirty(preview_definition *definition)
{
    if (!definition || !behavior_asset_mutable(definition->working)) {
        return -1;
    }
    definition->dirty = 1;
    definition->unsaved = true;
    return 0;
}

preview_world *preview_world_new(ecs_world *ecs)
{
    preview_world *world = xcalloc(1, sizeof *world);
    world->runtime = preview_runtime_new(ecs);
    if (!world->runtime) {
        free(world);
        return NULL;
    }
    return world;
}

void preview_world_free(preview_world *world)
{
    if (!world) {
        return;
    }
    assert(!preview_sources_staged(world));
    render_shader_clear_world(world->runtime->ecs);
    preview_runtime_free(world->runtime);
    for (int i = 0; i < acount(world->definitions); i++) {
        preview_definition *definition = world->definitions[i];
        definition->world = NULL;
        definition->registered = 0;
        preview_definition_free(definition);
    }
    afree(world->definitions);
    preview_sources_free(world);
    free(world);
}

/* Links against the world's graph definitions and the source libraries in
 * `code`, which may be a staged collection. */
static behavior_asset *link_snapshot(
    preview_world *world,
    const behavior_asset *source,
    const preview_definition *changed,
    const authoring_sources *code,
    char *message,
    size_t size
)
{
    for (int i = 0; i < acount(world->definitions); i++) {
        const preview_definition *definition = world->definitions[i];
        if (definition->working_presentation &&
            (source == definition->working || source == definition->applied)) {
            set_message(message, size, "Source presentations do not supply executable graph state.");
            return NULL;
        }
    }
    if (source->domain == BEHAVIOR_DOMAIN_EVENT ||
        source->domain == BEHAVIOR_DOMAIN_LIBRARY) {
        set_message(message, size, "Executable source requires canonical preparation.");
        return NULL;
    }
    if (source->domain == BEHAVIOR_DOMAIN_MODEL) {
        return behavior_asset_clone(source);
    }
    const behavior_asset **catalog = NULL;
    for (int i = 0; i < acount(world->definitions); i++) {
        preview_definition *d = world->definitions[i];
        if (!d->working_presentation) {
            apush(catalog, d == changed ? d->working : d->applied);
        }
    }
    behavior_asset **libraries = code ? authoring_source_graph_libraries(code) : NULL;
    for (int i = 0; i < acount(libraries); i++) {
        apush(catalog, libraries[i]);
    }
    behavior_asset *result = behavior_asset_link_libraries(source, catalog, acount(catalog), message, size);
    afree(catalog);
    authoring_source_graph_libraries_free(libraries);
    return result;
}

static behavior_asset *linked_snapshot(
    preview_world *world,
    const behavior_asset *source,
    const preview_definition *changed,
    char *message,
    size_t size
)
{
    return link_snapshot(world, source, changed, preview_sources_applied(world), message, size);
}

static bool shader_domain(behavior_domain domain)
{
    return domain == BEHAVIOR_DOMAIN_MATERIAL ||
           domain == BEHAVIOR_DOMAIN_SHAPE || domain == BEHAVIOR_DOMAIN_MODEL ||
           domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY;
}

static bool validate_shader(const behavior_asset *asset, char *message, size_t size)
{
    if (asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        shader_error error = { 0 };
        bool valid = render_shader_library_validate(asset, &error);
        if (!valid) {
            set_message(message, size, "%s", error.message);
        }
        return valid;
    }
    if (asset->domain == BEHAVIOR_DOMAIN_SHAPE &&
        (!(asset->bounds[0] > 0) || !(asset->bounds[1] > 0) ||
         !isfinite(asset->bounds[0]) || !isfinite(asset->bounds[1]))) {
        set_message(message, size, "Shape bounds must be finite positive half extents.");
        return false;
    }
    shader_graph graph;
    shader_graph_init(&graph);
    shader_error error = { 0 };
    model_program model;
    model_program_init(&model);
    bool valid;
    if (asset->domain == BEHAVIOR_DOMAIN_MODEL) {
        valid = model_compile(asset, &model, &error);
        if (valid) {
            graph = model.shader;
            shader_graph_init(&model.shader);
        }
    } else {
        valid = shader_graph_from_graph(asset->graph, &graph, &error);
    }
    model_program_free(&model);
    if (!valid) {
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

static int registration_error(preview_definition *definition, const char *message)
{
    compile_diagnostic diagnostic = { .severity = CMPL_ERROR };
    snprintf(diagnostic.message, sizeof diagnostic.message, "%s", message);
    apush(definition->diagnostics.items, diagnostic);
    definition->diagnostics.error_count++;
    return -1;
}

/* Keep the authored field independently of its linked applied generation. */
static bool publish_field(
    preview_world *world,
    preview_definition *definition,
    const behavior_asset *linked,
    char *error,
    size_t size
)
{
    behavior_asset *source = behavior_asset_clone(definition->working);
    if (!source) {
        set_message(error, size, "Cannot retain the authored field: %s", graph_last_error());
        return false;
    }
    if (!material_field_compile(materials_fields(world->runtime->materials), linked, error, size)) {
        behavior_asset_free(source);
        return false;
    }
    behavior_asset_free(definition->field_source);
    definition->field_source = source;
    return true;
}

int preview_add_definition(preview_world *world, preview_definition *definition)
{
    if (!world || !definition || preview_sources_staged(world)) {
        return -1;
    }
    if (definition->working_presentation) {
        return registration_error(definition, "Source presentations require canonical module publication.");
    }
    compile_report_free(&definition->diagnostics);
    compile_report_init(&definition->diagnostics);
    if (definition->world || find_definition(world, definition->name)) {
        return registration_error(definition, "A definition with this name is already registered.");
    }
    if (source_registered(world->runtime->sources, definition->name) ||
        definition->working->domain == BEHAVIOR_DOMAIN_EVENT ||
        definition->working->domain == BEHAVIOR_DOMAIN_LIBRARY) {
        return registration_error(definition, "Executable source uses canonical module publication.");
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION &&
        host_event_reserved(definition->working->name)) {
        return registration_error(definition, "This event name is reserved by the host.");
    }
    char message[256] = { 0 };
    behavior_asset *linked = linked_snapshot(world, definition->working, NULL, message, sizeof message);
    if (!linked) {
        return registration_error(definition, message[0] ? message : graph_last_error());
    }
    if (linked->domain != BEHAVIOR_DOMAIN_MODEL &&
        graph_asset_compile(linked, &definition->diagnostics)) {
        behavior_asset_free(linked);
        return -1;
    }
    if (linked->domain == BEHAVIOR_DOMAIN_FIELD &&
        !publish_field(world, definition, linked, message, sizeof message)) {
        behavior_asset_free(linked);
        return registration_error(definition, message);
    }
    if (shader_domain(linked->domain)) {
        shader_error error = { 0 };
        if (!render_shader_upsert(linked, &error)) {
            behavior_asset_free(linked);
            registration_error(definition, error.message);
            alast(definition->diagnostics.items).node_id = error.node;
            return -1;
        }
    }
    behavior_asset_free(definition->applied);
    definition->applied = linked;
    definition->world = world;
    definition->registered = 1;
    apush(world->definitions, definition);
    return 0;
}

int preview_add_projection(preview_world *world, preview_definition *definition)
{
    if (!world || !definition || preview_sources_staged(world)) {
        return -1;
    }
    if (definition->world || find_definition(world, definition->name)) {
        return registration_error(definition, "A definition with this name is already registered.");
    }
    if (!source_registered(world->runtime->sources, definition->name)) {
        return registration_error(definition, "Source projection requires a prepared canonical module.");
    }
    definition->world = world;
    definition->registered = 1;
    apush(world->definitions, definition);
    return 0;
}

static behavior_asset *publish_snapshot(preview_definition *, behavior_asset *);

static void replace_applied(preview_world *world, preview_definition *definition, behavior_asset *snapshot)
{
    (void)world;
    behavior_asset *old = publish_snapshot(definition, snapshot);
    behavior_asset_free(old);
}

static void rename_model_material_references(behavior_asset *asset, const char *from, const char *to)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (n->desc == &NODE_ModelMaterialSlot && n->reference_asset &&
                !strcmp(n->reference_asset, from)) {
                n->reference_asset = graph_own_str(g, to, strlen(to));
            }
            for (int k = 0; n->desc == &NODE_ModelInstance && k < n->input_count; k++) {
                pin_spec *pin = &n->input_pins[k];
                if (pin->type == PIN_STR && pin->type_name &&
                    !strcmp(pin->type_name, "MaterialAsset") &&
                    graph_text_cstring(g, pin->default_value) &&
                    !strcmp(graph_text_cstring(g, pin->default_value), from)) {
                    pin_value value = graph_text_value(g, to, strlen(to));
                    value.type_name = "MaterialAsset";
                    int changed = graph_set_pin_default(g, n, false, k, &value);
                    assert(!changed);
                    (void)changed;
                }
            }
        }
    }
}
int preview_rename_definition(preview_world *world, const char *from, const char *to)
{
    if (preview_sources_staged(world)) {
        return -1;
    }
    preview_definition *definition = find_definition(world, from);
    if (!definition || find_definition(world, to) ||
        source_registered(world->runtime->sources, to)) {
        return -1;
    }
    if (definition->working_presentation) {
        return registration_error(definition, "Source projections are renamed through canonical source preparation.");
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        behavior_asset *snapshot = behavior_asset_clone(definition->applied);
        shader_error error = { 0 };
        if (!snapshot || behavior_asset_set_name(snapshot, to) ||
            !render_shader_library_upsert(snapshot, &error)) {
            behavior_asset_free(snapshot);
            return registration_error(definition, error.message[0] ? error.message : "Shader library could not be renamed.");
        }
        render_shader_library_remove(from);
        replace_applied(world, definition, snapshot);
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_MODEL) {
        shader_error error = { 0 };
        if (!render_shader_model_rename(from, to, &error)) {
            registration_error(definition, error.message);
            return -1;
        }
        for (int i = 0; i < acount(world->definitions); i++) {
            preview_definition *d = world->definitions[i];
            if (d->working->domain != BEHAVIOR_DOMAIN_MODEL) {
                continue;
            }
            behavior_asset *snapshots[] = { d->working,
                                            behavior_asset_clone(d->applied) };
            for (int j = 0; j < 2; j++) {
                behavior_asset *asset = snapshots[j];
                if (d == definition) {
                    int renamed = behavior_asset_set_name(asset, to);
                    assert(!renamed);
                    (void)renamed;
                }
                for (int k = 0; k < acount(asset->graphs); k++) {
                    graph *g = asset->graphs[k];
                    for (int n = 0; n < acount(g->nodes); n++) {
                        node *instance = g->nodes[n];
                        if (instance->desc == &NODE_ModelInstance &&
                            instance->reference_asset &&
                            !strcmp(instance->reference_asset, from)) {
                            instance->reference_asset = graph_own_str(g, to, strlen(to));
                        }
                    }
                }
            }
            replace_applied(world, d, snapshots[1]);
        }
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_MATERIAL) {
        shader_error error = { 0 };
        if (!render_shader_material_rename(from, to, &error)) {
            registration_error(definition, error.message);
            return -1;
        }
        int renamed = behavior_asset_set_name(definition->working, to);
        assert(!renamed);
        behavior_asset *snapshot = behavior_asset_clone(definition->applied);
        renamed = behavior_asset_set_name(snapshot, to);
        assert(!renamed);
        (void)renamed;
        replace_applied(world, definition, snapshot);
        for (int i = 0; i < acount(world->definitions); i++) {
            preview_definition *d = world->definitions[i];
            if (d->working->domain == BEHAVIOR_DOMAIN_MODEL) {
                rename_model_material_references(d->working, from, to);
                behavior_asset *snapshot = behavior_asset_clone(d->applied);
                rename_model_material_references(snapshot, from, to);
                replace_applied(world, d, snapshot);
            }
        }
    }
    if (behavior_asset_set_name(definition->working, to) ||
        behavior_asset_set_name(definition->applied, to) ||
        (definition->field_source &&
         behavior_asset_set_name(definition->field_source, to))) {
        return -1;
    }
    free(definition->name);
    definition->name = xstrdup(to);
    return 0;
}

int preview_remove_projection(preview_world *world, const char *name)
{
    assert(world && name);
    if (preview_sources_staged(world)) {
        return -1;
    }
    preview_definition *definition = preview_find_definition(world, name);
    if (!definition || !definition->working_presentation) {
        return -1;
    }
    for (int i = 0; i < acount(world->definitions); i++) {
        if (world->definitions[i] == definition) {
            adel(world->definitions, i);
            break;
        }
    }
    definition->registered = 0;
    definition->world = NULL;
    preview_definition_free(definition);
    return 0;
}
int preview_remove_definition(preview_world *world, const char *asset_name)
{
    if (preview_sources_staged(world)) {
        return -1;
    }
    preview_definition *definition = preview_find_definition(world, asset_name);
    if (!definition) {
        return -1;
    }
    shader_error error = { 0 };
    if ((definition->working->domain == BEHAVIOR_DOMAIN_MODEL &&
         !render_shader_model_remove(asset_name, &error)) ||
        (definition->working->domain == BEHAVIOR_DOMAIN_MATERIAL &&
         !render_shader_material_remove(asset_name, &error))) {
        return registration_error(definition, error.message);
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        render_shader_library_remove(asset_name);
    }
    if (definition->working_presentation) {
        if (source_remove(world->runtime->sources, asset_name)) {
            return -1;
        }
    }
    for (int i = 0; i < acount(world->definitions); i++) {
        if (world->definitions[i] == definition) {
            adel(world->definitions, i);
            break;
        }
    }
    definition->world = NULL;
    definition->registered = 0;
    preview_definition_free(definition);
    return 0;
}

/* Authoring owns graph snapshots; runtime consumes prepared artifacts only. */
static behavior_asset *publish_snapshot(preview_definition *definition, behavior_asset *snapshot)
{
    assert(!definition->working_presentation);
    behavior_asset *old = definition->applied;
    definition->applied = snapshot;
    return old;
}

static int update_library(
    preview_world *world,
    preview_definition *definition,
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
    graph_asset_compile(library, &report);
    bool blocked = compile_report_blocked(&report);
    if (blocked) {
        set_message(message, size, "%s", compile_report_first_error(&report)->message);
    }
    compile_report_free(&report);
    if (!blocked) {
        blocked = !validate_shader(library, message, size);
    }
    if (blocked) {
        behavior_asset_free(library);
        return -1;
    }
    preview_definition **dependents = NULL;
    behavior_asset **snapshots = NULL;
    int status = -1;
    for (int i = 0; i < acount(world->definitions); i++) {
        preview_definition *d = world->definitions[i];
        if (!shader_domain(d->working->domain) &&
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
    }
    if (!commit) {
        status = 0;
        goto cleanup;
    }
    shader_error diagnostic = { 0 };
    if (!render_shader_library_upsert(library, &diagnostic)) {
        set_message(message, size, "%s", diagnostic.message);
        goto cleanup;
    }
    for (int i = 0; i < acount(dependents); i++) {
        behavior_asset *old = NULL;
        if (snapshots[i]->domain == BEHAVIOR_DOMAIN_FIELD) {
            bool applied = material_field_compile(
                materials_fields(world->runtime->materials),
                snapshots[i],
                message,
                size
            );
            assert(applied);
            (void)applied;
            old = dependents[i]->applied;
        } else if (shader_domain(snapshots[i]->domain)) {
            shader_error error = { 0 };
            bool applied = render_shader_upsert(snapshots[i], &error);
            assert(applied);
            (void)applied;
            old = dependents[i]->applied;
        }
        old = publish_snapshot(dependents[i], snapshots[i]);
        snapshots[i] = NULL;
        dependents[i]->dirty = 0;
        behavior_asset_free(old);
    }
    behavior_asset *previous = publish_snapshot(definition, library);
    library = NULL;
    behavior_asset_free(previous);
    definition->dirty = 0;
    status = 0;
    set_message(message, size, "Updated shader library and %d dependent assets.", acount(dependents));
cleanup:
    behavior_asset_free(library);
    for (int i = 0; i < acount(snapshots); i++) {
        behavior_asset_free(snapshots[i]);
    }
    afree(snapshots);
    afree(dependents);
    return status;
}

bool preview_fields_relink(
    preview_world *world,
    const authoring_sources *code,
    const char *const *libraries,
    int count,
    preview_field_relink **out,
    char *message,
    size_t size
)
{
    assert(!*out);
    for (int i = 0; i < acount(world->definitions); i++) {
        preview_definition *d = world->definitions[i];
        if (d->working_presentation || d->working->domain != BEHAVIOR_DOMAIN_FIELD) {
            continue;
        }
        bool uses = false;
        for (int g = 0; g < acount(d->applied->graphs); g++) {
            const char *name = d->applied->graphs[g]->source_asset;
            for (int l = 0; name && l < count; l++) {
                uses |= !strcmp(name, libraries[l]);
            }
        }
        if (!uses) {
            continue;
        }
        assert(d->field_source);
        behavior_asset *snapshot = link_snapshot(world, d->field_source, NULL, code, message, size);
        field_program *program = snapshot
                                   ? field_program_compile(snapshot, message, size)
                                   : NULL;
        if (!program) {
            behavior_asset_free(snapshot);
            preview_fields_relink_free(*out);
            *out = NULL;
            return false;
        }
        field_program_free(program);
        apush(*out, ((preview_field_relink){ d, snapshot }));
    }
    return true;
}

void preview_fields_relink_commit(preview_world *world, preview_field_relink *relinks)
{
    char message[256];
    for (int i = 0; i < acount(relinks); i++) {
        bool applied = material_field_compile(
            materials_fields(world->runtime->materials),
            relinks[i].snapshot,
            message,
            sizeof message
        );
        assert(applied);
        (void)applied;
        behavior_asset_free(publish_snapshot(relinks[i].definition, relinks[i].snapshot));
    }
    afree(relinks);
}

void preview_fields_relink_free(preview_field_relink *relinks)
{
    for (int i = 0; i < acount(relinks); i++) {
        behavior_asset_free(relinks[i].snapshot);
    }
    afree(relinks);
}

static int update_definition(
    preview_world *world,
    preview_definition *definition,
    char *message,
    size_t message_size,
    bool restart
)
{
    (void)restart;
    if (!world || !definition || definition->world != world || !definition->registered) {
        set_message(message, message_size, "definition is not registered with this world");
        return -1;
    }
    if (definition->working_presentation) {
        set_message(message, message_size, "Source projections are applied through canonical source preparation");
        return -1;
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION &&
        host_event_reserved(definition->working->name)) {
        set_message(message, message_size, "Event name is reserved for host dispatch");
        return -1;
    }
    if (!definition->dirty) {
        set_message(message, message_size, "definition has no unapplied edits");
        return 0;
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        return update_library(world, definition, message, message_size, true);
    }
    behavior_asset *snapshot = linked_snapshot(world, definition->working, NULL, message, message_size);
    if (!snapshot) {
        set_message(message, message_size, "working definition could not be loaded: %s", graph_last_error());
        return -1;
    }
    compile_report_free(&definition->diagnostics);
    compile_report_init(&definition->diagnostics);
    if (snapshot->domain == BEHAVIOR_DOMAIN_MODEL) {
        shader_error error = { 0 };
        if (!render_shader_upsert(snapshot, &error)) {
            registration_error(definition, error.message);
            definition->diagnostics.items[0].node_id = error.node;
            set_message(message, message_size, "apply blocked: %s", error.message);
            behavior_asset_free(snapshot);
            return -1;
        }
        behavior_asset *previous = publish_snapshot(definition, snapshot);
        behavior_asset_free(previous);
        definition->dirty = 0;
        set_message(
            message,
            message_size,
            "Updated %s and its model dependents.",
            definition->name
        );
        return 0;
    }
    if (graph_asset_compile(snapshot, &definition->diagnostics) != 0 &&
        compile_report_blocked(&definition->diagnostics)) {
        const compile_diagnostic *diagnostic = compile_report_first_error(
            &definition->diagnostics
        );
        set_message(message, message_size, "apply blocked: %s", diagnostic->message);
        behavior_asset_free(snapshot);
        return -1;
    }
    if (definition->working->domain == BEHAVIOR_DOMAIN_FIELD &&
        !publish_field(world, definition, snapshot, message, message_size)) {
        behavior_asset_free(snapshot);
        return -1;
    }
    if (shader_domain(definition->working->domain)) {
        shader_error error = { 0 };
        if (!render_shader_upsert(snapshot, &error)) {
            set_message(message, message_size, "%s", error.message);
            behavior_asset_free(snapshot);
            return -1;
        }
    }
    behavior_asset *previous = publish_snapshot(definition, snapshot);
    behavior_asset_free(previous);
    definition->dirty = 0;
    set_message(message, message_size, "Updated %s.", definition->name);
    return 0;
}

int preview_can_update_library(preview_world *world, preview_definition *definition, char *message, size_t size)
{
    if (!definition || definition->working_presentation ||
        definition->working->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        set_message(message, size, "Only shader graph libraries use this publication path.");
        return -1;
    }
    return update_library(world, definition, message, size, false);
}

int preview_apply(preview_world *world, preview_definition *definition, char *message, size_t message_size)
{
    return update_definition(world, definition, message, message_size, true);
}

int preview_update(preview_world *world, preview_definition *definition, char *message, size_t message_size)
{
    return update_definition(world, definition, message, message_size, false);
}

behavior_asset *preview_link(preview_world *world, const behavior_asset *source)
{
    char message[256];
    return linked_snapshot(world, source, NULL, message, sizeof message);
}
