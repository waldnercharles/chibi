#include "render/ui.h"

#include "runtime/component_catalog.h"
#include "runtime/ui/documents.h"
#include "runtime/ui_chord.h"
#include "support/array.h"
#include "support/rcss_map.h"
#include "support/ui_style.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"
#include "ui/input_keys.h"
#include "ui/list.h"
#include "ui/popover.h"

#include <SDL3/SDL_video.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#define PRIMARY RML_KM_META
#else
#define PRIMARY RML_KM_CTRL
#endif

/* Embedded documents form an ownership boundary for entity effects and
 * host shortcuts, while ordinary authored descendants keep their aliases. */
static bool mount_owns(rml_element *root, rml_element *element)
{
    for (rml_element *at = element; at; at = rml_element_parent(at)) {
        if (at == root) {
            return true;
        }
        if (rml_element_has_attribute(at, "data-ui-embedded-root")) {
            return false;
        }
    }
    return false;
}

static rml_element *first_owned(rml_element *root, const char *selector)
{
    int count = rml_element_query_selector_all(root, selector, NULL, 0);
    rml_element **found = count ? xmalloc((size_t)count * sizeof *found) : NULL;
    rml_element_query_selector_all(root, selector, found, count);
    rml_element *first = NULL;
    for (int i = 0; i < count && !first; i++) {
        if (mount_owns(root, found[i])) {
            first = found[i];
        }
    }
    free(found);
    return first;
}

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
    ui_activation taken; /* borrowed by the caller until next take */
    size_t activation_bytes;
    uint64_t applied; /* the model revision already applied */
    bool autofocus;   /* installed, and its autofocus element not yet focused */
    mount_list *lists;
    rml_element_ref *pointer_element;
    float pointer_x, pointer_y;
    int pointer_button, released_button;
};

static void forget_lists(ui_mount *m)
{
    for (int i = 0; i < acount(m->lists); i++) {
        rml_element_ref_destroy(m->lists[i].element);
    }
    asetlen(m->lists, 0);
}

const char *ui_activation_argument(const ui_activation *a, int index)
{
    assert(index >= 0 && index < a->argc);
    return a->full_args[index] ? a->full_args[index] : a->args[index];
}
const char *ui_activation_text(const ui_activation *a)
{
    return a->full_text ? a->full_text : a->text;
}
static void release_activation_strings(ui_activation *a)
{
    char *owned[UI_ACTIVATION_ARGS + 1];
    int count = 0;
    for (int i = 0; i <= UI_ACTIVATION_ARGS; i++) {
        char *value = i < UI_ACTIVATION_ARGS ? a->full_args[i] : a->full_text;
        bool alias = !value;
        for (int j = 0; j < count; j++) {
            alias |= value == owned[j];
        }
        if (!alias) {
            owned[count++] = value;
        }
    }
    for (int i = 0; i < count; i++) {
        free(owned[i]);
    }
    memset(a->full_args, 0, sizeof a->full_args);
    a->full_text = NULL;
}
void ui_activation_clear(ui_activation *a)
{
    release_activation_strings(a);
    rml_element_ref_destroy(a->anchor);
    memset(a, 0, sizeof *a);
}
bool ui_activation_capture(ui_activation *a, int argc, const char *const *args, const char *text)
{
    if (argc < 0 || argc > UI_ACTIVATION_ARGS) {
        return false;
    }
    size_t lengths[UI_ACTIVATION_ARGS], bytes = sizeof *a;
    for (int i = 0; i < argc; i++) {
        lengths[i] = strnlen(args[i], UI_ACTIVATION_BYTES_MAX + 1u);
        bool alias = false;
        for (int j = 0; j < i; j++) {
            alias |= lengths[i] == lengths[j] &&
                     !memcmp(args[i], args[j], lengths[i]);
        }
        if (!alias) {
            if (lengths[i] >= UI_ACTIVATION_BYTES_MAX ||
                lengths[i] + 1 > UI_ACTIVATION_BYTES_MAX - bytes) {
                return false;
            }
            bytes += lengths[i] + 1;
        }
    }
    size_t length = text ? strnlen(text, UI_ACTIVATION_BYTES_MAX + 1u) : 0;
    int text_alias = -1;
    for (int i = 0; text && i < argc; i++) {
        if (length == lengths[i] && !memcmp(text, args[i], length)) {
            text_alias = i;
            break;
        }
    }
    if (text && text_alias < 0) {
        if (length >= UI_ACTIVATION_BYTES_MAX ||
            length + 1 > UI_ACTIVATION_BYTES_MAX - bytes) {
            return false;
        }
        bytes += length + 1;
    }
    ui_activation next = { .argc = argc, .has_text = text != NULL, .payload_bytes = bytes };
    for (int i = 0; i < argc; i++) {
        snprintf(next.args[i], sizeof next.args[i], "%s", args[i]);
        if (lengths[i] < sizeof next.args[i]) {
            continue;
        }
        for (int j = 0; j < i; j++) {
            if (lengths[i] == lengths[j] && !memcmp(args[i], args[j], lengths[i])) {
                next.full_args[i] = next.full_args[j];
                break;
            }
        }
        if (!next.full_args[i]) {
            next.full_args[i] = xstrdup(args[i]);
        }
    }
    if (text) {
        snprintf(next.text, sizeof next.text, "%s", text);
        if (length >= sizeof next.text) {
            next.full_text = text_alias >= 0 ? next.full_args[text_alias]
                                             : xstrdup(text);
        }
    }
    release_activation_strings(a);
    a->argc = next.argc;
    a->has_text = next.has_text;
    a->payload_bytes = next.payload_bytes;
    memcpy(a->args, next.args, sizeof a->args);
    memcpy(a->full_args, next.full_args, sizeof a->full_args);
    memcpy(a->text, next.text, sizeof a->text);
    a->full_text = next.full_text;
    return true;
}
static void clear_mount_activations(ui_mount *m)
{
    for (int i = 0; i < acount(m->pending); i++) {
        ui_activation_clear(&m->pending[i]);
    }
    asetlen(m->pending, 0);
    ui_activation_clear(&m->taken);
    m->activation_bytes = 0;
}
static bool queue_activation(ui_mount *m, ui_activation *a)
{
    enum
    {
        MAX_EVENTS = 1024,
        MAX_BYTES = 16 * 1024 * 1024
    };
    if (!a->payload_bytes) {
        a->payload_bytes = sizeof *a;
    }
    if (acount(m->pending) + (m->taken.payload_bytes != 0) >= MAX_EVENTS ||
        a->payload_bytes > MAX_BYTES - m->activation_bytes) {
        snprintf(m->error, sizeof m->error, "UI activation queue exceeds 1024 events or 16 MiB; event refused");
        ui_activation_clear(a);
        return false;
    }
    m->activation_bytes += a->payload_bytes;
    apush(m->pending, *a);
    return true;
}

ui_mount *ui_mount_new(const char *content_root, const char *name)
{
    ui_mount *m = xcalloc(1, sizeof *m);
    m->content_root = xstrdup(content_root);
    m->name = xstrdup(name);
    m->pointer_button = m->released_button = -1;
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
    clear_mount_activations(m);
    afree(m->pending);
    rml_element_ref_destroy(m->pointer_element);
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
    return a.open == b.open && a.read == b.read && a.close == b.close &&
           a.context == b.context && a.open_context == b.open_context &&
           a.resident_images == b.resident_images;
}
static bool prepared_loader(render_ui_loader loader, bool prepared_only)
{
    return prepared_only ||
           (!loader.open_context && (!loader.open || ui_documents_active()));
}
static void *open_source(render_ui_loader loader, const char *root, const char *name, bool page)
{
    return loader.open_context ? loader.open_context(loader.context, root, name, page)
                               : loader.open(root, name, page);
}
#ifndef NDEBUG
static bool valid_loader(render_ui_loader loader)
{
    bool open = loader.open || loader.open_context;
    return !(
        (loader.open && loader.open_context) ||
        (open != (loader.read != NULL)) || (open != (loader.close != NULL))
    );
}
#endif
void ui_mount_set_loader(ui_mount *m, const render_ui_loader *loader)
{
    render_ui_loader next = loader ? *loader : (render_ui_loader){ 0 };
    assert(valid_loader(next));
    if (same_loader(m->loader, next)) {
        return;
    }
    if (m->source) {
        m->loader.close(m->source);
    }
    m->loader = next;
    m->source = (next.open || next.open_context)
                  ? open_source(next, m->content_root, m->name, false)
                  : NULL;
    m->prepared_loaded = false;
}
/* Reads the asset and its widgets when one changed; returns true when it
 * replaced the markup and RCSS. An invalid edit keeps the last valid ones. */
static bool read_asset(ui_mount *m, double time)
{
    if (prepared_loader(m->loader, m->prepared_only)) {
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
        m->source = open_source(m->loader, m->content_root, m->name, false);
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
        rml_element_add_event_listener(root, RML_EVENT_MOUSEDOWN, listener, true);
        rml_element_add_event_listener(root, RML_EVENT_MOUSEUP, listener, true);
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
    rml_element *first = m->autofocus ? first_owned(root, "[autofocus]") : NULL;
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
    clear_mount_activations(m);
    rml_element_ref_destroy(m->root);
    m->root = NULL;
    rml_element_ref_destroy(m->pointer_element);
    m->pointer_element = NULL;
    m->pointer_button = m->released_button = -1;
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

bool ui_key_binding(
    const char *bindings,
    const char *key,
    unsigned modifiers,
    unsigned primary,
    char *event,
    size_t size
)
{
    return ui_chord_binding(bindings, key, modifiers, primary, event, size);
}

/* Attribute text is bounded explicitly; never run a truncated chord list. */
static bool element_key_binding(
    rml_element *element,
    const char *attribute,
    const char *key,
    unsigned modifiers,
    char *name,
    size_t size
)
{
    enum
    {
        MAX_BINDING_BYTES = 16384
    };
    char local[256];
    size_t length = rml_element_attribute(element, attribute, local, sizeof local);
    if (!length || length > MAX_BINDING_BYTES) {
        return false;
    }
    char *bindings = local;
    if (length >= sizeof local) {
        bindings = xmalloc(length + 1);
        rml_element_attribute(element, attribute, bindings, length + 1);
    }
    bool found = ui_key_binding(bindings, key, modifiers, PRIMARY, name, size);
    if (bindings != local) {
        free(bindings);
    }
    return found;
}

/* Text and select controls keep editing/navigation keys unless the control
 * explicitly binds them. Radio/checkbox/range controls still use authored
 * ancestor navigation, such as Studio's SidebarNav. */
static bool owns_field_keys(rml_element *element)
{
    char ownership[2];
    if (rml_element_attribute(element, "data-own-keys", ownership, sizeof ownership)) {
        return true;
    }
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
static bool pointer_event(rml_event_id id)
{
    return id == RML_EVENT_MOUSEDOWN || id == RML_EVENT_MOUSEUP ||
           id == RML_EVENT_MOUSEMOVE || id == RML_EVENT_MOUSESCROLL ||
           id == RML_EVENT_CLICK || id == RML_EVENT_DBLCLICK ||
           id == RML_EVENT_DRAGSTART || id == RML_EVENT_DRAG ||
           id == RML_EVENT_DRAGMOVE || id == RML_EVENT_DRAGEND;
}

static ui_activation activation(ui_mount *m, rml_element *element, const rml_event *event)
{
    rml_vec2 origin = rml_element_absolute_offset(element, RML_BOX_BORDER);
    ui_activation a = { .modifiers = event->modifiers,
                        .button = event->button,
                        .x = event->mouse_x,
                        .y = event->mouse_y,
                        .local_x = event->mouse_x - origin.x,
                        .local_y = event->mouse_y - origin.y,
                        .wheel_x = event->wheel_x,
                        .wheel_y = event->wheel_y,
                        .canceled = event->canceled,
                        .anchor = rml_element_ref_create(element) };
    if (pointer_event(event->id)) {
        if (a.button < 0) {
            a.button = event->id == RML_EVENT_DRAGEND ? m->released_button
                                                      : m->pointer_button;
        }
        if (event->id != RML_EVENT_MOUSEDOWN && m->pointer_element &&
            rml_element_ref_get(m->pointer_element) == element) {
            a.delta_x = a.x - m->pointer_x;
            a.delta_y = a.y - m->pointer_y;
        }
        rml_element_ref_destroy(m->pointer_element);
        m->pointer_element = rml_element_ref_create(element);
        m->pointer_x = a.x;
        m->pointer_y = a.y;
    }
    char key[4];
    const char *name = ui_key_name(event->key, key);
    if (name && (event->id == RML_EVENT_KEYDOWN || event->id == RML_EVENT_KEYUP)) {
        snprintf(a.key, sizeof a.key, "%s", name);
    }
    snprintf(a.id, sizeof a.id, "%s", rml_element_id(element));
    return a;
}

/* A key pressed inside a data-keys element sends the event bound to it from
 * the nearest such element, and goes no further. */
static void listen_key(ui_mount *m, const rml_event *event)
{
    char letter[4];
    const char *key = ui_key_name(event->key, letter);
    rml_element *root = ui_mount_root(m);
    assert(root);
    for (rml_element *el = event->target; key && el && el != rml_element_parent(root);
         el = rml_element_parent(el)) {
        char name[128];
        if (element_key_binding(el, "data-keys", key, event->modifiers, name, sizeof name)) {
            ui_activation a = activation(m, el, event);
            snprintf(a.event, sizeof a.event, "%s", name);
            queue_activation(m, &a);
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
    if (event->id == RML_EVENT_MOUSEDOWN) {
        m->pointer_button = event->button;
        m->released_button = -1;
    }
    if (event->id == RML_EVENT_MOUSEUP) {
        m->released_button = event->button;
        m->pointer_button = -1;
    }
    if (event->id == RML_EVENT_KEYDOWN) {
        listen_key(m, event);
    }
}

bool ui_mount_area_key(ui_mount *m, const char *key)
{
    rml_element *root = ui_mount_root(m);
    rml_element *el = m->installed && root ? first_owned(root, "[data-area-keys]")
                                           : NULL;
    char name[128];
    if (!el || !element_key_binding(el, "data-area-keys", key, 0, name, sizeof name)) {
        return false;
    }
    ui_activation a = { .button = -1, .anchor = rml_element_ref_create(el) };
    snprintf(a.event, sizeof a.event, "%s", name);
    snprintf(a.id, sizeof a.id, "%s", rml_element_id(el));
    return queue_activation(m, &a);
}

void ui_mount_emit(ui_mount *m, const rml_event *event, const char *name, int argc, const char *const *args)
{
    rml_element *element = event->current;
    assert(ui_mount_holds(m, element));
    ui_activation a = activation(m, element, event);
    snprintf(a.event, sizeof a.event, "%s", name);
    char input[128], control[128];
    char *full_control = NULL;
    const char *text = NULL;
    if (event->native && event->has_value) {
        size_t length = rml_event_value(event, NULL, 0);
        if (length >= UI_ACTIVATION_BYTES_MAX) {
            snprintf(m->error, sizeof m->error, "UI control text exceeds the activation byte budget; event refused");
            ui_activation_clear(&a);
            return;
        }
        char *buffer = control;
        size_t capacity = sizeof control;
        if (length >= capacity) {
            full_control = xmalloc(length + 1);
            buffer = full_control;
            capacity = length + 1;
        }
        rml_event_value(event, buffer, capacity);
        text = buffer;
    }
    bool captured = ui_activation_capture(&a, argc, args, text);
    free(full_control);
    if (!captured) {
        snprintf(m->error, sizeof m->error, "UI event exceeds the argument or source-plus-metadata byte budget; event refused");
        ui_activation_clear(&a);
        return;
    }
    const char *value = a.has_text ? ui_activation_text(&a)
                      : argc       ? ui_activation_argument(&a, 0)
                                   : NULL;
    if (!value && !strcmp(rml_element_tag_name(element), "input") &&
        rml_element_value(element, input, sizeof input) > 0) {
        value = input;
    }
    if (value) {
        char *end;
        a.value = strtof(value, &end);
        a.has_value = end != value;
    }
    queue_activation(m, &a);
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
    m->activation_bytes -= m->taken.payload_bytes;
    ui_activation_clear(&m->taken);
    *out = m->pending[0];
    m->taken = *out;
    memmove(m->pending, m->pending + 1, (size_t)(count - 1) * sizeof *m->pending);
    asetlen(m->pending, count - 1);
    return true;
}

/* An emit argument read as a payload value of `type`, or false for a type
   a UI event cannot carry. */
static bool argument_value(const char *text, pin_value *value)
{
    switch (value->type) {
        case PIN_STR:
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
    if ((v->type.type == PIN_STR || v->type.type == PIN_NAME) &&
        !strcmp(v->name, "Key")) {
        value->str = a->key;
        return true;
    }
    if (v->type.type == PIN_I32 && !strcmp(v->name, "Modifiers")) {
        value->i = (int)a->modifiers;
        return true;
    }
    if (v->type.type == PIN_I32 && !strcmp(v->name, "Button")) {
        value->i = a->button;
        return true;
    }
    if (v->type.type == PIN_BOOL && !strcmp(v->name, "Canceled")) {
        value->b = a->canceled;
        return true;
    }
    if (a->has_value && !strcmp(v->name, "Value") &&
        (v->type.type == PIN_F32 || v->type.type == PIN_F64)) {
        if (v->type.type == PIN_F32) {
            value->f = a->value;
        } else {
            value->d = a->value;
        }
        return true;
    }
    if (a->has_text && !strcmp(v->name, "Text") &&
        (v->type.type == PIN_STR || v->type.type == PIN_NAME)) {
        value->str = ui_activation_text(a);
        return true;
    }
    if (v->type.type == PIN_STRUCT && v->type.type_name &&
        !strcmp(v->type.type_name, "CF_V2")) {
        const float vectors[][2] = { { a->x, a->y },
                                     { a->local_x, a->local_y },
                                     { a->delta_x, a->delta_y },
                                     { a->wheel_x, a->wheel_y } };
        const char *names[] = { "Pointer", "LocalPointer", "Delta", "Wheel" };
        for (int i = 0; i < 4; i++) {
            if (!strcmp(v->name, names[i])) {
                memcpy(pointer, vectors[i], sizeof vectors[i]);
                value->ptr = pointer;
                return true;
            }
        }
    }
    return false;
}

/* The declared payload in its order: event_value supplies automatic fields;
 * each other value consumes emit's next argument (strings borrow `a`).
 * `primary` holds Mod's platform modifier bits. Returns how many
 * values it wrote to `out`, or -1 for a payload a UI event cannot carry or
 * more than `capacity`. */
static int ui_activation_payload(
    const ui_activation *a,
    const render_ui_event_field *fields,
    int count,
    unsigned primary,
    float vectors[][2],
    pin_value *out,
    int capacity
)
{
    int argument = 0;
    if (count > capacity) {
        return -1;
    }
    /* A complete scalar tuple is explicit, including normally automatic
     * names such as Key. Partial tuples retain DOM-derived fields and consume
     * only authored arguments. Geometry vectors remain automatic. */
    bool explicit_all = a->argc == count;
    for (int i = 0; i < count && explicit_all; i++) {
        pin_value value = fields[i].type;
        explicit_all = value.container == PIN_CONTAINER_SINGLE &&
                       argument_value("", &value);
    }
    if (explicit_all) {
        for (int i = 0; i < count; i++) {
            out[i] = fields[i].type;
            if (!argument_value(ui_activation_argument(a, i), &out[i])) {
                return -1;
            }
        }
        return count;
    }
    int manual = 0;
    for (int i = 0; i < count; i++) {
        pin_value value = fields[i].type;
        float vector[2];
        if (!event_value(a, &fields[i], primary, vector, &value)) {
            manual++;
        }
    }
    int controls = a->argc > manual ? a->argc - manual : 0;
    for (int i = 0; i < count; i++) {
        const render_ui_event_field *v = &fields[i];
        out[i] = v->type;
        if (v->type.container != PIN_CONTAINER_SINGLE) {
            return -1;
        }
        bool automatic = event_value(a, v, primary, vectors[i], &out[i]);
        bool control = (!strcmp(v->name, "Text") &&
                        (v->type.type == PIN_STR || v->type.type == PIN_NAME)) ||
                       (!strcmp(v->name, "Value") &&
                        (v->type.type == PIN_F32 || v->type.type == PIN_F64));
        if (automatic && !(control && controls > 0)) {
            continue;
        }
        if (automatic && control) {
            controls--;
        }
        const char *text = argument < a->argc ? ui_activation_argument(a, argument)
                                              : "";
        argument++;
        if (!argument_value(text, &out[i])) {
            return -1;
        }
    }
    return count;
}

/* Values an event's payload may hold; a UI event declares a few. */
#define PAYLOAD_MAX UI_ACTIVATION_ARGS

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
    float vectors[PAYLOAD_MAX][2];
    count = ui_activation_payload(a, fields, count, primary, vectors, values, PAYLOAD_MAX);
    if (count < 0) {
        return UNCARRIED;
    }
    return host->dispatch(
        host->context,
        ecs_entity_to_u64(entity),
        a->event,
        (execution_arguments){ .values = values, .count = count }
    );
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
void ui_apply_element(rml_element *root, ui_element *e)
{
    bool is_root = !strcmp(e->selector, "body") || !strcmp(e->selector, ":scope");
    int count = is_root
                  ? 1
                  : rml_element_query_selector_all(root, e->selector, NULL, 0);
    rml_element **matches = count ? xmalloc((size_t)count * sizeof *matches) : NULL;
    if (is_root) {
        matches[0] = root;
    } else {
        rml_element_query_selector_all(root, e->selector, matches, count);
    }
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (mount_owns(root, matches[i])) {
            matches[kept++] = matches[i];
        }
    }
    count = kept;
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

static void apply_mount(ui_mount *m, ui *model, uint64_t entity, bool read_only)
{
    rml_element *root = ui_mount_root(m);
    if (!m->installed || !root) {
        return;
    }
    for (int i = 0; i < ui_element_count(model); i++) {
        ui_element *e = ui_element_at(model, i);
        if (e->entity == entity && e->revision > m->applied) {
            if (read_only) {
                ui_element local = *e;
                local.focus = local.click = local.popover = false;
                local.move_focus = 0;
                local.reveal = -1;
                ui_apply_element(root, &local);
            } else {
                ui_apply_element(root, e);
            }
        }
    }
    if (!read_only) {
        m->applied = ui_revision(model);
    }
}
void ui_mount_apply(ui_mount *m, ui *model, uint64_t entity)
{
    apply_mount(m, model, entity, false);
}
void ui_mount_apply_read_only(ui_mount *m, ui *model, uint64_t entity)
{
    apply_mount(m, model, entity, true);
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
        if (!mount_owns(root, found[i])) {
            continue;
        }
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
    ui_unmeasure(model, entity);
    rml_element *root = ui_mount_root(m);
    if (!m->installed || !root) {
        return;
    }
    window_lists(m, root);
    rml_element *local[16];
    const char *selector = "[id][data-event-scroll], [id][data-measure]";
    int count = rml_element_query_selector_all(root, selector, local, 16);
    rml_element **found = local;
    if (count > 16) {
        found = xmalloc((size_t)count * sizeof *found);
        rml_element_query_selector_all(root, selector, found, count);
    }
    float dp = rml_context_dp_ratio(rml_element_context(root));
    for (int i = 0; i < count; i++) {
        if (!mount_owns(root, found[i])) {
            continue;
        }
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
        ui_measure_width(model, entity, selector, rml_element_client_width(found[i]) / dp);
        ui_measure_pixel_ratio(model, entity, selector, dp);
    }
    if (found != local) {
        free(found);
    }
    const char *roots[] = { "body", ":scope" };
    for (int i = 0; i < 2; i++) {
        ui_measure(
            model,
            entity,
            roots[i],
            rml_element_scroll_top(root) / dp,
            rml_element_client_height(root) / dp,
            rml_element_scroll_height(root) / dp
        );
        ui_measure_width(model, entity, roots[i], rml_element_client_width(root) / dp);
        ui_measure_pixel_ratio(model, entity, roots[i], dp);
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
    if (prepared_loader(p->loader, p->prepared_only)) {
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
        p->source = open_source(p->loader, content_root, name, true);
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
    assert(valid_loader(next));
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
    bool shown;
} ui_view;

typedef struct ui_embed
{
    ui_view view;
    rml_element_ref *host;
} ui_embed;

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
    uint64_t model_entity;
    const ecs_world *world; /* identity of the owners currently mounted */
    ui_view *views;
    ui_embed *embedded;
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

/* The host or one of its conditional ancestors may already be gone. Mount
 * references expire with DOM destruction, so no cached raw root is touched. */
static void close_embed(render_ui *ui, ui_embed *embed)
{
    ui->restyle = true;
    embed->view.root = ui_mount_root(embed->view.mount);
    close_view(ui, &embed->view);
    rml_element_ref_destroy(embed->host);
    *embed = (ui_embed){ 0 };
}

static void close_embeds(render_ui *ui)
{
    for (int i = acount(ui->embedded) - 1; i >= 0; i--) {
        close_embed(ui, &ui->embedded[i]);
    }
    asetlen(ui->embedded, 0);
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
            (prepared_loader(page->loader, page->prepared_only) || page->loader.open_context)
                ? page->name
                : NULL
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
    close_embeds(ui);
    for (int i = 0; i < acount(ui->views); i++) {
        close_view(ui, &ui->views[i]);
    }
    asetlen(ui->views, 0);
    close_view(ui, &ui->overlay);
    ui->overlay.entity = ui->model_entity;
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
    if (ui->model_entity) {
        ui_data_bind_self(&ui->data, ui->doc, ui->model_entity);
    }
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
            ui->loader.resident_images || prepared_loader(ui->loader, prepared_only)
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
void render_ui_set_model_entity(render_ui *ui, uint64_t entity)
{
    assert(ui);
    if (ui->model_entity == entity) {
        return;
    }
    ui->model_entity = entity;
    load_page(ui);
}

void render_ui_set_loader(render_ui *ui, const render_ui_loader *loader)
{
    assert(ui && !ui->prepared_only);
    ui->loader = loader ? *loader : (render_ui_loader){ 0 };
    ui_page_set_loader(&ui->page, loader);
    if (ui->overlay.mount) {
        ui_mount_set_loader(ui->overlay.mount, &ui->loader);
    }
    for (int i = 0; i < acount(ui->views); i++) {
        if (ui->views[i].mount) {
            ui_mount_set_loader(ui->views[i].mount, loader);
        }
    }
    for (int i = 0; i < acount(ui->embedded); i++) {
        ui_mount_set_loader(ui->embedded[i].view.mount, loader);
    }
    rml_backend_require_resident_images(
        ui->backend,
        ui->loader.resident_images || prepared_loader(ui->loader, ui->prepared_only)
    );
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
    close_embeds(ui);
    afree(ui->embedded);
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

void render_ui_bind_texture(render_ui *ui, const char *name, uint64_t texture, uint64_t revision)
{
    rml_backend_set_texture_revision(ui->backend, name, texture, revision);
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
    for (rml_element *at = element; at; at = rml_element_parent(at)) {
        for (int i = 0; i < acount(ui->embedded); i++) {
            ui_mount *m = ui->embedded[i].view.mount;
            if (ui_mount_root(m) == at) {
                return m;
            }
        }
    }
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
    if (m && event->current == ui_mount_root(m)) {
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
static int view_count(const render_ui *ui)
{
    return acount(ui->views) + acount(ui->embedded);
}

static ui_view *view_at(render_ui *ui, int i)
{
    return i < acount(ui->views) ? &ui->views[i]
                                 : &ui->embedded[i - acount(ui->views)].view;
}

static bool entity_embedded(const render_ui *ui, uint64_t entity)
{
    for (int i = 0; i < acount(ui->embedded); i++) {
        if (ui->embedded[i].view.entity == entity &&
            ui_mount_root(ui->embedded[i].view.mount)) {
            return true;
        }
    }
    return false;
}

/* Sends each view's activations to its entity's behaviors now, an input's
 * value first, so what they set shows in this update. They wait while a
 * spawn initializes, as the engine holds the events it queues. */
static void send_activations(render_ui *renderer, const ui_world *world)
{
    ui *model = world->model;
    const render_ui_host *host = &world->host;
    renderer->failure[0] = '\0';
    if (world->read_only) {
        for (int i = -1; i < view_count(renderer); i++) {
            ui_mount *mount = i < 0 ? renderer->overlay.mount
                                    : view_at(renderer, i)->mount;
            ui_activation ignored;
            while (mount && ui_mount_take(mount, &ignored)) {
            }
        }
        return;
    }
    for (int i = 0; i < view_count(renderer); i++) {
        ui_view *view = view_at(renderer, i);
        ui_activation a;
        ecs_entity target = ecs_entity_from_u64(view->entity);
        if (!ecs_entity_is_alive(world->ecs, target) ||
            ecs_entity_is_retiring(world->ecs, target)) {
            while (view->mount && ui_mount_take(view->mount, &a)) {
            }
            continue;
        }
        if (view->mount && model &&
            (i >= acount(renderer->views) ||
             (view->shown && !entity_embedded(renderer, view->entity)))) {
            ui_mount_measure(view->mount, model, view->entity);
        }
        while (view->mount &&
               !(host->initializing && host->initializing(host->context)) &&
               ui_mount_take(view->mount, &a)) {
            if (!ecs_entity_is_alive(world->ecs, target) ||
                ecs_entity_is_retiring(world->ecs, target)) {
                continue;
            }
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
    view->shown = shown;
    if (!name[0]) {
        return;
    }
    char order[32];
    snprintf(order, sizeof order, "%d", component->order);
    if (!view->mount) {
        open_view(ui, view, name, order);
    }
    rml_element_set_property(view->root, "z-index", order);
    rml_element_set_property(view->root, "display", shown ? "block" : "none");
    ui->restyle |= ui_mount_sync(view->mount, view->root, ui->listener, ui->time);
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
static ui_mount *mount_at(render_ui *ui, int i)
{
    return i < 0 ? ui->overlay.mount : view_at(ui, i)->mount;
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
    for (int i = -1; i < view_count(ui); i++) {
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
    for (int i = -1; i < view_count(ui) && !ui->error[0]; i++) {
        ui_mount *m = mount_at(ui, i);
        if (m) {
            snprintf(ui->error, sizeof ui->error, "%s", ui_mount_error(m));
        }
    }
    if (!ui->error[0]) {
        snprintf(ui->error, sizeof ui->error, "%s", ui->failure);
    }
}

/* Generic authored composition, with an explicit live-world entity. The
 * document defaults to that entity's Ui.document; an explicit document uses
 * the same resident/provider loader as every ordinary view. Host geometry is
 * untouched. Empty attributes unmount. Recursive embedding is bounded. */
static void sync_embeds(render_ui *ui, const ui_world *world)
{
    enum
    {
        MAX_EMBEDS = 64,
        MAX_DEPTH = 8
    };
    for (int i = 0; i < acount(ui->embedded); i++) {
        ui->embedded[i].view.seen = false;
    }
    rml_element *found[MAX_EMBEDS];
    int count = rml_element_query_selector_all(ui->doc, "[data-ui-entity]", found, MAX_EMBEDS);
    if (count > MAX_EMBEDS) {
        count = MAX_EMBEDS;
    }
    rml_element_ref *refs[MAX_EMBEDS];
    for (int i = 0; i < count; i++) {
        refs[i] = rml_element_ref_create(found[i]);
    }
    for (int i = 0; i < count; i++) {
        rml_element *host = rml_element_ref_get(refs[i]);
        if (!host) {
            continue;
        }
        bool hidden = false;
        for (int j = 0; j < acount(ui->views); j++) {
            if (!ui->views[j].shown && ui->views[j].mount &&
                ui_mount_holds(ui->views[j].mount, host)) {
                hidden = true;
            }
        }
        if (hidden) {
            continue;
        }
        char text[64] = "", name[128] = "";
        size_t length = rml_element_attribute(host, "data-ui-entity", text, sizeof text);
        const char *digits = text;
        bool typed = !strncmp(text, "ecs.entity(", 11);
        if (typed) {
            digits += 11;
        }
        char *end;
        errno = 0;
        uint64_t entity = strtoull(digits, &end, 10);
        bool valid = isdigit((unsigned char)*digits) && errno != ERANGE &&
                     (typed ? !strcmp(end, ")") : !*end);
        ecs_entity target = ecs_entity_from_u64(entity);
        if (!length || length >= sizeof text || !valid || !entity ||
            !ecs_entity_is_alive(world->ecs, target) ||
            ecs_entity_is_retiring(world->ecs, target)) {
            continue;
        }
        length = rml_element_attribute(host, "data-ui-document", name, sizeof name);
        if (length >= sizeof name) {
            continue;
        }
        if (!name[0]) {
            const Ui *component = Ui_get_const(world->ecs, target);
            if (!component || !component->document ||
                strlen(component->document) >= sizeof name) {
                continue;
            }
            snprintf(name, sizeof name, "%s", component->document);
        }
        int depth = 0;
        bool cycle = false;
        for (rml_element *at = host; at; at = rml_element_parent(at)) {
            for (int j = 0; j < acount(ui->embedded); j++) {
                ui_view *view = &ui->embedded[j].view;
                if (ui_mount_root(view->mount) == at) {
                    depth++;
                    if (view->entity == entity &&
                        !strcmp(ui_mount_name(view->mount), name)) {
                        cycle = true;
                    }
                }
            }
        }
        if (cycle || depth >= MAX_DEPTH) {
            continue;
        }
        ui_embed *embed = NULL;
        for (int j = 0; j < acount(ui->embedded); j++) {
            if (rml_element_ref_get(ui->embedded[j].host) == host) {
                embed = &ui->embedded[j];
            }
        }
        if (embed && (!ui_mount_root(embed->view.mount) || embed->view.entity != entity ||
                      strcmp(ui_mount_name(embed->view.mount), name))) {
            if (world->model && !world->read_only) {
                ui_unmeasure(world->model, embed->view.entity);
            }
            close_embed(ui, embed);
        }
        if (!embed) {
            if (acount(ui->embedded) >= MAX_EMBEDS) {
                continue;
            }
            apush(ui->embedded, ((ui_embed){ 0 }));
            embed = &alast(ui->embedded);
        }
        if (!embed->view.mount) {
            embed->host = rml_element_ref_create(host);
            embed->view.entity = entity;
            embed->view.mount = ui_mount_new(ui->content_root, name);
            embed->view.mount->prepared_only = ui->prepared_only;
            ui_mount_set_loader(embed->view.mount, &ui->loader);
            embed->view.root = rml_element_create_child(host, "div", NULL);
            rml_element_set_attribute(embed->view.root, "data-ui-embedded-root", "");
            ui_data_bind_self(&ui->data, embed->view.root, entity);
            rml_element_set_property(embed->view.root, "width", "100%");
            rml_element_set_property(embed->view.root, "height", "100%");
            rml_element_set_property(embed->view.root, "min-width", "0");
            rml_element_set_property(embed->view.root, "min-height", "0");
        }
        embed->view.seen = true;
        ui->restyle |= ui_mount_sync(embed->view.mount, embed->view.root, ui->listener, ui->time);
    }
    for (int i = 0; i < count; i++) {
        rml_element_ref_destroy(refs[i]);
    }
    int kept = 0;
    for (int i = 0; i < acount(ui->embedded); i++) {
        if (!ui->embedded[i].view.seen || !ui_mount_root(ui->embedded[i].view.mount)) {
            if (world->model && !world->read_only) {
                ui_unmeasure(world->model, ui->embedded[i].view.entity);
            }
            close_embed(ui, &ui->embedded[i]);
        } else {
            ui->embedded[kept++] = ui->embedded[i];
        }
    }
    asetlen(ui->embedded, kept);
}

void render_ui_update(render_ui *ui, const ui_world *w, int width, int height, double dt)
{
    assert(ui && w && w->ecs);
    ecs_world *world = w->ecs;
    if (ui->world != world) {
        for (int i = 0; i < acount(ui->embedded); i++) {
            ui_data_forget_entity(&ui->data, ui->embedded[i].view.entity);
        }
        close_embeds(ui);
        for (int i = 0; i < acount(ui->views); i++) {
            ui_data_forget_entity(&ui->data, ui->views[i].entity);
            close_view(ui, &ui->views[i]);
        }
        asetlen(ui->views, 0);
        ui->data.data_seen = 0;
        ui->world = world;
    }
    ui->time += dt;
    send_activations(ui, w);
    rml_backend_require_resident_images(
        ui->backend,
        ui->loader.resident_images || prepared_loader(ui->loader, ui->prepared_only)
    );
    if (ui_page_sync(&ui->page, ui->content_root, w->page ? w->page : "", ui->time)) {
        if (w->model && !w->read_only) {
            for (int i = 0; i < acount(ui->embedded); i++) {
                ui_unmeasure(w->model, ui->embedded[i].view.entity);
            }
        }
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
    close_unseen(ui, w->read_only ? NULL : w->model);
    ui_data_sync(&ui->data, w->save);
    ui_data_show_entities(&ui->data, w->model);
    sync_embeds(ui, w);
    if (ui->restyle) {
        install_styles(ui);
        ui->restyle = false;
    }
    collect_error(ui);
    for (int i = 0; i < view_count(ui); i++) {
        ui_view *view = view_at(ui, i);
        ui_mount *m = view->mount;
        if (m && w->model &&
            (i >= acount(ui->views) ||
             (view->shown && !entity_embedded(ui, view->entity)))) {
            if (w->read_only) {
                ui_mount_apply_read_only(m, w->model, view->entity);
            } else {
                ui_mount_apply(m, w->model, view->entity);
            }
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
