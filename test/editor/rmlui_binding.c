/* RmlUi binding calls: documents from text, style sheets, color-mix() and
 * data models. */
#include "ui/json_items.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool red(rml_element *element)
{
    rml_color c = rml_element_color(element);
    return c.r == 255 && c.g == 0 && c.b == 0;
}

static bool loads_from_memory(rml_context *context)
{
    rml_element *doc = rml_context_load_document_from_memory(
        context,
        "<rml><head><style>div { color: #ff0000; }</style></head>"
        "<body><div id=\"a\" class=\"x\"/><div class=\"x\"/></body></rml>",
        "[memory]"
    );
    if (!doc) {
        return false;
    }
    rml_context_update(context);
    rml_element *a = rml_element_by_id(doc, "a");
    rml_element *matches[1];
    bool ok = a && rml_element_owner_document(a) == doc &&
              rml_element_query_selector_all(doc, ".x", matches, 1) == 2 &&
              matches[0] == a && red(a);
    rml_document_close(doc);
    return ok;
}

/* A later sheet wins over an earlier one; an empty sheet changes nothing. */
static bool replaces_style_sheet(rml_context *context)
{
    rml_element *doc = rml_context_load_document_from_memory(
        context,
        "<rml><head/><body><div id=\"a\"/></body></rml>",
        "[memory]"
    );
    assert(doc);
    rml_element *a = rml_element_by_id(doc, "a");
    bool set = rml_document_set_named_style_sheet(doc, "test", "#a { color: #ff0000; }\n#a { color: #00ff00; }\n");
    bool empty = rml_document_set_named_style_sheet(doc, "test", "");
    rml_context_update(context);
    rml_color c = rml_element_color(a);
    rml_document_close(doc);
    return set && !empty && c.r == 0 && c.g == 255 && c.b == 0;
}

static bool near(rml_color c, int r, int g, int b, int a)
{
    return abs(c.r - r) <= 2 && abs(c.g - g) <= 2 && abs(c.b - b) <= 2 &&
           abs(c.a - a) <= 2;
}

/* color-mix() (cmake/patches/rmlui-color-mix.patch), with tokens resolved
 * when the element is styled. */
static bool mixes_colors(rml_context *context)
{
    rml_element *doc = rml_context_load_document_from_memory(
        context,
        "<rml><head><style>"
        "body { --accent: #ff0000; }"
        "#srgb { color: color-mix(in srgb, #ff0000 50%, #0000ff); }"
        "#token { color: color-mix(in srgb, var(--accent) 50%, transparent); }"
        "#oklab { color: color-mix(in oklab, black, white); }"
        "</style></head><body><div id=\"srgb\"/><div id=\"token\"/><div "
        "id=\"oklab\"/></body></rml>",
        "[memory]"
    );
    assert(doc);
    rml_context_update(context);
    bool ok = near(rml_element_color(rml_element_by_id(doc, "srgb")), 128, 0, 128, 255) &&
              near(rml_element_color(rml_element_by_id(doc, "token")), 255, 0, 0, 128) &&
              near(rml_element_color(rml_element_by_id(doc, "oklab")), 99, 99, 99, 255);
    rml_document_close(doc);
    return ok;
}

/* Where `text` starts, relative to `row`. */
static rml_vec2 text_at(rml_element *row, rml_element *text)
{
    rml_vec2 at = rml_element_absolute_offset(text, RML_BOX_BORDER);
    rml_vec2 origin = rml_element_absolute_offset(row, RML_BOX_BORDER);
    return (rml_vec2){ at.x - origin.x, at.y - origin.y };
}

/* As in CSS, text directly inside a flex container is laid out as an item
 * of its own, where a span of it would be (cmake/patches/rmlui-flex-text.patch). */
static bool lays_out_flex_text(rml_context *context)
{
    rml_element *doc = rml_context_load_document_from_memory(
        context,
        "<rml><head><style>body { font-family: Inter; font-size: 12dp; }"
        ".row { display: flex; align-items: center; width: 300dp; height: "
        "40dp; }"
        ".box { width: 10dp; height: 10dp; }"
        "</style></head><body><div class=\"row\" id=\"bare\"><div "
        "class=\"box\" id=\"a\"/>Label"
        "<div class=\"box\" id=\"b\"/></div><div class=\"row\" "
        "id=\"wrapped\"><div class=\"box\"/>"
        "<span>Label</span></div></body></rml>",
        "[memory]"
    );
    assert(doc);
    rml_context_update(context);
    rml_element *bare = rml_element_by_id(doc, "bare"),
                *wrapped = rml_element_by_id(doc, "wrapped");
    float a = rml_element_absolute_offset(rml_element_by_id(doc, "a"), RML_BOX_BORDER)
                  .x;
    float b = rml_element_absolute_offset(rml_element_by_id(doc, "b"), RML_BOX_BORDER)
                  .x;
    rml_vec2 text = text_at(bare, rml_element_child(bare, 1));
    rml_vec2 span = text_at(wrapped, rml_element_first_child(rml_element_child(wrapped, 1)));
    rml_document_close(doc);
    return b - a > 10 + 20 && fabsf(text.x - span.x) < 0.5f &&
           fabsf(text.y - span.y) < 0.5f;
}

static char emitted[32];

static void on_emit(void *, const rml_event *event, const char *name, const char *value)
{
    snprintf(
        emitted,
        sizeof emitted,
        "%s:%s%s%s",
        rml_element_id(event->current),
        name,
        value ? "=" : "",
        value ? value : ""
    );
}

static bool text_is(rml_element *doc, const char *id, const char *text)
{
    char buffer[64];
    rml_element_inner_rml(rml_element_by_id(doc, id), buffer, sizeof buffer);
    return !strcmp(buffer, text);
}

/* A data model over JSON: text and lists follow it, a missing member reads
 * empty, bindings write back, a lazy one on blur, and emit('Name') and
 * emit('Name', value) reach the host. */
static bool binds_data(rml_context *context)
{
    yyjson_mut_doc *json = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(json);
    yyjson_mut_doc_set_root(json, root);
    yyjson_mut_obj_add_str(json, root, "title", "Tokens");
    yyjson_mut_val *rows = yyjson_mut_obj_add_arr(json, root, "rows");
    yyjson_mut_obj_add_str(json, yyjson_mut_arr_add_obj(json, rows), "name", "--primary");
    yyjson_mut_val *flags = yyjson_mut_obj_add_obj(json, root, "flags");
    rml_data_model *model = rml_data_model_create(context, "test", json, on_emit, NULL);
    rml_element *doc = rml_context_load_document_from_memory(
        context,
        "<rml><head><style>body { font-family: Inter; }</style></head>"
        "<body data-model=\"test\">"
        "<div id=\"title\">{{ title }} {{ rows.size }}</div>"
        "<div class=\"row\" data-for=\"row : rows\">{{ row.name }}</div>"
        "<div id=\"label\">{{ flags.label }}</div>"
        "<input id=\"on\" type=\"checkbox\" data-checked=\"flags.on\"/>"
        "<input id=\"lazy\" type=\"text\" data-value-lazy=\"title\"/>"
        "<div id=\"go\" data-event-click=\"emit('Go')\"/>"
        "<div id=\"pick\" data-for=\"row, i : rows\" "
        "data-event-click=\"emit('Pick', i)\"/>"
        "</body></rml>",
        "[memory]"
    );
    assert(doc);
    rml_context_update(context);
    bool shown = text_is(doc, "title", "Tokens 1") && text_is(doc, "label", "");
    yyjson_mut_obj_add_str(json, yyjson_mut_arr_add_obj(json, rows), "name", "--border");
    rml_data_model_dirty(model, "rows");
    rml_context_update(context);
    /* The data-for element stays as the hidden template. */
    bool listed = rml_element_query_selector_all(doc, ".row", NULL, 0) == 3;
    rml_element_click(rml_element_by_id(doc, "on"));
    bool wrote = rml_data_model_writes(model) == 1 &&
                 yyjson_mut_get_bool(yyjson_mut_obj_get(flags, "on"));
    rml_element_click(rml_element_by_id(doc, "go"));
    bool went = !strcmp(emitted, "go:Go");
    rml_element *picks[3];
    rml_element_query_selector_all(doc, "#pick", picks, 3);
    rml_element_click(picks[1]);
    bool picked = !strcmp(emitted, "pick:Pick=1");
    rml_element *lazy = rml_element_by_id(doc, "lazy");
    rml_element_focus(lazy, false);
    rml_element_set_value(lazy, "Theme");
    bool held = !strcmp(yyjson_mut_get_str(yyjson_mut_obj_get(root, "title")), "Tokens");
    rml_element_blur(lazy);
    bool committed = !strcmp(yyjson_mut_get_str(yyjson_mut_obj_get(root, "title")), "Theme");
    rml_document_close(doc);
    rml_context_update(context);
    rml_data_model_destroy(model);
    yyjson_mut_doc_free(json);
    return shown && listed && wrote && went && picked && held && committed;
}

/* Whether every index of `array` reads the item yyjson's own walk reaches,
 * and none past its end. */
static bool items_match(json_items *items, yyjson_mut_val *array)
{
    size_t size = yyjson_mut_arr_size(array);
    for (size_t i = 0; i <= size; i++) {
        if (json_items_get(items, array, i) != yyjson_mut_arr_get(array, i)) {
            return false;
        }
    }
    return true;
}

/* Constant-time lookups follow appends, removals, inserts and rotations by
 * themselves, and an item replaced in place after a clear. */
static bool indexes_array_items()
{
    yyjson_mut_doc *json = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *array = yyjson_mut_arr(json);
    for (int i = 0; i < 100; i++) {
        yyjson_mut_arr_add_int(json, array, i);
    }
    json_items *items = json_items_new();
    bool listed = items_match(items, array);
    yyjson_mut_arr_add_int(json, array, 100);
    bool appended = items_match(items, array);
    yyjson_mut_arr_remove(array, 40);
    bool removed = items_match(items, array);
    yyjson_mut_arr_insert(array, yyjson_mut_int(json, -1), 0);
    bool inserted = items_match(items, array);
    yyjson_mut_arr_rotate(array, 7);
    bool rotated = items_match(items, array);
    yyjson_mut_arr_replace(array, 60, yyjson_mut_int(json, 1000));
    json_items_clear(items);
    bool replaced = items_match(items, array) &&
                    yyjson_mut_get_int(json_items_get(items, array, 60)) == 1000;
    json_items_free(items);
    yyjson_mut_doc_free(json);
    return listed && appended && removed && inserted && rotated && replaced;
}

/* Whether the list's rows show `array`'s values in order, before the
 * data-for template. */
static bool rows_show(rml_element *doc, yyjson_mut_val *array)
{
    rml_element *rows[64];
    int count = rml_element_query_selector_all(doc, ".item", rows, 64);
    bool same = count == (int)yyjson_mut_arr_size(array) + 1;
    for (int i = 0; same && i < count - 1; i++) {
        char text[16], want[16];
        rml_element_inner_rml(rows[i], text, sizeof text);
        int value = yyjson_mut_get_int(yyjson_mut_arr_get(array, (size_t)i));
        snprintf(want, sizeof want, "%d", value);
        same = !strcmp(text, want);
    }
    return same;
}

/* A list's rows follow its array as it shrinks, grows and has an item
 * replaced, past the items views walk to. */
static bool lists_follow_arrays(rml_context *context)
{
    yyjson_mut_doc *json = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(json);
    yyjson_mut_doc_set_root(json, root);
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(json, root, "values");
    for (int i = 0; i < 40; i++) {
        yyjson_mut_arr_add_int(json, array, i);
    }
    rml_data_model *model = rml_data_model_create(context, "list", json, on_emit, NULL);
    rml_element *doc = rml_context_load_document_from_memory(
        context,
        "<rml><head><style>body { font-family: Inter; }</style></head>"
        "<body data-model=\"list\">"
        "<div class=\"item\" data-for=\"v : values\">{{ v }}</div>"
        "</body></rml>",
        "[memory]"
    );
    assert(doc);
    rml_context_update(context);
    bool shown = rows_show(doc, array);
    /* The same size and last item: only the dirty relists it. */
    yyjson_mut_arr_replace(array, 30, yyjson_mut_int(json, 1000));
    rml_data_model_dirty(model, "values");
    rml_context_update(context);
    bool replaced = rows_show(doc, array);
    yyjson_mut_arr_remove(array, 3);
    yyjson_mut_arr_add_int(json, array, 99);
    rml_data_model_dirty(model, "values");
    rml_context_update(context);
    bool edited = replaced && rows_show(doc, array);
    for (int i = 0; i < 10; i++) {
        yyjson_mut_arr_remove_last(array);
    }
    rml_data_model_dirty(model, "values");
    rml_context_update(context);
    bool shrunk = rows_show(doc, array);
    rml_data_reads reads = rml_data_model_reads(model);
    rml_document_close(doc);
    rml_context_update(context);
    rml_data_model_destroy(model);
    yyjson_mut_doc_free(json);
    return shown && edited && shrunk && reads.values > 0 && reads.lookups > 0;
}

bool test_rmlui_binding()
{
    rml_backend *backend = rml_backend_create(320, 240, 1);
    rml_context *context = rml_backend_context(backend);
    bool ok = loads_from_memory(context) && replaces_style_sheet(context) &&
              mixes_colors(context) && binds_data(context) &&
              lays_out_flex_text(context) && indexes_array_items() &&
              lists_follow_arrays(context);
    rml_backend_destroy(backend);
    return ok;
}
