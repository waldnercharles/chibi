#include "render/ui.h"

#include "runtime/component_catalog.h"
#include "runtime/ui/documents.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/rcss_map.h"
#include "support/ui_style.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"
#include "types/text.h"
#include "ui/list.h"
#include "ui/popover.h"

#include <SDL3/SDL_video.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Mounts ---------- */

/* A list a mount windows (ui/list.h): an element marked data-list-rows,
   such as the List widget's scroller, and the window it last sent. */
typedef struct mount_list
{
    rml_element_ref *element;
    ui_list_state state;
    int first, length;
} mount_list;

struct ui_mount
{
    char *content_root;
    char *name;
    render_ui_loader loader;
    void *source;
    uint64_t prepared_revision;
    bool prepared_only, prepared_loaded;
    char *markup, *style; /* from the last valid asset */
    rcss_map map;         /* where each line of style came from */
    char error[256];
    rml_element_ref *root; /* expires when a conditional host is destroyed */
    bool installed;        /* root holds the current markup */
    ui_activation *pending;
    rml_element_ref *taken; /* the anchor of the activation last taken */
    uint64_t applied;       /* the model revision already applied */
    bool autofocus; /* installed, and its autofocus element not yet focused */
    mount_list *lists;
};

static void forget_lists(ui_mount *m)
{
    for (int i = 0; i < acount(m->lists); i++) {
        rml_element_ref_destroy(m->lists[i].element);
    }
    asetlen(m->lists, 0);
}

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
    if (m->source) {
        m->loader.close(m->source);
    }
    for (int i = 0; i < acount(m->pending); i++) {
        rml_element_ref_destroy(m->pending[i].anchor);
    }
    afree(m->pending);
    rml_element_ref_destroy(m->taken);
    rml_element_ref_destroy(m->root);
    forget_lists(m);
    afree(m->lists);
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

static bool same_loader(render_ui_loader a, render_ui_loader b)
{
    return a.open == b.open && a.read == b.read && a.close == b.close;
}
void ui_mount_set_loader(ui_mount *m, const render_ui_loader *loader)
{
    render_ui_loader next = loader ? *loader : (render_ui_loader){ 0 };
    assert((!next.open && !next.read && !next.close) || (next.open && next.read && next.close));
    if (same_loader(m->loader, next)) {
        return;
    }
    if (m->source) {
        m->loader.close(m->source);
    }
    m->loader = next;
    m->source = next.open ? next.open(m->content_root, m->name, false) : NULL;
    m->prepared_loaded = false;
}
/* Reads the asset and its widgets when one changed; returns true when it
 * replaced the markup and RCSS. An invalid edit keeps the last valid ones. */
static bool read_asset(ui_mount *m, double time)
{
    if (m->prepared_only || !m->loader.open || ui_documents_active()) {
        uint64_t generation = ui_documents_revision();
        if (m->prepared_loaded && m->prepared_revision == generation) {
            return false;
        }
        const prepared_ui_document *record = ui_documents_active()
                                               ? ui_documents_find(ui_documents_current(), m->name)
                                               : NULL;
        if (!record || record->body) {
            snprintf(
                m->error,
                sizeof m->error,
                "Prepared UI widget is missing or has page attributes: %.120s",
                m->name
            );
            return false;
        }
        free(m->markup);
        free(m->style);
        rcss_map_free(&m->map);
        m->markup = xstrdup(record->markup);
        m->style = xstrdup(record->style);
        m->error[0] = 0;
        m->installed = false;
        m->prepared_revision = generation;
        m->prepared_loaded = true;
        return true;
    }
    if (m->prepared_loaded) {
        if (m->source) {
            m->loader.close(m->source);
        }
        m->source = m->loader.open(m->content_root, m->name, false);
        m->prepared_loaded = false;
    }
    ui_document next = { 0 };
    int status = m->loader.read(m->source, time, &next, m->error, sizeof m->error);
    if (status <= 0) {
        return false;
    }
    free(m->markup);
    free(m->style);
    rcss_map_free(&m->map);
    m->markup = next.rml;
    m->style = next.rcss;
    m->map = next.map;
    free(next.body);
    m->installed = false;
    return true;
}

bool ui_mount_sync(ui_mount *m, rml_element *root, rml_listener *listener, double time)
{
    assert(root && listener);
    bool restyled = read_asset(m, time);
    char mounted[256] = "";
    bool same_mount = rml_element_attribute(root, "data-ui", mounted, sizeof mounted) &&
                      !strcmp(mounted, m->name);
    if (root != ui_mount_root(m) || !same_mount) {
        rml_element_ref_destroy(m->root);
        m->root = rml_element_ref_create(root);
        m->installed = false;
        /* Binding moves the root, which binds its children's views again;
           another panel's evaluated markup would leave views RmlUi adds but
           never removes, so it goes first. */
        rml_element_set_inner_rml(root, "");
        rml_element_set_attribute(root, "data-ui", m->name);
        rml_element_set_data_model(root, UI_DATA_MODEL);
        /* Authored key bindings precede text fields, whose default handler
           consumes Escape and Enter before they can bubble to the host. */
        rml_element_add_event_listener(root, RML_EVENT_KEYDOWN, listener, true);
    }
    if (!m->installed && m->markup) {
        rml_backend_take_warning();
        rml_element_set_inner_rml(root, m->markup);
        const char *warning = rml_backend_take_warning();
        if (warning[0]) {
            snprintf(
                m->error,
                sizeof m->error,
                "ui/%s"
                ".rml"
                " %s",
                m->name,
                warning
            );
        }
        m->installed = true;
        m->applied = 0;
        m->autofocus = true;
        forget_lists(m);
    }
    /* A host may keep the root hidden for a frame, and hidden elements refuse
       focus, so this repeats until the focus takes. */
    rml_element *first = m->autofocus
                           ? rml_element_query_selector(root, "[autofocus]")
                           : NULL;
    m->autofocus = first && !rml_element_focus(first, false);
    return restyled;
}

/* Drop a host explicitly when its owner switches panels. Destruction also
 * expires the root reference, so reused element addresses remain unmounted. */
void ui_mount_detach(ui_mount *m)
{
    if (!m) {
        return;
    }
    rml_element_ref_destroy(m->root);
    m->root = NULL;
    m->installed = false;
}

rml_element *ui_mount_root(const ui_mount *m)
{
    return m->root ? rml_element_ref_get(m->root) : NULL;
}

bool ui_mount_holds(const ui_mount *m, rml_element *element)
{
    rml_element *root = ui_mount_root(m);
    for (; element; element = rml_element_parent(element)) {
        if (element == root) {
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

/* Text and select controls keep editing/navigation keys unless the control
 * explicitly binds them. Radio/checkbox/range controls still use authored
 * ancestor navigation, such as Studio's SidebarNav. */
static bool owns_field_keys(rml_element *element)
{
    const char *tag = rml_element_tag_name(element);
    if (!strcmp(tag, "textarea") || !strcmp(tag, "select")) {
        return true;
    }
    if (strcmp(tag, "input")) {
        return false;
    }
    char type[32] = "";
    rml_element_attribute(element, "type", type, sizeof type);
    return strcmp(type, "radio") && strcmp(type, "checkbox") &&
           strcmp(type, "range") && strcmp(type, "button") &&
           strcmp(type, "submit") && strcmp(type, "reset");
}

/* An activation `element` sent through `event`. */
static ui_activation activation(rml_element *element, const rml_event *event)
{
    ui_activation a = { .modifiers = event->modifiers,
                        .button = event->button,
                        .x = event->mouse_x,
                        .y = event->mouse_y,
                        .anchor = rml_element_ref_create(element) };
    snprintf(a.id, sizeof a.id, "%s", rml_element_id(element));
    return a;
}

/* A key pressed inside a data-keys element sends the event bound to it from
 * the nearest such element, and goes no further. */
static void listen_key(ui_mount *m, const rml_event *event)
{
    char letter[2];
    const char *key = key_name(event->key, letter);
    rml_element *root = ui_mount_root(m);
    assert(root);
    for (rml_element *el = event->target; key && el && el != rml_element_parent(root);
         el = rml_element_parent(el)) {
        char bindings[256], name[128];
        if (rml_element_attribute(el, "data-keys", bindings, sizeof bindings) > 0 &&
            bound_event(bindings, key, name, sizeof name)) {
            ui_activation a = activation(el, event);
            snprintf(a.event, sizeof a.event, "%s", name);
            apush(m->pending, a);
            rml_event_stop_propagation(event);
            return;
        }
        if (owns_field_keys(el) && (event->key != RML_KEY_ESCAPE ||
                                    !strcmp(rml_element_tag_name(el), "select"))) {
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

bool ui_mount_area_key(ui_mount *m, const char *key)
{
    rml_element *root = ui_mount_root(m);
    rml_element *el = m->installed && root
                        ? rml_element_query_selector(root, "[data-area-keys]")
                        : NULL;
    char bindings[256], name[128];
    if (!el ||
        rml_element_attribute(el, "data-area-keys", bindings, sizeof bindings) <= 0 ||
        !bound_event(bindings, key, name, sizeof name)) {
        return false;
    }
    ui_activation a = { .anchor = rml_element_ref_create(el) };
    snprintf(a.event, sizeof a.event, "%s", name);
    snprintf(a.id, sizeof a.id, "%s", rml_element_id(el));
    apush(m->pending, a);
    return true;
}

void ui_mount_emit(ui_mount *m, const rml_event *event, const char *name, int argc, const char *const *args)
{
    rml_element *element = event->current;
    assert(ui_mount_holds(m, element));
    ui_activation a = activation(element, event);
    snprintf(a.event, sizeof a.event, "%s", name);
    a.argc = argc < UI_ACTIVATION_ARGS ? argc : UI_ACTIVATION_ARGS;
    for (int i = 0; i < a.argc; i++) {
        snprintf(a.args[i], sizeof a.args[i], "%s", args[i]);
    }
    char input[64];
    const char *value = argc ? args[0] : NULL;
    if (!value && !strcmp(rml_element_tag_name(element), "input") &&
        rml_element_value(element, input, sizeof input) > 0) {
        value = input;
    }
    if (value) {
        char *end;
        a.value = strtof(value, &end);
        a.has_value = end != value;
    }
    apush(m->pending, a);
}

bool ui_mount_pending(const ui_mount *m)
{
    return acount(m->pending) > 0;
}

bool ui_mount_take(ui_mount *m, ui_activation *out)
{
    int count = acount(m->pending);
    if (!count) {
        return false;
    }
    rml_element_ref_destroy(m->taken);
    *out = m->pending[0];
    m->taken = out->anchor;
    memmove(m->pending, m->pending + 1, (size_t)(count - 1) * sizeof *m->pending);
    asetlen(m->pending, count - 1);
    return true;
}

/* An emit argument read as a payload value of `type`, or false for a type
   a UI event cannot carry. */
static bool argument_value(arena *storage, const char *text, pin_value *value)
{
    switch (value->type) {
        case PIN_STR:
            return !pin_value_text(storage, value, text, strlen(text));
        case PIN_NAME: value->str = text; return true;
        case PIN_BOOL:
            value->b = !strcmp(text, "true") || strtod(text, NULL) != 0;
            return true;
        case PIN_I32: value->i = (int)strtod(text, NULL); return true;
        case PIN_F32: value->f = strtof(text, NULL); return true;
        case PIN_F64: value->d = strtod(text, NULL); return true;
        default: return false;
    }
}

/* A payload value the DOM event gives, by its declared name; false for
   one emit's arguments give. */
static bool event_value(
    const ui_activation *a,
    const render_ui_event_field *v,
    unsigned primary,
    float pointer[2],
    pin_value *value
)
{
    static const struct
    {
        const char *name;
        unsigned bits;
    } MODIFIERS[] = { { "Shift", RML_KM_SHIFT }, { "Alt", RML_KM_ALT }, { "Mod", 0 } };
    for (size_t i = 0;
         v->type.type == PIN_BOOL && i < sizeof MODIFIERS / sizeof *MODIFIERS;
         i++) {
        if (!strcmp(v->name, MODIFIERS[i].name)) {
            value->b = (a->modifiers &
                        (MODIFIERS[i].bits ? MODIFIERS[i].bits : primary)) != 0;
            return true;
        }
    }
    if (v->type.type == PIN_STRUCT && v->type.type_name &&
        !strcmp(v->type.type_name, "vec2") && !strcmp(v->name, "Pointer")) {
        pointer[0] = a->x;
        pointer[1] = a->y;
        value->ptr = pointer;
        return true;
    }
    return false;
}

/* The payload `a` takes under `event`, the event asset declaring it (NULL
 * declares none), in its order: bool Shift, Alt and Mod (`primary` holds
 * Mod's modifier bits) and CF_V2 Pointer come from the DOM event, as a web
 * handler reads event.shiftKey and event.clientX; each other value is emit's
 * next argument read as its type (strings use dispatch storage). Returns how
 * many values it wrote to `out`, or -1 for a payload a UI event cannot carry or
 * more than `capacity`. */
static int ui_activation_payload(
    arena *storage,
    const ui_activation *a,
    const render_ui_event_field *fields,
    int count,
    unsigned primary,
    float pointer[2],
    pin_value *out,
    int capacity
)
{
    int argument = 0;
    if (count > capacity) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        const render_ui_event_field *v = &fields[i];
        out[i] = v->type;
        if (v->type.container != PIN_CONTAINER_SINGLE) {
            return -1;
        }
        if (event_value(a, v, primary, pointer, &out[i])) {
            continue;
        }
        const char *text = argument < a->argc ? a->args[argument] : "";
        argument++;
        if (!argument_value(storage, text, &out[i])) {
            return -1;
        }
    }
    return count;
}

/* Values an event's payload may hold; a UI event declares a few. */
#define PAYLOAD_MAX 16

/* A status no dispatch returns: the payload could not be carried. */
#define UNCARRIED INT_MIN

/* Runs `a` and returns the dispatch's status, or UNCARRIED. */
static int activation_send(const render_ui_host *host, ecs_entity entity, const ui_activation *a, unsigned primary)
{
    render_ui_event_field fields[PAYLOAD_MAX];
    int count = host->payload
                  ? host->payload(host->context, a->event, fields, PAYLOAD_MAX)
                  : 0;
    if (count < 0 || count > PAYLOAD_MAX) {
        return UNCARRIED;
    }
    pin_value values[PAYLOAD_MAX];
    float pointer[2];
    arena storage = { 0 };
    count = ui_activation_payload(&storage, a, fields, count, primary, pointer, values, PAYLOAD_MAX);
    if (count < 0) {
        arena_free(&storage);
        return UNCARRIED;
    }
    int status = host->dispatch(
        host->context,
        ecs_entity_to_u64(entity),
        a->event,
        (execution_arguments){ .values = values, .count = count }
    );
    arena_free(&storage);
    return status;
}

bool ui_activation_send(const render_ui_host *host, ecs_entity entity, const ui_activation *a, unsigned primary)
{
    return !host || !host->dispatch ||
           activation_send(host, entity, a, primary) != UNCARRIED;
}

static void escape_text(char *out, size_t size, const char *text)
{
    size_t n = 0;
    for (const char *c = text; *c && n + 6 < size; c++) {
        const char *entity = *c == '<' ? "&lt;"
                           : *c == '>' ? "&gt;"
                           : *c == '&' ? "&amp;"
                           : *c == '"' ? "&quot;"
                                       : NULL;
        if (entity) {
            n += (size_t)snprintf(out + n, size - n, "%s", entity);
            continue;
        }
        out[n++] = *c;
    }
    out[n] = '\0';
}

static void apply_to(rml_element *element, const ui_element *e)
{
    if (e->text) {
        char text[1024];
        escape_text(text, sizeof text, e->text);
        rml_element_set_inner_rml(element, text);
    }
    for (int i = 0; i < acount(e->classes); i++) {
        rml_element_set_class(element, e->classes[i].name, e->classes[i].on);
    }
    for (int i = 0; i < acount(e->styles); i++) {
        const ui_property_override *style = &e->styles[i];
        if (style->value) {
            rml_element_set_property(element, style->property, style->value);
        } else {
            rml_element_remove_property(element, style->property);
        }
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
static void apply_element(rml_element *root, ui_element *e)
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
    if (e->popover && count) {
        popover_show(
            matches[0],
            &(popover_anchor){ .side = POPOVER_POINT,
                               .box = { e->popover_x, e->popover_y, 0, 0 } }
        );
    }
    for (int i = 0; e->reveal >= 0 && i < count; i++) {
        char row[16];
        snprintf(row, sizeof row, "%d", e->reveal);
        rml_element_set_attribute(matches[i], "data-list-reveal", row);
    }
    e->focus = e->click = e->popover = false;
    e->move_focus = 0;
    e->reveal = -1;
    free(matches);
}

void ui_mount_apply(ui_mount *m, ui *model, uint64_t entity)
{
    rml_element *root = ui_mount_root(m);
    if (!m->installed || !root) {
        return;
    }
    for (int i = 0; i < ui_element_count(model); i++) {
        ui_element *e = ui_element_at(model, i);
        if (e->entity == entity && e->revision > m->applied) {
            apply_element(root, e);
        }
    }
    m->applied = ui_revision(model);
}

/* What the mount keeps for list `element`, kept from its first window. */
static mount_list *list_of(ui_mount *m, rml_element *element)
{
    for (int i = 0; i < acount(m->lists); i++) {
        if (rml_element_ref_get(m->lists[i].element) == element) {
            return &m->lists[i];
        }
    }
    apush(
        m->lists,
        ((mount_list){ .element = rml_element_ref_create(element),
                       .state = UI_LIST_STATE_INIT,
                       .first = -1,
                       .length = -1 })
    );
    return &alast(m->lists);
}

/* Windows each list marked data-list-rows (ui_list_window): sizes its spacer
   for that many rows, scrolls the row a data-list-reveal names into view
   once, as Reveal UI List Row asks, and sends `window` (ev.first, ev.count)
   when the rows to show change, as Virtuoso's rangeChanged does, so its
   document's behaviors fill the window in the same frame. */
static void window_lists(ui_mount *m, rml_element *root)
{
    rml_element *found[8];
    int count = rml_element_query_selector_all(root, "[data-list-rows]", found, 8);
    for (int i = 0; i < count && i < 8; i++) {
        char text[32] = "";
        rml_element_attribute(found[i], "data-list-rows", text, sizeof text);
        ui_list list = { .count = atoi(text), .reveal = -1 };
        if (rml_element_attribute(found[i], "data-list-reveal", text, sizeof text) > 0) {
            list.reveal = atoi(text);
            rml_element_remove_attribute(found[i], "data-list-reveal");
        }
        list.count = list.count > 0 ? list.count : 0;
        mount_list *at = list_of(m, found[i]);
        int first, length = ui_list_window(found[i], &list, &at->state, &first);
        if (first == at->first && length == at->length) {
            continue;
        }
        at->first = first;
        at->length = length;
        char start[16], rows[16];
        snprintf(start, sizeof start, "%d", first);
        snprintf(rows, sizeof rows, "%d", length);
        rml_element_dispatch_texts(
            found[i],
            "window",
            2,
            (const char *const[]){ "first", "count" },
            (const char *const[]){ start, rows }
        );
    }
}

void ui_mount_measure(ui_mount *m, ui *model, uint64_t entity)
{
    rml_element *root = ui_mount_root(m);
    if (!m->installed || !root) {
        return;
    }
    window_lists(m, root);
    rml_element *found[16];
    int count = rml_element_query_selector_all(root, "[id][data-event-scroll]", found, 16);
    float dp = rml_context_dp_ratio(rml_element_context(root));
    for (int i = 0; i < count && i < 16; i++) {
        char selector[160];
        snprintf(selector, sizeof selector, "#%s", rml_element_id(found[i]));
        ui_measure(
            model,
            entity,
            selector,
            rml_element_scroll_top(found[i]) / dp,
            rml_element_client_height(found[i]) / dp,
            rml_element_scroll_height(found[i]) / dp
        );
    }
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
            snprintf(buffer, size, "%.17g", yyjson_mut_get_num(v));
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

bool ui_data_put_text(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *name, const char *text)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(object, name);
    if (yyjson_mut_is_str(value) && !strcmp(yyjson_mut_get_str(value), text)) {
        return false;
    }
    ui_data_set_text(doc, value, text);
    return true;
}

bool ui_data_put_bool(yyjson_mut_val *object, const char *name, bool on)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(object, name);
    if (yyjson_mut_is_bool(value) && yyjson_mut_get_bool(value) == on) {
        return false;
    }
    yyjson_mut_set_bool(value, on);
    return true;
}

bool ui_data_put_int(yyjson_mut_val *object, const char *name, int number)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(object, name);
    if (yyjson_mut_is_int(value) && yyjson_mut_get_int(value) == number) {
        return false;
    }
    yyjson_mut_set_int(value, number);
    return true;
}

bool ui_data_put_number(yyjson_mut_val *object, const char *name, double number)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(object, name);
    if (yyjson_mut_is_num(value) && yyjson_mut_get_num(value) == number) {
        return false;
    }
    yyjson_mut_set_real(value, number);
    return true;
}

static bool saved_number(const char *text, double *number)
{
    yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
    if (!doc) {
        return false;
    }
    const yyjson_val *value = yyjson_doc_get_root(doc);
    bool valid = yyjson_is_num(value);
    if (valid) {
        *number = yyjson_get_num(value);
        valid = isfinite(*number);
    }
    yyjson_doc_free(doc);
    return valid;
}

/* Shows `save`'s keys in `shown`, changing only the values that differ, so
 * a long session does not grow the JSON; removed keys read as null. */
static void show_save(yyjson_mut_doc *doc, yyjson_mut_val *shown, const save *save)
{
    yyjson_mut_val *key;
    yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(shown);
    while ((key = yyjson_mut_obj_iter_next(&iter))) {
        if (!save || !save_get(save, yyjson_mut_get_str(key))) {
            yyjson_mut_set_null(yyjson_mut_obj_iter_get_val(key));
        }
    }
    for (int i = 0; save && i < save_count(save); i++) {
        const char *name = save_key(save, i), *text = save_get(save, name);
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
        double number;
        if (saved_number(text, &number)) {
            yyjson_mut_set_real(value, number);
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

/* The key `shown` holds `value` under, or NULL. */
static const char *shown_key(yyjson_mut_val *shown, const yyjson_mut_val *value)
{
    yyjson_mut_val *key;
    yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(shown);
    while ((key = yyjson_mut_obj_iter_next(&iter))) {
        if (yyjson_mut_obj_iter_get_val(key) == value) {
            return yyjson_mut_get_str(key);
        }
    }
    return NULL;
}

/* Saves in `save` the shown keys bindings changed, and only those: a key the
 * save changed since it was shown keeps the save's value. */
static void save_written(ui_data *d, yyjson_mut_val *shown, save *save)
{
    yyjson_mut_val *value;
    while (rml_data_model_take_written(d->model, &value)) {
        char number[32];
        const char *key = save && save == d->save ? shown_key(shown, value) : NULL;
        const char *text = key ? saved_text(value, number, sizeof number) : NULL;
        if (text) {
            save_set(save, key, text);
        }
    }
}

void ui_data_sync(ui_data *d, save *save)
{
    show_system(d);
    yyjson_mut_val *shown = yyjson_mut_obj_get(yyjson_mut_doc_get_root(d->doc), "save");
    save_written(d, shown, save);
    uint64_t revision = save ? save_revision(save) : 0;
    if (save == d->save && revision == d->save_seen) {
        return;
    }
    d->save = save;
    d->save_seen = revision;
    show_save(d->doc, shown, save);
    rml_data_model_dirty(d->model, "save");
}

/* `entity`'s variable: "e<entity>". */
static void entity_key(char out[32], uint64_t entity)
{
    snprintf(out, 32, "e%llu", (unsigned long long)entity);
}

/* The object holding `entity`'s data, a variable of its own, so what one
   entity's behaviors set dirties only the views bound to it (RmlUi dirties
   by variable); made and bound as needed. */
static yyjson_mut_val *entity_data(ui_data *d, uint64_t entity)
{
    yyjson_mut_val *root = yyjson_mut_doc_get_root(d->doc);
    char key[32];
    entity_key(key, entity);
    yyjson_mut_val *data = yyjson_mut_obj_get(root, key);
    if (!data) {
        data = yyjson_mut_obj(d->doc);
        yyjson_mut_obj_add(root, yyjson_mut_strcpy(d->doc, key), data);
        rml_data_model_bind(d->model, key, data);
    }
    return data;
}

void ui_data_bind_self(ui_data *d, rml_element *root, uint64_t entity)
{
    char alias[32], bound[32] = "";
    entity_key(alias, entity);
    rml_element_attribute(root, "data-alias-self", bound, sizeof bound);
    if (strcmp(bound, alias)) {
        entity_data(d, entity);
        rml_element_set_attribute(root, "data-alias-self", alias);
    }
}

/* Makes `shown` hold `value`, reusing what it holds: members and items keep
   their places, so bound views stay bound, and only text that changed or
   items a list gained take new memory. A member `value` lacks reads as
   null. */
static void assign(yyjson_mut_doc *doc, yyjson_mut_val *shown, av_value *value)
{
    switch (av_get_type(value)) {
        case AV_BOOL: yyjson_mut_set_bool(shown, av_get_bool(value)); return;
        case AV_SINT:
        case AV_UINT:
        case AV_REAL: yyjson_mut_set_real(shown, av_get_num(value)); return;
        case AV_STRING: ui_data_set_text(doc, shown, av_get_str(value)); return;
        case AV_OBJECT: {
            if (!yyjson_mut_is_obj(shown)) {
                yyjson_mut_set_obj(shown);
            }
            yyjson_mut_val *key;
            yyjson_mut_obj_iter kept = yyjson_mut_obj_iter_with(shown);
            while ((key = yyjson_mut_obj_iter_next(&kept))) {
                if (!av_obj_get(value, yyjson_mut_get_str(key))) {
                    yyjson_mut_set_null(yyjson_mut_obj_iter_get_val(key));
                }
            }
            size_t i, max;
            av_value *name, *member;
            av_obj_foreach(value, i, max, name, member)
            {
                yyjson_mut_val *target = yyjson_mut_obj_get(shown, av_get_str(name));
                if (!target) {
                    target = yyjson_mut_null(doc);
                    yyjson_mut_obj_add(shown, yyjson_mut_strcpy(doc, av_get_str(name)), target);
                }
                assign(doc, target, member);
            }
            return;
        }
        case AV_ARRAY: {
            if (!yyjson_mut_is_arr(shown)) {
                yyjson_mut_set_arr(shown);
            }
            size_t count = av_arr_size(value), held = yyjson_mut_arr_size(shown);
            if (held > count) {
                yyjson_mut_arr_remove_range(shown, count, held - count);
            }
            yyjson_mut_arr_iter items = yyjson_mut_arr_iter_with(shown);
            av_arr_iter values = av_arr_iter_with(value);
            av_value *item;
            while ((item = av_arr_iter_next(&values))) {
                yyjson_mut_val *target = yyjson_mut_arr_iter_next(&items);
                if (!target) {
                    target = yyjson_mut_null(doc);
                    yyjson_mut_arr_append(shown, target);
                }
                assign(doc, target, item);
            }
            return;
        }
        default: yyjson_mut_set_null(shown); return;
    }
}

/* The value at `path` ("items.2.name") under `data`, made as needed: a word
   names an object's member, digits a list's item, and lists grow with
   nulls. */
static yyjson_mut_val *data_at(yyjson_mut_doc *doc, yyjson_mut_val *data, const char *path)
{
    yyjson_mut_val *at = data;
    for (const char *segment = path; segment;) {
        size_t length = strcspn(segment, ".");
        char name[128];
        snprintf(name, sizeof name, "%.*s", (int)length, segment);
        segment = segment[length] ? segment + length + 1 : NULL;
        if (isdigit((unsigned char)name[0])) {
            size_t index = strtoul(name, NULL, 10);
            if (!yyjson_mut_is_arr(at)) {
                yyjson_mut_set_arr(at);
            }
            while (yyjson_mut_arr_size(at) <= index) {
                yyjson_mut_arr_append(at, yyjson_mut_null(doc));
            }
            at = yyjson_mut_arr_get(at, index);
            continue;
        }
        if (!yyjson_mut_is_obj(at)) {
            yyjson_mut_set_obj(at);
        }
        yyjson_mut_val *member = yyjson_mut_obj_get(at, name);
        if (!member) {
            member = yyjson_mut_null(doc);
            yyjson_mut_obj_add(at, yyjson_mut_strcpy(doc, name), member);
        }
        at = member;
    }
    return at;
}

static int by_revision(const void *a, const void *b)
{
    uint64_t x = (*(const ui_datum *const *)a)->revision;
    uint64_t y = (*(const ui_datum *const *)b)->revision;
    return x < y ? -1 : x > y;
}

void ui_data_show_entities(ui_data *d, const ui *model)
{
    /* In the order behaviors set them, so a list set after one of its rows
       replaces it, and a row set after its list changes it. */
    const ui_datum **changed = NULL;
    for (int i = 0; model && i < ui_data_count(model); i++) {
        const ui_datum *datum = ui_data_at(model, i);
        if (datum->revision > d->data_seen) {
            apush(changed, datum);
        }
    }
    if (!acount(changed)) {
        return;
    }
    asort(changed, by_revision);
    uint64_t *dirty = NULL;
    for (int i = 0; i < acount(changed); i++) {
        const ui_datum *datum = changed[i];
        yyjson_mut_val
            *shown = data_at(d->doc, entity_data(d, datum->entity), datum->name);
        assign(d->doc, shown, datum->value);
        d->data_seen = datum->revision;
        bool listed = false;
        for (int j = 0; j < acount(dirty); j++) {
            listed |= dirty[j] == datum->entity;
        }
        if (!listed) {
            apush(dirty, datum->entity);
        }
    }
    afree(changed);
    for (int i = 0; i < acount(dirty); i++) {
        char key[32];
        entity_key(key, dirty[i]);
        rml_data_model_dirty(d->model, key);
    }
    afree(dirty);
}

/* The entity's variable stays bound, emptied, as RmlUi cannot unbind one. */
void ui_data_forget_entity(ui_data *d, uint64_t entity)
{
    char key[32];
    entity_key(key, entity);
    yyjson_mut_val *data = yyjson_mut_obj_get(yyjson_mut_doc_get_root(d->doc), key);
    if (data) {
        yyjson_mut_obj_clear(data);
        rml_data_model_dirty(d->model, key);
    }
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
    bool renamed = strcmp(p->name, name) != 0 || !p->content_root ||
                   strcmp(p->content_root, content_root);
    if (renamed) {
        bool prepared_only = p->prepared_only;
        render_ui_loader loader = p->loader;
        ui_page_free(p);
        p->prepared_only = prepared_only;
        p->loader = loader;
        snprintf(p->name, sizeof p->name, "%s", name);
        p->content_root = xstrdup(content_root);
    }
    if (!name[0]) {
        return renamed;
    }
    if (p->prepared_only || !p->loader.open || ui_documents_active()) {
        uint64_t generation = ui_documents_revision();
        if (p->prepared_loaded && p->prepared_revision == generation && !renamed) {
            return false;
        }
        const prepared_ui_document *record = ui_documents_active()
                                               ? ui_documents_find(ui_documents_current(), name)
                                               : NULL;
        if (!record) {
            snprintf(p->error, sizeof p->error, "Prepared UI page is missing: %.120s", name);
            return renamed;
        }
        ui_document next = { .rml = xstrdup(record->markup),
                             .rcss = xstrdup(record->style),
                             .body = record->body ? xstrdup(record->body) : NULL,
                             .rml_line = 1 };
        ui_document_free(&p->document);
        p->document = next;
        p->error[0] = 0;
        p->prepared_revision = generation;
        p->prepared_loaded = true;
        return true;
    }
    if (p->prepared_loaded) {
        if (p->source) {
            p->loader.close(p->source);
        }
        p->source = NULL;
        p->prepared_loaded = false;
    }
    if (!p->source) {
        p->source = p->loader.open(content_root, name, true);
    }
    ui_document next = { 0 };
    int status = p->loader.read(p->source, time, &next, p->error, sizeof p->error);
    if (status <= 0) {
        return renamed;
    }
    ui_document_free(&p->document);
    p->document = next;
    return true;
}

void ui_page_free(ui_page *p)
{
    if (p->source) {
        p->loader.close(p->source);
    }
    ui_document_free(&p->document);
    free(p->content_root);
    *p = (ui_page){ 0 };
}

void ui_page_set_loader(ui_page *p, const render_ui_loader *loader)
{
    render_ui_loader next = loader ? *loader : (render_ui_loader){ 0 };
    assert((!next.open && !next.read && !next.close) || (next.open && next.read && next.close));
    if (same_loader(p->loader, next)) {
        return;
    }
    if (p->source) {
        p->loader.close(p->source);
    }
    p->source = NULL;
    p->loader = next;
    p->prepared_loaded = false;
}

/* ---------- Game host ---------- */

/* The reference height at which one RCSS dp is one pixel. */
#define UI_REFERENCE_HEIGHT 720.0f
/* The overlay's z-index, above any authored Ui order. */
#define OVERLAY_ORDER "1000000"

/* The game document's own style: the editor's face, a pointer that passes
 * through to the game except over what a player activates, and a slot that
 * fills what holds it. */
static const char BASE_RCSS[] =
    "body { display: block; width: 100%; height: 100%; font-family: Inter; "
    "font-size: 16dp; color: #ffffff; pointer-events: none; }\n"
    "slot { display: block; position: relative; width: 100%; "
    "height: 100%; }\n"
    "scrollbarvertical { width: 8dp; margin-left: 0dp; }\n"
    "button:not([disabled]) { tab-index: auto; }\n"
    "scrollbarvertical, scrollbarhorizontal, scrollbarcorner, "
    "scrollbarvertical slidertrack, scrollbarhorizontal slidertrack, "
    "scrollbarvertical sliderbar, scrollbarhorizontal sliderbar { "
    "pointer-events: auto; }\n"
    "scrollbarvertical slidertrack { background-color: transparent; }\n"
    "scrollbarvertical sliderbar { margin: 2dp; border-radius: 3dp; "
    "background-color: var(--scrollbar, #777); }\n"
    "scrollbarvertical sliderbar:hover { "
    "background-color: var(--scrollbar-hover, #aaa); }\n"
    "scrollbarhorizontal { height: 8dp; }\n"
    "scrollbarhorizontal sliderbar { margin: 2dp; border-radius: 3dp; "
    "background-color: var(--scrollbar, #777); }\n"
    "scrollbarhorizontal sliderbar:hover { "
    "background-color: var(--scrollbar-hover, #aaa); }\n"
    "scrollbarcorner { background-color: transparent; }\n"
    "[data-event-click], input, select, textarea, [popovertarget], [popover] { "
    "pointer-events: auto; }\n"
    /* ui/popover.c: a closed popover lays out and draws nothing, and a shown
       one waits unseen until it is placed. */
    "[popover]:not([data-open]) { display: none; }\n"
    "[popover][data-unplaced] { visibility: hidden; }\n";

/* One Ui entity's mount, or the host's overlay. */
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
    bool prepared_only;
    render_ui_loader loader;
    ui_page page;
    ui_data data;
    ui_view *views;
    ui_view overlay;         /* its entity is unused */
    char overlay_name[128];  /* the asset the host asked for, "" for none */
    char overlay_wanted[32]; /* the state it asked for */
    bool overlay_shown;      /* whether it asked for a state, or to hide */
    char overlay_state[32];  /* the state the overlay's root shows */
    char overlay_event[128];
    char sheet[32]; /* the name its style sheet reports (ui_style.h) */
    bool restyle;   /* a mount's RCSS changed or a mount closed */
    double time;
    int width, height;
    float scale;
    float reference; /* the dp the canvas's height spans */
    char error[256];
    char failure[256]; /* an activation the last update could not send */
};

static void on_event(void *user, const rml_event *event);
static void on_emit(void *user, const rml_event *event, const char *name, int argc, const char *const *args);

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
static char *frame_body(const char *attributes, const char *page)
{
    const char *model = "data-model=\"" UI_DATA_MODEL "\"";
    char scoped[1024] = "";
    if (page) {
        char escaped[768];
        escape_text(escaped, sizeof escaped, page);
        snprintf(scoped, sizeof scoped, " data-ui=\"%s\"", escaped);
    }
    size_t size = strlen(model) + strlen(scoped) +
                  (attributes ? strlen(attributes) + 1 : 0) + 1;
    char *body = xmalloc(size);
    snprintf(body, size, "%s%s%s%s", model, scoped, attributes ? " " : "", attributes ? attributes : "");
    return body;
}

/* The page's document: its body attributes, with the data model, and its
 * markup marked with where it was written, as ui_document_compose writes
 * one. */
static char *page_text(const ui_page *page)
{
    const ui_document *d = &page->document;
    ui_document body = {
        .rml = d->rml ? d->rml : "",
        .rcss = "",
        .body = frame_body(
            d->body,
            (page->prepared_only || !page->loader.open || ui_documents_active()) ? page->name : NULL
        ),
    };
    char *text = ui_document_compose(&body, "");
    free(body.body);
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
            "ui/%s"
            ".rml"
            " %s",
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

static render_ui *new_ui(const char *content_root, yyjson_mut_doc *data, bool prepared_only)
{
    static int hosts;
    render_ui *ui = xcalloc(1, sizeof *ui);
    snprintf(ui->sheet, sizeof ui->sheet, "game-ui-%d", ++hosts);
    ui->content_root = xstrdup(content_root);
    ui->prepared_only = prepared_only;
    ui->page.prepared_only = prepared_only;
    ui->backend = rml_backend_create(1, 1, 1);
    if (ui->backend) {
        rml_backend_require_resident_images(
            ui->backend,
            prepared_only || !ui->loader.open || ui_documents_active()
        );
    }
    ui->listener = rml_listener_create(on_event, ui);
    ui->scale = 1;
    ui->reference = UI_REFERENCE_HEIGHT;
    ui_data_init(&ui->data, rml_backend_context(ui->backend), data, on_emit, ui);
    load_page(ui);
    return ui;
}

render_ui *render_ui_new(const char *content_root, yyjson_mut_doc *data)
{
    return new_ui(content_root, data, false);
}
render_ui *render_ui_new_prepared(yyjson_mut_doc *data)
{
    return new_ui("", data, true);
}

void render_ui_set_loader(render_ui *ui, const render_ui_loader *loader)
{
    assert(ui && !ui->prepared_only);
    ui->loader = loader ? *loader : (render_ui_loader){ 0 };
    ui_page_set_loader(&ui->page, loader);
    for (int i = 0; i < acount(ui->views); i++) {
        if (ui->views[i].mount) {
            ui_mount_set_loader(ui->views[i].mount, loader);
        }
    }
    rml_backend_require_resident_images(ui->backend, !ui->loader.open || ui_documents_active());
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

ui_data *render_ui_data(render_ui *ui)
{
    return &ui->data;
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
    view->mount->prepared_only = ui->prepared_only;
    ui_mount_set_loader(view->mount, &ui->loader);
    view->root = rml_element_create_child(ui->slot, "div", NULL);
    ui_data_bind_self(&ui->data, view->root, view->entity);
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

static void on_emit(void *user, const rml_event *event, const char *name, int argc, const char *const *args)
{
    ui_mount *m = mount_holding(user, event->current);
    if (m) {
        ui_mount_emit(m, event, name, argc, args);
    }
}

void render_ui_set_overlay(render_ui *ui, const char *name, const char *state)
{
    snprintf(ui->overlay_name, sizeof ui->overlay_name, "%s", name);
    snprintf(ui->overlay_wanted, sizeof ui->overlay_wanted, "%s", state ? state : "");
    ui->overlay_shown = state != NULL;
}

/* Shows the overlay the host asked for, once the page is loaded: RmlUi keeps
 * the data views of elements removed before its next update, and warns about
 * each at every removal after, so an overlay installed before a page load
 * closed it would leave them behind. */
static void show_overlay(render_ui *ui)
{
    ui_view *view = &ui->overlay;
    if (!ui->overlay_name[0]) {
        return;
    }
    if (view->mount && strcmp(ui_mount_name(view->mount), ui->overlay_name) != 0) {
        close_view(ui, view);
    }
    if (!view->mount) {
        open_view(ui, view, ui->overlay_name, OVERLAY_ORDER);
        ui->overlay_state[0] = '\0';
    }
    if (ui->overlay_shown) {
        ui->restyle |= ui_mount_sync(view->mount, view->root, ui->listener, ui->time);
    }
    rml_element_set_property(view->root, "display", ui->overlay_shown ? "block" : "none");
    if (ui->overlay_shown && strcmp(ui->overlay_wanted, ui->overlay_state) != 0) {
        snprintf(ui->overlay_state, sizeof ui->overlay_state, "%s", ui->overlay_wanted);
        rml_element_set_class_names(view->root, ui->overlay_wanted);
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

bool render_ui_wants_text(const render_ui *ui)
{
    rml_element *focus = rml_context_focus_element(rml_backend_context(ui->backend));
    if (!focus) {
        return false;
    }
    const char *tag = rml_element_tag_name(focus);
    char type[16] = "";
    rml_element_attribute(focus, "type", type, sizeof type);
    bool field = !type[0] || !strcmp(type, "text") || !strcmp(type, "password");
    return !strcmp(tag, "textarea") || (!strcmp(tag, "input") && field);
}

/* Mod's modifier bits in a game, whose hosts trade Command and Control on a
 * Mac as they do for keys (viewport.c). */
#define PRIMARY RML_KM_CTRL

/* Sends each view's activations to its entity's behaviors now, an input's
 * value first, so what they set shows in this update. They wait while a
 * spawn initializes, as the engine holds the events it queues. */
static void send_activations(render_ui *renderer, const ui_world *world)
{
    ui *model = world->model;
    const render_ui_host *host = &world->host;
    renderer->failure[0] = '\0';
    for (int i = 0; i < acount(renderer->views); i++) {
        ui_view *view = &renderer->views[i];
        ui_activation a;
        if (view->mount && model) {
            ui_mount_measure(view->mount, model, view->entity);
        }
        while (view->mount &&
               !(host->initializing && host->initializing(host->context)) &&
               ui_mount_take(view->mount, &a)) {
            if (model && a.has_value && a.id[0]) {
                char selector[160];
                snprintf(selector, sizeof selector, "#%s", a.id);
                ui_set_value(model, view->entity, selector, a.value);
            }
            if (!host->dispatch) {
                continue;
            }
            int status = activation_send(host, ecs_entity_from_u64(view->entity), &a, PRIMARY);
            const char *why = host->failure ? host->failure(host->context) : "";
            if (status == UNCARRIED) {
                snprintf(
                    renderer->failure,
                    sizeof renderer->failure,
                    "%s declares a payload a UI event cannot carry",
                    a.event
                );
            } else if (status < 0 && why[0]) {
                snprintf(renderer->failure, sizeof renderer->failure, "%s failed: %s", a.event, why);
            } else if (status < 0) {
                snprintf(renderer->failure, sizeof renderer->failure, "%s failed", a.event);
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

static void show_view(render_ui *ui, uint64_t entity, const Ui *component, bool shown)
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
    rml_element_set_property(view->root, "display", shown ? "block" : "none");
}

/* Drops the views whose entity no longer shows a Ui. */
static void close_unseen(render_ui *renderer, ui *model)
{
    int kept = 0;
    for (int i = 0; i < acount(renderer->views); i++) {
        if (renderer->views[i].seen) {
            renderer->views[kept++] = renderer->views[i];
            continue;
        }
        if (model) {
            ui_forget(model, renderer->views[i].entity);
        }
        ui_data_forget_entity(&renderer->data, renderer->views[i].entity);
        close_view(renderer, &renderer->views[i]);
    }
    asetlen(renderer->views, kept);
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

/* Reports the page's error, else the first mount's, else an activation that
 * could not be sent. */
static void collect_error(render_ui *ui)
{
    snprintf(ui->error, sizeof ui->error, "%s", ui->page.error);
    for (int i = -1; i < acount(ui->views) && !ui->error[0]; i++) {
        ui_mount *m = mount_at(ui, i);
        if (m) {
            snprintf(ui->error, sizeof ui->error, "%s", ui_mount_error(m));
        }
    }
    if (!ui->error[0]) {
        snprintf(ui->error, sizeof ui->error, "%s", ui->failure);
    }
}

void render_ui_update(render_ui *ui, const ui_world *w, int width, int height, double dt)
{
    assert(ui && w && w->ecs);
    ecs_world *world = w->ecs;
    ui->time += dt;
    send_activations(ui, w);
    rml_backend_require_resident_images(
        ui->backend,
        ui->prepared_only || !ui->loader.open || ui_documents_active()
    );
    if (ui_page_sync(&ui->page, ui->content_root, w->page ? w->page : "", ui->time)) {
        load_page(ui);
    }
    if (width != ui->width || height != ui->height ||
        ui->scale != (float)height / ui->reference) {
        ui->width = width;
        ui->height = height;
        ui->scale = (float)height / ui->reference;
        rml_backend_resize_pixels(ui->backend, width, height, ui->scale);
    }
    show_overlay(ui);
    for (int i = 0; i < acount(ui->views); i++) {
        ui->views[i].seen = false;
    }
    ecs_component_id component = COMPONENT_Ui.id;
    ecs_query query;
    if (ecs_world_component(world, component) &&
        ecs_query_begin(world, &query, &component, 1) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            uint64_t id = ecs_entity_to_u64(entity);
            if (!ecs_entity_is_retiring(world, entity)) {
                const Ui *component = Ui_get_const(world, entity);
                bool shown = (!component->hidden || w->preview || id == w->revealed) &&
                             !render_skip_has(w->hidden, entity);
                show_view(ui, id, component, shown);
            }
        }
        ecs_query_end(&query);
    }
    close_unseen(ui, w->model);
    ui_data_sync(&ui->data, w->save);
    ui_data_show_entities(&ui->data, w->model);
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
    render_ui_composite_region(ui, target, (const float[4]){ 0, 0, 1, 1 });
}

void render_ui_composite_region(render_ui *ui, CF_Canvas target, const float region[4])
{
    if ((!acount(ui->views) && !ui->overlay.mount && !ui->page.name[0]) ||
        ui->width <= 0 || ui->height <= 0) {
        return;
    }
    rml_backend_render(ui->backend);
    cf_draw_push();
    rml_backend_present_region(ui->backend, region);
    cf_render_to(target, false);
    cf_draw_pop();
}

/* Grows `bounds` by the boxes of what `element` shows; a box as wide as the
   canvas is a wrapper, so its children's count instead. */
static void content_bounds(const render_ui *ui, rml_element *element, float bounds[4])
{
    rml_vec2 at = rml_element_absolute_offset(element, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(element, RML_BOX_BORDER);
    if (size.x <= 0 || size.y <= 0 || !rml_element_is_visible(element, false)) {
        return;
    }
    if (size.x < ui->width - 1 || !rml_element_child_count(element)) {
        bounds[0] = fminf(bounds[0], at.x);
        bounds[1] = fminf(bounds[1], at.y);
        bounds[2] = fmaxf(bounds[2], at.x + size.x);
        bounds[3] = fmaxf(bounds[3], at.y + size.y);
        return;
    }
    for (int i = 0; i < rml_element_child_count(element); i++) {
        content_bounds(ui, rml_element_child(element, i), bounds);
    }
}

void render_ui_set_reference_height(render_ui *ui, float dp)
{
    ui->reference = dp > 0 ? dp : UI_REFERENCE_HEIGHT;
}

bool render_ui_overlay_bounds(const render_ui *ui, float bounds[4])
{
    float found[4] = { INFINITY, INFINITY, -INFINITY, -INFINITY };
    for (int i = 0; ui->overlay.root && i < rml_element_child_count(ui->overlay.root);
         i++) {
        content_bounds(ui, rml_element_child(ui->overlay.root, i), found);
    }
    if (found[0] >= found[2] || found[1] >= found[3]) {
        return false;
    }
    float size[4] = { (float)ui->width,
                      (float)ui->height,
                      (float)ui->width,
                      (float)ui->height };
    for (int i = 0; i < 4; i++) {
        bounds[i] = found[i] / size[i];
    }
    return true;
}

/* ---------- UI wiring (workstream K) ---------- */

bool render_ui_take_overlay_activation(render_ui *ui, ui_activation *out)
{
    return ui->overlay.mount && ui_mount_take(ui->overlay.mount, out);
}
