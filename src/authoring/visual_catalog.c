#include "authoring/visual_catalog.h"

#include "authoring/shader_prepare.h"
#include "graph/document/serialize.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
    char *path;
    behavior_asset *asset;
} catalog_source;
static bool fail(char *error, size_t size, const char *path, const char *message)
{
    snprintf(error, size, "%s%s%s", path ? path : "", path ? ": " : "", message);
    return false;
}
static bool visual(const behavior_asset *a)
{
    return a && (a->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                 a->domain == BEHAVIOR_DOMAIN_SHAPE ||
                 a->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY ||
                 a->domain == BEHAVIOR_DOMAIN_MODEL ||
                 a->domain == BEHAVIOR_DOMAIN_FIELD);
}
static bool add(catalog_source **sources, const char *path, const char *text, char *error, size_t size)
{
    behavior_asset *asset = behavior_asset_load(text);
    if (!visual(asset)) {
        behavior_asset_free(asset);
        return fail(error, size, path, graph_last_error()[0] ? graph_last_error() : "Resource is not a visual graph");
    }
    for (int i = 0; i < acount(*sources); i++) {
        if (!strcmp((*sources)[i].path, path)) {
            behavior_asset_free((*sources)[i].asset);
            (*sources)[i].asset = asset;
            return true;
        }
    }
    if (acount(*sources) == VISUAL_CATALOG_SOURCES_MAX) {
        behavior_asset_free(asset);
        return fail(error, size, NULL, "Visual source catalog exceeds 256 assets");
    }
    apush(*sources, ((catalog_source){ strdup(path), asset }));
    return true;
}
static bool collect(
    workspace_documents *master,
    const char *root,
    workspace_resources *resources,
    catalog_source **sources,
    char *error,
    size_t size
)
{
    workspace_document_status status;
    workspace_documents *disk = workspace_documents_new(root, &status);
    if (!disk) {
        return fail(error, size, NULL, workspace_document_status_text(status));
    }
    const char *kinds[] = { "shader-library", "material", "shape", "field", "model" };
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX],
        count = workspace_document_handles(master, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    bool ok = true;
    for (int k = 0; ok && k < 5; k++) {
        arena storage = { 0 };
        pin_array *rows = NULL;
        workspace_resource_result
            result = workspace_resources_query(resources, 0, 256, kinds[k], "", &storage, &rows);
        if (!result.ok || !result.complete || result.total > 256) {
            ok = fail(
                error,
                size,
                NULL,
                result.total > 256
                    ? "Visual resource category exceeds 256 assets"
                : result.message[0] ? result.message
                                    : "Refresh a complete resource inventory "
                                      "before visual publication"
            );
        }
        for (int i = 0; ok && rows && i < rows->count; i++) {
            workspace_resource_row *row = rows->items[i].ptr;
            workspace_document_view view = { 0 };
            bool overlay = false;
            for (int j = 0; j < count; j++) {
                if (workspace_document_read(master, handles[j], &view) ==
                        WORKSPACE_DOCUMENT_OK &&
                    !strcmp(view.path, row->path)) {
                    overlay = true;
                    break;
                }
            }
            int handle = 0;
            if (!overlay) {
                status = workspace_document_open(disk, row->path, &handle);
                if (status == WORKSPACE_DOCUMENT_OK) {
                    status = workspace_document_read(disk, handle, &view);
                }
            }
            if (!overlay && status != WORKSPACE_DOCUMENT_OK) {
                ok = fail(error, size, row->path, workspace_document_status_text(status));
            } else {
                ok = add(sources, row->path, view.text, error, size);
            }
            if (handle) {
                workspace_document_close(disk, handle);
            }
        }
        arena_free(&storage);
    }
    /* New disk-backed open documents may not yet be in the inventory. */
    for (int i = 0; ok && i < count; i++) {
        workspace_document_view view;
        if (workspace_document_read(master, handles[i], &view) != WORKSPACE_DOCUMENT_OK ||
            !view.path[0]) {
            continue;
        }
        bool known = false;
        for (int j = 0; j < acount(*sources); j++) {
            known |= !strcmp((*sources)[j].path, view.path);
        }
        if (known) {
            continue;
        }
        const char *extension = strrchr(view.path, '.');
        if (!extension || strcmp(extension, ".kdl")) {
            continue;
        }
        behavior_asset *asset = behavior_asset_load(view.text);
        bool eligible = visual(asset);
        behavior_asset_free(asset);
        if (eligible) {
            ok = add(sources, view.path, view.text, error, size);
        }
    }
    workspace_documents_free(disk);
    for (int i = 0; ok && i < acount(*sources); i++) {
        for (int j = 0; j < i; j++) {
            if (!strcmp((*sources)[i].asset->name, (*sources)[j].asset->name)) {
                ok = fail(error, size, (*sources)[i].path, "Duplicate visual asset identity");
            }
        }
    }
    return ok;
}
static bool publish(preview_world *world, catalog_source *sources, char *error, size_t size)
{
    int count = acount(sources), remaining = count;
    bool done[VISUAL_CATALOG_SOURCES_MAX] = { 0 };
    /* Models compile as one dependency generation; libraries/materials/fields
     * are linked first so all model material dependencies already exist. */
    for (int i = 0; i < count; i++) {
        if (sources[i].asset->domain == BEHAVIOR_DOMAIN_MODEL) {
            done[i] = true;
            remaining--;
        }
    }
    for (int pass = 0; pass < count && remaining; pass++) {
        int before = remaining;
        for (int i = 0; i < count; i++) {
            if (!done[i]) {
                preview_definition *d = preview_definition_new(
                    sources[i].asset->name,
                    behavior_asset_clone(sources[i].asset)
                );
                if (preview_add_definition(world, d) >= 0) {
                    preview_definition_set_path(d, sources[i].path);
                    done[i] = true;
                    remaining--;
                } else {
                    const compile_diagnostic *diagnostic = compile_report_first_error(
                        &d->diagnostics
                    );
                    if (diagnostic) {
                        fail(error, size, sources[i].path, diagnostic->message);
                    }
                    preview_definition_free(d);
                }
            }
        }
        if (before == remaining) {
            break;
        }
    }
    if (remaining) {
        for (int i = 0; i < count; i++) {
            if (!done[i]) {
                return error[0]
                         ? false
                         : fail(error, size, sources[i].path, "Visual dependencies failed to compile or form a cycle");
            }
        }
    }
    const behavior_asset *models[VISUAL_CATALOG_SOURCES_MAX];
    int model_count = 0;
    for (int i = 0; i < count; i++) {
        if (sources[i].asset->domain == BEHAVIOR_DOMAIN_MODEL) {
            models[model_count++] = sources[i].asset;
        }
    }
    shader_error problem = { 0 };
    if (model_count && !render_shader_models_upsert(models, model_count, &problem)) {
        return fail(error, size, NULL, problem.message);
    }
    for (int i = 0; i < count; i++) {
        if (sources[i].asset->domain == BEHAVIOR_DOMAIN_MODEL) {
            preview_definition *d = preview_definition_new(
                sources[i].asset->name,
                behavior_asset_clone(sources[i].asset)
            );
            if (preview_add_definition(world, d) < 0) {
                preview_definition_free(d);
                return fail(error, size, sources[i].path, "Model publication failed after dependency preparation");
            }
            preview_definition_set_path(d, sources[i].path);
        }
    }
    return true;
}
bool visual_catalog_build(
    workspace_documents *documents,
    const char *root,
    workspace_resources *resources,
    render_image_loader loader,
    preview_world **out_world,
    render_resource_context **out_resources,
    char *error,
    size_t size
)
{
    *out_world = NULL;
    *out_resources = NULL;
    if (!resources) {
        return fail(error, size, NULL, "Visual catalog requires a resource inventory");
    }
    catalog_source *sources = NULL;
    bool ok = collect(documents, root, resources, &sources, error, size);
    render_resource_context *context = NULL;
    preview_world *world = NULL;
    render_resource_scope scope = { 0 };
    if (ok) {
        context = render_resource_context_new();
        ok = render_resource_scope_enter(context, &scope, error, size);
    }
    if (ok) {
        render_image_loader owned;
        ok = render_image_loader_copy(loader, &owned);
        if (!ok) {
            fail(error, size, NULL, "Image loader cannot be retained across catalog generations");
        }
        if (ok) {
            render_images_set_loader(owned);
        }
        if (ok) {
            world = preview_world_new(NULL);
        }
        ok = ok && world && publish(world, sources, error, size);
    }
    if (!ok && world) {
        preview_world_free(world);
        world = NULL;
    }
    if (scope.active) {
        bool left = render_resource_scope_leave(&scope, error, size);
        assert(left);
        (void)left;
    }
    for (int i = 0; i < acount(sources); i++) {
        free(sources[i].path);
        behavior_asset_free(sources[i].asset);
    }
    afree(sources);
    if (!ok) {
        if (context) {
            char cleanup[128];
            bool freed = render_resource_context_free(context, cleanup, sizeof cleanup);
            assert(freed);
            (void)freed;
        }
        return false;
    }
    *out_world = world;
    *out_resources = context;
    error[0] = 0;
    return true;
}
