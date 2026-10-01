/* Controls after Base UI: buttons, checkboxes, toggle groups and selects, text
 * and search fields, color pickers, panels, property and tree rows, splitters
 * and surfaces. Number fields are in kit_number.c. */
#include "editor/rmlui/kit_internal.h"
#include "editor/ui/kit_icons.h"
#include "support/math.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* "#rrggbbaa" and its NUL. */
#define HEX_TEXT 10

static bool locked(const node *n)
{
    return n->w.desc.flags & (KIT_READONLY | KIT_DISABLED);
}

static int int_attribute(const rml_element *el, const char *name)
{
    char *text = element_attribute(el, name);
    int value = atoi(text);
    free(text);
    return value;
}

static void take_value(char **s, const rml_element *el)
{
    char *value = element_value(el);
    str_set(s, value);
    free(value);
}

static rml_element *text_input(rml_element *parent, const char *cls)
{
    rml_element *input = rml_element_create_child(parent, "input", NULL);
    rml_element_set_attribute(input, "type", "text");
    rml_element_set_class_names(input, cls);
    return input;
}

/* ---------- Color ---------- */

static void rgb_to_hsv(uint32_t c, float hsv[3])
{
    float r = ((c >> 24) & 255) / 255.f, g = ((c >> 16) & 255) / 255.f,
          b = ((c >> 8) & 255) / 255.f;
    float mx = math_max(math_max(r, g), b), mn = math_min(math_min(r, g), b);
    float d = mx - mn;
    float h = 0;
    if (d > 0) {
        h = mx == r ? fmodf((g - b) / d, 6.f)
          : mx == g ? (b - r) / d + 2
                    : (r - g) / d + 4;
    }
    hsv[0] = fmodf(h * 60 + 360, 360.f);
    hsv[1] = mx > 0 ? d / mx : 0;
    hsv[2] = mx;
}

static float hsv_channel(const float hsv[3], float n)
{
    float k = fmodf(n + hsv[0] / 60, 6.f);
    float t = math_max(0.f, math_min(math_min(k, 4 - k), 1.f));
    return hsv[2] - hsv[2] * hsv[1] * t;
}

static uint32_t channel_byte(float v)
{
    return (uint32_t)lroundf(math_clamp(v, 0.f, 1.f) * 255);
}

static uint32_t hsv_to_rgb(const float hsv[3], uint32_t alpha)
{
    return (channel_byte(hsv_channel(hsv, 5)) << 24) |
           (channel_byte(hsv_channel(hsv, 3)) << 16) |
           (channel_byte(hsv_channel(hsv, 1)) << 8) | (alpha & 255);
}

static void hex_color(char out[static HEX_TEXT], uint32_t c)
{
    snprintf(out, HEX_TEXT, "#%02x%02x%02x%02x", (c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool parse_hex(const char *text, uint32_t *rgba)
{
    const char *hex = text[0] == '#' ? text + 1 : text;
    size_t length = strlen(hex);
    if (length != 6 && length != 8) {
        return false;
    }
    uint32_t value = 0;
    for (size_t i = 0; i < length; i++) {
        int digit = hex_digit(hex[i]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (uint32_t)digit;
    }
    *rgba = length == 6 ? (value << 8) : value;
    return true;
}

static rml_element *color_input_row(rml_element *target)
{
    const char *cls = rml_element_is_class_set(target, "hex") ? "hexrow" : "opacity";
    for (rml_element *el = target; el; el = rml_element_parent(el)) {
        if (rml_element_is_class_set(el, cls)) {
            return el;
        }
    }
    return NULL;
}

static void input_invalid(rml_element *target, bool invalid)
{
    rml_element *row = color_input_row(target);
    if (!row) {
        return;
    }
    if (invalid) {
        rml_element_set_attribute(row, "data-invalid", "");
    } else {
        rml_element_remove_attribute(row, "data-invalid");
    }
}

static void color_drag(kit *k, node *n, float mx, float my)
{
    popup *p = popup_find(k, frame_format(k, "color:%s", n->path));
    if (!p || !p->node) {
        return;
    }
    const char *selector = n->w.drag_part == 1 ? ".sv" : ".hue";
    rml_element *part = rml_element_query_selector(p->node->el, selector);
    if (!part) {
        return;
    }
    kit_rect r = element_rect(k, part);
    float x = math_clamp((mx / dp(k) - r.x) / math_max(r.w, 1.f), 0.f, 1.f);
    float y = math_clamp((my / dp(k) - r.y) / math_max(r.h, 1.f), 0.f, 1.f);
    if (n->w.drag_part == 1) {
        n->w.hsv[1] = x;
        n->w.hsv[2] = 1 - y;
    } else {
        n->w.hsv[0] = x * 360;
    }
    n->w.color = hsv_to_rgb(n->w.hsv, n->w.color & 255);
    n->w.pending = true;
}

/* ---------- Events ---------- */

static void color_press(kit *k, node *n, const rml_event *e)
{
    for (rml_element *el = e->target; el && el != n->el; el = rml_element_parent(el)) {
        if (!rml_element_has_attribute(el, "data-part")) {
            continue;
        }
        n->w.drag_part = int_attribute(el, "data-part");
        color_drag(k, n, e->mouse_x, e->mouse_y);
        return;
    }
}

void control_press(kit *k, node *n, const rml_event *e)
{
    switch (n->kind) {
        case KIND_SPLITTER: n->w.pressing = true; return;
        case KIND_COLOR: color_press(k, n, e); return;
        default: return;
    }
}

static void toggle_click(node *n, rml_element *target)
{
    if (locked(n)) {
        return;
    }
    for (rml_element *el = target; el && el != n->el; el = rml_element_parent(el)) {
        if (rml_element_has_attribute(el, "data-index")) {
            n->w.pending_index = int_attribute(el, "data-index");
            n->w.pending = true;
            return;
        }
    }
}

/* Closes the field's popup, or opens it alone below the field. */
static void toggle_popup(kit *k, node *n, const char *prefix)
{
    const char *key = frame_format(k, "%s:%s", prefix, n->path);
    if (kit_popup_is_open(k, key)) {
        close_popup(k, key);
        return;
    }
    close_all_popups(k);
    if (n->kind == KIND_COLOR) {
        rgb_to_hsv(n->w.color, n->w.hsv);
    }
    open_popup(k, key, element_rect(k, n->el), false, 0, 0, "");
    str_set(&popup_at(k, key)->trigger, n->path);
}

void control_click(kit *k, node *n, rml_element *target)
{
    switch (n->kind) {
        case KIND_BUTTON:
            if (!rml_element_has_attribute(n->el, "data-disabled")) {
                n->events |= EV_CLICK;
            }
            return;
        case KIND_CHECK:
            if (!locked(n)) {
                n->w.pending = true;
            }
            return;
        case KIND_TOGGLE: toggle_click(n, target); return;
        case KIND_SELECT:
            if (!locked(n)) {
                toggle_popup(k, n, "select");
            }
            return;
        case KIND_COLOR:
            if (!locked(n) && within(target, n->el)) {
                toggle_popup(k, n, "color");
            }
            return;
        case KIND_PANEL:
            if (n->w.menu_el && within(target, n->w.menu_el)) {
                n->events |= EV_CONTEXT;
                return;
            }
            if (n->w.head_el && within(target, n->w.head_el)) {
                n->w.open = !n->w.open;
            }
            return;
        case KIND_ROW:
            n->events |= n->w.twisty_el && within(target, n->w.twisty_el)
                           ? EV_TWISTY
                           : EV_CLICK;
            return;
        default: return;
    }
}

static void splitter_drag(kit *k, widget *w, const rml_event *e)
{
    float along = w->split.axis == 1 ? e->mouse_y : e->mouse_x;
    if (e->id == RML_EVENT_DRAGSTART) {
        w->press_x = along;
        w->start = w->value;
        w->scrubbing = true;
        return;
    }
    if (e->id == RML_EVENT_DRAGEND) {
        w->scrubbing = w->pressing = false;
        w->committed = true;
        return;
    }
    double delta = (along - w->press_x) / dp(k) * (w->split.invert ? -1 : 1);
    w->value = math_clamp(w->start + delta, w->split.min, w->split.max);
    w->pending = true;
    w->pending_value = w->value;
}

void control_drag(kit *k, node *n, const rml_event *e)
{
    if (n->kind == KIND_SPLITTER) {
        splitter_drag(k, &n->w, e);
        return;
    }
    if (n->kind != KIND_COLOR || !n->w.drag_part) {
        return;
    }
    if (e->id == RML_EVENT_DRAGEND) {
        n->w.drag_part = 0;
        return;
    }
    color_drag(k, n, e->mouse_x, e->mouse_y);
}

static void field_key(kit *k, node *n, const rml_event *e)
{
    widget *w = &n->w;
    if (locked(n)) {
        return;
    }
    bool enter = e->key == RML_KEY_RETURN || e->key == RML_KEY_NUMPADENTER;
    if (enter && w->area && (e->modifiers & RML_KM_SHIFT)) {
        rml_context_process_text_input(k->context, "\n", 1);
        rml_event_stop_propagation(e);
        return;
    }
    if (enter) {
        if (n->kind == KIND_TEXT) {
            take_value(&w->draft, w->input_el);
            w->pending = true;
            rml_element_blur(w->input_el);
        }
        rml_event_stop_propagation(e);
        return;
    }
    if (e->key != RML_KEY_ESCAPE) {
        return;
    }
    if (n->kind == KIND_SEARCH) {
        rml_element_set_value(w->input_el, "");
        str_set(&w->draft, "");
        w->pending = true;
    } else {
        rml_element_set_value(w->input_el, w->applied ? w->applied : "");
        str_set(&w->draft, w->applied);
    }
    rml_element_blur(w->input_el);
    rml_event_stop_propagation(e);
}

/* Space and Return press a checkbox or button. */
static void press_key(node *n, const rml_event *e)
{
    if (locked(n) || rml_element_has_attribute(n->el, "data-disabled") ||
        (e->key != RML_KEY_SPACE && e->key != RML_KEY_RETURN)) {
        return;
    }
    if (n->kind == KIND_CHECK) {
        n->w.pending = true;
    }
    if (n->kind == KIND_BUTTON) {
        n->events |= EV_CLICK;
    }
    rml_event_stop_propagation(e);
}

static void hex_change(node *n, rml_element *target);

static void color_key(kit *k, node *n, const rml_event *e)
{
    bool enter = e->key == RML_KEY_RETURN || e->key == RML_KEY_NUMPADENTER;
    if (e->target == n->el) {
        if (locked(n) || (e->key != RML_KEY_SPACE && !enter)) {
            return;
        }
        bool was_open = kit_popup_is_open(k, frame_format(k, "color:%s", n->path));
        toggle_popup(k, n, "color");
        n->w.focus_picker = !was_open;
        rml_event_stop_propagation(e);
        return;
    }
    if (!enter || locked(n) || !rml_element_is_class_set(e->target, "hex")) {
        return;
    }
    hex_change(n, e->target);
    rml_element_blur(e->target);
    rml_event_stop_propagation(e);
}

static void select_key(kit *k, node *n, const rml_event *e)
{
    if (locked(n)) {
        return;
    }
    switch (e->key) {
        case RML_KEY_SPACE:
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER:
        case RML_KEY_DOWN:
        case RML_KEY_UP:
            toggle_popup(k, n, "select");
            rml_event_stop_propagation(e);
            return;
        default: return;
    }
}

static void toggle_key(node *n, const rml_event *e)
{
    if (locked(n)) {
        return;
    }
    int count = acount(n->w.labels);
    if (!count || !rml_element_has_attribute(e->target, "data-index")) {
        return;
    }
    int at = int_attribute(e->target, "data-index");
    switch (e->key) {
        case RML_KEY_SPACE:
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER: break;
        case RML_KEY_LEFT:
        case RML_KEY_UP: at = (at + count - 1) % count; break;
        case RML_KEY_RIGHT:
        case RML_KEY_DOWN: at = (at + 1) % count; break;
        case RML_KEY_HOME: at = 0; break;
        case RML_KEY_END: at = count - 1; break;
        default: return;
    }
    n->w.pending_index = at;
    n->w.pending = true;
    rml_element_focus(rml_element_child(n->el, at), true);
    rml_event_stop_propagation(e);
}

void control_key(kit *k, node *n, const rml_event *e)
{
    switch (n->kind) {
        case KIND_TEXT:
        case KIND_SEARCH: field_key(k, n, e); return;
        case KIND_COLOR: color_key(k, n, e); return;
        case KIND_SELECT: select_key(k, n, e); return;
        case KIND_TOGGLE: toggle_key(n, e); return;
        case KIND_CHECK:
        case KIND_BUTTON: press_key(n, e); return;
        default: return;
    }
}

static void hex_change(node *n, rml_element *target)
{
    char *value = element_value(target);
    const char *hex = value[0] == '#' ? value + 1 : value;
    size_t length = strlen(hex);
    uint32_t rgba = 0;
    bool valid = !locked(n) && parse_hex(value, &rgba);
    input_invalid(target, !valid);
    if (valid) {
        if (length == 6) {
            rgba = (rgba & 0xffffff00) | (n->w.color & 255);
        }
        n->w.color = rgba;
        rgb_to_hsv(n->w.color, n->w.hsv);
        n->w.pending = true;
    }
    free(value);
}

static bool field_input(const node *n, const rml_element *target)
{
    return (n->kind == KIND_TEXT || n->kind == KIND_SEARCH) && target == n->w.input_el;
}

void control_change(node *n, rml_element *target)
{
    if (field_input(n, target)) {
        take_value(&n->w.draft, target);
        if (n->kind == KIND_SEARCH) {
            n->w.pending = true;
        }
        return;
    }
    if (n->kind == KIND_COLOR && rml_element_is_class_set(target, "hex")) {
        hex_change(n, target);
    }
}

void control_focus(node *n, rml_element *target)
{
    if (!field_input(n, target)) {
        return;
    }
    n->w.focused = true;
    set_flag(n, "data-editing", true);
}

void control_blur(node *n, rml_element *target)
{
    if (!field_input(n, target)) {
        return;
    }
    n->w.focused = false;
    set_flag(n, "data-editing", false);
    if (n->kind == KIND_TEXT && !str_eq(n->w.draft, n->w.applied)) {
        n->w.pending = true;
    }
}

/* ---------- Controls ---------- */

/* Button variants and sizes (cva): the only place their classes are named. */
static const char *const BUTTON_VARIANTS[] = { "", "subtle", "primary", "outline", "danger" };
static const char *const BUTTON_SIZES[] = { "", "sm" };

bool kit_button(kit *k, const char *key, const kit_button_desc *d)
{
    bool square = d->icon && !(d->label && d->label[0]);
    const kit_class list[] = {
        { BUTTON_VARIANTS[d->variant], true },
        { BUTTON_SIZES[d->size], true },
        { "icon-only", square },
        { d->cls, d->cls != NULL },
    };
    const char *cls = kit_classes(k, "btn", list, 4);
    node *n = emit(k, key, KIND_BUTTON, "button", cls);
    widget *w = &n->w;
    if (!w->label_el) {
        w->icon_el = raw_icon(n->el, "", 0);
        w->label_el = raw(n->el, d->morph ? "morph-text" : "span", "label", NULL);
    }
    if (w->icon != d->icon) {
        w->icon = d->icon;
        char icon[5];
        utf8(icon, d->icon);
        rml_element_set_inner_rml(w->icon_el, icon);
        rml_element_set_property(w->icon_el, "display", d->icon ? "inline-block" : "none");
    }
    if (str_set(&w->label, d->label)) {
        bool morph = !strcmp(rml_element_tag_name(w->label_el), "morph-text");
        assert(morph == d->morph);
        if (morph) {
            morph_text_set(w->label_el, w->label);
        } else {
            set_inner_text(w->label_el, w->label);
        }
        rml_element_set_property(w->label_el, "display", w->label[0] ? "inline-block" : "none");
    }
    set_flag(n, "data-disabled", d->flags & KIT_DISABLED);
    if (d->flags & KIT_ON) {
        set_attr(n, "data-state", "on");
    } else {
        set_flag(n, "data-state", false);
    }
    set_tip(n, d->tip, d->shortcut, NULL);
    return (n->events & EV_CLICK) && !(d->flags & KIT_DISABLED);
}

bool kit_checkbox(kit *k, const char *key, bool *value, const char *label, unsigned flags)
{
    node *n = emit(k, key, KIND_CHECK, "div", "check");
    widget *w = &n->w;
    if (!w->label_el) {
        rml_element *box = raw(n->el, "span", "box", NULL);
        raw_icon(box, "", KIT_ICON_CHECK);
        w->label_el = raw(n->el, "span", "label", NULL);
    }
    w->desc.flags = flags;
    if (str_set(&w->label, label)) {
        set_inner_text(w->label_el, w->label);
        rml_element_set_property(w->label_el, "display", w->label[0] ? "inline" : "none");
    }
    bool changed = false;
    if (w->pending) {
        w->pending = false;
        *value = !*value;
        changed = true;
    }
    set_flag(n, "data-checked", *value);
    set_flag(n, "data-disabled", flags & KIT_DISABLED);
    set_flag(n, "data-readonly", flags & KIT_READONLY);
    return changed;
}

static bool labels_equal(char **have, const char *const *labels, int count)
{
    if (acount(have) != count) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!str_eq(have[i], labels[i])) {
            return false;
        }
    }
    return true;
}

static void set_segments(node *n, const char *const *labels, int count)
{
    widget *w = &n->w;
    for (int i = 0; i < acount(w->labels); i++) {
        free(w->labels[i]);
    }
    aclear(w->labels);
    for (int i = 0; i < count; i++) {
        apush(w->labels, xstrdup(labels[i]));
    }
    while (rml_element_child_count(n->el)) {
        rml_element_remove_child(n->el, rml_element_child(n->el, 0));
    }
    for (int i = 0; i < count; i++) {
        rml_element *seg = raw(n->el, "button", "seg", labels[i]);
        char index[16];
        snprintf(index, sizeof index, "%d", i);
        rml_element_set_attribute(seg, "data-index", index);
    }
}

static bool choice_toggle(kit *k, const char *key, int *value, const char *const *labels, int count, unsigned flags)
{
    node *n = emit(k, key, KIND_TOGGLE, "div", "segmented");
    widget *w = &n->w;
    w->desc.flags = flags;
    if (!labels_equal(w->labels, labels, count)) {
        set_segments(n, labels, count);
    }
    bool changed = false;
    if (w->pending && !locked(n) && w->pending_index >= 0 && w->pending_index < count) {
        changed = *value != w->pending_index;
        *value = w->pending_index;
    }
    w->pending = false;
    for (int i = 0; i < count; i++) {
        rml_element *segment = rml_element_child(n->el, i);
        flag_element(segment, "data-pressed", i == *value);
        flag_element(segment, "disabled", flags & KIT_DISABLED);
        rml_element_set_property(segment, "tab-index", flags & KIT_DISABLED ? "none" : "auto");
    }
    set_flag(n, "data-readonly", flags & KIT_READONLY);
    set_flag(n, "data-disabled", flags & KIT_DISABLED);
    rml_element_set_property(n->el, "opacity", flags & KIT_DISABLED ? "0.45" : "1");
    return changed;
}

static void select_options(kit *k, node *n, const char *menu, const char *const *labels, int count, int value)
{
    kit_rect trigger = element_rect(k, n->el);
    char width[32];
    snprintf(width, sizeof width, "%.1fdp", trigger.w);
    rml_element_set_property(popup_find(k, menu)->node->el, "min-width", width);
    for (int i = 0; i < count; i++) {
        char item_key[16];
        snprintf(item_key, sizeof item_key, "o%d", i);
        kit_menu_item_desc item = { .label = labels[i], .checked = i == value ? 1 : 0 };
        if (kit_menu_item(k, item_key, &item)) {
            n->w.pending = true;
            n->w.pending_index = i;
        }
    }
}

static bool choice_select(kit *k, const char *key, int *value, const char *const *labels, int count, unsigned flags)
{
    node *n = emit(k, key, KIND_SELECT, "div", "field select");
    widget *w = &n->w;
    w->desc.flags = flags;
    if (!w->value_el) {
        w->value_el = raw(n->el, "span", "part value", NULL);
        raw_icon(n->el, "caret", KIT_ICON_DOWN);
    }
    set_element_text(w->value_el, *value >= 0 && *value < count ? labels[*value] : "-");
    set_flag(n, "data-readonly", flags & KIT_READONLY);
    set_flag(n, "data-disabled", flags & KIT_DISABLED);
    rml_element_set_property(n->el, "tab-index", flags & KIT_DISABLED ? "none" : "auto");
    const char *menu = frame_format(k, "select:%s", n->path);
    if (locked(n)) {
        close_popup(k, menu);
    }
    set_flag(n, "data-open", kit_popup_is_open(k, menu));
    bool changed = false;
    if (w->pending) {
        w->pending = false;
        if (!locked(n) && w->pending_index >= 0 && w->pending_index < count) {
            changed = *value != w->pending_index;
            *value = w->pending_index;
        }
    }
    if (kit_popup_begin(k, menu, "menu")) {
        select_options(k, n, menu, labels, count, *value);
        kit_popup_end(k);
    }
    k->last = n;
    return changed;
}

bool kit_toggle_group(kit *k, const char *key, int *value, const char *const *labels, int count, unsigned flags)
{
    return choice_toggle(k, key, value, labels, count, flags);
}

bool kit_choice(kit *k, const char *key, int *value, const char *const *labels, int count, unsigned flags)
{
    if (count <= 3) {
        return kit_toggle_group(k, key, value, labels, count, flags);
    }
    return choice_select(k, key, value, labels, count, flags);
}

static node *emit_input(kit *k, const char *key, node_kind kind, const char *cls, const char *placeholder)
{
    node *n = emit(k, key, kind, "div", cls);
    widget *w = &n->w;
    if (!w->input_el) {
        if (kind == KIND_SEARCH) {
            rml_element *addon = raw(n->el, "span", "part addon", NULL);
            raw_icon(addon, "sm", KIT_ICON_SEARCH);
        }
        w->area = !strcmp(cls, "field text area");
        w->input_el = w->area ? raw(n->el, "textarea", "part value", NULL)
                              : text_input(n->el, "part value");
    }
    if (!str_eq(w->placeholder, placeholder)) {
        str_set(&w->placeholder, placeholder);
        rml_element_set_attribute(w->input_el, "placeholder", w->placeholder);
    }
    return n;
}

static bool edit_text(node *n, char *buffer, size_t size, unsigned flags)
{
    widget *w = &n->w;
    bool secret = flags & KIT_SECRET;
    if (w->secret != secret) {
        rml_element_set_attribute(w->input_el, "type", secret ? "password" : "text");
        w->secret = secret;
    }
    w->desc.flags = flags;
    set_flag(n, "data-readonly", flags & KIT_READONLY);
    set_flag(n, "data-disabled", flags & KIT_DISABLED);
    flag_element(w->input_el, "disabled", flags & KIT_DISABLED);
    rml_element_set_property(w->input_el, "tab-index", flags & KIT_DISABLED ? "none" : "auto");
    if (locked(n)) {
        rml_element_set_attribute(w->input_el, "readonly", "");
    } else {
        rml_element_remove_attribute(w->input_el, "readonly");
    }
    bool committed = false;
    if (w->pending) {
        w->pending = false;
        committed = !locked(n);
        w->committed = committed;
        if (committed && !str_eq(w->draft, buffer)) {
            snprintf(buffer, size, "%s", w->draft ? w->draft : "");
        }
    }
    if (!w->focused && !str_eq(w->applied, buffer)) {
        str_set(&w->applied, buffer);
        str_set(&w->draft, buffer);
        rml_element_set_value(w->input_el, buffer);
    }
    return committed;
}

bool kit_text_field(kit *k, const char *key, char *buffer, size_t size, const char *placeholder, unsigned flags)
{
    return edit_text(emit_input(k, key, KIND_TEXT, "field text", placeholder), buffer, size, flags);
}

bool kit_text_area(kit *k, const char *key, char *buffer, size_t size, const char *placeholder, unsigned flags)
{
    enum
    {
        MAX_ROWS = 8,
    };
    node *n = emit_input(k, key, KIND_TEXT, "field text area", placeholder);
    bool committed = edit_text(n, buffer, size, flags);
    int rows = 1;
    for (const char *c = n->w.draft ? n->w.draft : ""; *c && rows < MAX_ROWS; c++) {
        rows += *c == '\n';
    }
    if (int_attribute(n->w.input_el, "rows") != rows) {
        char text[8];
        snprintf(text, sizeof text, "%d", rows);
        rml_element_set_attribute(n->w.input_el, "rows", text);
    }
    return committed;
}

bool kit_search(kit *k, const char *key, char *buffer, size_t size, const char *placeholder)
{
    node *n = emit_input(k, key, KIND_SEARCH, "field search", placeholder);
    widget *w = &n->w;
    bool changed = false;
    if (w->pending) {
        w->pending = false;
        if (!str_eq(w->draft, buffer)) {
            snprintf(buffer, size, "%s", w->draft ? w->draft : "");
            changed = true;
        }
    }
    if (w->focused) {
        return changed;
    }
    char *shown = element_value(w->input_el);
    if (strcmp(shown, buffer) != 0) {
        rml_element_set_value(w->input_el, buffer);
        str_set(&w->draft, buffer);
    }
    free(shown);
    return changed;
}

static const char *const HUE_STOPS[7] = {
    "#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff", "#ff0000",
};

/* The picker's plane, hue strip and RGBA input; opacity uses kit_number. */
static rml_element *build_picker(rml_element *el)
{
    rml_element *sv = raw(el, "div", "sv", NULL);
    rml_element_set_attribute(sv, "data-part", "1");
    raw(sv, "div", "sv-white", NULL);
    raw(sv, "div", "sv-black", NULL);
    raw(sv, "div", "puck", NULL);
    rml_element *hue = raw(el, "div", "hue", NULL);
    rml_element_set_attribute(hue, "data-part", "2");
    for (int i = 0; i < 6; i++) {
        rml_element *seg = raw(hue, "div", "hue-seg", NULL);
        char decorator[64];
        snprintf(
            decorator,
            sizeof decorator,
            "horizontal-gradient(%s %s)",
            HUE_STOPS[i],
            HUE_STOPS[i + 1]
        );
        rml_element_set_property(seg, "decorator", decorator);
    }
    raw(hue, "div", "puck", NULL);
    rml_element *row = raw(el, "div", "field text left hexrow", NULL);
    raw(row, "span", "part addon", "RGBA");
    rml_element *hex = text_input(row, "part value hex");
    rml_element_set_attribute(hex, "aria-label", "RGBA hex color");
    rml_element_set_attribute(hex, "maxlength", "9");
    rml_element_set_attribute(hex, "placeholder", "RRGGBBAA");
    return sv;
}

static void set_percent(rml_element *el, const char *property, float percent)
{
    char text[32];
    snprintf(text, sizeof text, "%.2f%%", percent);
    rml_element_set_property(el, property, text);
}

static void update_picker(kit *k, const widget *w, node *p, unsigned flags)
{
    if (!p->w.head_el) {
        p->w.head_el = build_picker(p->el);
    }
    rml_element *sv = rml_element_query_selector(p->el, ".sv");
    const float pure[3] = { w->hsv[0], 1, 1 };
    char hex[HEX_TEXT];
    hex_color(hex, hsv_to_rgb(pure, 255));
    rml_element_set_property(sv, "background-color", hex);
    rml_element *puck = rml_element_query_selector(sv, ".puck");
    set_percent(puck, "left", w->hsv[1] * 100);
    set_percent(puck, "top", (1 - w->hsv[2]) * 100);
    rml_element *hue_puck = rml_element_query_selector(p->el, ".hue .puck");
    set_percent(hue_puck, "left", w->hsv[0] / 360 * 100);
    rml_element *input = rml_element_query_selector(p->el, ".hex");
    bool readonly = flags & (KIT_READONLY | KIT_DISABLED);
    if (readonly) {
        rml_element_set_attribute(input, "readonly", "");
    } else {
        rml_element_remove_attribute(input, "readonly");
    }
    if (input != rml_context_focus_element(k->context)) {
        hex_color(hex, w->color);
        rml_element_set_value(input, hex + 1);
    }
}

bool kit_color(kit *k, const char *key, uint32_t *rgba, unsigned flags)
{
    node *n = emit(k, key, KIND_COLOR, "div", "field color");
    widget *w = &n->w;
    w->desc.flags = flags;
    if (!w->swatch_el) {
        raw(n->el, "div", "mark", NULL);
        w->swatch_el = raw(n->el, "div", "swatch", NULL);
    }
    const char *picker = frame_format(k, "color:%s", n->path);
    bool open = kit_popup_is_open(k, picker);
    bool changed = false;
    if (w->pending) {
        w->pending = false;
        changed = w->color != *rgba;
        *rgba = w->color;
    }
    if (!open) {
        w->color = *rgba;
        if (w->open) {
            w->committed = true;
            w->focus_picker = false;
        }
    }
    w->open = open;
    bool none = (*rgba & 255) == 0;
    rml_element_set_class(w->swatch_el, "none", none);
    char hex[HEX_TEXT];
    hex_color(hex, *rgba);
    rml_element_set_property(w->swatch_el, "background-color", none ? "transparent" : hex);
    set_flag(n, "data-overridden", flags & KIT_OVERRIDDEN);
    set_flag(n, "data-readonly", flags & KIT_READONLY);
    set_flag(n, "data-disabled", flags & KIT_DISABLED);
    set_attr(n, "role", "button");
    set_attr(n, "aria-label", "Color picker");
    set_attr(n, "aria-haspopup", "dialog");
    set_attr(n, "aria-expanded", open ? "true" : "false");
    set_attr(n, "aria-disabled", locked(n) ? "true" : "false");
    rml_element_set_property(n->el, "tab-index", flags & KIT_DISABLED ? "none" : "auto");
    if (kit_popup_begin(k, picker, "picker popover")) {
        node *p = popup_find(k, picker)->node;
        rml_element_set_attribute(p->el, "role", "dialog");
        rml_element_set_attribute(p->el, "aria-label", "Color picker");
        update_picker(k, w, p, flags);
        double opacity = (double)(w->color & 255) * 100 / 255;
        const kit_number_desc alpha = {
            .min = 0,
            .max = 100,
            .step = 1,
            .precision = 0,
            .integer = true,
            .unit = "%",
            .flags = flags,
        };
        kit_prop_begin(k, "opacity", "Opacity");
        if (kit_number(k, "value", &opacity, &alpha)) {
            uint32_t channel = (uint32_t)lround(opacity * 255 / 100);
            w->color = (w->color & 0xffffff00) | channel;
            changed |= *rgba != w->color;
            *rgba = w->color;
        }
        kit_prop_end(k);
        if (w->focus_picker) {
            rml_element *input = rml_element_query_selector(p->el, ".hex");
            rml_element_focus(input, true);
            rml_element_select(input);
            w->focus_picker = false;
        }
        kit_popup_end(k);
    }
    k->last = n;
    return changed;
}

bool kit_panel_begin(kit *k, const char *key, const char *title, const char *badge, bool default_open)
{
    node *n = emit(k, key, KIND_PANEL, "section", "panel");
    widget *w = &n->w;
    if (!w->head_el) {
        w->head_el = raw(n->el, "div", "panel-head", NULL);
        raw_icon(w->head_el, "chev", KIT_ICON_DOWN);
        w->label_el = raw(w->head_el, "span", "title", NULL);
        w->badge_el = raw(w->head_el, "span", "badge override", NULL);
        raw(w->head_el, "span", "spacer", NULL);
        raw_icon(w->head_el, "grip", KIT_ICON_GRIP);
        w->open = default_open;
    }
    if (str_set(&w->label, title)) {
        set_inner_text(w->label_el, w->label);
    }
    if (str_set(&w->badge, badge)) {
        set_inner_text(w->badge_el, w->badge);
        rml_element_set_property(w->badge_el, "display", w->badge[0] ? "inline-block" : "none");
    }
    set_flag(n, "data-open", w->open);
    push_node(k, n);
    if (!w->open) {
        return false;
    }
    push_node(k, emit(k, "@body", KIND_BODY, "div", "panel-body"));
    return true;
}

void kit_panel_end(kit *k)
{
    if (alast(k->stack)->kind == KIND_BODY) {
        pop_node(k);
    }
    assert(alast(k->stack)->kind == KIND_PANEL);
    pop_node(k);
}

bool kit_panel_menu(kit *k)
{
    node *n = k->last;
    assert(n && n->kind == KIND_PANEL);
    widget *w = &n->w;
    if (!w->menu_el) {
        w->menu_el = raw_icon(w->head_el, "menu", KIT_ICON_MORE);
        rml_element_set_attribute(w->menu_el, "data-tip", "More");
        set_flag(n, "data-menu", true);
    }
    return n->events & EV_CONTEXT;
}

void kit_prop_begin(kit *k, const char *key, const char *label)
{
    node *n = emit(k, key, KIND_PROP, "div", "prop");
    if (!n->w.label_el) {
        rml_element *l = raw(n->el, "span", "label", NULL);
        n->w.label_el = raw(l, "span", "", NULL);
    }
    if (str_set(&n->w.label, label)) {
        set_inner_text(n->w.label_el, n->w.label);
    }
    push_node(k, n);
    push_node(k, emit(k, "@control", KIND_BODY, "div", "control"));
}

void kit_prop_end(kit *k)
{
    pop_node(k);
    pop_node(k);
}

static void set_twisty(rml_element *el, int twisty)
{
    uint32_t cp = twisty < 0 ? 0 : twisty ? KIT_ICON_DOWN : KIT_ICON_RIGHT;
    char text[5];
    utf8(text, cp);
    char *shown = element_inner_rml(el);
    if (strcmp(shown, text) != 0) {
        rml_element_set_inner_rml(el, text);
    }
    free(shown);
}

static unsigned row_events(const node *n)
{
    return (n->events & EV_CLICK ? KIT_ROW_CLICK : 0u) |
           (n->events & EV_DOUBLE ? KIT_ROW_DOUBLE_CLICK : 0u) |
           (n->events & EV_CONTEXT ? KIT_ROW_CONTEXT : 0u) |
           (n->events & EV_TWISTY ? KIT_ROW_TWISTY : 0u);
}

unsigned kit_row_begin(kit *k, const char *key, const kit_row_desc *d)
{
    node *n = emit(k, key, KIND_ROW, "div", "tree-row");
    widget *w = &n->w;
    if (!w->label_el) {
        w->twisty_el = raw_icon(n->el, "twisty sm", 0);
        w->icon_el = raw_icon(n->el, "", 0);
        w->label_el = raw(n->el, "span", "name", NULL);
        w->meta_el = raw(n->el, "span", "meta", NULL);
    }
    set_twisty(w->twisty_el, d->twisty);
    if (w->icon != d->icon) {
        w->icon = d->icon;
        char icon[5];
        utf8(icon, d->icon);
        rml_element_set_inner_rml(w->icon_el, icon);
    }
    if (str_set(&w->label, d->label)) {
        set_inner_text(w->label_el, w->label);
    }
    if (str_set(&w->meta, d->meta)) {
        set_inner_text(w->meta_el, w->meta);
    }
    char pad[32];
    snprintf(pad, sizeof pad, "%ddp", 6 + d->depth * 14);
    rml_element_set_property(n->el, "padding-left", pad);
    set_flag(n, "data-selected", d->flags & KIT_ON);
    set_flag(n, "data-active", d->flags & KIT_ACTIVE);
    unsigned result = row_events(n);
    push_node(k, n);
    push_node(k, emit(k, "@trail", KIND_BODY, "span", "trail"));
    return result;
}

void kit_row_end(kit *k)
{
    pop_node(k);
    pop_node(k);
}

/* ---------- Splitter and surfaces ---------- */

bool kit_splitter(kit *k, const char *key, int axis, float *value, float minimum, float maximum, bool invert)
{
    node *n = emit(k, key, KIND_SPLITTER, "div", axis ? "split-h" : "split-v");
    widget *w = &n->w;
    w->split = (splitter_desc){ axis, invert, minimum, maximum };
    set_flag(n, "data-dragging", w->scrubbing);
    if (w->pending) {
        w->pending = false;
        *value = (float)w->pending_value;
        return true;
    }
    if (!w->scrubbing) {
        w->value = *value;
    }
    return false;
}

void kit_surface(kit *k, const char *key, const char *cls, const char *texture, kit_surface_input *input)
{
    node *n = emit(k, key, KIND_SURFACE, "img", cls);
    set_attr(n, "src", frame_format(k, "kit:%s", texture));
    kit_surface_input out = { .rect = element_rect(k, n->el) };
    out.x = k->in.mouse_x - out.rect.x;
    out.y = k->in.mouse_y - out.rect.y;
    out.hovered = rml_context_hover_element(k->context) == n->el &&
                  rect_contains(out.rect, k->in.mouse_x, k->in.mouse_y);
    bool captured = str_eq(k->surface_capture, n->path);
    for (int b = 0; b < 3; b++) {
        if (out.hovered && k->in.pressed[b] && !captured) {
            str_set(&k->surface_capture, n->path);
            k->surface_button = b;
            captured = true;
            out.pressed[b] = true;
        }
    }
    if (captured) {
        int b = k->surface_button;
        out.down[b] = k->in.down[b];
        out.released[b] = k->in.released[b];
        if (out.released[b] || !k->in.down[b]) {
            str_set(&k->surface_capture, "");
            k->surface_button = -1;
        }
    }
    out.wheel = out.hovered ? k->in.wheel : 0;
    *input = out;
}

/* ---------- UI wiring (workstream K) ---------- */

bool kit_picker(kit *k, const char *key, const kit_picker_desc *d)
{
    node *n = emit(k, key, KIND_BUTTON, "div", "field select picker");
    widget *w = &n->w;
    if (!w->value_el) {
        w->icon_el = raw_icon(n->el, "lead", 0);
        w->value_el = raw(n->el, "span", "part value", NULL);
        raw_icon(n->el, "caret", KIT_ICON_DOWN);
    }
    if (w->icon != d->icon) {
        w->icon = d->icon;
        char icon[5];
        utf8(icon, d->icon);
        rml_element_set_inner_rml(w->icon_el, icon);
        rml_element_set_property(w->icon_el, "display", d->icon ? "inline-block" : "none");
    }
    bool empty = !d->value || !d->value[0];
    set_element_text(w->value_el, empty ? d->placeholder : d->value);
    set_flag(n, "data-placeholder", empty);
    set_flag(n, "data-readonly", d->flags & KIT_READONLY);
    set_flag(n, "data-disabled", d->flags & KIT_DISABLED);
    set_flag(n, "data-invalid", d->flags & KIT_INVALID);
    set_tip(n, d->tip, NULL, NULL);
    return (n->events & EV_CLICK) && !(d->flags & (KIT_READONLY | KIT_DISABLED));
}
