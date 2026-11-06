/* <number-field> (controls.h), after Base UI's NumberField and Blender's
 * number buttons: drag to scrub, click to type a value or an expression,
 * step with the arrow keys or the held steppers, and Tab to the next field.
 * Bounded sliders scrub across their width and draw a fill.
 *
 * Its value attribute is the model's value. While idle it shows that; while
 * a gesture runs it shows its own and sends input, and once the gesture
 * ends it sends change and shows the model's value again at its next
 * update, so a model that refused the edit shows what it kept. */
#include "support/math.h"
#include "support/number_expr.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"
#include "ui/input_feel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Held steppers repeat after 400 ms, every 60 ms (Base UI NumberField). */
#define STEP_REPEAT_DELAY 0.4
#define STEP_REPEAT_INTERVAL 0.06
/* Unity's mixed value. */
#define MIXED_TEXT "\xe2\x80\x94"
#define PART_TEXT 64

typedef struct number_field
{
    rml_element *el, *fill, *addon, *value_el, *unit, *input;
    /* From the attributes. */
    double model, min, max, step;
    int precision;
    bool integer, single, slider, mixed, readonly, disabled, invalid;
    char addon_text[PART_TEXT], unit_text[PART_TEXT];
    /* Attributes changed since the last update read them: any, or only the
       value, which a Play refresh changes and reads alone. */
    bool dirty, value_dirty;
    /* The gesture. */
    double value, start, step_next;
    double scrub_step; /* the step as the scrub began, which its owner may change */
    int step_dir;
    unsigned step_modifiers;
    float press_x;
    bool editing, scrubbing, pressing, suppress_click, typed_invalid;
    /* What C owners take (number_field_take). */
    bool pending, committed;
    double pending_value;
} number_field;

static rml_listener *listener;

static number_field *field_of(const rml_element *el)
{
    return rml_element_user(el);
}

/* ---------- Values ---------- */

static bool bounded(const number_field *f)
{
    return isfinite(f->min) && isfinite(f->max);
}

static bool is_slider(const number_field *f)
{
    return f->slider && bounded(f);
}

static bool locked(const number_field *f)
{
    return f->readonly || f->disabled;
}

static double clamp_value(const number_field *f, double v)
{
    v = math_clamp(v, f->min, f->max);
    if (f->integer) {
        v = round(v);
    }
    return f->single ? (double)(float)v : v;
}

static double step_size(const number_field *f)
{
    return f->step > 0 ? f->step : 1;
}

/* Several values show a dash until a scrub or a step sets one. */
static bool mixed_shown(const number_field *f)
{
    return f->mixed && !f->scrubbing && !f->step_dir;
}

static void reflect(number_field *f)
{
    rml_element *el = f->el;
    control_flag(el, "data-editing", f->editing);
    control_flag(el, "data-scrubbing", f->scrubbing);
    control_flag(el, "data-invalid", f->invalid || f->typed_invalid);
    control_flag(el, "data-readonly", f->readonly);
    control_flag(el, "data-disabled", f->disabled);
    control_flag(el, "data-slider", is_slider(f));
    control_flag(el, "data-mixed", mixed_shown(f));
}

static void display(number_field *f)
{
    reflect(f);
    if (!f->editing) {
        char text[NUMBER_TEXT];
        number_format(text, f->value, f->precision, f->integer);
        line_text_set_right(f->value_el, mixed_shown(f) ? MIXED_TEXT : text);
    }
    if (is_slider(f)) {
        double range = f->max - f->min;
        double t = f->max > f->min ? (f->value - f->min) / range : 0;
        char width[32];
        snprintf(width, sizeof width, "%.2f%%", math_clamp(t, 0.0, 1.0) * 100);
        rml_element_set_property(f->fill, "width", width);
    }
}

/* A gesture sets the value: it shows, and input reports it. */
static void set_value(number_field *f, double v)
{
    f->value = clamp_value(f, v);
    f->pending = true;
    f->pending_value = f->value;
    if (f->mixed) {
        f->mixed = false;
        rml_element_remove_attribute(f->el, "mixed");
    }
    display(f);
    control_redraw(f->el);
    control_dispatch_number(f->el, "input", f->value, false);
}

/* The gesture ends: change reports its value once, and the next update
 * shows the model's. */
static void commit(number_field *f, bool canceled)
{
    f->committed = true;
    f->value_dirty = true;
    control_redraw(f->el);
    control_dispatch_number(f->el, "change", f->value, canceled);
}

static void step_by(number_field *f, int dir, unsigned modifiers)
{
    double m = modifiers & RML_KM_ALT ? 0.1 : modifiers & RML_KM_SHIFT ? 10 : 1;
    double v = f->value + dir * step_size(f) * m;
    set_value(f, round(v * 1e6) / 1e6);
}

/* ---------- Typing ---------- */

/* The shortest text that reads back as the value. */
static void edit_text(const number_field *f, char text[static NUMBER_TEXT])
{
    if (f->integer) {
        snprintf(text, NUMBER_TEXT, "%.0f", f->value);
        return;
    }
    int digits = f->single ? 9 : 17;
    for (int precision = 1; precision <= digits; precision++) {
        snprintf(text, NUMBER_TEXT, "%.*g", precision, f->value);
        double parsed = strtod(text, NULL);
        if (f->single) {
            parsed = (double)(float)parsed;
        }
        if (parsed == f->value) {
            return;
        }
    }
}

/* initial NULL selects the current value; text starts typed with the caret
 * after it. */
static void begin_edit(number_field *f, const char *initial)
{
    if (f->editing || locked(f)) {
        return;
    }
    f->editing = true;
    reflect(f);
    rml_element_set_property(f->value_el, "display", "none");
    f->input = rml_element_create_child(f->el, "input", f->value_el);
    rml_element_set_attribute(f->input, "type", "text");
    rml_element_set_class_names(f->input, "part value edit");
    char label[256];
    control_string(f->el, "aria-label", label, sizeof label);
    if (label[0]) {
        rml_element_set_attribute(f->input, "aria-label", label);
    }
    char text[NUMBER_TEXT] = "";
    if (!initial && !f->mixed) {
        edit_text(f, text);
    }
    rml_element_set_value(f->input, initial ? initial : text);
    rml_element_focus(f->input, true);
    if (initial) {
        int end = (int)strlen(initial);
        rml_element_set_selection_range(f->input, end, end);
    } else {
        rml_element_select(f->input);
    }
    f->start = f->value;
}

/* Applies the typed value; false marks it invalid and keeps editing. */
static bool accept(number_field *f)
{
    char text[256];
    rml_element_value(f->input, text, sizeof text);
    double v = NAN;
    if (number_expr_eval(text, f->value, &v)) {
        v = clamp_value(f, v);
    }
    if (!isfinite(v)) {
        f->typed_invalid = true;
        reflect(f);
        rml_element_select(f->input);
        return false;
    }
    if (v != f->value) {
        set_value(f, v);
    }
    commit(f, false);
    return true;
}

static void end_edit(number_field *f, bool keep, bool refocus)
{
    if (!f->editing || (keep && !accept(f))) {
        return;
    }
    f->editing = false;
    f->typed_invalid = false;
    rml_element *input = f->input;
    f->input = NULL;
    rml_element_set_property(f->value_el, "display", "block");
    if (refocus) {
        rml_element_focus(f->el, true);
    }
    rml_element_remove_child(f->el, input);
    display(f);
}

static void tab(number_field *f, int dir)
{
    end_edit(f, true, true);
    if (f->editing) {
        return;
    }
    focus_step(rml_element_owner_document(f->el), f->el, dir, false, NULL);
}

static void edit_key(number_field *f, const rml_event *e)
{
    switch (e->key) {
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER: end_edit(f, true, true); break;
        case RML_KEY_ESCAPE: end_edit(f, false, true); break;
        case RML_KEY_TAB: tab(f, e->modifiers & RML_KM_SHIFT ? -1 : 1); break;
        default: return;
    }
    rml_event_stop_propagation(e);
}

static bool typing_key(rml_key key)
{
    return (key >= RML_KEY_0 && key <= RML_KEY_9) || key == RML_KEY_OEM_MINUS ||
           key == RML_KEY_OEM_PERIOD || key == RML_KEY_SUBTRACT;
}

/* ---------- Scrubbing and steppers ---------- */

/* The value a scrub dx dp from the press gives. */
static double scrub_value(const number_field *f, float dx, unsigned modifiers)
{
    bool slider = is_slider(f);
    float width = rml_element_box_size(f->el, RML_BOX_BORDER).x / control_dp(f->el);
    double range = f->max - f->min;
    double per_dp = slider ? range / math_max(width, 1.f) : f->scrub_step / 2;
    double m = modifiers & RML_KM_ALT                ? 0.1
             : (modifiers & RML_KM_SHIFT) && !slider ? 10
                                                     : 1;
    double v = f->start + dx * per_dp * m;
    if (modifiers & RML_KM_CTRL) {
        double snap = slider ? range / 10 : f->scrub_step * 10;
        v = round(v / snap) * snap;
    }
    return round(v * 1e6) / 1e6;
}

/* Escape puts a scrub's starting value back. */
static void cancel_scrub(number_field *f)
{
    f->pressing = f->scrubbing = false;
    f->suppress_click = true;
    set_value(f, f->start);
    commit(f, true);
}

static void end_stepping(number_field *f)
{
    if (!f->step_dir) {
        return;
    }
    f->step_dir = 0;
    commit(f, false);
}

/* The step arrow the press is on, or NULL. */
static rml_element *stepper(const number_field *f, rml_element *target)
{
    for (rml_element *el = target; el && el != f->el; el = rml_element_parent(el)) {
        if (rml_element_is_class_set(el, "step")) {
            return el;
        }
    }
    return NULL;
}

static void press(number_field *f, const rml_event *e)
{
    if (e->button != 0 || f->editing || locked(f)) {
        return;
    }
    f->start = f->value;
    f->suppress_click = false;
    rml_element *arrow = stepper(f, e->target);
    if (arrow) {
        f->step_dir = rml_element_is_class_set(arrow, "inc") ? 1 : -1;
        f->step_modifiers = e->modifiers;
        step_by(f, f->step_dir, e->modifiers);
        f->step_next = rml_elapsed_time() + STEP_REPEAT_DELAY;
        rml_context_request_next_update(rml_element_context(f->el), STEP_REPEAT_DELAY);
        f->suppress_click = true;
        return;
    }
    f->pressing = true;
    f->scrubbing = false;
    f->press_x = e->mouse_x;
}

static void release(number_field *f)
{
    end_stepping(f);
    if (f->pressing && !f->scrubbing) {
        f->pressing = false;
    }
}

static void click(number_field *f)
{
    if (f->suppress_click) {
        f->suppress_click = false;
        return;
    }
    begin_edit(f, NULL);
}

static void drag(number_field *f, const rml_event *e)
{
    if (!f->pressing) {
        if (e->id == RML_EVENT_DRAGEND) {
            end_stepping(f);
        }
        return;
    }
    if (e->id == RML_EVENT_DRAGEND) {
        bool scrubbed = f->scrubbing;
        f->pressing = f->scrubbing = false;
        if (scrubbed) {
            /* RmlUi clicks before dragend; the release must not start
               editing. */
            f->suppress_click = true;
            commit(f, false);
            display(f);
        }
        return;
    }
    float dx = (e->mouse_x - f->press_x) / control_dp(f->el);
    if (!f->scrubbing && fabsf(dx) >= FEEL_DRAG_DP) {
        f->scrubbing = true;
        f->suppress_click = true;
        f->start = f->value;
        f->scrub_step = step_size(f);
    }
    if (f->scrubbing) {
        set_value(f, scrub_value(f, dx, e->modifiers));
    }
}

static void repeat_step(number_field *f)
{
    double now = rml_elapsed_time();
    if (now >= f->step_next) {
        step_by(f, f->step_dir, f->step_modifiers);
        f->step_next = now + STEP_REPEAT_INTERVAL;
    }
    rml_context_request_next_update(rml_element_context(f->el), f->step_next - now);
}

/* ---------- Keys ---------- */

static void key(number_field *f, const rml_event *e)
{
    if (f->editing) {
        edit_key(f, e);
        return;
    }
    if (f->scrubbing && e->key == RML_KEY_ESCAPE) {
        cancel_scrub(f);
        rml_event_stop_propagation(e);
        return;
    }
    if (locked(f)) {
        return;
    }
    unsigned mods = e->modifiers;
    switch (e->key) {
        case RML_KEY_UP:
        case RML_KEY_RIGHT: step_by(f, 1, mods); break;
        case RML_KEY_DOWN:
        case RML_KEY_LEFT: step_by(f, -1, mods); break;
        case RML_KEY_PRIOR: step_by(f, 1, RML_KM_SHIFT); break;
        case RML_KEY_NEXT: step_by(f, -1, RML_KM_SHIFT); break;
        case RML_KEY_HOME:
        case RML_KEY_END:
            if (!bounded(f)) {
                return;
            }
            set_value(f, e->key == RML_KEY_HOME ? f->min : f->max);
            break;
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER:
            begin_edit(f, NULL);
            rml_event_stop_propagation(e);
            return;
        default:
            if (typing_key(e->key) && !(mods & (RML_KM_CTRL | RML_KM_META))) {
                begin_edit(f, "");
            }
            return;
    }
    commit(f, false);
    rml_event_stop_propagation(e);
}

/* Leaving the typed text keeps a valid value and drops an invalid one. */
static void blur(number_field *f, const rml_event *e)
{
    if (!f->editing || e->target != f->input) {
        return;
    }
    end_edit(f, true, false);
    end_edit(f, false, false);
}

static void on_event(void *, const rml_event *e)
{
    number_field *f = field_of(e->current);
    switch (e->id) {
        case RML_EVENT_MOUSEDOWN: press(f, e); return;
        case RML_EVENT_MOUSEUP: release(f); return;
        case RML_EVENT_CLICK: click(f); return;
        case RML_EVENT_DRAGSTART:
        case RML_EVENT_DRAG:
        case RML_EVENT_DRAGEND: drag(f, e); return;
        case RML_EVENT_KEYDOWN: key(f, e); return;
        case RML_EVENT_BLUR: blur(f, e); return;
        case RML_EVENT_INPUT:
        case RML_EVENT_CHANGE:
            /* The typed text's edits stay inside; the field's input and
               change are its own. */
            if (e->target != f->el) {
                rml_event_stop_propagation(e);
            }
            return;
        default: return;
    }
}

static const rml_event_id EVENTS[] = {
    RML_EVENT_MOUSEDOWN, RML_EVENT_MOUSEUP, RML_EVENT_CLICK,
    RML_EVENT_DRAGSTART, RML_EVENT_DRAG,    RML_EVENT_DRAGEND,
};

/* ---------- Element ---------- */

static void build(number_field *f)
{
    rml_element *el = f->el;
    f->fill = control_part(el, "div", "fill");
    control_part(el, "div", "mark");
    f->addon = control_part(el, "span", "part addon");
    f->value_el = control_part(el, "span", "part value");
    f->unit = control_part(el, "span", "part unit");
    control_icon(control_part(el, "span", "step dec"), GLYPH_LEFT);
    control_icon(control_part(el, "span", "step inc"), GLYPH_RIGHT);
    for (size_t i = 0; i < sizeof EVENTS / sizeof *EVENTS; i++) {
        rml_element_add_event_listener(el, EVENTS[i], listener, false);
    }
    /* The typed input's keys reach the field before the input takes them,
       its blur, which does not bubble, as the field captures, and its
       input and changes stop there. */
    rml_element_add_event_listener(el, RML_EVENT_KEYDOWN, listener, true);
    rml_element_add_event_listener(el, RML_EVENT_BLUR, listener, true);
    rml_element_add_event_listener(el, RML_EVENT_INPUT, listener, true);
    rml_element_add_event_listener(el, RML_EVENT_CHANGE, listener, true);
    rml_element_set_property(el, "drag", "drag");
}

/* A part shows as a block, hidden when its text is empty. */
static void part_text(rml_element *part, char *shown, const char *text)
{
    if (!strcmp(shown, text)) {
        return;
    }
    snprintf(shown, PART_TEXT, "%s", text);
    control_text(part, text);
    rml_element_set_property(part, "display", text[0] ? "block" : "none");
}

static void read_attributes(number_field *f)
{
    rml_element *el = f->el;
    f->dirty = false;
    f->min = control_number(el, "min", -INFINITY);
    f->max = control_number(el, "max", INFINITY);
    f->step = control_number(el, "step", 0);
    f->precision = (int)control_number(el, "precision", 0);
    f->integer = control_bool(el, "integer");
    f->single = control_bool(el, "float");
    f->slider = control_bool(el, "slider");
    f->mixed = control_bool(el, "mixed");
    f->readonly = control_bool(el, "readonly");
    f->disabled = control_bool(el, "disabled");
    f->invalid = control_bool(el, "invalid");
    char text[PART_TEXT];
    control_string(el, "addon", text, sizeof text);
    part_text(f->addon, f->addon_text, text);
    control_string(el, "unit", text, sizeof text);
    part_text(f->unit, f->unit_text, text);
    rml_element_set_property(f->fill, "display", is_slider(f) ? "block" : "none");
    rml_element_set_property(el, "tab-index", f->disabled ? "none" : "auto");
}

static void apply_attributes(number_field *f)
{
    if (f->dirty) {
        read_attributes(f);
    }
    f->value_dirty = false;
    double model = control_number(f->el, "value", 0);
    f->model = f->single ? (double)(float)model : model;
    if (locked(f)) {
        end_edit(f, false, false);
    }
    if (!f->scrubbing && !f->editing && !f->step_dir) {
        f->value = f->model;
    }
    display(f);
}

static number_field *field_new(rml_element *el)
{
    number_field *f = xcalloc(1, sizeof *f);
    f->el = el;
    f->addon_text[0] = f->unit_text[0] = 1; /* nothing shown yet */
    rml_element_set_user(el, f);
    build(f);
    f->dirty = true;
    return f;
}

static void field_update(rml_element *el, void *user)
{
    number_field *f = user ? user : field_new(el);
    if (f->dirty || f->value_dirty) {
        apply_attributes(f);
    }
    if (f->step_dir) {
        repeat_step(f);
    }
}

static bool contract_attribute(const char *name)
{
    static const char *const NAMES[] = {
        "min",  "max",   "step",  "precision", "integer",  "float",   "slider",
        "unit", "addon", "mixed", "readonly",  "disabled", "invalid",
    };
    for (size_t i = 0; i < sizeof NAMES / sizeof *NAMES; i++) {
        if (!strcmp(name, NAMES[i])) {
            return true;
        }
    }
    return false;
}

static void field_attribute(rml_element *, void *user, const char *name)
{
    number_field *f = user;
    if (!f) {
        return;
    }
    f->value_dirty |= !strcmp(name, "value");
    f->dirty |= contract_attribute(name);
}

static void field_release(void *user)
{
    free(user);
}

static const rml_element_hooks HOOKS = { .release = field_release,
                                         .update = field_update,
                                         .attribute = field_attribute };

void number_field_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&HOOKS);
        listener = rml_listener_create(on_event, NULL);
    }
    rml_register_element_instancer("number-field", instancer);
}

/* ---------- C owners ---------- */

bool number_field_take(rml_element *field, double *value, bool *committed)
{
    number_field *f = field_of(field);
    *committed = f && f->committed;
    bool pending = f && f->pending;
    if (pending) {
        *value = f->pending_value;
    }
    if (f) {
        f->pending = f->committed = false;
    }
    return pending;
}

bool number_field_active(const rml_element *field)
{
    const number_field *f = field_of(field);
    return f && (f->scrubbing || f->editing || f->step_dir);
}
