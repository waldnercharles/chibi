/* Number fields after Base UI NumberField: drag to scrub, click to type a value
 * or an expression, step with the arrow keys or the held steppers, and Tab to
 * the next field. Bounded sliders scrub across their width and draw a fill. */
#include "editor/rmlui/kit_internal.h"
#include "editor/ui/kit_icons.h"
#include "support/math.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Expressions ---------- */

/* Numeric entry accepts arithmetic: "=2*pi", "120*1.5", or "*2" relative to
 * the current value. */
typedef struct expr
{
    const char *p;
    bool ok;
} expr;

static double expr_sum(expr *e);

static void expr_space(expr *e)
{
    while (*e->p == ' ') {
        e->p++;
    }
}

static bool expr_pi(const char *p)
{
    return (p[0] == 'p' || p[0] == 'P') && (p[1] == 'i' || p[1] == 'I');
}

static double expr_primary(expr *e)
{
    expr_space(e);
    if (*e->p == '(') {
        e->p++;
        double v = expr_sum(e);
        expr_space(e);
        e->ok &= *e->p == ')';
        if (*e->p) {
            e->p++;
        }
        return v;
    }
    if (*e->p == '-') {
        e->p++;
        return -expr_primary(e);
    }
    if (*e->p == '+') {
        e->p++;
        return expr_primary(e);
    }
    if (expr_pi(e->p)) {
        e->p += 2;
        return M_PI;
    }
    char *end = NULL;
    double v = strtod(e->p, &end);
    e->ok &= end != e->p;
    e->p = end;
    return v;
}

static double expr_product(expr *e)
{
    double v = expr_primary(e);
    for (;;) {
        expr_space(e);
        char op = *e->p;
        if (op != '*' && op != '/') {
            return v;
        }
        e->p++;
        double rhs = expr_primary(e);
        v = op == '*' ? v * rhs : v / rhs;
    }
}

static double expr_sum(expr *e)
{
    double v = expr_product(e);
    for (;;) {
        expr_space(e);
        char op = *e->p;
        if (op != '+' && op != '-') {
            return v;
        }
        e->p++;
        double rhs = expr_product(e);
        v = op == '+' ? v + rhs : v - rhs;
    }
}

static double parse(const char *s)
{
    if (!s[0]) {
        return NAN;
    }
    expr e = { s, true };
    double v = expr_sum(&e);
    expr_space(&e);
    return e.ok && !*e.p && isfinite(v) ? v : NAN;
}

static double evaluate(const char *text, double current)
{
    while (*text == ' ') {
        text++;
    }
    if (*text == '=') {
        text++;
    }
    char *s = NULL;
    if (*text == '*' || *text == '/' || *text == '+') {
        char base[NUMBER_TEXT];
        format_number(base, current, 12, false);
        str_cat(&s, base);
    }
    str_cat(&s, text);
    double v = parse(s);
    free(s);
    return v;
}

bool kit_number_parse(const char *text, double current, double *out)
{
    assert(out);
    double value = evaluate(text ? text : "", current);
    if (!isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

/* ---------- Values ---------- */

static bool number_bounded(const widget *w)
{
    return w->desc.min != 0 || w->desc.max != 0;
}

static bool number_slider(const widget *w)
{
    return w->desc.slider && number_bounded(w);
}

static double clamp_number(const widget *w, double v)
{
    if (number_bounded(w)) {
        v = math_clamp(v, w->desc.min, w->desc.max);
    }
    if (w->desc.integer) {
        v = round(v);
    }
    return v;
}

static double step_size(const widget *w)
{
    return w->desc.step > 0 ? w->desc.step : 1;
}

static void number_display(node *n)
{
    widget *w = &n->w;
    if (!w->editing) {
        char text[NUMBER_TEXT];
        format_number(text, w->value, w->desc.precision, w->desc.integer);
        set_element_text(w->value_el, text);
    }
    double range = w->desc.max - w->desc.min;
    double t = w->desc.max > w->desc.min ? (w->value - w->desc.min) / range : 0;
    char width[32];
    snprintf(width, sizeof width, "%.2f%%", math_clamp(t, 0.0, 1.0) * 100);
    rml_element_set_property(w->fill_el, "width", width);
}

static void number_change(node *n, double v)
{
    widget *w = &n->w;
    v = clamp_number(w, v);
    w->value = v;
    w->pending = true;
    w->pending_value = v;
}

static bool number_locked(const node *n)
{
    return n->w.desc.flags & (KIT_READONLY | KIT_DISABLED);
}

static void number_step(node *n, int dir, unsigned modifiers)
{
    double m = modifiers & KIT_MOD_ALT   ? 0.1
             : modifiers & KIT_MOD_SHIFT ? 10
                                         : 1;
    double v = n->w.value + dir * step_size(&n->w) * m;
    number_change(n, round(v * 1e6) / 1e6);
}

/* ---------- Editing ---------- */

/* initial NULL selects the current value; text starts typed with the caret
 * after it. */
static void number_begin_edit(node *n, const char *initial)
{
    widget *w = &n->w;
    if (w->editing || number_locked(n)) {
        return;
    }
    w->editing = true;
    set_flag(n, "data-editing", true);
    rml_element_set_property(w->value_el, "display", "none");
    w->input_el = rml_element_create_child(n->el, "input", w->value_el);
    rml_element_set_attribute(w->input_el, "type", "text");
    rml_element_set_class_names(w->input_el, "part value edit");
    char text[NUMBER_TEXT];
    if (!initial) {
        int precision = math_max(w->desc.precision, 3);
        format_number(text, w->value, precision, w->desc.integer);
    }
    rml_element_set_value(w->input_el, initial ? initial : text);
    rml_element_focus(w->input_el, true);
    if (!initial) {
        rml_element_select(w->input_el);
    } else {
        int end = (int)strlen(initial);
        rml_element_set_selection_range(w->input_el, end, end);
    }
    w->start = w->value;
}

/* Applies the typed value; false marks it invalid and keeps editing. */
static bool number_accept(node *n)
{
    widget *w = &n->w;
    char *text = element_value(w->input_el);
    double v = evaluate(text, w->value);
    free(text);
    if (!isfinite(v)) {
        set_flag(n, "data-invalid", true);
        rml_element_select(w->input_el);
        return false;
    }
    if (clamp_number(w, v) != w->value) {
        number_change(n, v);
    }
    w->committed = true;
    return true;
}

static void number_end_edit(node *n, bool accept, bool refocus)
{
    widget *w = &n->w;
    if (!w->editing || (accept && !number_accept(n))) {
        return;
    }
    w->editing = false;
    set_flag(n, "data-invalid", false);
    set_flag(n, "data-editing", false);
    rml_element *input = w->input_el;
    w->input_el = NULL;
    rml_element_set_property(w->value_el, "display", "block");
    if (refocus) {
        rml_element_focus(n->el, true);
    }
    rml_element_remove_child(n->el, input);
    number_display(n);
}

static void collect_numbers(node *n, node ***fields)
{
    if (n->kind == KIND_NUMBER && !number_locked(n)) {
        apush(*fields, n);
    }
    for (int i = 0; i < acount(n->children); i++) {
        collect_numbers(n->children[i], fields);
    }
}

/* The editable field dir steps from `from`, wrapping, in document order
 * across roots; NULL when there is no other. */
static node *next_number(const kit *k, const node *from, int dir)
{
    node **fields = NULL;
    for (int i = 0; i < acount(k->roots); i++) {
        collect_numbers(k->roots[i], &fields);
    }
    int count = acount(fields);
    int at = nodes_index(fields, from);
    node *next = NULL;
    if (at >= 0 && count >= 2) {
        next = fields[((at + dir) % count + count) % count];
    }
    afree(fields);
    return next;
}

static void number_tab(kit *k, node *n, unsigned modifiers)
{
    number_end_edit(n, true, false);
    if (n->w.editing) {
        return;
    }
    node *next = next_number(k, n, modifiers & KIT_MOD_SHIFT ? -1 : 1);
    if (next) {
        number_begin_edit(next, NULL);
    }
}

static void edit_key(kit *k, node *n, const rml_event *e)
{
    switch (e->key) {
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER: number_end_edit(n, true, true); break;
        case RML_KEY_ESCAPE: number_end_edit(n, false, true); break;
        case RML_KEY_TAB: number_tab(k, n, event_modifiers(e)); break;
        default: return;
    }
    rml_event_stop_propagation(e);
}

static bool typing_key(rml_key key)
{
    return (key >= RML_KEY_0 && key <= RML_KEY_9) || key == RML_KEY_OEM_MINUS ||
           key == RML_KEY_OEM_PERIOD || key == RML_KEY_SUBTRACT;
}

/* ---------- Events ---------- */

void number_key(kit *k, node *n, const rml_event *e)
{
    widget *w = &n->w;
    if (w->editing) {
        edit_key(k, n, e);
        return;
    }
    if (number_locked(n)) {
        return;
    }
    unsigned mods = event_modifiers(e);
    bool bounded = number_bounded(w);
    switch (e->key) {
        case RML_KEY_UP:
        case RML_KEY_RIGHT: number_step(n, 1, mods); break;
        case RML_KEY_DOWN:
        case RML_KEY_LEFT: number_step(n, -1, mods); break;
        case RML_KEY_HOME:
            if (!bounded) {
                return;
            }
            number_change(n, w->desc.min);
            break;
        case RML_KEY_END:
            if (!bounded) {
                return;
            }
            number_change(n, w->desc.max);
            break;
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER:
            number_begin_edit(n, NULL);
            rml_event_stop_propagation(e);
            return;
        default:
            if (typing_key(e->key) && !(mods & (KIT_MOD_CTRL | KIT_MOD_META))) {
                number_begin_edit(n, "");
            }
            return;
    }
    w->committed = true;
    rml_event_stop_propagation(e);
}

/* Held steppers repeat after 400 ms, every 60 ms (Base UI NumberField). */
void repeat_steppers(kit *k)
{
    for (int i = 0; i < cf_map_size(k->by_path); i++) {
        node *n = k->by_path[i];
        if (n->kind != KIND_NUMBER || !n->w.step_dir) {
            continue;
        }
        if (!k->in.down[0]) {
            n->w.step_dir = 0;
            n->w.committed = true;
            continue;
        }
        if (k->in.time >= n->w.step_next) {
            number_step(n, n->w.step_dir, k->in.modifiers);
            n->w.step_next = k->in.time + STEP_REPEAT_INTERVAL;
        }
    }
}

void number_press(kit *k, node *n, const rml_event *e)
{
    widget *w = &n->w;
    if (w->editing || number_locked(n)) {
        return;
    }
    rml_element *step = e->target;
    while (step && step != n->el && !rml_element_is_class_set(step, "step")) {
        step = rml_element_parent(step);
    }
    w->start = w->value;
    w->suppress_click = false;
    if (step && step != n->el) {
        w->step_dir = rml_element_is_class_set(step, "inc") ? 1 : -1;
        number_step(n, w->step_dir, event_modifiers(e));
        w->step_next = k->in.time + STEP_REPEAT_DELAY;
        w->suppress_click = true;
        return;
    }
    w->pressing = true;
    w->scrubbing = false;
    w->press_x = e->mouse_x;
}

void number_release(node *n)
{
    if (n->w.pressing && !n->w.scrubbing) {
        n->w.pressing = false;
    }
}

void number_click(kit *, node *n)
{
    if (n->w.suppress_click) {
        n->w.suppress_click = false;
        return;
    }
    number_begin_edit(n, NULL);
}

/* The value a scrub dx dp from the press gives. */
static double scrub_value(const kit *k, const node *n, float dx, unsigned modifiers)
{
    const widget *w = &n->w;
    bool slider = number_slider(w);
    kit_rect r = element_rect(k, n->el);
    double range = w->desc.max - w->desc.min;
    double per_px = slider ? range / math_max(r.w, 1.f) : step_size(w) / 2;
    double m = modifiers & KIT_MOD_ALT                ? 0.1
             : (modifiers & KIT_MOD_SHIFT) && !slider ? 10
                                                      : 1;
    double v = w->start + dx * per_px * m;
    if (modifiers & KIT_MOD_CTRL) {
        double snap = slider ? range / 10 : step_size(w) * 10;
        v = round(v / snap) * snap;
    }
    return round(v * 1e6) / 1e6;
}

void number_drag(kit *k, node *n, const rml_event *e)
{
    widget *w = &n->w;
    if (!w->pressing) {
        return;
    }
    if (e->id == RML_EVENT_DRAGEND) {
        if (w->scrubbing) {
            w->committed = true;
            w->suppress_click = true;
        }
        w->pressing = w->scrubbing = false;
        str_set(&k->surface_capture, "");
        return;
    }
    float dx = (e->mouse_x - w->press_x) / dp(k);
    if (!w->scrubbing && fabsf(dx) >= SCRUB_THRESHOLD) {
        /* RmlUi dispatches click before dragend; the release must not start
           editing. */
        w->scrubbing = true;
        w->suppress_click = true;
        w->start = w->value;
    }
    if (!w->scrubbing) {
        return;
    }
    number_change(n, scrub_value(k, n, dx, event_modifiers(e)));
}

void number_blur(kit *, node *n, rml_element *target)
{
    if (!n->w.editing || target != n->w.input_el) {
        return;
    }
    number_end_edit(n, true, false);
    if (n->w.editing) {
        number_end_edit(n, false, false);
    }
}

/* ---------- Field ---------- */

static void number_build(node *n)
{
    widget *w = &n->w;
    w->fill_el = raw(n->el, "div", "fill", NULL);
    raw(n->el, "div", "mark", NULL);
    w->addon_el = raw(n->el, "span", "part addon", NULL);
    w->value_el = raw(n->el, "span", "part value", NULL);
    w->unit_el = raw(n->el, "span", "part unit", NULL);
    rml_element *dec = raw(n->el, "span", "step dec", NULL);
    raw_icon(dec, "", KIT_ICON_LEFT);
    rml_element *inc = raw(n->el, "span", "step inc", NULL);
    raw_icon(inc, "", KIT_ICON_RIGHT);
}

/* A part shows as a block, hidden when its text is empty. */
static void show_part(rml_element *el, const char *text)
{
    rml_element_set_property(el, "display", text[0] ? "block" : "none");
}

bool kit_number(kit *k, const char *key, double *value, const kit_number_desc *d)
{
    node *n = emit(k, key, KIND_NUMBER, "div", "field number");
    widget *w = &n->w;
    if (!w->value_el) {
        number_build(n);
        w->value = *value;
    }
    w->desc = *d;
    bool slider = number_slider(w);
    bool addon = str_set(&w->addon, d->addon);
    bool addon_cls = str_set(&w->addon_cls, d->addon_cls);
    if (addon || addon_cls) {
        set_inner_text(w->addon_el, w->addon);
        const char *cls = frame_format(k, "part addon %s", w->addon_cls);
        rml_element_set_class_names(w->addon_el, cls);
        show_part(w->addon_el, w->addon);
    }
    if (str_set(&w->unit, d->unit)) {
        set_inner_text(w->unit_el, w->unit);
        show_part(w->unit_el, w->unit);
    }
    rml_element_set_property(w->fill_el, "display", slider ? "block" : "none");
    set_class(n, slider ? "field number slider" : "field number");
    set_flag(n, "data-overridden", d->flags & KIT_OVERRIDDEN);
    set_flag(n, "data-readonly", d->flags & KIT_READONLY);
    set_flag(n, "data-disabled", d->flags & KIT_DISABLED);
    set_flag(n, "data-scrubbing", w->scrubbing);
    bool changed = false;
    if (w->pending) {
        w->pending = false;
        *value = w->pending_value;
        changed = true;
    } else if (!w->scrubbing && !w->editing && !w->step_dir) {
        w->value = *value;
    }
    number_display(n);
    return changed;
}
