#include "authoring/application_catalog.h"
#include "authoring/language_catalog.h"
#include "authoring/preview_source_internal.h"
#include "authoring/source_presentation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_EDITS = 256,
    MAX_SOURCE_BYTES = 512 * 1024,
};

static void message(char *error, size_t size, const char *text)
{
    if (error && size) {
        snprintf(error, size, "%s", text);
    }
}
static preview_definition *projection_at_path(preview_world *world, const char *path)
{
    for (int i = 0; i < acount(world->definitions); i++) {
        preview_definition *definition = world->definitions[i];
        if (definition->working_presentation && definition->path &&
            !strcmp(definition->path, path)) {
            return definition;
        }
    }
    return NULL;
}
static void refresh_presentations(preview_world *world, bool applied)
{
    preview_source_state *state = world->source_state;
    const authoring_sources *sources = state->working;
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (!source->analysis->name) {
            continue;
        }
        preview_definition *definition = projection_at_path(world, source->path);
        if (!definition && !applied) {
            continue;
        }
        authoring_source_presentation *presentation = authoring_source_presentation_new(
            source->analysis,
            authoring_sources_catalog(sources),
            definition ? definition->working_presentation : NULL
        );
        if (!presentation) {
            continue;
        }
        if (!definition) {
            definition = preview_projection_new(source->analysis->name, presentation);
            int path_status = preview_definition_set_path(definition, source->path);
            int status = preview_add_projection(world, definition);
            assert(!path_status && !status);
            (void)path_status;
            (void)status;
        } else {
            if (applied) {
                free(definition->name);
                definition->name = xstrdup(source->analysis->name);
            }
            int status = preview_projection_set(definition, presentation, false);
            assert(!status);
            if (applied) {
                authoring_source_presentation *copy = authoring_source_presentation_clone(
                    presentation
                );
                status = preview_projection_set(definition, copy, true);
                authoring_source_presentation_free(copy);
                assert(!status);
            }
            (void)status;
        }
        authoring_source_presentation_free(presentation);
    }
}
static int candidate_status(const authoring_sources *sources, char *error, size_t size)
{
    if (authoring_sources_valid(sources)) {
        message(error, size, "");
        return 0;
    }
    if (authoring_sources_catalog_diagnostic_count(sources)) {
        message(error, size, authoring_sources_catalog_diagnostic_at(sources, 0)->message);
        return -1;
    }
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (source->diagnostic_count) {
            message(error, size, source->diagnostics[0].message);
            return -1;
        }
    }
    message(error, size, "Source collection has no valid executable generation");
    return -1;
}
static int prepare(preview_world *world, char *error, size_t size)
{
    preview_source_state *state = world->source_state;
    if (state->staging) {
        message(error, size, "Source publication is staged");
        return -1;
    }
    const script_catalog *native = authoring_language_catalog_view(state->native);
    source_catalog *catalog = state->replace_sources ? source_catalog_replaced(
                                                           state->root,
                                                           native,
                                                           state->edits,
                                                           acount(state->edits)
                                                       )
                                                     : source_catalog_edited(
                                                           state->root,
                                                           native,
                                                           state->edits,
                                                           acount(state->edits)
                                                       );
    script_lower_bindings bindings = authoring_language_bindings(state->native);
    authoring_sources *next = authoring_sources_prepare_incremental(
        catalog,
        &bindings,
        state->backend,
        state->working
    );
    if (state->working != state->applied) {
        authoring_sources_free(state->working);
    }
    state->working = next;
    refresh_presentations(world, false);
    return candidate_status(next, error, size);
}
static int prepare_sources(
    preview_world *world,
    const char *root,
    behavior_backend backend,
    native_capabilities *table,
    char *error,
    size_t size
)
{
    assert(world && root && root[0]);
    if (world->source_state && world->source_state->mirrored) {
        message(error, size, "An immutable source mirror is replaced by another mirror");
        return -1;
    }
    if (world->source_state) {
        if (table) {
            message(error, size, "A source world retains its original native catalog");
            return -1;
        }
        if (strcmp(world->source_state->root, root) ||
            world->source_state->backend != backend) {
            message(error, size, "A source world retains one content root and backend");
            return -1;
        }
        return prepare(world, error, size);
    }
    bool owned = !table;
    if (owned) {
        table = authoring_application_catalog_new(false, error, size);
    }
    if (!table) {
        return -1;
    }
    authoring_language_catalog_input input = { .native = table };
    authoring_language_catalog *native = authoring_language_catalog_new(&input, error, size);
    if (owned) {
        native_capabilities_free(table);
    }
    if (!native) {
        return -1;
    }
    preview_source_state *state = xcalloc(1, sizeof *state);
    state->root = xstrdup(root);
    state->backend = backend;
    state->native = native;
    world->source_state = state;
    return prepare(world, error, size);
}
int preview_sources_prepare(
    preview_world *world,
    const char *root,
    behavior_backend backend,
    char *error,
    size_t size
)
{
    return prepare_sources(world, root, backend, NULL, error, size);
}
int preview_sources_prepare_with_catalog(
    preview_world *world,
    const char *root,
    behavior_backend backend,
    native_capabilities *table,
    char *error,
    size_t size
)
{
    assert(table);
    return prepare_sources(world, root, backend, table, error, size);
}
int preview_sources_mirror(
    preview_world *destination,
    const preview_world *source,
    bool applied,
    char *error,
    size_t size
)
{
    assert(destination && source);
    const preview_source_state *origin = source->source_state;
    const authoring_sources *selected = origin ? (applied ? origin->applied
                                                          : origin->working)
                                               : NULL;
    if (!selected || !authoring_sources_valid(selected)) {
        message(error, size, "A source mirror requires an exact valid generation");
        return -1;
    }
    preview_source_state *state = destination->source_state;
    if (state && (state->staging || strcmp(state->root, origin->root) ||
                  state->backend != origin->backend)) {
        message(error, size, "Source mirror destination is staged or has a different content root/backend");
        return -1;
    }
    if (state && state->applied == selected && state->working == selected) {
        return 0;
    }
    bool created = !state;
    if (created) {
        state = xcalloc(1, sizeof *state);
        state->root = xstrdup(origin->root);
        state->backend = origin->backend;
        state->native = authoring_language_catalog_retain(origin->native);
        destination->source_state = state;
    }
    authoring_sources *previous = state->working;
    authoring_sources *previous_applied = state->applied;
    bool retained = selected != state->applied;
    state->working = retained ? authoring_sources_retain(selected) : state->applied;
    preview_source_staging *stage = preview_sources_stage(destination, NULL, error, size);
    if (!stage) {
        if (retained) {
            authoring_sources_free(state->working);
        }
        state->working = previous;
        if (created) {
            preview_sources_free(destination);
        }
        return -1;
    }
    preview_sources_commit(stage);
    if (previous != previous_applied) {
        authoring_sources_free(previous);
    }
    if (!created) {
        authoring_language_catalog *native = authoring_language_catalog_retain(
            origin->native
        );
        authoring_language_catalog_free(state->native);
        state->native = native;
    }
    state->mirrored = true;
    return 0;
}
static void edits_free(source_catalog_edit *edits)
{
    for (int i = 0; i < acount(edits); i++) {
        free((char *)edits[i].path);
        free((char *)edits[i].text);
        free((char *)edits[i].identity);
    }
    afree(edits);
}
static bool merged_edits(
    const preview_source_state *state,
    const source_catalog_edit *edits,
    int count,
    bool replace,
    source_catalog_edit **out,
    char *error,
    size_t size
)
{
    assert(count >= 0 && (!count || edits));
    if (!state) {
        message(error, size, "Source edits require a prepared content root");
        return false;
    }
    if (state->mirrored) {
        message(error, size, "An immutable source mirror cannot prepare disk-based edits");
        return false;
    }
    if (count > MAX_EDITS) {
        message(error, size, "Executable edit count limit exceeded");
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!edits[i].path || !source_catalog_edit_path(state->root, edits[i].path) ||
            (edits[i].text &&
             strnlen(edits[i].text, MAX_SOURCE_BYTES + 1) > MAX_SOURCE_BYTES) ||
            (edits[i].identity &&
             (!source_catalog_source_path(state->root, edits[i].path) ||
              !source_catalog_identity_valid(edits[i].identity)))) {
            message(error, size, "Executable edit path or byte limit exceeded");
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(edits[i].path, edits[j].path)) {
                message(error, size, "Duplicate executable edit path");
                return false;
            }
        }
    }
    source_catalog_edit *merged = NULL;
    for (int i = 0; !replace && i < acount(state->edits); i++) {
        apush(
            merged,
            ((source_catalog_edit){
                .path = xstrdup(state->edits[i].path),
                .text = state->edits[i].text ? xstrdup(state->edits[i].text) : NULL,
                .identity = state->edits[i].identity ? xstrdup(state->edits[i].identity)
                                                     : NULL })
        );
    }
    for (int i = 0; i < count; i++) {
        int index = -1;
        for (int j = 0; j < acount(merged); j++) {
            if (!strcmp(merged[j].path, edits[i].path)) {
                index = j;
                break;
            }
        }
        if (index < 0 && acount(merged) == MAX_EDITS) {
            edits_free(merged);
            message(error, size, "Executable edit count limit exceeded");
            return false;
        }
        if (index < 0) {
            index = acount(merged);
            apush(merged, ((source_catalog_edit){ .path = xstrdup(edits[i].path) }));
        }
        free((char *)merged[index].text);
        merged[index].text = edits[i].text ? xstrdup(edits[i].text) : NULL;
        if (edits[i].identity) {
            free((char *)merged[index].identity);
            merged[index].identity = xstrdup(edits[i].identity);
        }
    }
    *out = merged;
    return true;
}
static int edit_sources(
    preview_world *world,
    const source_catalog_edit *edits,
    int count,
    bool replace,
    char *error,
    size_t size
)
{
    assert(world);
    preview_source_state *state = world->source_state;
    if (state && state->staging) {
        message(error, size, "Source publication is staged");
        return -1;
    }
    source_catalog_edit *next = NULL;
    if (!merged_edits(state, edits, count, replace, &next, error, size)) {
        return -1;
    }
    edits_free(state->edits);
    state->edits = next;
    state->replace_sources |= replace;
    return prepare(world, error, size);
}
int preview_sources_edit(
    preview_world *world,
    const source_catalog_edit *edits,
    int count,
    char *error,
    size_t size
)
{
    return edit_sources(world, edits, count, false, error, size);
}
int preview_sources_replace(
    preview_world *world,
    const source_catalog_edit *edits,
    int count,
    char *error,
    size_t size
)
{
    return edit_sources(world, edits, count, true, error, size);
}
int preview_source_edit(preview_world *world, const char *path, const char *text, char *error, size_t size)
{
    source_catalog_edit edit = { .path = path, .text = text };
    return preview_sources_edit(world, &edit, 1, error, size);
}
authoring_sources *preview_sources_candidate(
    const preview_world *world,
    const source_catalog_edit *edits,
    int count,
    bool replace,
    char *error,
    size_t size
)
{
    assert(world);
    const preview_source_state *state = world->source_state;
    source_catalog_edit *merged = NULL;
    if (!merged_edits(state, edits, count, replace, &merged, error, size)) {
        return NULL;
    }
    const script_catalog *native = authoring_language_catalog_view(state->native);
    source_catalog *
        catalog = replace || state->replace_sources
                    ? source_catalog_replaced(state->root, native, merged, acount(merged))
                    : source_catalog_edited(state->root, native, merged, acount(merged));
    edits_free(merged);
    script_lower_bindings bindings = authoring_language_bindings(state->native);
    authoring_sources *candidate = authoring_sources_prepare_incremental(
        catalog,
        &bindings,
        state->backend,
        state->working
    );
    candidate_status(candidate, error, size);
    return candidate;
}
int preview_sources_validate(
    const preview_world *world,
    const source_catalog_edit *edits,
    int count,
    char *error,
    size_t size
)
{
    authoring_sources *candidate = preview_sources_candidate(world, edits, count, false, error, size);
    if (!candidate) {
        return -1;
    }
    bool valid = authoring_sources_valid(candidate);
    authoring_sources_free(candidate);
    return valid ? 0 : -1;
}
const authoring_source *preview_source_at_path(const authoring_sources *sources, const char *path)
{
    if (!sources) {
        return NULL;
    }
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (!strcmp(source->path, path)) {
            return source;
        }
    }
    return NULL;
}
int preview_source_apply(
    preview_world *world,
    const char *path,
    const char *text,
    bool restart,
    char *error,
    size_t size
)
{
    int status = preview_source_edit(world, path, text, error, size);
    return status
             ? status
             : preview_sources_publish(world, restart ? path : NULL, error, size);
}
const authoring_sources *preview_sources_working(const preview_world *world)
{
    return world->source_state ? world->source_state->working : NULL;
}
const authoring_sources *preview_sources_applied(const preview_world *world)
{
    return world->source_state ? world->source_state->applied : NULL;
}
int preview_sources_overlay_count(const preview_world *world)
{
    return world->source_state ? acount(world->source_state->edits) : 0;
}
const source_catalog_edit *preview_sources_overlay_at(const preview_world *world, int index)
{
    assert(
        world->source_state && index >= 0 && index < acount(world->source_state->edits)
    );
    return &world->source_state->edits[index];
}
const authoring_source *preview_source_find(const preview_world *world, const char *path, bool applied)
{
    const authoring_sources *sources = applied ? preview_sources_applied(world)
                                               : preview_sources_working(world);
    return preview_source_at_path(sources, path);
}
