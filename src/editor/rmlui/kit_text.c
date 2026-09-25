/* Text that changes in place.
 *
 * NumberFlow: each digit is a wheel of cells 0-9 twice over, clipped to one cell.
 * Digits keep their place value, so 99 -> 100 adds a hundreds wheel instead of
 * renumbering. Every wheel spins with the trend (the sign of the change) and
 * wraps, 9 -> 0 forward; new places fade and widen in, lost places spin to 0 and
 * fade out.
 *
 * Torph: text is split into characters, or words when it has spaces, keyed by
 * text and occurrence. Kept segments slide from where they were (FLIP), new ones
 * fade in after a quarter of the duration, removed ones fade out in place over the
 * first quarter, and the box eases between widths. 400 ms, expo-out. */
#include "editor/rmlui/kit_internal.h"
#include "support/math.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MORPH_DURATION 0.4
#define APPEAR_DURATION 0.45 /* NumberFlow opacity timing */
/* NumberFlow text: at most 63 digits, their separators and a sign. */
#define FLOW_TEXT 128

static double curve(double a, double b, double t)
{
    return 3 * a * t * (1 - t) * (1 - t) + 3 * b * t * t * (1 - t) + t * t * t;
}

/* cubic-bezier(x1, y1, x2, y2) at progress x, solved for the curve parameter. */
static double bezier(double x1, double y1, double x2, double y2, double x)
{
    double t = x;
    for (int i = 0; i < 8; i++) {
        double error = curve(x1, x2, t) - x;
        double slope = 3 * x1 * (1 - t) * (1 - t) +
                       6 * (x2 - x1) * t * (1 - t) + 3 * (1 - x2) * t * t;
        if (fabs(error) < 1e-5 || fabs(slope) < 1e-6) {
            break;
        }
        t = math_clamp(t - error / slope, 0.0, 1.0);
    }
    return curve(y1, y2, t);
}

static double expo_out(double x)
{
    return bezier(0.19, 1, 0.22, 1, math_clamp(x, 0.0, 1.0));
}

static double cubic_out(double x)
{
    x = 1 - math_clamp(x, 0.0, 1.0);
    return 1 - x * x * x;
}

static void style(rml_element *el, const char *property, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

static void style(rml_element *el, const char *property, const char *format, ...)
{
    char buffer[96];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    rml_element_set_property(el, property, buffer);
}

static void track(kit *k, node *n)
{
    if (nodes_index(k->text_motion, n) < 0) {
        apush(k->text_motion, n);
    }
}

/* ---------- NumberFlow ---------- */

typedef struct flow_place
{
    int place, digit;
} flow_place;

static void format_flow(char out[static FLOW_TEXT], double value, const kit_number_flow_desc *d)
{
    char digits[64];
    snprintf(digits, sizeof digits, "%.*f", math_max(d->precision, 0), fabs(value));
    size_t whole = strcspn(digits, ".");
    char grouped[FLOW_TEXT];
    size_t at = 0;
    for (size_t i = 0; i < whole; i++) {
        if (d->grouping && i > 0 && (whole - i) % 3 == 0) {
            grouped[at++] = ',';
        }
        grouped[at++] = digits[i];
    }
    snprintf(grouped + at, sizeof grouped - at, "%s", digits + whole);
    bool negative = value < 0 && grouped[strspn(grouped, "0.,")];
    snprintf(out, FLOW_TEXT, "%s%s", negative ? "-" : "", grouped);
}

static rml_element *digit_column(node *n)
{
    rml_element *col = raw(n->el, "span", "nf-digit", NULL);
    rml_element *strip = raw(col, "span", "nf-strip", NULL);
    for (int i = 0; i < 20; i++) {
        char cell[2] = { (char)('0' + i % 10), 0 };
        raw(strip, "span", "nf-cell", cell);
    }
    return col;
}

static void spin_to(flow_digit *g, int digit, int trend, bool instant)
{
    double current = fmod(g->target + g->pos.x, 10.0);
    if (current < 0) {
        current += 10;
    }
    double start = current, end = digit;
    if (trend > 0 && digit < current - 1e-6) {
        end = digit + 10;
    } else if (trend < 0) {
        start = current + 10;
        end = digit > current + 1e-6 ? digit : digit + 10;
    }
    if (instant) {
        g->target = end;
        g->pos = (spring){ 0 };
        return;
    }
    g->pos.x = start - end;
    g->target = end;
}

/* Place values: units 0, tens 1, ...; tenths -1, ... Returns the count. */
static int flow_places(const char *text, flow_place places[static FLOW_TEXT])
{
    const char *dot = strchr(text, '.');
    size_t point = dot ? (size_t)(dot - text) : strlen(text);
    int whole = 0;
    for (size_t i = 0; i < point; i++) {
        whole += text[i] >= '0' && text[i] <= '9';
    }
    int count = 0, seen = 0, fraction = 0;
    for (size_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') {
            continue;
        }
        bool after = dot && i > point;
        int place = after ? -(++fraction) : whole - 1 - seen++;
        places[count++] = (flow_place){ place, text[i] - '0' };
    }
    return count;
}

static flow_digit *find_digit(flow_state *f, int place)
{
    for (int i = 0; i < acount(f->digits); i++) {
        if (f->digits[i].place == place) {
            return &f->digits[i];
        }
    }
    return NULL;
}

static bool has_place(const flow_place *places, int count, int place)
{
    for (int i = 0; i < count; i++) {
        if (places[i].place == place) {
            return true;
        }
    }
    return false;
}

/* Lost places spin to 0 and leave; kept ones spin to their digit; new ones
 * start hidden unless instant. */
static void flow_retarget(node *n, const flow_place *places, int count, int trend, bool instant)
{
    flow_state *f = n->flow;
    for (int i = 0; i < acount(f->digits); i++) {
        flow_digit *g = &f->digits[i];
        if (!has_place(places, count, g->place) && !g->leaving) {
            g->leaving = true;
            spin_to(g, 0, trend, instant);
        }
    }
    for (int i = 0; i < count; i++) {
        flow_digit *g = find_digit(f, places[i].place);
        if (g) {
            g->leaving = false;
            spin_to(g, places[i].digit, trend, instant);
            continue;
        }
        rml_element *col = digit_column(n);
        apush(
            f->digits,
            (flow_digit){
                .col = col,
                .strip = rml_element_first_child(col),
                .place = places[i].place,
                .target = places[i].digit,
                .appear = instant ? 1 : 0,
            }
        );
    }
}

static int by_place_descending(const void *a, const void *b)
{
    int pa = ((const flow_digit *)a)->place, pb = ((const flow_digit *)b)->place;
    return (pa < pb) - (pa > pb);
}

static void flow_symbol(node *n, const char *text)
{
    apush(n->flow->symbols, raw(n->el, "span", "nf-symbol", text));
}

/* Order: prefix, sign, whole digits high to low with separators, point,
 * fraction, suffix. Leaving digits stay beside the place they held. */
static void flow_order(node *n, const char *text)
{
    flow_state *f = n->flow;
    for (int i = 0; i < acount(f->symbols); i++) {
        rml_element_remove_child(n->el, f->symbols[i]);
    }
    aclear(f->symbols);
    qsort(f->digits, (size_t)acount(f->digits), sizeof *f->digits, by_place_descending);
    if (!str_eq(f->prefix, "")) {
        flow_symbol(n, f->prefix);
    }
    if (text[0] == '-') {
        flow_symbol(n, "-");
    }
    bool point_done = !strchr(text, '.');
    for (int i = 0; i < acount(f->digits); i++) {
        flow_digit *g = &f->digits[i];
        if (!point_done && g->place < 0) {
            flow_symbol(n, ".");
            point_done = true;
        }
        rml_element_move_child(n->el, g->col, NULL);
        if (f->grouping && g->place > 0 && g->place % 3 == 0 && !g->leaving) {
            flow_symbol(n, ",");
        }
    }
    if (!point_done) {
        flow_symbol(n, ".");
    }
    if (!str_eq(f->suffix, "")) {
        flow_symbol(n, f->suffix);
    }
}

static void flow_restructure(node *n, const char *text, int trend, bool instant)
{
    flow_place places[FLOW_TEXT];
    int count = flow_places(text, places);
    flow_retarget(n, places, count, trend, instant);
    flow_order(n, text);
}

/* Fades and widens a column in, or out while leaving. */
static void digit_fade(kit *k, flow_state *f, flow_digit *g)
{
    double visible = cubic_out(g->appear);
    if (f->digit_w <= 0 && g->appear >= 1) {
        f->digit_w = element_rect(k, g->col).w;
    }
    if (g->appear >= 1) {
        rml_element_remove_property(g->col, "opacity");
        rml_element_remove_property(g->col, "width");
        return;
    }
    style(g->col, "opacity", "%.3f", visible);
    if (f->digit_w > 0) {
        style(g->col, "width", "%.2fdp", visible * f->digit_w);
    }
}

/* Steps one wheel; true while it still moves. */
static bool digit_frame(kit *k, flow_state *f, flow_digit *g, spring_params spin, double dt)
{
    if (k->reduced_motion) {
        g->pos = (spring){ 0 };
        g->appear = g->leaving ? 0 : 1;
    }
    spring_step(&g->pos, spin, dt);
    if (g->pos.x == 0 && g->pos.v == 0) {
        g->target = fmod(g->target, 10.0);
    }
    double goal = g->leaving ? 0 : 1;
    g->appear = goal > g->appear
                  ? math_min(g->appear + dt / APPEAR_DURATION, goal)
                  : math_max(g->appear - dt / APPEAR_DURATION, goal);
    style(g->strip, "transform", "translateY(%.3f%%)", -(g->target + g->pos.x) * 5);
    digit_fade(k, f, g);
    return g->pos.x != 0 || g->appear != goal;
}

static bool flow_frame(kit *k, node *n, double dt)
{
    flow_state *f = n->flow;
    spring_params spin = spring_for(0.6, 0);
    bool moving = false;
    for (int i = 0; i < acount(f->digits); i++) {
        moving |= digit_frame(k, f, &f->digits[i], spin, dt);
    }
    int kept = 0;
    for (int i = 0; i < acount(f->digits); i++) {
        flow_digit g = f->digits[i];
        if (g.leaving && g.appear <= 0) {
            rml_element_remove_child(n->el, g.col);
            continue;
        }
        f->digits[kept++] = g;
    }
    asetlen(f->digits, kept);
    return moving;
}

/* ---------- Torph ---------- */

static void strings_free(char **strings)
{
    for (int i = 0; i < acount(strings); i++) {
        free(strings[i]);
    }
    afree(strings);
}

static size_t segment_end(const char *text, size_t length, size_t i, bool words)
{
    size_t j = i + 1;
    if (words) {
        bool space = text[i] == ' ';
        while (j < length && (text[j] == ' ') == space) {
            j++;
        }
        return j;
    }
    while (j < length && ((unsigned char)text[j] & 0xc0) == 0x80) {
        j++;
    }
    return j;
}

static char *segment_copy(const char *s, size_t length, bool words)
{
    char *out = NULL;
    if (words && s[0] == ' ') {
        /* RmlUi collapses a box that holds only spaces; keep their width. */
        for (size_t i = 0; i < length; i++) {
            str_cat(&out, "\xc2\xa0");
        }
        return out;
    }
    str_catf(&out, "%.*s", (int)length, s);
    return out;
}

/* Characters, or words and runs of spaces when the text has spaces. */
static char **segment_text(const char *text)
{
    char **out = NULL;
    bool words = strchr(text, ' ') != NULL;
    size_t length = strlen(text);
    for (size_t i = 0; i < length;) {
        size_t j = segment_end(text, length, i, words);
        apush(out, segment_copy(text + i, j - i, words));
        i = j;
    }
    return out;
}

/* Each segment keyed by its text and occurrence. */
static char **segment_ids(char **segments)
{
    char **ids = NULL;
    for (int i = 0; i < acount(segments); i++) {
        int seen = 0;
        for (int j = 0; j < i; j++) {
            seen += strcmp(segments[j], segments[i]) == 0;
        }
        char *id = NULL;
        str_catf(&id, "%s#%d", segments[i], seen);
        apush(ids, id);
    }
    return ids;
}

static void segs_free(morph_seg *segs)
{
    for (int i = 0; i < acount(segs); i++) {
        free(segs[i].id);
    }
    afree(segs);
}

static kit_rect offset_in(const kit *k, rml_element *el, rml_element *box)
{
    kit_rect a = element_rect(k, el), b = element_rect(k, box);
    return (kit_rect){ a.x - b.x, a.y - b.y, a.w, a.h };
}

static morph_seg new_seg(rml_element *box, const char *id, const char *text, morph_phase state)
{
    return (morph_seg){
        .id = xstrdup(id),
        .el = raw(box, "span", "morph-seg", text),
        .state = state,
    };
}

static void morph_reset(morph_state *m, rml_element *box, char **segments, char **ids)
{
    while (rml_element_child_count(box)) {
        rml_element_remove_child(box, rml_element_child(box, 0));
    }
    segs_free(m->segs);
    m->segs = NULL;
    for (int i = 0; i < acount(segments); i++) {
        apush(m->segs, new_seg(box, ids[i], segments[i], MORPH_STAY));
    }
    m->init = true;
    m->start = -1;
}

/* Where everything is now, including any motion in flight; drops segments
 * still leaving. */
static void morph_capture(const kit *k, morph_state *m, rml_element *box)
{
    int kept = 0;
    for (int i = 0; i < acount(m->segs); i++) {
        morph_seg s = m->segs[i];
        if (s.state == MORPH_LEAVE) {
            rml_element_remove_child(box, s.el);
            free(s.id);
            continue;
        }
        kit_rect r = offset_in(k, s.el, box);
        s.from_x = r.x + s.dx;
        s.from_y = r.y + s.dy;
        m->segs[kept++] = s;
    }
    asetlen(m->segs, kept);
}

/* Moves the kept segment with this id out of m->segs, or returns false. */
static bool take_seg(morph_state *m, const char *id, morph_seg *out)
{
    for (int i = 0; i < acount(m->segs); i++) {
        morph_seg *s = &m->segs[i];
        if (s->el && str_eq(s->id, id)) {
            *out = *s;
            s->el = NULL;
            s->id = NULL;
            return true;
        }
    }
    return false;
}

/* Leaving segments hold their place, out of the flow. */
static void morph_leave(morph_state *m, morph_seg **next)
{
    for (int i = 0; i < acount(m->segs); i++) {
        morph_seg *old = &m->segs[i];
        if (!old->el) {
            continue;
        }
        old->state = MORPH_LEAVE;
        rml_element_set_property(old->el, "position", "absolute");
        style(old->el, "left", "%.2fdp", old->from_x);
        style(old->el, "top", "%.2fdp", old->from_y);
        rml_element_remove_property(old->el, "transform");
        apush(*next, *old);
        old->id = NULL;
    }
}

static void morph_set(kit *k, morph_state *m, rml_element *box, const char *text)
{
    char **segments = segment_text(text);
    char **ids = segment_ids(segments);
    str_set(&m->text, text);
    if (!m->init || k->reduced_motion) {
        morph_reset(m, box, segments, ids);
        strings_free(segments);
        strings_free(ids);
        return;
    }
    float from_w = element_rect(k, box).w;
    morph_capture(k, m, box);
    morph_seg *next = NULL;
    for (int i = 0; i < acount(segments); i++) {
        morph_seg s;
        if (!take_seg(m, ids[i], &s)) {
            apush(next, new_seg(box, ids[i], segments[i], MORPH_ENTER));
            continue;
        }
        s.state = MORPH_STAY;
        rml_element_move_child(box, s.el, NULL);
        apush(next, s);
    }
    morph_leave(m, &next);
    segs_free(m->segs);
    m->segs = next;
    m->from_w = from_w;
    m->start = k->in.time;
    m->measure = true;
    m->box = box;
    strings_free(segments);
    strings_free(ids);
}

/* The layout pass ran: invert each kept segment to where it was. */
static void morph_measure(const kit *k, morph_state *m)
{
    m->measure = false;
    rml_element_remove_property(m->box, "width");
    m->to_w = element_rect(k, m->box).w;
    for (int i = 0; i < acount(m->segs); i++) {
        morph_seg *s = &m->segs[i];
        if (s->state != MORPH_STAY) {
            continue;
        }
        kit_rect r = offset_in(k, s->el, m->box);
        s->dx = s->from_x - r.x;
        s->dy = s->from_y - r.y;
    }
}

static void seg_style(const morph_seg *s, double p, double e)
{
    switch (s->state) {
        case MORPH_STAY:
            style(s->el, "transform", "translate(%.2fdp, %.2fdp)", s->dx * (1 - e), s->dy * (1 - e));
            break;
        case MORPH_ENTER:
            style(s->el, "opacity", "%.3f", math_clamp((p - 0.25) / 0.5, 0.0, 1.0));
            style(s->el, "transform", "scale(%.4f)", 0.95 + 0.05 * e);
            break;
        case MORPH_LEAVE:
            style(s->el, "opacity", "%.3f", 1 - math_clamp(p / 0.25, 0.0, 1.0));
            style(s->el, "transform", "scale(%.4f)", 1 - 0.05 * e);
            break;
    }
}

static void morph_settle(morph_state *m)
{
    rml_element_remove_property(m->box, "width");
    int kept = 0;
    for (int i = 0; i < acount(m->segs); i++) {
        morph_seg s = m->segs[i];
        if (s.state == MORPH_LEAVE) {
            rml_element_remove_child(m->box, s.el);
            free(s.id);
            continue;
        }
        rml_element_remove_property(s.el, "transform");
        rml_element_remove_property(s.el, "opacity");
        s.dx = s.dy = 0;
        s.state = MORPH_STAY;
        m->segs[kept++] = s;
    }
    asetlen(m->segs, kept);
    m->start = -1;
}

static bool morph_frame(const kit *k, morph_state *m)
{
    if (m->start < 0) {
        return false;
    }
    if (m->measure) {
        morph_measure(k, m);
    }
    double p = math_clamp((k->in.time - m->start) / MORPH_DURATION, 0.0, 1.0);
    double e = expo_out(p);
    style(m->box, "width", "%.2fdp", m->from_w + (m->to_w - m->from_w) * e);
    for (int i = 0; i < acount(m->segs); i++) {
        seg_style(&m->segs[i], p, e);
    }
    if (p < 1) {
        return true;
    }
    morph_settle(m);
    return false;
}

void morph_label(kit *k, node *n, rml_element *box, const char *text)
{
    if (!n->morph) {
        n->morph = xcalloc(1, sizeof *n->morph);
        n->morph->start = -1;
    }
    morph_state *m = n->morph;
    if (m->init && str_eq(m->text, text)) {
        return;
    }
    morph_set(k, m, box, text ? text : "");
    if (m->start < 0) {
        return;
    }
    track(k, n);
    k->layout_pass = true;
}

void text_frame(kit *k, double dt)
{
    int kept = 0;
    for (int i = 0; i < acount(k->text_motion); i++) {
        node *n = k->text_motion[i];
        bool moving = false;
        if (n->flow) {
            moving |= flow_frame(k, n, dt);
        }
        if (n->morph) {
            moving |= morph_frame(k, n->morph);
        }
        if (moving) {
            k->text_motion[kept++] = n;
        }
    }
    asetlen(k->text_motion, kept);
}

/* ---------- API ---------- */

void kit_number_flow(kit *k, const char *key, const char *cls, double value, const kit_number_flow_desc *d)
{
    const kit_class list[] = { { cls, cls != NULL } };
    node *n = emit(k, key, KIND_BOX, "span", kit_classes(k, "number-flow", list, 1));
    if (!n->flow) {
        n->flow = xcalloc(1, sizeof *n->flow);
    }
    flow_state *f = n->flow;
    char text[FLOW_TEXT];
    format_flow(text, value, d);
    str_set(&f->prefix, d->prefix);
    str_set(&f->suffix, d->suffix);
    f->grouping = d->grouping;
    if (f->init && str_eq(f->text, text)) {
        return;
    }
    int trend = !f->init ? 0 : value > f->value ? 1 : value < f->value ? -1 : 0;
    flow_restructure(n, text, trend, !f->init || k->reduced_motion);
    set_attr(n, "data-value", frame_format(k, "%s%s%s", f->prefix, text, f->suffix));
    f->init = true;
    f->value = value;
    str_set(&f->text, text);
    track(k, n);
}

void kit_morph(kit *k, const char *key, const char *cls, const char *text)
{
    const kit_class list[] = { { cls, cls != NULL } };
    node *n = emit(k, key, KIND_BOX, "span", kit_classes(k, "morph", list, 1));
    morph_label(k, n, n->el, text);
    set_attr(n, "data-text", text ? text : "");
}
