/* Popups, menus and tooltips after Base UI. Popups stack in open order, close
 * on an outside press or Escape, and sit below their anchor or beside
 * their parent menu. Menus highlight with the arrow keys, Home, End and
 * typeahead; submenus open after a 100 ms hover; an open menubar moves between
 * its triggers. A tooltip shows after a 500 ms hover, switches instantly within
 * 400 ms of the last one, and hides while a button is held or a popup or the
 * palette is open. It holds a label, a shortcut and an optional description. */
#include "editor/rmlui/kit_internal.h"
#include "editor/ui/kit_icons.h"
#include "support/math.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A menu is as wide as its widest row and the popup's padding, in bounds. */
static const float popup_chrome = 10, menu_min = 180, menu_max = 480;
static const float autocomplete_chrome = 8;
/* Tooltip text in dp on screen, and its distance from the target. */
static const float tip_size = 11, tip_gap = 6;

/* ---------- Opening and closing ---------- */

static void unstack(kit *k, const popup *p)
{
    int kept = 0;
    for (int i = 0; i < acount(k->popup_stack); i++) {
        if (k->popup_stack[i] != p) {
            k->popup_stack[kept++] = k->popup_stack[i];
        }
    }
    asetlen(k->popup_stack, kept);
}

void close_popup(kit *k, const char *key)
{
    popup *p = popup_find(k, key);
    if (!p || !p->open) {
        return;
    }
    for (int i = 0; i < cf_map_size(k->popups); i++) {
        popup *child = k->popups[i];
        if (child->open && str_eq(child->parent, key)) {
            close_popup(k, child->key);
        }
    }
    p->open = false;
    p->placed = false;
    p->highlight = -1;
    /* Activation closes after emission; stop hit testing before the next frame
       retires the popup so another click can reach the control beneath it. */
    if (p->node) {
        rml_element_set_property(p->node->el, "pointer-events", "none");
    }
    unstack(k, p);
}

void close_all_popups(kit *k)
{
    /* The stack holds exactly the open popups; each close takes its popup and
       any submenus off it. */
    while (acount(k->popup_stack)) {
        popup *p = k->popup_stack[0];
        assert(p->open);
        close_popup(k, p->key);
    }
}

void open_popup(kit *k, const char *key, kit_rect anchor, bool at_point, float x, float y, const char *parent)
{
    popup *p = popup_at(k, key);
    if (p->open) {
        close_popup(k, p->key);
    }
    free(p->parent);
    free(p->sub_hover);
    free(p->typed);
    free(p->dropdown_placeholder);
    free(p->dropdown_trigger_path);
    free(p->trigger);
    afree(p->items);
    rml_element *focus = rml_context_focus_element(k->context);
    node *trigger = node_for(k, focus);
    *p = (popup){
        .key = p->key,
        .open = true,
        .at_point = at_point,
        .keyboard = k->keyboard_edit,
        .x = x,
        .y = y,
        .anchor = anchor,
        .highlight = -1,
        .focus_pending = true,
    };
    str_set(&p->trigger, trigger ? trigger->path : "");
    str_set(&p->parent, parent);
    apush(k->popup_stack, p);
}

static popup *top_popup(const kit *k)
{
    return acount(k->popup_stack) ? alast(k->popup_stack) : NULL;
}

static popup *popup_for_trigger(const kit *k, const node *trigger)
{
    if (!trigger) {
        return NULL;
    }
    for (int i = acount(k->popup_stack) - 1; i >= 0; i--) {
        popup *p = k->popup_stack[i];
        if (p->node && within(trigger->el, p->node->el)) {
            return p;
        }
    }
    return NULL;
}

static void close_popup_children(kit *k, popup *owner)
{
    while (top_popup(k) && top_popup(k) != owner) {
        close_popup(k, top_popup(k)->key);
    }
}

void popup_open_on(kit *k, const char *key, kit_rect anchor, const node *trigger)
{
    popup *owner = popup_for_trigger(k, trigger);
    if (owner) {
        close_popup_children(k, owner);
    } else {
        close_all_popups(k);
    }
    char *parent = owner ? xstrdup(owner->key) : NULL;
    open_popup(k, key, anchor, false, 0, 0, parent);
    free(parent);
}

void dropdown_open_on(kit *k, const char *key, kit_rect anchor, const char *placeholder, const node *trigger)
{
    popup *existing = popup_find(k, key);
    if (existing && existing->open && existing->dropdown && trigger &&
        str_eq(existing->dropdown_trigger_path, trigger->path)) {
        close_popup(k, key);
        rml_element_focus(trigger->el, true);
        return;
    }
    popup_open_on(k, key, anchor, trigger);
    popup *p = popup_at(k, key);
    p->dropdown = true;
    p->dropdown_focus = true;
    str_set(&p->dropdown_placeholder, placeholder ? placeholder : "Search…");
    if (trigger) {
        str_set(&p->dropdown_trigger_path, trigger->path);
    }
}

void autocomplete_open_on(kit *k, const char *key, kit_rect anchor, node *trigger)
{
    popup *existing = popup_find(k, key);
    if (existing && existing->open) {
        return;
    }
    popup_open_on(k, key, anchor, trigger);
    popup *p = popup_at(k, key);
    p->dropdown = true;
    p->autocomplete = true;
    p->autocomplete_opening = true;
    if (trigger) {
        str_set(&p->dropdown_trigger_path, trigger->path);
    }
}

static void focus_dropdown_trigger(kit *k, const popup *p)
{
    if (!p->dropdown || !p->dropdown_trigger_path) {
        return;
    }
    node *trigger = path_node(k, p->dropdown_trigger_path);
    if (trigger) {
        rml_element_focus(trigger->el, true);
    }
}

static const char *menu_popup_key(kit *k, const node *trigger)
{
    return frame_format(k, "menu:%s", trigger->path);
}

/* Opens a menubar trigger's menu in place of any open popups. */
static popup *open_menu(kit *k, node *trigger)
{
    const char *key = menu_popup_key(k, trigger);
    close_all_popups(k);
    open_popup(k, key, element_rect(k, trigger->el), false, 0, 0, "");
    popup *p = popup_at(k, key);
    p->menubar = true;
    str_set(&p->trigger, trigger->path);
    return p;
}

/* ---------- Placement ---------- */

static void set_position(rml_element *el, float x, float y)
{
    char value[32];
    snprintf(value, sizeof value, "%.1fdp", x);
    rml_element_set_property(el, "left", value);
    snprintf(value, sizeof value, "%.1fdp", y);
    rml_element_set_property(el, "top", value);
}

/* At its point, beside a submenu's row, or below its anchor, flipped to the
 * other side when it would leave the window. */
static rml_vec2 popup_origin(const kit *k, const popup *p, kit_rect size)
{
    kit_rect a = p->anchor;
    if (p->at_point) {
        return (rml_vec2){ p->x, p->y };
    }
    if (!str_eq(p->parent, "") && !p->dropdown) {
        float x = a.x + a.w + 2;
        if (x + size.w > k->in.width - 4) {
            x = a.x - size.w - 2;
        }
        return (rml_vec2){ x, a.y - 4 };
    }
    float y = a.y + a.h + 2;
    if (y + size.h > k->in.height - 4) {
        y = a.y - size.h - 2;
    }
    return (rml_vec2){ a.x, y };
}

/* Field choices retain their row limit; action menus only stop at the window. */
static void limit_popup_height(kit *k, popup *p)
{
    rml_element *list = NULL;
    float cap = 0;
    if (p->dropdown || p->autocomplete) {
        list = rml_element_query_selector(p->node->el, ".dropdown-results, .autocomplete-results");
        float chrome = p->autocomplete ? autocomplete_chrome
                                       : popup_chrome + kit_metric(k, "--h") + 8;
        float rows = p->autocomplete ? 7 * kit_metric(k, "--autocomplete-row-h")
                                     : kit_metric(k, "--dropdown-list-h");
        cap = math_min(rows, math_max(1.0f, k->in.height - 8 - chrome));
    } else if (
        rml_element_is_class_set(p->node->el, "menu") &&
        !rml_element_is_class_set(p->node->el, "struct-editor")
    ) {
        list = p->node->el;
        cap = math_max(1.0f, k->in.height - 8);
    }
    if (list) {
        char value[32];
        snprintf(value, sizeof value, "%.0fdp", cap);
        rml_element_set_property(list, "max-height", value);
    }
}

void place_popups(kit *k)
{
    float width = k->in.width, height = k->in.height;
    for (int i = 0; i < cf_map_size(k->popups); i++) {
        popup *p = k->popups[i];
        if (!p->open || !p->node) {
            continue;
        }
        limit_popup_height(k, p);
        kit_rect size = element_rect(k, p->node->el);
        if (size.w <= 0 || size.h <= 0) {
            continue;
        }
        rml_vec2 at = popup_origin(k, p, size);
        at.x = math_clamp(at.x, 4.0f, math_max(4.0f, width - size.w - 4));
        at.y = math_clamp(at.y, 4.0f, math_max(4.0f, height - size.h - 4));
        bool ready = p->placed;
        set_position(p->node->el, at.x, at.y);
        if (!p->placed) {
            p->placed = true;
            rml_element_remove_attribute(p->node->el, "data-unplaced");
        }
        if (p->dropdown_reveal && p->highlight >= 0 && p->highlight < acount(p->items)) {
            rml_element_scroll_into_view(p->items[p->highlight]->el, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
            p->dropdown_reveal = false;
        }
        if (ready && p->popover && p->focus_pending &&
            (focus_step(p->node->el, NULL, 1, false, NULL) ||
             rml_element_focus(p->node->el, true))) {
            p->focus_pending = false;
        }
    }
}

/* ---------- Highlight and activation ---------- */

static void highlight_item(popup *p, int index)
{
    p->highlight = index;
    for (int i = 0; i < acount(p->items); i++) {
        if (p->items[i]) {
            flag_element(p->items[i]->el, "data-highlighted", i == index);
        }
    }
}

static bool item_enabled(const node *n)
{
    return n && !rml_element_has_attribute(n->el, "data-disabled");
}

static node *highlighted(const popup *p)
{
    bool valid = p->highlight >= 0 && p->highlight < acount(p->items);
    return valid ? p->items[p->highlight] : NULL;
}

/* Highlights the next enabled item in dir, wrapping, or from an end when
 * absolute. */
static void move_highlight(popup *p, int dir, bool absolute)
{
    int count = acount(p->items);
    int start = absolute ? (dir > 0 ? -1 : count) : p->highlight;
    for (int step = 1; step <= count; step++) {
        int i = start + dir * step;
        if (!absolute) {
            i = (i % count + count) % count;
        }
        if (i < 0 || i >= count) {
            return;
        }
        if (item_enabled(p->items[i])) {
            highlight_item(p, i);
            rml_element_scroll_into_view(p->items[i]->el, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
            return;
        }
    }
}

static void activate_item(kit *k, popup *p, node *item)
{
    if (!item_enabled(item)) {
        return;
    }
    if (item->kind == KIND_SUBMENU) {
        const char *sub = frame_format(k, "sub:%s", item->path);
        open_popup(k, sub, element_rect(k, item->el), false, 0, 0, p->key);
        return;
    }
    item->w.pending = true;
    p->activated = true;
}

/* Closes the parent's other submenus and opens the row's. */
static void open_submenu(kit *k, const popup *parent, const char *path)
{
    node *row = path_node(k, path);
    if (!row) {
        return;
    }
    for (int i = 0; i < cf_map_size(k->popups); i++) {
        popup *q = k->popups[i];
        if (q->open && str_eq(q->parent, parent->key)) {
            close_popup(k, q->key);
        }
    }
    const char *sub = frame_format(k, "sub:%s", path);
    open_popup(k, sub, element_rect(k, row->el), false, 0, 0, parent->key);
}

/* Submenus open after a 100 ms hover. */
void open_hovered_submenus(kit *k)
{
    /* Paths are copied: opening a submenu resets its own hover. */
    popup **parents = NULL;
    char **paths = NULL;
    for (int i = 0; i < acount(k->popup_stack); i++) {
        popup *p = k->popup_stack[i];
        bool due = !str_eq(p->sub_hover, "") &&
                   k->in.time - p->sub_hover_since >= SUBMENU_DELAY;
        if (due && !kit_popup_is_open(k, frame_format(k, "sub:%s", p->sub_hover))) {
            apush(parents, p);
            apush(paths, xstrdup(p->sub_hover));
        }
    }
    for (int i = 0; i < acount(paths); i++) {
        open_submenu(k, parents[i], paths[i]);
        free(paths[i]);
    }
    afree(parents);
    afree(paths);
}

/* Any activated menu item closes the whole menu chain. */
void close_activated_menus(kit *k)
{
    for (int i = 0; i < cf_map_size(k->popups); i++) {
        popup *p = k->popups[i];
        if (p->open && p->activated) {
            bool nested_dropdown = p->dropdown && p->parent && p->parent[0];
            char *trigger_path = p->dropdown && p->dropdown_trigger_path
                                   ? xstrdup(p->dropdown_trigger_path)
                                   : NULL;
            if (nested_dropdown) {
                close_popup(k, p->key);
            } else {
                close_all_popups(k);
            }
            if (trigger_path) {
                node *trigger = path_node(k, trigger_path);
                if (trigger) {
                    rml_element_focus(trigger->el, true);
                }
                free(trigger_path);
            }
            return;
        }
    }
}

/* ---------- Events ---------- */

static bool opens_popup(node_kind kind)
{
    return kind == KIND_TRIGGER || kind == KIND_SELECT ||
           kind == KIND_AUTOCOMPLETE || kind == KIND_COLOR;
}

/* Outside presses dismiss popups (Base UI outside-press). */
void popup_press(kit *k, node *n, rml_element *target)
{
    for (int i = acount(k->popup_stack) - 1; i >= 0; i--) {
        popup *p = k->popup_stack[i];
        if (p->dropdown && n && str_eq(p->dropdown_trigger_path, n->path)) {
            return;
        }
    }
    for (int i = acount(k->popup_stack) - 1; i >= 0; i--) {
        popup *p = k->popup_stack[i];
        if (p->node && within(target, p->node->el)) {
            close_popup_children(k, p);
            return;
        }
        node *trigger = path_node(k, p->trigger);
        if (trigger && within(target, trigger->el)) {
            return;
        }
    }
    for (node *trigger = n; trigger; trigger = trigger->parent) {
        if (opens_popup(trigger->kind)) {
            return;
        }
    }
    for (int i = acount(k->popup_stack) - 1; i >= 0; i--) {
        popup *p = k->popup_stack[i];
        if (!p->autocomplete || !p->dropdown_trigger_path) {
            continue;
        }
        node *field = path_node(k, p->dropdown_trigger_path);
        if (field) {
            rml_element_blur(field->w.input_el);
        }
        break;
    }
    close_all_popups(k);
}

void trigger_press(kit *k, node *n, const rml_event *e)
{
    if (kit_popup_is_open(k, menu_popup_key(k, n))) {
        close_all_popups(k);
    } else {
        open_menu(k, n);
    }
    rml_event_stop_propagation(e);
}

void menu_click(kit *k, node *n)
{
    for (int i = 0; i < acount(k->popup_stack); i++) {
        popup *p = k->popup_stack[i];
        if (nodes_index(p->items, n) >= 0) {
            activate_item(k, p, n);
            return;
        }
    }
}

static void hover_item(kit *k, node *n)
{
    for (int i = 0; i < acount(k->popup_stack); i++) {
        popup *p = k->popup_stack[i];
        int at = nodes_index(p->items, n);
        if (at < 0) {
            continue;
        }
        highlight_item(p, at);
        str_set(&p->sub_hover, n->kind == KIND_SUBMENU ? n->path : "");
        p->sub_hover_since = k->in.time;
    }
}

/* Hovering another trigger while a menubar menu is open switches to it. */
static void hover_trigger(kit *k, node *n)
{
    bool menubar_open = false;
    for (int i = 0; i < acount(k->popup_stack); i++) {
        menubar_open |= k->popup_stack[i]->menubar;
    }
    if (menubar_open && !kit_popup_is_open(k, menu_popup_key(k, n))) {
        open_menu(k, n);
    }
}

void menu_hover(kit *k, node *n)
{
    switch (n->kind) {
        case KIND_MENU_ITEM:
        case KIND_SUBMENU: hover_item(k, n); return;
        case KIND_TRIGGER: hover_trigger(k, n); return;
        default: return;
    }
}

/* Opens the menu of the trigger dir steps from the open one, wrapping. */
static void step_menubar(kit *k, const popup *top, int dir)
{
    node **triggers = NULL;
    for (int i = 0; i < acount(k->triggers); i++) {
        if (k->triggers[i]) {
            apush(triggers, k->triggers[i]);
        }
    }
    int count = acount(triggers);
    for (int i = 0; i < count; i++) {
        if (str_eq(menu_popup_key(k, triggers[i]), top->key)) {
            open_menu(k, triggers[(i + dir + count) % count])->keyboard = true;
            break;
        }
    }
    afree(triggers);
}

/* Whether text starts with prefix, ignoring text's case; prefix is lower
 * case. */
static bool starts_with_lower(const char *text, const char *prefix)
{
    for (; *prefix; prefix++, text++) {
        if (tolower((unsigned char)*text) != *prefix) {
            return false;
        }
    }
    return true;
}

/* Highlights the first enabled item whose label starts with the letters typed
 * within TYPEAHEAD_RESET of each other. */
static void typeahead(kit *k, popup *p, char c)
{
    char letter[2] = { c, 0 };
    if (k->in.time - p->typed_at < TYPEAHEAD_RESET) {
        str_cat(&p->typed, letter);
    } else {
        str_set(&p->typed, letter);
    }
    p->typed_at = k->in.time;
    for (int i = 0; i < acount(p->items); i++) {
        node *item = p->items[i];
        if (item_enabled(item) && starts_with_lower(item->w.label, p->typed)) {
            highlight_item(p, i);
            return;
        }
    }
}

/* Applies a key to the top popup; false when the key is not a menu's. */
static bool apply_menu_key(kit *k, popup *p, rml_key key)
{
    node *item = highlighted(p);
    switch (key) {
        case RML_KEY_ESCAPE:
            close_popup(k, p->key);
            focus_dropdown_trigger(k, p);
            return true;
        case RML_KEY_TAB:
            if (p->dropdown && p->parent && p->parent[0]) {
                close_popup(k, p->key);
            } else {
                close_all_popups(k);
            }
            return true;
        case RML_KEY_DOWN: move_highlight(p, 1, false); return true;
        case RML_KEY_UP: move_highlight(p, -1, false); return true;
        case RML_KEY_HOME: move_highlight(p, 1, true); return true;
        case RML_KEY_END: move_highlight(p, -1, true); return true;
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER:
        case RML_KEY_SPACE: activate_item(k, p, item); return true;
        default: break;
    }
    bool horizontal = key == RML_KEY_LEFT || key == RML_KEY_RIGHT;
    if (key == RML_KEY_RIGHT && item && item->kind == KIND_SUBMENU) {
        activate_item(k, p, item);
        popup_at(k, frame_format(k, "sub:%s", item->path))->keyboard = true;
        return true;
    }
    if (key == RML_KEY_LEFT && !str_eq(p->parent, "")) {
        close_popup(k, p->key);
        return true;
    }
    if (horizontal && p->menubar) {
        step_menubar(k, p, key == RML_KEY_RIGHT ? 1 : -1);
        return true;
    }
    if (key >= RML_KEY_A && key <= RML_KEY_Z) {
        typeahead(k, p, (char)('a' + (key - RML_KEY_A)));
        return true;
    }
    return false;
}

bool popup_key(kit *k, const rml_event *e)
{
    popup *top = top_popup(k);
    if (!top) {
        return false;
    }
    if (top->popover) {
        node *trigger = path_node(k, top->trigger);
        if (e->key == RML_KEY_ESCAPE) {
            close_popup(k, top->key);
            if (trigger) {
                rml_element_focus(trigger->el, true);
            }
            rml_event_stop_propagation(e);
            return true;
        }
        if (e->key != RML_KEY_TAB || !top->node || !within(e->target, top->node->el)) {
            return false;
        }
        int step = e->modifiers & RML_KM_SHIFT ? -1 : 1;
        if (!focus_step(top->node->el, e->target, step, false, NULL)) {
            close_popup(k, top->key);
            if (trigger && step < 0) {
                rml_element_focus(trigger->el, true);
            }
            if (trigger && step > 0) {
                focus_step(
                    rml_element_owner_document(trigger->el),
                    trigger->el,
                    1,
                    false,
                    top->node->el
                );
            }
        }
        rml_event_stop_propagation(e);
        return true;
    }
    if (apply_menu_key(k, top, e->key)) {
        rml_event_stop_propagation(e);
    }
    return true;
}

/* The search field owns ordinary typing. Its dropdown still owns navigation
 * and selection keys, so Up/Down and Enter work without leaving the field. */
bool dropdown_key(kit *k, const rml_event *e, node *n)
{
    popup *top = top_popup(k);
    if (!top || !top->dropdown || !n || n->kind != KIND_SEARCH ||
        e->target != top->dropdown_search) {
        return false;
    }
    switch (e->key) {
        case RML_KEY_DOWN: move_highlight(top, 1, false); break;
        case RML_KEY_UP: move_highlight(top, -1, false); break;
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER:
            activate_item(k, top, highlighted(top));
            break;
        default: return false;
    }
    rml_event_stop_propagation(e);
    return true;
}

bool autocomplete_key(kit *k, const rml_event *e, node *n)
{
    if (!n || n->kind != KIND_AUTOCOMPLETE || e->target != n->w.input_el) {
        return false;
    }
    if (n->w.desc.flags & (KIT_READONLY | KIT_DISABLED)) {
        return false;
    }
    const char *key = frame_format(k, "autocomplete:%s", n->path);
    popup *p = popup_find(k, key);
    switch (e->key) {
        case RML_KEY_DOWN:
        case RML_KEY_UP: {
            if (!p || !p->open) {
                autocomplete_open_on(k, key, element_rect(k, n->el), n);
                p = popup_find(k, key);
            }
            p->keyboard = true;
            const char *query = n->w.autocomplete_query_active ? n->w.draft : "";
            if (str_eq(p->dropdown_query, query)) {
                move_highlight(p, e->key == RML_KEY_DOWN ? 1 : -1, false);
            } else {
                p->highlight = -1;
            }
            node *item = highlighted(p);
            if (item) {
                rml_element_set_attribute(n->w.input_el, "aria-activedescendant", item->path);
            } else {
                rml_element_remove_attribute(n->w.input_el, "aria-activedescendant");
            }
            rml_event_stop_propagation(e);
            return true;
        }
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER: {
            const char *query = n->w.autocomplete_query_active ? n->w.draft : "";
            if (p && p->open && str_eq(p->dropdown_query, query) &&
                highlighted(p) && item_enabled(highlighted(p))) {
                activate_item(k, p, highlighted(p));
            } else {
                n->w.autocomplete_submitted = true;
            }
            rml_event_stop_propagation(e);
            return true;
        }
        case RML_KEY_ESCAPE:
            if (p && p->open) {
                close_popup(k, key);
            }
            str_set(&n->w.draft, n->w.applied);
            n->w.autocomplete_query_active = false;
            n->w.autocomplete_canceled = true;
            rml_element_set_value(n->w.input_el, n->w.applied ? n->w.applied : "");
            rml_element_focus(n->w.input_el, true);
            rml_event_stop_propagation(e);
            return true;
        case RML_KEY_TAB:
            if (!(event_modifiers(e) & KIT_MOD_SHIFT) && p && p->open &&
                str_eq(p->dropdown_query, n->w.autocomplete_query_active ? n->w.draft : "") &&
                highlighted(p) && item_enabled(highlighted(p))) {
                activate_item(k, p, highlighted(p));
                rml_event_stop_propagation(e);
                return true;
            }
            if (p && p->open) {
                close_popup(k, key);
            }
            str_set(&n->w.draft, n->w.applied);
            n->w.autocomplete_query_active = false;
            rml_element_set_value(n->w.input_el, n->w.applied ? n->w.applied : "");
            return true;
        default: return false;
    }
}

void popup_focus(kit *k, rml_element *target)
{
    popup *top = top_popup(k);
    if (!top || !top->popover || top->focus_pending || !top->node ||
        within(target, top->node->el)) {
        return;
    }
    node *trigger = path_node(k, top->trigger);
    if (!trigger || !within(target, trigger->el)) {
        /* RmlUi briefly focuses a non-tabbable root when a field blurs.
           Only a focus move to an outside control is a focus-out dismissal. */
        char tab[16];
        rml_element_property(target, "tab-index", tab, sizeof tab);
        if (!strcmp(tab, "auto")) {
            close_popup(k, top->key);
        }
    }
}

/* ---------- Popups and menus ---------- */

void kit_popup_open(kit *k, const char *key)
{
    kit_rect anchor = k->last ? element_rect(k, k->last->el) : (kit_rect){ 0 };
    node *trigger = k->last && k->last->kind == KIND_BUTTON &&
                            (k->last->events & EV_CLICK)
                      ? k->last
                      : NULL;
    popup_open_on(k, key, anchor, trigger);
    str_set(&popup_at(k, key)->trigger, k->last ? k->last->path : "");
}

void kit_dropdown_open(kit *k, const char *key, const char *placeholder)
{
    kit_rect anchor = k->last ? element_rect(k, k->last->el) : (kit_rect){ 0 };
    dropdown_open_on(k, key, anchor, placeholder, k->last);
}

void kit_dropdown_open_at(kit *k, const char *key, kit_rect anchor, const char *placeholder)
{
    dropdown_open_on(k, key, anchor, placeholder, k->last);
}

bool kit_dropdown_is_open(const kit *k, const char *key)
{
    popup *p = popup_find(k, key);
    return p && p->open && p->dropdown;
}

const char *kit_dropdown_query(const kit *k, const char *key)
{
    popup *p = popup_find(k, key);
    return p && p->dropdown ? p->dropdown_query : "";
}

void kit_popup_open_at(kit *k, const char *key, float x, float y)
{
    close_all_popups(k);
    open_popup(k, key, (kit_rect){ 0 }, true, x, y, "");
}

void kit_popup_close(kit *k, const char *key)
{
    close_popup(k, key);
}

bool kit_popup_is_open(const kit *k, const char *key)
{
    popup *p = popup_find(k, key);
    return p && p->open;
}

bool kit_has_popup(const kit *k)
{
    return acount(k->popup_stack) > 0;
}

bool kit_popup_begin(kit *k, const char *key, const char *cls)
{
    popup *p = popup_find(k, key);
    if (!p || !p->open) {
        return false;
    }
    const char *c = frame_format(k, "popup %s", cls ? cls : "");
    node *n = emit_in(k, k->layer, frame_format(k, "@%s", key), KIND_POPUP, "div", c);
    rml_element_remove_property(n->el, "pointer-events");
    presence(k, n);
    flag_element(n->el, "data-unplaced", !p->placed);
    p->node = n;
    p->popover = rml_element_is_class_set(n->el, "popover");
    aclear(p->items);
    p->content = 0;
    apush(k->emitting, p);
    push_node(k, n);
    return true;
}

static popup *emitting_popup(const kit *k)
{
    return acount(k->emitting) ? alast(k->emitting) : NULL;
}

static float dp_ratio(const kit *k)
{
    return rml_context_dp_ratio(k->context);
}

/* RmlUi sizes an absolutely positioned box from its block content and not from
   flex rows, so a menu is widened here to its widest measured row. */
static void fit_row(kit *k, float width)
{
    popup *p = emitting_popup(k);
    if (p) {
        p->content = math_max(p->content, width);
    }
}

void kit_popup_end(kit *k)
{
    assert(acount(k->emitting));
    popup *p = apop(k->emitting);
    if (p->keyboard && p->highlight < 0 && acount(p->items)) {
        move_highlight(p, 1, true);
    }
    float width = math_clamp(p->content + popup_chrome, menu_min, menu_max);
    if (p->content > 0 && p->node && width != p->content_shown) {
        char value[32];
        snprintf(value, sizeof value, "%.0fdp", width);
        rml_element_set_property(p->node->el, "min-width", value);
        p->content_shown = width;
    }
    pop_node(k);
}

/* The shortcut, or a submenu's chevron. */
static void set_row_meta(widget *w, bool sub, const char *shortcut)
{
    flag_element(w->kbd_el, "data-empty", !sub && (!shortcut || !shortcut[0]));
    char *rml = NULL;
    if (sub) {
        char chevron[5];
        utf8(chevron, KIT_ICON_RIGHT);
        str_cat(&rml, chevron);
    } else {
        escape(&rml, shortcut);
    }
    if (str_set(&w->meta, rml)) {
        rml_element_set_inner_rml(w->kbd_el, w->meta);
        rml_element_set_class_names(w->kbd_el, sub ? "icon chev" : "kbd");
    }
    free(rml);
}

/* The chevron, or the gap and the shortcut. */
static float trail_width(kit *k, bool sub, const char *shortcut)
{
    if (sub) {
        return 20;
    }
    if (!shortcut || !shortcut[0]) {
        return 0;
    }
    return 8 + 24 + kit_text_width(k, shortcut, 11, 400, dp_ratio(k));
}

/* Padding, lead icon and gaps, then the label and the shortcut or chevron. */
/* Menu labels draw at the body's size, `--studio-font-13` when the font
   preference sets it (theme/base.rcss), else 13 dp. */
static float label_size(kit *k)
{
    char value[32] = "";
    rml_element_property(k->doc, "--studio-font-13", value, sizeof value);
    float size = strtof(value, NULL);
    return isfinite(size) && size > 0 ? size : 13;
}

static float row_width(kit *k, bool sub, const char *label, const char *shortcut)
{
    return 6 + 16 + 8 + kit_text_width(k, label, label_size(k), 400, dp_ratio(k)) +
           8 + trail_width(k, sub, shortcut);
}

/* A swatch tints the lead glyph; none restores the theme's icon color. */
static void set_swatch(rml_element *lead, uint32_t rgba)
{
    if (!rgba) {
        rml_element_remove_property(lead, "color");
        return;
    }
    char color[16];
    snprintf(color, sizeof color, "#%08x", rgba);
    rml_element_set_property(lead, "color", color);
}

static node *menu_row(kit *k, const char *key, node_kind kind, const kit_menu_item_desc *d)
{
    bool sub = kind == KIND_SUBMENU;
    node *n = emit(k, key, kind, "div", sub ? "menu-item sub" : "menu-item");
    widget *w = &n->w;
    if (!w->label_el) {
        w->icon_el = raw_icon(n->el, "lead", 0);
        w->head_el = raw(n->el, "div", "text", NULL);
        w->label_el = raw(w->head_el, "span", "label", NULL);
        w->about_el = raw(w->head_el, "span", "about", NULL);
        w->kbd_el = raw(n->el, "span", "kbd", NULL);
    }
    uint32_t lead = d->checked >= 0 ? (d->checked ? KIT_ICON_CHECK : 0)
                  : d->swatch       ? KIT_ICON_DOT
                                    : d->icon;
    if (w->icon != lead) {
        w->icon = lead;
        char text[5];
        utf8(text, lead);
        rml_element_set_inner_rml(w->icon_el, text);
    }
    if (w->color != d->swatch) {
        w->color = d->swatch;
        set_swatch(w->icon_el, d->swatch);
    }
    if (str_set(&w->label, d->label)) {
        set_inner_text(w->label_el, w->label);
    }
    set_element_text(w->about_el, d->description);
    set_flag(n, "data-described", d->description && d->description[0]);
    set_row_meta(w, sub, d->shortcut);
    set_flag(n, "data-disabled", d->disabled);
    set_flag(n, "data-checked", d->checked > 0);
    float width = row_width(k, sub, w->label, d->shortcut);
    if (d->description && d->description[0]) {
        width = math_max(
            width,
            6 + 16 + 8 + kit_text_width(k, d->description, 11, 400, dp_ratio(k)) +
                8 + trail_width(k, sub, d->shortcut)
        );
    }
    fit_row(k, width);
    popup *p = emitting_popup(k);
    if (p) {
        apush(p->items, n);
        bool on = p->highlight == acount(p->items) - 1;
        flag_element(n->el, "data-highlighted", on);
    }
    return n;
}

bool kit_menu_item(kit *k, const char *key, const kit_menu_item_desc *desc)
{
    node *n = menu_row(k, key, KIND_MENU_ITEM, desc);
    set_flag(n, "data-current", desc->current);
    bool activated = n->w.pending;
    n->w.pending = false;
    return activated && !desc->disabled;
}

void kit_menu_separator(kit *k, const char *key)
{
    emit(k, key, KIND_BOX, "div", "menu-sep");
}

void kit_menu_group(kit *k, const char *key, const char *label)
{
    node *n = emit(k, key, KIND_BOX, "div", "menu-group");
    set_text(n, label);
    fit_row(k, 12 + kit_text_width(k, label, 11, 400, dp_ratio(k)));
}

bool kit_submenu_begin(kit *k, const char *key, const char *label, uint32_t icon)
{
    kit_menu_item_desc d = { .label = label, .icon = icon, .checked = -1 };
    node *n = menu_row(k, key, KIND_SUBMENU, &d);
    const char *sub = frame_format(k, "sub:%s", n->path);
    popup *p = popup_find(k, sub);
    if (p && p->open) {
        p->anchor = element_rect(k, n->el);
    }
    return kit_popup_begin(k, sub, "menu");
}

void kit_submenu_end(kit *k)
{
    kit_popup_end(k);
}

bool kit_menubar_trigger(kit *k, const char *key, const char *label)
{
    node *n = emit(k, key, KIND_TRIGGER, "button", "btn trigger");
    if (!n->w.label_el) {
        n->w.label_el = raw(n->el, "span", "label", NULL);
    }
    set_element_text(n->w.label_el, label);
    apush(k->triggers, n);
    const char *menu = menu_popup_key(k, n);
    bool open = kit_popup_is_open(k, menu);
    set_flag(n, "data-pressed", open);
    return open && kit_popup_begin(k, menu, "menu");
}

/* ---------- Tooltips ---------- */

static void set_optional_attr(node *n, const char *name, const char *value)
{
    if (value && value[0]) {
        set_attr(n, name, value);
        return;
    }
    set_flag(n, name, false);
}

void set_tip(node *n, const char *label, const char *shortcut, const char *description)
{
    set_optional_attr(n, "data-tip", label);
    set_optional_attr(n, "data-kbd", shortcut);
    set_optional_attr(n, "data-tip-about", description);
}

/* The hovered element with a tip, unless a held button, a popup or the
 * palette blocks tooltips. */
static rml_element *tip_owner(kit *k)
{
    if (k->in.down[0] || k->in.down[1] || acount(k->popup_stack) || k->command.open) {
        return NULL;
    }
    rml_element *el = rml_context_hover_element(k->context);
    for (; el; el = rml_element_parent(el)) {
        if (rml_element_has_attribute(el, "data-tip")) {
            return el;
        }
    }
    return NULL;
}

/* Tooltips keep their size on screen in a zoomed canvas. */
static float tip_scale(const kit *k)
{
    return k->in.zoom > 0 ? 1 / k->in.zoom : 1;
}

/* A picture above, then the label and shortcut on one line; a description
 * wraps beneath them. */
static void tip_content(kit *k, rml_element *target)
{
    char *tip = element_attribute(target, "data-tip");
    char *shortcut = element_attribute(target, "data-kbd");
    char *about = element_attribute(target, "data-tip-about");
    char *image = element_attribute(target, "data-tip-image");
    char *rml = NULL;
    if (image[0]) {
        str_cat(&rml, "<img class=\"tip-image\" src=\"");
        escape(&rml, image);
        str_cat(&rml, "\"/>");
    }
    str_set(&k->tip_image, image);
    flag_element(k->tooltip, "data-image", image[0] != '\0');
    free(image);
    str_cat(&rml, "<div class=\"tip-head\"><span>");
    escape(&rml, tip);
    str_cat(&rml, "</span>");
    if (shortcut[0]) {
        str_cat(&rml, "<span class=\"kbd\">");
        escape(&rml, shortcut);
        str_cat(&rml, "</span>");
    }
    str_cat(&rml, "</div>");
    if (about[0]) {
        str_cat(&rml, "<div class=\"tip-about\">");
        escape(&rml, about);
        str_cat(&rml, "</div>");
    }
    rml_element_set_inner_rml(k->tooltip, rml);
    free(rml);
    free(about);
    free(shortcut);
    free(tip);
}

static void show_tip(kit *k, rml_element *target)
{
    tip_content(k, target);
    char size[32];
    snprintf(size, sizeof size, "%.2fdp", tip_size * tip_scale(k));
    rml_element_set_property(k->tooltip, "font-size", size);
    rml_element_set_property(k->tooltip, "display", "block");
    /* Warm tooltips in a group switch instantly (Base UI); a cold one fades in.
     */
    flag_element(k->tooltip, "data-instant", k->in.time < k->tip_warm_until);
    flag_element(k->tooltip, "data-starting-style", !k->reduced_motion);
    k->tip_shown_frame = k->frame;
    k->tip_visible = true;
}

/* Below the target, or above when it would leave the window; beside it for
 * data-tip-side="right". */
static void place_tip(kit *k, rml_element *target)
{
    kit_rect r = element_rect(k, target);
    kit_rect size = element_rect(k, k->tooltip);
    char *side = element_attribute(target, "data-tip-side");
    bool right = strcmp(side, "right") == 0;
    free(side);
    float gap = tip_gap * tip_scale(k), edge = 4 * tip_scale(k);
    float x = right ? r.x + r.w + gap : r.x + r.w / 2 - size.w / 2;
    float y = right ? r.y + r.h / 2 - size.h / 2 : r.y + r.h + gap;
    if (!right && y + size.h > k->in.height - edge) {
        y = r.y - size.h - gap;
    }
    x = math_clamp(x, edge, math_max(edge, k->in.width - size.w - edge));
    set_position(k->tooltip, x, y);
}

void update_tooltip(kit *k)
{
    rml_element *target = tip_owner(k);
    if (target != k->tip_target) {
        if (k->tip_visible) {
            k->tip_warm_until = k->in.time + FEEL_TOOLTIP_GROUP;
        }
        k->tip_visible = false;
        k->tip_target = target;
        k->tip_since = k->in.time;
    }
    if (k->tip_visible && k->frame > k->tip_shown_frame) {
        flag_element(k->tooltip, "data-starting-style", false);
    }
    bool show = target && (k->tip_visible ||
                           k->in.time - k->tip_since >= FEEL_TOOLTIP_DELAY ||
                           k->in.time < k->tip_warm_until);
    if (!show) {
        rml_element_set_property(k->tooltip, "display", "none");
        return;
    }
    if (!k->tip_visible) {
        show_tip(k, target);
    } else {
        char *image = element_attribute(target, "data-tip-image");
        if (!str_eq(image, k->tip_image ? k->tip_image : "")) {
            tip_content(k, target);
        }
        free(image);
    }
    place_tip(k, target);
}

void kit_tip(kit *k, const char *label, const char *shortcut, const char *description)
{
    assert(k->last);
    set_tip(k->last, label, shortcut, description);
}

void kit_tip_image(kit *k, const char *source)
{
    assert(k->last);
    set_optional_attr(k->last, "data-tip-image", source);
}
