/* The native elements authored UI uses for motion (src/ui/motion.h), in a
 * bare RmlUi context as games have: <number-flow> and <morph-text> bound to
 * a data model, and presence for data-if, with Reduce Motion off and on. */
#include "test/suite_internal.h"
#include "ui/motion.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_SECONDS (1.0 / 60.0)

static const char DOCUMENT
    [] = "<rml><head><style>"
         "body { font-family: Inter; font-size: 14dp; line-height: 16dp; }"
         "#report, #plain { display: block; }"
         "#report { transition: opacity 0.2s linear-in-out; }"
         "#report[data-starting-style], #report[data-ending-style] { opacity: "
         "0; }"

         "</style></head><body data-model=\"motion\">"
         "<div><number-flow id=\"count\" data-attr-value=\"count\"/></div>"
         "<div><number-flow id=\"price\" value=\"-1234.5\" precision=\"1\" "
         "grouping=\"\" "
         "prefix=\"$\" suffix=\"%\"/></div>"
         "<div><morph-text id=\"label\">{{ label }}</morph-text></div>"
         "<div><morph-text id=\"reference\">Pause</morph-text></div>"
         "<div id=\"report\" data-if=\"shown\" presence=\"\">Saved</div>"
         "<div id=\"plain\" data-if=\"shown\">Plain</div>"
         "</body></rml>";

typedef struct
{
    rml_backend *backend;
    rml_context *context;
    yyjson_mut_doc *json;
    yyjson_mut_val *count, *label, *shown;
    rml_data_model *model;
    rml_element *doc;
} motion_fixture;

static void frames(motion_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        rml_backend_update(f->backend, FRAME_SECONDS);
    }
}

static rml_element *by_id(motion_fixture *f, const char *id)
{
    rml_element *el = rml_element_by_id(f->doc, id);
    assert(el);
    return el;
}

static bool label_is(rml_element *el, const char *text)
{
    char label[64] = "";
    rml_element_attribute(el, "aria-label", label, sizeof label);
    return !strcmp(label, text);
}

static float property(rml_element *el, const char *name)
{
    char value[64] = "";
    rml_element_property(el, name, value, sizeof value);
    return strtof(value, NULL);
}

static bool property_is(rml_element *el, const char *name, const char *value)
{
    char text[64] = "";
    rml_element_property(el, name, text, sizeof text);
    return !strcmp(text, value);
}

/* The wheel's position in cells, from its strip's translateY. */
static float wheel(rml_element *col)
{
    char transform[64] = "";
    rml_element_property(rml_element_query_selector(col, ".nf-strip"), "transform", transform, sizeof transform);
    float percent = 0;
    sscanf(transform, "translateY(%f", &percent);
    return -percent / 5;
}

static int columns(rml_element *flow, rml_element **found, int max)
{
    return rml_element_query_selector_all(flow, ".nf-digit", found, max);
}

static void set_reduced(motion_fixture *f, bool on)
{
    rml_context_activate_theme(f->context, "reduced-motion", on);
}

static void open_fixture(motion_fixture *f)
{
    f->backend = rml_backend_create(640, 480, 1);
    f->context = rml_backend_context(f->backend);
    f->json = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(f->json);
    yyjson_mut_doc_set_root(f->json, root);
    f->count = yyjson_mut_real(f->json, 9);
    f->label = yyjson_mut_str(f->json, "Play");
    f->shown = yyjson_mut_false(f->json);
    yyjson_mut_obj_add_val(f->json, root, "count", f->count);
    yyjson_mut_obj_add_val(f->json, root, "label", f->label);
    yyjson_mut_obj_add_val(f->json, root, "shown", f->shown);
    f->model = rml_data_model_create(f->context, "motion", f->json, NULL, NULL);
    f->doc = rml_context_load_document_from_memory(f->context, DOCUMENT, "[motion]");
    assert(f->doc);
    rml_document_show(f->doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    frames(f, 2);
}

static void close_fixture(motion_fixture *f)
{
    rml_document_close(f->doc);
    frames(f, 1);
    rml_data_model_destroy(f->model);
    yyjson_mut_doc_free(f->json);
    rml_backend_destroy(f->backend);
}

static void set_count(motion_fixture *f, double count)
{
    yyjson_mut_set_real(f->count, count);
    rml_data_model_dirty(f->model, "count");
}

static void set_label(motion_fixture *f, const char *label)
{
    yyjson_mut_set_str(f->label, label);
    rml_data_model_dirty(f->model, "label");
}

static void set_shown(motion_fixture *f, bool shown)
{
    yyjson_mut_set_bool(f->shown, shown);
    rml_data_model_dirty(f->model, "shown");
}

/* 9 -> 10 gains a tens wheel that fades in while the units wheel spins
 * forward through 0; both settle. Under Reduce Motion a change lands at
 * once. */
static void number_flow_checks(motion_fixture *f)
{
    rml_element *count = by_id(f, "count"), *cols[4];
    bool first = label_is(count, "9") && columns(count, cols, 4) == 1 &&
                 fabsf(wheel(cols[0]) - 9) < 0.01f &&
                 label_is(by_id(f, "price"), "$-1,234.5%") &&
                 columns(by_id(f, "price"), cols, 4) == 5;
    test_check(first, "number-flow shows its bound value and formats precision, grouping, prefix and suffix");

    set_count(f, 10);
    frames(f, 3);
    bool two = columns(count, cols, 4) == 2;
    float appearing = two ? property(cols[0], "opacity") : 1;
    float units = two ? wheel(cols[1]) : 0;
    test_check(
        label_is(count, "10") && two && appearing > 0 && appearing < 1 &&
            units > 9 && units < 10,
        "number-flow rolls to a new value: the new place fades in as the units "
        "wheel spins forward"
    );

    frames(f, 90);
    test_check(
        columns(count, cols, 4) == 2 && property_is(cols[0], "opacity", "1") &&
            fabsf(wheel(cols[0]) - 1) < 0.01f && fabsf(wheel(cols[1])) < 0.01f,
        "number-flow settles on the new value"
    );

    set_reduced(f, true);
    set_count(f, 3);
    frames(f, 1);
    bool one = columns(count, cols, 4) == 1;
    test_check(label_is(count, "3") && one && fabsf(wheel(cols[0]) - 3) < 0.01f, "number-flow changes at once under Reduce Motion");
    set_reduced(f, false);
}

static int segments(rml_element *morph)
{
    return rml_element_query_selector_all(morph, ".morph-seg", NULL, 0);
}

/* Play -> Pause keeps P and a: the new text lays out hidden for a frame,
 * then kept letters slide, new ones fade in and the box eases to the new
 * width, ending as a fresh <morph-text>Pause</morph-text> lays out. */
static void morph_text_checks(motion_fixture *f)
{
    rml_element *label = by_id(f, "label"), *reference = by_id(f, "reference");
    test_check(
        label_is(label, "Play") && segments(label) == 4 &&
            property_is(label, "width", "auto"),
        "morph-text shows its bound text as segments"
    );

    set_label(f, "Pause");
    frames(f, 1);
    bool measuring = segments(label) == 9 && !property_is(label, "width", "auto");
    frames(f, 3);
    rml_element *kept = rml_element_query_selector(label, ".morph-seg");
    bool moving = !property_is(label, "width", "auto") && kept &&
                  !property_is(kept, "transform", "none");
    test_check(label_is(label, "Pause") && measuring && moving, "morph-text morphs when its bound text changes: kept segments slide and the box eases");

    frames(f, 40);
    float width = rml_element_box_size(label, RML_BOX_BORDER).x,
          expected = rml_element_box_size(reference, RML_BOX_BORDER).x;
    test_check(
        segments(label) == 5 && property_is(label, "width", "auto") &&
            width > 0 && fabsf(width - expected) < 0.5f,
        "morph-text settles to the new text"
    );

    set_reduced(f, true);
    set_label(f, "Play");
    frames(f, 1);
    test_check(
        label_is(label, "Play") && segments(label) == 4 &&
            property_is(label, "width", "auto"),
        "morph-text changes at once under Reduce Motion"
    );
    set_reduced(f, false);
}

static bool hidden(rml_element *el)
{
    return property_is(el, "display", "none");
}

/* A presence element enters from its starting style and leaves through its
 * ending style before it hides; showing it again while it leaves turns it
 * back. Without presence, or under Reduce Motion, data-if acts at once. */
static void presence_checks(motion_fixture *f)
{
    rml_element *report = by_id(f, "report"), *plain = by_id(f, "plain");
    test_check(hidden(report) && hidden(plain), "a presence element that data-if hides at first starts hidden");

    set_shown(f, true);
    frames(f, 1);
    bool starting = !hidden(report) && property(report, "opacity") == 0;
    frames(f, 4);
    float entering = property(report, "opacity");
    test_check(starting && entering > 0 && entering < 1 && !hidden(plain), "presence shows an element in its starting style, then transitions away from it");

    frames(f, 20);
    set_shown(f, false);
    frames(f, 1);
    bool ending = !hidden(report) &&
                  rml_element_has_attribute(report, "data-ending-style") &&
                  hidden(plain);
    frames(f, 4);
    float leaving = property(report, "opacity");
    frames(f, 20);
    test_check(
        ending && leaving > 0 && leaving < 1 && hidden(report) &&
            !rml_element_has_attribute(report, "data-ending-style"),
        "presence keeps a leaving element shown in its ending style until its "
        "transitions end"
    );

    set_shown(f, true);
    frames(f, 20);
    set_shown(f, false);
    frames(f, 3);
    set_shown(f, true);
    frames(f, 30);
    test_check(
        !hidden(report) && !rml_element_has_attribute(report, "data-ending-style") &&
            property(report, "opacity") == 1,
        "showing a leaving presence element turns it back"
    );

    set_reduced(f, true);
    set_shown(f, false);
    frames(f, 1);
    bool gone = hidden(report);
    set_shown(f, true);
    frames(f, 1);
    test_check(
        gone && !hidden(report) &&
            !rml_element_has_attribute(report, "data-starting-style"),
        "presence hides and shows at once under Reduce Motion"
    );
    set_reduced(f, false);
}

/* Values that change every frame or two, while measuring, morphing,
 * rolling, entering and leaving, end on the last ones. */
static void rapid_checks(motion_fixture *f)
{
    const char *words[] = { "Redo", "Undo", "Saved", "Deleted marker",
                            "",     "Undo", "Play" };
    int count = sizeof words / sizeof *words;
    for (int i = 0; i < 3 * count; i++) {
        set_label(f, words[i % count]);
        set_count(f, i * 37 % 23);
        set_shown(f, i % 2);
        frames(f, 1 + i % 2);
    }
    frames(f, 90);
    rml_element *label = by_id(f, "label"), *reference = by_id(f, "reference");
    set_label(f, "Pause");
    frames(f, 60);
    test_check(
        label_is(label, "Pause") && segments(label) == 5 &&
            fabsf(
                rml_element_box_size(label, RML_BOX_BORDER).x -
                rml_element_box_size(reference, RML_BOX_BORDER).x
            ) < 0.5f &&
            label_is(by_id(f, "count"), "4") && hidden(by_id(f, "report")),
        "changes every frame settle on the last values"
    );
}

/* Opening another project closes every context, which shuts RmlUi down,
 * and opens new ones: their transitions run from the first frame rather
 * than waiting out the time the last session ran. */
static void restart_checks()
{
    motion_fixture f = { 0 };
    open_fixture(&f);
    set_shown(&f, true);
    frames(&f, 5);
    float entering = property(by_id(&f, "report"), "opacity");
    close_fixture(&f);
    test_check(entering > 0 && entering < 1, "transitions run at once after RmlUi starts over");
}

void test_ui_motion()
{
    motion_fixture f = { 0 };
    open_fixture(&f);
    number_flow_checks(&f);
    morph_text_checks(&f);
    presence_checks(&f);
    rapid_checks(&f);
    close_fixture(&f);
    restart_checks();
}
