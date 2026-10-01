/* Studio graph values are edited through the controls in their node rows. */
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "runtime/generated.h"
#include "test/suite_internal.h"
#include "ui/rml.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROJECT "test/fixtures/studio"
#define FRAME_SECONDS (1.0 / 60.0)

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    engine_viewport *viewport;
    app_scene_authoring *scene;
    studio *studio;
    double time;
    bool scene_frame;
} graph_input_fixture;

static graph_input_fixture fixture_open(void)
{
    graph_input_fixture f = { .world = ecs_world_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    f.live = game_live_world_new(f.world);
    game_live_definition *definition = game_live_definition_new("Editing", behavior_asset_new("Editing"));
    assert(definition && game_live_add_definition(f.live, definition) == 0);
    f.graph = editor_new(behavior_asset_new("Editing"));
    f.runtime = engine_new(f.world, f.live);
    f.viewport = engine_viewport_new(640, 360);
    f.scene = app_scene_authoring_new(
        f.world,
        f.live,
        definition,
        editor_behavior_asset(f.graph),
        f.graph,
        NULL,
        NULL,
        f.runtime
    );
    char content[PATH_MAX];
    snprintf(content, sizeof content, "%s/content", PROJECT);
    assert(app_scene_authoring_set_content_root(f.scene, content) == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    assert(app_scene_authoring_load(f.scene, PROJECT "/content/scenes/Main.kdl") == 0);
    assert(app_scene_authoring_open_behavior(f.scene, "Teller") == 0);
    f.studio = studio_new(f.scene, f.viewport, PROJECT);
    f.scene_frame = true;
    studio_maximize_graph(f.studio);
    return f;
}

static void fixture_free(graph_input_fixture *f)
{
    studio_free(f->studio);
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    engine_viewport_free(f->viewport);
}

static void frame(graph_input_fixture *f)
{
    f->time += FRAME_SECONDS;
    cf_app_update(NULL);
    app_input_frame();
    editor_tick(f->graph, f->time);
    if (f->scene_frame) {
        assert(app_scene_authoring_frame(f->scene, f->time) == FLOW_OK);
    }
    studio_frame(f->studio, FRAME_SECONDS);
    cf_app_draw_onto_screen(true);
}

static void frames(graph_input_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        frame(f);
    }
}

static void capture_stage(graph_input_fixture *f, const char *name)
{
    const char *directory = getenv("CHIBI_GRAPH_EDIT_CAPTURE_DIR");
    if (!directory || !*directory) {
        return;
    }
    frames(f, 12);
    char path[PATH_MAX];
    int length = snprintf(path, sizeof path, "%s/%s.png", directory, name);
    assert(length > 0 && (size_t)length < sizeof path);
    assert(app_save_screenshot(path) == 0);
}

static void key_event(SDL_Scancode key, bool down)
{
    SDL_Event event = { .type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP };
    event.key.windowID = SDL_GetWindowID(cf_app_get_window());
    event.key.scancode = key;
    event.key.key = SDL_GetKeyFromScancode(key, SDL_KMOD_NONE, true);
    event.key.down = down;
    assert(SDL_PushEvent(&event));
}

static void key_event_mod(SDL_Scancode key, bool down, SDL_Keymod mod)
{
    SDL_Event event = { .type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP };
    event.key.windowID = SDL_GetWindowID(cf_app_get_window());
    event.key.scancode = key;
    event.key.key = SDL_GetKeyFromScancode(key, mod, true);
    event.key.mod = mod;
    event.key.down = down;
    assert(SDL_PushEvent(&event));
}

static void text_event(const char *text)
{
    SDL_Event event = { .type = SDL_EVENT_TEXT_INPUT };
    event.text.windowID = SDL_GetWindowID(cf_app_get_window());
    event.text.text = text;
    assert(SDL_PushEvent(&event));
}

static float screen_x(const graph_input_fixture *f, float graph_x)
{
    return f->studio->graph.surface.rect.x + graph_x * f->studio->graph.in.zoom;
}

static float screen_y(const graph_input_fixture *f, float graph_y)
{
    return f->studio->graph.surface.rect.y + graph_y * f->studio->graph.in.zoom;
}

static void mouse_move(const graph_input_fixture *f, float x, float y)
{
    SDL_Event event = { .type = SDL_EVENT_MOUSE_MOTION };
    event.motion.windowID = SDL_GetWindowID(cf_app_get_window());
    event.motion.x = x * f->studio->prefs.ui_scale;
    event.motion.y = y * f->studio->prefs.ui_scale;
    assert(SDL_PushEvent(&event));
}

static void click_graph_position(graph_input_fixture *f, float graph_x, float graph_y)
{
    float x = screen_x(f, graph_x);
    float y = screen_y(f, graph_y);
    mouse_move(f, x, y);
    for (int down = 1; down >= 0; down--) {
        SDL_Event event = { .type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                         : SDL_EVENT_MOUSE_BUTTON_UP };
        event.button.windowID = SDL_GetWindowID(cf_app_get_window());
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = down;
        event.button.clicks = 1;
        event.button.x = x * f->studio->prefs.ui_scale;
        event.button.y = y * f->studio->prefs.ui_scale;
        assert(SDL_PushEvent(&event));
    }
    frames(f, 2);
}

static void click_rml_element(graph_input_fixture *f, rml_element *element)
{
    rml_vec2 at = rml_element_absolute_offset(element, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(element, RML_BOX_BORDER);
    float ratio = rml_context_dp_ratio(rml_backend_context(f->studio->graph.backend));
    click_graph_position(f, (at.x + size.x * 0.5f) / ratio, (at.y + size.y * 0.5f) / ratio);
}

static bool click_path(graph_input_fixture *f, const char *path)
{
    kit_rect rect;
    if (!kit_find(f->studio->graph.kit, path, &rect)) {
        return false;
    }
    click_graph_position(f, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f);
    return true;
}

static rml_element *graph_query(graph_input_fixture *f, const char *selector)
{
    rml_context *context = rml_backend_context(f->studio->graph.backend);
    rml_element *document = rml_context_document(context, 0);
    return rml_element_query_selector(document, selector);
}

static int autocomplete_labels(graph_input_fixture *f, char labels[][128], int capacity)
{
    rml_element *popup = graph_query(f, ".popup.autocomplete:not([data-ending-style])");
    if (!popup || !rml_element_is_visible(popup, true) ||
        rml_element_has_attribute(popup, "data-ending-style")) {
        return 0;
    }
    rml_element *results = rml_element_query_selector(popup, ".autocomplete-results");
    if (!results) {
        return 0;
    }
    rml_element *items[32];
    int count = rml_element_query_selector_all(results, ".menu-item .label", items, 32);
    count = count < capacity ? count : capacity;
    for (int i = 0; i < count; i++) {
        char rml[256] = "";
        rml_element_inner_rml(items[i], rml, sizeof rml);
        int n = 0;
        bool tag = false;
        for (const char *c = rml; *c && n + 1 < 128; c++) {
            tag = *c == '<' || (tag && *c != '>');
            if (!tag && *c != '>') {
                labels[i][n++] = *c;
            }
        }
        labels[i][n] = '\0';
    }
    return count;
}

static bool autocomplete_has_label(graph_input_fixture *f, const char *expected)
{
    char labels[32][128] = { { 0 } };
    int count = autocomplete_labels(f, labels, 32);
    for (int i = 0; i < count; i++) {
        if (!strcmp(labels[i], expected)) {
            return true;
        }
    }
    return false;
}

static void wheel_at(graph_input_fixture *f, float graph_x, float graph_y, float direction)
{
    float x = screen_x(f, graph_x);
    float y = screen_y(f, graph_y);
    mouse_move(f, x, y);
    frame(f);
    SDL_Event event = { .type = SDL_EVENT_MOUSE_WHEEL };
    event.wheel.windowID = SDL_GetWindowID(cf_app_get_window());
    event.wheel.mouse_x = x * f->studio->prefs.ui_scale;
    event.wheel.mouse_y = y * f->studio->prefs.ui_scale;
    event.wheel.y = direction;
    assert(SDL_PushEvent(&event));
    frame(f);
}

static void enter(graph_input_fixture *f)
{
    key_event(SDL_SCANCODE_RETURN, true);
    key_event(SDL_SCANCODE_RETURN, false);
    frames(f, 2);
}

static void replace_focused_number(graph_input_fixture *f, const char *value)
{
    SDL_Keymod primary = f->studio->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    key_event_mod(SDL_SCANCODE_A, true, primary);
    key_event_mod(SDL_SCANCODE_A, false, primary);
    text_event(value);
    key_event(SDL_SCANCODE_RETURN, true);
    key_event(SDL_SCANCODE_RETURN, false);
    frames(f, 2);
}

static void replace_focused_text(graph_input_fixture *f, const char *value)
{
    SDL_Keymod primary = f->studio->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    key_event_mod(SDL_SCANCODE_A, true, primary);
    key_event_mod(SDL_SCANCODE_A, false, primary);
    text_event(value);
    frames(f, 2);
}

static const pin_binding *input_binding(const graph_input_fixture *f, uint64_t id, int pin)
{
    const graph *gr = editor_active_graph(f->graph);
    const node *n = graph_node_by_id(gr, id);
    return n ? graph_pin_binding(gr, n, 0, pin) : NULL;
}

static void bound_input_can_be_changed_and_edited()
{
    graph_input_fixture f = fixture_open();
    frames(&f, 3);
    node *vector = editor_spawn(f.graph, registry_node_by_name("Shader Vector2"), 100, 100);
    assert(vector && vector->input_count == 2 && vector->input_pins[0].type == PIN_F32);
    uint64_t vector_id = vector->id;
    graph *gr = f.graph->active_graph;
    pin_binding aim_x = { .kind = PIN_BINDING_COMPONENT_FIELD,
                          .component_key = "GAME.GameAim",
                          .field_name = "direction.x" };
    assert(graph_set_pin_binding(gr, vector, 0, 0, &aim_x) == 0);
    frames(&f, 3);

    char input_path[128], autocomplete_path[200], results_path[240];
    snprintf(input_path, sizeof input_path, "world/stage/n%llu/b/i0/source-edit", (unsigned long long)vector_id);
    snprintf(autocomplete_path, sizeof autocomplete_path, "layer/@autocomplete:%s", input_path);
    snprintf(results_path, sizeof results_path, "%s/results", autocomplete_path);
    rml_element *field = graph_query(&f, ".field.autocomplete input.value");
    bool field_visible = field && click_path(&f, input_path) &&
                         kit_wants_text(f.studio->graph.kit, NULL);
    if (field_visible) {
        replace_focused_text(&f, "Aim.");
    }
    char labels[32][128] = { { 0 } };
    int aim_count = field_visible ? autocomplete_labels(&f, labels, 32) : 0;
    bool aim_suggested = false;
    for (int i = 0; i < aim_count; i++) {
        aim_suggested |= strstr(labels[i], "Direction") != NULL;
    }
    char typed_query[128] = { 0 };
    bool aim_query = field &&
                     rml_element_value(field, typed_query, sizeof typed_query) > 0 &&
                     !strcmp(typed_query, "Aim.");
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 1);
    const pin_binding *after_escape = input_binding(&f, vector_id, 0);
    bool escape_preserved = field_visible && after_escape &&
                            after_escape->field_name &&
                            !strcmp(after_escape->field_name, "direction.x") &&
                            !kit_has_popup(f.studio->graph.kit);

    bool reopened = click_path(&f, input_path) &&
                    kit_wants_text(f.studio->graph.kit, NULL);
    if (reopened) {
        replace_focused_text(&f, "Aim.direction.");
    }
    char scoped[32][128] = { { 0 } };
    int scoped_count = reopened ? autocomplete_labels(&f, scoped, 32) : 0;
    bool scoped_xy = scoped_count == 2 && !strcmp(scoped[0], "X") &&
                     !strcmp(scoped[1], "Y");
    field = graph_query(&f, ".field.autocomplete input.value");
    char query[128] = { 0 };
    bool query_matches = field && rml_element_value(field, query, sizeof query) > 0 &&
                         !strcmp(query, "Aim.direction.");
    if (query_matches) {
        capture_stage(&f, "source-autocomplete-direction");
        if (getenv("CHIBI_GRAPH_EDIT_CAPTURE_DIR")) {
            int theme = f.studio->prefs.theme;
            f.studio->prefs.theme = STUDIO_THEME_LIGHT;
            capture_stage(&f, "source-autocomplete-direction-light");
            f.studio->prefs.theme = theme;
            frames(&f, 2);
        }
    }
    replace_focused_text(&f, "Aim.direction.x");
    char partial[32][128] = { { 0 } };
    int partial_count = autocomplete_labels(&f, partial, 32);
    field = graph_query(&f, ".field.autocomplete input.value");
    char partial_query[128] = { 0 };
    bool partial_query_matches = field &&
                                 rml_element_value(field, partial_query, sizeof partial_query) >
                                     0 &&
                                 !strcmp(partial_query, "Aim.direction.x");
    bool partial_x_only = partial_count == 1 && !strcmp(partial[0], "X");
    capture_stage(&f, "source-autocomplete-partial");
    replace_focused_text(&f, "Aim.direction.");
    key_event(SDL_SCANCODE_DOWN, true);
    key_event(SDL_SCANCODE_DOWN, false);
    key_event(SDL_SCANCODE_TAB, true);
    key_event(SDL_SCANCODE_TAB, false);
    frames(&f, 2);
    const pin_binding *selected = input_binding(&f, vector_id, 0);
    bool selected_y = selected && selected->kind == PIN_BINDING_COMPONENT_FIELD &&
                      selected->component_key && selected->field_name &&
                      !strcmp(selected->component_key, "GAME.GameAim") &&
                      !strcmp(selected->field_name, "direction.y");
    bool undo = false, redo = false;
    if (selected_y) {
        editor_undo(f.graph);
        frames(&f, 1);
        const pin_binding *undone = input_binding(&f, vector_id, 0);
        undo = undone && undone->field_name &&
               !strcmp(undone->field_name, "direction.x");
        editor_redo(f.graph);
        frames(&f, 1);
        const pin_binding *redone = input_binding(&f, vector_id, 0);
        redo = redone && redone->field_name &&
               !strcmp(redone->field_name, "direction.y");
    }

    bool field_reopened = click_path(&f, input_path) &&
                          kit_wants_text(f.studio->graph.kit, NULL);
    if (field_reopened) {
        replace_focused_text(&f, "0.5");
        capture_stage(&f, "source-autocomplete-literal");
        enter(&f);
    }
    const node *edited = graph_node_by_id(editor_active_graph(f.graph), vector_id);
    bool constant = field_reopened && input_binding(&f, vector_id, 0) == NULL &&
                    edited &&
                    fabsf(edited->input_pins[0].default_value.f - 0.5f) < 0.001f;
    bool literal_undo = false, literal_redo = false;
    if (constant) {
        editor_undo(f.graph);
        frames(&f, 1);
        const pin_binding *restored = input_binding(&f, vector_id, 0);
        literal_undo = restored && restored->field_name &&
                       !strcmp(restored->field_name, "direction.y");
        editor_redo(f.graph);
        frames(&f, 1);
        edited = graph_node_by_id(editor_active_graph(f.graph), vector_id);
        literal_redo = input_binding(&f, vector_id, 0) == NULL && edited &&
                       fabsf(edited->input_pins[0].default_value.f - 0.5f) < 0.001f;
    }

    /* A focused autocomplete owns F, so the graph-level frame shortcut does not run. */
    bool focus_opened = click_path(&f, input_path) &&
                        kit_wants_text(f.studio->graph.kit, NULL);
    float before_focus_zoom = f.studio->graph.zoom;
    replace_focused_text(&f, "f");
    field = graph_query(&f, ".field.autocomplete input.value");
    char focused_query[32] = { 0 };
    bool typed_f = field &&
                   rml_element_value(field, focused_query, sizeof focused_query) > 0 &&
                   !strcmp(focused_query, "f");
    key_event(SDL_SCANCODE_F, true);
    key_event(SDL_SCANCODE_F, false);
    frames(&f, 1);
    bool focused = focus_opened && typed_f &&
                   fabsf(f.studio->graph.zoom - before_focus_zoom) < 0.001f;
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 1);
    field = graph_query(&f, ".field.autocomplete input.value");
    char restored_text[32] = { 0 };
    bool escape_restored = field &&
                           rml_element_value(field, restored_text, sizeof restored_text) >
                               0 &&
                           !strcmp(restored_text, "0.5");

    /* A list with overflow owns wheel input and leaves graph zoom unchanged. */
    bool list_open = click_path(&f, input_path) && kit_has_popup(f.studio->graph.kit);
    rml_element *results = graph_query(&f, ".popup.autocomplete:not([data-ending-style]) .autocomplete-results");
    kit_rect results_rect;
    bool scrollable = results &&
                      rml_element_scroll_height(results) >
                          rml_element_client_height(results) + 1 &&
                      kit_find(f.studio->graph.kit, results_path, &results_rect);
    float zoom = f.studio->graph.zoom;
    float scroll_before = results ? rml_element_scroll_top(results) : 0;
    if (scrollable) {
        wheel_at(
            &f,
            results_rect.x + results_rect.w * 0.5f,
            results_rect.y + results_rect.h * 0.5f,
            -2
        );
        results = graph_query(&f, ".popup.autocomplete:not([data-ending-style]) .autocomplete-results");
        if (results && fabsf(rml_element_scroll_top(results) - scroll_before) < 0.5f) {
            wheel_at(
                &f,
                results_rect.x + results_rect.w * 0.5f,
                results_rect.y + results_rect.h * 0.5f,
                2
            );
            results = graph_query(&f, ".popup.autocomplete:not([data-ending-style]) .autocomplete-results");
        }
    }
    bool scrolled = scrollable && results &&
                    fabsf(rml_element_scroll_top(results) - scroll_before) > 0.5f;
    bool wheel_stays_in_popup = list_open && scrolled &&
                                kit_has_popup(f.studio->graph.kit) &&
                                fabsf(f.studio->graph.zoom - zoom) < 0.001f;
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 1);

    node *integer64 = editor_spawn(f.graph, registry_node_by_name("Integer64"), 520, 320);
    assert(
        integer64 && integer64->output_count == 1 && integer64->output_pins[0].type == PIN_I64
    );
    uint64_t integer64_id = integer64->id;
    frames(&f, 2);
    char integer_path[128];
    snprintf(
        integer_path,
        sizeof integer_path,
        "world/stage/n%llu/b/i0/source-edit",
        (unsigned long long)integer64_id
    );
    bool integer_focused = click_path(&f, integer_path) &&
                           kit_wants_text(f.studio->graph.kit, NULL);
    if (integer_focused) {
        replace_focused_text(&f, "9007199254740993");
        enter(&f);
    }
    const node *integer_edited = graph_node_by_id(editor_active_graph(f.graph), integer64_id);
    bool integer_exact = integer_focused && integer_edited &&
                         integer_edited->input_pins[0].default_value.i64 ==
                             INT64_C(9007199254740993);
    bool integer_undo = false, integer_redo = false;
    if (integer_exact) {
        editor_undo(f.graph);
        frames(&f, 1);
        integer_edited = graph_node_by_id(editor_active_graph(f.graph), integer64_id);
        integer_undo = integer_edited &&
                       integer_edited->input_pins[0].default_value.i64 == 0;
        editor_redo(f.graph);
        frames(&f, 1);
        integer_edited = graph_node_by_id(editor_active_graph(f.graph), integer64_id);
        integer_redo = integer_edited &&
                       integer_edited->input_pins[0].default_value.i64 ==
                           INT64_C(9007199254740993);
    }

    test_check(field_visible && aim_count > 0 && aim_suggested && aim_query && escape_preserved, "the inline source field autocompletes Aim fields and Escape preserves the existing binding");
    if (!(reopened && query_matches && scoped_xy && selected_y && undo && redo)) {
        printf(
            "  source: reopen %d query %d count %d Y %d undo %d redo %d\n",
            reopened,
            query_matches,
            scoped_count,
            selected_y,
            undo,
            redo
        );
        for (int i = 0; i < scoped_count; i++) {
            printf("  option: %s\n", scoped[i]);
        }
    }
    if (!wheel_stays_in_popup) {
        printf(
            "  wheel: open %d scrollable %d scrolled %d zoom %.3f -> %.3f "
            "scroll %.3f\n",
            list_open,
            scrollable,
            scrolled,
            zoom,
            f.studio->graph.zoom,
            scroll_before
        );
    }
    if (!integer_exact) {
        printf(
            "  integer: focused %d value %lld\n",
            integer_focused,
            integer_edited
                ? (long long)integer_edited->input_pins[0].default_value.i64
                : -1LL
        );
    }
    test_check(reopened && query_matches && scoped_xy && selected_y && undo && redo, "Aim.direction. offers compact X/Y completions, accepting Y with Tab supports Undo/Redo");
    test_check(partial_query_matches && partial_x_only, "typing Aim.direction.x keeps only its compact X completion");
    test_check(constant && literal_undo && literal_redo, "typing 0.5 turns a binding into a constant and Undo/Redo restores each value");
    test_check(focused && escape_restored, "the focused source field owns graph shortcuts and Escape restores its committed value");
    if (scrollable) {
        test_check(wheel_stays_in_popup, "scrolling source suggestions does not zoom the graph canvas");
    }
    test_check(integer_exact && integer_undo && integer_redo, "an Integer64 node preserves values beyond floating-point precision with Undo/Redo");
    fixture_free(&f);
}

static void folded_inputs_are_editable_in_place()
{
    graph_input_fixture f = fixture_open();
    frames(&f, 3);
    node *consumer = editor_spawn(f.graph, registry_node_by_name("Atan2"), 440, 140);
    node *source = editor_spawn(f.graph, registry_node_by_name("Add"), 120, 140);
    assert(consumer && source && consumer->input_pins[0].type == PIN_F64);
    graph *gr = f.graph->active_graph;
    assert(graph_connect_data(gr, source, 0, consumer, 0) == 0);
    assert(graph_node_is_inline(gr, source));
    uint64_t consumer_id = consumer->id, source_id = source->id;
    uint64_t input_id = consumer->input_pins[0].id;
    frames(&f, 3);

    char source_edit[128], popup_key[64], popup_path[96], control[180];
    snprintf(source_edit, sizeof source_edit, "world/stage/n%llu/b/i0/source-edit", (unsigned long long)consumer_id);
    bool source_visible = click_path(&f, source_edit) &&
                          kit_wants_text(f.studio->graph.kit, NULL);
    if (source_visible) {
        replace_focused_text(&f, "Edit Inline Inputs");
    }
    bool action_suggested = source_visible &&
                            autocomplete_has_label(&f, "Edit Inline Inputs");
    if (action_suggested) {
        enter(&f);
    }
    snprintf(
        popup_key,
        sizeof popup_key,
        "inline-values-%llu-%llu",
        (unsigned long long)consumer_id,
        (unsigned long long)input_id
    );
    snprintf(popup_path, sizeof popup_path, "layer/@%s", popup_key);
    snprintf(control, sizeof control, "%s/node-%llu/input-0/source-edit", popup_path, (unsigned long long)source_id);
    bool popup_open = action_suggested &&
                      kit_popup_is_open(f.studio->graph.kit, popup_key);
    if (popup_open) {
        capture_stage(&f, "inline-inputs");
    }
    rml_context *context = rml_backend_context(f.studio->graph.backend);
    /* Placement clears data-unplaced before the following layout updates
       inherited visibility and gives the form its initial focus. */
    frames(&f, 2);
    rml_element *inline_popup = popup_open ? graph_query(&f, ".graph-inline-values")
                                           : NULL;
    rml_element *fields[2] = { 0 };
    int field_count = inline_popup
                        ? rml_element_query_selector_all(inline_popup, ".field.autocomplete input", fields, 2)
                        : 0;
    bool initial_focus = field_count >= 2 &&
                         rml_context_focus_element(context) == fields[0];
    if (initial_focus) {
        /* Escape the first input's completion list, then Tab between the
           inline form controls without dismissing their parent popover. */
        key_event(SDL_SCANCODE_ESCAPE, true);
        key_event(SDL_SCANCODE_ESCAPE, false);
        frames(&f, 1);
        key_event(SDL_SCANCODE_TAB, true);
        key_event(SDL_SCANCODE_TAB, false);
        frames(&f, 2);
    }
    bool tab_forward = initial_focus &&
                       kit_popup_is_open(f.studio->graph.kit, popup_key) &&
                       rml_context_focus_element(context) == fields[1];
    if (tab_forward) {
        key_event_mod(SDL_SCANCODE_TAB, true, SDL_KMOD_SHIFT);
        key_event_mod(SDL_SCANCODE_TAB, false, SDL_KMOD_SHIFT);
        frames(&f, 2);
    }
    bool tab_back = tab_forward &&
                    kit_popup_is_open(f.studio->graph.kit, popup_key) &&
                    rml_context_focus_element(context) == fields[0];
    test_check(initial_focus && tab_forward && tab_back, "inline input forms initially focus their first control and keep Tab/Shift+Tab inside the open form");
    kit_rect field_rect;
    bool field_visible = popup_open &&
                         kit_find(f.studio->graph.kit, control, &field_rect);
    bool field_clicked = field_visible && click_path(&f, control) &&
                         kit_wants_text(f.studio->graph.kit, NULL);
    if (field_clicked) {
        replace_focused_number(&f, "17.25");
    }
    const node *edited = graph_node_by_id(editor_active_graph(f.graph), source_id);
    bool value_changed = edited &&
                         fabs(edited->input_pins[0].default_value.d - 17.25) < 0.001;
    bool remains_folded = edited &&
                          graph_node_is_inline(editor_active_graph(f.graph), edited);
    test_check(source_visible && action_suggested, "a folded math source offers Edit Inline Inputs from its autocomplete field");
    test_check(popup_open && field_visible && field_clicked, "Edit Inline Inputs opens a popover with a focused typed field");
    test_check(value_changed && remains_folded, "editing a folded math input changes its value without expanding the graph");
    fixture_free(&f);
}

static void shader_values_and_collections_are_editable()
{
    graph_input_fixture f = fixture_open();
    behavior_asset *material = behavior_asset_new("Graph Input Material");
    assert(material);
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *surface = shader_graph_add_output(material->graph, false);
    node *ramp = graph_add_node(material->graph, registry_node_by_name("Shader Color Ramp"));
    node *scalar = graph_add_node(material->graph, registry_node_by_name("Shader Constant"));
    assert(
        surface && ramp && scalar && shader_graph_init_node(material->graph, ramp) &&
        shader_graph_init_node(material->graph, scalar)
    );
    ramp->x = 100;
    ramp->y = 100;
    ramp->placed = true;
    scalar->x = 100;
    scalar->y = 320;
    scalar->placed = true;
    surface->x = 520;
    surface->y = 100;
    surface->placed = true;
    uint64_t surface_id = surface->id, ramp_id = ramp->id, scalar_id = scalar->id;
    uint64_t color_pin_id = surface->input_pins[0].id;
    uint64_t stops_pin_id = ramp->input_pins[1].id;
    uint64_t graph_id = material->graph->graph_id;
    assert(editor_open_behavior_asset(f.graph, material, graph_id) == 0);
    behavior_asset_free(material);
    f.scene_frame = false;
    frames(&f, 3);

    char number[128];
    snprintf(number, sizeof number, "world/stage/n%llu/b/o0/v", (unsigned long long)scalar_id);
    bool number_clicked = click_path(&f, number) &&
                          kit_wants_text(f.studio->graph.kit, NULL);
    if (number_clicked) {
        replace_focused_number(&f, "0.25");
    }
    const node *current_scalar = graph_node_by_id(editor_active_graph(f.graph), scalar_id);
    bool numeric = current_scalar &&
                   fabsf(current_scalar->output_pins[0].default_value.f - 0.25f) < 0.001f;

    char color_control[128], color_popup[192];
    snprintf(
        color_control,
        sizeof color_control,
        "world/stage/n%llu/b/i0/pin-%llu",
        (unsigned long long)surface_id,
        (unsigned long long)color_pin_id
    );
    snprintf(color_popup, sizeof color_popup, "color:%s", color_control);
    kit_rect color_rect;
    bool color_visible = kit_find(f.studio->graph.kit, color_control, &color_rect);
    bool color_clicked = color_visible && click_path(&f, color_control);
    bool color_open = color_clicked &&
                      kit_popup_is_open(f.studio->graph.kit, color_popup);
    if (color_open) {
        capture_stage(&f, "shader-color");
    }
    rml_element *hex = color_open ? graph_query(&f, ".popup.picker .hex") : NULL;
    bool hex_focused = false;
    if (hex) {
        click_rml_element(&f, hex);
        hex_focused = kit_wants_text(f.studio->graph.kit, NULL);
    }
    if (hex_focused) {
        replace_focused_text(&f, "336699");
    }
    bool hex_committed = hex_focused && click_path(&f, number);
    frames(&f, 1);
    const node *current_surface = graph_node_by_id(editor_active_graph(f.graph), surface_id);
    float *rgb = current_surface ? current_surface->input_pins[0].default_value.ptr
                                 : NULL;
    bool color = hex_committed && rgb && fabsf(rgb[0] - 51.0f / 255) < 0.01f &&
                 fabsf(rgb[1] - 102.0f / 255) < 0.01f &&
                 fabsf(rgb[2] - 153.0f / 255) < 0.01f;
    test_check(color_visible && color_clicked, "a shader color field is present and responds to a click");
    test_check(color_open && hex, "clicking a shader color field opens its hex picker");
    test_check(hex_focused, "the shader color picker's hex field accepts text focus");
    test_check(color, "typing a hex value and leaving the field updates all three shader color channels");
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 1);

    char collection_edit[128], collection_popup[64], add_path[96];
    snprintf(collection_edit, sizeof collection_edit, "world/stage/n%llu/b/i1/edit", (unsigned long long)ramp_id);
    snprintf(collection_popup, sizeof collection_popup, "pin-%llu-items", (unsigned long long)stops_pin_id);
    snprintf(add_path, sizeof add_path, "layer/@%s/add", collection_popup);
    const node *current_ramp = graph_node_by_id(editor_active_graph(f.graph), ramp_id);
    const pin_array *stops = current_ramp
                               ? current_ramp->input_pins[1].default_value.ptr
                               : NULL;
    int before = stops ? stops->count : 0;
    shader_ramp_stop initial_stops[SHADER_RAMP_MAX];
    int initial_count = current_ramp
                          ? shader_node_ramp_get(current_ramp, initial_stops)
                          : 0;
    bool initial_ramp_valid = initial_count == 2 &&
                              shader_ramp_valid(initial_stops, initial_count) &&
                              initial_stops[0].position == 0 &&
                              initial_stops[1].position == 1;
    kit_rect collection_rect;
    bool collection_visible = kit_find(f.studio->graph.kit, collection_edit, &collection_rect);
    bool collection_clicked = collection_visible && click_path(&f, collection_edit);
    bool collection_open = collection_clicked &&
                           kit_popup_is_open(f.studio->graph.kit, collection_popup);
    char struct_trigger[128], struct_popup[96];
    snprintf(struct_trigger, sizeof struct_trigger, "layer/@%s/item-0/@control/edit", collection_popup);
    snprintf(struct_popup, sizeof struct_popup, "pin-%llu-item-0-fields", (unsigned long long)stops_pin_id);
    bool struct_open = collection_open && click_path(&f, struct_trigger) &&
                       kit_popup_is_open(f.studio->graph.kit, struct_popup);
    if (struct_open) {
        frames(&f, 2);
        capture_stage(&f, "collection-nested");
    }
    rml_element *position = struct_open ? graph_query(&f, ".popup.menu .field.number")
                                        : NULL;
    bool position_clicked = false;
    if (position) {
        click_rml_element(&f, position);
        position_clicked = kit_wants_text(f.studio->graph.kit, NULL);
    }
    if (position_clicked) {
        replace_focused_number(&f, "0.75");
    }
    current_ramp = graph_node_by_id(editor_active_graph(f.graph), ramp_id);
    stops = current_ramp ? current_ramp->input_pins[1].default_value.ptr : NULL;
    shader_ramp_stop *first_stop = stops && stops->count ? stops->items[0].ptr : NULL;
    bool nested_value = struct_open && position_clicked && first_stop &&
                        fabsf(first_stop->position - 0.75f) < 0.001f;
    float rgb_before[3] = { first_stop ? first_stop->color[0] : 0,
                            first_stop ? first_stop->color[1] : 0,
                            first_stop ? first_stop->color[2] : 0 };
    rml_element *number_fields[16];
    rml_element *
        document = rml_context_document(rml_backend_context(f.studio->graph.backend), 0);
    int number_field_count = rml_element_query_selector_all(
        document,
        ".popup.menu .field.number",
        number_fields,
        16
    );
    rml_element *alpha_field = NULL;
    for (int i = 0; i < number_field_count; i++) {
        rml_element *addon = rml_element_query_selector(number_fields[i], ".addon");
        char addon_text[32] = { 0 };
        if (addon && rml_element_inner_rml(addon, addon_text, sizeof addon_text) > 0 &&
            !strcmp(addon_text, "Alpha")) {
            alpha_field = number_fields[i];
            break;
        }
    }
    bool alpha_visible = struct_open && alpha_field;
    bool alpha_clicked = false;
    if (alpha_visible) {
        click_rml_element(&f, alpha_field);
        alpha_clicked = kit_wants_text(f.studio->graph.kit, NULL);
    }
    if (alpha_clicked) {
        replace_focused_number(&f, "0.375");
        capture_stage(&f, "collection-alpha");
    }
    current_ramp = graph_node_by_id(editor_active_graph(f.graph), ramp_id);
    stops = current_ramp ? current_ramp->input_pins[1].default_value.ptr : NULL;
    first_stop = stops && stops->count ? stops->items[0].ptr : NULL;
    bool alpha_changed = alpha_clicked && first_stop &&
                         fabsf(first_stop->color[3] - 0.375f) < 0.001f;
    bool alpha_kept_rgb = alpha_changed &&
                          fabsf(first_stop->color[0] - rgb_before[0]) < 0.001f &&
                          fabsf(first_stop->color[1] - rgb_before[1]) < 0.001f &&
                          fabsf(first_stop->color[2] - rgb_before[2]) < 0.001f;
    shader_ramp_stop before_add_stops[SHADER_RAMP_MAX];
    int before_add_count = current_ramp
                             ? shader_node_ramp_get(current_ramp, before_add_stops)
                             : 0;
    bool before_add_valid = before_add_count == 2 &&
                            shader_ramp_valid(before_add_stops, before_add_count);
    bool collection_remains_open = kit_popup_is_open(f.studio->graph.kit, collection_popup);
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 1);
    bool add_clicked = collection_remains_open && click_path(&f, add_path);
    current_ramp = graph_node_by_id(editor_active_graph(f.graph), ramp_id);
    shader_ramp_stop added_stops[SHADER_RAMP_MAX];
    int added_count = current_ramp ? shader_node_ramp_get(current_ramp, added_stops)
                                   : 0;
    bool added_valid = add_clicked && added_count == 3 &&
                       shader_ramp_valid(added_stops, added_count) &&
                       before_add_valid &&
                       fabsf(added_stops[0].position - before_add_stops[0].position) <
                           0.001f &&
                       fabsf(added_stops[2].position - before_add_stops[1].position) <
                           0.001f &&
                       added_stops[1].position > added_stops[0].position &&
                       added_stops[1].position < added_stops[2].position;
    bool inserted_color_interpolated = added_valid;
    if (added_valid) {
        float t = (added_stops[1].position - before_add_stops[0].position) /
                  (before_add_stops[1].position - before_add_stops[0].position);
        for (int channel = 0; channel < 4; channel++) {
            float expected = before_add_stops[0].color[channel] +
                             t * (before_add_stops[1].color[channel] -
                                  before_add_stops[0].color[channel]);
            inserted_color_interpolated &= fabsf(added_stops[1].color[channel] - expected) <
                                           0.002f;
        }
    }
    bool add_undo = false, add_redo = false;
    if (added_valid) {
        editor_undo(f.graph);
        frames(&f, 1);
        current_ramp = graph_node_by_id(editor_active_graph(f.graph), ramp_id);
        shader_ramp_stop undone_stops[SHADER_RAMP_MAX];
        int undone_count = current_ramp
                             ? shader_node_ramp_get(current_ramp, undone_stops)
                             : 0;
        add_undo = undone_count == before_add_count &&
                   shader_ramp_valid(undone_stops, undone_count);
        for (int i = 0; add_undo && i < undone_count; i++) {
            add_undo &= fabsf(undone_stops[i].position - before_add_stops[i].position) <
                        0.001f;
            for (int channel = 0; add_undo && channel < 4; channel++) {
                add_undo &= fabsf(
                                undone_stops[i].color[channel] -
                                before_add_stops[i].color[channel]
                            ) < 0.001f;
            }
        }
        editor_redo(f.graph);
        frames(&f, 1);
        current_ramp = graph_node_by_id(editor_active_graph(f.graph), ramp_id);
        shader_ramp_stop redone_stops[SHADER_RAMP_MAX];
        int redone_count = current_ramp
                             ? shader_node_ramp_get(current_ramp, redone_stops)
                             : 0;
        add_redo = redone_count == added_count &&
                   shader_ramp_valid(redone_stops, redone_count);
    }

    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 1);
    char interpolation[128], interpolation_popup[192];
    snprintf(interpolation, sizeof interpolation, "world/stage/n%llu/b/i2/v", (unsigned long long)ramp_id);
    snprintf(interpolation_popup, sizeof interpolation_popup, "select:%s", interpolation);
    kit_rect interpolation_anchor, interpolation_menu;
    bool interpolation_visible = kit_find(f.studio->graph.kit, interpolation, &interpolation_anchor);
    bool interpolation_open = interpolation_visible &&
                              click_path(&f, interpolation) &&
                              kit_dropdown_is_open(f.studio->graph.kit, interpolation_popup);
    char interpolation_menu_path[224];
    snprintf(interpolation_menu_path, sizeof interpolation_menu_path, "layer/@%s", interpolation_popup);
    bool interpolation_anchored = interpolation_open &&
                                  kit_find(f.studio->graph.kit, interpolation_menu_path, &interpolation_menu) &&
                                  fabsf(
                                      interpolation_anchor.x -
                                      interpolation_menu.x
                                  ) < 2 &&
                                  interpolation_menu.y >= interpolation_anchor.y;
    bool interpolation_filtered = false;
    if (interpolation_open) {
        text_event("Smoothstep");
        frame(&f);
        interpolation_filtered = !strcmp(
            kit_dropdown_query(f.studio->graph.kit, interpolation_popup),
            "Smoothstep"
        );
        enter(&f);
    }
    current_ramp = graph_node_by_id(editor_active_graph(f.graph), ramp_id);
    bool smoothstep = current_ramp && current_ramp->input_pins[2].default_value.i == 2;
    bool interpolation_undo = false;
    if (smoothstep) {
        editor_undo(f.graph);
        frames(&f, 1);
        current_ramp = graph_node_by_id(editor_active_graph(f.graph), ramp_id);
        interpolation_undo = current_ramp &&
                             current_ramp->input_pins[2].default_value.i == 0;
    }

    test_check(number_clicked && numeric, "a shader scalar accepts direct numeric typing in its node");
    test_check(before == 2 && initial_ramp_valid, "a shader color ramp starts with valid stops at 0 and 1");
    test_check(collection_visible, "the Shader Ramp exposes its Stops value control in the node");
    test_check(collection_clicked, "the Shader Ramp Stops value control responds to a click");
    test_check(collection_open, "clicking Stops opens its collection editor");
    test_check(struct_open, "a Shader Ramp stop opens its nested struct editor");
    test_check(position != NULL && position_clicked && nested_value, "a Shader Ramp stop's Position field accepts numeric typing");
    test_check(alpha_visible && alpha_clicked, "a Shader Ramp stop exposes a directly editable color Alpha field");
    test_check(alpha_changed && alpha_kept_rgb, "typing alpha preserves its precise value and leaves the stop's RGB channels unchanged");
    test_check(added_valid && inserted_color_interpolated && add_undo && add_redo, "adding a ramp stop inserts a valid interpolated RGBA value and Undo/Redo restores it");
    test_check(interpolation_visible, "the Shader Ramp exposes its Interpolation setting in the node");
    test_check(interpolation_open && interpolation_anchored, "the Interpolation setting opens an anchored searchable choice list");
    test_check(interpolation_filtered, "searching the Interpolation choices finds Smoothstep by name");
    test_check(smoothstep && interpolation_undo, "selecting Smoothstep writes its named integer value and Undo restores Linear");
    fixture_free(&f);
}

int studio_graph_inputs_run(const char *program)
{
    CF_Result result = cf_make_app(
        "Studio graph inputs",
        0,
        0,
        0,
        1280,
        800,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "Studio graph inputs app: %s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    app_input_initialize();
    bound_input_can_be_changed_and_edited();
    folded_inputs_are_editable_in_place();
    shader_values_and_collections_are_editable();
    app_input_shutdown();
    cf_destroy_app();
    return 0;
}
