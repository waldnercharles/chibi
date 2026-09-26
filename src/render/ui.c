#include "render/ui.h"

#include "assets/ui_document.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/file_watch.h"
#include "support/rcss.h"
#include "support/text_file.h"
#include "support/ui_style.h"
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
    file_watch *uses;     /* the widgets and stylesheets its asset uses */
    char *markup, *style; /* from the last valid asset */
    rcss_map map;         /* where each line of style came from */
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

void ui_mount_free(ui_mount *m)
{
    if (!m) {
        return;
    }
    afree(m->uses);
    afree(m->pending);
    free(m->content_root);
    free(m->name);
    free(m->markup);
    free(m->style);
    rcss_map_free(&m->map);
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

/* Records RCSS a host will install as `kind` `path`, so the host finds where
 * its lines came from (ui_style_map). */
static void record_style(const char *kind, const char *path, const char *rcss, const rcss_map *map)
{
    char sheet[1100];
    snprintf(sheet, sizeof sheet, "%s %s", kind, path);
    ui_style_set(sheet, rcss, map);
}

/* Whether any watched file changed; polls every one. */
static bool files_changed(file_watch *watches, double time)
{
    bool any = false;
    for (int i = 0; i < acount(watches); i++) {
        any |= file_watch_changed(&watches[i], watches[i].path, time);
    }
    return any;
}

/* Watches `paths` from now instead, taking them. */
static void watch_paths(file_watch **watches, char **paths, double time)
{
    asetlen(*watches, 0);
    for (int i = 0; i < acount(paths); i++) {
        file_watch w = { 0 };
        file_watch_changed(&w, paths[i], time);
        apush(*watches, w);
        free(paths[i]);
    }
    afree(paths);
}

/* The widgets' files, then the stylesheets, taking both lists. */
static char **used_files(const ui_mount *m, char **widgets, char **imports)
{
    for (int i = 0; i < acount(widgets); i++) {
        char path[1024];
        asset_path(m, widgets[i], path, sizeof path);
        apush(imports, xstrdup(path));
        free(widgets[i]);
    }
    afree(widgets);
    return imports;
}

/* Reads the asset and its widgets when one changed; returns true when it
 * replaced the markup and RCSS. An invalid edit keeps the last valid ones. */
static bool read_asset(ui_mount *m, double time)
{
    char path[1024], directory[1024];
    asset_path(m, m->name, path, sizeof path);
    bool asset = file_watch_changed(&m->watch, path, time);
    if (!files_changed(m->uses, time) && !asset) {
        return false;
    }
    snprintf(directory, sizeof directory, "%s/ui", m->content_root);
    char *text = text_file_read(path);
    char error[200] = "is missing", **imports = NULL, **widgets = NULL;
    ui_document source = { 0 };
    bool read = text &&
                ui_document_read(text, directory, &imports, &source, error, sizeof error);
    free(text);
    if (read && source.body) {
        snprintf(error, sizeof error, "has a <body>, which only a scene's root document has");
        read = false;
    }
    if (read) {
        read = ui_document_expand(&source, m->name, directory, true, &widgets, error, sizeof error);
    }
    watch_paths(&m->uses, used_files(m, widgets, imports), time);
    if (!read) {
        ui_document_free(&source);
        snprintf(m->error, sizeof m->error, "ui/%s.yaml %s", m->name, error);
        return false;
    }
    m->error[0] = '\0';
    free(m->markup);
    free(m->style);
    rcss_map_free(&m->map);
    m->markup = source.rml;
    m->style = source.rcss;
    m->map = source.map;
    m->installed = false;
    record_style("mount", path, m->style, &m->map);
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
    yyjson_mut_obj_add_bool(d->doc, yyjson_mut_obj_add_obj(d->doc, root, "system"), "dark", false);
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

/* Shows whether the system's theme is dark. */
static void show_system(ui_data *d)
{
    yyjson_mut_val *system = yyjson_mut_obj_get(yyjson_mut_doc_get_root(d->doc), "system");
    yyjson_mut_val *dark = yyjson_mut_obj_get(system, "dark");
    bool now = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
    if (yyjson_mut_get_bool(dark) != now) {
        yyjson_mut_set_bool(dark, now);
        rml_data_model_dirty(d->model, "system");
    }
}

void ui_data_sync(ui_data *d, game_save *save)
{
    show_system(d);
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

/* ---------- Pages ---------- */

bool ui_page_sync(ui_page *p, const char *content_root, const char *name, double time)
{
    bool renamed = strcmp(p->name, name) != 0;
    if (renamed) {
        ui_page_free(p);
        snprintf(p->name, sizeof p->name, "%s", name);
    }
    if (!name[0]) {
        return renamed;
    }
    char path[1024], directory[1024];
    snprintf(path, sizeof path, "%s/ui/%s.yaml", content_root, name);
    bool changed = file_watch_changed(&p->asset, path, time);
    if (!files_changed(p->imports, time) && !changed && !renamed) {
        return false;
    }
    snprintf(directory, sizeof directory, "%s/ui", content_root);
    char *text = text_file_read(path);
    char error[200] = "is missing", **imports = NULL;
    ui_document next = { 0 };
    bool read = text &&
                ui_document_read(text, directory, &imports, &next, error, sizeof error);
    free(text);
    /* Every file it reached, so fixing a broken import retries. */
    watch_paths(&p->imports, imports, time);
    if (!read) {
        ui_document_free(&next);
        snprintf(p->error, sizeof p->error, "ui/%s.yaml %s", name, error);
        return renamed;
    }
    rcss_map_own(&next.map, path);
    ui_document_free(&p->document);
    p->document = next;
    p->error[0] = '\0';
    record_style("page", path, p->document.rcss, &p->document.map);
    return true;
}

void ui_page_free(ui_page *p)
{
    afree(p->imports);
    ui_document_free(&p->document);
    *p = (ui_page){ 0 };
}

/* ---------- Game host ---------- */

/* The reference height at which one RCSS dp is one pixel. */
#define UI_REFERENCE_HEIGHT 720.0f
/* The overlay's z-index, above any authored GameUi order. */
#define OVERLAY_ORDER "1000000"

/* The game document's own style: the editor's face, a pointer that passes
 * through to the game except over what a player activates, and a slot that
 * fills what holds it. */
static const char
    BASE_RCSS[] = "body { width: 100%; height: 100%; font-family: Inter; "
                  "font-size: 16dp; color: #ffffff; pointer-events: none; }\n"
                  "slot { display: block; position: relative; width: 100%; "
                  "height: 100%; }\n"
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
    rml_element *doc;  /* the page */
    rml_element *slot; /* where the world's documents show on it */
    rml_listener *listener;
    char *content_root;
    ui_page page;
    ui_data data;
    ui_view *views;
    ui_view overlay; /* its entity is unused */
    char overlay_state[32];
    char overlay_event[128];
    char sheet[32]; /* the name its style sheet reports (ui_style.h) */
    bool restyle;   /* a mount's RCSS changed or a mount closed */
    double time;
    int width, height;
    float scale;
    char error[256];
};

static void on_event(void *user, const rml_event *event);
static void on_emit(void *user, const rml_event *event, const char *name);

/* Removes the view's root, which detaches the listener, then its mount. */
static void close_view(render_ui *ui, ui_view *view)
{
    if (view->root) {
        rml_element_remove_child(rml_element_parent(view->root), view->root);
        ui->restyle = true;
    }
    ui_mount_free(view->mount);
    *view = (ui_view){ .entity = view->entity };
}

/* The page body's attributes: the data model, then the page's own. */
static char *frame_body(const char *attributes)
{
    const char *model = "data-model=\"" UI_DATA_MODEL "\"";
    size_t size = strlen(model) + (attributes ? strlen(attributes) + 1 : 0) + 1;
    char *body = xmalloc(size);
    snprintf(body, size, "%s%s%s", model, attributes ? " " : "", attributes ? attributes : "");
    return body;
}

/* The page's document: its body attributes, with the data model, and its
 * markup marked with where it was written, as ui_document_compose writes
 * one. */
static char *page_text(const ui_page *page)
{
    const ui_document *d = &page->document;
    char *marked = d->rml ? ui_document_sources(d->rml, page->name, d->rml_line) : NULL;
    ui_document body = {
        .rml = marked ? marked : "",
        .rcss = "",
        .body = frame_body(d->body),
    };
    char *text = ui_document_compose(&body, "");
    free(body.body);
    free(marked);
    return text;
}

/* Shows the page anew: closes the world's documents, which open again in its
 * slot, and loads its markup; markup RmlUi rejects is the page's error. */
static void load_page(render_ui *ui)
{
    for (int i = 0; i < acount(ui->views); i++) {
        close_view(ui, &ui->views[i]);
    }
    asetlen(ui->views, 0);
    close_view(ui, &ui->overlay);
    if (ui->doc) {
        rml_document_close(ui->doc);
    }
    char *text = page_text(&ui->page);
    rml_context *context = rml_backend_context(ui->backend);
    rml_backend_take_warning();
    ui->doc = rml_context_load_document_from_memory(context, text, "game-ui");
    const char *warning = rml_backend_take_warning();
    free(text);
    if (warning[0] && !ui->page.error[0]) {
        snprintf(
            ui->page.error,
            sizeof ui->page.error,
            "ui/%s.yaml %s",
            ui->page.name,
            warning
        );
    }
    assert(ui->doc);
    rml_document_show(ui->doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_AUTO);
    ui->slot = rml_element_query_selector(ui->doc, "slot");
    ui->slot = ui->slot ? ui->slot : ui->doc;
    ui->restyle = true;
}

render_ui *render_ui_new(const char *content_root)
{
    static int hosts;
    render_ui *ui = xcalloc(1, sizeof *ui);
    snprintf(ui->sheet, sizeof ui->sheet, "game-ui-%d", ++hosts);
    ui->content_root = xstrdup(content_root);
    ui->backend = rml_backend_create(1, 1, 1);
    ui->listener = rml_listener_create(on_event, ui);
    ui->scale = 1;
    ui_data_init(&ui->data, rml_backend_context(ui->backend), NULL, on_emit, ui);
    load_page(ui);
    return ui;
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
    ui_page_free(&ui->page);
    ui_style_set(ui->sheet, NULL, NULL);
    free(ui->content_root);
    free(ui);
}

const char *render_ui_error(const render_ui *ui)
{
    return ui->error;
}

rml_context *render_ui_context(const render_ui *ui)
{
    return rml_backend_context(ui->backend);
}

/* Opens a mount of `name` in a new full-view root stacked at `order`. */
static void open_view(render_ui *ui, ui_view *view, const char *name, const char *order)
{
    static const char *const LAYOUT[][2] = {
        { "position", "absolute" }, { "left", "0" },      { "top", "0" },
        { "width", "100%" },        { "height", "100%" },
    };
    view->mount = ui_mount_new(ui->content_root, name);
    view->root = rml_element_create_child(ui->slot, "div", NULL);
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

static void show_view(render_ui *ui, uint64_t entity, const GameUi *component, bool revealed)
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
    rml_element_set_property(view->root, "display", component->hidden && !revealed ? "none" : "block");
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

/* The base, then the page's RCSS, then each shown asset's RCSS once, with
 * where each line came from recorded under the host's sheet name. */
static void install_styles(render_ui *ui)
{
    char *sheet = NULL;
    const char **names = NULL;
    rcss_map map = { 0 };
    const ui_document *page = &ui->page.document;
    rcss_join(&sheet, &map, BASE_RCSS, NULL);
    rcss_join(&sheet, &map, page->rcss ? page->rcss : "", &page->map);
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
        rcss_join(&sheet, &map, ui_mount_style(m), &m->map);
    }
    apush(sheet, '\0');
    if (!rml_document_set_named_style_sheet(ui->doc, ui->sheet, sheet)) {
        snprintf(ui->error, sizeof ui->error, "RmlUi could not read the game UI style sheets");
    }
    ui_style_set(ui->sheet, sheet, &map);
    rcss_map_free(&map);
    afree(sheet);
    afree(names);
}

/* Reports the page's error, else the first mount's. */
static void collect_error(render_ui *ui)
{
    snprintf(ui->error, sizeof ui->error, "%s", ui->page.error);
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
    if (ui_page_sync(&ui->page, ui->content_root, w->page ? w->page : "", ui->time)) {
        load_page(ui);
    }
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
            uint64_t id = ecs_entity_to_u64(entity);
            if (!ecs_entity_is_retiring(world, entity)) {
                show_view(ui, id, GAME_GameUi_get_const(world, entity), w->preview || id == w->revealed);
            }
        }
        ecs_query_end(&query);
    }
    close_unseen(ui, w->model);
    ui_data_sync(&ui->data, w->save);
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
    if ((!acount(ui->views) && !ui->overlay.mount && !ui->page.name[0]) ||
        ui->width <= 0 || ui->height <= 0) {
        return;
    }
    rml_backend_render(ui->backend);
    cf_draw_push();
    rml_backend_present(ui->backend);
    cf_render_to(target, false);
    cf_draw_pop();
}
