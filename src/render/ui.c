#include "render/ui.h"

#include "assets/ui_document.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/file_watch.h"
#include "support/rcss.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <SDL3/SDL_video.h>
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
    bool autofocus;   /* installed, and its autofocus element not yet focused */
};

/* The names data-keys binds, for keys that are not letters or digits. */
static const struct
{
    rml_key key;
    const char *name;
} KEY_NAMES[] = {
    { RML_KEY_SPACE, "Space" },       { RML_KEY_RETURN, "Enter" },
    { RML_KEY_NUMPADENTER, "Enter" }, { RML_KEY_ESCAPE, "Escape" },
    { RML_KEY_TAB, "Tab" },           { RML_KEY_BACK, "Backspace" },
    { RML_KEY_DELETE, "Delete" },     { RML_KEY_UP, "Up" },
    { RML_KEY_DOWN, "Down" },         { RML_KEY_LEFT, "Left" },
    { RML_KEY_RIGHT, "Right" },       { RML_KEY_HOME, "Home" },
    { RML_KEY_END, "End" },           { RML_KEY_PRIOR, "PageUp" },
    { RML_KEY_NEXT, "PageDown" },
};

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
        rml_element_set_data_model(root, UI_DATA_MODEL);
        rml_element_add_event_listener(root, RML_EVENT_KEYDOWN, listener, false);
    }
    if (!m->installed && m->markup) {
        rml_backend_take_warning();
        rml_element_set_inner_rml(root, m->markup);
        const char *warning = rml_backend_take_warning();
        if (warning[0]) {
            snprintf(m->error, sizeof m->error, "ui/%s.yaml %s", m->name, warning);
        }
        m->installed = true;
        m->applied = 0;
        m->autofocus = true;
    }
    /* A host may keep the root hidden for a frame, and hidden elements refuse
       focus, so this repeats until the focus takes. */
    rml_element *first = m->autofocus
                           ? rml_element_query_selector(root, "[autofocus]")
                           : NULL;
    m->autofocus = first && !rml_element_focus(first, false);
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

/* The name data-keys uses for `key`, or NULL for keys it cannot bind. */
static const char *key_name(rml_key key, char *letter)
{
    if (key >= RML_KEY_A && key <= RML_KEY_Z) {
        letter[0] = (char)('A' + (key - RML_KEY_A));
        letter[1] = '\0';
        return letter;
    }
    if (key >= RML_KEY_0 && key <= RML_KEY_9) {
        letter[0] = (char)('0' + (key - RML_KEY_0));
        letter[1] = '\0';
        return letter;
    }
    for (size_t i = 0; i < sizeof KEY_NAMES / sizeof *KEY_NAMES; i++) {
        if (KEY_NAMES[i].key == key) {
            return KEY_NAMES[i].name;
        }
    }
    return NULL;
}

/* The event `bindings` ("Key:Event Key:Event") names for `key`, if any. */
static bool bound_event(const char *bindings, const char *key, char *event, size_t size)
{
    size_t length = strlen(key);
    for (const char *p = bindings; *p;) {
        while (*p == ' ') {
            p++;
        }
        const char *end = p + strcspn(p, " ");
        if (!strncmp(p, key, length) && p[length] == ':') {
            snprintf(event, size, "%.*s", (int)(end - p - (int)length - 1), p + length + 1);
            return event[0] != '\0';
        }
        p = end;
    }
    return false;
}

/* A key pressed inside a data-keys element sends the event bound to it from
 * the nearest such element, and goes no further. */
static void listen_key(ui_mount *m, const rml_event *event)
{
    char letter[2];
    const char *key = key_name(event->key, letter);
    for (rml_element *el = event->target;
         key && el && el != rml_element_parent(m->root);
         el = rml_element_parent(el)) {
        char bindings[256];
        ui_activation a = { 0 };
        if (rml_element_attribute(el, "data-keys", bindings, sizeof bindings) > 0 &&
            bound_event(bindings, key, a.event, sizeof a.event)) {
            snprintf(a.id, sizeof a.id, "%s", rml_element_id(el));
            apush(m->pending, a);
            rml_event_stop_propagation(event);
            return;
        }
    }
}

void ui_mount_listen(ui_mount *m, const rml_event *event)
{
    if (event->id == RML_EVENT_KEYDOWN) {
        listen_key(m, event);
    }
}

void ui_mount_emit(ui_mount *m, rml_element *element, const char *event)
{
    assert(ui_mount_holds(m, element));
    ui_activation a = { 0 };
    snprintf(a.event, sizeof a.event, "%s", event);
    snprintf(a.id, sizeof a.id, "%s", rml_element_id(element));
    char value[64];
    if (!strcmp(rml_element_tag_name(element), "input") &&
        rml_element_value(element, value, sizeof value) > 0) {
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

/* Focuses the match `step` places from the one holding focus, wrapping; with
 * none holding it, the first for a forward step and the last for a back one. */
static void move_focus(rml_element *root, rml_element **matches, int count, int step)
{
    rml_element *focus = rml_context_focus_element(rml_element_context(root));
    int at = -1;
    for (int i = 0; focus && i < count && at < 0; i++) {
        for (rml_element *el = focus; el && at < 0; el = rml_element_parent(el)) {
            at = el == matches[i] ? i : -1;
        }
    }
    int next = at < 0 ? (step > 0 ? 0 : count - 1)
                      : ((at + step) % count + count) % count;
    rml_element_focus(matches[next], true);
    rml_element_scroll_into_view(matches[next], RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
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
    if (e->click && count) {
        rml_element_click(matches[0]);
    }
    if (e->move_focus && count) {
        move_focus(root, matches, count, e->move_focus);
    }
    e->focus = e->click = false;
    e->move_focus = 0;
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

/* ---------- Data ---------- */

void ui_data_init(ui_data *d, rml_context *context, yyjson_mut_doc *doc, rml_emit_fn emit, void *user)
{
    *d = (ui_data){ .doc = doc ? doc : yyjson_mut_doc_new(NULL),
                    .save_seen = UINT64_MAX };
    yyjson_mut_val *root = yyjson_mut_doc_get_root(d->doc);
    if (!root) {
        root = yyjson_mut_obj(d->doc);
        yyjson_mut_doc_set_root(d->doc, root);
    }
    yyjson_mut_obj_add_val(d->doc, root, "save", yyjson_mut_obj(d->doc));
    d->model = rml_data_model_create(context, UI_DATA_MODEL, d->doc, emit, user);
}

/* A value a binding set, as saved text; NULL for what is not a value. */
static const char *saved_text(yyjson_mut_val *v, char *buffer, size_t size)
{
    switch (yyjson_mut_get_type(v)) {
        case YYJSON_TYPE_STR: return yyjson_mut_get_str(v);
        case YYJSON_TYPE_BOOL: return yyjson_mut_get_bool(v) ? "true" : "false";
        case YYJSON_TYPE_NUM:
            snprintf(buffer, size, "%g", yyjson_mut_get_num(v));
            return buffer;
        default: return NULL;
    }
}

void ui_data_set_text(yyjson_mut_doc *doc, yyjson_mut_val *value, const char *text)
{
    if (yyjson_mut_is_str(value) && !strcmp(yyjson_mut_get_str(value), text)) {
        return;
    }
    yyjson_mut_val *copy = yyjson_mut_strcpy(doc, text);
    yyjson_mut_set_strn(value, yyjson_mut_get_str(copy), yyjson_mut_get_len(copy));
}

/* Shows `save`'s keys in `shown`, changing only the values that differ, so
 * a long session does not grow the JSON; removed keys read as null. */
static void show_save(yyjson_mut_doc *doc, yyjson_mut_val *shown, const game_save *save)
{
    yyjson_mut_val *key;
    yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(shown);
    while ((key = yyjson_mut_obj_iter_next(&iter))) {
        if (!save || !game_save_get(save, yyjson_mut_get_str(key))) {
            yyjson_mut_set_null(yyjson_mut_obj_iter_get_val(key));
        }
    }
    for (int i = 0; save && i < game_save_count(save); i++) {
        const char *name = game_save_key(save, i), *text = game_save_get(save, name);
        yyjson_mut_val *value = yyjson_mut_obj_get(shown, name);
        if (!value) {
            value = yyjson_mut_null(doc);
            yyjson_mut_obj_add(shown, yyjson_mut_strcpy(doc, name), value);
        }
        bool flag = !strcmp(text, "true");
        if (flag || !strcmp(text, "false")) {
            yyjson_mut_set_bool(value, flag);
            continue;
        }
        ui_data_set_text(doc, value, text);
    }
}

void ui_data_sync(ui_data *d, game_save *save)
{
    yyjson_mut_val *shown = yyjson_mut_obj_get(yyjson_mut_doc_get_root(d->doc), "save");
    uint64_t writes = rml_data_model_writes(d->model);
    if (save && save == d->save && writes != d->writes_seen) {
        yyjson_mut_val *key;
        yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(shown);
        while ((key = yyjson_mut_obj_iter_next(&iter))) {
            char number[32];
            const char *text = saved_text(yyjson_mut_obj_iter_get_val(key), number, sizeof number);
            if (text) {
                game_save_set(save, yyjson_mut_get_str(key), text);
            }
        }
    }
    d->writes_seen = writes;
    uint64_t revision = save ? game_save_revision(save) : 0;
    if (save == d->save && revision == d->save_seen) {
        return;
    }
    d->save = save;
    d->save_seen = revision;
    show_save(d->doc, shown, save);
    rml_data_model_dirty(d->model, "save");
}

void ui_data_free(ui_data *d)
{
    rml_data_model_destroy(d->model);
    yyjson_mut_doc_free(d->doc);
    *d = (ui_data){ 0 };
}

/* ---------- Themes ---------- */

bool ui_theme_sync(ui_theme *t, const char *content_root, double time)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/ui/Theme.rcss", content_root);
    bool changed = !acount(t->watches);
    if (changed) {
        apush(t->watches, (file_watch){ 0 });
    }
    for (int i = 0; i < acount(t->watches); i++) {
        changed |= file_watch_changed(&t->watches[i], i ? t->watches[i].path : path, time);
    }
    if (!changed) {
        return false;
    }
    asetlen(t->watches, 1);
    t->error[0] = '\0';
    char *text = text_file_read(path);
    if (!text) {
        free(t->rcss);
        t->rcss = NULL;
        return true;
    }
    char directory[1024], error[200], **imports = NULL;
    snprintf(directory, sizeof directory, "%s/ui", content_root);
    char *rcss = rcss_expand(text, directory, &imports, error, sizeof error);
    free(text);
    /* Every file it reached, so fixing a broken import retries. */
    for (int i = 0; i < acount(imports); i++) {
        file_watch w = { 0 };
        file_watch_changed(&w, imports[i], time);
        apush(t->watches, w);
        free(imports[i]);
    }
    afree(imports);
    if (!rcss) {
        snprintf(t->error, sizeof t->error, "ui/Theme.rcss %s", error);
        return true;
    }
    free(t->rcss);
    t->rcss = rcss;
    return true;
}

void ui_theme_free(ui_theme *t)
{
    afree(t->watches);
    free(t->rcss);
    *t = (ui_theme){ 0 };
}

/* ---------- Game host ---------- */

/* The reference height at which one RCSS dp is one pixel. */
#define UI_REFERENCE_HEIGHT 720.0f
/* The overlay's z-index, above any authored GameUi order. */
#define OVERLAY_ORDER "1000000"

/* The game document's own style: the editor's face, and a pointer that passes
 * through to the game except over what a player activates. */
static const char
    BASE_RCSS[] = "body { width: 100%; height: 100%; font-family: Inter; "
                  "font-size: 16dp; color: #ffffff; pointer-events: none; }\n"
                  "[data-event-click], input, select, textarea { "
                  "pointer-events: auto; }\n";

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
    ui_theme theme;
    ui_data data;
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
static void on_emit(void *user, const rml_event *event, const char *name);

render_ui *render_ui_new(const char *content_root)
{
    render_ui *ui = xcalloc(1, sizeof *ui);
    ui->content_root = xstrdup(content_root);
    ui->backend = rml_backend_create(1, 1, 1);
    ui->listener = rml_listener_create(on_event, ui);
    ui->scale = 1;
    rml_context *context = rml_backend_context(ui->backend);
    ui_data_init(&ui->data, context, NULL, on_emit, ui);
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
    ui_data_free(&ui->data);
    rml_backend_destroy(ui->backend);
    rml_listener_destroy(ui->listener);
    ui_theme_free(&ui->theme);
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

/* The overlay's or view's mount holding `element`, or NULL. */
static ui_mount *mount_holding(render_ui *ui, rml_element *element)
{
    if (ui->overlay.mount && ui_mount_holds(ui->overlay.mount, element)) {
        return ui->overlay.mount;
    }
    for (int i = 0; i < acount(ui->views); i++) {
        if (ui->views[i].mount && ui_mount_holds(ui->views[i].mount, element)) {
            return ui->views[i].mount;
        }
    }
    return NULL;
}

static void on_event(void *user, const rml_event *event)
{
    ui_mount *m = mount_holding(user, event->target);
    if (m) {
        ui_mount_listen(m, event);
    }
}

static void on_emit(void *user, const rml_event *event, const char *name)
{
    ui_mount *m = mount_holding(user, event->current);
    if (m) {
        ui_mount_emit(m, event->current, name);
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

static void show_view(render_ui *ui, uint64_t entity, const GameUi *component, bool preview)
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
    rml_element_set_property(view->root, "display", component->hidden && !preview ? "none" : "block");
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

static void append(char **sheet, const char *text)
{
    int start = acount(*sheet);
    asetlen(*sheet, start + (int)strlen(text));
    memcpy(*sheet + start, text, strlen(text));
}

/* The base, then the project's theme, then each shown asset's RCSS once. */
static void install_styles(render_ui *ui)
{
    char *sheet = NULL;
    const char **names = NULL;
    append(&sheet, BASE_RCSS);
    append(&sheet, ui->theme.rcss ? ui->theme.rcss : "");
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
        append(&sheet, ui_mount_style(m));
    }
    apush(sheet, '\0');
    if (!rml_document_set_style_sheet(ui->doc, sheet)) {
        snprintf(ui->error, sizeof ui->error, "RmlUi could not read the game UI style sheets");
    }
    afree(sheet);
    afree(names);
}

/* Reports the theme's error, else the first mount's. */
static void collect_error(render_ui *ui)
{
    snprintf(ui->error, sizeof ui->error, "%s", ui->theme.error);
    for (int i = -1; i < acount(ui->views) && !ui->error[0]; i++) {
        ui_mount *m = mount_at(ui, i);
        if (m) {
            snprintf(ui->error, sizeof ui->error, "%s", ui_mount_error(m));
        }
    }
}

/* A saved "theme" setting picks the palette, as Studio's theme setting does:
 * dark, light, or the system's. */
static void apply_theme_mode(render_ui *ui, game_save *save)
{
    const char *mode = save ? game_save_get(save, "theme") : NULL;
    bool dark = mode && (!strcmp(mode, "dark") ||
                         (!strcmp(mode, "system") &&
                          SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK));
    rml_element_set_class(ui->doc, "dark", dark);
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
                show_view(ui, ecs_entity_to_u64(entity), GAME_GameUi_get_const(world, entity), w->preview);
            }
        }
        ecs_query_end(&query);
    }
    close_unseen(ui, w->model);
    apply_theme_mode(ui, w->save);
    ui_data_sync(&ui->data, w->save);
    ui->restyle |= ui_theme_sync(&ui->theme, ui->content_root, ui->time);
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
