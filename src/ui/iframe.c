/* Documents shown in another document's element (see iframe.h). The
 * element's document lays out first in an update; afterwards each shown
 * document takes the box its element got, as left, top, width and height
 * in pixels, and lays out again only when that box changed. A fitting
 * element takes its document's height, which moves what follows it, so
 * settling repeats while anything moves. */
#include "ui/iframe.h"

#include "support/array.h"
#include "ui/iframe_profile.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The bars fit in the first pass and the areas they move follow in the
   second; a third finds nothing left to move. A layout whose heights keep
   changing stops here rather than loop. */
#define SETTLE_PASSES 4

typedef struct iframe
{
    int id;
    rml_element_ref *document, *element;
    rml_context *context;
    bool fit;
    rml_vec2 at, size; /* the box the document took, in pixels */
    float height;      /* the height a fitting element took */
} iframe;

static iframe *iframes;
static int last_id;

static bool profiling;
static iframe_profile_phase phase;
static iframe_profile_sample samples[IFRAME_PROFILE_PHASES];

static double profile_clock()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

void iframe_profile_enable(bool enabled)
{
    profiling = enabled;
    phase = IFRAME_PROFILE_OUTSIDE;
    memset(samples, 0, sizeof samples);
}

void iframe_profile_begin(iframe_profile_phase next)
{
    if (profiling) {
        assert(phase == IFRAME_PROFILE_OUTSIDE);
        phase = next;
        samples[phase].calls++;
        samples[phase].started = profile_clock();
    }
}

void iframe_profile_end()
{
    if (profiling) {
        samples[phase].seconds += profile_clock() - samples[phase].started;
        phase = IFRAME_PROFILE_OUTSIDE;
    }
}

iframe_profile_phase iframe_profile_current()
{
    return phase;
}

iframe_profile_sample iframe_profile_get(iframe_profile_phase which)
{
    return samples[which];
}

void iframe_profile_context_add(const double seconds[RML_UPDATE_PARTS])
{
    assert(profiling && phase == IFRAME_PROFILE_CONTEXT);
    samples[phase].context_calls++;
    for (int i = 0; i < RML_UPDATE_PARTS; i++) {
        samples[phase].context_seconds[i] += seconds[i];
    }
}

void iframe_attach(rml_element *document, rml_element *element, bool fit)
{
    assert(rml_element_owner_document(document) == document);
    assert(rml_element_context(document) == rml_element_context(element));
    assert(last_id < INT_MAX);
    apush(
        iframes,
        ((iframe){ .id = ++last_id,
                   .document = rml_element_ref_create(document),
                   .element = rml_element_ref_create(element),
                   .context = rml_element_context(document),
                   .fit = fit,
                   .at = { -1, -1 },
                   .size = { -1, -1 },
                   .height = -1 })
    );
}

static void drop(int i)
{
    rml_element_ref_destroy(iframes[i].document);
    rml_element_ref_destroy(iframes[i].element);
    adel(iframes, i);
    if (!acount(iframes)) {
        afree(iframes);
    }
}

/* Callbacks can detach or append entries, invalidating every array pointer. */
static iframe *resolve(int id, rml_element **document, rml_element **element)
{
    for (int i = 0; i < acount(iframes); i++) {
        iframe *f = &iframes[i];
        if (f->id != id) {
            continue;
        }
        *document = rml_element_ref_get(f->document);
        *element = rml_element_ref_get(f->element);
        if (!*document || !*element) {
            drop(i);
            return NULL;
        }
        return f;
    }
    return NULL;
}

void iframe_detach(rml_element *document)
{
    for (int i = 0; i < acount(iframes); i++) {
        if (rml_element_ref_get(iframes[i].document) == document) {
            drop(i);
            return;
        }
    }
    assert(!"the document is shown in no element");
}

static void set_pixels(rml_element *element, const char *property, float value)
{
    char text[32];
    snprintf(text, sizeof text, "%.9gpx", value);
    rml_element_set_property(element, property, text);
}

/* The document takes the element's box; true when it moved. */
static bool take_box(iframe *f, rml_element *document, rml_element *element)
{
    rml_vec2 at = rml_element_absolute_offset(element, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(element, RML_BOX_BORDER);
    if (at.x == f->at.x && at.y == f->at.y && size.x == f->size.x &&
        (f->fit || size.y == f->size.y)) {
        return false;
    }
    f->at = at;
    f->size = size;
    if (profiling) {
        samples[phase].boxes++;
    }
    set_pixels(document, "left", at.x);
    set_pixels(document, "top", at.y);
    set_pixels(document, "width", size.x);
    if (!f->fit) {
        set_pixels(document, "height", size.y);
    }
    rml_document_update(document);
    return true;
}

/* How far down what the document holds reaches. A document whose height is
   auto fills its context, so it never measures its content itself. */
static float content_height(rml_element *document)
{
    float top = rml_element_absolute_offset(document, RML_BOX_BORDER).y, bottom = top;
    for (int i = 0; i < rml_element_child_count(document); i++) {
        rml_element *child = rml_element_child(document, i);
        float y = rml_element_absolute_offset(child, RML_BOX_MARGIN).y;
        bottom = fmaxf(bottom, y + rml_element_box_size(child, RML_BOX_MARGIN).y);
    }
    return bottom - top;
}

/* The document and its element take the height of what the document
   holds; true when it changed. */
static bool take_height(int id, iframe *f, rml_element *document, rml_element *element)
{
    float height = content_height(document);
    if (height == f->height) {
        return false;
    }
    f->height = height;
    if (profiling) {
        samples[phase].heights++;
    }
    set_pixels(document, "height", height);
    set_pixels(element, "height", height);
    rml_document_update(document);
    if (resolve(id, &document, &element)) {
        rml_document_update(rml_element_owner_document(element));
    }
    return true;
}

/* Places context's documents once; true when any moved. */
static bool settle_pass(rml_context *context)
{
    if (profiling) {
        samples[phase].passes++;
    }
    bool moved = false;
    int upper = last_id, count = acount(iframes), after = 0;
    for (;;) {
        int id = 0;
        for (int i = 0; i < acount(iframes); i++) {
            int next = iframes[i].id;
            if (next > after && next <= upper && (!id || next < id)) {
                id = next;
            }
        }
        if (!id) {
            break;
        }
        after = id;
        rml_element *document, *element;
        iframe *f = resolve(id, &document, &element);
        if (!f) {
            moved = true;
            continue;
        }
        if (f->context != context) {
            continue;
        }
        bool box = take_box(f, document, element);
        moved |= box;
        f = resolve(id, &document, &element);
        if (!f) {
            moved = true;
            continue;
        }
        /* A later Context callback can change fitted content without
         * moving its slot. Resolve that content before measuring it. */
        if (f->fit && !box) {
            rml_document_refresh_layout(document);
            f = resolve(id, &document, &element);
            if (!f) {
                moved = true;
                continue;
            }
        }
        moved |= f->fit && take_height(id, f, document, element);
    }
    return moved || count != acount(iframes) || upper != last_id;
}

/* Lays out the documents that hold context's elements, each once. */
static void lay_out_holders(rml_context *context)
{
    rml_element **holders = NULL;
    for (int i = 0; i < acount(iframes); i++) {
        rml_element *element = rml_element_ref_get(iframes[i].element);
        rml_element *holder = element && iframes[i].context == context
                                ? rml_element_owner_document(element)
                                : NULL;
        bool seen = !holder;
        for (int h = 0; h < acount(holders); h++) {
            seen |= holders[h] == holder;
        }
        if (!seen) {
            apush(holders, holder);
            double start = profiling ? profile_clock() : 0;
            rml_document_update(holder);
            if (profiling) {
                samples[phase].holder_updates++;
                samples[phase].holder_seconds += profile_clock() - start;
            }
        }
    }
    afree(holders);
}

void iframes_settle(rml_context *context)
{
    lay_out_holders(context);
    for (int pass = 0; pass < SETTLE_PASSES && settle_pass(context); pass++) {
    }
}
