#include "host/workspace_host.h"

#include "assets/project.h"
#include "authoring/document_semantics.h"
#include "authoring/graph_catalog.h"
#include "authoring/image_prepare.h"
#include "authoring/observations.h"
#include "authoring/preview.h"
#include "authoring/project_semantics.h"
#include "authoring/renderer_catalog.h"
#include "authoring/resource_previews.h"
#include "authoring/ui_authoring.h"
#include "authoring/ui_dom.h"
#include "authoring/ui_source_preview.h"
#include "authoring/ui_source_provider.h"
#include "authoring/visual_authoring_definition.h"
#include "authoring/visual_authoring_preview.h"
#include "authoring/visual_authoring_preview_definition.h"
#include "authoring/visual_model_definition.h"
#include "authoring/workspace_assets.h"
#include "authoring/workspace_assets_scope.h"
#include "authoring/workspace_css.h"
#include "authoring/workspace_debug.h"
#include "authoring/workspace_documents.h"
#include "authoring/workspace_file_history.h"
#include "authoring/workspace_info.h"
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
#include "authoring/workspace_imports.h"
#endif
#include "authoring/workspace_resources.h"
#include "authoring/workspace_runtime.h"
#include "authoring/workspace_simulation.h"
#include "host/authoring_platform.h"
#include "host/browser_messages.h"
#include "host/package_identity.h"
#include "render/shader_images.h"
#include "render/shader_publish.h"
#include "render/ui.h"
#include "runtime/component_catalog.h"
#include "runtime/native_prepared.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREVIEWS 16
struct workspace_host
{
    workspace_documents *documents;
    workspace_file_history *file_history;
    workspace_info *info;
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
    workspace_imports *imports;
#endif
    workspace_resources *resources;
    document_semantics_service semantics;
    workspace_assets_service *assets;
    visual_authoring_service visuals;
    visual_preview_service previews;
    visual_model_service *models;
    ui_authoring *ui_source;
    ui_dom *dom;
    workspace_css_service css;
    ui_source_preview *ui_preview;
    int ui_preview_bound;
    authoring_platform *platform;
    browser_messages *messages;
    resource_previews *thumbnails;
    native_capabilities *thumbnail_native;
    native_prepared_context thumbnail_context;
    execution_prepared_linker thumbnail_linker;
    player_host thumbnail_player;
    behavior_backend backend;
    char *compiler;
    render_image_loader thumbnail_images;
    workspace_runtime *runtime;
    workspace_simulation *simulation;
    workspace_debug *debug;
    project_semantics *project;
    authoring_observations *observations;
    package_identity identity;
    char scope_error[256];
    char *content, *content_prefix;
    int bound[PREVIEWS], bound_count;
    int runtime_bound;
    int thumbnail_bound[8], thumbnail_bound_count;
    char *runtime_error;
    render_ui *runtime_ui;
    rml_context *runtime_context;
};
static bool thumbnail_source_catalog(
    void *context,
    const script_catalog **catalog,
    void **owner,
    char *error,
    size_t size
)
{
    workspace_host *host = context;
    return project_semantics_catalog_acquire(host->project, catalog, owner, error, size);
}
static void thumbnail_source_catalog_release(void *context, void *owner)
{
    workspace_host *host = context;
    project_semantics_catalog_release(host->project, owner);
}
static workspace_runtime *thumbnail_scene_runtime(
    void *context,
    workspace_documents *documents,
    const char *root,
    char *error,
    size_t size
)
{
    workspace_host *host = context;
    const workspace_runtime_config config = {
        .documents = documents,
        .root = root,
        .compiler = host->compiler,
        .host = host->thumbnail_player,
        .backend = host->backend,
        .read_only_presentation = true,
        .engine_version = host->identity.version,
        .build_id = host->identity.build_id,
        .manifest_hash = host->identity.manifest_sha256
    };
    return workspace_runtime_new(&config, error, size);
}
static behavior_asset *link_source(void *context, const behavior_asset *source)
{
    workspace_host *host = context;
    render_resource_scope scope = { 0 };
    if (!render_resource_scope_enter(
            visual_previews_resources(host->previews.previews),
            &scope,
            host->scope_error,
            sizeof host->scope_error
        )) {
        return NULL;
    }
    behavior_asset *result = preview_link(visual_previews_world(host->previews.previews), source);
    bool left = render_resource_scope_leave(
        &scope,
        host->scope_error,
        sizeof host->scope_error
    );
    assert(left);
    (void)left;
    return result;
}
static bool resolve_image(void *context, const shader_image_source *source, shader_image_view *view)
{
    workspace_host *host = context;
    render_resource_scope scope = { 0 };
    if (!render_resource_scope_enter(
            visual_previews_resources(host->previews.previews),
            &scope,
            host->scope_error,
            sizeof host->scope_error
        )) {
        return false;
    }
    bool result = render_image_resolve(NULL, source, view);
    bool left = render_resource_scope_leave(
        &scope,
        host->scope_error,
        sizeof host->scope_error
    );
    assert(left);
    (void)left;
    return result;
}
static void *enter_model(void *context, const char **error)
{
    workspace_host *host = context;
    render_resource_scope *scope = xcalloc(1, sizeof *scope);
    if (!render_resource_scope_enter(
            visual_previews_resources(host->previews.previews),
            scope,
            host->scope_error,
            sizeof host->scope_error
        )) {
        *error = host->scope_error;
        free(scope);
        return NULL;
    }
    return scope;
}
static void leave_model(void *context, void *token)
{
    workspace_host *host = context;
    bool
        left = render_resource_scope_leave(token, host->scope_error, sizeof host->scope_error);
    assert(left);
    (void)left;
    free(token);
}
static behavior_asset **model_catalog(void *context, int *count)
{
    workspace_host *host = context;
    return visual_previews_catalog_snapshot(host->previews.previews, count);
}
static const script_catalog *component_catalog(void *context, char *error, size_t size)
{
    workspace_host *host = context;
    if (!workspace_assets_service_sync(host->assets, error, size)) {
        return NULL;
    }
    return workspace_assets_service_component_catalog(host->assets);
}
static const script_catalog *candidate_catalog(
    void *context,
    const workspace_document_replacement *replacements,
    int count,
    void **owner,
    char *error,
    size_t size
)
{
    workspace_host *host = context;
    workspace_assets_service *candidate = workspace_assets_service_candidate(
        host->assets,
        replacements,
        count,
        error,
        size
    );
    *owner = candidate;
    return candidate ? workspace_assets_service_component_catalog(candidate) : NULL;
}
static const prototype_registry *prototype_catalog(
    void *context,
    void *candidate_owner,
    char *error,
    size_t size
)
{
    workspace_host *host = context;
    workspace_assets_service *assets = candidate_owner ? candidate_owner : host->assets;
    if (!assets || !workspace_assets_service_sync(assets, error, size)) {
        return NULL;
    }
    return assets->registry;
}
static void release_candidate_catalog(void *context, void *owner)
{
    (void)context;
    workspace_assets_service_free(owner);
}
static void runtime_ui_retire(void *context, render_ui *ui)
{
    workspace_host *host = context;
    if (host->ui_preview) {
        ui_source_preview_retire_target(host->ui_preview);
    }
    if (ui && host->runtime_ui == ui) {
        char key[64];
        snprintf(key, sizeof key, "runtime/%d", host->runtime_bound);
        ui_dom_unregister(host->dom, key);
        host->runtime_ui = NULL;
        host->runtime_context = NULL;
    }
}
static source_registry *debug_sources(void *context, int session)
{
    workspace_host *host = context;
    workspace_runtime_result current = workspace_runtime_current(host->runtime);
    const world *world = workspace_runtime_world(host->runtime);
    return world && session > 0 && current.session == session ? world_sources(world)
                                                              : NULL;
}
static entities *debug_entities(void *context, int session)
{
    workspace_host *host = context;
    workspace_runtime_result current = workspace_runtime_current(host->runtime);
    const world *world = workspace_runtime_world(host->runtime);
    return world && session > 0 && current.session == session ? world_entities(world)
                                                              : NULL;
}
static bool debug_owner(void *context, int session, uint64_t owner, workspace_debug_owner *out)
{
    workspace_host *host = context;
    entity_origin origin;
    if (!out || !workspace_runtime_origin(host->runtime, session, owner, &origin)) {
        return false;
    }
    *out = (workspace_debug_owner){ .prototype = origin.prototype,
                                    .creation = origin.creation,
                                    .object = origin.object };
    return true;
}
static declared_components *debug_types(void *context, int session)
{
    workspace_host *host = context;
    return debug_sources(host, session) ? workspace_runtime_types(host->runtime) : NULL;
}
static const char *debug_source_root(void *context, int session)
{
    workspace_host *host = context;
    return debug_sources(host, session) ? workspace_runtime_source_root(host->runtime)
                                        : NULL;
}
typedef struct
{
    render_resource_scope render;
    declared_components_scope types;
} debug_scope;
static void *enter_debug(void *context, int session, const char **error)
{
    workspace_host *host = context;
    if (!debug_sources(host, session)) {
        *error = "The runtime session has retired";
        return NULL;
    }
    debug_scope *scope = xcalloc(1, sizeof *scope);
    if (!declared_components_scope_enter(debug_types(host, session), &scope->types)) {
        *error = "Cannot enter runtime type scope";
        free(scope);
        return NULL;
    }
    if (!render_resource_scope_enter(
            render_resource_context_for_world(workspace_runtime_world(host->runtime)),
            &scope->render,
            host->scope_error,
            sizeof host->scope_error
        )) {
        bool left = declared_components_scope_leave(&scope->types);
        assert(left);
        (void)left;
        *error = host->scope_error;
        free(scope);
        return NULL;
    }
    return scope;
}
static void leave_debug(void *context, void *token)
{
    workspace_host *host = context;
    debug_scope *scope = token;
    bool left = render_resource_scope_leave(
        &scope->render,
        host->scope_error,
        sizeof host->scope_error
    );
    assert(left);
    (void)left;
    left = declared_components_scope_leave(&scope->types);
    assert(left);
    (void)left;
    free(scope);
}
static bool host_services_new(
    workspace_host *host,
    const workspace_host_config *config,
    char *error,
    size_t size
)
{
    host->dom = ui_dom_new();
    authoring_platform_config platform_config = { .root = config->root,
                                                  .compiler = config->compiler,
                                                  .launcher = config->launcher,
                                                  .window = config->window };
    host->platform = authoring_platform_new(&platform_config, error, size);
    if (!host->platform) {
        return false;
    }
    host->messages = browser_messages_new(config->root, host->documents, error, size);
    return host->messages != NULL;
}
workspace_host *workspace_host_new(const workspace_host_config *config, char *error, size_t size)
{
    if (!config || !config->native) {
        snprintf(error, size, "A native catalog is required");
        return NULL;
    }
    if ((config->document_state || config->document_state_bytes) &&
        (!config->root || !config->document_state || !config->document_state_bytes)) {
        snprintf(error, size, "Document state requires an active workspace and nonempty bytes");
        return NULL;
    }
    const char *root = config->root;
    if ((config->requested_root || config->requested_version) &&
        (root || !config->requested_root)) {
        snprintf(error, size, "Pending project requires an idle host and explicit root");
        return NULL;
    }
    native_capabilities *native = config->native;
    workspace_host *host = xcalloc(1, sizeof *host);
    host->observations = authoring_observations_new();
    host->info = root ? workspace_info_new(root, error, size) : workspace_info_idle();
    if (!host->info) {
        goto fail;
    }
    if (config->requested_root && !workspace_info_set_pending_project(
                                      host->info,
                                      config->requested_root,
                                      config->requested_version
                                  )) {
        snprintf(error, size, "Invalid pending project metadata");
        goto fail;
    }
    if (!root) {
        if (!host_services_new(host, config, error, size)) {
            goto fail;
        }
        return host;
    }
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
    host->imports = workspace_imports_new(root);
    if (!host->imports) {
        snprintf(error, size, "Cannot create workspace import service");
        goto fail;
    }
#endif
    workspace_document_status status;
    host->documents = workspace_documents_new(root, &status);
    if (!host->documents) {
        snprintf(error, size, "%s", workspace_document_status_text(status));
        goto fail;
    }
    if (config->document_state) {
        status = workspace_documents_restore(
            host->documents,
            config->document_state,
            config->document_state_bytes
        );
        if (status != WORKSPACE_DOCUMENT_OK) {
            snprintf(error, size, "Cannot restore document state: %s", workspace_document_status_text(status));
            goto fail;
        }
        workspace_info_set_documents_restored(host->info, true);
    }
    host->resources = workspace_resources_new(root, error, size);
    if (!host->resources) {
        goto fail;
    }
    host->file_history = workspace_file_history_new(root, host->documents);
    if (!host->file_history) {
        snprintf(error, size, "Cannot create rooted file history service");
        goto fail;
    }
    workspace_resource_result inventory = workspace_resources_refresh(host->resources);
    if (!inventory.ok && !inventory.total) {
        snprintf(error, size, "%s", inventory.message);
        goto fail;
    }
    host->content = chibi_project_content(root, error, size);
    if (!host->content) {
        goto fail;
    }
    host->content_prefix = xstrdup(workspace_info_get(host->info)->content);
    host->css.documents = host->documents;
    host->css.provider = ui_source_provider_new(root, host->content_prefix, host->documents, error, size);
    if (!host->css.provider) {
        goto fail;
    }
    host->previews.previews = visual_previews_new(host->documents, NULL, host->content, root);
    host->previews.resources = host->resources;
    if (!visual_previews_refresh_catalog(
            host->previews.previews,
            host->resources,
            host->scope_error,
            sizeof host->scope_error
        )) {
        authoring_observations_append(host->observations, 0, "catalog", "error", host->scope_error, "", 0);
    }
    package_identity_load(&host->identity, NULL, host->scope_error, sizeof host->scope_error);
    host->thumbnail_native = authoring_renderer_catalog_new(error, size);
    if (!host->thumbnail_native) {
        goto fail;
    }
    host->thumbnail_context.native = host->thumbnail_native;
    host->thumbnail_linker = native_capabilities_prepared_linker(&host->thumbnail_context);
    host->thumbnail_player = config->player;
    host->thumbnail_player.linker = &host->thumbnail_linker;
    host->thumbnail_player.log = (execution_log_service){ 0 };
    host->thumbnail_player.decoded_budget = 0;
    host->backend = config->backend;
    host->compiler = config->compiler ? xstrdup(config->compiler) : NULL;
    host->previews.engine_version = host->identity.version;
    host->previews.build_id = host->identity.build_id;
    host->previews.manifest_hash = host->identity.manifest_sha256;
    host->models = visual_models_new(host->documents, root);
    visual_models_set_scope(host->models, enter_model, leave_model, host);
    visual_models_set_images(host->models, resolve_image, host);
    visual_models_set_catalog(host->models, model_catalog, host);
    host->ui_source = ui_authoring_new(host->documents);
    if (!host_services_new(host, config, error, size)) {
        goto fail;
    }
    if (!image_prepare_loader_new(host->content, &host->thumbnail_images, error, size)) {
        goto fail;
    }
    resource_preview_config thumbnails = {
        .root = root,
        .content = host->content,
        .inventory = host->resources,
        .overlays = host->documents,
        .images = host->thumbnail_images,
        .catalog = model_catalog,
        .catalog_user = host,
        .source_catalog = thumbnail_source_catalog,
        .source_catalog_release = thumbnail_source_catalog_release,
        .source_catalog_user = host,
        .scene_runtime = thumbnail_scene_runtime,
        .scene_runtime_user = host
    };
    host->thumbnails = resource_previews_new(&thumbnails, error, size);
    if (!host->thumbnails) {
        goto fail;
    }
    host->semantics = (document_semantics_service){
        .documents = host->documents,
        .catalog = native_capabilities_catalog(native),
        .content_prefix = host->content_prefix
    };
    host->assets = workspace_assets_service_new(
        host->documents,
        root,
        native_capabilities_catalog(native),
        error,
        size
    );
    if (!host->assets) {
        goto fail;
    }
    workspace_runtime_config runtime_config = {
        .documents = host->documents,
        .root = root,
        .compiler = config->compiler,
        .host = config->player,
        .backend = config->backend,
        .ui_userdata = host,
        .ui_retire = runtime_ui_retire,
        .engine_version = host->identity.version,
        .build_id = host->identity.build_id,
        .manifest_hash = host->identity.manifest_sha256
    };
    host->runtime = workspace_runtime_new(&runtime_config, error, size);
    if (!host->runtime) {
        goto fail;
    }
    host->simulation = workspace_simulation_new(host->runtime);
    if (!host->simulation) {
        snprintf(error, size, "Cannot create scoped simulation service");
        goto fail;
    }
    ui_source_preview_config ui_preview_config = {
        .root = root,
        .content = workspace_info_get(host->info)->content,
        .documents = host->documents,
        .runtime = host->runtime,
        .dom = host->dom
    };
    host->ui_preview = ui_source_preview_new(&ui_preview_config, error, size);
    if (!host->ui_preview) {
        goto fail;
    }
    host->project = project_semantics_new_content(
        host->documents,
        root,
        host->content_prefix,
        native_capabilities_catalog(native)
    );
    project_semantics_set_catalog(host->project, component_catalog, host);
    project_semantics_set_candidate_catalog(host->project, candidate_catalog, release_candidate_catalog, host);
    project_semantics_set_registry(host->project, prototype_catalog, host);
    host->semantics.catalog_acquire = project_semantics_catalog_acquire;
    host->semantics.catalog_release = project_semantics_catalog_release;
    host->semantics.catalog_user = host->project;
    host->debug = workspace_debug_new(debug_sources, host);
    workspace_debug_set_entities(host->debug, debug_entities);
    workspace_debug_set_owner_resolver(host->debug, debug_owner);
    workspace_debug_set_types(host->debug, debug_types);
    workspace_debug_set_source_root(host->debug, debug_source_root);
    workspace_debug_set_scope(host->debug, enter_debug, leave_debug);
    host->visuals = (visual_authoring_service){ .documents = host->documents,
                                                .content_root = host->content,
                                                .workspace_root = root,
                                                .link_source = link_source,
                                                .link_userdata = host,
                                                .resolve_image = resolve_image,
                                                .image_userdata = host };
    return host;
fail:
    workspace_host_free(host);
    return NULL;
}
bool workspace_host_bind(workspace_host *host, source_registry *sources, char *error, size_t size)
{
    const struct
    {
        const char *name;
        void *service;
    } services[] = { { "workspace_documents", host->documents },
                     { "workspace_file_history", host->file_history },
                     { "workspace_info", host->info },
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
                     { "workspace_imports", host->imports },
#endif
                     { "workspace_resources", host->resources },
                     { "document_semantics", host->documents ? &host->semantics : NULL },
                     { "workspace_assets", host->assets },
                     { "visual.authoring", host->documents ? &host->visuals : NULL },
                     { "visual.previews", host->documents ? &host->previews : NULL },
                     { "visual.model", host->models },
                     { "ui_authoring", host->ui_source },
                     { "ui_dom", host->dom },
                     { "workspace_css", host->css.provider ? &host->css : NULL },
                     { "ui_source_preview", host->ui_preview },
                     { "authoring_platform", host->platform },
                     { "workspace_runtime", host->runtime },
                     { "workspace_simulation", host->simulation },
                     { "authoring_observations", host->observations },
                     { "browser_messages", host->messages },
                     { "resource_previews", host->thumbnails },
                     { "project_semantics", host->project },
                     { "workspace.debug", host->debug } };
    for (size_t i = 0; i < sizeof services / sizeof *services; i++) {
        if (!services[i].service) {
            continue;
        }
        if (source_bind_service(sources, services[i].name, services[i].service)) {
            snprintf(
                error,
                size,
                "Cannot bind authoring service: %s",
                services[i].name
            );
            return false;
        }
    }
    return true;
}
void workspace_host_pump(workspace_host *host, render_ui *ui)
{
    ui_dom_context(host->dom, render_ui_context(ui));
    authoring_platform_tick(host->platform);
    if (!host->runtime) {
        return;
    }
    resource_previews_tick(host->thumbnails);
    int thumbnail_handles[8];
    int thumbnail_count = resource_preview_handles(host->thumbnails, thumbnail_handles, 8);
    for (int i = 0; i < host->thumbnail_bound_count; i++) {
        bool found = false;
        for (int j = 0; j < thumbnail_count; j++) {
            found |= host->thumbnail_bound[i] == thumbnail_handles[j];
        }
        if (!found) {
            char name[64];
            snprintf(name, sizeof name, "resource/%d", host->thumbnail_bound[i]);
            render_ui_bind_texture(ui, name, 0, 0);
        }
    }
    for (int i = 0; i < thumbnail_count; i++) {
        resource_preview_view view;
        if (resource_preview_read(host->thumbnails, thumbnail_handles[i], &view)) {
            char name[64];
            snprintf(name, sizeof name, "resource/%d", thumbnail_handles[i]);
            render_ui_bind_texture(ui, name, view.texture, view.texture_revision);
        }
    }
    memcpy(host->thumbnail_bound, thumbnail_handles, (size_t)thumbnail_count * sizeof *thumbnail_handles);
    host->thumbnail_bound_count = thumbnail_count;
    workspace_runtime_result runtime = workspace_runtime_poll(host->runtime);
    const char *runtime_error = runtime.error ? runtime.error : "";
    if (!host->runtime_error || strcmp(host->runtime_error, runtime_error)) {
        free(host->runtime_error);
        host->runtime_error = xstrdup(runtime_error);
        if (*runtime_error) {
            authoring_observations_append(
                host->observations,
                runtime.time,
                "target",
                "error",
                runtime_error,
                "",
                0
            );
        }
    }
    if (host->runtime_bound && host->runtime_bound != runtime.session) {
        char name[64];
        snprintf(name, sizeof name, "runtime/%d", host->runtime_bound);
        render_ui_bind_texture(ui, name, 0, 0);
    }
    if (runtime.session) {
        char name[64];
        snprintf(name, sizeof name, "runtime/%d", runtime.session);
        render_ui_bind_texture(ui, name, runtime.texture, runtime.texture_revision);
    }
    host->runtime_bound = runtime.session;
    ui_source_preview_result ui_preview = ui_source_preview_current(host->ui_preview);
    if (host->ui_preview_bound && host->ui_preview_bound != ui_preview.preview) {
        char name[64];
        snprintf(name, sizeof name, "ui-source/%d", host->ui_preview_bound);
        render_ui_bind_texture(ui, name, 0, 0);
    }
    if (ui_preview.preview) {
        char name[64];
        snprintf(name, sizeof name, "ui-source/%d", ui_preview.preview);
        render_ui_bind_texture(ui, name, ui_preview.texture, ui_preview.texture_revision);
    }
    host->ui_preview_bound = ui_preview.preview;
    render_ui *runtime_ui = workspace_runtime_ui(host->runtime);
    if (runtime_ui && (runtime_ui != host->runtime_ui ||
                       render_ui_context(runtime_ui) != host->runtime_context)) {
        char key[64];
        snprintf(key, sizeof key, "runtime/%d", runtime.session);
        ui_dom_register(host->dom, key, render_ui_context(runtime_ui));
        host->runtime_ui = runtime_ui;
        host->runtime_context = render_ui_context(runtime_ui);
    }
    const engine_frame_timing *timing = workspace_runtime_timing(host->runtime);
    authoring_timing observed = { .gpu_ms = -1 };
    if (timing) {
        observed = (authoring_timing){ .available = true,
                                       .time = timing->simulation_time,
                                       .behavior_ms = timing->behavior_ms,
                                       .physics_ms = timing->physics_ms,
                                       .systems_ms = timing->systems_ms,
                                       .rendering_ms = timing->rendering_ms,
                                       .gpu_ms = -1,
                                       .steps = timing->steps,
                                       .backlog = timing->event_backlog,
                                       .playing = runtime.playing };
        render_radiance_status lighting;
        if (workspace_runtime_lighting(host->runtime, &lighting)) {
            observed.lighting = true;
            observed.gpu_ms = lighting.gpu_ms;
            observed.budget_ms = lighting.budget_ms;
            observed.resolution = lighting.resolution;
        }
    }
    authoring_observations_timing(host->observations, true, observed);
    visual_previews_poll(host->previews.previews);
    int handles[PREVIEWS];
    int count = visual_preview_handles(host->previews.previews, handles, PREVIEWS);
    for (int i = 0; i < host->bound_count; i++) {
        bool found = false;
        for (int j = 0; j < count; j++) {
            found |= host->bound[i] == handles[j];
        }
        if (!found) {
            char name[64];
            snprintf(name, sizeof name, "preview/%d", host->bound[i]);
            render_ui_bind_texture(ui, name, 0, 0);
        }
    }
    for (int i = 0; i < count; i++) {
        visual_preview_view view;
        if (visual_preview_read(host->previews.previews, handles[i], &view)) {
            char name[64];
            snprintf(name, sizeof name, "preview/%d", handles[i]);
            render_ui_bind_texture(ui, name, view.texture, view.texture_revision);
        }
    }
    memcpy(host->bound, handles, (size_t)count * sizeof *handles);
    host->bound_count = count;
}
void workspace_host_record_log(workspace_host *host, double time, const char *program, const char *text)
{
    if (host) {
        authoring_observations_append(host->observations, time, program, "log", text, "", 0);
    }
}
void workspace_host_record_timing(workspace_host *host, authoring_timing timing)
{
    authoring_observations_timing(host->observations, false, timing);
}
bool workspace_host_quit_requested(const workspace_host *host)
{
    return authoring_platform_quit_requested(host->platform);
}
void workspace_host_close_request(workspace_host *host)
{
    authoring_platform_close_request(host->platform);
}
void workspace_host_detach_ui(workspace_host *host)
{
    if (host) {
        ui_dom_context(host->dom, NULL);
        host->bound_count = 0;
        host->ui_preview_bound = 0;
    }
}
double workspace_host_ui_scale(const workspace_host *host)
{
    return authoring_platform_ui_scale(host->platform);
}
bool workspace_host_text_input_requested(const workspace_host *host)
{
    return host && authoring_platform_text_input_requested(host->platform);
}
void workspace_host_set_frame_focus(workspace_host *host, bool focused)
{
    if (host) {
        authoring_platform_set_frame_focus(host->platform, focused);
    }
}
const char *workspace_host_pending_project(const workspace_host *host)
{
    return authoring_platform_pending_project(host->platform);
}
void workspace_host_project_complete(workspace_host *host, bool success, const char *error)
{
    authoring_platform_project_complete(host->platform, success, error);
}
void workspace_host_free(workspace_host *host)
{
    if (!host) {
        return;
    }
    ui_source_provider_free(host->css.provider);
    host->css.provider = NULL;
    workspace_debug_free(host->debug);
    ui_source_preview_free(host->ui_preview);
    host->ui_preview = NULL;
    workspace_simulation_free(host->simulation);
    workspace_runtime_free(host->runtime);
    ui_dom_free(host->dom);
    resource_previews_free(host->thumbnails);
    native_capabilities_free(host->thumbnail_native);
    render_image_loader_release(&host->thumbnail_images);
    browser_messages_free(host->messages);
    authoring_platform_free(host->platform);
    ui_authoring_free(host->ui_source);
    visual_models_free(host->models);
    visual_previews_free(host->previews.previews);
    workspace_resources_free(host->resources);
    project_semantics_free(host->project);
    workspace_assets_service_free(host->assets);
    workspace_file_history_free(host->file_history);
    workspace_documents_free(host->documents);
    workspace_info_free(host->info);
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
    workspace_imports_free(host->imports);
#endif
    authoring_observations_free(host->observations);
    package_identity_free(&host->identity);
    free(host->content);
    free(host->content_prefix);
    free(host->runtime_error);
    free(host->compiler);
    free(host);
}
