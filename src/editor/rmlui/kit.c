/* Studio kit: immediate-mode calls reconciled onto a retained RmlUi document.
 * This file keys, updates and retires nodes and routes RmlUi events to the
 * controllers in kit_number.c, kit_controls.c, kit_popup.c and kit_command.c.
 * They implement the Base UI contracts recorded in
 * docs/prototypes/studio/kit.html; the C editor owns every model value. */
#include "editor/rmlui/kit_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CHIBI_RMLUI_RESOURCE_DIR
#define CHIBI_RMLUI_RESOURCE_DIR "src/editor/rmlui/resources"
#endif

/* ---------- Strings ---------- */

uint64_t str_hash(const char *s)
{
    return cf_fnv1a(s, (int)strlen(s));
}

bool str_eq(const char *a, const char *b)
{
    return strcmp(a ? a : "", b ? b : "") == 0;
}

bool str_set(char **s, const char *text)
{
    text = text ? text : "";
    if (*s && strcmp(*s, text) == 0) {
        return false;
    }
    char *copy = xstrdup(text);
    free(*s);
    *s = copy;
    return true;
}

/* Builds the result apart from *s, so arguments may point into it. */
static void str_vcatf(char **s, const char *format, va_list args)
{
    va_list measure;
    va_copy(measure, args);
    int add = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    assert(add >= 0);
    size_t have = *s ? strlen(*s) : 0;
    char *out = xmalloc(have + (size_t)add + 1);
    memcpy(out, *s ? *s : "", have);
    vsnprintf(out + have, (size_t)add + 1, format, args);
    free(*s);
    *s = out;
}

void str_catf(char **s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    str_vcatf(s, format, args);
    va_end(args);
}

void str_cat(char **s, const char *text)
{
    str_catf(s, "%s", text ? text : "");
}

static const char *entity(char c)
{
    switch (c) {
        case '&': return "&amp;";
        case '<': return "&lt;";
        case '>': return "&gt;";
        case '"': return "&quot;";
        default: return NULL;
    }
}

void escape(char **s, const char *text)
{
    text = text ? text : "";
    size_t have = *s ? strlen(*s) : 0, add = 0;
    for (const char *c = text; *c; c++) {
        const char *e = entity(*c);
        add += e ? strlen(e) : 1;
    }
    char *out = xmalloc(have + add + 1);
    memcpy(out, *s ? *s : "", have);
    char *at = out + have;
    for (const char *c = text; *c; c++) {
        const char *e = entity(*c);
        if (!e) {
            *at++ = *c;
            continue;
        }
        size_t length = strlen(e);
        memcpy(at, e, length);
        at += length;
    }
    *at = 0;
    free(*s);
    *s = out;
}

void utf8(char out[static 5], uint32_t cp)
{
    static const uint32_t lead[] = { 0, 0, 0xc0, 0xe0, 0xf0 };
    int count = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    out[count] = 0;
    for (int i = count - 1; i > 0; i--) {
        out[i] = (char)(0x80 | (cp & 0x3f));
        cp >>= 6;
    }
    out[0] = (char)(lead[count] | cp);
}

void format_number(char out[static NUMBER_TEXT], double v, int precision, bool integer)
{
    if (!isfinite(v)) {
        snprintf(out, NUMBER_TEXT, "-");
        return;
    }
    if (integer) {
        snprintf(out, NUMBER_TEXT, "%.0f", v);
        return;
    }
    snprintf(out, NUMBER_TEXT, "%.*f", precision > 0 ? precision : 0, v);
    if (strchr(out, '.')) {
        size_t length = strlen(out);
        while (length && out[length - 1] == '0') {
            out[--length] = 0;
        }
        if (length && out[length - 1] == '.') {
            out[--length] = 0;
        }
    }
    if (strcmp(out, "-0") == 0) {
        snprintf(out, NUMBER_TEXT, "0");
    }
}

const char *frame_format(kit *k, const char *format, ...)
{
    char *s = NULL;
    va_list args;
    va_start(args, format);
    str_vcatf(&s, format, args);
    va_end(args);
    apush(k->strings, s);
    return s;
}

bool rect_contains(kit_rect r, float x, float y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* ---------- Elements ---------- */

char *element_value(const rml_element *el)
{
    size_t size = rml_element_value(el, NULL, 0) + 1;
    char *s = xmalloc(size);
    rml_element_value(el, s, size);
    return s;
}

char *element_attribute(const rml_element *el, const char *name)
{
    size_t size = rml_element_attribute(el, name, NULL, 0) + 1;
    char *s = xmalloc(size);
    rml_element_attribute(el, name, s, size);
    return s;
}

char *element_inner_rml(const rml_element *el)
{
    size_t size = rml_element_inner_rml(el, NULL, 0) + 1;
    char *s = xmalloc(size);
    rml_element_inner_rml(el, s, size);
    return s;
}

float dp(const kit *k)
{
    return k->in.dp_ratio > 0 ? k->in.dp_ratio : 1;
}

kit_rect element_rect(const kit *k, rml_element *el)
{
    rml_vec2 offset = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    float s = dp(k);
    return (kit_rect){ offset.x / s, offset.y / s, size.x / s, size.y / s };
}

static pair *pair_find(pair *pairs, const char *name)
{
    for (int i = 0; i < acount(pairs); i++) {
        if (strcmp(pairs[i].name, name) == 0) {
            return &pairs[i];
        }
    }
    return NULL;
}

static void pair_put(pair **pairs, const char *name, const char *value)
{
    pair *p = pair_find(*pairs, name);
    if (!p) {
        apush(*pairs, (pair){ xstrdup(name), NULL });
        p = &alast(*pairs);
    }
    str_set(&p->value, value);
}

static void pairs_free(pair *pairs)
{
    for (int i = 0; i < acount(pairs); i++) {
        free(pairs[i].name);
        free(pairs[i].value);
    }
    afree(pairs);
}

void set_class(node *n, const char *cls)
{
    if (str_eq(n->cls, cls)) {
        return;
    }
    str_set(&n->cls, cls);
    rml_element_set_class_names(n->el, n->cls);
}

void set_attr(node *n, const char *name, const char *value)
{
    pair *p = pair_find(n->attrs, name);
    if (p && str_eq(p->value, value)) {
        return;
    }
    pair_put(&n->attrs, name, value);
    rml_element_set_attribute(n->el, name, value);
}

void set_flag(node *n, const char *name, bool on)
{
    pair *p = pair_find(n->attrs, name);
    if ((p != NULL) == on) {
        return;
    }
    if (on) {
        pair_put(&n->attrs, name, "");
        rml_element_set_attribute(n->el, name, "");
        return;
    }
    free(p->name);
    free(p->value);
    adel(n->attrs, p - n->attrs);
    rml_element_remove_attribute(n->el, name);
}

void flag_element(rml_element *el, const char *name, bool on)
{
    if (on == rml_element_has_attribute(el, name)) {
        return;
    }
    if (on) {
        rml_element_set_attribute(el, name, "");
        return;
    }
    rml_element_remove_attribute(el, name);
}

void set_inner_text(rml_element *el, const char *text)
{
    char *rml = NULL;
    escape(&rml, text);
    rml_element_set_inner_rml(el, rml);
    free(rml);
}

void set_text(node *n, const char *text)
{
    if (str_eq(n->text, text)) {
        return;
    }
    str_set(&n->text, text);
    set_inner_text(n->el, n->text);
}

void set_element_text(rml_element *el, const char *text)
{
    char *rml = NULL;
    escape(&rml, text);
    char *current = element_inner_rml(el);
    if (strcmp(current, rml) != 0) {
        rml_element_set_inner_rml(el, rml);
    }
    free(current);
    free(rml);
}

rml_element *raw(rml_element *parent, const char *tag, const char *cls, const char *text)
{
    rml_element *el = rml_element_create_child(parent, tag, NULL);
    rml_element_set_class_names(el, cls);
    if (text) {
        set_inner_text(el, text);
    }
    return el;
}

rml_element *raw_icon(rml_element *parent, const char *cls, uint32_t cp)
{
    char *c = NULL;
    str_catf(&c, "icon %s", cls ? cls : "");
    rml_element *el = raw(parent, "span", c, NULL);
    free(c);
    if (cp) {
        char text[5];
        utf8(text, cp);
        rml_element_set_inner_rml(el, text);
    }
    return el;
}

/* ---------- Nodes ---------- */

static node *node_new(node_kind kind)
{
    node *n = xcalloc(1, sizeof *n);
    n->kind = kind;
    n->w.pending_index = -1;
    n->w.icon = UINT32_MAX;
    return n;
}

static void strings_free(char **strings)
{
    for (int i = 0; i < acount(strings); i++) {
        free(strings[i]);
    }
    afree(strings);
}

static void widget_free(widget *w)
{
    char *owned[] = { w->unit,  w->addon,   w->addon_cls,
                      w->draft, w->applied, w->label,
                      w->meta,  w->badge,   w->placeholder };
    for (size_t i = 0; i < sizeof owned / sizeof *owned; i++) {
        free(owned[i]);
    }
    strings_free(w->labels);
}

static void flow_free(flow_state *f)
{
    if (!f) {
        return;
    }
    free(f->text);
    free(f->prefix);
    free(f->suffix);
    afree(f->digits);
    afree(f->symbols);
    free(f);
}

static void morph_free(morph_state *m)
{
    if (!m) {
        return;
    }
    free(m->text);
    for (int i = 0; i < acount(m->segs); i++) {
        free(m->segs[i].id);
    }
    afree(m->segs);
    free(m);
}

static void chart_free(chart_state *c)
{
    if (!c) {
        return;
    }
    afree(c->labels);
    free(c);
}

void node_free(node *n)
{
    for (int i = 0; i < acount(n->children); i++) {
        node_free(n->children[i]);
    }
    afree(n->children);
    free(n->key);
    free(n->path);
    free(n->cls);
    free(n->text);
    free(n->tag);
    free(n->drop_type);
    pairs_free(n->attrs);
    pairs_free(n->styles);
    widget_free(&n->w);
    flow_free(n->flow);
    morph_free(n->morph);
    chart_free(n->chart);
    free(n);
}

static uint64_t element_key(const rml_element *el)
{
    return (uint64_t)(uintptr_t)el;
}

/* Indexes a node by element and path; distinct paths must hash apart. */
static void index_node(kit *k, node *n)
{
    cf_map_set(k->by_element, element_key(n->el), n);
    uint64_t h = str_hash(n->path);
    node *same = cf_map_get(k->by_path, h);
    assert(!same || str_eq(same->path, n->path));
    cf_map_set(k->by_path, h, n);
}

void unmap(kit *k, node *n)
{
    for (int i = 0; i < acount(n->children); i++) {
        unmap(k, n->children[i]);
    }
    forget_motion(k, n);
    cf_map_del(k->by_element, element_key(n->el));
    cf_map_del(k->by_path, str_hash(n->path));
    if (k->last == n) {
        k->last = NULL;
    }
    for (int i = 0; i < acount(k->triggers); i++) {
        if (k->triggers[i] == n) {
            k->triggers[i] = NULL;
        }
    }
    for (int i = 0; i < cf_map_size(k->popups); i++) {
        popup *p = k->popups[i];
        if (p->node == n) {
            p->node = NULL;
        }
        nodes_remove(p->items, n);
    }
}

void prune(kit *k, node *n)
{
    while (acount(n->children) > n->cursor) {
        retire(k, apop(n->children));
    }
}

static void insert_child(node *parent, int i, node *n)
{
    apush(parent->children, n);
    int after = acount(parent->children) - 1 - i;
    memmove(
        &parent->children[i + 1],
        &parent->children[i],
        (size_t)after * sizeof *parent->children
    );
    parent->children[i] = n;
}

static void remove_child(node *parent, int i)
{
    int count = acount(parent->children);
    memmove(
        &parent->children[i],
        &parent->children[i + 1],
        (size_t)(count - 1 - i) * sizeof *parent->children
    );
    asetlen(parent->children, count - 1);
}

/* Moves child j back to i, the ones between moving up one. */
static void rotate_child(node *parent, int i, int j)
{
    node *moved = parent->children[j];
    memmove(
        &parent->children[i + 1],
        &parent->children[i],
        (size_t)(j - i) * sizeof *parent->children
    );
    parent->children[i] = moved;
}

/* The first child from the cursor with this key, brought to the cursor. One
 * of another kind or tag is retired so a new one takes its place. */
static node *reuse_child(kit *k, node *parent, const char *key, node_kind kind, const char *tag)
{
    int i = parent->cursor;
    for (int j = i; j < acount(parent->children); j++) {
        node *candidate = parent->children[j];
        if (strcmp(candidate->key, key) != 0) {
            continue;
        }
        if (candidate->kind != kind || strcmp(candidate->tag, tag) != 0) {
            remove_child(parent, j);
            candidate->presence = false;
            retire(k, candidate);
            return NULL;
        }
        if (j != i) {
            rml_element *before = parent->children[i]->el;
            rml_element_move_child(parent->container, candidate->el, before);
            rotate_child(parent, i, j);
        }
        return candidate;
    }
    return NULL;
}

static node *create_child(kit *k, node *parent, const char *key, node_kind kind, const char *tag)
{
    int i = parent->cursor;
    node *n = node_new(kind);
    str_set(&n->key, key);
    str_set(&n->tag, tag);
    n->parent = parent;
    if (str_eq(parent->path, "")) {
        str_set(&n->path, key);
    } else {
        str_catf(&n->path, "%s/%s", parent->path, key);
    }
    n->created = k->frame;
    rml_element *before = i < acount(parent->children) ? parent->children[i]->el : NULL;
    n->el = n->container = rml_element_create_child(parent->container, tag, before);
    index_node(k, n);
    insert_child(parent, i, n);
    return n;
}

node *emit_in(kit *k, node *parent, const char *key, node_kind kind, const char *tag, const char *cls)
{
    assert(parent && key && key[0]);
    const char *element_tag = tag && tag[0] ? tag : "div";
    node *n = reuse_child(k, parent, key, kind, element_tag);
    if (!n) {
        n = create_child(k, parent, key, kind, element_tag);
    }
    parent->cursor++;
    n->frame = k->frame;
    free(n->drop_type);
    n->drop_type = NULL;
    set_class(n, cls);
    k->last = n;
    return n;
}

node *emit(kit *k, const char *key, node_kind kind, const char *tag, const char *cls)
{
    assert(acount(k->stack));
    return emit_in(k, alast(k->stack), key, kind, tag, cls);
}

void push_node(kit *k, node *n)
{
    n->cursor = 0;
    apush(k->stack, n);
}

void pop_node(kit *k)
{
    assert(acount(k->stack) > 1);
    node *n = alast(k->stack);
    prune(k, n);
    (void)apop(k->stack);
    k->last = n;
}

static node *element_node(const kit *k, const rml_element *el)
{
    return cf_map_get(k->by_element, element_key(el));
}

node *node_for(const kit *k, rml_element *el)
{
    for (; el; el = rml_element_parent(el)) {
        node *n = element_node(k, el);
        if (n) {
            return n;
        }
    }
    return NULL;
}

node *path_node(const kit *k, const char *path)
{
    node *n = cf_map_get(k->by_path, str_hash(path));
    return n && str_eq(n->path, path) ? n : NULL;
}

/* Picker content lives in a layer popup; route its events to the owning field. */
static node *owner(const kit *k, node *n)
{
    if (!n || n->kind != KIND_POPUP || strncmp(n->key, "@color:", 7) != 0) {
        return n;
    }
    node *field = path_node(k, n->key + 7);
    return field ? field : n;
}

bool within(rml_element *el, rml_element *ancestor)
{
    for (; el; el = rml_element_parent(el)) {
        if (el == ancestor) {
            return true;
        }
    }
    return false;
}

int nodes_index(node *const *list, const node *n)
{
    for (int i = 0; i < acount(list); i++) {
        if (list[i] == n) {
            return i;
        }
    }
    return -1;
}

void nodes_remove(node **list, const node *n)
{
    int kept = 0;
    for (int i = 0; i < acount(list); i++) {
        if (list[i] != n) {
            list[kept++] = list[i];
        }
    }
    asetlen(list, kept);
}

/* ---------- Popups ---------- */

popup *popup_find(const kit *k, const char *key)
{
    popup *p = cf_map_get(k->popups, str_hash(key));
    return p && str_eq(p->key, key) ? p : NULL;
}

popup *popup_at(kit *k, const char *key)
{
    popup *p = popup_find(k, key);
    if (p) {
        return p;
    }
    uint64_t h = str_hash(key);
    assert(!cf_map_has(k->popups, h));
    p = xcalloc(1, sizeof *p);
    str_set(&p->key, key);
    p->highlight = -1;
    cf_map_set(k->popups, h, p);
    return p;
}

static void popup_free(popup *p)
{
    free(p->key);
    free(p->parent);
    free(p->sub_hover);
    free(p->typed);
    afree(p->items);
    free(p);
}

/* ---------- Event routing ---------- */

unsigned event_modifiers(const rml_event *e)
{
    return (e->modifiers & RML_KM_CTRL ? KIT_MOD_CTRL : 0u) |
           (e->modifiers & RML_KM_SHIFT ? KIT_MOD_SHIFT : 0u) |
           (e->modifiers & RML_KM_ALT ? KIT_MOD_ALT : 0u) |
           (e->modifiers & RML_KM_META ? KIT_MOD_META : 0u);
}

static void mousedown(kit *k, const rml_event *e, node *n)
{
    popup_press(k, n, e->target);
    command_press(k, e->target);
    if (!n) {
        return;
    }
    if (e->button == 1) {
        n->events |= EV_CONTEXT;
        return;
    }
    if (e->button != 0) {
        return;
    }
    n->events |= EV_PRESS;
    k->keyboard_edit = false;
    switch (n->kind) {
        case KIND_NUMBER: number_press(k, n, e); break;
        case KIND_TRIGGER: trigger_press(k, n, e); break;
        default: control_press(k, n, e); break;
    }
}

static void click(kit *k, const rml_event *e, node *n)
{
    if (k->drag.active) {
        return;
    }
    switch (n->kind) {
        case KIND_NUMBER: number_click(k, n); return;
        case KIND_MENU_ITEM:
        case KIND_SUBMENU: menu_click(k, n); return;
        case KIND_COMMAND_ITEM: command_click(k, n); return;
        case KIND_BOX:
            /* A click on a label or icon is also a click on the boxes around
               it, as DOM clicks bubble, so a whole row or tab answers. */
            for (node *box = n; box && box->kind == KIND_BOX; box = box->parent) {
                box->events |= EV_CLICK;
            }
            return;
        default: control_click(k, n, e->target); return;
    }
}

static void drag(kit *k, const rml_event *e)
{
    rml_element *dragged = e->drag_element ? e->drag_element : e->target;
    node *n = owner(k, node_for(k, dragged));
    if (!n) {
        return;
    }
    if (n->kind == KIND_NUMBER) {
        number_drag(k, n, e);
        return;
    }
    control_drag(k, n, e);
}

static void keydown(kit *k, const rml_event *e, node *n)
{
    k->keyboard_edit = true;
    if (k->drag.active && e->key == RML_KEY_ESCAPE) {
        drag_cancel(k);
        rml_event_stop_propagation(e);
        return;
    }
    if (command_key(k, e) || popup_key(k, e) || !n) {
        return;
    }
    if (n->kind == KIND_NUMBER) {
        number_key(k, n, e);
        return;
    }
    control_key(n, e);
}

static void blur(kit *k, const rml_event *e, node *n)
{
    if (!n) {
        return;
    }
    if (n->kind == KIND_NUMBER) {
        number_blur(k, n, e->target);
        return;
    }
    control_blur(n, e->target);
}

static void mouseover(kit *k, node *n)
{
    if (!n) {
        return;
    }
    if (n->kind == KIND_COMMAND_ITEM) {
        command_hover(k, n);
        return;
    }
    menu_hover(k, n);
}

static void on_event(void *user, const rml_event *e)
{
    kit *k = user;
    node *n = owner(k, node_for(k, e->target));
    switch (e->id) {
        case RML_EVENT_MOUSEDOWN: mousedown(k, e, n); break;
        case RML_EVENT_MOUSEUP:
            if (n && n->kind == KIND_NUMBER) {
                number_release(n);
            }
            break;
        case RML_EVENT_CLICK:
            if (n) {
                click(k, e, n);
            }
            break;
        case RML_EVENT_DBLCLICK:
            if (n) {
                n->events |= EV_DOUBLE;
            }
            break;
        case RML_EVENT_DRAGSTART:
        case RML_EVENT_DRAG:
        case RML_EVENT_DRAGEND: drag(k, e); break;
        case RML_EVENT_KEYDOWN: keydown(k, e, n); break;
        case RML_EVENT_CHANGE:
            if (!command_change(k, e->target) && n) {
                control_change(n, e->target);
            }
            break;
        case RML_EVENT_FOCUS:
            if (n) {
                control_focus(n, e->target);
            }
            break;
        case RML_EVENT_BLUR: blur(k, e, n); break;
        case RML_EVENT_MOUSEOVER: mouseover(k, n); break;
        default: break;
    }
}

static const rml_event_id kit_events[] = {
    RML_EVENT_MOUSEDOWN, RML_EVENT_MOUSEUP,   RML_EVENT_CLICK,
    RML_EVENT_DBLCLICK,  RML_EVENT_DRAGSTART, RML_EVENT_DRAG,
    RML_EVENT_DRAGEND,   RML_EVENT_KEYDOWN,   RML_EVENT_CHANGE,
    RML_EVENT_MOUSEOVER, RML_EVENT_FOCUS,     RML_EVENT_BLUR,
};

#define KIT_EVENT_COUNT (sizeof kit_events / sizeof *kit_events)

/* ---------- Stylesheet reload ---------- */

/* RCSS edits apply without a rebuild: the document re-reads its stylesheets
   when a resource stylesheet changes. */
static void watch_styles(kit *k)
{
    DIR *dir = opendir(CHIBI_RMLUI_RESOURCE_DIR);
    assert(dir);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || strcmp(dot, ".rcss") != 0) {
            continue;
        }
        file_watch w = { 0 };
        char path[sizeof w.path];
        snprintf(path, sizeof path, "%s/%s", CHIBI_RMLUI_RESOURCE_DIR, entry->d_name);
        file_watch_changed(&w, path, 0);
        apush(k->styles, w);
    }
    closedir(dir);
}

static void sync_styles(kit *k)
{
    bool changed = false;
    for (int i = 0; i < acount(k->styles); i++) {
        changed |= file_watch_changed(&k->styles[i], k->styles[i].path, k->in.time);
    }
    if (!changed && !k->restyle) {
        return;
    }
    rml_document_reload_style_sheet(k->doc);
    for (int i = 0; i < acount(k->extra_styles); i++) {
        rml_document_add_style_sheet(k->doc, k->extra_styles[i].value);
    }
    k->restyle = false;
}

void kit_style_sheet(kit *k, const char *key, const char *rcss)
{
    pair *p = pair_find(k->extra_styles, key);
    if (!rcss && p) {
        free(p->name);
        free(p->value);
        adel(k->extra_styles, p - k->extra_styles);
        k->restyle = true;
        return;
    }
    if (!rcss || (p && str_eq(p->value, rcss))) {
        return;
    }
    pair_put(&k->extra_styles, key, rcss);
    k->restyle = true;
}

/* ---------- API ---------- */

kit *kit_create(void *context, const char *document_path)
{
    kit *k = xcalloc(1, sizeof *k);
    k->context = context;
    k->frame = 1;
    k->surface_button = -1;
    k->command.chosen = -1;
    char *path = NULL;
    str_catf(&path, "%s/%s", CHIBI_RMLUI_RESOURCE_DIR, document_path);
    k->doc = rml_context_load_document(k->context, path);
    free(path);
    assert(k->doc);
    rml_document_show(k->doc, RML_MODAL_NONE, RML_FOCUS_AUTO, RML_SCROLL_FLAG_AUTO);
    register_chart_element();
    register_wires_element();
    register_dot_element();
    k->listener = rml_listener_create(on_event, k);
    for (size_t i = 0; i < KIT_EVENT_COUNT; i++) {
        rml_element_add_event_listener(k->doc, kit_events[i], k->listener, true);
    }
    rml_element *layer = rml_element_by_id(k->doc, "layer");
    assert(layer);
    k->layer = node_new(KIND_ROOT);
    k->layer->el = k->layer->container = layer;
    str_set(&k->layer->path, "layer");
    cf_map_set(k->by_element, element_key(layer), k->layer);
    apush(k->roots, k->layer);
    k->tooltip = raw(layer, "div", "tooltip", NULL);
    rml_element_set_property(k->tooltip, "display", "none");
    watch_styles(k);
    return k;
}

static void kit_free(kit *k)
{
    for (int i = 0; i < acount(k->roots); i++) {
        node_free(k->roots[i]);
    }
    afree(k->roots);
    for (int i = 0; i < acount(k->exiting); i++) {
        node_free(k->exiting[i].node);
    }
    afree(k->exiting);
    for (int i = 0; i < cf_map_size(k->popups); i++) {
        popup_free(k->popups[i]);
    }
    cf_map_free(k->popups);
    for (int i = 0; i < cf_map_size(k->springs); i++) {
        free(k->springs[i].key);
    }
    cf_map_free(k->springs);
    for (int i = 0; i < acount(k->toasts); i++) {
        free(k->toasts[i].title);
        free(k->toasts[i].description);
        free(k->toasts[i].action);
    }
    afree(k->toasts);
    drag_state *d = &k->drag;
    char *drag_strings[] = { d->type,   d->payload, d->label,
                             d->source, d->over,    d->target };
    for (size_t i = 0; i < sizeof drag_strings / sizeof *drag_strings; i++) {
        free(drag_strings[i]);
    }
    free(k->command.placeholder);
    free(k->command.query);
    afree(k->command.shown);
    free(k->surface_capture);
    strings_free(k->strings);
    cf_map_free(k->by_element);
    cf_map_free(k->by_path);
    cf_map_free(k->toast_actions);
    afree(k->stack);
    afree(k->popup_stack);
    afree(k->triggers);
    afree(k->emitting);
    afree(k->entering);
    afree(k->layout_nodes);
    afree(k->text_motion);
    afree(k->styles);
    pairs_free(k->extra_styles);
    free(k);
}

void kit_destroy(kit *k)
{
    if (!k) {
        return;
    }
    for (size_t i = 0; i < KIT_EVENT_COUNT; i++) {
        rml_element_remove_event_listener(k->doc, kit_events[i], k->listener, true);
    }
    rml_document_close(k->doc);
    rml_listener_destroy(k->listener);
    kit_free(k);
}

static void close_root(kit *k)
{
    if (!k->root) {
        return;
    }
    while (acount(k->stack) > 1) {
        pop_node(k);
    }
    prune(k, k->root);
    aclear(k->stack);
    k->root = NULL;
}

void kit_begin(kit *k, const kit_frame_input *input)
{
    k->in = *input;
    sync_styles(k);
    aclear(k->triggers);
    place_popups(k);
    drag_begin_frame(k);
    repeat_steppers(k);
    open_hovered_submenus(k);
    k->layer->cursor = 0;
    k->layer->frame = k->frame;
}

void kit_end(kit *k)
{
    close_root(k);
    close_activated_menus(k);
    toasts_frame(k);
    drag_end_frame(k);
    prune(k, k->layer);
    if (!k->command.open) {
        k->command_backdrop = NULL;
    }
    update_tooltip(k);
    motion_frame(k);
    for (int i = 0; i < cf_map_size(k->by_path); i++) {
        k->by_path[i]->events = 0;
        k->by_path[i]->w.committed = false;
    }
    strings_free(k->strings);
    k->strings = NULL;
    k->frame++;
}

static node *root_node(const kit *k, const char *element_id)
{
    for (int i = 0; i < acount(k->roots); i++) {
        if (str_eq(k->roots[i]->path, element_id)) {
            return k->roots[i];
        }
    }
    return NULL;
}

static node *add_root(kit *k, const char *element_id)
{
    rml_element *el = rml_element_by_id(k->doc, element_id);
    assert(el && "kit_root element must exist in the document");
    node *root = node_new(KIND_ROOT);
    root->el = root->container = el;
    str_set(&root->path, element_id);
    index_node(k, root);
    apush(k->roots, root);
    return root;
}

void kit_root(kit *k, const char *element_id)
{
    close_root(k);
    node *root = root_node(k, element_id);
    k->root = root ? root : add_root(k, element_id);
    k->root->frame = k->frame;
    push_node(k, k->root);
}

void kit_push(kit *k, const char *key, const char *tag, const char *cls)
{
    push_node(k, emit(k, key, KIND_BOX, tag, cls));
}

void kit_pop(kit *k)
{
    pop_node(k);
}

void kit_leaf(kit *k, const char *key, const char *tag, const char *cls)
{
    emit(k, key, KIND_BOX, tag, cls);
}

rml_element *kit_host(kit *k, const char *key, const char *cls)
{
    return emit(k, key, KIND_BOX, "div", cls)->el;
}

void kit_label(kit *k, const char *key, const char *cls, const char *text)
{
    node *n = emit(k, key, KIND_BOX, "span", cls);
    set_text(n, text);
}

void kit_icon(kit *k, const char *key, const char *cls, uint32_t codepoint)
{
    const char *c = frame_format(k, "icon %s", cls ? cls : "");
    node *n = emit(k, key, KIND_BOX, "span", c);
    char text[5];
    utf8(text, codepoint);
    set_text(n, text);
}

void kit_markup(kit *k, const char *key, const char *cls, const char *rml)
{
    node *n = emit(k, key, KIND_BOX, "span", cls);
    if (str_eq(n->text, rml)) {
        return;
    }
    str_set(&n->text, rml);
    rml_element_set_inner_rml(n->el, n->text);
}

void kit_text(kit *k, const char *text)
{
    assert(k->last);
    set_text(k->last, text);
}

void kit_attr(kit *k, const char *name, const char *value)
{
    assert(k->last);
    set_attr(k->last, name, value ? value : "");
}

void kit_flag(kit *k, const char *name, bool on)
{
    assert(k->last);
    set_flag(k->last, name, on);
}

void kit_style(kit *k, const char *property, const char *value)
{
    node *n = k->last;
    assert(n);
    pair *p = pair_find(n->styles, property);
    if (p && str_eq(p->value, value)) {
        return;
    }
    pair_put(&n->styles, property, value);
    rml_element_set_property(n->el, property, value);
}

bool kit_item_clicked(kit *k)
{
    return k->last && (k->last->events & EV_CLICK);
}

bool kit_item_double_clicked(kit *k)
{
    return k->last && (k->last->events & EV_DOUBLE);
}

bool kit_item_context_clicked(kit *k)
{
    return k->last && (k->last->events & EV_CONTEXT);
}

bool kit_item_pressed(kit *k)
{
    return k->last && (k->last->events & EV_PRESS);
}

bool kit_item_hovered(kit *k)
{
    return k->last && rml_element_is_pseudo_class_set(k->last->el, "hover");
}

bool kit_item_active(kit *k)
{
    node *n = k->last;
    if (!n) {
        return false;
    }
    const widget *w = &n->w;
    bool picker = n->kind == KIND_COLOR &&
                  kit_popup_is_open(k, frame_format(k, "color:%s", n->path));
    return w->scrubbing || w->editing || w->step_dir || w->focused || picker ||
           (w->pressing && n->kind != KIND_NUMBER);
}

bool kit_item_committed(kit *k)
{
    return k->last && k->last->w.committed;
}

kit_rect kit_item_rect(kit *k)
{
    return k->last ? element_rect(k, k->last->el) : (kit_rect){ 0 };
}

unsigned kit_modifiers(const kit *k)
{
    return k->in.modifiers;
}

const char *kit_string(kit *k, const char *text)
{
    apush(k->strings, xstrdup(text ? text : ""));
    return alast(k->strings);
}

const char *kit_classes(kit *k, const char *base, const kit_class *list, int count)
{
    char *cls = NULL;
    str_cat(&cls, base);
    for (int i = 0; i < count; i++) {
        if (!list[i].on || !list[i].name || !list[i].name[0]) {
            continue;
        }
        str_catf(&cls, "%s%s", cls[0] ? " " : "", list[i].name);
    }
    apush(k->strings, cls);
    return cls;
}

/* ---------- Host queries ---------- */

static bool is_input(const rml_element *el)
{
    return strcmp(rml_element_tag_name(el), "input") == 0;
}

bool kit_wants_keyboard(const kit *k)
{
    if (k->command.open || acount(k->popup_stack)) {
        return true;
    }
    rml_element *focus = rml_context_focus_element(k->context);
    /* A focused number field handles its own keys during RmlUi dispatch; global
       shortcuts still apply unless text is being edited. */
    return focus && is_input(focus);
}

bool kit_accepts_text(const kit *k)
{
    rml_element *focus = rml_context_focus_element(k->context);
    if (!focus) {
        return false;
    }
    node *n = element_node(k, focus);
    return is_input(focus) || (n && n->kind == KIND_NUMBER);
}

bool kit_wants_text(const kit *k, kit_rect *caret)
{
    rml_element *focus = rml_context_focus_element(k->context);
    if (!focus || !is_input(focus)) {
        return false;
    }
    if (caret) {
        *caret = element_rect(k, focus);
    }
    return true;
}

bool kit_wants_pointer(const kit *k)
{
    if (k->command.open || acount(k->popup_stack)) {
        return true;
    }
    if (!str_eq(k->surface_capture, "")) {
        return false;
    }
    rml_element *hover = rml_context_hover_element(k->context);
    if (!hover || hover == k->doc) {
        return false;
    }
    node *n = element_node(k, hover);
    return !n || n->kind != KIND_SURFACE;
}

/* Measured with the face RmlUi draws at this ratio: it truncates font sizes to
   whole pixels (ElementStyle) and hints glyph advances. */
float kit_text_width(const kit *, const char *text, float size, int weight, float ratio)
{
    assert(ratio > 0);
    int pixels = (int)(size * ratio);
    if (!text || !text[0] || pixels < 1) {
        return 0;
    }
    /* The font provider keys families in lower case. */
    uintptr_t face = rml_font_face("inter", RML_FONT_NORMAL, weight > 0 ? weight : 400, pixels);
    if (!face) {
        return 0;
    }
    return (float)rml_font_string_width(face, text) / ratio;
}

void kit_focus(kit *k)
{
    assert(k->last);
    rml_element *el = k->last->w.input_el ? k->last->w.input_el : k->last->el;
    rml_element_focus(el, true);
    if (is_input(el)) {
        rml_element_select(el);
    }
}

void kit_blur(kit *k)
{
    rml_element *focus = rml_context_focus_element(k->context);
    if (focus && focus != k->doc) {
        rml_element_blur(focus);
    }
}

const char *kit_hovered_path(const kit *k)
{
    node *n = node_for(k, rml_context_hover_element(k->context));
    return n && n->kind != KIND_ROOT ? n->path : NULL;
}

bool kit_pointer_on_control(const kit *k)
{
    rml_element *hover = rml_context_hover_element(k->context);
    for (node *n = node_for(k, hover); n; n = n->parent) {
        switch (n->kind) {
            case KIND_BUTTON:
            case KIND_NUMBER:
            case KIND_CHECK:
            case KIND_TOGGLE:
            case KIND_SELECT:
            case KIND_TEXT:
            case KIND_SEARCH:
            case KIND_COLOR: return true;
            default: break;
        }
    }
    return false;
}

const char *kit_item_path(const kit *k)
{
    return k->last ? k->last->path : NULL;
}

bool kit_find(const kit *k, const char *path, kit_rect *out)
{
    node *n = path_node(k, path);
    if (!n) {
        return false;
    }
    *out = element_rect(k, n->el);
    return out->w > 0 && out->h > 0;
}

void kit_set_theme(kit *k, const char *name, bool active)
{
    rml_context_activate_theme(k->context, name, active);
    if (strcmp(name, "reduced-motion") == 0) {
        k->reduced_motion = active;
    }
}

void kit_set_body_class(kit *k, const char *name, bool on)
{
    rml_element_set_class(k->doc, name, on);
}
