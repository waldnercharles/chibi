#include "render/ui.h"

#include "assets/ui_document.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/file_watch.h"
#include "support/text_file.h"
#include "support/xalloc.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The reference height at which one RCSS dp is one pixel. */
#define UI_REFERENCE_HEIGHT 720.0f

/* Every document starts from this: it fills the view with the editor's face. */
static const char
    BASE_RCSS[] = "body { width: 100%; height: 100%; font-family: Inter; "
                  "font-size: 16dp; color: #ffffff; }";

/* One GameUi entity's document. */
typedef struct ui_view
{
    uint64_t entity;
    char *name;
    file_watch watch;
    ui_document source; /* the last valid asset */
    rml_element *doc;
    uint64_t applied; /* the model revision already applied */
    bool seen;
} ui_view;

struct render_ui
{
    rml_backend *backend;
    char *content_root;
    ui_view *views;
    double time;
    int width, height;
    char error[256];
};

render_ui *render_ui_new(const char *content_root)
{
    render_ui *ui = xcalloc(1, sizeof *ui);
    ui->content_root = xstrdup(content_root);
    ui->backend = rml_backend_create(1, 1, 1);
    return ui;
}

static void close_view(ui_view *view)
{
    if (view->doc) {
        rml_document_close(view->doc);
    }
    free(view->name);
    ui_document_free(&view->source);
    *view = (ui_view){ 0 };
}

void render_ui_free(render_ui *ui)
{
    if (!ui) {
        return;
    }
    for (int i = 0; i < acount(ui->views); i++) {
        close_view(&ui->views[i]);
    }
    afree(ui->views);
    rml_backend_destroy(ui->backend);
    free(ui->content_root);
    free(ui);
}

const char *render_ui_error(const render_ui *ui)
{
    return ui->error;
}

static ui_view *find_view(render_ui *ui, uint64_t entity)
{
    for (int i = 0; i < acount(ui->views); i++) {
        if (ui->views[i].entity == entity) {
            return &ui->views[i];
        }
    }
    return NULL;
}

/* Reads the view's asset when it changed and reloads its document from the
 * last valid version. */
static void sync_document(render_ui *ui, ui_view *view)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/ui/%s.yaml", ui->content_root, view->name);
    if (!file_watch_changed(&view->watch, path, ui->time)) {
        return;
    }
    char *text = text_file_read(path);
    char error[200] = "is missing";
    bool read = text && ui_document_read(text, &view->source, error, sizeof error);
    free(text);
    if (!read) {
        snprintf(ui->error, sizeof ui->error, "ui/%s.yaml %s", view->name, error);
        return;
    }
    ui->error[0] = '\0';
    if (view->doc) {
        rml_document_close(view->doc);
    }
    char *rml = ui_document_compose(&view->source, BASE_RCSS);
    rml_context *context = rml_backend_context(ui->backend);
    view->doc = rml_context_load_document_from_memory(context, rml, path);
    free(rml);
    view->applied = 0;
    if (view->doc) {
        rml_document_show(view->doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_AUTO);
    }
}

static void show_view(render_ui *ui, uint64_t entity, const GameUi *component)
{
    ui_view *view = find_view(ui, entity);
    if (!view) {
        apush(ui->views, ((ui_view){ .entity = entity }));
        view = &alast(ui->views);
    }
    view->seen = true;
    const char *name = component->document ? component->document : "";
    if (!view->name || strcmp(view->name, name) != 0) {
        close_view(view);
        view->entity = entity;
        view->seen = true;
        view->name = xstrdup(name);
    }
    if (name[0]) {
        sync_document(ui, view);
    }
    if (!view->doc) {
        return;
    }
    char order[32];
    snprintf(order, sizeof order, "%d", component->order);
    rml_element_set_property(view->doc, "z-index", order);
    rml_element_set_property(view->doc, "display", component->hidden ? "none" : "block");
}

static void escape_text(char *out, size_t size, const char *text)
{
    size_t n = 0;
    for (const char *c = text; *c && n + 6 < size; c++) {
        const char *entity = *c == '<' ? "&lt;"
                           : *c == '>' ? "&gt;"
                           : *c == '&' ? "&amp;"
                                       : NULL;
        if (entity) {
            n += (size_t)snprintf(out + n, size - n, "%s", entity);
            continue;
        }
        out[n++] = *c;
    }
    out[n] = '\0';
}

static void apply_element(rml_element *doc, game_ui_element *e)
{
    rml_element *element = rml_element_by_id(doc, e->id);
    if (!element) {
        return;
    }
    if (e->text) {
        char text[1024];
        escape_text(text, sizeof text, e->text);
        rml_element_set_inner_rml(element, text);
    }
    for (int i = 0; i < acount(e->classes); i++) {
        rml_element_set_class(element, e->classes[i].name, e->classes[i].on);
    }
    if (e->visible == 0) {
        rml_element_set_property(element, "display", "none");
    } else if (e->visible == 1) {
        rml_element_remove_property(element, "display");
    }
    if (e->has_value) {
        char value[32];
        snprintf(value, sizeof value, "%g", e->value);
        rml_element_set_attribute(element, "value", value);
    }
    if (e->focus) {
        rml_element_focus(element, true);
        e->focus = false;
    }
}

static void apply_model(render_ui *ui, game_ui *model)
{
    uint64_t revision = game_ui_revision(model);
    for (int i = 0; i < game_ui_element_count(model); i++) {
        game_ui_element *e = game_ui_element_at(model, i);
        ui_view *view = find_view(ui, e->entity);
        if (view && view->doc && e->revision > view->applied) {
            apply_element(view->doc, e);
        }
    }
    for (int i = 0; i < acount(ui->views); i++) {
        ui->views[i].applied = revision;
    }
}

void render_ui_update(render_ui *ui, ecs_world *world, game_ui *model, int width, int height, double dt)
{
    assert(ui && world);
    ui->time += dt;
    if (width != ui->width || height != ui->height) {
        ui->width = width;
        ui->height = height;
        rml_backend_resize_pixels(ui->backend, width, height, (float)height / UI_REFERENCE_HEIGHT);
    }
    for (int i = 0; i < acount(ui->views); i++) {
        ui->views[i].seen = false;
    }
    ecs_component_id component = GAME_COMPONENT_GameUi.id;
    ecs_query query;
    if (ecs_world_component(world, component) &&
        ecs_query_begin(world, &query, &component, 1) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (!ecs_entity_is_retiring(world, entity)) {
                show_view(ui, ecs_entity_to_u64(entity), GAME_GameUi_get_const(world, entity));
            }
        }
        ecs_query_end(&query);
    }
    int kept = 0;
    for (int i = 0; i < acount(ui->views); i++) {
        if (ui->views[i].seen) {
            ui->views[kept++] = ui->views[i];
            continue;
        }
        if (model) {
            game_ui_forget(model, ui->views[i].entity);
        }
        close_view(&ui->views[i]);
    }
    asetlen(ui->views, kept);
    if (model) {
        apply_model(ui, model);
    }
    rml_backend_update(ui->backend, dt);
}

void render_ui_composite(render_ui *ui, CF_Canvas target)
{
    if (!acount(ui->views) || ui->width <= 0 || ui->height <= 0) {
        return;
    }
    rml_backend_render(ui->backend);
    cf_draw_push();
    rml_backend_present(ui->backend);
    cf_render_to(target, false);
    cf_draw_pop();
}
