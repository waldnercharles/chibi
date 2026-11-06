#include "authoring/source_collection.h"

#include "authoring/source_catalog_snapshot.h"
#include "authoring/source_link.h"
#include "authoring/source_prepare_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct authoring_sources
{
    source_catalog *catalog;
    authoring_source **sources;
    bool valid;
    int references;
    script_lower_bindings bindings;
    behavior_backend backend;
    int reused;
};

static int library_owner(const authoring_sources *sources, uint64_t id)
{
    for (int i = 0; i < acount(sources->sources); i++) {
        const script_analysis *analysis = sources->sources[i]->analysis;
        if (analysis->kind != SCRIPT_PROGRAM_LIBRARY) {
            continue;
        }
        for (int j = 0; j < analysis->function_count; j++) {
            int symbol = analysis->functions[j].symbol;
            if (symbol >= 0 && analysis->symbols[symbol].id == id) {
                return i;
            }
        }
    }
    return -1;
}

static void reject(authoring_source *source, const char *message)
{
    script_diagnostic diagnostic = { .range = { .line = 1, .column = 1 } };
    snprintf(diagnostic.message, sizeof diagnostic.message, "%s", message);
    apush(source->diagnostics, diagnostic);
    source->diagnostic_count = acount(source->diagnostics);
}

authoring_sources *authoring_sources_prepare(
    const char *root,
    const script_catalog *native,
    const script_lower_bindings *bindings,
    behavior_backend backend
)
{
    assert(root && native && bindings);
    return authoring_sources_prepare_catalog(source_catalog_new(root, native), bindings, backend);
}

authoring_sources *authoring_sources_prepare_catalog(
    source_catalog *owned,
    const script_lower_bindings *bindings,
    behavior_backend backend
)
{
    return authoring_sources_prepare_incremental(owned, bindings, backend, NULL);
}

static authoring_source *unchanged_source(const authoring_sources *previous, const source_catalog_source *current)
{
    for (int i = 0; previous && i < acount(previous->sources); i++) {
        authoring_source *source = previous->sources[i];
        if (source->valid && !strcmp(source->path, current->path) &&
            !strcmp(source->analysis->identity, current->identity) &&
            !strcmp(source->syntax->text, current->text)) {
            return authoring_source_retain(source);
        }
    }
    return NULL;
}

authoring_sources *authoring_sources_prepare_incremental(
    source_catalog *owned,
    const script_lower_bindings *bindings,
    behavior_backend backend,
    const authoring_sources *previous
)
{
    assert(owned && bindings);
    authoring_sources *sources = xcalloc(1, sizeof *sources);
    sources->references = 1;
    sources->catalog = owned;
    sources->bindings = *bindings;
    sources->backend = backend;
    const script_catalog *catalog = source_catalog_view(sources->catalog);
    int request_count = 0;
    const script_specialization_request *requests = source_catalog_requests(owned, &request_count);
    int count = source_catalog_source_count(sources->catalog);
    bool compatible = previous && bindings->generation &&
                      bindings->generation == previous->bindings.generation &&
                      bindings->user == previous->bindings.user &&
                      bindings->host == previous->bindings.host &&
                      bindings->action == previous->bindings.action &&
                      bindings->global == previous->bindings.global &&
                      backend == previous->backend &&
                      !source_catalog_diagnostic_count(owned) &&
                      !source_catalog_diagnostic_count(previous->catalog) &&
                      source_catalog_same(owned, previous->catalog);
    bool *reuse = xcalloc((size_t)count, sizeof *reuse);
    for (int i = 0; i < count; i++) {
        const source_catalog_source *source = source_catalog_source_at(sources->catalog, i);
        authoring_source *prior = compatible ? unchanged_source(previous, source)
                                             : NULL;
        reuse[i] = prior != NULL;
        apush(
            sources->sources,
            prior ? prior
                  : authoring_source_analyze_requested(
                        source->path,
                        source->identity,
                        source->syntax,
                        catalog,
                        requests,
                        request_count
                    )
        );
    }
    if (source_catalog_diagnostic_count(sources->catalog)) {
        for (int i = 0; i < count; i++) {
            reject(sources->sources[i], AUTHORING_SOURCES_CATALOG_INCOMPLETE);
        }
        free(reuse);
        return sources;
    }
    bool *dependencies = xcalloc((size_t)count * (size_t)count, sizeof *dependencies);
    bool *done = xcalloc((size_t)count, sizeof *done);
    for (int i = 0; i < count; i++) {
        const script_analysis *analysis = sources->sources[i]->analysis;
        for (int j = 0; j < analysis->expression_count; j++) {
            const script_expression *expression = &analysis->expressions[j];
            const uint64_t ids[] = { expression->capability,
                                     expression->read,
                                     expression->write };
            for (int id = 0; id < 3; id++) {
                int owner = ids[id] ? library_owner(sources, ids[id]) : -1;
                if (owner >= 0 && owner != i) {
                    dependencies[i * count + owner] = true;
                }
            }
        }
    }
    /* A caller retains the exact library bodies it was linked against. Even
     * when declarations did not change, a body edit invalidates every caller
     * transitively before any executable generation is selected. */
    bool changed;
    do {
        changed = false;
        for (int i = 0; i < count; i++) {
            for (int j = 0; reuse[i] && j < count; j++) {
                if (dependencies[i * count + j] && !reuse[j]) {
                    reuse[i] = false;
                    changed = true;
                }
            }
        }
    } while (changed);
    for (int i = 0; i < count; i++) {
        authoring_source *source = sources->sources[i];
        if (source->module && !reuse[i]) {
            const source_catalog_source *input = source_catalog_source_at(owned, i);
            sources->sources[i] = authoring_source_analyze_requested(
                input->path,
                input->identity,
                input->syntax,
                catalog,
                requests,
                request_count
            );
            authoring_source_free(source);
        }
        sources->reused += reuse[i];
    }
    execution_module **libraries = NULL;
    int remaining = count;
    while (remaining) {
        bool progress = false;
        for (int i = 0; i < count; i++) {
            if (done[i]) {
                continue;
            }
            bool ready = true, invalid = false;
            for (int j = 0; j < count; j++) {
                if (!dependencies[i * count + j]) {
                    continue;
                }
                ready &= done[j];
                invalid |= done[j] && !sources->sources[j]->valid;
            }
            if (!ready) {
                continue;
            }
            authoring_source *source = sources->sources[i];
            if (invalid) {
                reject(source, "Imported library has no valid executable generation");
            } else if (!reuse[i]) {
                authoring_source_link link = { .native = *bindings,
                                               .catalog = catalog,
                                               .libraries = libraries,
                                               .library_count = acount(libraries) };
                script_lower_bindings resolved = authoring_source_bindings(&link);
                authoring_source_lower(source, catalog, &resolved, backend);
            }
            if (source->valid && source->analysis->kind == SCRIPT_PROGRAM_LIBRARY) {
                apush(libraries, source->module);
            }
            done[i] = true;
            remaining--;
            progress = true;
        }
        if (!progress) {
            for (int i = 0; i < count; i++) {
                if (!done[i]) {
                    reject(sources->sources[i], "Cross-library import cycle has no publishable generation");
                }
            }
            break;
        }
    }
    sources->valid = true;
    for (int i = 0; i < count; i++) {
        sources->valid &= sources->sources[i]->valid;
    }
    afree(libraries);
    free(dependencies);
    free(done);
    free(reuse);
    return sources;
}

int authoring_sources_reused(const authoring_sources *sources)
{
    assert(sources);
    return sources->reused;
}

const script_catalog *authoring_sources_catalog(const authoring_sources *sources)
{
    return source_catalog_view(sources->catalog);
}
const source_catalog *authoring_sources_source_catalog(const authoring_sources *sources)
{
    return sources->catalog;
}
const input_action *authoring_sources_input(const authoring_sources *sources)
{
    return source_catalog_input(sources->catalog);
}
int authoring_sources_definition_count(const authoring_sources *sources)
{
    return source_catalog_definition_count(sources->catalog);
}
const source_catalog_definition *authoring_sources_definitions(const authoring_sources *sources)
{
    return source_catalog_definitions(sources->catalog);
}
int authoring_sources_reference_count(const authoring_sources *sources)
{
    return source_catalog_reference_count(sources->catalog);
}
const source_catalog_reference *authoring_sources_references(const authoring_sources *sources)
{
    return source_catalog_references(sources->catalog);
}
int authoring_sources_count(const authoring_sources *sources)
{
    return acount(sources->sources);
}
const authoring_source *authoring_sources_at(const authoring_sources *sources, int index)
{
    assert(index >= 0 && index < acount(sources->sources));
    return sources->sources[index];
}
int authoring_sources_catalog_diagnostic_count(const authoring_sources *sources)
{
    return source_catalog_diagnostic_count(sources->catalog);
}
const source_catalog_diagnostic *authoring_sources_catalog_diagnostic_at(
    const authoring_sources *sources,
    int index
)
{
    return source_catalog_diagnostic_at(sources->catalog, index);
}
bool authoring_sources_valid(const authoring_sources *sources)
{
    return sources->valid;
}
authoring_sources *authoring_sources_retain(const authoring_sources *sources)
{
    assert(sources && sources->references > 0);
    authoring_sources *owned = (authoring_sources *)sources;
    owned->references++;
    return owned;
}
void authoring_sources_free(authoring_sources *sources)
{
    if (!sources) {
        return;
    }
    assert(sources->references > 0);
    if (--sources->references) {
        return;
    }
    for (int i = 0; i < acount(sources->sources); i++) {
        authoring_source_free(sources->sources[i]);
    }
    afree(sources->sources);
    source_catalog_free(sources->catalog);
    free(sources);
}
