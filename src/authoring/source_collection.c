#include "authoring/source_collection.h"

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
    assert(owned && bindings);
    authoring_sources *sources = xcalloc(1, sizeof *sources);
    sources->references = 1;
    sources->catalog = owned;
    const script_catalog *catalog = source_catalog_view(sources->catalog);
    int count = source_catalog_source_count(sources->catalog);
    for (int i = 0; i < count; i++) {
        const source_catalog_source *source = source_catalog_source_at(sources->catalog, i);
        apush(
            sources->sources,
            authoring_source_analyze(source->path, source->identity, source->syntax, catalog)
        );
    }
    if (source_catalog_diagnostic_count(sources->catalog)) {
        for (int i = 0; i < count; i++) {
            reject(sources->sources[i], "Project declaration catalog is incomplete");
        }
        return sources;
    }
    bool *dependencies = xcalloc((size_t)count * (size_t)count, sizeof *dependencies);
    bool *done = xcalloc((size_t)count, sizeof *done);
    for (int i = 0; i < count; i++) {
        const script_analysis *analysis = sources->sources[i]->analysis;
        for (int j = 0; j < analysis->expression_count; j++) {
            const script_expression *expression = &analysis->expressions[j];
            if (expression->kind != SCRIPT_EXPR_CALL || expression->right >= 0) {
                continue;
            }
            int owner = library_owner(sources, expression->capability);
            if (owner >= 0 && owner != i) {
                dependencies[i * count + owner] = true;
            }
        }
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
            } else {
                authoring_source_link link = { .native = *bindings,
                                               .catalog = catalog,
                                               .libraries = libraries,
                                               .library_count = acount(libraries) };
                script_lower_bindings resolved = authoring_source_bindings(&link);
                authoring_source_lower(source, catalog, &resolved, backend);
                if (source->valid && source->analysis->kind == SCRIPT_PROGRAM_LIBRARY) {
                    apush(libraries, source->module);
                }
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
    return sources;
}

const script_catalog *authoring_sources_catalog(const authoring_sources *sources)
{
    return source_catalog_view(sources->catalog);
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
