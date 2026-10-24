#include "authoring/native_graph_prepare.h"

#include "authoring/graph_sources.h"
#include "authoring/shader_prepare.h"
#include "authoring/source_graph_library.h"
#include "graph/compiler/material_field.h"
#include "graph/compiler/validation.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>

static bool failed(char *error, size_t size, const char *path, const char *message)
{
    if (size) {
        snprintf(error, size, "%s: %s", path, message);
    }
    return false;
}
bool authoring_native_graphs_prepare(
    const char *content,
    const authoring_sources *code,
    const source_catalog *declarations,
    material_fields *fields,
    char *error,
    size_t size
)
{
    authoring_graph_source *sources = NULL;
    if (!authoring_graph_sources_load(content, &sources)) {
        return failed(error, size, content, "Cannot load native graph documents");
    }
    const behavior_asset **catalog = NULL, **models = NULL;
    behavior_asset **libraries = NULL, **linked = NULL;
    bool valid = false;
    /* Only graphs link libraries, so a project without them analyzes none. */
    if (acount(sources) && code) {
        libraries = authoring_source_graph_libraries(code);
    } else if (acount(sources) && !authoring_source_graph_libraries_catalog(declarations, &libraries, error, size)) {
        goto done;
    }
    for (int i = 0; i < acount(sources); i++) {
        for (int j = 0; j < i; j++) {
            if (!strcmp(sources[i].asset->name, sources[j].asset->name)) {
                failed(error, size, sources[i].path, "Duplicate native graph declaration");
                goto done;
            }
        }
        for (int j = 0; j < acount(libraries); j++) {
            if (!strcmp(sources[i].asset->name, libraries[j]->name)) {
                failed(error, size, sources[i].path, "Native graph declaration shares a source library's name");
                goto done;
            }
        }
        apush(catalog, sources[i].asset);
    }
    for (int i = 0; i < acount(libraries); i++) {
        apush(catalog, libraries[i]);
    }
    /* Library linking is the same graph-owned transformation used by preview;
     * models retain their own explicit catalog compilation path. */
    for (int i = 0; i < acount(sources); i++) {
        const behavior_asset *source = sources[i].asset;
        behavior_asset *asset = source->domain == BEHAVIOR_DOMAIN_MODEL
                                  ? behavior_asset_clone(source)
                                  : behavior_asset_link_libraries(
                                        source,
                                        catalog,
                                        acount(catalog),
                                        error,
                                        size
                                    );
        if (!asset) {
            goto done;
        }
        apush(linked, asset);
        compile_report report;
        compile_report_init(&report);
        bool rejected = graph_asset_compile_with_models(asset, catalog, acount(catalog), &report) !=
                        0;
        const compile_diagnostic *diagnostic = compile_report_first_error(&report);
        if (rejected) {
            failed(error, size, sources[i].path, diagnostic ? diagnostic->message : "Native graph validation failed");
        }
        compile_report_free(&report);
        if (rejected) {
            goto done;
        }
        if (asset->domain == BEHAVIOR_DOMAIN_MODEL) {
            apush(models, asset);
        }
    }
    /* Publish libraries/fields/materials before models, retaining the original
     * model catalog for the existing dependency compiler. */
    for (int i = 0; i < acount(linked); i++) {
        behavior_asset *asset = linked[i];
        if (asset->domain == BEHAVIOR_DOMAIN_FIELD &&
            !material_field_compile(fields, asset, error, size)) {
            goto done;
        }
        if (asset->domain == BEHAVIOR_DOMAIN_SHAPE ||
            asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
            asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
            shader_error diagnostic = { 0 };
            if (!render_shader_upsert(asset, &diagnostic)) {
                failed(error, size, sources[i].path, diagnostic.message);
                goto done;
            }
        }
    }
    if (acount(models)) {
        shader_error diagnostic = { 0 };
        if (!render_shader_models_upsert(models, acount(models), &diagnostic)) {
            failed(error, size, content, diagnostic.message);
            goto done;
        }
    }
    valid = true;
done:
    for (int i = 0; i < acount(linked); i++) {
        behavior_asset_free(linked[i]);
    }
    afree(linked);
    afree(models);
    afree(catalog);
    authoring_source_graph_libraries_free(libraries);
    authoring_graph_sources_free(sources);
    return valid;
}
