/* <color-field value="#rrggbbaa" mixed readonly disabled/> (controls.h): a
 * swatch that opens its own picker, as <input type=color> opens the
 * system's. The picker is a popover inside the field, fixed and unclipped
 * (`position: fixed; clip: none`) and stacked, placed and dismissed by
 * ui/popover.c, so it escapes its area: a saturation and value plane, a hue
 * strip, an RGBA hex field and an Opacity number field. Every edit while it
 * is open sends input; closing it, by an outside press, Escape, Tab past its
 * ends or the swatch, sends change once. One picker is open at a time.
 * While closed the field shows its value attribute's. */
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"
#include "ui/motion.h"
#include "ui/popover.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* "#rrggbbaa" and its NUL. */
#define HEX_TEXT 10
#define LABEL_TEXT 256

enum
{
    PART_NONE,
    PART_PLANE,
    PART_HUE,
};

typedef struct color_field
{
    rml_element *el, *swatch;
    rml_element *popover, *plane, *plane_puck, *hue_puck, *hexrow, *hex, *opacity;
    rml_element *leaving; /* a closed picker fading out */
    double leave_at;
    uint32_t color, model;
    float hsv[3];
    int part; /* what a press on the picker drags */
    bool mixed, readonly, disabled, dirty;
    bool select_hex;         /* a key opened it: its hex text selects */
    bool pending, committed; /* what C owners take */
} color_field;

static rml_listener *listener, *input_listener;

static color_field *field_of(const rml_element *el)
{
    return rml_element_user(el);
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
    snprintf(out, HEX_TEXT, "#%08x", c);
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

/* "#rrggbb" or "#rrggbbaa", the # optional; *digits is 6 or 8. */
static bool parse_hex(const char *text, uint32_t *rgba, size_t *digits)
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
    *digits = length;
    return true;
}

/* ---------- Showing ---------- */

static bool locked(const color_field *f)
{
    return f->readonly || f->disabled;
}

static void set_percent(rml_element *el, const char *property, float percent)
{
    char text[32];
    snprintf(text, sizeof text, "%.2f%%", percent);
    rml_element_set_property(el, property, text);
}

static void show_swatch(color_field *f)
{
    bool none = (f->color & 255) == 0;
    rml_element_set_class(f->swatch, "none", none);
    char hex[HEX_TEXT];
    hex_color(hex, f->color);
    rml_element_set_property(f->swatch, "background-color", none ? "transparent" : hex);
    control_flag(f->el, "data-mixed", f->mixed);
    control_flag(f->el, "data-readonly", f->readonly);
    control_flag(f->el, "data-disabled", f->disabled);
    rml_element_set_attribute(f->el, "aria-disabled", locked(f) ? "true" : "false");
    rml_element_set_property(f->el, "tab-index", f->disabled ? "none" : "auto");
}

static void show_picker(color_field *f)
{
    if (!f->popover) {
        return;
    }
    const float pure[3] = { f->hsv[0], 1, 1 };
    char hex[HEX_TEXT];
    hex_color(hex, hsv_to_rgb(pure, 255));
    rml_element_set_property(f->plane, "background-color", hex);
    set_percent(f->plane_puck, "left", f->hsv[1] * 100);
    set_percent(f->plane_puck, "top", (1 - f->hsv[2]) * 100);
    set_percent(f->hue_puck, "left", f->hsv[0] / 360 * 100);
    if (f->hex != rml_context_focus_element(rml_element_context(f->el))) {
        hex_color(hex, f->color);
        rml_element_set_value(f->hex, hex + 1);
    }
    char opacity[32];
    snprintf(opacity, sizeof opacity, "%.17g", (double)(f->color & 255) * 100 / 255);
    rml_element_set_attribute(f->opacity, "value", opacity);
}

/* An edit while the picker is open: it shows, and input reports it. */
static void set_color(color_field *f, uint32_t color)
{
    f->color = color;
    f->pending = true;
    if (f->mixed) {
        f->mixed = false;
        rml_element_remove_attribute(f->el, "mixed");
    }
    show_swatch(f);
    show_picker(f);
    control_redraw(f->el);
    char hex[HEX_TEXT];
    hex_color(hex, color);
    rml_element_dispatch_value(f->el, "input", hex, false);
}

/* ---------- The picker ---------- */

static const char *const HUE_STOPS[7] = {
    "#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff", "#ff0000",
};

static rml_element *build_plane(color_field *f, rml_element *popover)
{
    rml_element *plane = control_part(popover, "div", "sv");
    rml_element_set_attribute(plane, "data-part", "1");
    control_part(plane, "div", "sv-white");
    control_part(plane, "div", "sv-black");
    f->plane_puck = control_part(plane, "div", "puck");
    return plane;
}

static void build_hue(color_field *f, rml_element *popover)
{
    rml_element *hue = control_part(popover, "div", "hue");
    rml_element_set_attribute(hue, "data-part", "2");
    for (int i = 0; i < 6; i++) {
        rml_element *seg = control_part(hue, "div", "hue-seg");
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
    f->hue_puck = control_part(hue, "div", "puck");
}

static void build_hex(color_field *f, rml_element *popover)
{
    f->hexrow = control_part(popover, "div", "field text left hexrow");
    control_text(control_part(f->hexrow, "span", "part addon"), "RGBA");
    f->hex = control_part(f->hexrow, "input", "part value hex");
    rml_element_set_attribute(f->hex, "type", "text");
    rml_element_set_attribute(f->hex, "aria-label", "RGBA hex color");
    rml_element_set_attribute(f->hex, "maxlength", "9");
    rml_element_set_attribute(f->hex, "placeholder", "RRGGBBAA");
}

/* Opacity as a property row: its label edits the bounded percent field. */
static void build_opacity(color_field *f, rml_element *popover)
{
    rml_element *prop = control_part(popover, "div", "prop");
    rml_element *label = control_part(prop, "label", "label");
    control_text(control_part(label, "span", ""), "Opacity");
    rml_element *control = control_part(prop, "div", "control");
    f->opacity = control_part(control, "number-field", "field number");
    static const char *const ATTRIBUTES[][2] = {
        { "min", "0" },
        { "max", "100" },
        { "step", "1" },
        { "precision", "0" },
        { "integer", "" },
        { "unit", "%" },
        { "id", "color-field-opacity" },
        { "aria-label", "Opacity" },
    };
    for (size_t i = 0; i < sizeof ATTRIBUTES / sizeof *ATTRIBUTES; i++) {
        rml_element_set_attribute(f->opacity, ATTRIBUTES[i][0], ATTRIBUTES[i][1]);
    }
    rml_element_set_attribute(label, "for", "color-field-opacity");
}

static void build_popover(color_field *f)
{
    rml_element *p = control_part(f->el, "div", "popup picker popover");
    f->popover = p;
    char label[LABEL_TEXT];
    control_string(f->el, "aria-label", label, sizeof label);
    rml_element_set_attribute(p, "role", "dialog");
    rml_element_set_attribute(p, "aria-label", label[0] ? label : "Color picker");
    rml_element_set_attribute(p, "data-starting-style", "");
    rml_element_set_property(p, "position", "fixed");
    rml_element_set_property(p, "clip", "none");
    f->plane = build_plane(f, p);
    build_hue(f, p);
    build_hex(f, p);
    build_opacity(f, p);
    rml_element_add_named_listener(p, "input", input_listener, false);
}

static void drop_leaving(color_field *f)
{
    if (f->leaving) {
        rml_element_remove_child(f->el, f->leaving);
        f->leaving = NULL;
    }
}

static void open_picker(color_field *f, bool keyboard);
static void close_picker(color_field *f, bool refocus);

static void open_picker(color_field *f, bool keyboard)
{
    if (f->popover || locked(f)) {
        return;
    }
    drop_leaving(f);
    rgb_to_hsv(f->color, f->hsv);
    build_popover(f);
    f->select_hex = keyboard;
    show_picker(f);
    rml_element_set_attribute(f->el, "aria-expanded", "true");
    control_flag(f->el, "data-open", true);
    /* Showing it closes any other picker, as showing a popover does. */
    popover_anchor at = { .side = POPOVER_BELOW, .element = f->el, .trigger = f->el };
    popover_show(f->popover, &at);
    control_redraw(f->el);
}

/* The picker fades out as the theme's exit transition says, then goes. */
static void close_picker(color_field *f, bool refocus)
{
    if (!f->popover) {
        return;
    }
    rml_element *p = f->popover;
    f->popover = NULL;
    f->part = PART_NONE;
    f->select_hex = false;
    popover_hide(p);
    rml_element_set_attribute(f->el, "aria-expanded", "false");
    control_flag(f->el, "data-open", false);
    if (refocus) {
        rml_element_focus(f->el, true);
    }
    rml_element_set_property(p, "pointer-events", "none");
    rml_element_set_attribute(p, "data-ending-style", "");
    f->leaving = p;
    f->leave_at = rml_elapsed_time() +
                  (motion_reduced(p) ? 0 : motion_transition_time(p));
    rml_context_request_next_update(rml_element_context(f->el), 0);
    f->committed = f->dirty = true;
    char hex[HEX_TEXT];
    hex_color(hex, f->color);
    rml_element_dispatch_value(f->el, "change", hex, false);
}

/* ---------- Editing ---------- */

static void drag_part(color_field *f, float mx, float my)
{
    rml_element *part = f->part == PART_PLANE ? f->plane
                                              : rml_element_parent(f->hue_puck);
    rml_vec2 at = rml_element_absolute_offset(part, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(part, RML_BOX_BORDER);
    float x = math_clamp((mx - at.x) / math_max(size.x, 1.f), 0.f, 1.f);
    float y = math_clamp((my - at.y) / math_max(size.y, 1.f), 0.f, 1.f);
    if (f->part == PART_PLANE) {
        f->hsv[1] = x;
        f->hsv[2] = 1 - y;
    } else {
        f->hsv[0] = x * 360;
    }
    set_color(f, hsv_to_rgb(f->hsv, f->color & 255));
}

/* The hex field's text: six digits keep the alpha, eight set it; anything
 * else marks the row invalid. */
static void hex_typed(color_field *f)
{
    char text[16];
    rml_element_value(f->hex, text, sizeof text);
    uint32_t rgba = 0;
    size_t digits = 0;
    bool valid = parse_hex(text, &rgba, &digits);
    control_flag(f->hexrow, "data-invalid", !valid);
    if (!valid) {
        return;
    }
    if (digits == 6) {
        rgba = (rgba & 0xffffff00) | (f->color & 255);
    }
    rgb_to_hsv(rgba, f->hsv);
    set_color(f, rgba);
}

static void opacity_input(color_field *f)
{
    double percent;
    bool committed;
    if (!number_field_take(f->opacity, &percent, &committed)) {
        return;
    }
    uint32_t alpha = (uint32_t)lround(math_clamp(percent, 0.0, 100.0) * 255 / 100);
    set_color(f, (f->color & 0xffffff00) | alpha);
}

/* Enter in the hex field applies it; popover.c takes Escape and Tab. */
static void picker_key(color_field *f, const rml_event *e)
{
    bool enter = e->key == RML_KEY_RETURN || e->key == RML_KEY_NUMPADENTER;
    if (enter && e->target == f->hex) {
        hex_typed(f);
        rml_element_blur(f->hex);
        rml_event_stop_propagation(e);
    }
}

static void field_key(color_field *f, const rml_event *e)
{
    bool enter = e->key == RML_KEY_RETURN || e->key == RML_KEY_NUMPADENTER;
    if (locked(f) || (e->key != RML_KEY_SPACE && !enter)) {
        return;
    }
    if (f->popover) {
        close_picker(f, false);
    } else {
        open_picker(f, true);
    }
    rml_event_stop_propagation(e);
}

static void press(color_field *f, const rml_event *e)
{
    for (rml_element *el = e->target; el && el != f->el; el = rml_element_parent(el)) {
        if (rml_element_has_attribute(el, "data-part")) {
            f->part = (int)control_number(el, "data-part", PART_NONE);
            drag_part(f, e->mouse_x, e->mouse_y);
            return;
        }
    }
}

static bool in_picker(const color_field *f, const rml_element *target)
{
    return f->popover && control_within(target, f->popover);
}

static void on_event(void *, const rml_event *e)
{
    color_field *f = field_of(e->current);
    bool inside = in_picker(f, e->target);
    switch (e->id) {
        case RML_EVENT_KEYDOWN:
            if (inside) {
                picker_key(f, e);
            } else if (e->target == f->el) {
                field_key(f, e);
            }
            return;
        case RML_EVENT_MOUSEDOWN:
            if (inside && e->button == 0) {
                press(f, e);
            }
            return;
        case RML_EVENT_DRAG:
        case RML_EVENT_DRAGEND:
            if (f->part && e->id == RML_EVENT_DRAGEND) {
                f->part = PART_NONE;
            } else if (f->part && f->popover) {
                drag_part(f, e->mouse_x, e->mouse_y);
            }
            return;
        case RML_EVENT_CLICK:
            if (inside || control_within(e->target, f->leaving) || locked(f)) {
                return;
            }
            if (f->popover) {
                close_picker(f, false);
                return;
            }
            open_picker(f, false);
            return;
        case RML_EVENT_INPUT:
            /* The picker's fields hear their own edits first; the field's
               input is its color moving. */
            if (e->target != f->el) {
                rml_event_stop_propagation(e);
            }
            return;
        case RML_EVENT_CHANGE:
            /* The picker's own fields change inside it; the field's change is
               its picker closing. */
            if (inside && e->target == f->hex) {
                hex_typed(f);
            }
            if (e->target != f->el) {
                rml_event_stop_propagation(e);
            }
            return;
        default: return;
    }
}

/* The Opacity field's input. */
static void on_input(void *, const rml_event *e)
{
    rml_element *field = rml_element_parent(e->current);
    color_field *f = field_of(field);
    if (e->target == f->opacity) {
        opacity_input(f);
    }
    rml_event_stop_propagation(e);
}

/* ---------- Element ---------- */

static void read_attributes(color_field *f)
{
    char text[16];
    control_string(f->el, "value", text, sizeof text);
    uint32_t rgba = 0;
    size_t digits;
    f->model = parse_hex(text, &rgba, &digits) ? rgba : 0;
    f->mixed = control_bool(f->el, "mixed");
    f->readonly = control_bool(f->el, "readonly");
    f->disabled = control_bool(f->el, "disabled");
    f->dirty = false;
    if (locked(f)) {
        close_picker(f, false);
    }
    if (!f->popover) {
        f->color = f->model;
    }
    show_swatch(f);
}

static color_field *field_new(rml_element *el)
{
    color_field *f = xcalloc(1, sizeof *f);
    f->el = el;
    f->dirty = true;
    rml_element_set_user(el, f);
    control_part(el, "div", "mark");
    f->swatch = control_part(el, "div", "swatch");
    static const rml_event_id EVENTS[] = {
        RML_EVENT_MOUSEDOWN,
        RML_EVENT_DRAG,
        RML_EVENT_DRAGEND,
        RML_EVENT_CLICK,
    };
    for (size_t i = 0; i < sizeof EVENTS / sizeof *EVENTS; i++) {
        rml_element_add_event_listener(el, EVENTS[i], listener, false);
    }
    /* Keys reach the picker before its controls do, as a popover's do, and
       its controls' changes stop at the field. */
    rml_element_add_event_listener(el, RML_EVENT_KEYDOWN, listener, true);
    rml_element_add_event_listener(el, RML_EVENT_CHANGE, listener, true);
    rml_element_add_event_listener(el, RML_EVENT_INPUT, listener, false);
    rml_element_set_attribute(el, "role", "button");
    rml_element_set_attribute(el, "aria-haspopup", "dialog");
    rml_element_set_attribute(el, "aria-expanded", "false");
    return f;
}

static void field_update(rml_element *el, void *user)
{
    color_field *f = user ? user : field_new(el);
    if (f->dirty) {
        read_attributes(f);
    }
    if (f->leaving && rml_elapsed_time() >= f->leave_at) {
        drop_leaving(f);
    } else if (f->leaving) {
        rml_context_request_next_update(rml_element_context(el), f->leave_at - rml_elapsed_time());
    }
    /* popover.c closed it: an outside press, Escape or Tab past its ends. */
    if (f->popover && !popover_open(f->popover)) {
        close_picker(f, false);
    }
    if (!f->select_hex) {
        return;
    }
    /* popover.c focuses its first control once placed: the hex field. */
    if (rml_context_focus_element(rml_element_context(el)) == f->hex) {
        rml_element_select(f->hex);
        f->select_hex = false;
        return;
    }
    rml_context_request_next_update(rml_element_context(el), 0);
}

static void field_attribute(rml_element *, void *user, const char *name)
{
    color_field *f = user;
    static const char *const NAMES[] = { "value", "mixed", "readonly", "disabled" };
    for (size_t i = 0; f && i < sizeof NAMES / sizeof *NAMES; i++) {
        f->dirty |= !strcmp(name, NAMES[i]);
    }
}

static void field_release(void *user)
{
    free(user);
}

static const rml_element_hooks HOOKS = { .release = field_release,
                                         .update = field_update,
                                         .attribute = field_attribute };

void color_field_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&HOOKS);
        listener = rml_listener_create(on_event, NULL);
        input_listener = rml_listener_create(on_input, NULL);
    }
    rml_register_element_instancer("color-field", instancer);
}

/* ---------- C owners ---------- */

bool color_field_take(rml_element *field, uint32_t *rgba, bool *committed)
{
    color_field *f = field_of(field);
    *committed = f && f->committed;
    bool pending = f && f->pending;
    if (pending) {
        *rgba = f->color;
    }
    if (f) {
        f->pending = f->committed = false;
    }
    return pending;
}

bool color_field_open(const rml_element *field)
{
    const color_field *f = field_of(field);
    return f && f->popover;
}
