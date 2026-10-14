#include "ui/controls/code_control.h"

#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

TEST_CASE(code_selection_maps_unicode_characters_to_document_bytes)
{
    const char *text = "a\xc3\xa9\xf0\x9f\x8e\xae\nZ";
    REQUIRE(code_text_byte_offset(text, -1) == 0);
    REQUIRE(code_text_byte_offset(text, 1) == 1);
    REQUIRE(code_text_byte_offset(text, 2) == 3);
    REQUIRE(code_text_byte_offset(text, 3) == 7);
    REQUIRE(code_text_byte_offset(text, 99) == 9);
    REQUIRE(code_text_character_offset(text, 2) == 1);
    REQUIRE(code_text_character_offset(text, 6) == 2);
    REQUIRE(code_text_character_offset(text, 7) == 3);
    REQUIRE(code_text_character_offset(text, 99) == 5);
    REQUIRE(code_text_character_offset(text, -10) == 0);
    int line, column;
    code_text_position(text, 7, &line, &column);
    REQUIRE(line == 1 && column == 4);
    code_text_position(text, 8, &line, &column);
    REQUIRE(line == 2 && column == 1);
    code_text_position("a\r\nz", 2, &line, &column);
    REQUIRE(line == 1 && column == 2);
    return true;
}

TEST_CASE(code_edits_never_split_a_multibyte_glyph)
{
    const char *text = "a\xc3\xa9\xf0\x9f\x8e\xae\nZ";
    char *value = code_text_replace(text, 2, 6, "x");
    bool replaced = !strcmp(value, "ax\xf0\x9f\x8e\xae\nZ");
    free(value);
    REQUIRE(replaced);
    value = code_text_replace(text, 7, 1, "\t");
    replaced = !strcmp(value, "a\t\nZ");
    free(value);
    REQUIRE(replaced);
    value = code_text_replace(text, -1, 999, "");
    replaced = !strcmp(value, "");
    free(value);
    REQUIRE(replaced);
    return true;
}

TEST_CASE(code_history_and_navigation_commands_leave_the_view)
{
    REQUIRE(!strcmp(code_text_command(RML_KEY_Z, RML_KM_META, true), "undo"));
    REQUIRE(!strcmp(code_text_command(RML_KEY_Z, RML_KM_META | RML_KM_SHIFT, true), "redo"));
    REQUIRE(!strcmp(code_text_command(RML_KEY_Z, RML_KM_CTRL, false), "undo"));
    REQUIRE(!strcmp(code_text_command(RML_KEY_Y, RML_KM_CTRL, false), "redo"));
    REQUIRE(code_text_command(RML_KEY_Z, RML_KM_CTRL, true) == NULL);
    REQUIRE(code_text_command(RML_KEY_Z, RML_KM_META | RML_KM_ALT, true) == NULL);
    REQUIRE(code_text_command(RML_KEY_C, RML_KM_META, true) == NULL);
    REQUIRE(code_text_command(RML_KEY_V, RML_KM_CTRL, false) == NULL);
    REQUIRE(!strcmp(code_text_command(RML_KEY_F12, 0, false), "definition"));
    REQUIRE(!strcmp(code_text_command(RML_KEY_F12, RML_KM_SHIFT, false), "references"));
    REQUIRE(!strcmp(code_text_command(RML_KEY_SPACE, RML_KM_CTRL, true), "suggest"));
    REQUIRE(!strcmp(code_text_command(RML_KEY_ESCAPE, 0, false), "cancel"));
    return true;
}

#ifdef CHIBI_TEST_CODE_UI
#include "render/ui.h"
#include "runtime/ui/builtin_fonts.h"

#include <assert.h>
#include <stdio.h>
#include <yyjson.h>

static rml_context *context;
static rml_renderer *renderer;
static rml_element *document, *editor, *input;
static rml_listener *input_listener, *command_listener, *raw_key_listener,
    *raw_text_listener;
static int edits, commands;
static char last_value[256];
static char clipboard[256];

static double elapsed()
{
    return 1.0;
}

static void cursor(const char *) {}
static bool log_message(rml_log_type, const char *)
{
    return true;
}
static void clipboard_set(const char *value)
{
    snprintf(clipboard, sizeof clipboard, "%s", value);
}
static void clipboard_get(rml_string *value)
{
    rml_string_set(value, clipboard);
}
static uintptr_t compile(void *, rml_mesh)
{
    return 1;
}
static void draw(void *, uintptr_t, rml_vec2, uintptr_t) {}
static void release_geometry(void *, uintptr_t) {}
static uintptr_t texture(void *, const uint8_t *, int, int)
{
    return 1;
}
static void release_texture(void *, uintptr_t) {}

static void input_event(void *, const rml_event *event)
{
    edits++;
    rml_event_value(event, last_value, sizeof last_value);
    /* The application accepts the edit and reflects its document snapshot. */
    rml_element_set_attribute(editor, "value", last_value);
}
static void command_event(void *, const rml_event *)
{
    commands++;
}

static void ui_setup()
{
    static const rml_system_interface system = { .elapsed_time = elapsed,
                                                 .set_mouse_cursor = cursor,
                                                 .set_clipboard_text = clipboard_set,
                                                 .get_clipboard_text = clipboard_get,
                                                 .log_message = log_message };
    static const rml_render_interface render = { .compile_geometry = compile,
                                                 .render_geometry = draw,
                                                 .release_geometry = release_geometry,
                                                 .generate_texture = texture,
                                                 .release_texture = release_texture };
    raw_key_listener = raw_text_listener = NULL;
    edits = commands = 0;
    last_value[0] = clipboard[0] = 0;
    rml_set_system_interface(&system);
    assert(rml_initialize());
    code_control_register();
    ui_font font = ui_builtin_font_at(0);
    assert(rml_load_font_face(font.data, font.length, "CodeTest", font.style, font.weight, true));
    renderer = rml_renderer_create(&render, NULL);
    context = rml_create_context("code-control-test", 640, 480, renderer);
    assert(context);
    document = rml_context_load_document_from_memory(
        context,
        "<rml><head><style>body{font-family:CodeTest;font-size:16px;}"
        "code-editor{display:block;width:400px;height:240px;}"
        "textarea{font-family:CodeTest;font-size:16px;line-height:20px;caret-"
        "color:white;}"
        "</style></head><body><code-editor id=\"code\" value=\"aé🎮\"/>"
        "</body></rml>",
        "code-control-test.rml"
    );
    assert(document);
    rml_document_show(document, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    rml_context_update(context);
    editor = rml_element_query_selector(document, "#code");
    input = rml_element_query_selector(editor, "textarea");
    assert(input);
    input_listener = rml_listener_create(input_event, NULL);
    command_listener = rml_listener_create(command_event, NULL);
    rml_element_add_named_listener(editor, "code-input", input_listener, false);
    rml_element_add_named_listener(editor, "code-command", command_listener, false);
    assert(rml_element_focus(input, true));
    rml_context_update(context);
}

static void ui_teardown()
{
    /* Listener destruction detaches named listeners through RmlUi's observer
     * list; destroying the document first avoids retaining any DOM pointer. */
    rml_document_close(document);
    rml_context_update(context);
    rml_remove_context("code-control-test");
    rml_listener_destroy(input_listener);
    rml_listener_destroy(command_listener);
    if (raw_key_listener) {
        rml_listener_destroy(raw_key_listener);
    }
    if (raw_text_listener) {
        rml_listener_destroy(raw_text_listener);
    }
    rml_release_render_managers();
    rml_renderer_destroy(renderer);
    rml_shutdown();
}

static bool input_is(const char *expected)
{
    char value[256];
    rml_element_value(input, value, sizeof value);
    return !strcmp(value, expected);
}

TEST_CASE(code_native_input_preserves_unicode_and_emits_edits)
{
    rml_element_set_selection_range(input, 1, 2);
    rml_context_process_text_input(context, "界", strlen("界"));
    rml_context_update(context);
    REQUIRE(edits == 1 && input_is("a界🎮"));
    REQUIRE(!strcmp(last_value, "a界🎮"));
    rml_context_process_key_down(context, RML_KEY_TAB, 0);
    rml_context_update(context);
    REQUIRE(edits == 2 && input_is("a界\t🎮"));
    rml_context_process_key_down(
        context,
        RML_KEY_Z,
#ifdef __APPLE__
        RML_KM_META
#else
        RML_KM_CTRL
#endif
    );
    REQUIRE(commands == 1 && edits == 2 && input_is("a界\t🎮"));
    rml_element_set_attribute(editor, "completion-open", "true");
    rml_context_process_key_down(context, RML_KEY_DOWN, 0);
    rml_context_process_key_down(context, RML_KEY_TAB, 0);
    REQUIRE(commands == 3 && edits == 2 && input_is("a界\t🎮"));
    return true;
}

static int delegated_keys, delegated_text;
static void delegated_key(void *, const rml_event *)
{
    delegated_keys++;
}
static void delegated_glyph(void *, const rml_event *)
{
    delegated_text++;
}
TEST_CASE(code_native_delegated_keyboard_reports_input_without_editing)
{
    delegated_keys = delegated_text = 0;
    raw_key_listener = rml_listener_create(delegated_key, NULL);
    raw_text_listener = rml_listener_create(delegated_glyph, NULL);
    rml_element_add_named_listener(editor, "code-key", raw_key_listener, false);
    rml_element_add_named_listener(editor, "code-text", raw_text_listener, false);
    rml_element_set_attribute(editor, "keyboard-mode", "delegated");
    rml_context_process_key_down(context, RML_KEY_L, 0);
    rml_context_process_text_input(context, "界", strlen("界"));
    rml_context_process_key_down(context, RML_KEY_DELETE, 0);
    rml_context_update(context);
    bool delegated = delegated_keys == 2 && delegated_text == 1 && edits == 0 &&
                     input_is("aé🎮");
    rml_element_set_selection_range(input, 1, 3);
    rml_element_blur(input);
    rml_context_update(context);
    rml_element_set_attribute(editor, "request-command", "copy");
    rml_element_set_attribute(editor, "request-sequence", "1");
    rml_context_update(context);
    bool requested = !strcmp(clipboard, "é🎮") && delegated_keys == 2;
    rml_element_set_attribute(editor, "keyboard-mode", "native");
    rml_context_process_text_input(context, "x", 1);
    rml_context_update(context);
    bool native = edits == 1 && delegated_text == 1;
    REQUIRE(delegated && native && requested);
    return true;
}

static ui_activation ingress_activation;
static bool ingress_captured, ingress_delivered;
static char ingress_expected[512];
static void ingress_emit(void *, const rml_event *, const char *name, int argc, const char *const *args)
{
    snprintf(ingress_activation.event, sizeof ingress_activation.event, "%s", name);
    ingress_captured = ui_activation_capture(&ingress_activation, argc, args, NULL);
}
static int ingress_fields(void *, const char *, render_ui_event_field *fields, int capacity)
{
    if (capacity < 7) {
        return -1;
    }
    const char *names[] = { "Key",  "Control", "Shift", "Alt",
                            "Meta", "Caret",   "Buffer" };
    for (int i = 0; i < 7; i++) {
        fields[i] = (render_ui_event_field){ names[i],
                                             { .type = i == 0 || i == 6 ? PIN_STR
                                                                        : PIN_I32 } };
    }
    return 7;
}
static int ingress_receive(void *, uint64_t, const char *name, execution_arguments args)
{
    ingress_delivered = !strcmp(name, "SourceKey") && args.count == 7 &&
                        !strcmp(args.values[0].str, "L") && args.values[1].i == 1 &&
                        args.values[2].i == 1 && args.values[3].i == 1 &&
                        args.values[4].i == 1 && args.values[5].i == 3 &&
                        !strcmp(args.values[6].str, ingress_expected);
    return 0;
}
TEST_CASE(code_native_seven_field_authored_key_ingress_owns_unicode_buffer)
{
    ingress_captured = ingress_delivered = false;
    memset(&ingress_activation, 0, sizeof ingress_activation);
    strcpy(ingress_expected, "aé🎮");
    memset(ingress_expected + strlen(ingress_expected), 'x', 400);
    ingress_expected[407] = 0;
    yyjson_mut_doc *json = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(json, yyjson_mut_obj(json));
    rml_data_model *model = rml_data_model_create(context, "code-ingress", json, ingress_emit, NULL);
    assert(model);
    rml_element *probe = rml_context_load_document_from_memory(
        context,
        "<rml><head><style>body{font-family:CodeTest;font-size:16px;}code-"
        "editor{display:block;width:400px;height:120px;}textarea{font-family:"
        "CodeTest;font-size:16px;}</style></head>"
        "<body data-model='code-ingress'><code-editor id='probe' "
        "keyboard-mode='delegated' "
        "data-event-code-key=\"emit('SourceKey',ev.key,ev.control,ev.shift,ev."
        "alt,ev.meta,ev.caret,ev.buffer)\"/></body></rml>",
        "code-ingress.rml"
    );
    assert(probe);
    rml_document_show(probe, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    rml_element *control = rml_element_query_selector(probe, "#probe");
    rml_element_set_attribute(control, "value", ingress_expected);
    rml_context_update(context);
    rml_element *field = rml_element_query_selector(control, "textarea");
    assert(field && rml_element_focus(field, true));
    rml_element_set_selection_range(field, 2, 2);
    rml_context_process_key_down(context, RML_KEY_L, RML_KM_CTRL | RML_KM_SHIFT | RML_KM_ALT | RML_KM_META);
    rml_element_set_attribute(control, "value", "changed after event");
    rml_context_update(context);
    render_ui_host host = { .payload = ingress_fields, .dispatch = ingress_receive };
    bool sent = ingress_captured &&
                ui_activation_send(&host, ecs_entity_from_u64(17), &ingress_activation, RML_KM_CTRL);
    ui_activation_clear(&ingress_activation);
    rml_document_close(probe);
    rml_context_update(context);
    rml_data_model_destroy(model);
    yyjson_mut_doc_free(json);
    REQUIRE(ingress_captured && sent && ingress_delivered);
    return true;
}

/* These exercise RmlUi's reference-address binding, not code-editor policy. */
static bool indexed_attribute(rml_element *element, const char *expected)
{
    char value[128];
    rml_element_attribute(element, "title", value, sizeof value);
    return !strcmp(value, expected);
}
TEST_CASE(code_ui_indexed_aliases_follow_reorder_nested_collections_and_writes)
{
    const char *source = "{\"self\":{\"slots\":[1,0],\"rows\":[{\"name\":"
                         "\"Alpha\",\"items\":[10,11]},{\"name\":\"Beta\","
                         "\"items\":[20]}]},\"chosen\":0}";
    yyjson_doc *parsed = yyjson_read(source, strlen(source), 0);
    assert(parsed);
    yyjson_mut_doc *json = yyjson_doc_mut_copy(parsed, NULL);
    yyjson_doc_free(parsed);
    rml_data_model *model = rml_data_model_create(context, "indexed-references", json, NULL, NULL);
    assert(model);
    rml_element *probe = rml_context_load_document_from_memory(
        context,
        "<rml><body data-model='indexed-references'><div id='rows'>"
        "<div class='row' data-for='slot : self.slots' "
        "data-alias-row='self.rows[slot]'>"
        "<input class='edit' data-value='row.name'/><div data-alias-item='row'>"
        "<span class='name' data-attr-title='item.name'/><b class='nested' "
        "data-for='value : item.items' "
        "data-attr-title='value'/></div></div></div>"
        "<div id='chosen'><i class='choice' data-for='value : "
        "self.rows[chosen].items' data-attr-title='value'/></div>"
        "<span id='literal' data-alias-row='self.rows[0]' "
        "data-attr-title='row.name'/>"
        "<span id='deep' data-alias-row='self.rows[self.slots[0]]' "
        "data-attr-title='row.name'/></body></rml>",
        "indexed-references.rml"
    );
    assert(probe);
    rml_document_show(probe, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    rml_context_update(context);
    rml_element *names[4], *rows[4], *choices[4];
    REQUIRE(rml_element_query_selector_all(probe, ".name", names, 4) == 2);
    REQUIRE(indexed_attribute(names[0], "Beta") && indexed_attribute(names[1], "Alpha"));
    REQUIRE(indexed_attribute(rml_element_query_selector(probe, "#literal"), "Alpha"));
    REQUIRE(indexed_attribute(rml_element_query_selector(probe, "#deep"), "Beta"));
    REQUIRE(rml_element_query_selector_all(probe, ".nested[title]", choices, 4) == 3);
    REQUIRE(rml_element_query_selector_all(probe, ".choice[title]", choices, 4) == 2);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(json);
    yyjson_mut_val *self = yyjson_mut_obj_get(root, "self");
    yyjson_mut_val *slots = yyjson_mut_obj_get(self, "slots");
    yyjson_mut_set_int(yyjson_mut_arr_get(slots, 0), 0);
    yyjson_mut_set_int(yyjson_mut_arr_get(slots, 1), 1);
    rml_data_model_dirty(model, "self");
    rml_context_update(context);
    REQUIRE(indexed_attribute(names[0], "Alpha") && indexed_attribute(names[1], "Beta"));
    REQUIRE(indexed_attribute(rml_element_query_selector(probe, "#deep"), "Alpha"));
    REQUIRE(rml_element_query_selector_all(probe, ".edit", rows, 4) == 2);
    REQUIRE(rml_element_focus(rows[0], true));
    rml_element_set_selection_range(rows[0], 0, 5);
    rml_context_process_text_input(context, "Edited", 6);
    rml_context_update(context);
    yyjson_mut_val *source_rows = yyjson_mut_obj_get(self, "rows");
    REQUIRE(
        !strcmp(yyjson_mut_get_str(yyjson_mut_obj_get(yyjson_mut_arr_get(source_rows, 0), "name")), "Edited")
    );
    REQUIRE(
        !strcmp(yyjson_mut_get_str(yyjson_mut_obj_get(yyjson_mut_arr_get(source_rows, 1), "name")), "Beta")
    );
    yyjson_mut_val *chosen = yyjson_mut_obj_get(root, "chosen");
    yyjson_mut_set_int(chosen, 1);
    rml_data_model_dirty(model, "chosen");
    rml_context_update(context);
    REQUIRE(rml_element_query_selector_all(probe, ".choice[title]", choices, 4) == 1);
    REQUIRE(indexed_attribute(choices[0], "20"));
    const double invalid[] = { -1, 0.5, 99 };
    for (int i = 0; i < 3; i++) {
        yyjson_mut_set_real(chosen, invalid[i]);
        rml_data_model_dirty(model, "chosen");
        rml_context_update(context);
        REQUIRE(rml_element_query_selector_all(probe, ".choice[title]", choices, 4) == 0);
    }
    yyjson_mut_set_int(chosen, 0);
    rml_data_model_dirty(model, "chosen");
    rml_context_update(context);
    REQUIRE(rml_element_query_selector_all(probe, ".choice[title]", choices, 4) == 2);
    yyjson_mut_arr_remove(slots, 1);
    rml_data_model_dirty(model, "self");
    rml_context_update(context);
    REQUIRE(rml_element_query_selector_all(probe, ".name", names, 4) == 1);
    rml_document_close(probe);
    rml_context_update(context);
    rml_data_model_dirty_all(model);
    rml_context_update(context);
    rml_data_model_destroy(model);
    yyjson_mut_doc_free(json);
    return true;
}

TEST_CASE(code_native_readonly_blocks_input_but_keeps_selection)
{
    rml_element_set_attribute(editor, "readonly", "");
    rml_context_update(context);
    rml_element_set_selection_range(input, 1, 3);
    rml_context_process_text_input(context, "x", 1);
    rml_context_process_key_down(context, RML_KEY_BACK, 0);
    rml_context_process_key_down(context, RML_KEY_TAB, 0);
    rml_context_update(context);
    REQUIRE(edits == 0 && input_is("aé🎮"));
    int start, end;
    rml_element_selection_range(input, &start, &end);
    REQUIRE(start == 1 && end == 3);
    return true;
}

TEST_CASE(code_native_platform_clipboard_uses_the_primary_modifier)
{
    rml_element_set_selection_range(input, 1, 3);
#ifdef __APPLE__
    unsigned primary = RML_KM_META;
#else
    unsigned primary = RML_KM_CTRL;
#endif
    rml_context_process_key_down(context, RML_KEY_C, primary);
    REQUIRE(!strcmp(clipboard, "é🎮"));
    rml_context_process_key_down(context, RML_KEY_X, primary);
    rml_context_update(context);
    REQUIRE(edits == 1 && input_is("a"));
    rml_context_process_key_down(context, RML_KEY_V, primary);
    rml_context_update(context);
    REQUIRE(edits == 2 && input_is("aé🎮"));
    return true;
}

TEST_CASE(code_native_menu_clipboard_retains_the_selection_after_blur)
{
    rml_element_set_selection_range(input, 1, 3);
    rml_element_blur(input);
    rml_context_update(context);
    rml_element_set_attribute(editor, "request-command", "copy");
    rml_element_set_attribute(editor, "request-sequence", "1");
    rml_context_update(context);
    REQUIRE(!strcmp(clipboard, "é🎮"));
    rml_element_set_attribute(editor, "request-command", "cut");
    rml_element_set_attribute(editor, "request-sequence", "2");
    rml_context_update(context);
    REQUIRE(edits == 1 && input_is("a"));
    return true;
}

TEST_CASE(code_native_backwards_selection_retains_the_active_caret)
{
    rml_element_set_selection_range(input, 3, 2);
    REQUIRE(rml_element_textarea_caret(input) == 3);
    rml_element_set_selection_range(input, 3, 1);
    int start, end;
    rml_element_selection_range(input, &start, &end);
    REQUIRE(start == 1 && end == 3);
    REQUIRE(rml_element_textarea_caret(input) == 1);
    rml_element_blur(input);
    rml_context_update(context);
    rml_element_set_attribute(editor, "request-command", "copy");
    rml_element_set_attribute(editor, "request-sequence", "1");
    rml_context_update(context);
    REQUIRE(!strcmp(clipboard, "é🎮"));
    REQUIRE(rml_element_textarea_caret(input) == 1);
    rml_element_set_attribute(editor, "selection-start", "1");
    rml_element_set_attribute(editor, "selection-end", "7");
    rml_element_set_attribute(editor, "selection-caret", "1");
    rml_context_update(context);
    REQUIRE(rml_element_textarea_caret(input) == 1);
    return true;
}

TEST_CASE(code_native_highlights_treat_source_as_text_and_reject_split_glyphs)
{
    rml_element_set_attribute(editor, "value", "<tag>é\r\n\t");
    rml_element_set_attribute(editor, "highlights", "[{\"start\":0,\"end\":5,\"classes\":\"tok-tag\"}]");
    rml_context_update(context);
    REQUIRE(input_is("<tag>é\r\n\t"));
    rml_element *span = rml_element_query_selector(editor, ".code-highlight .tok-tag");
    REQUIRE(span && !rml_element_query_selector(editor, "tag"));
    rml_element_set_attribute(editor, "highlights", "[{\"start\":0,\"end\":6,\"classes\":\"tok-tag\"}]");
    rml_context_update(context);
    REQUIRE(rml_element_has_attribute(editor, "data-highlight-invalid"));
    char color[64];
    rml_element_property(input, "color", color, sizeof color);
    REQUIRE(strcmp(color, "transparent") != 0);
    rml_element_set_attribute(editor, "highlights", "{bad json");
    rml_context_update(context);
    REQUIRE(rml_element_has_attribute(editor, "data-highlight-invalid"));
    rml_element_remove_attribute(editor, "highlights");
    rml_context_update(context);
    REQUIRE(!rml_element_has_attribute(editor, "data-highlight-invalid"));
    return true;
}

TEST_CASE(code_native_removal_releases_listeners_before_replacing_the_view)
{
    rml_element *parent = rml_element_parent(editor);
    REQUIRE(parent);
    for (int i = 0; i < 8; i++) {
        rml_element_remove_child(parent, editor);
        editor = input = NULL;
        rml_context_update(context);
        REQUIRE(!rml_element_query_selector(document, "#code"));
        rml_element_set_inner_rml(parent, "<code-editor id=\"code\" value=\"a\"/>");
        rml_context_update(context);
        editor = rml_element_query_selector(document, "#code");
        input = rml_element_query_selector(editor, "textarea");
        REQUIRE(editor && input);
        rml_element_add_named_listener(editor, "code-input", input_listener, false);
        rml_element_add_named_listener(editor, "code-command", command_listener, false);
        REQUIRE(rml_element_focus(input, true));
        rml_element_set_selection_range(input, 1, 1);
        rml_context_process_text_input(context, "é", strlen("é"));
        rml_context_update(context);
        REQUIRE(edits == i + 1 && input_is("aé"));
    }
    return true;
}
#endif

void test_code_control()
{
    RUN_TEST_CASE(code_selection_maps_unicode_characters_to_document_bytes);
    RUN_TEST_CASE(code_edits_never_split_a_multibyte_glyph);
    RUN_TEST_CASE(code_history_and_navigation_commands_leave_the_view);
#ifdef CHIBI_TEST_CODE_UI
    pu_setup(ui_setup, ui_teardown);
    RUN_TEST_CASE(code_native_input_preserves_unicode_and_emits_edits);
    RUN_TEST_CASE(code_native_seven_field_authored_key_ingress_owns_unicode_buffer);
    RUN_TEST_CASE(code_ui_indexed_aliases_follow_reorder_nested_collections_and_writes);
    RUN_TEST_CASE(code_native_readonly_blocks_input_but_keeps_selection);
    RUN_TEST_CASE(code_native_delegated_keyboard_reports_input_without_editing);
    RUN_TEST_CASE(code_native_platform_clipboard_uses_the_primary_modifier);
    RUN_TEST_CASE(code_native_menu_clipboard_retains_the_selection_after_blur);
    RUN_TEST_CASE(code_native_backwards_selection_retains_the_active_caret);
    RUN_TEST_CASE(code_native_highlights_treat_source_as_text_and_reject_split_glyphs);
    RUN_TEST_CASE(code_native_removal_releases_listeners_before_replacing_the_view);
    pu_clear_setup();
#endif
}
