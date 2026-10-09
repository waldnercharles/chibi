/* Text that changes in place, as native elements (see motion.h). Each steps
 * itself in its update hook, before its style and its children's are
 * computed, so what it sets shows in that frame. Structural styles are
 * inline, so a document needs no stylesheet for them.
 *
 * <number-flow>, NumberFlow: each digit is a wheel of cells 0-9 twice over,
 * clipped to one cell by a hidden sizer digit. Digits keep their place
 * value, so 99 -> 100 adds a hundreds wheel instead of renumbering. Every
 * wheel spins with the trend (the sign of the change) and wraps, 9 -> 0
 * forward; new places fade and widen in, lost places spin to 0 and fade out.
 *
 * <morph-text>, Torph: its text, from its text content, is split into
 * characters, or words when it has spaces, keyed by text and occurrence.
 * When it changes, the new segments lay out hidden for a frame while the old
 * ones stay where they were; then kept segments slide from where they were
 * (FLIP), new ones fade in after a quarter of the duration, removed ones fade
 * out in place over the first quarter, and the box eases between widths.
 * 400 ms, expo-out.
 *
 * Both set aria-label to the text they show. */
#include "support/array.h"
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/motion.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MORPH_DURATION 0.4
#define APPEAR_DURATION 0.45 /* NumberFlow opacity timing */
/* NumberFlow text: at most 63 digits, their separators and a sign. */
#define FLOW_TEXT 128
#define FLOW_AFFIX 64
#define FLOW_PRECISION_MAX 20

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

/* A span with class `cls` holding `text`, as a text node so it needs no
 * escaping. */
static rml_element *span(rml_element *parent, const char *cls, const char *text)
{
    rml_element *el = rml_element_create_child(parent, "span", NULL);
    rml_element_set_class_names(el, cls);
    if (text) {
        rml_element_set_text(rml_element_create_child(el, "#text", NULL), text);
    }
    return el;
}

static bool is_text_node(const rml_element *el)
{
    return strcmp(rml_element_tag_name(el), "#text") == 0;
}

/* The element's attribute, "" when it has none. */
static void attribute(const rml_element *el, const char *name, char *out, size_t size)
{
    out[0] = '\0';
    if (rml_element_has_attribute(el, name)) {
        rml_element_attribute(el, name, out, size);
    }
}

/* Keeps the element updating while it moves. */
static void keep_updating(rml_element *el)
{
    rml_context_request_next_update(rml_element_context(el), 0);
}

/* Changes land at once under Reduce Motion, and while the element is hidden
 * or has just shown, as they would in one just created. `visible` holds
 * whether it showed at the last update; call it every update. */
static bool instant(const rml_element *el, bool *visible)
{
    bool was = *visible;
    *visible = rml_element_is_visible(el, true);
    return !was || !*visible || motion_reduced(el);
}

/* An inline-block, unless the element's display is set already, as the kit
 * sets a hidden label's. */
static void inline_block(rml_element *el)
{
    char display[32];
    rml_element_property(el, "display", display, sizeof display);
    if (!strcmp(display, "inline")) {
        rml_element_set_property(el, "display", "inline-block");
    }
}

/* ---------- NumberFlow ---------- */

typedef struct flow_place
{
    int place, digit;
} flow_place;

/* A wheel's position is in cells of its 20-cell strip, a spring around the
 * target cell. */
typedef struct flow_digit
{
    rml_element *col, *strip;
    int place; /* units 0, tens 1, tenths -1 */
    double target;
    spring pos;
    double appear; /* 0 hidden to 1 shown */
    bool leaving;
} flow_digit;

typedef struct number_flow
{
    bool shown, moving, grouping, visible;
    double value, time;
    char text[FLOW_TEXT]; /* sign, digits, separators and point */
    char prefix[FLOW_AFFIX], suffix[FLOW_AFFIX];
    flow_digit *digits;
    rml_element **symbols;
    float digit_w; /* a shown column's width, px */
} number_flow;

/* What the attributes ask to show. */
typedef struct flow_format
{
    double value;
    char text[FLOW_TEXT];
    char prefix[FLOW_AFFIX], suffix[FLOW_AFFIX];
    bool grouping;
} flow_format;

static void format_digits(char out[static FLOW_TEXT], double value, int precision, bool grouping)
{
    char digits[64];
    snprintf(digits, sizeof digits, "%.*f", precision, fabs(value));
    size_t whole = strcspn(digits, ".");
    char grouped[FLOW_TEXT];
    size_t at = 0;
    for (size_t i = 0; i < whole; i++) {
        if (grouping && i > 0 && (whole - i) % 3 == 0) {
            grouped[at++] = ',';
        }
        grouped[at++] = digits[i];
    }
    snprintf(grouped + at, sizeof grouped - at, "%s", digits + whole);
    bool negative = value < 0 && grouped[strspn(grouped, "0.,")];
    snprintf(out, FLOW_TEXT, "%s%s", negative ? "-" : "", grouped);
}

/* A value that is not a number shows nothing. */
static flow_format read_format(const rml_element *el)
{
    char value[64], precision[16], grouping[16];
    attribute(el, "value", value, sizeof value);
    attribute(el, "precision", precision, sizeof precision);
    attribute(el, "grouping", grouping, sizeof grouping);
    flow_format f = { .grouping = rml_element_has_attribute(el, "grouping") &&
                                  strcmp(grouping, "false") != 0 };
    char *end;
    f.value = strtod(value, &end);
    if (end == value || fabs(f.value) >= 1e60) {
        return (flow_format){ 0 };
    }
    int digits = math_clamp(atoi(precision), 0, FLOW_PRECISION_MAX);
    format_digits(f.text, f.value, digits, f.grouping);
    attribute(el, "prefix", f.prefix, sizeof f.prefix);
    attribute(el, "suffix", f.suffix, sizeof f.suffix);
    return f;
}

static bool same_format(const number_flow *f, const flow_format *next)
{
    return !strcmp(f->text, next->text) && !strcmp(f->prefix, next->prefix) &&
           !strcmp(f->suffix, next->suffix) && f->grouping == next->grouping;
}

static double cubic_out(double x)
{
    x = 1 - math_clamp(x, 0.0, 1.0);
    return 1 - x * x * x;
}

/* A column sized by a hidden 0, with its strip of cells 0-9 twice over. */
static flow_digit digit_column(rml_element *el, int place, int digit, bool shown)
{
    rml_element *col = span(el, "nf-digit", NULL);
    rml_element_set_property(col, "display", "inline-block");
    rml_element_set_property(col, "position", "relative");
    /* RmlUi clips overflow only when in-flow content overflows, so the
       absolute strip needs clip: always. */
    rml_element_set_property(col, "overflow", "hidden");
    rml_element_set_property(col, "clip", "always");
    rml_element_set_property(col, "vertical-align", "top");
    rml_element_set_property(span(col, "nf-sizer", "0"), "visibility", "hidden");
    rml_element *strip = span(col, "nf-strip", "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n0\n1\n2\n3\n4\n5\n6\n7\n8\n9");
    rml_element_set_property(strip, "display", "block");
    rml_element_set_property(strip, "position", "absolute");
    rml_element_set_property(strip, "left", "0px");
    rml_element_set_property(strip, "right", "0px");
    rml_element_set_property(strip, "top", "0px");
    rml_element_set_property(strip, "white-space", "pre");
    rml_element_set_property(strip, "text-align", "center");
    return (flow_digit){
        .col = col,
        .strip = strip,
        .place = place,
        .target = digit,
        .appear = shown ? 1 : 0,
    };
}

static void spin_to(flow_digit *g, int digit, int trend, bool now)
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
    g->target = end;
    g->pos = now ? (spring){ 0 } : (spring){ start - end, 0 };
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

static flow_digit *find_digit(number_flow *f, int place)
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
 * start hidden. An instant change shows and drops places at once. */
static void flow_retarget(
    number_flow *f,
    rml_element *el,
    const flow_place *places,
    int count,
    int trend,
    bool now
)
{
    for (int i = 0; i < acount(f->digits); i++) {
        flow_digit *g = &f->digits[i];
        if (!has_place(places, count, g->place) && !g->leaving) {
            g->leaving = true;
            spin_to(g, 0, trend, now);
        }
    }
    for (int i = 0; i < count; i++) {
        flow_digit *g = find_digit(f, places[i].place);
        if (g) {
            g->leaving = false;
            spin_to(g, places[i].digit, trend, now);
            continue;
        }
        apush(f->digits, digit_column(el, places[i].place, places[i].digit, now));
    }
    for (int i = 0; now && i < acount(f->digits); i++) {
        f->digits[i].appear = f->digits[i].leaving ? 0 : 1;
    }
}

static int by_place_descending(const void *a, const void *b)
{
    int pa = ((const flow_digit *)a)->place, pb = ((const flow_digit *)b)->place;
    return (pa < pb) - (pa > pb);
}

static void flow_symbol(number_flow *f, rml_element *el, const char *text)
{
    rml_element *symbol = span(el, "nf-symbol", text);
    rml_element_set_property(symbol, "display", "inline-block");
    rml_element_set_property(symbol, "vertical-align", "top");
    apush(f->symbols, symbol);
}

/* Order: prefix, sign, whole digits high to low with separators, point,
 * fraction, suffix. Leaving digits stay beside the place they held. */
static void flow_order(number_flow *f, rml_element *el)
{
    for (int i = 0; i < acount(f->symbols); i++) {
        rml_element_remove_child(el, f->symbols[i]);
    }
    aclear(f->symbols);
    if (acount(f->digits) > 1) {
        asort(f->digits, by_place_descending);
    }
    if (f->prefix[0]) {
        flow_symbol(f, el, f->prefix);
    }
    if (f->text[0] == '-') {
        flow_symbol(f, el, "-");
    }
    bool point_done = !strchr(f->text, '.');
    for (int i = 0; i < acount(f->digits); i++) {
        flow_digit *g = &f->digits[i];
        if (!point_done && g->place < 0) {
            flow_symbol(f, el, ".");
            point_done = true;
        }
        rml_element_move_child(el, g->col, NULL);
        if (f->grouping && g->place > 0 && g->place % 3 == 0 && !g->leaving) {
            flow_symbol(f, el, ",");
        }
    }
    if (!point_done) {
        flow_symbol(f, el, ".");
    }
    if (f->suffix[0]) {
        flow_symbol(f, el, f->suffix);
    }
}

/* Shows what the attributes ask, rolling from the last value unless `now`. */
static void flow_show(number_flow *f, rml_element *el, const flow_format *next, bool now)
{
    int trend = !f->shown || next->value == f->value ? 0
              : next->value > f->value               ? 1
                                                     : -1;
    now |= !f->shown;
    snprintf(f->text, sizeof f->text, "%s", next->text);
    snprintf(f->prefix, sizeof f->prefix, "%s", next->prefix);
    snprintf(f->suffix, sizeof f->suffix, "%s", next->suffix);
    f->grouping = next->grouping;
    f->value = next->value;
    flow_place places[FLOW_TEXT];
    int count = flow_places(f->text, places);
    flow_retarget(f, el, places, count, trend, now);
    flow_order(f, el);
    char label[FLOW_TEXT + 2 * FLOW_AFFIX];
    snprintf(label, sizeof label, "%s%s%s", f->prefix, f->text, f->suffix);
    rml_element_set_attribute(el, "aria-label", label);
    f->shown = f->moving = true;
}

/* Fades and widens a column in, or out while leaving. */
static void digit_fade(number_flow *f, flow_digit *g)
{
    if (g->appear >= 1) {
        f->digit_w = rml_element_box_size(g->col, RML_BOX_BORDER).x;
        rml_element_remove_property(g->col, "opacity");
        rml_element_remove_property(g->col, "width");
        return;
    }
    double visible = cubic_out(g->appear);
    style(g->col, "opacity", "%.3f", visible);
    if (f->digit_w > 0) {
        style(g->col, "width", "%.2fpx", visible * f->digit_w);
    }
}

/* Steps one wheel; true while it still moves. */
static bool digit_frame(number_flow *f, flow_digit *g, spring_params spin, double dt, bool settle)
{
    double goal = g->leaving ? 0 : 1;
    if (settle) {
        g->pos = (spring){ 0 };
        g->appear = goal;
    }
    spring_step(&g->pos, spin, dt);
    if (g->pos.x == 0 && g->pos.v == 0) {
        g->target = fmod(g->target, 10.0);
    }
    g->appear = goal > g->appear
                  ? math_min(g->appear + dt / APPEAR_DURATION, goal)
                  : math_max(g->appear - dt / APPEAR_DURATION, goal);
    style(g->strip, "transform", "translateY(%.3f%%)", -(g->target + g->pos.x) * 5);
    digit_fade(f, g);
    return g->pos.x != 0 || g->appear != goal;
}

static bool flow_frame(number_flow *f, rml_element *el, double dt)
{
    spring_params spin = spring_for(0.6, 0);
    bool settle = motion_reduced(el), moving = false;
    for (int i = 0; i < acount(f->digits); i++) {
        moving |= digit_frame(f, &f->digits[i], spin, dt, settle);
    }
    int kept = 0;
    for (int i = 0; i < acount(f->digits); i++) {
        flow_digit g = f->digits[i];
        if (g.leaving && g.appear <= 0) {
            rml_element_remove_child(el, g.col);
            continue;
        }
        f->digits[kept++] = g;
    }
    asetlen(f->digits, kept);
    return moving;
}

static number_flow *flow_new(rml_element *el)
{
    number_flow *f = xcalloc(1, sizeof *f);
    f->time = rml_elapsed_time();
    rml_element_set_user(el, f);
    inline_block(el);
    rml_element_set_property(el, "white-space", "nowrap");
    return f;
}

static void flow_update(rml_element *el, void *user)
{
    number_flow *f = user ? user : flow_new(el);
    double now = rml_elapsed_time();
    double dt = math_clamp(now - f->time, 0.0, 0.1);
    f->time = now;
    bool at_once = instant(el, &f->visible);
    flow_format next = read_format(el);
    if (!f->shown || !same_format(f, &next)) {
        flow_show(f, el, &next, at_once);
    }
    if (!f->moving) {
        return;
    }
    f->moving = flow_frame(f, el, dt);
    if (f->moving) {
        keep_updating(el);
    }
}

static void flow_release(void *user)
{
    number_flow *f = user;
    if (!f) {
        return;
    }
    afree(f->digits);
    afree(f->symbols);
    free(f);
}

/* ---------- Torph ---------- */

typedef enum seg_phase
{
    SEG_STAY,  /* in flow, sliding from where it was */
    SEG_ENTER, /* in flow, fading in */
    SEG_LEAVE, /* where it was, fading out */
    SEG_OLD,   /* where it was, while the new text lays out */
    SEG_NEW,   /* in flow and hidden, to be measured */
} seg_phase;

typedef struct morph_seg
{
    char *id;
    rml_element *el;
    seg_phase phase;
    float x, y;   /* from the box's padding edge, px */
    float dx, dy; /* a kept segment's old place, from its new one */
} morph_seg;

typedef struct morph_text
{
    bool init, measuring, visible;
    char *text;
    morph_seg *segs;
    double start;       /* -1 at rest */
    double eased;       /* the progress applied, 1 at rest */
    float from_w, to_w; /* content widths, px */
} morph_text;

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

static char *text_of(const rml_element *node)
{
    size_t size = rml_element_text(node, NULL, 0) + 1;
    char *text = xmalloc(size);
    rml_element_text(node, text, size);
    return text;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Appends `text` with runs of white space as one space, as HTML shows
 * text; a leading space only after other text. */
static void append_collapsed(char **out, const char *text)
{
    for (const char *c = text; *c; c++) {
        if (!is_space(*c)) {
            apush(*out, *c);
            continue;
        }
        if (acount(*out) && alast(*out) != ' ') {
            apush(*out, ' ');
        }
    }
}

/* The element's text content, hiding its text nodes: the segments show it. */
static char *source_text(rml_element *el)
{
    char *text = NULL;
    for (int i = 0; i < rml_element_child_count(el); i++) {
        rml_element *child = rml_element_child(el, i);
        if (!is_text_node(child)) {
            continue;
        }
        rml_element_set_property(child, "display", "none");
        char *part = text_of(child);
        append_collapsed(&text, part);
        free(part);
    }
    while (acount(text) && alast(text) == ' ') {
        (void)apop(text);
    }
    apush(text, '\0');
    char *out = xstrdup(text);
    afree(text);
    return out;
}

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

/* RmlUi collapses a box that holds only spaces, so spaces keep their width
 * as no-break spaces. */
static char *segment_copy(const char *s, size_t length)
{
    if (s[0] != ' ') {
        char *out = xmalloc(length + 1);
        memcpy(out, s, length);
        out[length] = '\0';
        return out;
    }
    char *out = xmalloc(2 * length + 1);
    for (size_t i = 0; i < length; i++) {
        memcpy(out + 2 * i, "\xc2\xa0", 2);
    }
    out[2 * length] = '\0';
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
        apush(out, segment_copy(text + i, j - i));
        i = j;
    }
    return out;
}

/* Each segment keyed by its text and occurrence. */
static char *segment_id(char **segments, int i)
{
    int seen = 0;
    for (int j = 0; j < i; j++) {
        seen += strcmp(segments[j], segments[i]) == 0;
    }
    size_t size = strlen(segments[i]) + 16;
    char *id = xmalloc(size);
    snprintf(id, size, "%s#%d", segments[i], seen);
    return id;
}

static void seg_remove(rml_element *box, morph_seg *s)
{
    rml_element_remove_child(box, s->el);
    free(s->id);
    s->el = NULL;
    s->id = NULL;
}

/* Drops the segments already removed. */
static void segs_compact(morph_text *m)
{
    int kept = 0;
    for (int i = 0; i < acount(m->segs); i++) {
        if (m->segs[i].el) {
            m->segs[kept++] = m->segs[i];
        }
    }
    asetlen(m->segs, kept);
}

static void segs_clear(morph_text *m, rml_element *box)
{
    for (int i = 0; i < acount(m->segs); i++) {
        seg_remove(box, &m->segs[i]);
    }
    aclear(m->segs);
}

/* Segments for `text` in flow, hidden to be measured or shown at rest. */
static void segs_add(morph_text *m, rml_element *box, const char *text, seg_phase phase)
{
    char **segments = segment_text(text);
    for (int i = 0; i < acount(segments); i++) {
        rml_element *el = span(box, "morph-seg", segments[i]);
        rml_element_set_property(el, "display", "inline-block");
        rml_element_set_property(el, "white-space", "pre");
        if (phase == SEG_NEW) {
            rml_element_set_property(el, "visibility", "hidden");
        }
        apush(m->segs, (morph_seg){ .id = segment_id(segments, i), .el = el, .phase = phase });
    }
    strings_free(segments);
}

static void morph_label(morph_text *m, rml_element *box, const char *text)
{
    free(m->text);
    m->text = xstrdup(text);
    rml_element_set_attribute(box, "aria-label", text);
}

/* Shows `text` at once. */
static void morph_reset(morph_text *m, rml_element *box, const char *text)
{
    segs_clear(m, box);
    segs_add(m, box, text, SEG_STAY);
    rml_element_remove_property(box, "width");
    morph_label(m, box, text);
    m->init = true;
    m->measuring = false;
    m->start = -1;
    m->eased = 1;
}

/* A segment's laid out place, from the box's padding edge. */
static rml_vec2 seg_place(rml_element *box, rml_element *el)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 origin = rml_element_absolute_offset(box, RML_BOX_PADDING);
    return (rml_vec2){ at.x - origin.x, at.y - origin.y };
}

/* The width the segments in `phase` lay out across: the box's content
 * width once it holds them at rest, as it sizes to its text. */
static float segs_extent(const morph_text *m, rml_element *box, seg_phase phase)
{
    float left = FLT_MAX, right = -FLT_MAX;
    for (int i = 0; i < acount(m->segs); i++) {
        if (m->segs[i].phase != phase) {
            continue;
        }
        float x = seg_place(box, m->segs[i].el).x;
        left = math_min(left, x);
        right = math_max(
            right,
            x + rml_element_box_size(m->segs[i].el, RML_BOX_BORDER).x
        );
    }
    return right > left ? right - left : 0;
}

/* Where each segment shows now, motion included, held there out of the
 * flow; segments already leaving go. */
static void morph_capture(morph_text *m, rml_element *box)
{
    for (int i = 0; i < acount(m->segs); i++) {
        if (m->segs[i].phase == SEG_LEAVE) {
            seg_remove(box, &m->segs[i]);
        }
    }
    segs_compact(m);
    for (int i = 0; i < acount(m->segs); i++) {
        morph_seg *s = &m->segs[i];
        rml_vec2 at = seg_place(box, s->el);
        s->x = at.x + s->dx * (float)(1 - m->eased);
        s->y = at.y + s->dy * (float)(1 - m->eased);
        s->phase = SEG_OLD;
        rml_element_set_property(s->el, "position", "absolute");
        style(s->el, "left", "%.2fpx", s->x);
        style(s->el, "top", "%.2fpx", s->y);
        rml_element_remove_property(s->el, "transform");
    }
}

/* The old segments stay where they show while the new text lays out hidden;
 * a change during that frame replaces the new text. */
static void morph_begin(morph_text *m, rml_element *box, const char *text)
{
    if (m->measuring) {
        for (int i = 0; i < acount(m->segs); i++) {
            if (m->segs[i].phase == SEG_NEW) {
                seg_remove(box, &m->segs[i]);
            }
        }
        segs_compact(m);
    } else {
        m->from_w = rml_element_box_size(box, RML_BOX_CONTENT).x;
        morph_capture(m, box);
        style(box, "width", "%.2fpx", m->from_w);
    }
    segs_add(m, box, text, SEG_NEW);
    morph_label(m, box, text);
    m->measuring = true;
    m->start = -1;
}

/* Takes the old segment with this id, or returns NULL. */
static morph_seg *take_old(morph_text *m, const char *id)
{
    for (int i = 0; i < acount(m->segs); i++) {
        morph_seg *s = &m->segs[i];
        if (s->phase == SEG_OLD && s->el && !strcmp(s->id, id)) {
            return s;
        }
    }
    return NULL;
}

/* The new text laid out: each kept segment inverts to where it was and the
 * old one goes; the others enter; old segments left over leave. */
static void morph_measure(morph_text *m, rml_element *box, double now)
{
    float extent = segs_extent(m, box, SEG_NEW);
    for (int i = 0; i < acount(m->segs); i++) {
        morph_seg *s = &m->segs[i];
        if (s->phase != SEG_NEW) {
            continue;
        }
        rml_vec2 at = seg_place(box, s->el);
        morph_seg *old = take_old(m, s->id);
        s->phase = old ? SEG_STAY : SEG_ENTER;
        s->dx = old ? old->x - at.x : 0;
        s->dy = old ? old->y - at.y : 0;
        if (old) {
            seg_remove(box, old);
        }
        rml_element_remove_property(s->el, "visibility");
    }
    for (int i = 0; i < acount(m->segs); i++) {
        if (m->segs[i].phase == SEG_OLD && m->segs[i].el) {
            m->segs[i].phase = SEG_LEAVE;
        }
    }
    segs_compact(m);
    m->to_w = extent;
    m->measuring = false;
    m->start = now;
}

static void seg_style(const morph_seg *s, double p, double e)
{
    switch (s->phase) {
        case SEG_STAY:
            style(s->el, "transform", "translate(%.2fpx, %.2fpx)", s->dx * (1 - e), s->dy * (1 - e));
            break;
        case SEG_ENTER:
            style(s->el, "opacity", "%.3f", math_clamp((p - 0.25) / 0.5, 0.0, 1.0));
            style(s->el, "transform", "scale(%.4f)", 0.95 + 0.05 * e);
            break;
        case SEG_LEAVE:
            style(s->el, "opacity", "%.3f", 1 - math_clamp(p / 0.25, 0.0, 1.0));
            style(s->el, "transform", "scale(%.4f)", 1 - 0.05 * e);
            break;
        case SEG_OLD:
        case SEG_NEW: break;
    }
}

static void morph_settle(morph_text *m, rml_element *box)
{
    rml_element_remove_property(box, "width");
    for (int i = 0; i < acount(m->segs); i++) {
        morph_seg *s = &m->segs[i];
        if (s->phase == SEG_LEAVE) {
            seg_remove(box, s);
            continue;
        }
        rml_element_remove_property(s->el, "transform");
        rml_element_remove_property(s->el, "opacity");
        s->dx = s->dy = 0;
        s->phase = SEG_STAY;
    }
    segs_compact(m);
    m->start = -1;
    m->eased = 1;
}

/* Styles this frame of the morph; false once it has settled. */
static bool morph_animate(morph_text *m, rml_element *box, double now)
{
    double p = math_clamp((now - m->start) / MORPH_DURATION, 0.0, 1.0);
    m->eased = expo_out(p);
    style(box, "width", "%.2fpx", m->from_w + (m->to_w - m->from_w) * m->eased);
    for (int i = 0; i < acount(m->segs); i++) {
        seg_style(&m->segs[i], p, m->eased);
    }
    if (p < 1) {
        return true;
    }
    morph_settle(m, box);
    return false;
}

static morph_text *morph_new(rml_element *el)
{
    morph_text *m = xcalloc(1, sizeof *m);
    m->start = -1;
    m->eased = 1;
    rml_element_set_user(el, m);
    inline_block(el);
    rml_element_set_property(el, "position", "relative");
    rml_element_set_property(el, "white-space", "nowrap");
    /* Segments keep their places as the box's width moves. */
    rml_element_set_property(el, "text-align", "left");
    return m;
}

/* Starts, measures or steps a morph; true while it moves. */
static bool morph_step(morph_text *m, rml_element *el, const char *text, double now)
{
    bool at_once = instant(el, &m->visible);
    if (!m->init || (strcmp(text, m->text) && at_once)) {
        morph_reset(m, el, text);
        return false;
    }
    if (strcmp(text, m->text)) {
        morph_begin(m, el, text);
        return true;
    }
    if (m->measuring) {
        morph_measure(m, el, now);
    }
    return m->start >= 0 && morph_animate(m, el, now);
}

static void morph_update(rml_element *el, void *user)
{
    morph_text *m = user ? user : morph_new(el);
    char *text = source_text(el);
    if (morph_step(m, el, text, rml_elapsed_time())) {
        keep_updating(el);
    }
    free(text);
}

static void morph_release(void *user)
{
    morph_text *m = user;
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

void morph_text_set(rml_element *element, const char *text)
{
    for (int i = 0; i < rml_element_child_count(element); i++) {
        rml_element *child = rml_element_child(element, i);
        if (!is_text_node(child)) {
            continue;
        }
        char *current = text_of(child);
        if (strcmp(current, text)) {
            rml_element_set_text(child, text);
        }
        free(current);
        return;
    }
    rml_element *node = rml_element_create_child(element, "#text", rml_element_first_child(element));
    rml_element_set_text(node, text);
}

/* ---------- Registration ---------- */

static const rml_element_hooks flow_hooks = { .release = flow_release,
                                              .update = flow_update };
static const rml_element_hooks morph_hooks = { .release = morph_release,
                                               .update = morph_update };

void text_motion_register()
{
    static rml_instancer *flow, *morph;
    if (!flow) {
        flow = rml_instancer_create(&flow_hooks);
        morph = rml_instancer_create(&morph_hooks);
    }
    rml_register_element_instancer("number-flow", flow);
    rml_register_element_instancer("morph-text", morph);
}
