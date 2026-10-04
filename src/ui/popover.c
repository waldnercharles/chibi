/* Popovers (popover.h). A context's open popovers form a chain in `stack`,
 * each opened from the one below it: showing one closes those above the one
 * it opened from. Listeners on each context's root element hear every
 * event before the context's documents do. */
#include "ui/popover.h"

#include "support/array.h"
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"
#include "ui/motion.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Base UI's timings, which the kit's menus kept, in seconds. */
#define SUBMENU_DELAY 0.1
#define TYPEAHEAD_RESET 0.5
/* In dp: from the anchor, from the window's edges, and a submenu above its
   row, which aligns their first items. */
#define GAP 2
#define MARGIN 4
#define SUBMENU_LIFT 4
/* A document holding an open popover stacks at this z-index: above the
   areas, whose documents leave it auto, and below the kit's overlay layer
   (10), whichever document has focus. */
#define RAISED 9
#define TYPED_MAX 32
#define LABEL_MAX 256

typedef struct entry
{
    rml_element_ref *el, *anchor, *trigger;
    rml_context *context;
    popover_side side;
    float box[4];
    float left, top;        /* the offset set, in pixels */
    float anchor_width;     /* --anchor-width, in pixels, as Base UI sets it */
    float available_height; /* --available-height, in pixels */
    bool managed; /* it has the popover attribute: shown and hidden here */
    bool placed, focus_pending, keyboard;
    rml_element_ref *sub; /* the submenu row the pointer rests on */
    double sub_since;
    char typed[TYPED_MAX];
    double typed_at;
} entry;

/* A managed popover fading out. */
typedef struct leaving
{
    rml_element_ref *el;
    double until;
} leaving;

/* A document stacked above the others, and the z-index it had. */
typedef struct raised
{
    rml_element_ref *document;
    char z[16];
} raised;

static entry *stack;
static leaving *leavings;
static raised *raises;
static rml_element_ref **roots;
static rml_listener *listener;
static bool key_input; /* the last press was a key's */
/* A trigger a key opens through its owner, whose popover highlights first. */
static rml_element_ref *key_trigger;

/* ---------- Elements ---------- */

static rml_element *get(const rml_element_ref *ref)
{
    return ref ? rml_element_ref_get(ref) : NULL;
}

static rml_element_ref *ref(rml_element *el)
{
    return el ? rml_element_ref_create(el) : NULL;
}

static void unref(rml_element_ref **r)
{
    if (*r) {
        rml_element_ref_destroy(*r);
        *r = NULL;
    }
}

static bool attribute_is(const rml_element *el, const char *name, const char *value)
{
    char text[32];
    control_string(el, name, text, sizeof text);
    return !strcmp(text, value);
}

static bool is_item(const rml_element *el)
{
    char role[32];
    control_string(el, "role", role, sizeof role);
    return !strncmp(role, "menuitem", 8) || !strcmp(role, "option");
}

static bool is_menu(const rml_element *el)
{
    return el && attribute_is(el, "role", "menu");
}

/* Menus and lists keep the focus where it was; other popovers take it. */
static bool takes_focus(const rml_element *el)
{
    return !is_menu(el) && !attribute_is(el, "role", "listbox");
}

static bool has_popup(const rml_element *el)
{
    return rml_element_has_attribute(el, "aria-haspopup") &&
           !attribute_is(el, "aria-haspopup", "false");
}

static bool enabled(const rml_element *el)
{
    return !rml_element_has_attribute(el, "data-disabled") &&
           !attribute_is(el, "aria-disabled", "true");
}

/* Base UI's checkbox and radio items keep their menu open. */
static bool stays_open(const rml_element *el)
{
    return attribute_is(el, "role", "menuitemcheckbox") ||
           attribute_is(el, "role", "menuitemradio");
}

/* A field that edits text keeps its keys. */
static bool editing(const rml_element *el)
{
    const char *tag = rml_element_tag_name(el);
    if (!strcmp(tag, "textarea") || !strcmp(tag, "select")) {
        return true;
    }
    if (strcmp(tag, "input")) {
        return false;
    }
    static const char *const BUTTONS[] = { "checkbox", "radio", "button", "submit", "range" };
    for (size_t i = 0; i < sizeof BUTTONS / sizeof *BUTTONS; i++) {
        if (attribute_is(el, "type", BUTTONS[i])) {
            return false;
        }
    }
    return true;
}

static bool in_document(const rml_element *el)
{
    return el && rml_element_owner_document(el) != el;
}

/* el or its nearest ancestor that `matches`, within its document. */
static rml_element *closest(rml_element *el, bool (*matches)(const rml_element *))
{
    for (; in_document(el); el = rml_element_parent(el)) {
        if (matches(el)) {
            return el;
        }
    }
    return NULL;
}

static bool invokes(const rml_element *el)
{
    return rml_element_has_attribute(el, "popovertarget");
}

static bool opens_popup(const rml_element *el)
{
    return invokes(el) || has_popup(el);
}

static bool is_menubar(const rml_element *el)
{
    return !strcmp(rml_element_tag_name(el), "menubar");
}

static bool is_owner(const rml_element *el)
{
    return is_menu(el) || attribute_is(el, "role", "listbox") ||
           rml_element_has_attribute(el, "popover");
}

/* The menu or list an item belongs to: its nearest owning ancestor. */
static rml_element *owner_of(rml_element *item)
{
    return closest(rml_element_parent(item), is_owner);
}

/* The popover `invoker` names, looked up from the invoker outward. */
static rml_element *target_of(rml_element *invoker)
{
    char id[64], selector[72];
    control_string(invoker, "popovertarget", id, sizeof id);
    if (!id[0]) {
        return NULL;
    }
    snprintf(selector, sizeof selector, "#%s", id);
    for (rml_element *el = invoker; el; el = rml_element_parent(el)) {
        rml_element *found = rml_element_query_selector(el, selector);
        if (found) {
            return found;
        }
    }
    return NULL;
}

/* An item's label for typeahead: its .label's text, else its own. */
static void label_of(rml_element *item, char *out, size_t size)
{
    rml_element *label = rml_element_query_selector(item, ".label");
    control_text_of(label ? label : item, out, size);
}

/* ---------- The stack ---------- */

static int find(const rml_element *el)
{
    for (int i = 0; el && i < acount(stack); i++) {
        if (get(stack[i].el) == el) {
            return i;
        }
    }
    return -1;
}

/* The open popover holding `el`, or -1. */
static int holding(rml_element *el)
{
    for (; el; el = rml_element_parent(el)) {
        int i = find(el);
        if (i >= 0) {
            return i;
        }
    }
    return -1;
}

static int top_of(const rml_context *context)
{
    for (int i = acount(stack) - 1; i >= 0; i--) {
        if (stack[i].context == context) {
            return i;
        }
    }
    return -1;
}

static int bottom_of(const rml_context *context)
{
    for (int i = 0; i < acount(stack); i++) {
        if (stack[i].context == context) {
            return i;
        }
    }
    return -1;
}

/* The popover entry i opened from, or -1. */
static int below(int i)
{
    for (int j = i - 1; j >= 0; j--) {
        if (stack[j].context == stack[i].context) {
            return j;
        }
    }
    return -1;
}

static int above(int i)
{
    for (int j = i + 1; j < acount(stack); j++) {
        if (stack[j].context == stack[i].context) {
            return j;
        }
    }
    return -1;
}

static bool submenu_open(const rml_element *row)
{
    for (int i = 0; i < acount(stack); i++) {
        if (get(stack[i].trigger) == row) {
            return true;
        }
    }
    return false;
}

/* ---------- Stacking above the other documents ---------- */

static bool holds_popover(const rml_element *document)
{
    for (int i = 0; i < acount(stack); i++) {
        rml_element *el = get(stack[i].el);
        if (el && rml_element_owner_document(el) == document) {
            return true;
        }
    }
    for (int i = 0; i < acount(leavings); i++) {
        rml_element *el = get(leavings[i].el);
        if (el && rml_element_owner_document(el) == document) {
            return true;
        }
    }
    return false;
}

static void raise_document(rml_element *document)
{
    for (int i = 0; i < acount(raises); i++) {
        if (get(raises[i].document) == document) {
            return;
        }
    }
    raised r = { .document = ref(document) };
    rml_element_property(document, "z-index", r.z, sizeof r.z);
    if (strcmp(r.z, "auto") && atoi(r.z) >= RAISED) {
        unref(&r.document);
        return;
    }
    apush(raises, r);
    char z[16];
    snprintf(z, sizeof z, "%d", RAISED);
    rml_element_set_property(document, "z-index", z);
    rml_document_update(document);
}

/* Documents take their z-index back once nothing they hold is open. */
static void lower_documents()
{
    for (int i = 0; i < acount(raises);) {
        rml_element *document = get(raises[i].document);
        if (document && holds_popover(document)) {
            i++;
            continue;
        }
        if (document && !strcmp(raises[i].z, "auto")) {
            rml_element_remove_property(document, "z-index");
        } else if (document) {
            rml_element_set_property(document, "z-index", raises[i].z);
        }
        if (document) {
            rml_document_update(document);
        }
        unref(&raises[i].document);
        adel(raises, i);
    }
}

/* ---------- Mounting ---------- */

/* A popover with data-template builds its content while it shows, as Base
   UI's Popup is mounted only while open: RmlUi leaves the content unbuilt
   with its markup in rmlui-inner-rml, showing builds it and the end of its
   fade removes it with its views, so a closed menu holds no elements and
   binds nothing. */
static bool has_template(const rml_element *el)
{
    return rml_element_has_attribute(el, "data-template");
}

static void mount(rml_element *el)
{
    if (!has_template(el) || rml_element_child_count(el)) {
        return;
    }
    size_t size = rml_element_attribute(el, "rmlui-inner-rml", NULL, 0);
    char *markup = xmalloc(size + 1);
    rml_element_attribute(el, "rmlui-inner-rml", markup, size + 1);
    rml_element_set_inner_rml(el, markup);
    free(markup);
}

static void unmount(rml_element *el)
{
    if (has_template(el)) {
        rml_element_set_inner_rml(el, "");
    }
}

/* ---------- Showing and hiding ---------- */

static void appear(entry *e, rml_element *el)
{
    rml_element_set_property(el, "left", "0px");
    rml_element_set_property(el, "top", "0px");
    rml_element_set_attribute(el, "data-unplaced", "");
    if (!e->managed) {
        return;
    }
    for (int i = 0; i < acount(leavings); i++) {
        if (get(leavings[i].el) == el) {
            unref(&leavings[i].el);
            adel(leavings, i);
            break;
        }
    }
    rml_element_set_attribute(el, "data-open", "");
    rml_element_remove_attribute(el, "data-ending-style");
    rml_element_remove_property(el, "pointer-events");
    rml_element_set_property(el, "position", "fixed");
    rml_element_set_property(el, "clip", "none");
    if (!motion_reduced(el)) {
        rml_element_set_attribute(el, "data-starting-style", "");
    }
}

static void unhighlight(rml_element *menu)
{
    int count = rml_element_query_selector_all(menu, "[data-highlighted]", NULL, 0);
    rml_element **lit = xmalloc((size_t)(count + 1) * sizeof *lit);
    rml_element_query_selector_all(menu, "[data-highlighted]", lit, count);
    for (int i = 0; i < count; i++) {
        if (owner_of(lit[i]) == menu) {
            rml_element_remove_attribute(lit[i], "data-highlighted");
        }
    }
    free(lit);
}

/* It fades out as the theme's exit transition says, then hides. */
static void fade(rml_element *el)
{
    unhighlight(el);
    rml_element_remove_attribute(el, "data-starting-style");
    rml_element_set_attribute(el, "data-ending-style", "");
    rml_element_set_property(el, "pointer-events", "none");
    double time = motion_reduced(el) ? 0 : motion_transition_time(el);
    apush(leavings, ((leaving){ ref(el), rml_elapsed_time() + time }));
    rml_context_request_next_update(rml_element_context(el), time);
}

static void drop(int i)
{
    entry *e = &stack[i];
    rml_element *el = get(e->el);
    if (el && e->managed) {
        fade(el);
    }
    rml_element *trigger = get(e->trigger);
    if (trigger) {
        rml_element_remove_attribute(trigger, "data-popup-open");
    }
    unref(&e->el);
    unref(&e->anchor);
    unref(&e->trigger);
    unref(&e->sub);
    adel(stack, i);
}

/* Closes entry i and those above it; focus inside them returns to its
   trigger with `refocus`. */
static void close_at(int i, bool refocus)
{
    rml_context *context = stack[i].context;
    rml_element *focus = rml_context_focus_element(context);
    rml_element *trigger = get(stack[i].trigger);
    bool inside = false;
    for (int j = acount(stack) - 1; j >= i; j--) {
        if (stack[j].context != context) {
            continue;
        }
        rml_element *el = get(stack[j].el);
        inside |= el && focus && control_within(focus, el);
        drop(j);
    }
    if (refocus && inside && trigger) {
        rml_element_focus(trigger, true);
    }
    lower_documents();
}

/* Closes the popover an item activated in and the menus it opened from. */
static void close_chain(int i)
{
    if (is_menu(get(stack[i].el))) {
        for (int j = below(i); j >= 0 && is_menu(get(stack[j].el)); j = below(j)) {
            i = j;
        }
    }
    close_at(i, true);
}

/* --anchor-width gives the anchor's width, as Base UI's does, so a list can
   be as wide as its field; true when it changed. */
static bool follow_width(entry *e, rml_element *el)
{
    rml_element *anchor = get(e->anchor);
    float width = anchor ? rml_element_box_size(anchor, RML_BOX_BORDER).x : 0;
    if (!anchor || width == e->anchor_width) {
        return false;
    }
    char value[32];
    snprintf(value, sizeof value, "%.0fpx", width);
    rml_element_set_property(el, "--anchor-width", value);
    e->anchor_width = width;
    return true;
}

/* --available-height gives the window's height less its margins, as Base
   UI's gives the room beside the anchor, so a menu taller than the window
   scrolls; true when it changed. */
static bool follow_height(entry *e, rml_element *el)
{
    rml_element *root = rml_context_root(e->context);
    float height = rml_element_box_size(root, RML_BOX_BORDER).y -
                   2 * MARGIN * control_dp(el);
    if (height == e->available_height) {
        return false;
    }
    char value[32];
    snprintf(value, sizeof value, "%.0fpx", height);
    rml_element_set_property(el, "--available-height", value);
    e->available_height = height;
    return true;
}

void popover_show(rml_element *popover, const popover_anchor *at)
{
    if (find(popover) >= 0) {
        return;
    }
    rml_context *context = rml_element_context(popover);
    int parent = -1;
    for (int i = top_of(context); i >= 0 && parent < 0; i--) {
        rml_element *el = stack[i].context == context ? get(stack[i].el) : NULL;
        bool from = el && (control_within(popover, el) ||
                           (at->trigger && control_within(at->trigger, el)));
        parent = from ? i : -1;
    }
    int first = parent < 0 ? bottom_of(context) : above(parent);
    if (first >= 0) {
        close_at(first, false);
    }
    bool keyed = at->trigger && get(key_trigger) == at->trigger;
    if (keyed) {
        unref(&key_trigger);
    }
    entry e = {
        .el = ref(popover),
        .anchor = ref(at->element),
        .trigger = ref(at->trigger),
        .context = context,
        .side = at->side,
        .managed = rml_element_has_attribute(popover, "popover"),
        .focus_pending = takes_focus(popover),
        .keyboard = at->keyboard || keyed,
    };
    memcpy(e.box, at->box, sizeof e.box);
    apush(stack, e);
    if (at->trigger) {
        rml_element_set_attribute(at->trigger, "data-popup-open", "");
    }
    appear(&alast(stack), popover);
    follow_width(&alast(stack), popover);
    raise_document(rml_element_owner_document(popover));
    mount(popover);
    rml_context_request_next_update(context, 0);
}

void popover_hide(rml_element *popover)
{
    int i = find(popover);
    if (i >= 0) {
        close_at(i, false);
    }
}

bool popover_open(const rml_element *popover)
{
    return find(popover) >= 0;
}

rml_element *popover_top(rml_context *context)
{
    int i = top_of(context);
    return i >= 0 ? get(stack[i].el) : NULL;
}

void popover_hide_all(rml_context *context)
{
    int i = bottom_of(context);
    if (i >= 0) {
        close_at(i, false);
    }
}

/* ---------- Placement ---------- */

static void anchor_box(const entry *e, float box[4])
{
    rml_element *anchor = get(e->anchor);
    if (!anchor) {
        memcpy(box, e->box, sizeof e->box);
        return;
    }
    rml_vec2 at = rml_element_absolute_offset(anchor, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(anchor, RML_BOX_BORDER);
    box[0] = at.x;
    box[1] = at.y;
    box[2] = size.x;
    box[3] = size.y;
}

/* Where the border box goes, before it is kept inside the window. */
static rml_vec2 origin(const entry *e, rml_vec2 size, rml_vec2 window, float dp)
{
    float a[4], gap = GAP * dp, edge = window.x - MARGIN * dp;
    anchor_box(e, a);
    switch (e->side) {
        case POPOVER_POINT: return (rml_vec2){ a[0], a[1] };
        case POPOVER_BESIDE: {
            float x = a[0] + a[2] + gap;
            if (x + size.x > edge) {
                x = a[0] - size.x - gap;
            }
            return (rml_vec2){ x, a[1] - SUBMENU_LIFT * dp };
        }
        case POPOVER_BELOW: break;
    }
    float y = a[1] + a[3] + gap;
    if (y + size.y > window.y - MARGIN * dp) {
        y = a[1] - size.y - gap;
    }
    return (rml_vec2){ a[0], y };
}

/* Places it in the window; true when it moved. A fixed or absolute box is
   offset from its containing block, which its last offset gives. */
static bool place(entry *e, rml_element *el)
{
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    if (size.x <= 0 || size.y <= 0) {
        return false;
    }
    float dp = control_dp(el), margin = MARGIN * dp;
    rml_vec2 window = rml_element_box_size(rml_context_root(e->context), RML_BOX_BORDER);
    rml_vec2 at = origin(e, size, window, dp);
    at.x = math_clamp(at.x, margin, math_max(margin, window.x - size.x - margin));
    at.y = math_clamp(at.y, margin, math_max(margin, window.y - size.y - margin));
    rml_vec2 now = rml_element_absolute_offset(el, RML_BOX_BORDER);
    /* A tenth of a pixel, as the kit placed its popups. */
    float left = roundf((at.x - (now.x - e->left)) * 10) / 10;
    float top = roundf((at.y - (now.y - e->top)) * 10) / 10;
    bool moved = !e->placed || left != e->left || top != e->top;
    if (moved) {
        char value[32];
        snprintf(value, sizeof value, "%.1fpx", left);
        rml_element_set_property(el, "left", value);
        snprintf(value, sizeof value, "%.1fpx", top);
        rml_element_set_property(el, "top", value);
        e->left = left;
        e->top = top;
    }
    if (!e->placed) {
        e->placed = true;
        rml_element_remove_attribute(el, "data-unplaced");
        rml_element_remove_attribute(el, "data-starting-style");
    }
    return moved;
}

/* ---------- Menus ---------- */

/* The menu's own shown items in order; free with afree. */
static rml_element **items_of(rml_element *menu)
{
    int count = rml_element_query_selector_all(menu, "[role]", NULL, 0);
    rml_element **all = xmalloc((size_t)(count + 1) * sizeof *all), **items = NULL;
    rml_element_query_selector_all(menu, "[role]", all, count);
    for (int i = 0; i < count; i++) {
        if (is_item(all[i]) && owner_of(all[i]) == menu &&
            rml_element_is_visible(all[i], true)) {
            apush(items, all[i]);
        }
    }
    free(all);
    return items;
}

static rml_element *highlighted(rml_element *menu)
{
    rml_element **items = items_of(menu), *lit = NULL;
    for (int i = 0; i < acount(items) && !lit; i++) {
        lit = rml_element_has_attribute(items[i], "data-highlighted") ? items[i] : NULL;
    }
    afree(items);
    return lit;
}

static void highlight(rml_element *menu, rml_element *item, bool reveal)
{
    if (rml_element_has_attribute(item, "data-highlighted")) {
        return;
    }
    unhighlight(menu);
    rml_element_set_attribute(item, "data-highlighted", "");
    if (reveal) {
        rml_element_scroll_into_view(item, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
    }
}

/* Highlights the next enabled item after `from` in `dir`, wrapping; with no
   `from`, the first or the last. */
static void step(rml_element *menu, rml_element *from, int dir)
{
    rml_element **items = items_of(menu);
    int count = acount(items), at = -1;
    for (int i = 0; i < count; i++) {
        at = items[i] == from ? i : at;
    }
    for (int n = 1; n <= count; n++) {
        int i = at < 0 ? (dir > 0 ? n - 1 : count - n)
                       : ((at + dir * n) % count + count) % count;
        if (enabled(items[i])) {
            highlight(menu, items[i], true);
            break;
        }
    }
    afree(items);
}

static bool starts_with(const char *text, const char *prefix)
{
    for (; *prefix; prefix++, text++) {
        if (tolower((unsigned char)*text) != *prefix) {
            return false;
        }
    }
    return true;
}

/* Highlights the first enabled item whose label starts with the letters
   typed within TYPEAHEAD_RESET of each other. */
static void typeahead(entry *e, rml_element *menu, char letter)
{
    double now = rml_elapsed_time();
    size_t length = now - e->typed_at < TYPEAHEAD_RESET ? strlen(e->typed) : 0;
    if (length + 1 < TYPED_MAX) {
        e->typed[length] = letter;
        e->typed[length + 1] = '\0';
    }
    e->typed_at = now;
    rml_element **items = items_of(menu);
    for (int i = 0; i < acount(items); i++) {
        char label[LABEL_MAX];
        label_of(items[i], label, sizeof label);
        if (enabled(items[i]) && starts_with(label, e->typed)) {
            highlight(menu, items[i], true);
            break;
        }
    }
    afree(items);
}

/* Opens a row's submenu: its popovertarget's here, or its owner's, which
   opens it on a click. */
static void open_submenu(rml_element *row, bool keyboard)
{
    if (submenu_open(row)) {
        return;
    }
    rml_element *target = target_of(row);
    if (target) {
        popover_anchor at = { .side = POPOVER_BESIDE,
                              .element = row,
                              .trigger = row,
                              .keyboard = keyboard };
        popover_show(target, &at);
        return;
    }
    unref(&key_trigger);
    key_trigger = keyboard ? ref(row) : NULL;
    rml_element_click(row);
}

static void activate(rml_element *item)
{
    if (has_popup(item)) {
        open_submenu(item, true);
        return;
    }
    rml_element_click(item);
}

/* ---------- Menubars ---------- */

static bool is_bar_trigger(const rml_element *el)
{
    return opens_popup(el) && !is_item(el) && closest((rml_element *)el, is_menubar);
}

/* The menubar's own triggers in order; free with afree. */
static rml_element **triggers_of(rml_element *bar)
{
    const char *selector = "[popovertarget], [aria-haspopup]";
    int count = rml_element_query_selector_all(bar, selector, NULL, 0);
    rml_element **all = xmalloc((size_t)(count + 1) * sizeof *all), **triggers = NULL;
    rml_element_query_selector_all(bar, selector, all, count);
    for (int i = 0; i < count; i++) {
        if (is_bar_trigger(all[i]) && closest(all[i], is_menubar) == bar &&
            rml_element_is_visible(all[i], true)) {
            apush(triggers, all[i]);
        }
    }
    free(all);
    return triggers;
}

/* Opens a menubar trigger's menu: its popovertarget's here, or its
   owner's, which opens it on a press. */
static void open_from_bar(rml_element *trigger, bool keyboard)
{
    rml_element *target = target_of(trigger);
    if (target) {
        popover_anchor at = { .side = POPOVER_BELOW,
                              .element = trigger,
                              .trigger = trigger,
                              .keyboard = keyboard };
        popover_show(target, &at);
        return;
    }
    unref(&key_trigger);
    key_trigger = keyboard ? ref(trigger) : NULL;
    rml_element_dispatch_event(trigger, "mousedown", &(rml_event){ .button = 0 });
}

/* Left and Right in the menu a menubar trigger opened step to the next
   trigger's menu; false outside a menubar. */
static bool step_bar(int i, int dir)
{
    rml_element *trigger = below(i) < 0 ? get(stack[i].trigger) : NULL;
    rml_element *bar = trigger ? closest(trigger, is_menubar) : NULL;
    if (!bar) {
        return false;
    }
    rml_element **triggers = triggers_of(bar), *next = NULL;
    int count = acount(triggers);
    for (int t = 0; t < count; t++) {
        next = triggers[t] == trigger ? triggers[((t + dir) % count + count) % count]
                                      : next;
    }
    afree(triggers);
    if (next && next != trigger) {
        open_from_bar(next, true);
    }
    return true;
}

/* While a menu a menubar trigger opened is open, hovering another trigger
   in that menubar opens its menu. */
static void hover_bar(rml_context *context, rml_element *trigger)
{
    int bottom = bottom_of(context);
    rml_element *open = bottom >= 0 ? get(stack[bottom].trigger) : NULL;
    if (open && open != trigger &&
        closest(open, is_menubar) == closest(trigger, is_menubar)) {
        open_from_bar(trigger, false);
    }
}

/* ---------- Events ---------- */

/* Base UI's outside press: a press inside an open popover closes those above
   it; one on a trigger is the trigger's own; any other closes them all. */
static void dismiss(rml_context *context, rml_element *target)
{
    int top = top_of(context);
    for (int i = top; i >= 0; i--) {
        rml_element *el = stack[i].context == context ? get(stack[i].el) : NULL;
        if (el && control_within(target, el)) {
            if (i < top) {
                close_at(above(i), false);
            }
            return;
        }
    }
    for (int i = top; i >= 0; i--) {
        rml_element *trigger = stack[i].context == context ? get(stack[i].trigger)
                                                           : NULL;
        if (trigger && control_within(target, trigger)) {
            return;
        }
    }
    popover_hide_all(context);
}

/* A popovertarget click toggles its popover below it, or opens a menu
   item's submenu beside it. */
static void invoke(rml_element *invoker)
{
    rml_element *target = target_of(invoker);
    if (!target) {
        return;
    }
    int open = find(target);
    if (is_item(invoker)) {
        if (enabled(invoker)) {
            open_submenu(invoker, key_input);
        }
        return;
    }
    if (open >= 0) {
        close_at(open, true);
        return;
    }
    popover_anchor at = { .side = POPOVER_BELOW,
                          .element = invoker,
                          .trigger = invoker,
                          .keyboard = key_input };
    popover_show(target, &at);
}

static void pressed(const rml_event *e)
{
    key_input = false;
    rml_element *invoker = closest(e->target, invokes);
    if (invoker && is_bar_trigger(invoker)) {
        int open = find(target_of(invoker));
        if (open >= 0) {
            popover_hide_all(stack[open].context);
            return;
        }
        open_from_bar(invoker, false);
        return;
    }
    dismiss(rml_element_context(e->target), e->target);
}

static void clicked(const rml_event *e)
{
    rml_element *invoker = closest(e->target, invokes);
    if (invoker) {
        /* A disabled button opens nothing, as HTML's popovertarget. */
        if (!is_bar_trigger(invoker) && enabled(invoker)) {
            invoke(invoker);
        }
        return;
    }
    rml_element *item = closest(e->target, is_item);
    int i = item ? holding(item) : -1;
    /* A disabled item's click reaches nothing, as a disabled button's. */
    if (i >= 0 && !enabled(item)) {
        rml_event_stop_propagation(e);
        return;
    }
    if (i >= 0 && !has_popup(item) && !stays_open(item)) {
        close_chain(i);
    }
}

/* Tab in a popover moves within it; past either end it closes, to its
   trigger going back or the trigger's next stop going on. Tab closes a
   menu or a list. */
static bool tabbed(int i, const rml_event *e)
{
    rml_element *el = get(stack[i].el);
    if (!takes_focus(el)) {
        close_chain(i);
        return true;
    }
    if (!control_within(e->target, el)) {
        return false;
    }
    int step = e->modifiers & RML_KM_SHIFT ? -1 : 1;
    if (focus_step(el, e->target, step, false, NULL)) {
        return true;
    }
    rml_element *trigger = get(stack[i].trigger);
    close_at(i, false);
    if (trigger && step < 0) {
        rml_element_focus(trigger, true);
    }
    if (trigger && step > 0) {
        focus_step(rml_element_owner_document(trigger), trigger, 1, false, el);
    }
    return true;
}

static bool menu_key(int i, const rml_event *e)
{
    rml_element *menu = get(stack[i].el), *item = highlighted(menu);
    switch (e->key) {
        case RML_KEY_DOWN: step(menu, item, 1); return true;
        case RML_KEY_UP: step(menu, item, -1); return true;
        case RML_KEY_HOME: step(menu, NULL, 1); return true;
        case RML_KEY_END: step(menu, NULL, -1); return true;
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER:
        case RML_KEY_SPACE:
            if (item && enabled(item)) {
                activate(item);
            }
            return true;
        case RML_KEY_RIGHT:
            if (item && enabled(item) && has_popup(item)) {
                open_submenu(item, true);
                return true;
            }
            return step_bar(i, 1);
        case RML_KEY_LEFT:
            if (is_menu(get(below(i) >= 0 ? stack[below(i)].el : NULL))) {
                close_at(i, true);
                return true;
            }
            return step_bar(i, -1);
        default: break;
    }
    if (e->key >= RML_KEY_A && e->key <= RML_KEY_Z) {
        typeahead(&stack[i], menu, (char)('a' + (e->key - RML_KEY_A)));
        return true;
    }
    return false;
}

/* A menu takes the keys, unless a field has the focus or content it holds
   took it, as a hosted panel's row does. */
static bool takes_keys(rml_element *menu, rml_element *focus)
{
    return !editing(focus) &&
           (!control_within(focus, menu) || closest(focus, is_item));
}

static void keyed(const rml_event *e)
{
    key_input = true;
    int i = top_of(rml_element_context(e->target));
    rml_element *el = i >= 0 ? get(stack[i].el) : NULL;
    if (!el || attribute_is(e->target, "role", "combobox")) {
        return;
    }
    bool handled = true;
    if (e->key == RML_KEY_ESCAPE) {
        close_at(i, true);
    } else if (e->key == RML_KEY_TAB) {
        handled = tabbed(i, e);
    } else {
        handled = is_menu(el) && takes_keys(el, e->target) && menu_key(i, e);
    }
    if (handled) {
        rml_event_stop_propagation(e);
    }
}

/* The pointer highlights a menu's items and rests on a submenu's row; on a
   menubar trigger it switches menus. */
static void moved(const rml_event *e)
{
    rml_context *context = rml_element_context(e->target);
    if (top_of(context) < 0) {
        return;
    }
    rml_element *trigger = closest(e->target, is_bar_trigger);
    if (trigger) {
        hover_bar(context, trigger);
        return;
    }
    rml_element *item = closest(e->target, is_item);
    rml_element *menu = item ? owner_of(item) : NULL;
    int i = is_menu(menu) ? find(menu) : -1;
    if (i < 0) {
        return;
    }
    entry *at = &stack[i];
    /* A disabled item never highlights (Base UI); the pointer on it leaves
       none highlighted. */
    if (!enabled(item)) {
        unhighlight(menu);
        unref(&at->sub);
        return;
    }
    highlight(menu, item, false);
    if (!has_popup(item) || submenu_open(item)) {
        unref(&at->sub);
        return;
    }
    if (get(at->sub) != item) {
        unref(&at->sub);
        at->sub = ref(item);
        at->sub_since = rml_elapsed_time();
        rml_context_request_next_update(context, SUBMENU_DELAY);
    }
}

/* Focus moving to a control outside a popover that took it closes it. */
static void focused(const rml_event *e)
{
    int i = top_of(rml_element_context(e->target));
    rml_element *el = i >= 0 ? get(stack[i].el) : NULL;
    if (!el || !takes_focus(el) || stack[i].focus_pending ||
        control_within(e->target, el)) {
        return;
    }
    rml_element *trigger = get(stack[i].trigger);
    char tab[16];
    rml_element_property(e->target, "tab-index", tab, sizeof tab);
    /* RmlUi briefly focuses a non-tabbable root as a field blurs. */
    if ((!trigger || !control_within(e->target, trigger)) && !strcmp(tab, "auto")) {
        close_at(i, false);
    }
}

static void on_event(void *, const rml_event *e)
{
    switch (e->id) {
        case RML_EVENT_MOUSEDOWN: pressed(e); return;
        case RML_EVENT_CLICK: clicked(e); return;
        case RML_EVENT_KEYDOWN: keyed(e); return;
        case RML_EVENT_MOUSEMOVE: moved(e); return;
        case RML_EVENT_FOCUS: focused(e); return;
        default: return;
    }
}

/* The context's root holds its documents, so its listeners hear every
   event first. */
static void listen(rml_context *context)
{
    rml_element *root = rml_context_root(context);
    for (int i = 0; i < acount(roots);) {
        rml_element *seen = get(roots[i]);
        if (seen == root) {
            return;
        }
        if (seen) {
            i++;
            continue;
        }
        unref(&roots[i]);
        adel(roots, i);
    }
    if (!listener) {
        listener = rml_listener_create(on_event, NULL);
    }
    static const rml_event_id EVENTS[] = {
        RML_EVENT_MOUSEDOWN, RML_EVENT_CLICK, RML_EVENT_KEYDOWN,
        RML_EVENT_MOUSEMOVE, RML_EVENT_FOCUS,
    };
    for (size_t i = 0; i < sizeof EVENTS / sizeof *EVENTS; i++) {
        rml_element_add_event_listener(root, EVENTS[i], listener, true);
    }
    apush(roots, ref(root));
}

/* ---------- Settling ---------- */

/* Opens the submenu whose row the pointer rested on for SUBMENU_DELAY. */
static void open_due_submenu(rml_context *context, double now)
{
    rml_element *due = NULL;
    for (int i = 0; i < acount(stack) && !due; i++) {
        rml_element *row = stack[i].context == context ? get(stack[i].sub) : NULL;
        double left = row ? stack[i].sub_since + SUBMENU_DELAY - now : 0;
        if (left > 0) {
            rml_context_request_next_update(context, left);
        } else if (row) {
            unref(&stack[i].sub);
            due = row;
        }
    }
    if (due) {
        open_submenu(due, false);
    }
}

static void end_fades(rml_context *context, double now)
{
    for (int i = 0; i < acount(leavings);) {
        rml_element *el = get(leavings[i].el);
        if (el && rml_element_context(el) != context) {
            i++;
            continue;
        }
        if (el && now < leavings[i].until) {
            rml_context_request_next_update(context, leavings[i].until - now);
            i++;
            continue;
        }
        if (el) {
            rml_element_remove_attribute(el, "data-open");
            rml_element_remove_attribute(el, "data-ending-style");
            rml_element_remove_property(el, "pointer-events");
            unmount(el);
            rml_context_request_next_update(context, 0);
        }
        unref(&leavings[i].el);
        adel(leavings, i);
    }
    lower_documents();
}

void popovers_settle(rml_context *context)
{
    listen(context);
    for (int i = acount(stack) - 1; i >= 0; i--) {
        if (i < acount(stack) && stack[i].context == context && !get(stack[i].el)) {
            close_at(i, false);
        }
    }
    for (int i = 0; i < acount(stack); i++) {
        entry *e = &stack[i];
        rml_element *el = e->context == context ? get(e->el) : NULL;
        bool resized = el && follow_width(e, el);
        resized |= el && follow_height(e, el);
        if (el && (place(e, el) || resized)) {
            rml_document_update(rml_element_owner_document(el));
        }
        if (el && e->placed && e->keyboard && is_menu(el)) {
            e->keyboard = false;
            step(el, NULL, 1);
        }
    }
    int top = top_of(context);
    entry *e = top >= 0 ? &stack[top] : NULL;
    rml_element *el = e && e->placed && e->focus_pending ? get(e->el) : NULL;
    if (el) {
        e->focus_pending = false;
        if (!focus_step(el, NULL, 1, false, NULL)) {
            rml_element_focus(el, true);
        }
    }
    double now = rml_elapsed_time();
    open_due_submenu(context, now);
    end_fades(context, now);
}
