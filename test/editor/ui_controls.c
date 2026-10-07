/* The native controls (src/ui/controls/controls.h) in a bare RmlUi context,
 * as games have them, bound to a data model whose event bindings count what
 * they send: a scrub's inputs and its one change, Escape cancelling typing
 * and a scrub, typing committing once, mixed and readonly fields, values
 * that repaint without laying out the document, a splitter's drag, a
 * color field's picker and a line chart drawing published series; and
 * projects/studio's widgets over them. */
#include "assets/ui_document.h"
#include "support/array.h"
#include "test/suite_internal.h"
#include "ui/controls/controls.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_SECONDS (1.0 / 60.0)

static const char DOCUMENT[] =
    "<rml><head><style>"
    "body { font-family: Inter; font-size: 13dp; line-height: 18dp; }"
    "number-field, color-field { position: relative; display: flex; "
    "align-items: center; width: 160dp; height: 26dp; margin: 4dp; }"
    "number-field .part { display: block; height: 24dp; line-height: 24dp; }"
    "number-field .value { flex: 1 1 0dp; text-align: right; white-space: "
    "nowrap; overflow: hidden; font-family: InterNumeric; }"
    "number-field .step { position: absolute; top: 0dp; bottom: 0dp; "
    "width: 14dp; }"
    "number-field .step.dec { left: 0dp; } number-field .step.inc { right: "
    "0dp; }"
    "number-field .fill, number-field .mark { position: absolute; left: "
    "0dp; top: 0dp; bottom: 0dp; }"
    "number-field input.value { height: 24dp; }"
    "splitter { display: block; width: 6dp; height: 40dp; margin: 4dp; }"
    "live-text { font-family: InterNumeric; }"
    ".popup { width: 232dp; padding: 8dp; z-index: 20; }"
    ".popup .sv { position: relative; height: 150dp; drag: drag; }"
    ".popup .hue { display: flex; height: 12dp; drag: drag; }"
    ".popup .hue-seg { flex: 1 1 0dp; }"
    ".popup .puck { position: absolute; width: 4dp; height: 4dp; }"
    ".popup .field { display: flex; height: 26dp; }"
    ".popup input.hex { width: 120dp; height: 24dp; }"
    "line-chart { display: block; position: relative; width: 200dp; "
    "height: 80dp; margin: 4dp; color: #ff0000; border-color: #00ff00; }"
    ".chart-tick, .chart-badge, .chart-hover { position: absolute; "
    "font-family: InterNumeric; }"
    ".chart-tick { left: 4dp; } .chart-badge { right: 4dp; } "
    ".chart-hover { top: 4dp; }"
    "</style></head><body data-model=\"controls\">"
    "<number-field id=\"number\" step=\"1\" precision=\"2\" "
    "data-attr-value=\"value\" data-attrif-mixed=\"mixed\" "
    "data-attrif-readonly=\"readonly\" "
    "data-event-input=\"inputs = inputs + 1; last = ev.value\" "
    "data-event-change=\"changes = changes + 1; committed = ev.value; "
    "canceled = ev.canceled\"></number-field>"
    "<div><live-text id=\"readout\" "
    "data-attr-value=\"readout\"></live-text></div>"
    "<splitter id=\"split\" axis=\"x\" min=\"50\" max=\"80\" "
    "data-attr-value=\"width\" "
    "data-event-input=\"split_inputs = split_inputs + 1; width = ev.value\" "
    "data-event-change=\"split_changes = split_changes + 1\"></splitter>"
    "<color-field id=\"color\" data-attr-value=\"color\" "
    "data-event-input=\"color_inputs = color_inputs + 1\" "
    "data-event-change=\"color_changes = color_changes + 1; color = "
    "ev.value\"></color-field>"
    "<line-chart id=\"chart\" series=\"test:frame test:phase\" "
    "unit=\"ms\" precision=\"1\" min=\"0\" max=\"40\"></line-chart>"
    "<layout-probe/>"
    "</body></rml>";

typedef struct
{
    rml_backend *backend;
    rml_context *context;
    yyjson_mut_doc *json;
    yyjson_mut_val *root;
    rml_data_model *model;
    rml_element *doc;
} controls_fixture;

static int layouts;

static void probe_layout(rml_element *, void *)
{
    layouts++;
}

static void frames(controls_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        rml_backend_update(f->backend, FRAME_SECONDS);
    }
}

static rml_element *by_id(controls_fixture *f, const char *id)
{
    rml_element *el = rml_element_by_id(f->doc, id);
    assert(el);
    return el;
}

static yyjson_mut_val *var(controls_fixture *f, const char *name)
{
    yyjson_mut_val *v = yyjson_mut_obj_get(f->root, name);
    assert(v);
    return v;
}

static double number(controls_fixture *f, const char *name)
{
    yyjson_mut_val *v = var(f, name);
    return yyjson_mut_is_str(v) ? strtod(yyjson_mut_get_str(v), NULL)
                                : yyjson_mut_get_num(v);
}

static const char *text(controls_fixture *f, const char *name)
{
    yyjson_mut_val *v = var(f, name);
    return yyjson_mut_is_str(v) ? yyjson_mut_get_str(v) : "";
}

static void set_number(controls_fixture *f, const char *name, double value)
{
    yyjson_mut_set_real(var(f, name), value);
    rml_data_model_dirty(f->model, name);
}

static void set_bool(controls_fixture *f, const char *name, bool value)
{
    yyjson_mut_set_bool(var(f, name), value);
    rml_data_model_dirty(f->model, name);
}

static void set_text(controls_fixture *f, const char *name, const char *value)
{
    yyjson_mut_set_str(var(f, name), value); /* a literal: it outlives the model */
    rml_data_model_dirty(f->model, name);
}

/* The text the element's `.part.value` (or the element) shows. */
static void shown(rml_element *el, const char *selector, char *out, size_t size)
{
    rml_element *label = selector ? rml_element_query_selector(el, selector) : el;
    rml_element *node = label ? rml_element_first_child(label) : NULL;
    out[0] = '\0';
    if (node && !strcmp(rml_element_tag_name(node), "#text")) {
        rml_element_text(node, out, size);
    }
}

static rml_vec2 center(rml_element *el)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    return (rml_vec2){ at.x + size.x / 2, at.y + size.y / 2 };
}

static void move(controls_fixture *f, rml_vec2 at)
{
    rml_context_process_mouse_move(f->context, (int)at.x, (int)at.y, 0);
}

static void click(controls_fixture *f, rml_element *el)
{
    rml_vec2 at = center(el);
    move(f, at);
    rml_context_process_mouse_button_down(f->context, 0, 0);
    rml_context_process_mouse_button_up(f->context, 0, 0);
    frames(f, 1);
}

static void key(controls_fixture *f, rml_key k)
{
    rml_context_process_key_down(f->context, k, 0);
    rml_context_process_key_up(f->context, k, 0);
    frames(f, 1);
}

static void type(controls_fixture *f, const char *typed)
{
    rml_context_process_text_input(f->context, typed, strlen(typed));
    frames(f, 1);
}

/* Presses `el`'s middle and drags it `steps` times by `dx` px. */
static void drag(controls_fixture *f, rml_element *el, int steps, float dx)
{
    rml_vec2 at = center(el);
    move(f, at);
    rml_context_process_mouse_button_down(f->context, 0, 0);
    frames(f, 1);
    for (int i = 1; i <= steps; i++) {
        move(f, (rml_vec2){ at.x + i * dx, at.y });
        frames(f, 1);
    }
}

static void release(controls_fixture *f)
{
    rml_context_process_mouse_button_up(f->context, 0, 0);
    frames(f, 1);
}

static void open_fixture(controls_fixture *f)
{
    static rml_instancer *probe;
    static const rml_element_hooks PROBE = { .layout = probe_layout };
    if (!probe) {
        probe = rml_instancer_create(&PROBE);
    }
    f->backend = rml_backend_create(640, 480, 1);
    rml_register_element_instancer("layout-probe", probe);
    f->context = rml_backend_context(f->backend);
    f->json = yyjson_mut_doc_new(NULL);
    f->root = yyjson_mut_obj(f->json);
    yyjson_mut_doc_set_root(f->json, f->root);
    static const char *const NUMBERS[] = {
        "value",        "inputs",        "changes",      "width",
        "split_inputs", "split_changes", "color_inputs", "color_changes",
    };
    for (size_t i = 0; i < sizeof NUMBERS / sizeof *NUMBERS; i++) {
        yyjson_mut_obj_add_real(f->json, f->root, NUMBERS[i], 0);
    }
    static const char *const TEXTS[] = { "last", "committed", "readout" };
    for (size_t i = 0; i < sizeof TEXTS / sizeof *TEXTS; i++) {
        yyjson_mut_obj_add_strcpy(f->json, f->root, TEXTS[i], "");
    }
    yyjson_mut_obj_add_strcpy(f->json, f->root, "color", "#336699ff");
    yyjson_mut_obj_add_bool(f->json, f->root, "mixed", false);
    yyjson_mut_obj_add_bool(f->json, f->root, "readonly", false);
    yyjson_mut_obj_add_bool(f->json, f->root, "canceled", false);
    yyjson_mut_set_real(var(f, "value"), 10);
    yyjson_mut_set_real(var(f, "width"), 60);
    f->model = rml_data_model_create(f->context, "controls", f->json, NULL, NULL);
    f->doc = rml_context_load_document_from_memory(f->context, DOCUMENT, "[controls]");
    assert(f->doc);
    rml_document_show(f->doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    frames(f, 3);
}

static void close_fixture(controls_fixture *f)
{
    rml_document_close(f->doc);
    frames(f, 1);
    rml_data_model_destroy(f->model);
    yyjson_mut_doc_free(f->json);
    rml_backend_destroy(f->backend);
}

/* A scrub sends input as it moves and change once when released; the field
 * then shows the model's value until the model takes the change. */
static void scrub_checks(controls_fixture *f)
{
    rml_element *field = by_id(f, "number");
    char text[32];
    shown(field, ".value", text, sizeof text);
    test_check(!strcmp(text, "10"), "a number field shows its bound value");
    drag(f, field, 5, 8);
    double inputs = number(f, "inputs");
    test_check(inputs >= 4 && number(f, "changes") == 0 && number(f, "last") == 30, "scrubbing sends input with each move past the threshold and no change yet");
    release(f);
    test_check(
        number(f, "changes") == 1 && number(f, "committed") == 30 &&
            number(f, "inputs") == inputs && !yyjson_mut_get_bool(var(f, "canceled")),
        "releasing a scrub sends one change, uncanceled, with its value: one "
        "Undo step"
    );
    frames(f, 1);
    shown(field, ".value", text, sizeof text);
    test_check(!strcmp(text, "10"), "a model that has not taken the change still shows its own value");
    set_number(f, "value", 30);
    frames(f, 1);
    shown(field, ".value", text, sizeof text);
    test_check(!strcmp(text, "30"), "the model taking the change shows it");
}

/* Typing commits once on Enter; Escape leaves the value alone. */
static void typing_checks(controls_fixture *f)
{
    rml_element *field = by_id(f, "number");
    double inputs = number(f, "inputs"), changes = number(f, "changes");
    click(f, field);
    rml_element *input = rml_element_query_selector(field, "input");
    test_check(input && rml_context_focus_element(f->context) == input, "a click without moving types into the field");
    type(f, "99");
    key(f, RML_KEY_ESCAPE);
    char text[32];
    shown(field, ".value", text, sizeof text);
    test_check(
        number(f, "inputs") == inputs && number(f, "changes") == changes &&
            !rml_element_query_selector(field, "input") && !strcmp(text, "30"),
        "Escape cancels typing: no input, no change, the value shown again"
    );
    click(f, field);
    type(f, "*2");
    key(f, RML_KEY_RETURN);
    test_check(
        number(f, "inputs") == inputs + 1 &&
            number(f, "changes") == changes + 1 && number(f, "committed") == 60,
        "Enter commits a typed expression with one input and one change"
    );
    set_number(f, "value", 60);
    frames(f, 1);
}

/* Escape during a scrub puts the starting value back and cancels it. */
static void cancel_checks(controls_fixture *f)
{
    rml_element *field = by_id(f, "number");
    double changes = number(f, "changes");
    drag(f, field, 3, 8);
    key(f, RML_KEY_ESCAPE);
    test_check(
        number(f, "changes") == changes + 1 &&
            yyjson_mut_get_bool(var(f, "canceled")) &&
            number(f, "committed") == 60 && number(f, "last") == 60,
        "Escape cancels a scrub: the start value as input, then change with "
        "ev.canceled"
    );
    release(f);
    test_check(number(f, "changes") == changes + 1, "the release after a canceled scrub sends nothing more");
}

/* A mixed field shows a dash until a step sets one value. */
static void mixed_checks(controls_fixture *f)
{
    rml_element *field = by_id(f, "number");
    set_bool(f, "mixed", true);
    frames(f, 1);
    char text[32];
    shown(field, ".value", text, sizeof text);
    test_check(
        !strcmp(text, "\xe2\x80\x94") && rml_element_has_attribute(field, "data-mixed"),
        "a mixed field shows a dash"
    );
    rml_element_focus(field, true);
    key(f, RML_KEY_UP);
    test_check(
        !rml_element_has_attribute(field, "mixed") &&
            !rml_element_has_attribute(field, "data-mixed") &&
            number(f, "committed") == 61,
        "a step sets one value and clears mixed, as a checkbox's indeterminate"
    );
    set_bool(f, "mixed", false);
    set_number(f, "value", 61);
    frames(f, 1);
}

/* A readonly field takes no gesture. */
static void readonly_checks(controls_fixture *f)
{
    rml_element *field = by_id(f, "number");
    set_bool(f, "readonly", true);
    frames(f, 1);
    double inputs = number(f, "inputs"), changes = number(f, "changes");
    drag(f, field, 3, 8);
    release(f);
    click(f, field);
    rml_element_focus(field, true);
    key(f, RML_KEY_UP);
    test_check(
        rml_element_has_attribute(field, "data-readonly") &&
            number(f, "inputs") == inputs && number(f, "changes") == changes &&
            !rml_element_query_selector(field, "input"),
        "a readonly field neither scrubs, steps nor types"
    );
    set_bool(f, "readonly", false);
    frames(f, 1);
}

/* New values repaint their glyphs without laying out the document. */
static void repaint_checks(controls_fixture *f)
{
    rml_element *field = by_id(f, "number");
    frames(f, 2);
    int before = layouts;
    set_number(f, "value", 42.5);
    frames(f, 1);
    set_number(f, "value", 43.75);
    frames(f, 1);
    char text[32];
    shown(field, ".value", text, sizeof text);
    test_check(layouts == before && !strcmp(text, "43.75"), "a number field's new value repaints without laying out the document");
    rml_element *readout = by_id(f, "readout");
    set_text(f, "readout", "12.5 ms");
    frames(f, 2);
    before = layouts;
    set_text(f, "readout", "13.5 ms");
    frames(f, 1);
    shown(readout, NULL, text, sizeof text);
    test_check(layouts == before && !strcmp(text, "13.5 ms"), "live-text changes its glyphs without laying out the document");
}

/* Publishes two samples of `value` under `name`, the newest at 30 s. */
static void publish(const char *name, float value)
{
    float times[] = { 0, 30 }, values[] = { value, value };
    line_chart_publish(name, &(line_chart_series){ times, values, 2, 0, 30 });
}

static bool label_shows(rml_element *label, const char *expected)
{
    char text[32];
    shown(label, NULL, text, sizeof text);
    return rml_element_is_visible(label, false) && !strcmp(text, expected);
}

/* A line chart draws what C publishes under the names its series attribute
   lists: its badge shows the first series' newest value, a new sample moves
   it without laying out the document, the pointer reads the value under it,
   and a forgotten series leaves the chart empty. */
static void chart_checks(controls_fixture *f)
{
    rml_element *chart = by_id(f, "chart");
    frames(f, 1);
    rml_element *badge = rml_element_query_selector(chart, ".chart-badge");
    rml_element *hover = rml_element_query_selector(chart, ".chart-hover");
    test_check(
        badge && hover && !rml_element_is_visible(badge, false),
        "a line chart builds its badge and scrub readout and shows nothing "
        "before its series are published"
    );
    publish("test:frame", 12.5f);
    publish("test:phase", 3);
    frames(f, 2);
    int ticks = rml_element_query_selector_all(chart, ".chart-tick", NULL, 0);
    test_check(
        label_shows(badge, "12.5 ms") && ticks == 5,
        "publishing a chart's series shows its first series' newest value "
        "and the range's ticks"
    );
    int before = layouts;
    publish("test:frame", 16.5f);
    frames(f, 1);
    publish("test:frame", 14.5f);
    frames(f, 1);
    test_check(
        layouts == before && label_shows(badge, "14.5 ms"),
        "new samples move the chart's badge and redraw it without laying out "
        "the document"
    );
    rml_vec2 at = center(chart);
    move(f, at);
    frames(f, 1);
    test_check(label_shows(hover, "14.5 ms"), "the pointer over a chart reads the value under it");
    move(f, (rml_vec2){ 2, 2 });
    frames(f, 1);
    test_check(!rml_element_is_visible(hover, false), "leaving a chart hides its readout");
    line_chart_publish("test:frame", NULL);
    line_chart_publish("test:phase", NULL);
    frames(f, 1);
    test_check(!rml_element_is_visible(badge, false), "a forgotten series leaves its chart empty");
}

/* A splitter's drag sends input within its bounds and change once. */
static void splitter_checks(controls_fixture *f)
{
    rml_element *split = by_id(f, "split");
    drag(f, split, 4, 10);
    test_check(
        number(f, "split_inputs") >= 3 && number(f, "width") == 80 &&
            rml_element_has_attribute(split, "data-dragging"),
        "dragging a splitter sends input and stops at its maximum"
    );
    release(f);
    test_check(
        number(f, "split_changes") == 1 &&
            !rml_element_has_attribute(split, "data-dragging"),
        "releasing a splitter sends one change"
    );
}

/* The color field's picker sends input for each edit and change once as it
 * closes. */
static void color_checks(controls_fixture *f)
{
    rml_element *field = by_id(f, "color");
    click(f, field);
    frames(f, 3);
    rml_element *hex = rml_element_query_selector(field, ".popup.picker .hex");
    test_check(
        rml_element_has_attribute(field, "data-open") && hex &&
            rml_context_focus_element(f->context) == hex,
        "a color field opens its picker focused on the hex value"
    );
    if (!hex) {
        return;
    }
    rml_element_select(hex);
    type(f, "112233");
    key(f, RML_KEY_RETURN);
    test_check(
        number(f, "color_inputs") >= 1 && number(f, "color_changes") == 0,
        "hex edits send input while the picker stays open"
    );
    rml_element_focus(hex, true);
    key(f, RML_KEY_ESCAPE);
    frames(f, 2);
    test_check(
        number(f, "color_changes") == 1 && !strcmp(text(f, "color"), "#112233ff") &&
            !rml_element_has_attribute(field, "data-open") &&
            rml_context_focus_element(f->context) == field,
        "Escape closes the picker with one change, keeps alpha, and returns "
        "focus"
    );
}

/* projects/studio's control widgets, expanded as a ui asset is, bound to a
 * panel and a property row. */
static const char WIDGETS
    [] = "<use ui=\"Panel\" data-alias-panel=\"transform\">"
         "<use ui=\"PropertyRow\" data-alias-property=\"position\">"
         "<use ui=\"VectorField\" data-alias-axes=\"position.axes\" "
         "data-alias-values=\"positionValues\" "
         "data-alias-field=\"position\" "
         "data-event-change=\"changes = changes + 1; named = ev.name\"/>"
         "</use>"
         "<use ui=\"NumberField\" id=\"lone\" "
         "data-attr-value=\"positionValues[0]\" "
         "precision=\"2\"/>"
         "<use ui=\"PropertyRow\" data-alias-property=\"position\">"
         "<use ui=\"ColorField\" data-attr-value=\"color\"/>"
         "<use ui=\"Checkbox\" data-checked=\"checked\">Visible</use>"
         "</use>"
         "<use ui=\"Splitter\" axis=\"y\" min=\"0\" max=\"10\" value=\"5\"/>"
         "</use>";

static const char WIDGET_DATA
    [] = "{\"transform\": {\"title\": \"Transform\", \"badge\": \"\", "
         "\"open\": true},"
         "\"position\": {\"id\": \"position\", \"label\": \"Position\", "
         "\"tip\": \"Position\", \"about\": \"Where it is\", \"overridden\": "
         "true, \"readonly\": false, \"float\": true, \"axes\": [{\"key\": "
         "\"x\", \"label\": \"X\", \"slot\": 0, \"precision\": 2, \"step\": "
         "1, \"min\": \"-inf\", \"max\": \"inf\", \"unit\": \"\", "
         "\"slider\": false, \"mixed\": false, \"overridden\": true}, "
         "{\"key\": "
         "\"y\", \"label\": \"Y\", \"slot\": 1, \"precision\": 2, \"step\": "
         "1, \"min\": \"-inf\", \"max\": \"inf\", \"unit\": \"\", "
         "\"slider\": false, \"mixed\": false, \"overridden\": false}]},"
         "\"positionValues\": [1.5, 2], "
         "\"color\": \"#336699ff\", \"checked\": false, \"changes\": 0, "
         "\"named\": \"\"}";

/* The widgets' expansion of WIDGETS as a document's RML, or NULL. */
static char *widget_document()
{
    ui_document document = { 0 };
    char error[256] = "", **uses = NULL;
    bool expanded = ui_document_read(WIDGETS, "", NULL, NULL, &document, error, sizeof error) &&
                    ui_document_expand(
                        &document,
                        "WidgetTest",
                        "projects/studio/content/ui",
                        false,
                        &uses,
                        error,
                        sizeof error
                    );
    for (int i = 0; i < acount(uses); i++) {
        free(uses[i]);
    }
    afree(uses);
    char *rml = NULL;
    if (expanded) {
        size_t size = strlen(document.rml) + strlen(document.rcss) + 256;
        rml = malloc(size);
        snprintf(
            rml,
            size,
            "<rml><head><style>body { font-family: Inter; font-size: 13dp; "
            "--primary: #3b82f6; } %s</style></head><body "
            "data-model=\"widgets\">%s</body></rml>",
            document.rcss,
            document.rml
        );
    } else {
        fprintf(stderr, "  widgets: %s\n", error);
    }
    ui_document_free(&document);
    return rml;
}

static void widget_checks()
{
    char *rml = widget_document();
    test_check(rml != NULL, "the control widgets expand");
    if (!rml) {
        return;
    }
    rml_backend *backend = rml_backend_create(640, 480, 1);
    rml_context *context = rml_backend_context(backend);
    yyjson_doc *parsed = yyjson_read(WIDGET_DATA, strlen(WIDGET_DATA), 0);
    yyjson_mut_doc *data = yyjson_doc_mut_copy(parsed, NULL);
    yyjson_doc_free(parsed);
    rml_data_model *model = rml_data_model_create(context, "widgets", data, NULL, NULL);
    rml_element *doc = rml_context_load_document_from_memory(context, rml, "[widgets]");
    free(rml);
    assert(doc);
    rml_document_show(doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    for (int i = 0; i < 3; i++) {
        rml_backend_update(backend, FRAME_SECONDS);
    }
    rml_element *found[4], *axes[2] = { NULL, NULL };
    int count = 0;
    for (int i = rml_element_query_selector_all(doc, ".joined number-field", found, 4);
         i-- > 0;) {
        if (!rml_element_has_attribute(found[i], "data-for") && count < 2) {
            axes[count++] = found[i];
        }
    }
    char title[32] = "", label[32] = "", x[32] = "", lone[32] = "";
    shown(rml_element_query_selector(doc, ".panel .title"), NULL, title, sizeof title);
    shown(rml_element_query_selector(doc, ".prop .label span"), NULL, label, sizeof label);
    shown(rml_element_by_id(doc, "lone"), ".value", lone, sizeof lone);
    if (count == 2) {
        shown(axes[1], ".value", x, sizeof x);
    }
    test_check(
        count == 2 && !strcmp(title, "Transform") && !strcmp(label, "Position") &&
            !strcmp(x, "1.5") && !strcmp(lone, "1.5") &&
            rml_element_query_selector(doc, ".prop[data-overridden] number-field[data-overridden]"),
        "Panel, PropertyRow, VectorField and NumberField show a component's "
        "row from data"
    );
    if (count == 2) {
        rml_element_focus(axes[0], true);
        rml_context_process_key_down(context, RML_KEY_UP, 0);
        rml_backend_update(backend, FRAME_SECONDS);
    }
    yyjson_mut_val *root = yyjson_mut_doc_get_root(data);
    const char *axis = yyjson_mut_get_str(yyjson_mut_obj_get(root, "named"));
    test_check(
        yyjson_mut_get_num(yyjson_mut_obj_get(root, "changes")) == 1 && axis &&
            !strcmp(axis, "y"),
        "a VectorField's change names its axis as ev.name"
    );
    rml_element *swatch = rml_element_query_selector(doc, "color-field .swatch");
    char background[32] = "";
    if (swatch) {
        rml_element_property(swatch, "background-color", background, sizeof background);
    }
    rml_element *check = rml_element_query_selector(doc, ".check input");
    test_check(
        swatch && background[0] && check &&
            rml_element_query_selector(doc, "splitter[axis=y]"),
        "ColorField, Checkbox and Splitter build their native parts"
    );
    rml_element *head = rml_element_query_selector(doc, ".panel-head");
    rml_vec2 at = rml_element_absolute_offset(head, RML_BOX_BORDER);
    rml_context_process_mouse_move(context, (int)at.x + 4, (int)at.y + 4, 0);
    rml_context_process_mouse_button_down(context, 0, 0);
    rml_context_process_mouse_button_up(context, 0, 0);
    rml_backend_update(backend, FRAME_SECONDS);
    yyjson_mut_val *panel = yyjson_mut_obj_get(root, "transform");
    test_check(
        !yyjson_mut_get_bool(yyjson_mut_obj_get(panel, "open")) &&
            !rml_element_query_selector(doc, ".panel[data-open]"),
        "a Panel's head folds it"
    );
    rml_document_close(doc);
    rml_backend_update(backend, FRAME_SECONDS);
    rml_data_model_destroy(model);
    yyjson_mut_doc_free(data);
    rml_backend_destroy(backend);
}

static const char TAB_DOCUMENT
    [] = "<rml><head><style>"
         "body { font-family: Inter; font-size: 13dp; }"
         "tab-list { display: flex; width: 180dp; height: 32dp; overflow-x: "
         "auto; overflow-y: hidden; }"
         "tab-list scrollbarhorizontal { height: 0dp; }"
         ".tab { display: flex; flex: 0 0 110dp; height: 32dp; }"
         "button { display: block; width: 80dp; height: 30dp; }"
         "button.close { width: 26dp; }"
         "</style></head><body><tab-list id=\"tabs\" role=\"tablist\">"
         "<div class=\"tab\" role=\"presentation\"><button role=\"tab\" "
         "id=\"first\" aria-selected=\"true\">First</button>"
         "<button class=\"close\" data-tab-close=\"\">X</button></div>"
         "<div class=\"tab\" role=\"presentation\"><button role=\"tab\" "
         "id=\"disabled\" data-disabled=\"\">Disabled</button></div>"
         "<div class=\"tab\" role=\"presentation\"><button role=\"tab\" "
         "id=\"last\" aria-selected=\"false\">Last</button>"
         "<button class=\"close\" data-tab-close=\"\">X</button></div>"
         "</tab-list></body></rml>";

typedef struct tab_events
{
    rml_element *first, *last;
    int selections, closes;
} tab_events;

static void tab_clicked(void *user, const rml_event *e)
{
    tab_events *events = user;
    if (rml_element_has_attribute(e->target, "data-tab-close")) {
        events->closes++;
        return;
    }
    events->selections++;
    rml_element_set_attribute(
        events->first,
        "aria-selected",
        e->target == events->first ? "true" : "false"
    );
    rml_element_set_attribute(events->last, "aria-selected", e->target == events->last ? "true" : "false");
}

void test_ui_tab_list()
{
    controls_fixture f = { 0 };
    f.backend = rml_backend_create(640, 480, 1);
    f.context = rml_backend_context(f.backend);
    f.doc = rml_context_load_document_from_memory(f.context, TAB_DOCUMENT, "[tabs]");
    assert(f.doc);
    rml_document_show(f.doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    frames(&f, 3);
    rml_element *tabs = by_id(&f, "tabs");
    tab_events events = { .first = by_id(&f, "first"), .last = by_id(&f, "last") };
    rml_listener *clicks = rml_listener_create(tab_clicked, &events);
    rml_element_add_event_listener(tabs, RML_EVENT_CLICK, clicks, false);
    rml_element_focus(events.first, true);
    key(&f, RML_KEY_RIGHT);
    frames(&f, 2);
    test_check(
        events.selections == 1 && rml_context_focus_element(f.context) == events.last,
        "tab arrows skip disabled tabs, focus and activate the next tab"
    );
    test_check(
        rml_element_scroll_left(tabs) > 100 &&
            rml_element_has_attribute(tabs, "data-overflow"),
        "activating an overflowed tab reveals its row and close action"
    );
    key(&f, RML_KEY_RIGHT);
    test_check(
        events.selections == 2 && rml_context_focus_element(f.context) == events.first,
        "tab arrows wrap at the end of the strip"
    );
    key(&f, RML_KEY_END);
    test_check(rml_context_focus_element(f.context) == events.last, "End activates the last enabled tab");
    key(&f, RML_KEY_HOME);
    test_check(rml_context_focus_element(f.context) == events.first, "Home activates the first enabled tab");
    int selections = events.selections;
    rml_context_process_key_down(f.context, RML_KEY_RIGHT, RML_KM_CTRL);
    rml_context_process_key_up(f.context, RML_KEY_RIGHT, RML_KM_CTRL);
    test_check(
        events.selections == selections &&
            rml_context_focus_element(f.context) == events.first,
        "modified arrow chords do not activate tabs"
    );
    frames(&f, 2);
    rml_element_set_scroll_left(tabs, 0);
    move(&f, center(events.first));
    rml_context_process_mouse_wheel(f.context, (rml_vec2){ 0, 1 }, 0);
    frames(&f, 2);
    float scrolled = rml_element_scroll_left(tabs);
    test_check(scrolled > 0, "a vertical wheel over a tab strip scrolls horizontally");
    rml_element_set_property(f.doc, "padding-top", "1dp");
    frames(&f, 3);
    test_check(fabsf(rml_element_scroll_left(tabs) - scrolled) < 0.5f, "unrelated layouts preserve manual tab scrolling");
    rml_element_set_scroll_left(tabs, 0);
    move(&f, center(events.first));
    rml_context_process_mouse_button_down(f.context, 2, 0);
    rml_context_process_mouse_button_up(f.context, 2, 0);
    test_check(events.closes == 1, "middle-click activates the tab row's close action once");
    rml_element *close = rml_element_query_selector(rml_element_parent(events.first), "[data-tab-close]");
    rml_element_set_attribute(close, "data-disabled", "");
    rml_context_process_mouse_button_down(f.context, 2, 0);
    rml_context_process_mouse_button_up(f.context, 2, 0);
    test_check(events.closes == 1, "middle-click respects a disabled close action");
    rml_element_remove_event_listener(tabs, RML_EVENT_CLICK, clicks, false);
    rml_listener_destroy(clicks);
    rml_document_close(f.doc);
    frames(&f, 1);
    rml_backend_destroy(f.backend);
}

void test_ui_controls()
{
    controls_fixture f = { 0 };
    open_fixture(&f);
    scrub_checks(&f);
    typing_checks(&f);
    cancel_checks(&f);
    mixed_checks(&f);
    readonly_checks(&f);
    repaint_checks(&f);
    splitter_checks(&f);
    color_checks(&f);
    chart_checks(&f);
    close_fixture(&f);
    widget_checks();
    test_ui_tab_list();
}
