/* Lists and drag (src/ui/drag.h, src/ui/list.h) in a bare RmlUi context, as
 * games have them: a drag from one document to targets in another, picked up
 * past FEEL_DRAG_DP, with the accept and over marks, tree and list zones,
 * the drop event's fields, ui_drag_dropped, the hint, Escape, the swallowed
 * click and a press after a drag; a list window that keeps its length and
 * its elements as it scrolls, reveals, follows and fills before layout; and
 * projects/studio's List, TreeRow and Tile widgets bound to rows. */
#include "assets/ui_document.h"
#include "render/ui.h"
#include "support/array.h"
#include "test/suite_internal.h"
#include "ui/drag.h"
#include "ui/input_feel.h"
#include "ui/list.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_SECONDS (1.0 / 60.0)

static const char SOURCES[] =
    "<rml><head><style>"
    "body { position: absolute; left: 0px; top: 0px; width: 200px; height: "
    "400px; font-family: Inter; font-size: 13px; }"
    ".row { display: block; height: 20px; }"
    "#list { display: block; height: 200px; overflow-y: auto; --row: 20px; }"
    "#spacer { display: block; box-sizing: border-box; }"
    "</style></head><body data-model=\"drag\">"
    "<div id=\"source\" class=\"row\" data-drag=\"asset\" "
    "data-drag-payload=\"apple\" data-drag-label=\"Apple\">Apple</div>"
    "<div id=\"pear\" class=\"row\" data-drag=\"asset\" "
    "data-drag-payload=\"pear\" data-event-click=\"clicks = clicks + 1\" "
    "data-event-dblclick=\"doubles = doubles + 1\">Pear</div>"
    "<div id=\"list\"><div id=\"spacer\"><div class=\"row\" "
    "data-for=\"r : rows\">{{ r }}</div></div></div>"
    "</body></rml>";

static const char TARGETS
    [] = "<rml><head><style>"
         "body { position: absolute; left: 300px; top: 0px; width: 200px; "
         "height: 400px; font-family: Inter; font-size: 13px; }"
         "div { display: block; height: 40px; }"
         "</style></head><body data-model=\"drag\">"
         "<div id=\"tree\" data-drop=\"other asset\" data-drop-zones=\"tree\" "
         "data-drop-hint=\"Put here\" data-event-drop=\"drops = drops + 1; "
         "type "
         "= ev.type; payload = ev.payload; zone = ev.zone\">Tree</div>"
         "<div id=\"wrong\" data-drop=\"other\">Other</div>"
         "<div id=\"surface\" data-drop=\"asset\">Surface</div>"
         "<div id=\"halves\" data-drop=\"asset\" "
         "data-drop-zones=\"list\">Halves</div>"
         "</body></rml>";

/* Above the others, as Studio's layer is, and passing the pointer. */
static const char LAYER[] = "<rml><head><style>"
                            "body { position: absolute; left: 0px; top: 0px; "
                            "width: 640px; height: 480px; z-index: 10; "
                            "pointer-events: none; font-family: Inter; }"
                            "</style></head><body/></rml>";

typedef struct
{
    rml_backend *backend;
    rml_context *context;
    yyjson_mut_doc *json;
    yyjson_mut_val *root;
    rml_data_model *model;
    rml_element *sources, *targets, *layer;
} lists_fixture;

static void frames(lists_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        rml_backend_update(f->backend, FRAME_SECONDS);
    }
}

static double number(lists_fixture *f, const char *name)
{
    return yyjson_mut_get_num(yyjson_mut_obj_get(f->root, name));
}

static const char *text(lists_fixture *f, const char *name)
{
    const char *value = yyjson_mut_get_str(yyjson_mut_obj_get(f->root, name));
    return value ? value : "";
}

static rml_element *by_id(lists_fixture *f, const char *id)
{
    rml_element *el = rml_element_by_id(f->sources, id);
    el = el ? el : rml_element_by_id(f->targets, id);
    assert(el);
    return el;
}

/* A point in the element: its middle across, `t` of its height down. */
static rml_vec2 at(rml_element *el, float t)
{
    rml_vec2 offset = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    return (rml_vec2){ offset.x + size.x / 2, offset.y + size.y * t };
}

static void move(lists_fixture *f, rml_vec2 point)
{
    rml_context_process_mouse_move(f->context, (int)point.x, (int)point.y, 0);
    frames(f, 1);
}

static void down(lists_fixture *f)
{
    rml_context_process_mouse_button_down(f->context, 0, 0);
    frames(f, 1);
}

static void up(lists_fixture *f)
{
    rml_context_process_mouse_button_up(f->context, 0, 0);
    frames(f, 1);
}

static rml_element *show(lists_fixture *f, const char *rml, const char *url)
{
    rml_element *doc = rml_context_load_document_from_memory(f->context, rml, url);
    assert(doc);
    rml_document_show(doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    return doc;
}

static void open_fixture(lists_fixture *f)
{
    f->backend = rml_backend_create(640, 480, 1);
    f->context = rml_backend_context(f->backend);
    f->json = yyjson_mut_doc_new(NULL);
    f->root = yyjson_mut_obj(f->json);
    yyjson_mut_doc_set_root(f->json, f->root);
    static const char *const NUMBERS[] = { "drops", "clicks", "doubles" };
    for (size_t i = 0; i < sizeof NUMBERS / sizeof *NUMBERS; i++) {
        yyjson_mut_obj_add_real(f->json, f->root, NUMBERS[i], 0);
    }
    static const char *const TEXTS[] = { "type", "payload", "zone" };
    for (size_t i = 0; i < sizeof TEXTS / sizeof *TEXTS; i++) {
        yyjson_mut_obj_add_strcpy(f->json, f->root, TEXTS[i], "");
    }
    yyjson_mut_obj_add_arr(f->json, f->root, "rows");
    f->model = rml_data_model_create(f->context, "drag", f->json, NULL, NULL);
    f->sources = show(f, SOURCES, "[sources]");
    f->targets = show(f, TARGETS, "[targets]");
    f->layer = show(f, LAYER, "[layer]");
}

static void close_fixture(lists_fixture *f)
{
    rml_element *docs[] = { f->layer, f->targets, f->sources };
    for (size_t i = 0; i < sizeof docs / sizeof *docs; i++) {
        rml_document_close(docs[i]);
    }
    frames(f, 1);
    rml_data_model_destroy(f->model);
    yyjson_mut_doc_free(f->json);
    rml_backend_destroy(f->backend);
}

/* How many elements in the context have the attribute. */
static int marked(lists_fixture *f, const char *attribute)
{
    char selector[64];
    snprintf(selector, sizeof selector, "[%s]", attribute);
    rml_element *found[8];
    return rml_element_query_selector_all(rml_context_root(f->context), selector, found, 8);
}

static void part_text(rml_element *overlay, const char *selector, char *out, size_t size)
{
    rml_element *part = overlay ? rml_element_query_selector(overlay, selector) : NULL;
    out[0] = '\0';
    if (part) {
        rml_element_inner_rml(part, out, size);
    }
}

static bool has_value(rml_element *el, const char *attribute, const char *value)
{
    char found[32] = "";
    rml_element_attribute(el, attribute, found, sizeof found);
    return !strcmp(found, value);
}

/* ---------- Drag ---------- */

/* Picked up past the threshold, marking the targets that take it in another
 * document, the one under the pointer, its zone and the overlay. */
static void pickup_checks(lists_fixture *f)
{
    rml_element *source = by_id(f, "source"), *tree = by_id(f, "tree");
    rml_vec2 start = at(source, 0.5f);
    move(f, start);
    down(f);
    move(f, (rml_vec2){ start.x + FEEL_DRAG_DP - 1, start.y });
    test_check(!ui_drag_active(f->context, NULL) && !marked(f, "data-dragging"), "a press moved under FEEL_DRAG_DP is no drag");
    move(f, (rml_vec2){ start.x + FEEL_DRAG_DP + 1, start.y });
    ui_drag_item item = { 0 };
    test_check(
        ui_drag_active(f->context, &item) && !strcmp(item.type, "asset") &&
            !strcmp(item.payload, "apple") &&
            rml_element_has_attribute(source, "data-dragging"),
        "past FEEL_DRAG_DP the source is picked up with its type and payload"
    );
    test_check(
        rml_element_has_attribute(tree, "data-drop-accept") &&
            rml_element_has_attribute(by_id(f, "surface"), "data-drop-accept") &&
            !rml_element_has_attribute(by_id(f, "wrong"), "data-drop-accept") &&
            marked(f, "data-drop-accept") == 3,
        "every target in the context that takes the type is marked, in "
        "another document too"
    );
    move(f, at(tree, 0.1f));
    rml_element *overlay = rml_element_query_selector(f->layer, "drag-overlay.drag-overlay");
    char label[32], hint[32];
    part_text(overlay, ".label", label, sizeof label);
    part_text(overlay, ".hint", hint, sizeof hint);
    test_check(
        rml_element_has_attribute(tree, "data-drop-over") &&
            has_value(tree, "data-drop-zone", "before") && ui_drag_over(tree, &item) &&
            !strcmp(item.zone, "before") && marked(f, "data-drop-over") == 1,
        "the target under the pointer is over, its top quarter before"
    );
    test_check(
        overlay && !strcmp(label, "Apple") && !strcmp(hint, "Put here") &&
            rml_element_has_attribute(overlay, "data-over") &&
            rml_element_has_attribute(overlay, "data-hint"),
        "the overlay shows in the topmost document with the label and the "
        "target's hint"
    );
    move(f, at(tree, 0.5f));
    bool inside = has_value(tree, "data-drop-zone", "inside");
    move(f, at(tree, 0.9f));
    test_check(
        inside && has_value(tree, "data-drop-zone", "after"),
        "a tree target's middle drops inside and its bottom quarter after"
    );
    ui_drag_hint(f->context, "Collect");
    part_text(overlay, ".hint", hint, sizeof hint);
    test_check(!strcmp(hint, "Collect"), "C names what a drop does under the label");
}

/* Released over the target: a drop event with its fields, kept once for C,
 * and every mark gone. */
static void drop_checks(lists_fixture *f)
{
    rml_element *tree = by_id(f, "tree");
    up(f);
    test_check(
        number(f, "drops") == 1 && !strcmp(text(f, "type"), "asset") &&
            !strcmp(text(f, "payload"), "apple") && !strcmp(text(f, "zone"), "after"),
        "a drop sends the target a drop event with its type, payload and zone"
    );
    ui_drag_item item = { 0 };
    bool other = ui_drag_dropped(tree, "other", &item);
    bool taken = ui_drag_dropped(tree, "asset", &item) &&
                 !strcmp(item.payload, "apple");
    test_check(!other && taken && !ui_drag_dropped(tree, "asset", &item), "C takes the drop on its target once, by type");
    rml_element *overlay = rml_element_query_selector(f->layer, "drag-overlay");
    frames(f, 3);
    char display[16] = "";
    if (overlay) {
        rml_element_property(overlay, "display", display, sizeof display);
    }
    test_check(
        !ui_drag_active(f->context, NULL) && !marked(f, "data-drop-accept") &&
            !marked(f, "data-drop-over") && !marked(f, "data-dragging") &&
            !rml_element_has_attribute(tree, "data-drop-zone") &&
            !strcmp(display, "none"),
        "after a drop no target, source or overlay stays marked or shown"
    );
}

static void drag_to(lists_fixture *f, rml_element *from, rml_element *to, float t)
{
    rml_vec2 start = at(from, 0.5f);
    move(f, start);
    down(f);
    move(f, (rml_vec2){ start.x + 10, start.y });
    move(f, at(to, t));
}

/* A surface without a drop handler takes the drop in C; list zones split a
 * target in halves; Escape and ui_drag_cancel put a drag back. */
static void surface_and_cancel_checks(lists_fixture *f)
{
    rml_element *source = by_id(f, "source"), *surface = by_id(f, "surface");
    drag_to(f, source, surface, 0.5f);
    up(f);
    ui_drag_item item = { 0 };
    test_check(
        ui_drag_dropped(surface, NULL, &item) &&
            !strcmp(item.payload, "apple") && !item.zone[0],
        "a C surface takes a drop on it with ui_drag_dropped"
    );
    rml_element *halves = by_id(f, "halves");
    drag_to(f, source, halves, 0.4f);
    bool before = has_value(halves, "data-drop-zone", "before");
    move(f, at(halves, 0.6f));
    test_check(before && has_value(halves, "data-drop-zone", "after"), "a list target's halves drop before and after it");
    rml_element *tree = by_id(f, "tree");
    move(f, at(tree, 0.5f));
    rml_context_process_key_down(f->context, RML_KEY_ESCAPE, 0);
    frames(f, 1);
    bool cancelled = !ui_drag_active(f->context, NULL) && !marked(f, "data-drop-over");
    up(f);
    test_check(
        cancelled && number(f, "drops") == 1 && !ui_drag_dropped(tree, NULL, &item),
        "Escape cancels a drag: nothing lands, even released over a target"
    );
    drag_to(f, source, tree, 0.5f);
    ui_drag_cancel(f->context);
    bool put_back = !ui_drag_active(f->context, NULL) &&
                    !marked(f, "data-drop-accept");
    up(f);
    test_check(put_back && number(f, "drops") == 1, "a host's ui_drag_cancel ends a drag whose release it saw first");
}

/* The release ending a drag clicks nothing, and the next press is a click,
 * not a double click; a double click after it still is one. */
static void click_checks(lists_fixture *f)
{
    rml_element *pear = by_id(f, "pear"), *surface = by_id(f, "surface");
    drag_to(f, pear, surface, 0.5f);
    move(f, at(pear, 0.5f));
    up(f);
    test_check(number(f, "clicks") == 0, "the release ending a drag is no click");
    down(f);
    up(f);
    test_check(
        number(f, "clicks") == 1 && number(f, "doubles") == 0,
        "a quick press on a row just dragged from is a click, not a double "
        "click"
    );
    for (int i = 0; i < 2; i++) {
        down(f);
        up(f);
    }
    test_check(number(f, "doubles") == 1, "a double click after it still double-clicks");
}

/* ---------- List window ---------- */

static void fill_rows(lists_fixture *f, int first, int length)
{
    yyjson_mut_val *rows = yyjson_mut_obj_get(f->root, "rows");
    while ((int)yyjson_mut_arr_size(rows) < length) {
        yyjson_mut_arr_add_strcpy(f->json, rows, "");
    }
    while ((int)yyjson_mut_arr_size(rows) > length) {
        yyjson_mut_arr_remove_last(rows);
    }
    for (int i = 0; i < length; i++) {
        char row[16];
        snprintf(row, sizeof row, "r%d", first + i);
        ui_data_set_text(f->json, yyjson_mut_arr_get(rows, (size_t)i), row);
    }
    rml_data_model_dirty(f->model, "rows");
    frames(f, 1);
}

/* The first row data-for made, past its template. */
static rml_element *first_row(rml_element *spacer)
{
    for (int i = 0; i < rml_element_child_count(spacer); i++) {
        rml_element *row = rml_element_child(spacer, i);
        if (!rml_element_has_attribute(row, "data-for")) {
            return row;
        }
    }
    return NULL;
}

static float padding_top(rml_element *spacer)
{
    char value[32] = "";
    rml_element_property(spacer, "padding-top", value, sizeof value);
    return strtof(value, NULL);
}

static void list_checks(lists_fixture *f)
{
    rml_element *list = by_id(f, "list"), *spacer = by_id(f, "spacer");
    ui_list_state state = UI_LIST_STATE_INIT;
    ui_list desc = { .count = 1000, .reveal = -1 };
    int first = -1;
    int length = ui_list_window(list, &desc, &state, &first);
    frames(f, 1);
    char height[32] = "";
    rml_element_property(spacer, "height", height, sizeof height);
    test_check(
        first == 0 && length == 23 && strtof(height, NULL) == 20000,
        "a list window fills its view and overscan with rows of --row, and "
        "its spacer is as tall as every row"
    );
    fill_rows(f, first, length);
    rml_element *row = first_row(spacer);
    rml_element_set_scroll_top(list, 5000);
    int scrolled = ui_list_window(list, &desc, &state, &first);
    fill_rows(f, first, scrolled);
    char shown[16] = "";
    rml_element_inner_rml(first_row(spacer), shown, sizeof shown);
    test_check(
        scrolled == length && first == 244 && padding_top(spacer) == 4880 &&
            first_row(spacer) == row && !strcmp(shown, "r244"),
        "scrolled, the window keeps its length, so its rows keep their "
        "elements and show the rows in view"
    );
    rml_element_set_scroll_top(list, 19800);
    int last = ui_list_window(list, &desc, &state, &first);
    test_check(last == length && first == 1000 - length, "at the end the window keeps its length, starting earlier");
    desc.reveal = 10;
    ui_list_window(list, &desc, &state, &first);
    float revealed = rml_element_scroll_top(list);
    rml_element_set_scroll_top(list, 1000);
    ui_list_window(list, &desc, &state, &first);
    test_check(revealed == 200 && rml_element_scroll_top(list) == 1000, "reveal scrolls the least that shows its row, once");
    desc = (ui_list){ .count = 1000, .reveal = -1, .follow = true };
    rml_element_set_scroll_top(list, 19800);
    ui_list_window(list, &desc, &state, &first);
    desc.count = 1010;
    ui_list_window(list, &desc, &state, &first);
    frames(f, 1);
    ui_list_window(list, &desc, &state, &first);
    test_check(rml_element_scroll_top(list) == 20000, "a following list at its bottom stays there as rows arrive");
    desc = (ui_list){ .count = 5, .reveal = -1 };
    test_check(ui_list_window(list, &desc, &state, &first) == 5 && first == 0, "a short list's window holds all of it");
}

/* ---------- Widgets ---------- */

static const char WIDGETS
    [] = "<use ui=\"List\" id=\"widget-list\">"
         "<use ui=\"TreeRow\" data-for=\"row : rows\" data-alias-item=\"row\" "
         "data-attr-data-drag=\"row.drag\" "
         "data-attr-data-drag-payload=\"row.label\"></use>"
         "</use>"
         "<use ui=\"Tile\" id=\"widget-tile\" data-alias-tile=\"bank\"></use>";

static const char WIDGET_DATA
    [] = "{\"rows\": ["
         "{\"label\": \"Swarm\", \"icon\": \"P\", \"meta\": \"prototype\", "
         "\"depth\": 2, \"parent\": true, \"open\": true, \"selected\": true, "
         "\"active\": false, \"marked\": false, \"hidden\": false, \"drag\": "
         "\"prototype\"},"
         "{\"label\": \"Leaf\", \"icon\": \"L\", \"meta\": \"\", \"depth\": 0, "
         "\"parent\": false, \"open\": false, \"selected\": false, \"active\": "
         "false, \"marked\": false, \"hidden\": false, \"drag\": \"\"}],"
         "\"bank\": {\"label\": \"Bank\", \"icon\": \"U\", \"badge\": \"B\", "
         "\"image\": \"\", \"loading\": false, \"selected\": false, "
         "\"unresolved\": false}}";

static char *widget_document()
{
    ui_document document = { 0 };
    char error[256] = "", **uses = NULL;
    bool expanded = ui_document_read(WIDGETS, "", NULL, NULL, &document, error, sizeof error) &&
                    ui_document_expand(
                        &document,
                        "ListTest",
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
            "--row: 24dp; height: 300dp; } %s</style></head><body "
            "data-model=\"widgets\">%s</body></rml>",
            document.rcss,
            document.rml
        );
    } else {
        fprintf(stderr, "  list widgets: %s\n", error);
    }
    ui_document_free(&document);
    return rml;
}

/* The glyph of the row's twisty that shows. */
static void twisty_text(rml_element *row, char *out, size_t size)
{
    rml_element *twisties[4];
    int count = rml_element_query_selector_all(row, ".twisty", twisties, 4);
    out[0] = '\0';
    for (int i = 0; i < count && i < 4; i++) {
        if (rml_element_is_visible(twisties[i], true)) {
            rml_element_inner_rml(twisties[i], out, size);
            return;
        }
    }
}

static void shown_text(rml_element *el, const char *selector, char *out, size_t size)
{
    rml_element *part = el ? rml_element_query_selector(el, selector) : NULL;
    out[0] = '\0';
    if (part) {
        rml_element_inner_rml(part, out, size);
    }
}

static void widget_checks()
{
    char *rml = widget_document();
    test_check(rml != NULL, "List, TreeRow and Tile expand");
    if (!rml) {
        return;
    }
    rml_backend *backend = rml_backend_create(640, 480, 1);
    rml_context *context = rml_backend_context(backend);
    yyjson_doc *parsed = yyjson_read(WIDGET_DATA, strlen(WIDGET_DATA), 0);
    yyjson_mut_doc *data = yyjson_doc_mut_copy(parsed, NULL);
    yyjson_doc_free(parsed);
    rml_data_model *model = rml_data_model_create(context, "widgets", data, NULL, NULL);
    rml_element *doc = rml_context_load_document_from_memory(context, rml, "[list widgets]");
    free(rml);
    assert(doc);
    rml_document_show(doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    for (int i = 0; i < 3; i++) {
        rml_backend_update(backend, FRAME_SECONDS);
    }
    rml_element *list = rml_element_by_id(doc, "widget-list");
    rml_element *rows[4];
    int count = rml_element_query_selector_all(doc, "[data-ui=TreeRow]:not([data-for])", rows, 4);
    char twisty[8] = "", label[32] = "", meta[32] = "", padding[16] = "", leaf[8] = "";
    if (count == 2) {
        twisty_text(rows[0], twisty, sizeof twisty);
        shown_text(rows[0], ".name", label, sizeof label);
        shown_text(rows[0], ".meta", meta, sizeof meta);
        rml_element_property(
            rml_element_query_selector(rows[0], ".tree-row"),
            "padding-left",
            padding,
            sizeof padding
        );
        twisty_text(rows[1], leaf, sizeof leaf);
    }
    test_check(
        list && rml_element_is_class_set(list, "list") &&
            rml_element_query_selector(list, ".list-spacer") && count == 2 &&
            !strcmp(twisty, "\xee\xaa\xb4") && !strcmp(label, "Swarm") &&
            !strcmp(meta, "prototype") && strtof(padding, NULL) == 34 && !leaf[0] &&
            rml_element_query_selector(rows[0], ".tree-row[data-selected]") &&
            has_value(rows[0], "data-drag", "prototype") &&
            has_value(rows[0], "data-drag-payload", "Swarm"),
        "TreeRow shows a row's depth, twisty, label and meta, and its use "
        "carries the row's drag"
    );
    ui_list_state state = UI_LIST_STATE_INIT;
    int first = -1;
    int length = list
                   ? ui_list_window(list, &(ui_list){ .count = 2, .reveal = -1 }, &state, &first)
                   : 0;
    test_check(length == 2 && first == 0, "ui_list_window windows a List widget, reading its --row");
    rml_element *tile = rml_element_by_id(doc, "widget-tile");
    char name[32] = "", glyph[8] = "";
    shown_text(tile, ".name", name, sizeof name);
    shown_text(tile, ".glyph", glyph, sizeof glyph);
    /* The picture is a decorator, none until there is one. */
    rml_element *picture = tile ? rml_element_query_selector(tile, ".picture") : NULL;
    char decorator[64] = "";
    if (picture) {
        rml_element_property(picture, "decorator", decorator, sizeof decorator);
    }
    test_check(
        tile && !strcmp(name, "Bank") && !strcmp(glyph, "U") &&
            rml_element_query_selector(tile, ".tile:not([data-ready])") &&
            (!decorator[0] || !strcmp(decorator, "none")),
        "a Tile without its picture shows its kind's icon as a skeleton"
    );
    rml_document_close(doc);
    rml_backend_update(backend, FRAME_SECONDS);
    rml_data_model_destroy(model);
    yyjson_mut_doc_free(data);
    rml_backend_destroy(backend);
}

void test_ui_lists()
{
    lists_fixture f = { 0 };
    open_fixture(&f);
    frames(&f, 3);
    pickup_checks(&f);
    drop_checks(&f);
    surface_and_cancel_checks(&f);
    click_checks(&f);
    list_checks(&f);
    close_fixture(&f);
    widget_checks();
}
