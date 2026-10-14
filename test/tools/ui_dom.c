#include "authoring/ui_dom.h"

#include "runtime/ui/builtin_fonts.h"
#include "third_party/pico_headers/pico_unit.h"
#include "ui/controls/controls_internal.h"
#include "ui/rml.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static double elapsed()
{
    return 0;
}
static void cursor(const char *) {}
static void clipboard_set(const char *) {}
static void clipboard_get(rml_string *text)
{
    rml_string_set(text, "");
}
static bool log_message(rml_log_type, const char *)
{
    return true;
}
static bool capture_glyph;
static rml_vertex glyph_vertices[256];
static size_t glyph_vertex_count, glyph_index_count;
static int glyph_uploads;
static uintptr_t compile_geometry(void *, rml_mesh mesh)
{
    if (capture_glyph) {
        assert(mesh.vertex_count <= 256);
        memcpy(glyph_vertices, mesh.vertices, mesh.vertex_count * sizeof *mesh.vertices);
        glyph_vertex_count = mesh.vertex_count;
        glyph_index_count = mesh.index_count;
        glyph_uploads++;
    }
    return 1;
}
static void draw(void *, uintptr_t, rml_vec2, uintptr_t)
{
    assert(capture_glyph && "DOM inspection must not draw");
}
static void release(void *, uintptr_t) {}
static uintptr_t texture(void *, const uint8_t *, int, int)
{
    return 1;
}
TEST_CASE(dom_queries_report_missing_context_and_bounded_pages)
{
    ui_dom *dom = ui_dom_new();
    arena a = { 0 };
    pin_array *rows;
    ui_dom_status status = ui_dom_query(dom, 0, 64, &a, &rows);
    REQUIRE(!status.ok && rows->count == 0 && strstr(status.error, "context"));
    status = ui_dom_query(dom, 0, 257, &a, &rows);
    REQUIRE(!status.ok && rows->count == 0);
    status = ui_dom_pseudo(dom, "0", "arbitrary", true);
    REQUIRE(!status.ok);
    ui_dom_free(dom);
    arena_free(&a);
    return true;
}
TEST_CASE(dom_snapshots_copy_geometry_origins_and_survive_removal)
{
    static const rml_system_interface system = { .elapsed_time = elapsed,
                                                 .set_mouse_cursor = cursor,
                                                 .set_clipboard_text = clipboard_set,
                                                 .get_clipboard_text = clipboard_get,
                                                 .log_message = log_message };
    static const rml_render_interface render = { .compile_geometry = compile_geometry,
                                                 .render_geometry = draw,
                                                 .release_geometry = release,
                                                 .generate_texture = texture,
                                                 .release_texture = release };
    rml_set_system_interface(&system);
    REQUIRE(rml_initialize());
    rml_renderer *renderer = rml_renderer_create(&render, NULL);
    rml_context *context = rml_create_context("dom-inspection-test", 128, 128, renderer);
    REQUIRE(context);
    rml_context_set_dp_ratio(context, 2);
    rml_element *document = rml_context_load_document_from_memory(
        context,
        "<rml><head/><body><div id='boxed' data-src='Widgets/Card:7' "
        "data-source-start='123' "
        "style='display:block;width:30px;height:10px;padding:3px;border:2px "
        "#fff;margin:1px;'>"
        "<span id='nested'/></div><div id='target' class='panel' "
        "style='display:block;width:40px;height:20px;'/></body></rml>",
        "dom.rml"
    );
    REQUIRE(document);
    rml_document_show(document, false, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    REQUIRE(rml_context_update(context));
    ui_dom *dom = ui_dom_new();
    ui_dom_context(dom, context);
    arena a = { 0 };
    pin_array *rows;
    ui_dom_status status = ui_dom_query(dom, 0, 64, &a, &rows);
    REQUIRE(status.ok && status.complete && rows->count > 0);
    ui_dom_row *selected = NULL;
    bool boxed = false, nested = false;
    const char *boxed_key = "";
    for (int i = 0; i < rows->count; i++) {
        ui_dom_row *row = rows->items[i].ptr;
        if (!strcmp(row->id, "target")) {
            selected = row;
        }
        if (!strcmp(row->id, "boxed")) {
            REQUIRE(row->content_width == 30 && row->content_height == 10);
            REQUIRE(row->width == 40 && row->height == 20 && row->dp_ratio == 2);
            REQUIRE(
                row->margin_top == 1 && row->margin_right == 1 &&
                row->margin_bottom == 1 && row->margin_left == 1
            );
            REQUIRE(
                row->border_top == 2 && row->border_right == 2 &&
                row->border_bottom == 2 && row->border_left == 2
            );
            REQUIRE(
                row->padding_top == 3 && row->padding_right == 3 &&
                row->padding_bottom == 3 && row->padding_left == 3
            );
            REQUIRE(
                !strcmp(row->source_asset, "Widgets/Card") &&
                row->source_line == 7 && row->source_start == 123
            );
            boxed_key = row->key;
            REQUIRE(!strcmp(row->source_key, row->key));
            boxed = true;
        }
        if (!strcmp(row->id, "nested")) {
            REQUIRE(
                !strcmp(row->source_asset, "Widgets/Card") &&
                row->source_line == 7 && row->source_start == 123
            );
            REQUIRE(strcmp(row->key, row->source_key) && !strcmp(row->source_key, boxed_key));
            nested = true;
        }
    }
    REQUIRE(boxed && nested);
    REQUIRE(selected);
    if (selected->width != 40 || selected->height != 20) {
        fprintf(
            stderr,
            "DOM target box %.9g x %.9g (expected explicit block 40x20)\n",
            selected->width,
            selected->height
        );
    }
    REQUIRE(selected->width == 40 && selected->height == 20);
    const char *key = selected->key;
    double pixels = 0;
    REQUIRE(ui_dom_unit(dom, key, "px", "x", &pixels).ok && pixels == 1);
    REQUIRE(ui_dom_unit(dom, key, "dp", "y", &pixels).ok && pixels == 2);
    REQUIRE(ui_dom_unit(dom, key, "%", "x", &pixels).ok && pixels > 0);
    REQUIRE(!ui_dom_unit(dom, key, "calc(1px)", "x", &pixels).ok && pixels == 0);
    REQUIRE(!ui_dom_unit(dom, key, "px", "z", &pixels).ok && pixels == 0);

    status = ui_dom_inspect(dom, key, "attributes", 0, 64, &a, &rows, NULL);
    REQUIRE(status.ok && rows->count >= 2);
    status = ui_dom_inspect(dom, key, "computed", 0, 256, &a, &rows, NULL);
    REQUIRE(status.ok && status.complete);
    bool width = false;
    for (int i = 0; i < rows->count; i++) {
        ui_dom_property *p = rows->items[i].ptr;
        if (!strcmp(p->name, "width") && !strcmp(p->value, "40px")) {
            width = true;
        }
    }
    REQUIRE(width);
    status = ui_dom_pseudo(dom, key, "hover", true);
    REQUIRE(status.ok);
    rml_element *target = rml_element_by_id(document, "target");
    REQUIRE(rml_element_is_pseudo_class_set(target, "hover"));
    rml_context *runtime = rml_create_context("dom-runtime-test", 64, 64, renderer);
    REQUIRE(runtime && ui_dom_register(dom, "runtime/7", runtime));
    status = ui_dom_contexts(dom, &a, &rows);
    REQUIRE(status.ok && rows->count == 2);
    ui_dom_context_row *first_context = rows->items[0].ptr;
    REQUIRE(!strcmp(first_context->key, "application") && first_context->selected);
    REQUIRE(ui_dom_select(dom, "runtime/7").ok);
    REQUIRE(!rml_element_is_pseudo_class_set(target, "hover"));
    REQUIRE(!ui_dom_inspect(dom, key, "attributes", 0, 64, &a, &rows, NULL).ok);
    /* The host's regular application registration must not undo user choice. */
    ui_dom_context(dom, context);
    REQUIRE(!ui_dom_select(dom, "retired/missing").ok);
    status = ui_dom_contexts(dom, &a, &rows);
    REQUIRE(status.ok && rows->count == 2);
    REQUIRE(!((ui_dom_context_row *)rows->items[0].ptr)->selected);
    REQUIRE(((ui_dom_context_row *)rows->items[1].ptr)->selected);
    /* Caller snapshots survive registry retirement and context destruction. */
    ui_dom_unregister(dom, "runtime/7");
    REQUIRE(rml_remove_context("dom-runtime-test"));
    REQUIRE(!strcmp(first_context->key, "application") && first_context->selected);
    status = ui_dom_contexts(dom, &a, &rows);
    REQUIRE(status.ok && rows->count == 1);
    REQUIRE(((ui_dom_context_row *)rows->items[0].ptr)->selected);
    status = ui_dom_query(dom, 0, 64, &a, &rows);
    REQUIRE(status.ok);
    for (int i = 0; i < rows->count; i++) {
        ui_dom_row *row = rows->items[i].ptr;
        if (!strcmp(row->id, "target")) {
            REQUIRE(strcmp(key, row->key));
            key = row->key;
            break;
        }
    }
    rml_element_remove_child(rml_element_parent(target), target);
    status = ui_dom_inspect(dom, key, "attributes", 0, 64, &a, &rows, NULL);
    REQUIRE(!status.ok && !strcmp(selected->id, "target"));
    REQUIRE(!ui_dom_unit(dom, key, "dp", "x", &pixels).ok && pixels == 0);
    ui_dom_context(dom, NULL);
    ui_dom_free(dom);
    arena_free(&a);
    rml_document_close(document);
    REQUIRE(rml_remove_context("dom-inspection-test"));
    rml_renderer_destroy(renderer);
    rml_shutdown();
    return true;
}
TEST_CASE(authored_glyphs_preserve_feather_geometry_and_text_uses_live_fonts)
{
    static const rml_system_interface system = { .elapsed_time = elapsed,
                                                 .set_mouse_cursor = cursor,
                                                 .set_clipboard_text = clipboard_set,
                                                 .get_clipboard_text = clipboard_get,
                                                 .log_message = log_message };
    static const rml_render_interface render = { .compile_geometry = compile_geometry,
                                                 .render_geometry = draw,
                                                 .release_geometry = release,
                                                 .generate_texture = texture,
                                                 .release_texture = release };
    rml_set_system_interface(&system);
    REQUIRE(rml_initialize());
    shape_glyph_register();
    vector_path_register();
    ui_font font = ui_builtin_font_at(0);
    REQUIRE(
        rml_load_font_face(font.data, font.length, "MetricTest", font.style, font.weight, false)
    );
    rml_renderer *renderer = rml_renderer_create(&render, NULL);
    rml_context *context = rml_create_context("glyph-metrics-test", 128, 128, renderer);
    rml_element *document = rml_context_load_document_from_memory(
        context,
        "<rml><head/><body style='font-family:MetricTest;font-size:12px;'>"
        "<shape-glyph id='glyph' shape='circle' fill='#80402080' "
        "style='display:block;width:10px;height:10px;'/></body></rml>",
        "glyph.rml"
    );
    REQUIRE(document);
    rml_document_show(document, false, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    REQUIRE(rml_context_update(context));
    ui_dom *dom = ui_dom_new();
    ui_dom_context(dom, context);
    float width = -1;
    REQUIRE(ui_dom_measure_text(dom, "application", "#glyph", "AV Wide", 12, 400, &width));
    REQUIRE(
        width ==
        rml_font_string_width(rml_font_face("metrictest", RML_FONT_NORMAL, 400, 12), "AV Wide")
    );
    REQUIRE(width > 0);
    REQUIRE(!ui_dom_measure_text(dom, "missing", "#glyph", "x", 12, 400, &width) && width == 0);
    REQUIRE(
        !ui_dom_measure_text(dom, "application", "#missing", "x", 12, 400, &width) &&
        width == 0
    );
    REQUIRE(!ui_dom_measure_text(dom, "application", "#glyph", "x", NAN, 400, &width));
    capture_glyph = true;
    glyph_uploads = 0;
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_uploads == 1 && glyph_vertex_count == 25 && glyph_index_count == 108);
    REQUIRE(glyph_vertices[0].position.x == 5 && glyph_vertices[0].position.y == 5);
    REQUIRE(glyph_vertices[1].position.x == 9 && glyph_vertices[2].position.x == 10);
    REQUIRE(
        glyph_vertices[0].color.r == 64 && glyph_vertices[0].color.g == 32 &&
        glyph_vertices[0].color.b == 16 && glyph_vertices[0].color.a == 128
    );
    REQUIRE(glyph_vertices[2].color.a == 0);
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_uploads == 1);
    rml_element *glyph = rml_element_query_selector(document, "#glyph");
    rml_element_set_attribute(glyph, "shape", "square");
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_vertex_count == 26 && glyph_index_count == 78);
    REQUIRE(glyph_vertices[0].position.x == 1.5f && glyph_vertices[0].position.y == 1.5f);
    rml_element_set_attribute(glyph, "shape", "arrow");
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_vertex_count == 19 && glyph_index_count == 57);
    REQUIRE(glyph_vertices[1].position.x == 9.5f && glyph_vertices[1].position.y == 5);
    rml_element_set_attribute(glyph, "shape", "circle");
    rml_element_set_attribute(glyph, "stroke", "#ff0000ff");
    rml_element_set_attribute(glyph, "stroke-width", "1.5");
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_vertex_count == 50 && glyph_index_count == 216);
    REQUIRE(glyph_vertices[0].color.r == 255 && glyph_vertices[0].color.a == 255);
    REQUIRE(glyph_vertices[26].position.x == 7.5f);
    rml_context_set_dp_ratio(context, 2);
    REQUIRE(rml_context_update(context));
    REQUIRE(ui_dom_measure_text(dom, "application", "#glyph", "AV Wide", 12, 400, &width));
    REQUIRE(
        width ==
        rml_font_string_width(rml_font_face("metrictest", RML_FONT_NORMAL, 400, 24), "AV Wide") / 2.0f
    );
    REQUIRE(rml_context_render(context));
    rml_context_set_dp_ratio(context, 1.25f);
    REQUIRE(rml_context_update(context));
    REQUIRE(ui_dom_measure_text(dom, "application", "#glyph", "AV Wide", 11, 400, &width));
    REQUIRE(
        width ==
        rml_font_string_width(rml_font_face("metrictest", RML_FONT_NORMAL, 400, 13), "AV Wide") / 1.25f
    );
    rml_context_set_dp_ratio(context, 1);
    rml_element_set_property(glyph, "display", "none");
    rml_element *vector = rml_element_create_child(rml_element_parent(glyph), "vector-path", NULL);
    rml_element_set_property(vector, "display", "block");
    rml_element_set_property(vector, "width", "30px");
    rml_element_set_property(vector, "height", "30px");
    rml_element_set_attribute(vector, "path", "M 0 5 L 20 5");
    rml_element_set_attribute(vector, "stroke", "#80402080");
    rml_element_set_attribute(vector, "stroke-width", "2");
    REQUIRE(rml_context_update(context));
    glyph_uploads = 0;
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_uploads == 1 && glyph_vertex_count == 8 && glyph_index_count == 18);
    REQUIRE(glyph_vertices[0].position.y == 6.5f && glyph_vertices[1].position.y == 5.5f);
    REQUIRE(
        glyph_vertices[0].color.a == 0 && glyph_vertices[1].color.r == 64 &&
        glyph_vertices[1].color.a == 128
    );
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_uploads == 1);
    rml_element_set_attribute(vector, "path", "M 0 0 C 0 10 20 0 20 10");
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_vertex_count == 100 && glyph_index_count == 432);
    REQUIRE((glyph_vertices[49].position.x + glyph_vertices[50].position.x) / 2 == 10);
    REQUIRE((glyph_vertices[49].position.y + glyph_vertices[50].position.y) / 2 == 5);
    rml_element_set_attribute(vector, "path", "M 0 0 C 0 0 20 10 20 10");
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_vertex_count == 8 && glyph_index_count == 18);
    rml_element_set_attribute(vector, "path", "M 0 5 L 20 5 M 0 10 L 20 10");
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_vertex_count == 16 && glyph_index_count == 36);
    int uploads = glyph_uploads;
    rml_element_set_attribute(vector, "path", "M 0 5 L 20 5 C broken");
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_uploads == uploads && rml_element_has_attribute(vector, "data-invalid"));
    char excessive_path[4096] = "M 0 0";
    size_t path_length = strlen(excessive_path);
    for (int i = 0; i < 256; i++) {
        path_length += (size_t)
            snprintf(excessive_path + path_length, sizeof excessive_path - path_length, " L 1 1");
    }
    rml_element_set_attribute(vector, "path", excessive_path);
    REQUIRE(rml_context_render(context));
    REQUIRE(glyph_uploads == uploads && rml_element_has_attribute(vector, "data-invalid"));
    rml_element_set_attribute(vector, "path", "M 0 5 L 20 5");
    rml_context_set_dp_ratio(context, 2);
    REQUIRE(rml_context_update(context));
    REQUIRE(rml_context_render(context));
    REQUIRE(!rml_element_has_attribute(vector, "data-invalid"));
    REQUIRE(glyph_vertex_count == 8 && glyph_vertices[4].position.x == 40);
    REQUIRE(glyph_vertices[0].position.y == 12.5f && glyph_vertices[1].position.y == 11.5f);
    ui_dom_unregister(dom, "application");
    REQUIRE(
        !ui_dom_measure_text(dom, "application", "#glyph", "x", 12, 400, &width) &&
        width == 0
    );
    ui_dom_free(dom);
    rml_document_close(document);
    REQUIRE(rml_remove_context("glyph-metrics-test"));
    rml_renderer_destroy(renderer);
    rml_shutdown();
    capture_glyph = false;
    return true;
}
void test_tool_ui_dom()
{
    RUN_TEST_CASE(authored_glyphs_preserve_feather_geometry_and_text_uses_live_fonts);
    RUN_TEST_CASE(dom_queries_report_missing_context_and_bounded_pages);
    RUN_TEST_CASE(dom_snapshots_copy_geometry_origins_and_survive_removal);
}
