#include "render/ui.h"

#include "assets/ui_document.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/file_watch.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Mounts ---------- */

struct ui_mount
{
    char *content_root;
    char *name;
    file_watch watch;
    char **uses;             /* the widgets its asset uses */
    file_watch *use_watches; /* parallel to uses */
    char *markup, *style;    /* from the last valid asset */
    char error[256];
    rml_element *root;
    bool installed; /* root holds the current markup */
    ui_activation *pending;
    uint64_t applied; /* the model revision already applied */
    game_save *settings;
    uint64_t settings_seen; /* the settings revision shown; UINT64_MAX when stale */
    bool refreshing;        /* ignore the change events a refresh raises */
};

/* The events a mount's root reports. */
static const rml_event_id MOUNT_EVENTS[] = { RML_EVENT_CLICK,
                                             RML_EVENT_CHANGE,
                                             RML_EVENT_SUBMIT };

ui_mount *ui_mount_new(const char *content_root, const char *name)
{
    ui_mount *m = xcalloc(1, sizeof *m);
    m->content_root = xstrdup(content_root);
    m->name = xstrdup(name);
    return m;
}

static void free_uses(ui_mount *m)
{
    for (int i = 0; i < acount(m->uses); i++) {
        free(m->uses[i]);
    }
    afree(m->uses);
}

void ui_mount_free(ui_mount *m)
{
    if (!m) {
        return;
    }
    free_uses(m);
    afree(m->use_watches);
    afree(m->pending);
    free(m->content_root);
    free(m->name);
    free(m->markup);
    free(m->style);
    free(m);
}

const char *ui_mount_name(const ui_mount *m)
{
    return m->name;
}

const char *ui_mount_style(const ui_mount *m)
{
    return m->style ? m->style : "";
}

const char *ui_mount_error(const ui_mount *m)
{
    return m->error;
}

static void asset_path(const ui_mount *m, const char *name, char *path, size_t size)
{
    snprintf(path, size, "%s/ui/%s.yaml", m->content_root, name);
}

/* Whether the asset or a widget it uses changed; polls every watch. */
static bool changed(ui_mount *m, const char *path, double time)
{
    bool any = file_watch_changed(&m->watch, path, time);
    for (int i = 0; i < acount(m->uses); i++) {
        char use[1024];
        asset_path(m, m->uses[i], use, sizeof use);
        any |= file_watch_changed(&m->use_watches[i], use, time);
    }
    return any;
}

/* Replaces the watched widgets with `uses`, taking ownership. */
static void watch_uses(ui_mount *m, char **uses, double time)
{
    free_uses(m);
    m->uses = uses;
    asetlen(m->use_watches, acount(uses));
    for (int i = 0; i < acount(uses); i++) {
        char use[1024];
        asset_path(m, uses[i], use, sizeof use);
        m->use_watches[i] = (file_watch){ 0 };
        file_watch_changed(&m->use_watches[i], use, time);
    }
}

/* Reads the asset and its widgets when one changed; returns true when it
 * replaced the markup and RCSS. An invalid edit keeps the last valid ones. */
static bool read_asset(ui_mount *m, double time)
{
    char path[1024];
    asset_path(m, m->name, path, sizeof path);
    if (!changed(m, path, time)) {
        return false;
    }
    char *text = text_file_read(path);
    char error[200] = "is missing";
    ui_document source = { 0 };
    bool read = text && ui_document_read(text, &source, error, sizeof error);
    free(text);
    if (read) {
        char root[1024], **uses = NULL;
        snprintf(root, sizeof root, "%s/ui", m->content_root);
        read = ui_document_expand(&source, m->name, root, &uses, error, sizeof error);
        watch_uses(m, uses, time);
    }
    if (!read) {
        ui_document_free(&source);
        snprintf(m->error, sizeof m->error, "ui/%s.yaml %s", m->name, error);
        return false;
    }
    m->error[0] = '\0';
    free(m->markup);
    free(m->style);
    m->markup = source.rml;
    m->style = source.rcss;
    m->installed = false;
    return true;
}

bool ui_mount_sync(ui_mount *m, rml_element *root, rml_listener *listener, double time)
{
    assert(root && listener);
    bool restyled = read_asset(m, time);
    if (root != m->root) {
        m->root = root;
        m->installed = false;
        rml_element_set_attribute(root, "data-ui", m->name);
        for (size_t i = 0; i < sizeof MOUNT_EVENTS / sizeof *MOUNT_EVENTS; i++) {
            rml_element_add_event_listener(root, MOUNT_EVENTS[i], listener, false);
        }
    }
    if (!m->installed && m->markup) {
        rml_element_set_inner_rml(root, m->markup);
        m->installed = true;
        m->applied = 0;
        m->settings_seen = UINT64_MAX;
    }
    return restyled;
}

bool ui_mount_holds(const ui_mount *m, rml_element *element)
{
    for (; element; element = rml_element_parent(element)) {
        if (element == m->root) {
            return true;
        }
    }
    return false;
}

/* The element from `element` up that names an event in data-event. */
static rml_element *event_source(rml_element *element, char *name, size_t size)
{
    for (; element; element = rml_element_parent(element)) {
        if (rml_element_attribute(element, "data-event", name, size) > 0) {
            return element;
        }
    }
    return NULL;
}

/* The element from `element` up, within the mount, bound with data-setting. */
static rml_element *setting_source(const ui_mount *m, rml_element *element, char *key, size_t size)
{
    for (; element && element != m->root; element = rml_element_parent(element)) {
        if (rml_element_attribute(element, "data-setting", key, size) > 0) {
            return element;
        }
    }
    return NULL;
}

static bool input_type(rml_element *element, const char *type)
{
    char own[32] = "";
    rml_element_attribute(element, "type", own, sizeof own);
    return !strcmp(own, type);
}

/* Stores a changed data-setting element's value under its key. */
static void write_setting(ui_mount *m, rml_element *target)
{
    char key[128], value[256];
    rml_element *element = setting_source(m, target, key, sizeof key);
    if (!element) {
        return;
    }
    if (input_type(element, "checkbox")) {
        snprintf(value, sizeof value, "%s", rml_element_has_attribute(element, "checked") ? "true" : "false");
    } else if (input_type(element, "radio")) {
        if (!rml_element_has_attribute(element, "checked")) {
            return;
        }
        rml_element_attribute(element, "value", value, sizeof value);
    } else {
        rml_element_value(element, value, sizeof value);
    }
    game_save_set(m->settings, key, value);
}

/* Shows each data-setting element's stored value; unset keys stay as authored. */
static void refresh_settings(ui_mount *m)
{
    int count = rml_element_query_selector_all(m->root, "[data-setting]", NULL, 0);
    rml_element **elements = count ? xmalloc((size_t)count * sizeof *elements) : NULL;
    rml_element_query_selector_all(m->root, "[data-setting]", elements, count);
    m->refreshing = true;
    for (int i = 0; i < count; i++) {
        char key[128], own[256];
        rml_element_attribute(elements[i], "data-setting", key, sizeof key);
        const char *value = game_save_get(m->settings, key);
        if (!value) {
            continue;
        }
        bool check = input_type(elements[i], "checkbox") && !strcmp(value, "true");
        if (input_type(elements[i], "radio")) {
            rml_element_attribute(elements[i], "value", own, sizeof own);
            check = !strcmp(own, value);
        }
        if (!input_type(elements[i], "checkbox") && !input_type(elements[i], "radio")) {
            rml_element_set_value(elements[i], value);
            continue;
        }
        if (check) {
            rml_element_set_attribute(elements[i], "checked", "");
            continue;
        }
        rml_element_remove_attribute(elements[i], "checked");
    }
    m->refreshing = false;
    free(elements);
}

void ui_mount_settings(ui_mount *m, game_save *settings)
{
    if (settings != m->settings) {
        m->settings = settings;
        m->settings_seen = UINT64_MAX;
    }
    if (!m->installed || !settings || m->settings_seen == game_save_revision(settings)) {
        return;
    }
    refresh_settings(m);
    m->settings_seen = game_save_revision(settings);
}

/* A click activates buttons and other elements; inputs report changes, which
 * carry their value and first update a bound setting. */
void ui_mount_listen(ui_mount *m, const rml_event *event)
{
    if (m->refreshing) {
        return;
    }
    if (event->id == RML_EVENT_CHANGE && m->settings) {
        write_setting(m, event->target);
    }
    ui_activation a = { 0 };
    rml_element *source = event_source(event->target, a.event, sizeof a.event);
    bool input = source && strcmp(rml_element_tag_name(source), "input") == 0;
    if (!source || !ui_mount_holds(m, source) ||
        (event->id == RML_EVENT_CLICK && input) ||
        (event->id == RML_EVENT_CHANGE && !input)) {
        return;
    }
    snprintf(a.id, sizeof a.id, "%s", rml_element_id(source));
    char value[64];
    if (event->id == RML_EVENT_CHANGE &&
        rml_element_value(source, value, sizeof value) > 0) {
        char *end;
        a.value = strtof(value, &end);
        a.has_value = end != value;
    }
    apush(m->pending, a);
}

bool ui_mount_take(ui_mount *m, ui_activation *out)
{
    int count = acount(m->pending);
    if (!count) {
        return false;
    }
    *out = m->pending[0];
    memmove(m->pending, m->pending + 1, (size_t)(count - 1) * sizeof *m->pending);
    asetlen(m->pending, count - 1);
    return true;
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

static void apply_to(rml_element *element, const game_ui_element *e)
{
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
}

/* Applies the setting to every element its selector matches in `root`;
 * focus goes to the first. */
static void apply_element(rml_element *root, game_ui_element *e)
{
    int count = rml_element_query_selector_all(root, e->selector, NULL, 0);
    rml_element **matches = count ? xmalloc((size_t)count * sizeof *matches) : NULL;
    rml_element_query_selector_all(root, e->selector, matches, count);
    for (int i = 0; i < count; i++) {
        apply_to(matches[i], e);
    }
    if (e->focus && count) {
        rml_element_focus(matches[0], true);
    }
    e->focus = false;
    free(matches);
}

void ui_mount_apply(ui_mount *m, game_ui *model, uint64_t entity)
{
    if (!m->installed) {
        return;
    }
    for (int i = 0; i < game_ui_element_count(model); i++) {
        game_ui_element *e = game_ui_element_at(model, i);
        if (e->entity == entity && e->revision > m->applied) {
            apply_element(m->root, e);
        }
    }
    m->applied = game_ui_revision(model);
}

/* ---------- Game host ---------- */

/* The reference height at which one RCSS dp is one pixel. */
#define UI_REFERENCE_HEIGHT 720.0f
/* The overlay's z-index, above any authored GameUi order. */
#define OVERLAY_ORDER "1000000"

/* The game document's own style: the editor's face, and a pointer that passes
 * through to the game except over what a player activates. */
static const char BASE_RCSS
    [] = "body { width: 100%; height: 100%; font-family: Inter; "
         "font-size: 16dp; color: #ffffff; pointer-events: none; }\n"
         "[data-event], input, select, textarea { pointer-events: auto; }\n";

/* One GameUi entity's mount, or the host's overlay. */
typedef struct ui_view
{
    uint64_t entity;
    ui_mount *mount;
    rml_element *root;
    bool seen;
} ui_view;

struct render_ui
{
    rml_backend *backend;
    rml_element *doc;
    rml_listener *listener;
    char *content_root;
    ui_view *views;
    ui_view overlay; /* its entity is unused */
    char overlay_state[32];
    char overlay_event[128];
    bool restyle; /* a mount's RCSS changed or a mount closed */
    double time;
    int width, height;
    float scale;
    char error[256];
};

static void on_event(void *user, const rml_event *event);

render_ui *render_ui_new(const char *content_root)
{
    render_ui *ui = xcalloc(1, sizeof *ui);
    ui->content_root = xstrdup(content_root);
    ui->backend = rml_backend_create(1, 1, 1);
    ui->listener = rml_listener_create(on_event, ui);
    ui->scale = 1;
    rml_context *context = rml_backend_context(ui->backend);
    ui->doc = rml_context_load_document_from_memory(context, "<rml><head/><body/></rml>", "game-ui");
    assert(ui->doc);
    rml_document_set_style_sheet(ui->doc, BASE_RCSS);
    rml_document_show(ui->doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_AUTO);
    return ui;
}

/* Removes the view's root, which detaches the listener, then its mount. */
static void close_view(render_ui *ui, ui_view *view)
{
    if (view->root) {
        rml_element_remove_child(ui->doc, view->root);
        ui->restyle = true;
    }
    ui_mount_free(view->mount);
    *view = (ui_view){ .entity = view->entity };
}

void render_ui_free(render_ui *ui)
{
    if (!ui) {
        return;
    }
    for (int i = 0; i < acount(ui->views); i++) {
        close_view(ui, &ui->views[i]);
    }
    afree(ui->views);
    close_view(ui, &ui->overlay);
    rml_backend_destroy(ui->backend);
    rml_listener_destroy(ui->listener);
    free(ui->content_root);
    free(ui);
}

const char *render_ui_error(const render_ui *ui)
{
    return ui->error;
}

/* Opens a mount of `name` in a new full-view root stacked at `order`. */
static void open_view(render_ui *ui, ui_view *view, const char *name, const char *order)
{
    static const char *const LAYOUT[][2] = {
        { "position", "absolute" }, { "left", "0" },      { "top", "0" },
        { "width", "100%" },        { "height", "100%" },
    };
    view->mount = ui_mount_new(ui->content_root, name);
    view->root = rml_element_create_child(ui->doc, "div", NULL);
    for (size_t i = 0; i < sizeof LAYOUT / sizeof *LAYOUT; i++) {
        rml_element_set_property(view->root, LAYOUT[i][0], LAYOUT[i][1]);
    }
    rml_element_set_property(view->root, "z-index", order);
}

static void on_event(void *user, const rml_event *event)
{
    render_ui *ui = user;
    if (ui->overlay.mount && ui_mount_holds(ui->overlay.mount, event->target)) {
        ui_mount_listen(ui->overlay.mount, event);
        return;
    }
    for (int i = 0; i < acount(ui->views); i++) {
        if (ui->views[i].mount && ui_mount_holds(ui->views[i].mount, event->target)) {
            ui_mount_listen(ui->views[i].mount, event);
            return;
        }
    }
}

void render_ui_set_overlay(render_ui *ui, const char *name, const char *state)
{
    ui_view *view = &ui->overlay;
    if (view->mount && strcmp(ui_mount_name(view->mount), name) != 0) {
        close_view(ui, view);
    }
    if (!view->mount) {
        open_view(ui, view, name, OVERLAY_ORDER);
        ui->overlay_state[0] = '\0';
    }
    if (state) {
        ui->restyle |= ui_mount_sync(view->mount, view->root, ui->listener, ui->time);
    }
    rml_element_set_property(view->root, "display", state ? "block" : "none");
    if (state && strcmp(state, ui->overlay_state) != 0) {
        snprintf(ui->overlay_state, sizeof ui->overlay_state, "%s", state);
        rml_element_set_class_names(view->root, state);
    }
}

bool render_ui_take_overlay_event(render_ui *ui, char *name, size_t size)
{
    ui_activation a;
    while (ui->overlay.mount && ui_mount_take(ui->overlay.mount, &a)) {
        snprintf(ui->overlay_event, sizeof ui->overlay_event, "%s", a.event);
    }
    if (!ui->overlay_event[0]) {
        return false;
    }
    snprintf(name, size, "%s", ui->overlay_event);
    ui->overlay_event[0] = '\0';
    return true;
}

unsigned render_ui_input(render_ui *ui, const rml_input *input)
{
    rml_input scaled = *input;
    scaled.x /= ui->scale;
    scaled.y /= ui->scale;
    return rml_backend_process_input(ui->backend, &scaled);
}

/* Sends each view's activations to its entity, an input's value first. */
static void send_activations(render_ui *ui, const ui_world *world)
{
    game_ui *model = world->model;
    game_entities *entities = world->entities;
    for (int i = 0; i < acount(ui->views); i++) {
        ui_view *view = &ui->views[i];
        ui_activation a;
        while (view->mount && ui_mount_take(view->mount, &a)) {
            if (model && a.has_value && a.id[0]) {
                char selector[160];
                snprintf(selector, sizeof selector, "#%s", a.id);
                game_ui_set_value(model, view->entity, selector, a.value);
            }
            if (entities) {
                game_entities_request_event(
                    entities,
                    ecs_entity_from_u64(view->entity),
                    a.event,
                    (flow_inputs){ 0 }
                );
            }
        }
    }
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

static void show_view(render_ui *ui, uint64_t entity, const GameUi *component)
{
    ui_view *view = find_view(ui, entity);
    if (!view) {
        apush(ui->views, ((ui_view){ .entity = entity }));
        view = &alast(ui->views);
    }
    view->seen = true;
    const char *name = component->document ? component->document : "";
    if (view->mount && strcmp(ui_mount_name(view->mount), name) != 0) {
        close_view(ui, view);
        view->seen = true;
    }
    if (!name[0]) {
        return;
    }
    char order[32];
    snprintf(order, sizeof order, "%d", component->order);
    if (!view->mount) {
        open_view(ui, view, name, order);
    }
    ui->restyle |= ui_mount_sync(view->mount, view->root, ui->listener, ui->time);
    rml_element_set_property(view->root, "z-index", order);
    rml_element_set_property(view->root, "display", component->hidden ? "none" : "block");
}

/* Drops the views whose entity no longer shows a GameUi. */
static void close_unseen(render_ui *ui, game_ui *model)
{
    int kept = 0;
    for (int i = 0; i < acount(ui->views); i++) {
        if (ui->views[i].seen) {
            ui->views[kept++] = ui->views[i];
            continue;
        }
        if (model) {
            game_ui_forget(model, ui->views[i].entity);
        }
        close_view(ui, &ui->views[i]);
    }
    asetlen(ui->views, kept);
}

/* The overlay's mount for i < 0, else view i's. */
static ui_mount *mount_at(const render_ui *ui, int i)
{
    return i < 0 ? ui->overlay.mount : ui->views[i].mount;
}

/* Puts each shown asset's RCSS after the base, once per asset. */
static void install_styles(render_ui *ui)
{
    char *sheet = NULL;
    const char **names = NULL;
    int base = (int)sizeof BASE_RCSS - 1;
    asetlen(sheet, base);
    memcpy(sheet, BASE_RCSS, (size_t)base);
    for (int i = -1; i < acount(ui->views); i++) {
        ui_mount *m = mount_at(ui, i);
        bool known = !m;
        for (int j = 0; !known && j < acount(names); j++) {
            known = !strcmp(names[j], ui_mount_name(m));
        }
        if (known) {
            continue;
        }
        apush(names, ui_mount_name(m));
        const char *style = ui_mount_style(m);
        int start = acount(sheet);
        asetlen(sheet, start + (int)strlen(style));
        memcpy(sheet + start, style, strlen(style));
    }
    apush(sheet, '\0');
    rml_document_set_style_sheet(ui->doc, sheet);
    afree(sheet);
    afree(names);
}

/* Reports the first mount's error. */
static void collect_error(render_ui *ui)
{
    ui->error[0] = '\0';
    for (int i = -1; i < acount(ui->views) && !ui->error[0]; i++) {
        ui_mount *m = mount_at(ui, i);
        if (m) {
            snprintf(ui->error, sizeof ui->error, "%s", ui_mount_error(m));
        }
    }
}

void render_ui_update(render_ui *ui, const ui_world *w, int width, int height, double dt)
{
    assert(ui && w && w->ecs);
    ecs_world *world = w->ecs;
    ui->time += dt;
    send_activations(ui, w);
    if (width != ui->width || height != ui->height) {
        ui->width = width;
        ui->height = height;
        ui->scale = (float)height / UI_REFERENCE_HEIGHT;
        rml_backend_resize_pixels(ui->backend, width, height, ui->scale);
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
    close_unseen(ui, w->model);
    if (ui->restyle) {
        install_styles(ui);
        ui->restyle = false;
    }
    collect_error(ui);
    for (int i = 0; i < acount(ui->views); i++) {
        ui_mount *m = ui->views[i].mount;
        if (m && w->model) {
            ui_mount_apply(m, w->model, ui->views[i].entity);
        }
        if (m) {
            ui_mount_settings(m, w->settings);
        }
    }
    rml_backend_update(ui->backend, dt);
}

void render_ui_composite(render_ui *ui, CF_Canvas target)
{
    if ((!acount(ui->views) && !ui->overlay.mount) || ui->width <= 0 ||
        ui->height <= 0) {
        return;
    }
    rml_backend_render(ui->backend);
    cf_draw_push();
    rml_backend_present(ui->backend);
    cf_render_to(target, false);
    cf_draw_pop();
}
