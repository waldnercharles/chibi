#include "authoring/ui_source_preview.h"

#include "authoring/ui_dom.h"
#include "authoring/ui_source_provider.h"
#include "authoring/workspace_runtime.h"
#include "render/shader_publish.h"
#include "render/ui.h"
#include "runtime/declared_components.h"
#include "runtime/world.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct ui_source_preview
{
    char *root, *content, *asset;
    workspace_documents *documents;
    workspace_runtime *runtime;
    ui_dom *dom;
    ui_source_provider *provider;
    render_ui *renderer;
    rml_context *registered;
    render_resource_context *fallback;
    ecs_world *empty;
    CF_Canvas canvas;
    int next_handle;
    uint64_t entity;
    bool page;
    char context[64], error[256];
    ui_source_preview_result result;
};
typedef struct
{
    render_resource_scope resources;
    declared_components_scope types;
    const world *world;
} preview_scope;
static ui_source_preview_result result(ui_source_preview *s, bool ok, const char *error)
{
    snprintf(s->error, sizeof s->error, "%s", error ? error : "");
    s->result.ok = ok;
    s->result.stale = false;
    s->result.error = s->error;
    s->result.context = s->context;
    if (!ok) {
        s->result.texture = 0;
    }
    return s->result;
}
static ui_source_preview_result stale(ui_source_preview *s, const char *error)
{
    result(s, false, error);
    s->result.stale = true;
    return s->result;
}
static bool enter(ui_source_preview *s, preview_scope *scope, int session)
{
    render_resource_context *resources = s->fallback;
    if (session > 0) {
        if (!s->runtime) {
            result(s, false, "UI preview has no target runtime service");
            return false;
        }
        workspace_runtime_result current = workspace_runtime_current(s->runtime);
        if (!current.ready || current.session != session) {
            result(s, false, "UI preview target session is unavailable or stale");
            return false;
        }
        scope->world = workspace_runtime_world(s->runtime);
        resources = render_resource_context_for_world(scope->world);
    }
    if (!render_resource_scope_enter(
            resources,
            &scope->resources,
            s->error,
            sizeof s->error
        )) {
        s->result.ok = false;
        s->result.texture = 0;
        return false;
    }
    declared_components *types = session > 0 ? workspace_runtime_types(s->runtime)
                                             : NULL;
    if (!declared_components_scope_enter(types, &scope->types)) {
        bool left = render_resource_scope_leave(
            &scope->resources,
            s->error,
            sizeof s->error
        );
        assert(left);
        (void)left;
        result(s, false, "UI preview type scope is unavailable");
        return false;
    }
    return true;
}
static void leave(preview_scope *scope)
{
    bool types = declared_components_scope_leave(&scope->types);
    assert(types);
    (void)types;
    char error[256];
    bool resources = render_resource_scope_leave(&scope->resources, error, sizeof error);
    assert(resources);
    (void)resources;
}
static void retire(ui_source_preview *s)
{
    if (s->dom && *s->context) {
        ui_dom_unregister(s->dom, s->context);
    }
    s->registered = NULL;
    if (s->renderer) {
        render_ui_free(s->renderer);
        s->renderer = NULL;
    }
    if (s->canvas.id) {
        cf_destroy_canvas(s->canvas);
        s->canvas = (CF_Canvas){ 0 };
    }
    ui_source_provider_free(s->provider);
    s->provider = NULL;
    free(s->asset);
    s->asset = NULL;
    s->context[0] = 0;
    s->result.texture = 0;
}
static bool retire_scoped(ui_source_preview *s)
{
    if (!s->renderer && !s->canvas.id) {
        retire(s);
        return true;
    }
    preview_scope scope = { 0 };
    if (!enter(s, &scope, s->result.session)) {
        return false;
    }
    retire(s);
    leave(&scope);
    return true;
}
ui_source_preview *ui_source_preview_new(const ui_source_preview_config *config, char *error, size_t size)
{
    assert(config && config->documents);
    ui_source_preview *s = xcalloc(1, sizeof *s);
    s->root = config->root ? xstrdup(config->root) : NULL;
    s->content = xstrdup(config->content ? config->content : "content");
    s->documents = config->documents;
    s->runtime = config->runtime;
    s->dom = config->dom;
    s->empty = ecs_world_new();
    s->fallback = render_resource_context_new();
    s->result.error = s->error;
    s->result.context = s->context;
    if (size) {
        error[0] = 0;
    }
    return s;
}
void ui_source_preview_free(ui_source_preview *s)
{
    if (!s) {
        return;
    }
    bool retired = retire_scoped(s);
    assert(retired);
    (void)retired;
    ecs_world_free(s->empty);
    char error[256];
    bool freed = render_resource_context_free(s->fallback, error, sizeof error);
    assert(freed);
    (void)freed;
    free(s->root);
    free(s->content);
    free(s);
}
ui_source_preview_result ui_source_preview_current(const ui_source_preview *s)
{
    assert(s);
    return s->result;
}
ui_source_preview_result ui_source_preview_open(
    ui_source_preview *s,
    int document,
    int revision,
    const char *asset,
    int session,
    uint64_t entity
)
{
    assert(s);
    workspace_document_view view;
    if (!s->root) {
        return result(s, false, "UI source preview requires an active workspace");
    }
    if (session < 0) {
        return result(s, false, "UI preview session must be nonnegative");
    }
    if (entity && !session) {
        return result(s, false, "Binding a Ui model entity requires a target session");
    }
    workspace_document_status status = workspace_document_read(s->documents, document, &view);
    if (status || view.revision != revision) {
        return result(s, false, "UI source document revision is stale");
    }
    if (!asset || !*asset) {
        return result(s, false, "Choose an RML asset to preview this source");
    }
    char error[256];
    ui_source_provider *
        candidate = ui_source_provider_new(s->root, s->content, s->documents, error, sizeof error);
    if (!candidate) {
        return result(s, false, error);
    }
    render_ui_loader loader = ui_source_provider_loader(candidate);
    void *probe = loader.open_context(loader.context, "", asset, true);
    ui_document source = { 0 };
    int read = loader.read(probe, 0, &source, error, sizeof error);
    loader.close(probe);
    bool page = source.body != NULL;
    ui_document_free(&source);
    if (read == 1 && !ui_source_provider_contains(candidate, view.path)) {
        read = -1;
        snprintf(error, sizeof error, "The selected source is not used by this UI asset");
    }
    if (read != 1) {
        ui_source_provider_free(candidate);
        return result(s, false, error);
    }
    bool same = s->result.preview > 0 && s->result.document == document &&
                s->result.session == session && s->entity == entity &&
                s->page == page && s->asset && !strcmp(s->asset, asset);
    if (!same && s->next_handle == INT_MAX) {
        ui_source_provider_free(candidate);
        return result(s, false, "UI preview handle exhausted");
    }
    if (!same) {
        if (!retire_scoped(s)) {
            ui_source_provider_free(candidate);
            return s->result;
        }
        s->result.preview = ++s->next_handle;
        s->asset = xstrdup(asset);
        s->entity = entity;
        s->page = page;
        snprintf(s->context, sizeof s->context, "source-preview/%d", s->result.preview);
    }
    if (s->renderer) {
        preview_scope scope = { 0 };
        if (!enter(s, &scope, s->result.session)) {
            ui_source_provider_free(candidate);
            return s->result;
        }
        render_ui_set_loader(s->renderer, &loader);
        leave(&scope);
    }
    ui_source_provider_free(s->provider);
    s->provider = candidate;
    s->result.document = document;
    s->result.revision = revision;
    s->result.session = session;
    s->result.texture = 0;
    return result(s, true, "");
}
ui_source_preview_result ui_source_preview_draw(
    ui_source_preview *s,
    int preview,
    int revision,
    int width,
    int height,
    double seconds
)
{
    assert(s);
    if (!s->provider || preview <= 0 || preview != s->result.preview ||
        revision != s->result.revision) {
        return result(s, false, "UI source preview handle or revision is stale");
    }
    if (width < 1 || height < 1 || width > 2048 || height > 2048 ||
        !isfinite(seconds) || seconds < 0 || seconds > 0.25) {
        return result(s, false, "UI preview dimensions must be 1..2048 and delta 0..0.25 seconds");
    }
    workspace_document_view source;
    workspace_document_status
        status = workspace_document_read(s->documents, s->result.document, &source);
    if (status || source.revision != revision) {
        return stale(s, "UI source changed; refresh preview before drawing");
    }
    if (!ui_source_provider_current(s->provider)) {
        return stale(s, "A linked UI source changed; refresh preview before drawing");
    }
    if (s->result.texture_revision == UINT64_MAX) {
        return result(s, false, "UI texture revision exhausted");
    }
    preview_scope scope = { 0 };
    if (!enter(s, &scope, s->result.session)) {
        return s->result;
    }
    if (s->entity &&
        !ecs_entity_is_alive(world_ecs(scope.world), ecs_entity_from_u64(s->entity))) {
        leave(&scope);
        return result(s, false, "UI preview model entity is no longer alive");
    }
    if (!s->renderer) {
        s->renderer = render_ui_new("", NULL);
        render_ui_loader loader = ui_source_provider_loader(s->provider);
        render_ui_set_loader(s->renderer, &loader);
        render_ui_set_model_entity(s->renderer, s->entity);
        if (!s->page) {
            render_ui_set_overlay(s->renderer, s->asset, "");
        }
    }
    if (s->canvas.id && (s->result.width != width || s->result.height != height)) {
        cf_destroy_canvas(s->canvas);
        s->canvas = (CF_Canvas){ 0 };
    }
    if (!s->canvas.id) {
        s->canvas = cf_make_canvas(cf_canvas_defaults(width, height));
    }
    ui_world model = { .ecs = s->empty, .page = s->page ? s->asset : "", .read_only = true };
    if (scope.world) {
        const engine_modules *modules = world_modules(scope.world);
        model.ecs = world_ecs(scope.world);
        model.model = modules->ui;
        model.save = modules->save;
    }
    render_ui_update(s->renderer, &model, width, height, seconds);
    rml_context *context = render_ui_context(s->renderer);
    if (s->dom && (!context || context != s->registered)) {
        ui_dom_unregister(s->dom, s->context);
        if (!context || !ui_dom_register(s->dom, s->context, context)) {
            retire(s);
            leave(&scope);
            return result(s, false, "UI context is unavailable or registration budget exhausted");
        }
        s->registered = context;
    }
    const char *failure = render_ui_error(s->renderer);
    if (*failure) {
        char owned[256];
        snprintf(owned, sizeof owned, "%s", failure);
        leave(&scope);
        return result(s, false, owned);
    }
    cf_draw_push();
    cf_render_to(s->canvas, true);
    render_ui_composite(s->renderer, s->canvas);
    cf_draw_pop();
    s->result.width = width;
    s->result.height = height;
    s->result.texture = cf_canvas_get_target(s->canvas).id;
    s->result.texture_revision++;
    leave(&scope);
    return result(s, true, "");
}
ui_source_preview_result ui_source_preview_close(ui_source_preview *s, int preview)
{
    assert(s);
    if (preview <= 0 || preview != s->result.preview) {
        return result(s, false, "UI preview handle is stale");
    }
    if (!retire_scoped(s)) {
        return s->result;
    }
    s->result.preview = 0;
    return result(s, true, "");
}
void ui_source_preview_retire_target(ui_source_preview *s)
{
    assert(s);
    if (s->result.session > 0) {
        retire(s);
        s->result.preview = 0;
        result(s, false, "UI target retired; reopen source preview");
    }
}
