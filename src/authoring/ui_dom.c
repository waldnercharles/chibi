#include "authoring/ui_dom.h"

#include "support/xalloc.h"
#include "ui/rml.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum
{
    NODES_MAX = 4096,
    DEPTH_MAX = 64,
    KEY_MAX = 512,
    VALUE_MAX = 2048
};
typedef struct
{
    rml_element_ref *ref;
    int handle;
    unsigned forced, original;
    bool seen;
} reference;
struct ui_dom
{
    rml_context *context;
    struct
    {
        char key[128];
        rml_context *context;
    } contexts[16];
    int selected, context_revision;
    uint64_t hash;
    int revision, next_handle;
    reference references[NODES_MAX];
};
typedef struct
{
    rml_element *element;
    char key[32];
    int parent, depth, reference;
} node;
typedef struct
{
    ui_dom *service;
    node *nodes;
    int count, visited, buckets[NODES_MAX * 2];
    bool complete;
    uint64_t hash;
} tree;
static const char *const PSEUDOS[] = { "hover", "active", "focus", "checked" };
static void drop(reference *ref)
{
    if (!ref->ref) {
        return;
    }
    rml_element *element = rml_element_ref_get(ref->ref);
    if (element) {
        for (int i = 0; i < 4; i++) {
            if (ref->forced & (1u << i)) {
                rml_element_set_pseudo_class(element, PSEUDOS[i], (ref->original & (1u << i)) != 0);
            }
        }
    }
    rml_element_ref_destroy(ref->ref);
    *ref = (reference){ 0 };
}
static unsigned bucket(rml_element *element)
{
    uintptr_t key = (uintptr_t)element;
    key ^= key >> 17;
    return (unsigned)(key >> 3) & (NODES_MAX * 2 - 1);
}
static int retain(tree *t, rml_element *element)
{
    unsigned at = bucket(element);
    while (t->buckets[at]) {
        int index = t->buckets[at] - 1;
        if (rml_element_ref_get(t->service->references[index].ref) == element) {
            t->service->references[index].seen = true;
            return index;
        }
        at = (at + 1) & (NODES_MAX * 2 - 1);
    }
    if (t->service->next_handle == INT_MAX) {
        return -1;
    }
    for (int i = 0; i < NODES_MAX; i++) {
        if (!t->service->references[i].ref) {
            t->service->references[i] = (reference){ .ref = rml_element_ref_create(element),
                                                     .handle = ++t->service->next_handle,
                                                     .seen = true };
            t->buckets[at] = i + 1;
            return i;
        }
    }
    return -1;
}
static void hash(tree *t, const void *data, size_t bytes)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < bytes; i++) {
        t->hash ^= p[i];
        t->hash *= UINT64_C(1099511628211);
    }
}
static void walk(tree *t, rml_element *element, int parent, int depth)
{
    if (t->visited == NODES_MAX || depth > DEPTH_MAX) {
        t->complete = false;
        return;
    }
    t->visited++;
    int index = t->count++;
    node *n = &t->nodes[index];
    n->element = element;
    n->parent = parent;
    n->depth = depth;
    n->reference = retain(t, element);
    if (n->reference < 0) {
        t->complete = false;
        t->count--;
        return;
    }
    int identity = t->service->references[n->reference].handle;
    snprintf(n->key, sizeof n->key, "%d", identity);
    hash(t, &identity, sizeof identity);
    const char *tag = rml_element_tag_name(element), *id = rml_element_id(element);
    hash(t, tag, strnlen(tag, VALUE_MAX));
    hash(t, id, strnlen(id, VALUE_MAX));
    char classes[VALUE_MAX], text[VALUE_MAX], pseudo[256];
    size_t bytes = rml_element_class_names(element, classes, sizeof classes);
    if (bytes >= sizeof classes) {
        t->complete = false;
    }
    hash(t, classes, strnlen(classes, sizeof classes));
    if (!strcmp(tag, "#text")) {
        bytes = rml_element_text(element, text, sizeof text);
        if (bytes >= sizeof text) {
            t->complete = false;
        }
        hash(t, text, strnlen(text, sizeof text));
    }
    rml_element_pseudo_classes(element, pseudo, sizeof pseudo);
    hash(t, pseudo, strnlen(pseudo, sizeof pseudo));
    bool visible = rml_element_is_visible(element, true);
    hash(t, &visible, sizeof visible);
    rml_vec2 position = rml_element_absolute_offset(element, RML_BOX_BORDER),
             size = rml_element_box_size(element, RML_BOX_BORDER);
    hash(t, &position, sizeof position);
    hash(t, &size, sizeof size);
    int children = rml_element_child_count(element);
    hash(t, &children, sizeof children);
    for (int i = 0; i < children; i++) {
        if (t->visited == NODES_MAX) {
            t->complete = false;
            break;
        }
        walk(t, rml_element_child(element, i), index, depth + 1);
    }
}
static ui_dom_status collect(ui_dom *d, tree *t)
{
    *t = (tree){ .service = d,
                 .nodes = xcalloc(NODES_MAX, sizeof(node)),
                 .complete = true,
                 .hash = UINT64_C(14695981039346656037) };
    if (!d->context) {
        return (ui_dom_status){ .error = "No live UI context is bound" };
    }
    for (int i = 0; i < NODES_MAX; i++) {
        if (d->references[i].ref) {
            d->references[i].seen = false;
            rml_element *element = rml_element_ref_get(d->references[i].ref);
            if (!element || rml_element_context(element) != d->context) {
                drop(&d->references[i]);
                continue;
            }
            unsigned at = bucket(element);
            while (t->buckets[at]) {
                at = (at + 1) & (NODES_MAX * 2 - 1);
            }
            t->buckets[at] = i + 1;
        }
    }
    int count = rml_context_document_count(d->context);
    for (int i = 0; i < count; i++) {
        if (t->visited == NODES_MAX) {
            t->complete = false;
            break;
        }
        walk(t, rml_context_document(d->context, i), -1, 0);
    }
    for (int i = 0; i < NODES_MAX; i++) {
        if (d->references[i].ref && !d->references[i].seen) {
            drop(&d->references[i]);
        }
    }
    if (d->hash != t->hash) {
        if (d->revision == INT_MAX) {
            return (ui_dom_status){ .error = "DOM revision exhausted" };
        }
        d->hash = t->hash;
        d->revision++;
    }
    return (ui_dom_status){ .ok = true,
                            .complete = t->complete,
                            .revision = d->revision,
                            .total = t->count,
                            .error = t->complete ? "" : "DOM tree exceeds node/depth/key budget" };
}
ui_dom *ui_dom_new()
{
    ui_dom *d = xcalloc(1, sizeof(ui_dom));
    d->selected = -1;
    return d;
}
void ui_dom_free(ui_dom *d)
{
    if (!d) {
        return;
    }
    for (int i = 0; i < NODES_MAX; i++) {
        drop(&d->references[i]);
    }
    free(d);
}
static void bind_context(ui_dom *d, rml_context *context)
{
    assert(d);
    if (d->context != context) {
        for (int i = 0; i < NODES_MAX; i++) {
            drop(&d->references[i]);
        }
        d->context = context;
        d->hash = 0;
    }
}
static int context_index(const ui_dom *d, const char *key)
{
    for (int i = 0; i < 16; i++) {
        if (d->contexts[i].context && !strcmp(d->contexts[i].key, key)) {
            return i;
        }
    }
    return -1;
}
bool ui_dom_register(ui_dom *d, const char *key, rml_context *context)
{
    assert(d);
    if (!context || !key || !*key || strlen(key) >= sizeof d->contexts[0].key) {
        return false;
    }
    int index = context_index(d, key);
    if (index < 0) {
        for (int i = 0; i < 16; i++) {
            if (!d->contexts[i].context) {
                index = i;
                break;
            }
        }
    }
    if (index < 0) {
        return false;
    }
    if (d->contexts[index].context == context) {
        return true;
    }
    if (d->context_revision == INT_MAX) {
        return false;
    }
    /* Restore forced states while the replaced context is still alive. */
    if (d->selected == index) {
        bind_context(d, context);
    }
    snprintf(d->contexts[index].key, sizeof d->contexts[index].key, "%s", key);
    d->contexts[index].context = context;
    d->context_revision++;
    if (d->selected < 0) {
        d->selected = index;
        bind_context(d, context);
    }
    return true;
}
void ui_dom_unregister(ui_dom *d, const char *key)
{
    assert(d && key);
    int index = context_index(d, key);
    if (index < 0) {
        return;
    }
    assert(d->context_revision < INT_MAX);
    d->context_revision++;
    if (d->selected == index) {
        bind_context(d, NULL);
        d->selected = -1;
    }
    memset(&d->contexts[index], 0, sizeof d->contexts[index]);
    if (d->selected < 0) {
        int application = context_index(d, "application");
        if (application >= 0) {
            d->selected = application;
            bind_context(d, d->contexts[application].context);
        }
    }
}
void ui_dom_context(ui_dom *d, rml_context *context)
{
    if (context) {
        bool registered = ui_dom_register(d, "application", context);
        assert(registered);
        (void)registered;
        return;
    }
    ui_dom_unregister(d, "application");
}
ui_dom_status ui_dom_select(ui_dom *d, const char *key)
{
    assert(d);
    int index = key ? context_index(d, key) : -1;
    if (index < 0) {
        return (ui_dom_status){ .error = "Named UI context is unavailable" };
    }
    if (index != d->selected && d->context_revision == INT_MAX) {
        return (ui_dom_status){ .error = "UI context revision exhausted" };
    }
    d->context_revision += index != d->selected;
    d->selected = index;
    bind_context(d, d->contexts[index].context);
    return (
        ui_dom_status
    ){ .ok = true, .complete = true, .revision = d->context_revision, .error = "" };
}
static const char *copy(arena *a, const char *text)
{
    if (!text) {
        text = "";
    }
    size_t bytes = strlen(text) + 1;
    char *out = arena_alloc(a, bytes);
    memcpy(out, text, bytes);
    return out;
}
ui_dom_status ui_dom_contexts(ui_dom *d, arena *a, pin_array **rows)
{
    assert(d && a && rows);
    int count = 0;
    for (int i = 0; i < 16; i++) {
        count += d->contexts[i].context != NULL;
    }
    *rows = pin_array_snapshot_allocate(a, PIN_STRUCT, "UiDomContext", count);
    int out = 0;
    /* Names, not registration order, define the deterministic query order. */
    int order[16];
    for (int i = 0; i < 16; i++) {
        if (!d->contexts[i].context) {
            continue;
        }
        int at = out++;
        while (at && strcmp(d->contexts[order[at - 1]].key, d->contexts[i].key) > 0) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = i;
    }
    for (int i = 0; i < count; i++) {
        int index = order[i];
        ui_dom_context_row *row = arena_alloc(a, sizeof *row);
        *row = (ui_dom_context_row){ copy(a, d->contexts[index].key),
                                     d->selected == index };
        (*rows)->items[i] = (pin_value){ .type = PIN_STRUCT,
                                         .type_name = "UiDomContext",
                                         .ptr = row };
    }
    return (ui_dom_status){ .ok = true,
                            .complete = true,
                            .revision = d->context_revision,
                            .total = count,
                            .error = "" };
}
static const char *bounded_copy(arena *a, const char *text, ui_dom_status *status)
{
    size_t size = strnlen(text, VALUE_MAX + 1);
    if (size > VALUE_MAX) {
        size = VALUE_MAX;
        status->complete = false;
    }
    char *out = arena_alloc(a, size + 1);
    memcpy(out, text, size);
    out[size] = 0;
    return out;
}
static int source_integer(const char *text, int limit)
{
    if (!text[0]) {
        return -1;
    }
    int value = 0;
    for (int i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9' || value > (limit - (text[i] - '0')) / 10) {
            return -1;
        }
        value = value * 10 + text[i] - '0';
    }
    return value;
}
static void source_origin(const tree *t, int index, arena *a, ui_dom_row *out)
{
    out->source_asset = "";
    out->source_key = "";
    out->source_start = -1;
    /* The collected ancestry is bounded and owns exact parent indices. */
    for (int at = index; at >= 0; at = t->nodes[at].parent) {
        rml_element *element = t->nodes[at].element;
        char source[VALUE_MAX], start[32];
        if (rml_element_attribute(element, "data-src", source, sizeof source) >=
                sizeof source ||
            !source[0]) {
            continue;
        }
        char *colon = strrchr(source, ':');
        if (!colon) {
            continue;
        }
        int line = source_integer(colon + 1, INT_MAX);
        if (line < 1) {
            continue;
        }
        *colon = 0;
        out->source_asset = copy(a, source);
        out->source_key = copy(a, t->nodes[at].key);
        out->source_line = line;
        if (rml_element_attribute(element, "data-source-start", start, sizeof start) <
            sizeof start) {
            out->source_start = source_integer(start, 1024 * 1024);
        }
        return;
    }
}
static void box_edges(
    rml_element *element,
    rml_box_area outer,
    rml_box_area inner,
    double *top,
    double *right,
    double *bottom,
    double *left
)
{
    rml_vec2 outer_position = rml_element_relative_offset(element, outer),
             inner_position = rml_element_relative_offset(element, inner);
    rml_vec2 outer_size = rml_element_box_size(element, outer),
             inner_size = rml_element_box_size(element, inner);
    *top = inner_position.y - outer_position.y;
    *left = inner_position.x - outer_position.x;
    *right = outer_position.x + outer_size.x - inner_position.x - inner_size.x;
    *bottom = outer_position.y + outer_size.y - inner_position.y - inner_size.y;
}
static void describe(const tree *t, int index, arena *a, ui_dom_row *out, ui_dom_status *status)
{
    const node *n = &t->nodes[index];
    rml_element *e = n->element;
    char classes[VALUE_MAX], text[VALUE_MAX] = "";
    if (rml_element_class_names(e, classes, sizeof classes) >= sizeof classes) {
        status->complete = false;
    }
    const char *tag = rml_element_tag_name(e);
    if (!strcmp(tag, "#text") && rml_element_text(e, text, sizeof text) >= sizeof text) {
        status->complete = false;
    }
    rml_vec2 position = rml_element_absolute_offset(e, RML_BOX_BORDER),
             size = rml_element_box_size(e, RML_BOX_BORDER);
    *out = (ui_dom_row){
        .key = copy(a, n->key),
        .parent = copy(a, n->parent < 0 ? "" : t->nodes[n->parent].key),
        .tag = bounded_copy(a, tag, status),
        .id = bounded_copy(a, rml_element_id(e), status),
        .classes = copy(a, classes),
        .text = copy(a, text),
        .depth = n->depth,
        .x = position.x,
        .y = position.y,
        .width = size.x,
        .height = size.y,
        .visible = rml_element_is_visible(e, true)
    };
    rml_vec2 content = rml_element_box_size(e, RML_BOX_CONTENT);
    out->content_width = content.x;
    out->content_height = content.y;
    out->dp_ratio = rml_context_dp_ratio(rml_element_context(e));
    box_edges(
        e,
        RML_BOX_MARGIN,
        RML_BOX_BORDER,
        &out->margin_top,
        &out->margin_right,
        &out->margin_bottom,
        &out->margin_left
    );
    box_edges(
        e,
        RML_BOX_BORDER,
        RML_BOX_PADDING,
        &out->border_top,
        &out->border_right,
        &out->border_bottom,
        &out->border_left
    );
    box_edges(
        e,
        RML_BOX_PADDING,
        RML_BOX_CONTENT,
        &out->padding_top,
        &out->padding_right,
        &out->padding_bottom,
        &out->padding_left
    );
    source_origin(t, index, a, out);
    if (!status->complete && !status->error[0]) {
        status->error = "DOM field exceeds retained text budget";
    }
}
static int locate(tree *t, const char *key)
{
    if (!key || strlen(key) >= KEY_MAX) {
        return -1;
    }
    for (int i = 0; i < t->count; i++) {
        if (!strcmp(key, t->nodes[i].key)) {
            return i;
        }
    }
    return -1;
}
ui_dom_status ui_dom_query(ui_dom *d, int first, int count, arena *a, pin_array **rows)
{
    assert(d && a && rows);
    *rows = pin_array_snapshot_allocate(a, PIN_STRUCT, "UiDomRow", 0);
    if (first < 0 || count < 0 || count > 256) {
        return (ui_dom_status){
            .error = "DOM page must have nonnegative first and count <=256"
        };
    }
    tree t;
    ui_dom_status result = collect(d, &t);
    if (result.ok) {
        int take = first < t.count ? t.count - first : 0;
        if (take > count) {
            take = count;
        }
        *rows = pin_array_snapshot_allocate(a, PIN_STRUCT, "UiDomRow", take);
        for (int i = 0; i < take; i++) {
            ui_dom_row *row = arena_alloc(a, sizeof *row);
            describe(&t, first + i, a, row, &result);
            (*rows)->items[i] = (pin_value){ .type = PIN_STRUCT,
                                             .type_name = "UiDomRow",
                                             .ptr = row };
        }
    }
    free(t.nodes);
    return result;
}
typedef struct
{
    arena *storage;
    pin_array *rows;
    int first, count, total;
    bool complete;
} properties;
static void property(void *userdata, const rml_property_info *p)
{
    properties *s = userdata;
    if (s->total == 4096) {
        s->complete = false;
        return;
    }
    int index = s->total++;
    if (index < s->first || s->rows->count >= s->count) {
        return;
    }
    if (strlen(p->value) > VALUE_MAX || strlen(p->name) > 256 ||
        strlen(p->path) > VALUE_MAX || strlen(p->rule) > VALUE_MAX) {
        s->complete = false;
        return;
    }
    ui_dom_property *out = arena_alloc(s->storage, sizeof *out);
    *out = (ui_dom_property){ copy(s->storage, p->name),
                              copy(s->storage, p->value),
                              copy(s->storage, p->path),
                              copy(s->storage, p->rule),
                              p->line,
                              p->specificity };
    s->rows->items[s->rows->count++] = (pin_value){
        .type = PIN_STRUCT,
        .type_name = "UiDomProperty",
        .ptr = out
    };
}
static void attribute(void *userdata, const char *name, const char *value)
{
    rml_property_info p = { .name = name, .value = value, .path = "", .rule = "" };
    property(userdata, &p);
}
ui_dom_status ui_dom_inspect(
    ui_dom *d,
    const char *key,
    const char *mode,
    int first,
    int count,
    arena *a,
    pin_array **rows,
    ui_dom_row *selected
)
{
    assert(d && a && rows);
    if (selected) {
        *selected = (ui_dom_row){ .key = "",
                                  .parent = "",
                                  .tag = "",
                                  .id = "",
                                  .classes = "",
                                  .text = "",
                                  .source_asset = "",
                                  .source_key = "",
                                  .source_start = -1 };
    }
    *rows = pin_array_snapshot_allocate(a, PIN_STRUCT, "UiDomProperty", 0);
    if (!mode || first < 0 || count < 0 || count > 256) {
        return (ui_dom_status){ .error = "DOM property page is invalid" };
    }
    tree t;
    ui_dom_status result = collect(d, &t);
    int index = locate(&t, key);
    if (result.ok && index < 0) {
        result.ok = false;
        result.error = "DOM key is absent";
    }
    if (result.ok) {
        properties s = {
            .storage = a,
            .rows = pin_array_snapshot_allocate(a, PIN_STRUCT, "UiDomProperty", count),
            .first = first,
            .count = count,
            .complete = true
        };
        s.rows->count = 0;
        rml_element *e = t.nodes[index].element;
        if (selected) {
            describe(&t, index, a, selected, &result);
        }
        if (!strcmp(mode, "attributes")) {
            rml_element_attributes(e, attribute, &s);
        } else if (!strcmp(mode, "declared")) {
            rml_element_local_properties(e, NULL, property, &s);
        } else if (!strcmp(mode, "computed")) {
            for (int i = 0; i <= UINT8_MAX; i++) {
                const char *name = rml_property_name((uint8_t)i);
                if (!name) {
                    continue;
                }
                char value[VALUE_MAX], path[VALUE_MAX] = "";
                int line = 0;
                if (rml_element_property(e, name, value, sizeof value) >= sizeof value) {
                    s.complete = false;
                    continue;
                }
                rml_element_property_source(e, name, path, sizeof path, &line);
                rml_property_info p = { .name = name,
                                        .value = value,
                                        .path = path,
                                        .line = line,
                                        .rule = "" };
                property(&s, &p);
            }
        } else {
            result.ok = false;
            result.error = "DOM mode must be attributes, declared or computed";
        }
        *rows = s.rows;
        result.total = s.total;
        result.complete = result.complete && s.complete;
        if (!s.complete) {
            result.error = "DOM properties exceed count/text budget";
        }
    }
    free(t.nodes);
    return result;
}
ui_dom_status ui_dom_pick(ui_dom *d, double x, double y, const char *ignore_selector, arena *a, ui_dom_row *row)
{
    assert(d && a && row);
    *row = (ui_dom_row){ .key = "",
                         .parent = "",
                         .tag = "",
                         .id = "",
                         .classes = "",
                         .text = "",
                         .source_asset = "",
                         .source_key = "",
                         .source_start = -1 };
    if (!isfinite(x) || !isfinite(y) || fabs(x) > 1000000 ||
        fabs(y) > 1000000 || !ignore_selector || strlen(ignore_selector) > 256) {
        return (ui_dom_status){ .error = "Pointer coordinates are invalid" };
    }
    tree t;
    ui_dom_status result = collect(d, &t);
    if (result.ok) {
        rml_element *ignore = ignore_selector[0]
                                ? rml_element_query_selector(rml_context_root(d->context), ignore_selector)
                                : NULL;
        rml_element *picked = rml_context_element_at_point(
            d->context,
            (rml_vec2){ (float)x, (float)y },
            ignore
        );
        int index = -1;
        for (int i = 0; i < t.count; i++) {
            if (t.nodes[i].element == picked) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            result.ok = false;
            result.error = "No inspected element at the point";
        } else {
            describe(&t, index, a, row, &result);
        }
    }
    free(t.nodes);
    return result;
}
ui_dom_status ui_dom_pseudo(ui_dom *d, const char *key, const char *pseudo, bool on)
{
    if (!pseudo || (strcmp(pseudo, "hover") && strcmp(pseudo, "active") &&
                    strcmp(pseudo, "focus") && strcmp(pseudo, "checked"))) {
        return (ui_dom_status){
            .error = "Expected hover, active, focus or checked pseudo state"
        };
    }
    tree t;
    ui_dom_status result = collect(d, &t);
    int index = locate(&t, key);
    if (result.ok && index < 0) {
        result.ok = false;
        result.error = "DOM key is absent";
    }
    if (result.ok) {
        reference *ref = &d->references[t.nodes[index].reference];
        int flag = 0;
        while (strcmp(pseudo, PSEUDOS[flag])) {
            flag++;
        }
        unsigned bit = 1u << flag;
        if (on && !(ref->forced & bit)) {
            if (rml_element_is_pseudo_class_set(t.nodes[index].element, pseudo)) {
                ref->original |= bit;
            } else {
                ref->original &= ~bit;
            }
        }
        if (on) {
            ref->forced |= bit;
        } else {
            ref->forced &= ~bit;
        }
        rml_element_set_pseudo_class(t.nodes[index].element, pseudo, on);
    }
    free(t.nodes);
    return result;
}

ui_dom_status ui_dom_unit(ui_dom *d, const char *key, const char *unit, const char *axis, double *pixels)
{
    assert(d && pixels);
    *pixels = 0;
    bool horizontal = axis && !strcmp(axis, "x");
    if (!unit || (!horizontal && (!axis || strcmp(axis, "y")))) {
        return (ui_dom_status){ .error = "Length unit requires axis x/y" };
    }
    static const char *const names[] = { "px", "dp", "em", "rem", "vw", "vh",
                                         "in", "cm", "mm", "pt",  "pc" };
    bool percentage = !strcmp(unit, "%");
    int type = -1;
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        if (!strcmp(unit, names[i])) {
            type = i;
        }
    }
    if (!percentage && type < 0) {
        return (ui_dom_status){
            .error = "Only literal CSS length units can be resolved"
        };
    }
    tree t;
    ui_dom_status status = collect(d, &t);
    int index = locate(&t, key);
    if (status.ok && index < 0) {
        status = (ui_dom_status){
            .error = "Element key is retired or unavailable"
        };
    }
    if (status.ok) {
        rml_element *element = t.nodes[index].element;
        if (percentage) {
            rml_vec2 base = rml_element_containing_block(element);
            *pixels = (horizontal ? base.x : base.y) / 100.0;
        } else {
            *pixels = rml_element_unit_pixels(element, (rml_length_unit)type);
        }
        if (!isfinite(*pixels) || *pixels <= 0) {
            *pixels = 0;
            status = (ui_dom_status){
                .error = "Length unit has no positive resolved scale"
            };
        }
    }
    free(t.nodes);
    return status;
}

bool ui_dom_measure_text(
    ui_dom *d,
    const char *key,
    const char *selector,
    const char *text,
    float size,
    int weight,
    float *width
)
{
    *width = 0;
    if (!key || !selector || !*selector || !text || !isfinite(size) ||
        size <= 0 || size > 4096 || weight < 1 || weight > 1000) {
        return false;
    }
    int index = context_index(d, key);
    if (index < 0) {
        return false;
    }
    rml_context *context = d->contexts[index].context;
    float dp = rml_context_dp_ratio(context);
    if (!isfinite(dp) || dp <= 0 || size * dp > 4096) {
        return false;
    }
    rml_element *element = NULL;
    for (int i = 0; !element && i < rml_context_document_count(context); i++) {
        element = rml_element_query_selector(rml_context_document(context, i), selector);
    }
    if (!element) {
        return false;
    }
    char family[256], style[32];
    if (rml_element_property(element, "font-family", family, sizeof family) >=
        sizeof family) {
        return false;
    }
    /* RmlUi stores font-provider keys lowercased; computed font-family retains
     * the author's spelling. Match ElementStyle's lookup normalization. */
    for (char *c = family; *c; c++) {
        *c = (char)tolower((unsigned char)*c);
    }
    rml_element_property(element, "font-style", style, sizeof style);
    int pixels = (int)(size * dp);
    if (pixels < 1) {
        return false;
    }
    uintptr_t face = rml_font_face(
        family,
        !strcmp(style, "italic") ? RML_FONT_ITALIC : RML_FONT_NORMAL,
        weight,
        pixels
    );
    if (!face) {
        return false;
    }
    *width = rml_font_string_width(face, text) / dp;
    return true;
}
