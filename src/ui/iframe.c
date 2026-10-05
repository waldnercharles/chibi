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
    rml_element_ref *document, *element;
    rml_context *context;
    bool fit;
    rml_vec2 at, size; /* the box the document took, in pixels */
    float height;      /* the height a fitting element took */
} iframe;

static iframe *iframes;

static bool profiling;
static iframe_profile_phase phase;
static iframe_profile_sample samples[IFRAME_PROFILE_PHASES];
static iframe_profile_sample before;
static iframe_profile_record current_record, *records;

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
    aclear(records);
    if (!enabled) {
        afree(records);
    }
}

void iframe_profile_begin(iframe_profile_phase next, const char *context)
{
    if (profiling) {
        assert(phase == IFRAME_PROFILE_OUTSIDE);
        phase = next;
        before = samples[phase];
        snprintf(current_record.context, sizeof current_record.context, "%s", context);
        current_record.phase = phase;
        samples[phase].calls++;
        samples[phase].started = profile_clock();
    }
}

void iframe_profile_end()
{
    if (profiling) {
        samples[phase].seconds += profile_clock() - samples[phase].started;
        iframe_profile_sample s = samples[phase];
        current_record.sample = (iframe_profile_sample){
            .calls = s.calls - before.calls,
            .holder_updates = s.holder_updates - before.holder_updates,
            .boxes = s.boxes - before.boxes,
            .heights = s.heights - before.heights,
            .passes = s.passes - before.passes,
            .seconds = s.seconds - before.seconds,
            .holder_seconds = s.holder_seconds - before.holder_seconds,
            .started = s.started,
        };
        apush(records, current_record);
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

int iframe_profile_record_count()
{
    return acount(records);
}

iframe_profile_record iframe_profile_record_get(int index)
{
    assert(index >= 0 && index < acount(records));
    return records[index];
}

void iframe_attach(rml_element *document, rml_element *element, bool fit)
{
    assert(rml_element_owner_document(document) == document);
    assert(rml_element_context(document) == rml_element_context(element));
    apush(
        iframes,
        ((iframe){ .document = rml_element_ref_create(document),
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
static bool take_height(iframe *f, rml_element *document, rml_element *element)
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
    rml_document_update(rml_element_owner_document(element));
    return true;
}

/* Places context's documents once; true when any moved. */
static bool settle_pass(rml_context *context)
{
    if (profiling) {
        samples[phase].passes++;
    }
    bool moved = false;
    for (int i = 0; i < acount(iframes);) {
        iframe *f = &iframes[i];
        rml_element *document = rml_element_ref_get(f->document),
                    *element = rml_element_ref_get(f->element);
        if (!document || !element) {
            drop(i);
            continue;
        }
        if (f->context == context) {
            moved |= take_box(f, document, element);
            moved |= f->fit && take_height(f, document, element);
        }
        i++;
    }
    return moved;
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
