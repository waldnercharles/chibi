/* Studio frames over a project on disk: drawing a graph never changes what its
 * asset saves, so reloading a viewed asset leaves it saved. */
#include "assets/ui_events.h"
#include "editor/collab/collab.h"
#include "editor/studio/studio_designer_internal.h"
#include "editor/studio/studio_graph_internal.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "graph/document/serialize.h"
#include "graph/nodes/component_nodes.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "test/suite_internal.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FRAME_SECONDS (1.0 / 60.0)
#define PROJECT "test/fixtures/studio"
/* Weapons' Ignore Shooter function and its automatic component setter. The
   setter's file entry, {id: 10247, type: Set_GameCollider_ignore_entity},
   stays one line wherever the canvas arranges it. */
#define FUNCTION_GRAPH 1
#define SETTER 10247

static pin_spec VALUE_CONTROL_INPUTS[] = {
    { .name = "Packed Color",
      .type = PIN_U32,
      .type_name = "unsigned int",
      .flags = PIN_FLAG_COLOR,
      .default_value = { .type = PIN_U32, .u = 0xf0cd69ff } },
    { .name = "Vector Color",
      .type = PIN_STRUCT,
      .type_name = "CF_V3",
      .flags = PIN_FLAG_COLOR,
      .default_value = { .type = PIN_STRUCT, .type_name = "CF_V3" } },
    { .name = "Shape",
      .type = PIN_ENUM,
      .type_name = "GameShape",
      .default_value = { .type = PIN_ENUM, .type_name = "GameShape", .e = 1 } },
};
static node_desc VALUE_CONTROL_NODE = { .name = "Value Control Fixture",
                                        .input_count = 3,
                                        .inputs = VALUE_CONTROL_INPUTS };

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
} studio_fixture;

/* Studio over scene `scene` of the project at `project` with `behavior`
   open. */
static studio_fixture fixture_open(const char *project, const char *scene, const char *behavior)
{
    studio_fixture f = { .world = ecs_world_new() };
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
    snprintf(content, sizeof content, "%s/content", project);
    assert(app_scene_authoring_set_content_root(f.scene, content) == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    assert(app_scene_authoring_load(f.scene, scene) == 0);
    assert(app_scene_authoring_open_behavior(f.scene, behavior) == 0);
    return f;
}

static studio_fixture fixture_new()
{
    studio_fixture f = fixture_open(PROJECT, PROJECT "/content/scenes/Main.kdl", "Weapons");
    assert(editor_open_graph(f.graph, FUNCTION_GRAPH) == 0);
    f.studio = studio_new(f.scene, f.viewport, PROJECT);
    studio_maximize_graph(f.studio);
    return f;
}

static void fixture_free(studio_fixture *f)
{
    studio_free(f->studio);
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    engine_viewport_free(f->viewport);
}

static void frames(studio_fixture *f, int count);
static rml_element *studio_element(studio *s, const char *selector);
static void press(studio *s, rml_element *el);
static void key_event(SDL_Scancode key, bool down);
static void text_event(const char *text);
static void mouse_move(const studio *s, float x, float y);
static int tab_named(const app_scene_authoring *scene, const char *label);

static void graph_key(studio *s, rml_input_key key)
{
    (void)s;
    assert(key == RML_INPUT_KEY_ENTER);
    key_event(SDL_SCANCODE_RETURN, true);
    key_event(SDL_SCANCODE_RETURN, false);
}

static void graph_text(studio *s, const char *text)
{
    (void)s;
    text_event(text);
}

static void graph_press_rect(studio *s, kit_rect rect)
{
    studio_graph *g = &s->graph;
    float x = g->surface.rect.x + (rect.x + rect.w * 0.5f) * g->in.zoom;
    float y = g->surface.rect.y + (rect.y + rect.h * 0.5f) * g->in.zoom;
    mouse_move(s, x, y);
    for (int down = 1; down >= 0; down--) {
        SDL_Event event = { .type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                         : SDL_EVENT_MOUSE_BUTTON_UP };
        event.button.windowID = SDL_GetWindowID(cf_app_get_window());
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = down;
        event.button.clicks = 1;
        event.button.x = x * s->prefs.ui_scale;
        event.button.y = y * s->prefs.ui_scale;
        assert(SDL_PushEvent(&event));
    }
}

static bool graph_value_control_shown(studio_fixture *f, node *n, int pin)
{
    char path[128];
    kit_rect rect;
    const pin_spec *spec = &n->input_pins[pin];
    char control[32];
    if (sg_value_kind_of(f->studio, n, 0, pin) == VALUE_COLOR) {
        snprintf(control, sizeof control, "pin-%llu", (unsigned long long)spec->id);
    } else {
        snprintf(control, sizeof control, "%s", spec->flags & PIN_FLAG_SETTING ? "v" : "source-edit");
    }
    snprintf(
        path,
        sizeof path,
        "world/stage/n%llu/b/i%d/%s",
        (unsigned long long)n->id,
        pin,
        control
    );
    return kit_find(f->studio->graph.kit, path, &rect) && rect.w > 0 && rect.h > 0;
}

/* Color presentation reaches the graph canvas for declared pins and reflected
   component fields; enum metadata continues to select its catalog control. */
static void graph_value_controls_use_metadata()
{
    studio_fixture f = fixture_new();
    graph *gr = f.graph->active_graph;
    node *values = graph_add_node(gr, &VALUE_CONTROL_NODE);
    assert(values);
    values->x = 420;
    values->y = 90;
    values->placed = true;
    node *set = graph_add_node(gr, registry_node_by_name("Set Component"));
    assert(set && component_node_retarget(gr, set, "GameAppearance") == 0);
    assert(component_node_split_value(gr, set, true) == 0);
    set->x = 120;
    set->y = 90;
    set->placed = true;
    int color_pin = -1;
    for (int i = 0; i < set->input_count; i++) {
        if (set->input_pins[i].name && !strcmp(set->input_pins[i].name, "color")) {
            color_pin = i;
            break;
        }
    }
    assert(color_pin >= 0);
    bool kinds = sg_value_kind_of(f.studio, values, 0, 0) == VALUE_COLOR &&
                 sg_value_kind_of(f.studio, values, 0, 1) == VALUE_COLOR &&
                 sg_value_kind_of(f.studio, values, 0, 2) == VALUE_CHOICE &&
                 sg_value_kind_of(f.studio, set, 0, color_pin) == VALUE_COLOR;
    frames(&f, 3);
    bool controls = graph_value_control_shown(&f, values, 0) &&
                    graph_value_control_shown(&f, values, 1) &&
                    graph_value_control_shown(&f, values, 2) &&
                    graph_value_control_shown(&f, set, color_pin);
    test_check(kinds && controls, "graph pins draw color pickers from pin and reflected metadata and enums use choices");

    fixture_free(&f);
}

static void graph_color_session_saves_undoes_and_replays()
{
    studio_fixture f = fixture_new();
    graph *gr = f.graph->active_graph;
    node *set = graph_add_node(gr, registry_node_by_name("Set Component"));
    assert(set && component_node_retarget(gr, set, "GameAppearance") == 0);
    assert(component_node_split_value(gr, set, true) == 0);
    set->x = 120;
    set->y = 90;
    set->placed = true;
    int color_pin = -1;
    for (int i = 0; i < set->input_count; i++) {
        if (set->input_pins[i].name && !strcmp(set->input_pins[i].name, "color")) {
            color_pin = i;
            break;
        }
    }
    assert(color_pin >= 0);
    uint64_t set_id = set->id;
    uint32_t initial = set->input_pins[color_pin].default_value.u;
    bool can_undo_before = editor_can_undo(f.graph);
    char path[128];
    snprintf(
        path,
        sizeof path,
        "world/stage/n%llu/b/i%d/pin-%llu",
        (unsigned long long)set_id,
        color_pin,
        (unsigned long long)set->input_pins[color_pin].id
    );
    frames(&f, 3);
    kit_rect control;
    assert(kit_find(f.studio->graph.kit, path, &control));
    graph_press_rect(f.studio, control);
    frames(&f, 3);
    rml_element *graph_doc = rml_context_document(
        rml_backend_context(f.studio->graph.backend),
        0
    );
    rml_element *hex = rml_element_query_selector(graph_doc, ".popup.picker .hex");
    if (hex) {
        rml_element_focus(hex, true);
        rml_element_select(hex);
        graph_text(f.studio, "482b86ff");
        graph_key(f.studio, RML_INPUT_KEY_ENTER);
        frames(&f, 2);
    }
    const graph *active = editor_active_graph(f.graph);
    node *edited = graph_node_by_id(active, set_id);
    bool changed = hex && edited &&
                   edited->input_pins[color_pin].default_value.u == 0x482b86ff;
    if (kit_find(f.studio->graph.kit, path, &control)) {
        graph_press_rect(f.studio, control);
    }
    frames(&f, 3);

    char *saved_document = behavior_asset_save(editor_behavior_asset(f.graph));
    behavior_asset *saved = saved_document ? behavior_asset_load(saved_document) : NULL;
    free(saved_document);
    graph *saved_graph = saved ? behavior_asset_find_graph(saved, active->graph_id)
                               : NULL;
    node *saved_set = saved_graph ? graph_node_by_id(saved_graph, set_id) : NULL;
    bool persisted = saved_set &&
                     saved_set->input_pins[color_pin].default_value.u == 0x482b86ff;
    if (saved) {
        behavior_asset_free(saved);
    }

    sg_update_routes(f.studio);
    unsigned before_undo = f.studio->graph.calculations->route_preparations;
    editor_undo(f.graph);
    active = editor_active_graph(f.graph);
    node *undone_set = graph_node_by_id(active, set_id);
    bool undone = undone_set &&
                  undone_set->input_pins[color_pin].default_value.u == initial &&
                  editor_can_undo(f.graph) == can_undo_before &&
                  editor_can_redo(f.graph);
    sg_update_routes(f.studio);
    sg_find_unused(f.studio);
    bool undo_routes = f.studio->graph.calculations->route_preparations > before_undo;
    editor_redo(f.graph);
    active = editor_active_graph(f.graph);
    node *redone_set = graph_node_by_id(active, set_id);
    bool redone = redone_set &&
                  redone_set->input_pins[color_pin].default_value.u == 0x482b86ff;
    sg_update_routes(f.studio);
    sg_find_unused(f.studio);
    bool current_routes = true;
    for (int i = 0; i < acount(f.studio->graph.calculations->routing.nodes); i++) {
        node *routed = f.studio->graph.calculations->routing.nodes[i];
        current_routes &= graph_node_by_id(active, routed->id) == routed;
    }
    test_check(undo_routes && current_routes, "undo and redo replace cached routing inputs with the restored graph");
    test_check(changed && persisted && undone && redone, "a graph color picker saves its RGBA value and streams one undoable session that replays");
    fixture_free(&f);
}

/* One application frame. File > Reload runs inside Studio's frame, before the
   canvas draws, so a reload lands between the scene and Studio frames. */
static void frame_delta(studio_fixture *f, bool reload, double delta_seconds)
{
    f->time += delta_seconds;
    cf_app_update(NULL);
    app_input_frame();
    editor_tick(f->graph, f->time);
    assert(app_scene_authoring_frame(f->scene, f->time) == FLOW_OK);
    if (reload) {
        assert(app_scene_authoring_reload(f->scene) == 0);
    }
    studio_frame(f->studio, delta_seconds);
    cf_app_draw_onto_screen(true);
}

static void frame(studio_fixture *f, bool reload)
{
    frame_delta(f, reload, FRAME_SECONDS);
}

void test_studio_graph_zoom(studio *s, node *n);

static void reload_keeps_viewed_graph_saved()
{
    studio_fixture f = fixture_new();
    for (int i = 0; i < 3; i++) {
        frame(&f, false);
    }
    frame(&f, true);
    for (int i = 0; i < 3; i++) {
        frame(&f, false);
    }
    const node *setter = graph_node_by_id(editor_active_graph(f.graph), SETTER);
    test_check(setter && setter->x != 0, "Studio arranges the reloaded graph it shows");
    const game_live_definition *weapons = game_live_find_definition(f.live, "Weapons");
    test_check(!weapons->unsaved, "reloading a library Studio drew leaves it saved");
    fixture_free(&f);
}

static void graph_layout_does_not_wiggle()
{
    studio_fixture f = fixture_new();
    for (int i = 0; i < 3; i++) {
        frame(&f, false);
    }
    test_studio_graph_zoom(f.studio, graph_node_by_id(editor_active_graph(f.graph), SETTER));
    fixture_free(&f);
}

static void graph_calculations_follow_edits()
{
    studio_fixture f = fixture_new();
    frames(&f, 3);
    studio *s = f.studio;
    graph *gr = sg_active_graph(s);
    node *n = graph_add_node(gr, &VALUE_CONTROL_NODE);
    assert(n);
    node_layout before = *sg_layout_of(s, n);
    sg_update_routes(s);
    sg_find_unused(s);
    struct studio_graph_calculations *c = s->graph.calculations;
    unsigned measured = c->measurements, prepared = c->route_preparations;
    unsigned reachable = c->reachability_builds;
    s->graph.x += 150;
    s->graph.y -= 70;
    s->graph.zoom *= 0.9f;
    node_layout panned = *sg_layout_of(s, n);
    sg_update_routes(s);
    sg_find_unused(s);
    test_check(
        c->measurements == measured && c->route_preparations == prepared &&
            c->reachability_builds == reachable && before.w == panned.w &&
            before.h == panned.h,
        "camera movement retains intrinsic measurements, routing input and "
        "reachability"
    );

    n->x += 37;
    n->y -= 19;
    sg_update_routes(s);
    bool moved = false;
    for (int i = 0; i < acount(c->routing.nodes); i++) {
        if (c->routing.nodes[i] == n) {
            moved = c->routing.pos[i].x == n->x && c->routing.pos[i].y == n->y;
        }
    }
    test_check(moved && c->route_preparations == prepared, "uncommitted node movement updates routing positions without preparing topology again");

    assert(graph_set_pin_name(gr, n, 0, 0, "An intentionally much longer reflected input label") == 0);
    node_layout renamed = *sg_layout_of(s, n);
    sg_update_routes(s);
    sg_find_unused(s);
    test_check(renamed.w > before.w && c->route_preparations > prepared && c->reachability_builds > reachable, "pin label edits invalidate measured geometry and dependent calculations");
    unsigned after_label = c->measurements;
    s->prefs.standard_font_size = 18;
    (void)sg_layout_of(s, n);
    test_check(c->measurements > after_label, "font presentation changes invalidate intrinsic measurements");

    static pin_spec choice_output = { .name = "Shape",
                                      .type = PIN_ENUM,
                                      .type_name = "GameShape" };
    static node_desc choice_source = { .name = "Cache Choice Source",
                                       .output_count = 1,
                                       .outputs = &choice_output };
    node *source = graph_add_node(gr, &choice_source);
    assert(source && graph_connect_data(gr, source, 0, n, 2) == 0);
    assert(graph_set_node_presentation(gr, source, NODE_PRESENTATION_EXPANDED) == 0);
    sg_update_routes(s);
    int expanded_count = acount(c->routing.nodes);
    assert(graph_set_node_presentation(gr, source, NODE_PRESENTATION_INLINE) == 0);
    sg_update_routes(s);
    sg_find_unused(s);
    test_check(!sg_node_visible(gr, source) && acount(c->routing.nodes) == expanded_count - 1, "folding removes the source box and its route input while retaining its editable model");
    assert(graph_remove_node(gr, source) == 0);

    uint64_t id = n->id;
    assert(graph_remove_node(gr, n) == 0);
    sg_update_routes(s);
    sg_find_unused(s);
    bool removed = !sg_is_unused(&s->graph, id);
    for (int i = 0; i < acount(c->routing.nodes); i++) {
        removed &= c->routing.nodes[i]->id != id;
    }
    for (int i = 0; i < acount(s->graph.layouts); i++) {
        removed &= s->graph.layouts[i].id != id;
    }
    test_check(removed, "removed nodes leave measurement, route and unused caches together");
    fixture_free(&f);
}

static void graph_drag_button(const studio *s, float x, float y, bool down)
{
    SDL_Event event = { .type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                     : SDL_EVENT_MOUSE_BUTTON_UP };
    event.button.windowID = SDL_GetWindowID(cf_app_get_window());
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x * s->prefs.ui_scale;
    event.button.y = y * s->prefs.ui_scale;
    assert(SDL_PushEvent(&event));
}

void test_studio_menu_context(studio *s);

static void graph_drag_retains_calculations_and_undo()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 3);
    studio_graph *g = &s->graph;
    node *n = graph_node_by_id(sg_active_graph(s), SETTER);
    assert(n);
    g->camera.active = false;
    g->framed = true;
    g->zoom = 1;
    g->x = n->x - 80;
    g->y = n->y - 80;
    frames(&f, 3);
    float old_x = n->x, old_y = n->y;
    int undo_before = acount(f.graph->undo);
    kit_rect title;
    assert(kit_find(g->kit, "world/stage/n10247/h/t", &title));
    float x = g->surface.rect.x + (title.x + title.w / 2) * sg_zoom(s);
    float y = g->surface.rect.y + (title.y + title.h / 2) * sg_zoom(s);
    mouse_move(s, x, y);
    graph_drag_button(s, x, y, true);
    frame(&f, false);
    mouse_move(s, x + 20, y + 12);
    frame(&f, false);
    bool moving = g->gesture == STUDIO_GRAPH_MOVE && f.graph->pending_snapshot &&
                  !editor_has_pending_edits(f.graph);
    struct studio_graph_calculations *c = g->calculations;
    unsigned measured = c->measurements, prepared = c->route_preparations;
    unsigned reachable = c->reachability_builds;
    bool cached = moving, follows = moving, anchored = moving;
    for (int step = 1; step <= 3; step++) {
        float dx = 20 + (float)step * 10, dy = 12 + (float)step * 8;
        mouse_move(s, x + dx, y + dy);
        frame(&f, false);
        cached &= g->gesture == STUDIO_GRAPH_MOVE && f.graph->pending_snapshot &&
                  !editor_has_pending_edits(f.graph) &&
                  c->measurements == measured && c->route_preparations == prepared &&
                  c->reachability_builds == reachable;
        follows &= fabsf(n->x - old_x - dx / sg_zoom(s)) < 0.01f &&
                   fabsf(n->y - old_y - dy / sg_zoom(s)) < 0.01f &&
                   g->hover_node == SETTER;
        bool wire = false;
        graph *gr = sg_active_graph(s);
        for (int w = 0; w < acount(g->wire_edges); w++) {
            const edge *ed = &gr->edges[g->wire_edges[w]];
            if (ed->from_id != SETTER) {
                continue;
            }
            int output, pin;
            assert(graph_pin_by_id(n, ed->from_pin_id, &output, &pin) && output);
            float sx, sy, points[4];
            sg_socket_at(s, n, ED_PIN_OUTPUT, pin, &sx, &sy);
            wire |= kit_wire_points(g->kit, "back/wires", w, points, 2) >= 2 &&
                    fabsf(points[0] - (sx - g->x)) < 0.5f &&
                    fabsf(points[1] - (sy - g->y)) < 0.5f;
        }
        anchored &= wire;
    }
    test_check(cached, "real held node dragging retains intrinsic measurements, route preparation and reachability while its undo snapshot is pending");
    test_check(follows && anchored, "real node dragging updates pointer picking and rendered wire anchors on each movement");
    graph_drag_button(s, x + 50, y + 36, false);
    frame(&f, false);
    bool committed = g->gesture == STUDIO_GRAPH_IDLE && !f.graph->pending_snapshot &&
                     acount(f.graph->undo) == undo_before + 1;
    editor_undo(f.graph);
    frames(&f, 3);
    n = graph_node_by_id(sg_active_graph(s), SETTER);
    test_check(
        committed && n && fabsf(n->x - old_x) < 0.01f &&
            fabsf(n->y - old_y) < 0.01f && acount(f.graph->undo) == undo_before,
        "releasing a real drag commits one undo step that restores the "
        "original node position"
    );
    test_studio_menu_context(s);
    fixture_free(&f);
}

static void host_ui_refresh_keeps_input_immediate()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 90);
    s->active_until = 0;
    uint64_t before = s->ui_schedule.frames;
    double time = f.time;
    frames(&f, 60);
    uint64_t idle = s->ui_schedule.frames - before;
    test_check(idle >= 3 && idle < 10 && f.time > time + 0.99, "settled idle UI refresh is bounded while simulation frames keep advancing");

    s->active_until = 0;
    s->ui_schedule.next = s->time + 1;
    before = s->ui_schedule.frames;
    int toast = kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){
            .title = "Async host wake",
            .duration = -1,
        }
    );
    frame(&f, false);
    char toast_text[128] = "";
    rml_element *toast_element = studio_element(s, ".toast .title");
    if (toast_element) {
        rml_element_inner_rml(toast_element, toast_text, sizeof toast_text);
    }
    test_check(
        s->ui_schedule.frames == before + 1 && strstr(toast_text, "Async host wake"),
        "a new toast wakes and renders the idle host without SDL input"
    );
    kit_toast_dismiss(s->kit, toast);
    frames(&f, 90);
    s->active_until = 0;
    s->ui_schedule.next = s->time + 1;
    before = s->ui_schedule.frames;
    kit_style_sheet(s->kit, "host-wake-probe", ".statusbar { opacity: 0.999; }");
    frame(&f, false);
    char opacity[32] = "";
    rml_element *bar = studio_element(s, ".statusbar");
    if (bar) {
        rml_element_property(bar, "opacity", opacity, sizeof opacity);
    }
    test_check(s->ui_schedule.frames == before + 1 && fabs(atof(opacity) - 0.999) < 0.0001, "an asynchronous restyle wakes and updates the idle host without SDL input");
    kit_style_sheet(s->kit, "host-wake-probe", "");
    frames(&f, 3);

    before = s->ui_schedule.frames;
    s->prefs.short_bindings = !s->prefs.short_bindings;
    frame(&f, false);
    bool prefs = s->ui_schedule.frames == before + 1;
    /* Hold the passive deadline ahead to make each immediate wake observable. */
    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    ed_select_node(f.graph, graph_node_by_id(sg_active_graph(s), 2), false);
    frame(&f, false);
    bool selection = s->ui_schedule.frames == before + 1;

    graph *selected_graph = sg_active_graph(s);
    node *first = graph_node_by_id(selected_graph, 2);
    node *second = graph_node_by_id(selected_graph, SETTER);
    node *third = graph_node_by_id(selected_graph, 6);
    assert(first && second && third);
    ed_add_selected(f.graph, second);
    frame(&f, false);
    s->active_until = 0;
    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    ed_remove_selected(f.graph, second);
    ed_add_selected(f.graph, third);
    frame(&f, false);
    test_check(s->ui_schedule.frames == before + 1, "replacing a secondary selected node wakes host emission with the first node and count unchanged");

    assert(acount(selected_graph->edges) >= 2);
    ed_link_ref first_link, second_link;
    sg_link_ref_of(&selected_graph->edges[0], &first_link);
    sg_link_ref_of(&selected_graph->edges[1], &second_link);
    ed_add_selected_link(f.graph, &first_link);
    frame(&f, false);
    s->active_until = 0;
    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    asetlen(f.graph->selected_links, 0);
    ed_add_selected_link(f.graph, &second_link);
    frame(&f, false);
    test_check(s->ui_schedule.frames == before + 1, "replacing a selected link wakes host emission with its selection count unchanged");

    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    graph *gr = sg_active_graph(s);
    assert(
        graph_set_node_presentation(gr, graph_node_by_id(gr, SETTER), NODE_PRESENTATION_EXPANDED) == 0
    );
    frame(&f, false);
    bool model = s->ui_schedule.frames == before + 1;
    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    text_event("a");
    frame(&f, false);
    bool input = s->ui_schedule.frames == before + 1;
    test_check(prefs && selection && model && input, "preferences, selection, direct graph edits and SDL text wake the host UI on the next frame");

    s->active_until = 0;
    s->in.down[0] = true;
    before = s->ui_schedule.frames;
    frames(&f, 3);
    bool held = s->ui_schedule.frames == before + 3;
    s->in.down[0] = false;
    test_check(held, "held pointer interaction retains one UI emission per simulation frame");

    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 4);
    s->active_until = 0;
    before = s->ui_schedule.frames;
    time = f.time;
    frames(&f, 60);
    uint64_t playing = s->ui_schedule.frames - before;
    app_runtime_state runtime = app_scene_authoring_runtime_state(f.scene);
    if (!runtime.playing || playing < 15 || playing >= 30 || f.time <= time + 0.99) {
        printf(
            "  playing refresh: running=%d target=%d emissions=%llu "
            "elapsed=%.6f kit_idle=%.6f rml_next=%.6f active_for=%.6f "
            "next_in=%.6f\n",
            runtime.playing,
            runtime.target,
            (unsigned long long)playing,
            f.time - time,
            kit_idle_time(s->kit),
            rml_context_next_update_delay(rml_backend_context(s->backend)),
            s->active_until - s->time,
            s->ui_schedule.next - s->time
        );
    }
    test_check(runtime.playing && playing >= 15 && playing < 30 && f.time > time + 0.99, "playing inspection refreshes at a bounded rate while runtime frames continue");
    /* A real input burst keeps the application active, while settled model
       inspection can continue at its passive cadence. Empty canvas does not
       start tooltip or control motion. */
    mouse_move(
        s,
        s->graph.surface.rect.x + s->graph.surface.rect.w - 12,
        s->graph.surface.rect.y + s->graph.surface.rect.h - 12
    );
    frame(&f, false);
    bool active = s->active_until > s->time + 0.9;
    before = s->ui_schedule.frames;
    time = f.time;
    frames(&f, 60);
    uint64_t after_input = s->ui_schedule.frames - before;
    test_check(
        active && after_input >= 15 && after_input < 30 &&
            app_scene_authoring_runtime_state(f.scene).playing && f.time > time + 0.99,
        "a real input activity window keeps runtime clocks advancing without "
        "forcing full-rate host model emissions"
    );
    studio_runtime(s, APP_RUNTIME_STOP);
    fixture_free(&f);
}

static void performance_pointer_button(studio *s, float x, float y, bool down)
{
    SDL_Event event = { .type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                     : SDL_EVENT_MOUSE_BUTTON_UP };
    event.button.windowID = SDL_GetWindowID(cf_app_get_window());
    event.button.button = SDL_BUTTON_MIDDLE;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x * s->prefs.ui_scale;
    event.button.y = y * s->prefs.ui_scale;
    assert(SDL_PushEvent(&event));
}

static bool performance_pan_frames(studio_fixture *f, float x, float y)
{
    studio *s = f->studio;
    bool deadline = true;
    for (int i = 0; i < 60; i++) {
        double next = s->perf_publish_next;
        uint64_t publications = s->perf_publications;
        float camera_x = s->graph.x, camera_y = s->graph.y;
        rml_element *focus = rml_context_focus_element(rml_backend_context(s->backend));
        mouse_move(s, x + (float)(i % 20), y + (float)(i % 10));
        frame(f, false);
        deadline &= (!i || s->graph.x != camera_x || s->graph.y != camera_y) &&
                    rml_context_focus_element(rml_backend_context(s->backend)) == focus;
        if (s->time < next) {
            deadline &= s->perf_publications == publications &&
                        s->perf_publish_next == next;
        }
        deadline &= s->graph.gesture == STUDIO_GRAPH_PAN;
    }
    return deadline;
}

static void performance_snapshot_retains_intermediate_frames()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 90);
    float x = s->graph.surface.rect.x + s->graph.surface.rect.w / 2;
    float y = s->graph.surface.rect.y + s->graph.surface.rect.h / 2;
    mouse_move(s, x, y);
    performance_pointer_button(s, x, y, true);
    frame(&f, false);
    int recorded = acount(s->perf);
    uint64_t published = s->perf_publications, emitted = s->ui_schedule.frames;
    bool idle_deadline = performance_pan_frames(&f, x, y);
    uint64_t idle_publications = s->perf_publications - published;
    test_check(
        idle_deadline && acount(s->perf) == recorded + 60 && idle_publications >= 3 &&
            idle_publications <= 5 && s->ui_schedule.frames >= emitted + 3 &&
            s->ui_schedule.frames <= emitted + 10,
        "real idle panning advances the camera and recorder every frame while "
        "host inspection and telemetry stay bounded without losing focus or "
        "rebasing deadlines"
    );
    emitted = s->ui_schedule.frames;
    mouse_move(s, s->graph.surface.rect.x - 4, y);
    frame(&f, false);
    bool boundary = s->ui_schedule.frames == emitted + 1 &&
                    !s->graph.surface.hovered && s->graph.gesture == STUDIO_GRAPH_PAN;
    emitted = s->ui_schedule.frames;
    mouse_move(s, x, y);
    frame(&f, false);
    test_check(
        boundary && s->ui_schedule.frames == emitted + 1 &&
            s->graph.surface.hovered && s->graph.gesture == STUDIO_GRAPH_PAN,
        "a captured pan still routes leaving and reentering the graph surface "
        "through the host"
    );

    emitted = s->ui_schedule.frames;
    studio_inspect_toggle(s);
    mouse_move(s, x + 1, y + 1);
    frame(&f, false);
    test_check(studio_inspect_picking(s) && s->ui_schedule.frames == emitted + 1, "Inspect Element picking keeps host motion delivery during an established graph pan");
    studio_inspect_toggle(s);
    frame(&f, false);

    kit_command_open(s->kit, "Search");
    emitted = s->ui_schedule.frames;
    mouse_move(s, x + 2, y + 2);
    frame(&f, false);
    rml_element *input = studio_element(s, ".cmd-input input");
    test_check(
        kit_command_is_open(s->kit) && input &&
            rml_context_focus_element(rml_backend_context(s->backend)) == input &&
            s->ui_schedule.frames == emitted + 1,
        "a command overlay immediately takes host focus and input during graph "
        "panning"
    );
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frame(&f, false);
    performance_pointer_button(s, x, y, false);
    emitted = s->ui_schedule.frames;
    frame(&f, false);
    test_check(
        s->ui_schedule.frames == emitted + 1 &&
            s->graph.gesture == STUDIO_GRAPH_IDLE && !s->graph.captured &&
            !s->in.down[2] && !s->graph.in.down[2] && !kit_command_is_open(s->kit),
        "Escape and the captured pan release keep normal host and graph input "
        "lifecycles"
    );
    frames(&f, 90);

    published = s->perf_publications;
    studio_runtime(s, APP_RUNTIME_PLAY);
    frame(&f, false);
    test_check(s->perf_publish_playing && s->perf_publications == published + 1, "entering Play immediately publishes telemetry instead of waiting for the idle deadline");
    published = s->perf_publications;
    mouse_move(s, x + 1, y + 1);
    frame_delta(&f, false, 0.025);
    float spike_time = alast(s->perf).time;
    bool intermediate = s->perf_publications == published &&
                        fabsf(alast(s->perf).frame_ms - 25) < 0.001f;
    frames(&f, 2);
    bool retained = false;
    for (int i = 0; i < acount(s->perf_published); i++) {
        const studio_perf_sample *sample = &s->perf_published[i];
        retained |= sample->time == spike_time && fabsf(sample->frame_ms - 25) < 0.001f;
    }
    test_check(intermediate && retained && s->perf_publications > published, "a longer real frame between telemetry polls survives in the next complete published history");

    performance_pointer_button(s, x, y, true);
    frame(&f, false);
    recorded = acount(s->perf);
    published = s->perf_publications;
    emitted = s->ui_schedule.frames;
    bool playing_deadline = performance_pan_frames(&f, x, y);
    uint64_t playing_publications = s->perf_publications - published;
    test_check(
        playing_deadline && acount(s->perf) == recorded + 60 &&
            playing_publications >= 15 && playing_publications <= 21 &&
            s->ui_schedule.frames >= emitted + 15 &&
            s->ui_schedule.frames <= emitted + 29,
        "real playing panning retains full-rate camera input and recording "
        "while host inspection and telemetry publish at bounded rates"
    );
    performance_pointer_button(s, x, y, false);
    frame(&f, false);
    published = s->perf_publications;
    studio_runtime(s, APP_RUNTIME_STOP);
    frame(&f, false);
    test_check(
        !s->perf_publish_playing && s->perf_publications == published + 1,
        "stopping Play immediately publishes the idle telemetry mode"
    );
    fixture_free(&f);
}

/* Physical Tab commits the semantic row marked as selected after filtering,
   rather than relying on the visual resemblance of adjacent completion rows. */
static void graph_completion_tab_matches_highlight()
{
    studio_fixture f = fixture_open("projects/arena2d", "projects/arena2d/content/scenes/Sandbox.kdl", "Weapon");
    f.studio = studio_new(f.scene, f.viewport, "projects/arena2d");
    studio *s = f.studio;
    studio_maximize_graph(s);
    frames(&f, 3);
    node *expression = graph_node_by_id(sg_active_graph(s), 48);
    assert(expression);
    int pin = -1;
    for (int i = 0; i < expression->input_count; i++) {
        if (!strcmp(expression->input_pins[i].name, "elapsed")) {
            pin = i;
        }
    }
    assert(pin >= 0);
    char path[128];
    snprintf(path, sizeof path, "world/stage/n48/b/i%d/source-edit", pin);
    kit_rect rect;
    assert(kit_find(s->graph.kit, path, &rect));
    graph_press_rect(s, rect);
    frames(&f, 3);
    rml_context *context = rml_backend_context(s->graph.backend);
    rml_element *input = rml_context_focus_element(context);
    assert(input && !strcmp(rml_element_tag_name(input), "input"));
    rml_element_select(input);
    text_event("Tr");
    frames(&f, 3);
    mouse_move(
        s,
        s->graph.surface.rect.x + 20,
        s->graph.surface.rect.y + s->graph.surface.rect.h - 20
    );
    frames(&f, 24);
    rml_element *doc = rml_context_document(context, 0);
    rml_element *selected = rml_element_query_selector(doc, ".popup.autocomplete .menu-item[data-highlighted] .label");
    char label[256] = "";
    if (selected) {
        rml_element_inner_rml(selected, label, sizeof label);
    }
    char plain[256] = "";
    size_t length = 0;
    bool tag = false;
    for (const char *at = label; *at; at++) {
        if (*at == '<') {
            tag = true;
        } else if (*at == '>') {
            tag = false;
        } else if (!tag) {
            plain[length++] = *at;
        }
    }
    char expected[2] = { 0 };
    if (!strcmp(plain, "Transform \xE2\x80\xBA X")) {
        expected[0] = 'x';
    } else if (!strcmp(plain, "Transform \xE2\x80\xBA Y")) {
        expected[0] = 'y';
    }
    key_event(SDL_SCANCODE_TAB, true);
    frames(&f, 8);
    key_event(SDL_SCANCODE_TAB, false);
    frames(&f, 8);
    expression = graph_node_by_id(sg_active_graph(s), 48);
    const pin_binding *binding = graph_pin_binding(sg_active_graph(s), expression, 0, pin);
    bool same = selected && expected[0] && binding &&
                binding->kind == PIN_BINDING_COMPONENT_FIELD &&
                binding->component_key &&
                !strcmp(binding->component_key, "GAME.GameTransform") &&
                binding->field_name && !strcmp(binding->field_name, expected);
    if (!same) {
        fprintf(
            stderr,
            "completion Tab: highlighted=%s expected_field=%s "
            "accepted_component=%s accepted_field=%s\n",
            label,
            expected,
            binding && binding->component_key ? binding->component_key : "none",
            binding && binding->field_name ? binding->field_name : "none"
        );
    }
    test_check(same, "settled graph completion Tab accepts the displayed highlighted semantic field");
    fixture_free(&f);
}

static void replay_close_stop_reopen_keeps_graph_layout()
{
    studio_fixture f = fixture_open("projects/arena2d", "projects/arena2d/content/scenes/Sandbox.kdl", "Weapon");
    f.studio = studio_new(f.scene, f.viewport, "projects/arena2d");
    studio *s = f.studio;
    studio_maximize_graph(s);
    frames(&f, 3);
    graph *gr = sg_active_graph(s);
    uint64_t lifetime = gr->lifetime_id;
    uint64_t *ids = NULL;
    float *positions = NULL;
    float lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float box[4];
        if (!sg_node_visible(gr, n) || !sg_node_box(s, n, box)) {
            continue;
        }
        apush(ids, n->id);
        apush(positions, n->x);
        apush(positions, n->y);
        lo[0] = fminf(lo[0], box[0]);
        lo[1] = fminf(lo[1], box[1]);
        hi[0] = fmaxf(hi[0], box[0] + box[2]);
        hi[1] = fmaxf(hi[1], box[1] + box[3]);
    }
    assert(acount(ids) > 1 && hi[0] - lo[0] > 360);
    behavior_asset *before = behavior_asset_clone(editor_behavior_asset(f.graph));
    assert(before);
    assert(app_scene_authoring_open_behavior(f.scene, "Movement") == 0);
    bool pending = f.graph->arrange_pending;
    char *checkpoint = editor_checkpoint(f.graph);
    assert(checkpoint);
    bool checkpoint_restored = editor_restore_checkpoint(f.graph, checkpoint);
    free(checkpoint);
    test_check(pending && checkpoint_restored && f.graph->arrange_pending, "a native checkpoint taken before the first graph frame retains pending automatic arrangement");
    frames(&f, 3);
    assert(app_scenario_replay(f.scene, "Firefight") == 0);
    frames(&f, 3);
    int weapon = tab_named(f.scene, "Weapon");
    assert(weapon >= 0 && !scene_tabs_dirty(f.scene, &f.scene->tabs[weapon]));
    studio_close_tab(s, weapon);
    frames(&f, 2);
    int movement = tab_named(f.scene, "Movement");
    assert(movement >= 0 && !scene_tabs_dirty(f.scene, &f.scene->tabs[movement]));
    studio_close_tab(s, movement);
    frames(&f, 2);
    bool hidden = tab_named(f.scene, "Weapon") < 0 &&
                  tab_named(f.scene, "Movement") < 0 && !studio_graph_active(s);
    studio_runtime(s, APP_RUNTIME_STOP);
    frame(&f, false);
    assert(app_scene_authoring_open_behavior(f.scene, "Weapon") == 0);
    studio_maximize_graph(s);
    frames(&f, 3);
    gr = sg_active_graph(s);
    bool positions_equal = gr != NULL;
    for (int i = 0; i < acount(ids); i++) {
        node *n = gr ? graph_node_by_id(gr, ids[i]) : NULL;
        positions_equal &= n && fabsf(n->x - positions[i * 2]) < 0.5f &&
                           fabsf(n->y - positions[i * 2 + 1]) < 0.5f;
    }
    bool restored = hidden && gr && gr->lifetime_id != lifetime && positions_equal;
    char *before_text = behavior_asset_save(before);
    char *after_text = behavior_asset_save(editor_behavior_asset(f.graph));
    char error[256];
    char *before_disk = before_text
                          ? asset_text_to_disk(before_text, error, sizeof error)
                          : NULL;
    char *after_disk = after_text
                         ? asset_text_to_disk(after_text, error, sizeof error)
                         : NULL;
    bool authored_equal = before_disk && after_disk &&
                          !strcmp(before_disk, after_disk);
    free(before_text);
    free(after_text);
    free(before_disk);
    free(after_disk);
    test_check(
        restored && authored_equal,
        "Firefight replay, closing both graph tabs, Stop and reopening Weapon "
        "restore its automatic positions without authoring or saving an edit"
    );
    behavior_asset_free(before);

    sg_frame_nodes(s, false, false);
    frame(&f, false);
    bool fitted = true;
    for (int i = 0; i < acount(ids); i++) {
        float box[4];
        node *n = graph_node_by_id(gr, ids[i]);
        fitted &= sg_node_box(s, n, box) && box[0] >= s->graph.x - 1 &&
                  box[1] >= s->graph.y - 1 &&
                  box[0] + box[2] <= s->graph.x + s->graph.in.width + 1 &&
                  box[1] + box[3] <= s->graph.y + s->graph.in.height + 1;
    }
    bool wires = acount(s->graph.wire_edges) > 0;
    for (int w = 0; w < acount(s->graph.wire_edges); w++) {
        const edge *ed = &gr->edges[s->graph.wire_edges[w]];
        node *from, *to;
        int from_pin, to_pin;
        assert(sg_edge_ends(gr, ed, &from, &from_pin, &to, &to_pin));
        int count = kit_wire_points(s->graph.kit, "back/wires", w, NULL, 0);
        float *points = xmalloc((size_t)(count ? count : 1) * 2 * sizeof *points);
        kit_wire_points(s->graph.kit, "back/wires", w, points, count);
        float ax, ay, bx, by;
        sg_socket_at(s, from, ED_PIN_OUTPUT, from_pin, &ax, &ay);
        sg_socket_at(s, to, ED_PIN_INPUT, to_pin, &bx, &by);
        wires &= count >= 2 && fabsf(points[0] - ax + s->graph.x) < 0.5f &&
                 fabsf(points[1] - ay + s->graph.y) < 0.5f &&
                 fabsf(points[(count - 1) * 2] - bx + s->graph.x) < 0.5f &&
                 fabsf(points[(count - 1) * 2 + 1] - by + s->graph.y) < 0.5f;
        free(points);
    }
    test_check(fitted && wires, "Frame fits the restored graph spread and every rendered wire keeps its restored socket anchors");
    afree(ids);
    afree(positions);
    fixture_free(&f);
}

/* UI documents as tabs and in the Text Editor (test/editor/ui_documents.c). */
void test_ui_document_tabs();
/* Assets ▸ New and Settings ▸ Project (test/editor/studio_new_assets.c). */
void test_studio_new_assets();
/* Workstream I: the Text Editor's code intelligence
   (test/editor/code_intelligence_studio.c). */
void test_code_intelligence_studio();

/* ---------- UI wiring (workstream K) ---------- */

#define VAULT PROJECT "/content/scenes/Vault.kdl"
#define BANK PROJECT "/content/ui/Bank.rml"
#define BANK_STYLES PROJECT "/content/ui/Bank.rcss"

/* Studio over `project`'s scene `scene` with `behavior` open and the Scene
   filling the workspace. Preferences stay as saved but for the layout: UI
   Scale 1 and the default split modes, so the areas the checks click are the
   same whatever the user's. */
static studio_fixture scene_open(const char *project, const char *scene, const char *behavior)
{
    studio_fixture f = fixture_open(project, scene, behavior);
    f.studio = studio_new(f.scene, f.viewport, project);
    studio_prefs defaults = studio_prefs_default();
    studio_prefs *p = &f.studio->prefs;
    p->ui_scale = 1;
    p->workspace = defaults.workspace;
    memcpy(p->layouts, defaults.layouts, sizeof defaults.layouts);
    f.studio->applied = *p;
    f.studio->maximized = STUDIO_AREA_TOP;
    return f;
}

/* Studio over the Vault scene, whose Vault shows the Bank document and
   attaches Teller. */
static studio_fixture vault_open()
{
    return scene_open(PROJECT, VAULT, "Teller");
}

static void frames(studio_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        frame(f, false);
        /* The user's preferences stay as they are. */
        f->studio->prefs_changed_at = 0;
    }
}

static int bank_errors(const studio *s)
{
    int count = 0;
    for (int i = 0; i < acount(s->records); i++) {
        count += s->records[i].kind == KIT_TOAST_ERROR &&
                 strstr(s->records[i].text, "ui/Bank.rml") != NULL;
    }
    return count;
}

/* A broken ui asset's error shows in the Scene and Problems, and Output
   records it once per change. */
static void ui_errors_recorded()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 3);
    bool clean = !s->ui_error[0];
    text_overlay_set(BANK, "");
    frames(&f, 3);
    bool shown = strstr(s->ui_error, "ui/Bank.rml") != NULL && bank_errors(s) == 1;
    frames(&f, 3);
    bool once = bank_errors(s) == 1;
    text_overlay_set(BANK, "<div><body/></div>\n");
    frames(&f, 3);
    bool changed = bank_errors(s) == 2;
    text_overlay_set(BANK, NULL);
    frames(&f, 3);
    bool fixed = !s->ui_error[0] && bank_errors(s) == 2;
    if (!(clean && shown && once && changed && fixed)) {
        printf("  %d %d %d %d %d: %s\n", clean, shown, once, changed, fixed, s->ui_error);
    }
    test_check(
        clean && shown && once && changed && fixed,
        "a game UI error shows in the Scene and Output records it once "
        "per change"
    );
    fixture_free(&f);
}

/* Teller hears the Bank the Vault shows. */
static void ui_events_offered()
{
    studio_fixture f = vault_open();
    frames(&f, 3);
    ui_event *events = studio_behavior_ui_events(f.studio);
    bool ok = acount(events) == 2 && !strcmp(events[0].name, "Extract") &&
              !strcmp(events[0].description, "Sent when the player clicks #extract in Bank") &&
              !strcmp(events[1].name, "Close");
    test_check(
        ok,
        "the Add menu offers the events of the GameUi documents that "
        "reach the open behavior"
    );
    ui_events_free(events);
    fixture_free(&f);
}

static void click(render_ui *ui, float x, float y)
{
    rml_input_type types[] = { RML_INPUT_POINTER_MOVE,
                               RML_INPUT_POINTER_DOWN,
                               RML_INPUT_POINTER_UP };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        render_ui_input(ui, &(rml_input){ .type = types[i], .x = x, .y = y });
    }
}

/* The asset's root in the preview's documents, found from the element under
   the pointer. */
static rml_element *preview_root(render_ui *ui)
{
    rml_element *hover = rml_context_hover_element(render_ui_context(ui));
    return hover ? rml_element_query_selector(rml_element_owner_document(hover), "[data-ui=Bank]")
                 : NULL;
}

/* The preview shows the asset alone with the backdrop's class and logs the
   events it emits, which reach no behavior. */
static void preview_logs_actions()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 3);
    studio_preview_ui(s, "Bank");
    /* The stage lays out, then the frame's size holds for two draws. */
    frames(&f, 8);
    studio_canvas *c = s->canvas;
    render_ui *ui = engine_viewport_ui(c->viewport);
    click(ui, 5, 5);
    rml_element *root = preview_root(ui);
    bool alone = root && rml_element_query_selector_all(
                             rml_element_owner_document(root),
                             "[data-ui=Bank]",
                             NULL,
                             0
                         ) == 1;
    bool dark = root && rml_element_is_class_set(root, "dark");
    frames(&f, 1);
    bool logged = acount(c->actions) == 1 &&
                  !strcmp(c->actions[0].event, "Extract") &&
                  !strcmp(c->actions[0].element, "#extract");
    click(ui, 5, 5);
    c->light = true;
    frames(&f, 1);
    render_ui_input(ui, &(rml_input){ .type = RML_INPUT_POINTER_MOVE, .x = 5, .y = 5 });
    root = preview_root(ui);
    bool light = root && rml_element_is_class_set(root, "light") &&
                 !rml_element_is_class_set(root, "dark");
    bool counted = acount(c->actions) == 1 && c->actions[0].count == 2;
    studio_preview_ui(s, NULL);
    bool closed = !studio_previewed_ui(s);
    /* The scene's surface lays out again, then the closed preview frees. */
    frames(&f, 3);
    closed &= !s->canvas;
    studio_command_run(s, STUDIO_COMMAND_PREVIEW_UI);
    bool picking = s->pick.open && !strcmp(s->pick.key, STUDIO_PICK_PREVIEW) &&
                   kit_command_is_open(s->kit);
    if (!(picking && alone && dark && logged && light && counted && closed)) {
        printf("  %d %d %d %d %d %d %d\n", picking, alone, dark, logged, light, counted, closed);
    }
    test_check(
        picking && alone && dark && logged && light && counted && closed,
        "the Canvas preview shows a ui asset alone, themed, and logs the "
        "events it emits"
    );
    fixture_free(&f);
}

static bool preview_geometry_matches(studio *s)
{
    rml_element *image = s->canvas->frame;
    assert(image);
    rml_vec2 size = rml_element_box_size(image, RML_BOX_BORDER);
    float ratio = studio_pixel_ratio(s);
    int width, height;
    engine_viewport_pixel_size(s->canvas->viewport, &width, &height);
    return fabsf(size.x / ratio - s->surface.rect.w) <= 0.5f &&
           fabsf(size.y / ratio - s->surface.rect.h) <= 0.5f &&
           abs(width - (int)(s->surface.rect.w * s->prefs.ui_scale)) <= 1 &&
           abs(height - (int)(s->surface.rect.h * s->prefs.ui_scale)) <= 1;
}

/* Preview measurements must converge even when passive refresh is suspended. */
static void preview_geometry_settles_without_input()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 60);
    studio_preview_ui(s, "Bank");
    bool matched = true;
    const studio_canvas_size sizes[] = {
        STUDIO_CANVAS_FIT,
        STUDIO_CANVAS_PHONE,
        STUDIO_CANVAS_DESKTOP,
    };
    for (int size = 0; size < (int)(sizeof sizes / sizeof *sizes); size++) {
        s->canvas->size = sizes[size];
        s->ui_schedule.wake = true;
        frame(&f, false);
        /* No input or passive inspection should be needed to finish layout. */
        for (int i = 0; i < 10; i++) {
            s->ui_schedule.next = s->time + 10;
            frame(&f, false);
        }
        matched &= preview_geometry_matches(s);
    }
    test_check(matched, "Canvas preview texture and measured surface settle to the rendered aspect without input or passive refresh");
    s->maximized = -1;
    studio_active_layout(s)->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_SCENE;
    studio_active_layout(s)->editors[STUDIO_AREA_BOTTOM] = STUDIO_EDITOR_SCENE;
    frames(&f, 30);
    rml_element *panes[2];
    int count = rml_element_query_selector_all(
        rml_context_document(rml_backend_context(s->backend), 0),
        ".canvas-frame",
        panes,
        2
    );
    bool two = count == 2 && rml_element_is_visible(panes[0], false) &&
               rml_element_is_visible(panes[1], false) && panes[0] != panes[1];
    if (two) {
        rml_vec2 first = rml_element_absolute_offset(panes[0], RML_BOX_BORDER);
        rml_vec2 second = rml_element_absolute_offset(panes[1], RML_BOX_BORDER);
        two = fabsf(first.x - second.x) > 0.5f || fabsf(first.y - second.y) > 0.5f;
    }
    test_check(two && preview_geometry_matches(s), "two visible Scene panes reconcile the exact pane that supplies the shared Canvas surface");
    s->active_until = 0;
    s->ui_schedule.next = s->time;
    uint64_t before = s->ui_schedule.frames;
    frames(&f, 60);
    uint64_t emissions = s->ui_schedule.frames - before;
    test_check(
        emissions >= 3 && emissions < 10 && !s->ui_schedule.wake,
        "settled Canvas geometry keeps the idle emission schedule bounded"
    );
    fixture_free(&f);
}

/* The Scene area follows the active document: a ui asset's UI document
   shows its preview, and the scene's document shows the scene again. */
static void preview_follows_document()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 3);
    char path[PATH_MAX];
    assert(realpath(BANK, path));
    bool opened = studio_open_ui_source(s, path, 1);
    frames(&f, 2);
    bool shown = opened && studio_previewed_ui(s) &&
                 !strcmp(studio_previewed_ui(s), "Bank");
    int scene = -1;
    for (int i = 0; i < acount(f.scene->tabs); i++) {
        scene = f.scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE ? i : scene;
    }
    assert(scene >= 0 && scene_tabs_activate(f.scene, scene) == 0);
    frames(&f, 2);
    bool back = !studio_previewed_ui(s);
    test_check(
        shown && back,
        "the Scene area previews the active UI document's "
        "asset and returns to the scene with its document"
    );
    fixture_free(&f);
}

static bool logged_last(const studio *s, const char *event)
{
    const studio_canvas *c = s->canvas;
    return acount(c->actions) &&
           !strcmp(c->actions[acount(c->actions) - 1].event, event);
}

/* The elements of the Canvas's asset that `selector` matches, up to 8. */
static int canvas_elements(studio *s, const char *selector, rml_element **found)
{
    char query[192];
    snprintf(query, sizeof query, "[data-ui=%s] %s", s->canvas->asset, selector);
    rml_context *context = render_ui_context(engine_viewport_ui(s->canvas->viewport));
    return rml_element_query_selector_all(rml_context_document(context, 0), query, found, 8);
}

/* The variables the Canvas's documents bind. */
static yyjson_mut_val *canvas_variables(studio *s)
{
    render_ui *ui = engine_viewport_ui(s->canvas->viewport);
    return yyjson_mut_doc_get_root(render_ui_data(ui)->doc);
}

/* Studio over the Studio project's scene, whose root document is its
   manifest's, as the application opens it. */
static studio_fixture studio_project_open()
{
    static char root[PATH_MAX];
    char scene[PATH_MAX + 32];
    assert(realpath("projects/studio", root));
    snprintf(scene, sizeof scene, "%s/content/scenes/Studio.kdl", root);
    studio_fixture f = scene_open(root, scene, "Dismiss");
    app_scene_authoring_set_default_ui(f.scene, "StudioShell");
    return f;
}

/* With the Studio project open, its ui assets are Studio's chrome, so the
   Canvas previews them over Studio's own data: TopBar lists the document
   tabs and labels its commands, whose studio.* events only reach the
   Actions log. A game's preview binds only save and system. */
static void preview_binds_studio_data()
{
    studio_fixture f = studio_project_open();
    studio *s = f.studio;
    frames(&f, 3);
    studio_preview_ui(s, "TopBar");
    frames(&f, 8);
    rml_element *found[8];
    /* Besides data-for's template. */
    int tabs = canvas_elements(s, ".doc-tab", found) - 1;
    bool listed = s->own_project && tabs > 0 && tabs == acount(f.scene->tabs);
    char label[64] = "";
    if (canvas_elements(s, ".search-trigger .label", found) == 1) {
        rml_element_inner_rml(found[0], label, sizeof label);
    }
    bool labeled = !strcmp(
        label,
        studio_command_describe(s, STUDIO_COMMAND_SEARCH).label
    );
    canvas_elements(s, ".search-trigger", found);
    rml_element_click(found[0]);
    frames(&f, 2);
    bool logged = logged_last(s, "studio.search") && !kit_command_is_open(s->kit);
    fixture_free(&f);

    f = vault_open();
    frames(&f, 3);
    studio_preview_ui(f.studio, "Bank");
    frames(&f, 3);
    yyjson_mut_val *variables = canvas_variables(f.studio);
    bool game = yyjson_mut_obj_size(variables) == 3 &&
                yyjson_mut_obj_get(variables, "save") &&
                yyjson_mut_obj_get(variables, "system") &&
                yyjson_mut_obj_get(variables, "entities");
    if (!(listed && labeled && logged && game)) {
        printf("  %d %d %d %d: %d tabs, '%s'\n", listed, labeled, logged, game, tabs, label);
    }
    test_check(
        listed && labeled && logged && game,
        "a Studio-project Canvas preview binds Studio's data and only logs its "
        "commands; a game's binds save, system and entities' self data"
    );
    fixture_free(&f);
}

static void ui_wiring_checks()
{
    ui_errors_recorded();
    ui_events_offered();
    preview_logs_actions();
    preview_geometry_settles_without_input();
    preview_follows_document();
    preview_binds_studio_data();
}

/* ---------- Authored status bar (workstream J) ---------- */

static rml_element *status_bar(studio *s)
{
    rml_element *doc = rml_context_document(rml_backend_context(s->backend), 0);
    return rml_element_query_selector(doc, "[data-ui=StatusBar]");
}

/* The shown labels of the status bar's key hints, joined by '|'. */
static void status_hints(studio *s, char *out, size_t size)
{
    rml_element *labels[8];
    int count = rml_element_query_selector_all(status_bar(s), "[data-ui=KeyHint] .label", labels, 8);
    out[0] = '\0';
    for (int i = 0; i < count && i < 8; i++) {
        char text[64] = "";
        rml_element_inner_rml(labels[i], text, sizeof text);
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%s", used ? "|" : "", text);
    }
}

static bool toggle_on(studio *s)
{
    char state[8] = "";
    rml_element *assets = rml_element_query_selector(status_bar(s), ".toggle-drawer");
    rml_element_attribute(assets, "data-state", state, sizeof state);
    return !strcmp(state, "on");
}

/* Studio shows the authored status bar over its own state: the hovered
   area's hints, the last report, a drawer toggle that runs its command, and
   UI Scale native in its slot. */
static void status_bar_follows_studio()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 3);
    kit_rect area = s->areas[STUDIO_AREA_TOP].rect;
    s->in.mouse_x = area.x + area.w / 2;
    s->in.mouse_y = area.y + area.h / 2;
    frames(&f, 2);
    char scene[256], elsewhere[256];
    status_hints(s, scene, sizeof scene);
    s->in.mouse_x = s->in.mouse_y = 1;
    frames(&f, 2);
    status_hints(s, elsewhere, sizeof elsewhere);
    bool hints = !strcmp(scene, "Select|Grab|Snap|Frame") &&
                 !strcmp(elsewhere, "Select|Search|Shortcuts");

    studio_report(s, "Saved");
    frames(&f, 2);
    rml_element *report = rml_element_query_selector(status_bar(s), ".report");
    char said[32] = "";
    rml_element_attribute(rml_element_query_selector(report, "morph-text"), "aria-label", said, sizeof said);
    bool reported = rml_element_is_visible(report, false) && !strcmp(said, "Saved");
    s->status_time = s->time - 4;
    frames(&f, 2);
    reported &= rml_element_has_attribute(report, "data-ending-style") ||
                !rml_element_is_visible(report, false);

    int before = studio_command_checked(s, STUDIO_COMMAND_TOGGLE_ASSETS);
    bool shown_before = toggle_on(s) == (before == 1);
    rml_element_click(rml_element_query_selector(status_bar(s), ".toggle-drawer"));
    frames(&f, 2);
    int after = studio_command_checked(s, STUDIO_COMMAND_TOGGLE_ASSETS);
    bool toggled = shown_before && after != before && toggle_on(s) == (after == 1);

    kit_rect scale;
    bool native = kit_find(s->kit, "app/status/scale/scale", &scale);
    if (!(hints && reported && toggled && native)) {
        printf("  %d %d %d %d: %s / %s\n", hints, reported, toggled, native, scene, elsewhere);
    }
    test_check(
        hints && reported && toggled && native,
        "Studio's authored status bar follows the hovered area and reports, "
        "runs "
        "its drawer toggles' commands and hosts UI Scale natively"
    );
    fixture_free(&f);
}

/* ---------- Wave 2 follow-ups (workstream P) ---------- */

static void key_event_mod(SDL_Scancode key, bool down, SDL_Keymod mod, bool repeat)
{
    SDL_Event event = { .type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP };
    event.key.windowID = SDL_GetWindowID(cf_app_get_window());
    event.key.scancode = key;
    event.key.key = SDL_GetKeyFromScancode(key, mod, true);
    event.key.mod = mod;
    event.key.down = down;
    event.key.repeat = repeat;
    assert(SDL_PushEvent(&event));
}

static void key_event(SDL_Scancode key, bool down)
{
    key_event_mod(key, down, SDL_KMOD_NONE, false);
}

/* The Canvas preview takes the keys typed while its area has the keyboard,
   so the asset's data-keys bindings send their events, and not while
   another area has it. */
static void preview_hears_keys()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    text_overlay_set(BANK, "<div id=\"extract\" autofocus data-keys=\"C:Close\">Extract</div>\n");
    text_overlay_set(BANK_STYLES, "#extract { display: block; height: 100%; tab-index: auto; }\n");
    frames(&f, 3);
    studio_preview_ui(s, "Bank");
    frames(&f, 8);
    s->focus_area = STUDIO_AREA_BOTTOM;
    key_event(SDL_SCANCODE_C, true);
    key_event(SDL_SCANCODE_C, false);
    frames(&f, 2);
    bool elsewhere = !logged_last(s, "Close");
    s->focus_area = STUDIO_AREA_TOP;
    key_event(SDL_SCANCODE_C, true);
    key_event(SDL_SCANCODE_C, false);
    frames(&f, 2);
    bool heard = logged_last(s, "Close");
    /* Studio keeps its keys there: Space still plays the scene. */
    key_event(SDL_SCANCODE_SPACE, true);
    key_event(SDL_SCANCODE_SPACE, false);
    frames(&f, 2);
    bool shortcut = app_scene_authoring_runtime_state(f.scene).playing;
    studio_runtime(s, APP_RUNTIME_STOP);
    test_check(
        elsewhere && heard && shortcut,
        "the focused Canvas preview takes keys, so data-keys bindings "
        "fire, and Studio keeps its shortcuts"
    );
    studio_preview_ui(s, NULL);
    frames(&f, 3);
    game_entities *entities = f.scene->live->entities;
    int before = game_entities_pending_events(entities);
    key_event(SDL_SCANCODE_C, true);
    key_event(SDL_SCANCODE_C, false);
    frames(&f, 2);
    bool editing = game_entities_pending_events(entities) == before;
    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 3);
    before = game_entities_pending_events(entities);
    key_event(SDL_SCANCODE_C, true);
    key_event(SDL_SCANCODE_C, false);
    /* The key reaches the document, whose activation reaches Vault a frame
       later, before the next scene frame runs it. */
    frames(&f, 2);
    bool playing = game_entities_pending_events(entities) == before + 1;
    test_check(editing && playing, "the focused Scene's game documents take keys while the scene plays");
    text_overlay_set(BANK, NULL);
    text_overlay_set(BANK_STYLES, NULL);
    fixture_free(&f);
}

/* The first element in Studio's documents that `selector` matches. */
static rml_element *studio_element(studio *s, const char *selector)
{
    rml_context *context = rml_backend_context(s->backend);
    rml_element *found = NULL;
    for (int i = 0; !found && i < rml_context_document_count(context); i++) {
        found = rml_element_query_selector(rml_context_document(context, i), selector);
    }
    return found;
}

static void text_event(const char *text);
static void tap(SDL_Scancode key);

/* Recording consumes key and text events before the normal shortcut and
   game-input paths. A repeat must leave the recorder active for the next
   non-repeat keydown. */
static void settings_records_shortcut()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    studio_ui_show(s, "Settings");
    frames(&f, 4);
    rml_element *search_field = studio_element(s, ".search-input");
    key_event(SDL_SCANCODE_SPACE, true);
    key_event(SDL_SCANCODE_SPACE, false);
    frames(&f, 2);
    bool text_field_kept = search_field &&
                           !app_scene_authoring_runtime_state(f.scene).playing;

    /* The Settings page test covers the Record control. Here, exercise the
       host after that control has requested a recording. */
    s->shortcut_recording = STUDIO_COMMAND_SEARCH + 1;
    bool recording = s->shortcut_recording == STUDIO_COMMAND_SEARCH + 1;
    SDL_Event lost = { .type = SDL_EVENT_WINDOW_FOCUS_LOST };
    lost.window.windowID = SDL_GetWindowID(cf_app_get_window());
    assert(SDL_PushEvent(&lost));
    frames(&f, 1);
    bool focus_canceled = recording && s->shortcut_recording == 0 &&
                          !s->prefs.shortcuts[STUDIO_COMMAND_SEARCH][0];
    lost.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    assert(SDL_PushEvent(&lost));
    frames(&f, 1);
    s->shortcut_recording = STUDIO_COMMAND_SEARCH + 1;
    recording = s->shortcut_recording == STUDIO_COMMAND_SEARCH + 1;

    SDL_Keymod primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    SDL_Scancode primary_key = s->mac ? SDL_SCANCODE_LGUI : SDL_SCANCODE_LCTRL;
    key_event_mod(primary_key, true, primary, false);
    key_event_mod(SDL_SCANCODE_P, true, primary | SDL_KMOD_SHIFT, true);
    key_event_mod(SDL_SCANCODE_P, false, primary | SDL_KMOD_SHIFT, false);
    frames(&f, 2);
    bool repeat_ignored = recording &&
                          s->shortcut_recording == STUDIO_COMMAND_SEARCH + 1;
    text_event("p");
    key_event_mod(SDL_SCANCODE_P, true, primary | SDL_KMOD_SHIFT, false);
    key_event_mod(SDL_SCANCODE_P, false, primary | SDL_KMOD_SHIFT, false);
    key_event_mod(primary_key, false, SDL_KMOD_NONE, false);
    frames(&f, 3);
    bool captured = s->shortcut_recording == 0 &&
                    !strcmp(s->prefs.shortcuts[STUDIO_COMMAND_SEARCH], "Mod+Shift+P");
    if (!(text_field_kept && focus_canceled && repeat_ignored && captured)) {
        printf(
            "  %d %d %d %d (recording %d)\n",
            text_field_kept,
            focus_canceled,
            repeat_ignored,
            captured,
            s->shortcut_recording
        );
    }
    test_check(
        text_field_kept && focus_canceled && repeat_ignored && captured,
        "Settings text focus blocks Play; focus loss cancels recording, which "
        "ignores repeats and captures a chord through the host input queue"
    );
    fixture_free(&f);
}

/* The palette finds settings: a switch flips where it is found, and any
   other setting opens Settings searching for it. */
static void palette_finds_settings()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 2);
    bool vim = s->prefs.code_vim;
    studio_command_run(s, STUDIO_COMMAND_SEARCH);
    frames(&f, 2);
    text_event("vim");
    frames(&f, 2);
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool flipped = s->prefs.code_vim == !vim && !kit_command_is_open(s->kit) &&
                   studio_element(s, ".toast");
    studio_command_run(s, STUDIO_COMMAND_SEARCH);
    frames(&f, 2);
    text_event("wire style");
    frames(&f, 2);
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 4);
    rml_element *search = studio_element(s, ".search-input");
    char query[64] = "";
    if (search) {
        rml_element_value(search, query, sizeof query);
    }
    bool searched = !strcmp(query, "Wire Style");
    if (!(flipped && searched)) {
        printf("  %d %d '%s'\n", flipped, searched, query);
    }
    test_check(
        flipped && searched,
        "the palette flips a switch setting where it "
        "finds it and opens Settings at any other"
    );
    fixture_free(&f);
}

/* The palette's first row's label, without its highlighting, and the group
   it heads, or "". */
static void first_row(studio *s, char *label, char *group, size_t size)
{
    label[0] = group[0] = '\0';
    char rml[256] = "";
    rml_element *row = studio_element(s, ".cmd-item .label");
    if (row) {
        rml_element_inner_rml(row, rml, sizeof rml);
    }
    size_t n = 0;
    bool tag = false;
    for (const char *c = rml; *c && n + 1 < size; c++) {
        tag = *c == '<' || (tag && *c != '>');
        if (!tag && *c != '>') {
            label[n++] = *c;
        }
    }
    label[n] = '\0';
    rml_element *heading = studio_element(s, ".cmd-group");
    if (heading) {
        rml_element_inner_rml(heading, group, size);
    }
}

static void search_for(studio_fixture *f, const char *query)
{
    /* The last palette finishes leaving first. */
    frames(f, 20);
    studio_command_run(f->studio, STUDIO_COMMAND_SEARCH);
    frames(f, 2);
    if (query[0]) {
        text_event(query);
    }
    frames(f, 2);
}

/* The palette puts what was chosen lately first, alone while nothing is
   typed and above equal matches after; '>' keeps commands and settings, '@'
   the open behavior's graphs and nodes, and '?' lists the prefixes, one of
   which types itself. */
static void palette_recent_and_prefixes()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    studio_prefs saved = s->prefs;
    memset(s->prefs.recent, 0, sizeof s->prefs.recent);
    frames(&f, 2);
    char label[128], group[64];
    search_for(&f, "toggle");
    first_row(s, label, group, sizeof label);
    bool unranked = !strcmp(label, "Toggle Assets");
    tap(SDL_SCANCODE_DOWN);
    tap(SDL_SCANCODE_DOWN);
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool remembered = !strcmp(s->prefs.recent[0].key, "command:togglePerformance") &&
                      s->prefs.recent[0].uses == 1;
    search_for(&f, "toggle");
    first_row(s, label, group, sizeof label);
    bool boosted = !strcmp(label, "Toggle Performance");
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    search_for(&f, "");
    first_row(s, label, group, sizeof label);
    bool recent_first = !strcmp(label, "Toggle Performance") &&
                        !strcmp(group, "Recent");
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);

    search_for(&f, ">weapons");
    first_row(s, label, group, sizeof label);
    bool commands_only = strcmp(group, "Assets") != 0;
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    search_for(&f, "@");
    first_row(s, label, group, sizeof label);
    bool symbols = !strcmp(group, "In This Behavior") && label[0];
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    search_for(&f, "?");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    bool prefix = kit_command_is_open(s->kit) &&
                  !strcmp(kit_command_query(s->kit), ">");
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    if (!(unranked && remembered && boosted && recent_first && commands_only &&
          symbols && prefix)) {
        printf(
            "  %d %d %d %d %d %d %d '%s' '%s'\n",
            unranked,
            remembered,
            boosted,
            recent_first,
            commands_only,
            symbols,
            prefix,
            label,
            group
        );
    }
    test_check(
        unranked && remembered && boosted && recent_first,
        "the palette ranks what was chosen lately first, and leads with "
        "it while nothing is typed"
    );
    test_check(
        commands_only && symbols && prefix,
        "'>' keeps commands and settings, '@' the open behavior's "
        "symbols, and '?' types a prefix"
    );
    s->prefs = saved;
    s->applied = saved;
    fixture_free(&f);
}

/* The rows data-for made in the element at `selector`, leaving out its
   template, and the first one's text. */
static int list_rows(studio *s, const char *selector, char *first, size_t size)
{
    first[0] = '\0';
    rml_element *window = studio_element(s, selector);
    int rows = 0;
    for (int i = 0; window && i < rml_element_child_count(window); i++) {
        rml_element *row = rml_element_child(window, i);
        if (rml_element_has_attribute(row, "data-for") ||
            !rml_element_is_visible(row, false)) {
            continue;
        }
        if (!rows++) {
            rml_element_inner_rml(row, first, size);
        }
    }
    return rows;
}

/* The widget gallery's VirtualList, driven by the VirtualList and
   WidgetGallery behaviors, makes only the rows in view of ten thousand, and
   scrolling shows the rows there. */
/* The index is rebuilt in place even when no dirty-generation change preceded
 * an explicit refresh. Numeric row caches must follow completed rebuilds. */
static void assets_cache_follows_explicit_refresh()
{
    studio_fixture f = studio_project_open();
    studio *s = f.studio;
    s->maximized = STUDIO_AREA_TOP;
    studio_active_layout(s)->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_ASSETS;
    frames(&f, 6);
    reference_index_add_asset(f.scene->asset_index, REFERENCE_ASSET_BEHAVIOR, "QaRefreshCache", NULL, "qa-cache-probe");
    /* The fixture inserts directly into the index; start its cached model
     * afresh, as a completed rebuild would. */
    studio_assets_free(s);
    snprintf(s->assets_filter, sizeof s->assets_filter, "QaRefreshCache");
    s->ui_schedule.wake = true;
    frames(&f, 3);
    char first[256];
    int rows = list_rows(s, ".assets-list .vlist-content", first, sizeof first);
    bool found = rows == 2 && strstr(first, "Behaviors");
    unsigned generation = f.scene->asset_index_generation;
    uint64_t revision = f.scene->asset_index_revision;
    s->ui_schedule.next = s->time + 1;
    s->active_until = 0;
    uint64_t emitted = s->ui_schedule.frames;
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    frame(&f, false);
    rows = list_rows(s, ".assets-list .vlist-content", first, sizeof first);
    if (!found || rows || generation != f.scene->asset_index_generation ||
        f.scene->asset_index_revision != revision + 1 ||
        s->ui_schedule.frames != emitted + 1) {
        printf(
            "  Assets cache: found=%d rows=%d generation=%u/%u "
            "revision=%llu/%llu emissions=%llu/%llu first='%s'\n",
            found,
            rows,
            generation,
            f.scene->asset_index_generation,
            (unsigned long long)revision,
            (unsigned long long)f.scene->asset_index_revision,
            (unsigned long long)emitted,
            (unsigned long long)s->ui_schedule.frames,
            first
        );
    }
    test_check(
        found && !rows && generation == f.scene->asset_index_generation &&
            f.scene->asset_index_revision == revision + 1 &&
            s->ui_schedule.frames == emitted + 1,
        "Assets discards cached rows after an explicit index rebuild with "
        "unchanged dirty generation"
    );
    fixture_free(&f);
}

static void virtual_list_rows()
{
    studio_fixture f = studio_project_open();
    studio *s = f.studio;
    s->maximized = -1;
    studio_layout *layout = studio_active_layout(s);
    layout->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_PANEL;
    snprintf(layout->panels[STUDIO_AREA_TOP], sizeof layout->panels[0], "Widgets");
    frames(&f, 12);
    char first[96];
    int rows = list_rows(s, ".vlist-window", first, sizeof first);
    bool windowed = rows > 10 && rows < 60 && !strcmp(first, "Row 1 of 10000");
    rml_element *view = studio_element(s, ".vlist");
    if (view) {
        rml_element_set_scroll_top(view, 24 * 500 * studio_pixel_ratio(s));
    }
    frames(&f, 6);
    int scrolled_rows = list_rows(s, ".vlist-window", first, sizeof first);
    bool scrolled = scrolled_rows > 10 && scrolled_rows < 60 &&
                    !strcmp(first, "Row 495 of 10000");
    if (!(windowed && scrolled)) {
        printf("  %d %d rows %d then %d, first '%s'\n", windowed, scrolled, rows, scrolled_rows, first);
    }
    test_check(
        windowed && scrolled,
        "a VirtualList of ten thousand rows makes only those in view, and "
        "scrolling shows the rows there"
    );
    fixture_free(&f);
}

/* Runs one key press as Studio's shortcuts do, over area `hover`. */
static void press_over(studio *s, int hover, SDL_Keycode key, SDL_Keymod mod)
{
    s->hover_area = hover;
    asetlen(s->keys, 0);
    apush(s->keys, ((studio_key){ key, mod, false }));
    studio_shortcuts(s, false);
    asetlen(s->keys, 0);
}

/* Keys act on the area under the pointer (Blender): Undo there undoes the
   graph although another area was clicked last, Find opens the graph's,
   and a menu's Undo, with no key, still follows the area clicked last. */
static void keys_follow_pointer()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 3);
    int graph = s->graph.area;
    assert(graph >= 0);
    editor *e = f.graph;
    int count = acount(editor_active_graph(e)->nodes);
    assert(count > 0);
    ed_clear_selection(e);
    ed_add_selected(e, editor_active_graph(e)->nodes[0]);
    ed_duplicate_selected(e);
    bool duplicated = acount(editor_active_graph(e)->nodes) > count;
    int other = graph == STUDIO_AREA_OUTLINER ? STUDIO_AREA_PROPERTIES
                                              : STUDIO_AREA_OUTLINER;
    s->focus_area = other;
    bool menu_follows_click = studio_command_area(s) == other;
    SDL_Keymod primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    press_over(s, graph, SDLK_Z, primary);
    bool undone = acount(editor_active_graph(e)->nodes) == count;
    press_over(s, graph, SDLK_F, primary);
    bool found = s->graph.find.open;
    test_check(
        duplicated && menu_follows_click && undone && found && s->key_area < 0,
        "Undo and Find from the keyboard act on the area under the pointer, "
        "not the one clicked last"
    );
    fixture_free(&f);
}

/* Repeats remain limited to commands that are useful while held. */
static void shortcut_repeat_policy()
{
    studio_fixture zoom = fixture_new();
    studio *s = zoom.studio;
    SDL_Keymod primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    snprintf(s->prefs.shortcuts[STUDIO_COMMAND_ZOOM_IN], STUDIO_CHORD_SIZE, "Mod+F12");
    s->prefs.ui_scale = 1;
    apush(s->keys, ((studio_key){ SDLK_F12, primary, true }));
    studio_shortcuts(s, false);
    float held_scale = s->prefs.ui_scale;
    asetlen(s->keys, 0);
    apush(s->keys, ((studio_key){ SDLK_F12, primary, false }));
    studio_shortcuts(s, false);
    bool zoom_repeat_runs = held_scale > 1;
    bool zoom_press_runs = s->prefs.ui_scale > held_scale;
    fixture_free(&zoom);

    studio_fixture search = fixture_new();
    s = search.studio;
    primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    apush(s->keys, ((studio_key){ SDLK_K, primary, true }));
    studio_shortcuts(s, false);
    bool toggle_repeat_ignored = !kit_command_is_open(s->kit);
    asetlen(s->keys, 0);
    apush(s->keys, ((studio_key){ SDLK_K, primary, false }));
    studio_shortcuts(s, false);
    bool toggle_press_runs = kit_command_is_open(s->kit);
    test_check(
        zoom_repeat_runs && zoom_press_runs && toggle_repeat_ignored && toggle_press_runs,
        "repeatable zoom runs on a held shortcut while the Search toggle "
        "waits for a fresh press"
    );
    fixture_free(&search);
}

/* Whether the Scene's last draw framed the camera it holds now, as the
   pointer it measured shows. */
static bool drew_camera(const studio_fixture *f)
{
    engine_viewport_camera camera;
    engine_viewport_pointer pointer;
    engine_viewport_get_camera(f->viewport, &camera);
    engine_viewport_get_pointer(f->viewport, &pointer);
    float width = f->studio->surface.rect.w * f->studio->prefs.ui_scale;
    return fabsf(pointer.world_per_pixel * width - camera.span_width) <
           0.01f * camera.span_width;
}

/* The Scene draws the camera its first draw fits to the Vault, which the
   reset camera does not frame, and its zoom readout shows nothing before
   then and that camera's zoom after, so it never rolls from the reset
   camera's. */
static void readout_follows_camera()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    bool shown = false, current = true, drawn = true;
    for (int i = 0; i < 6; i++) {
        frames(&f, 1);
        drawn &= f.scene->camera_target < 0 || drew_camera(&f);
        rml_element *readout = studio_element(s, ".readout");
        if (!readout) {
            continue;
        }
        engine_viewport_view view;
        engine_viewport_get_view(f.viewport, &view);
        char value[32], expected[32];
        rml_element_attribute(readout, "aria-label", value, sizeof value);
        snprintf(expected, sizeof expected, "%d%%", (int)roundf(s->surface.rect.w / view.width * 100));
        current &= !strcmp(value, expected);
        shown = true;
    }
    test_check(
        shown && current && drawn,
        "the Scene draws the camera it fits from its first frame, and its "
        "zoom readout shows that camera"
    );
    fixture_free(&f);
}

/* ---------- Authored top bar and area headers (workstream T) ---------- */

/* The elements of Studio's `panel` that `selector` matches, up to 8. */
static int panel_elements(studio *s, const char *panel, const char *selector, rml_element **found)
{
    char query[128];
    snprintf(query, sizeof query, "[data-ui=%s] %s", panel, selector);
    rml_element *doc = rml_context_document(rml_backend_context(s->backend), 0);
    return rml_element_query_selector_all(doc, query, found, 8);
}

/* Find a Properties row by its visible label so the test follows the authored
   pane's slots instead of depending on a generated kit path. */
static rml_element *property_row(studio *s, const char *label)
{
    rml_context *context = rml_backend_context(s->backend);
    for (int d = 0; d < rml_context_document_count(context); d++) {
        rml_element *rows[128];
        int count = rml_element_query_selector_all(
            rml_context_document(context, d),
            "[data-ui=Properties] .prop",
            rows,
            128
        );
        for (int i = 0; i < count; i++) {
            rml_element *text = rml_element_query_selector(rows[i], ".label span");
            char shown[128] = "";
            if (text && rml_element_inner_rml(text, shown, sizeof shown) &&
                !strcmp(shown, label)) {
                return rows[i];
            }
        }
    }
    return NULL;
}

/* Keep the selected actor's Object fields visible when this fixture also has
   an active graph node, which normally drives the inspector to Node. */
static void show_object_properties(studio *s)
{
    studio_show_editor(s, STUDIO_EDITOR_PROPERTIES);
    s->shown_node = studio_graph_node(s) ? studio_graph_node(s)
                                         : studio_graph_comment(s);
    s->shown_member = studio_member_shown(s);
    s->shown_actor = s->scene->selected;
    s->tab = STUDIO_TAB_OBJECT;
}

/* Optional visual evidence for the focused Properties run. */
static void capture_properties_screenshot(studio_fixture *f, const char *name)
{
    const char *directory = getenv("CHIBI_PROPERTIES_CAPTURE_DIR");
    if (!directory || !directory[0]) {
        return;
    }
    frames(f, 12);
    char path[PATH_MAX];
    int length = snprintf(path, sizeof path, "%s/%s.png", directory, name);
    assert(length > 0 && (size_t)length < sizeof path);
    assert(app_save_screenshot(path) == 0);
}

/* Compute a line's baseline from the same face metrics RmlUi uses to draw it.
   Comparing a property row's outer boxes misses differences in line-height. */
static float text_baseline(rml_element *line)
{
    rml_vec2 at = rml_element_absolute_offset(line, RML_BOX_CONTENT);
    rml_vec2 size = rml_element_box_size(line, RML_BOX_CONTENT);
    uintptr_t face = rml_font_face("inter", RML_FONT_NORMAL, 400, 12);
    assert(face);
    rml_font_metrics metrics = rml_font_face_metrics(face);
    return at.y + (size.y - metrics.line_spacing) * 0.5f + metrics.ascent;
}

/* A press and release on `el`, which focuses it as a click does. Input is
   in dp, and the element's box in pixels. */
static void press(studio *s, rml_element *el)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    float ratio = studio_pixel_ratio(s);
    rml_input input = { .x = (at.x + size.x * 0.5f) / ratio,
                        .y = (at.y + size.y * 0.5f) / ratio };
    rml_input_type types[] = { RML_INPUT_POINTER_MOVE,
                               RML_INPUT_POINTER_DOWN,
                               RML_INPUT_POINTER_UP };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        input.type = types[i];
        rml_backend_process_input(s->backend, &input);
    }
}

static int tab_named(const app_scene_authoring *scene, const char *label)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene->tabs[i].label && !strcmp(scene->tabs[i].label, label)) {
            return i;
        }
    }
    return -1;
}

static void settings_reopens_after_font_change()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    char path[PATH_MAX];
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    studio_set_workspace(s, STUDIO_WORKSPACE_HORIZONTAL_SPLIT);
    s->prefs.code_font_family = 0;
    s->prefs.code_font_size = 13;
    frames(&f, 3);
    bool reopened = true;
    for (int i = 0; i < 3 && reopened; i++) {
        rml_element *gear = studio_element(s, "[data-ui=TopBar] [data-tip=Settings]");
        assert(gear);
        press(s, gear);
        frames(&f, 3);
        rml_element *close = studio_element(s, "[data-ui=Settings] .close");
        reopened = close && rml_element_is_visible(close, true) &&
                   studio_element(s, "[data-ui=Settings] .search-input");
        if (!reopened) {
            break;
        }
        s->prefs.code_font_family = (i + 1) % 2;
        frames(&f, 2);
        press(s, close);
        frames(&f, 3);
        reopened = !studio_element(s, ".modal-host");
    }
    test_check(
        reopened,
        "Settings closes and reopens through the top bar after "
        "changing the monospace font with a code document open"
    );
    fixture_free(&f);
}

/* Studio shows its authored top bar and area headers over its own state:
   the menubar, the play target and each area's editor type menu and
   controls in their native slots, the document tabs, whose faces and close
   buttons run their commands with the tab's index, the transport's
   commands, and each header's Maximize. */
static void chrome_follows_studio()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    char path[PATH_MAX];
    /* The Text Editor shows the document, so the Scene gives up the workspace. */
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    frames(&f, 3);
    kit_rect r;
    rml_element *heads[8], *tabs[8], *play[8];
    int shown = panel_elements(s, "AreaHeader", ".area-head", heads);
    bool slots = kit_find(s->kit, "app/topbar/menus/file", &r) &&
                 kit_find(s->kit, "app/topbar/target/target", &r) &&
                 kit_find(s->kit, "app/workspace/main/editors/top/head/type/type", &r) &&
                 kit_find(s->kit, "app/workspace/main/editors/top/head/controls", &r) &&
                 s->maximized < 0 && shown >= STUDIO_AREA_COUNT - 1;
    int count = panel_elements(s, "TopBar", ".doc-tab", tabs) - 1;
    int bank = tab_named(f.scene, "Bank"), scene = bank == 0 ? 1 : 0;
    rml_element *close = bank >= 0
                           ? rml_element_query_selector(tabs[bank], ".doc-close")
                           : NULL;
    bool listed = count == acount(f.scene->tabs) && f.scene->active_tab == bank &&
                  close && rml_element_is_visible(close, true) &&
                  !rml_element_is_visible(rml_element_query_selector(tabs[scene], ".doc-close"), true);
    /* The press takes the keyboard from the Text Editor, whose focused text
       would make its document active again. */
    press(s, rml_element_query_selector(tabs[scene], ".face"));
    frames(&f, 2);
    bool selected = f.scene->active_tab == scene &&
                    kit_wants_keyboard(s->kit) && studio_ui_shortcut_focus(s);
    /* This narrow window clips the last tab, so the click goes to it. */
    rml_element_click(close);
    frames(&f, 2);
    bool closed = tab_named(f.scene, "Bank") < 0 &&
                  panel_elements(s, "TopBar", ".doc-tab", tabs) - 1 ==
                      acount(f.scene->tabs);

    panel_elements(s, "TopBar", ".group > .btn", play);
    rml_element_click(play[0]);
    frames(&f, 2);
    bool playing = app_scene_authoring_runtime_state(f.scene).playing;
    studio_runtime(s, APP_RUNTIME_STOP);
    frames(&f, 1);

    rml_element_click(rml_element_query_selector(heads[0], ".maximize"));
    frames(&f, 2);
    bool maximized = s->maximized == STUDIO_AREA_TOP &&
                     panel_elements(s, "AreaHeader", ".area-head", heads) == 1;
    rml_element_click(rml_element_query_selector(heads[0], ".maximize"));
    frames(&f, 2);
    bool restored = maximized && s->maximized < 0 &&
                    panel_elements(s, "AreaHeader", ".area-head", heads) == shown;
    if (!(slots && listed && selected && closed && playing && restored)) {
        printf("  %d %d %d %d %d %d\n", slots, listed, selected, closed, playing, restored);
    }
    test_check(
        slots && listed && selected && closed && playing && restored,
        "Studio's authored top bar and area headers host their native "
        "slots and run tab, transport and area commands"
    );
    fixture_free(&f);
}

/* Global shortcuts remain available after authored toolbar controls take Rml
   focus, while an authored text input keeps its keys. */
static void toolbar_shortcuts_keep_text_focus()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    char path[PATH_MAX];
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    frames(&f, 4);
    rml_context *context = rml_backend_context(s->backend);
    rml_element *button = studio_element(s, ".side-pane");
    assert(context && button);
    press(s, button);
    frames(&f, 1);
    studio_command_run(s, STUDIO_COMMAND_TOGGLE_SIDE_PANE);
    frames(&f, 1);
    /* Toolbar actions can remount the authored header; focus the button
       from the reconciled document before sending the shortcut. */
    button = studio_element(s, ".side-pane");
    assert(button);
    rml_element_focus(button, true);
    frames(&f, 1);
    bool button_allowed = studio_ui_shortcut_focus(s);
    bool button_keyboard = kit_wants_keyboard(s->kit);
    bool button_focus = button_allowed && button_keyboard;

    SDL_Keymod primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    key_event_mod(SDL_SCANCODE_K, true, primary, false);
    key_event_mod(SDL_SCANCODE_K, false, primary, false);
    frames(&f, 2);
    bool button_shortcut = kit_command_is_open(s->kit);
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 2);
    bool dismissed = !kit_command_is_open(s->kit);

    rml_element *tab = studio_element(s, ".doc-tab .face");
    assert(tab);
    press(s, tab);
    frames(&f, 1);
    tab = studio_element(s, ".doc-tab");
    assert(tab && rml_element_focus(tab, true));
    frames(&f, 1);
    bool tab_focus = rml_context_focus_element(context) == tab &&
                     studio_ui_shortcut_focus(s) && kit_wants_keyboard(s->kit);
    key_event_mod(SDL_SCANCODE_K, true, primary, false);
    key_event_mod(SDL_SCANCODE_K, false, primary, false);
    frames(&f, 2);
    bool tab_shortcut = kit_command_is_open(s->kit);
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 2);

    rml_element *input = studio_element(s, "input[placeholder=Filter]");
    assert(input);
    bool focused_input = rml_element_focus(input, true);
    frames(&f, 1);
    bool input_keyboard = kit_wants_keyboard(s->kit);
    bool input_allowed = studio_ui_shortcut_focus(s);
    bool input_focus = focused_input && rml_context_focus_element(context) == input &&
                       input_keyboard && !input_allowed;
    /* A plain key stays in the field: Space types, not Play. */
    key_event(SDL_SCANCODE_SPACE, true);
    key_event(SDL_SCANCODE_SPACE, false);
    frames(&f, 2);
    rml_element *input_focus_after = rml_context_focus_element(context);
    bool input_protected = !app_scene_authoring_runtime_state(s->scene).playing &&
                           input_focus_after == input && kit_wants_keyboard(s->kit);
    /* A chord the field edits no text with still reaches Studio. */
    key_event_mod(SDL_SCANCODE_K, true, primary, false);
    key_event_mod(SDL_SCANCODE_K, false, primary, false);
    frames(&f, 2);
    bool input_palette = kit_command_is_open(s->kit);
    input_protected &= input_palette;

    if (!(button_focus && button_shortcut && dismissed && tab_focus &&
          tab_shortcut && input_focus && input_protected)) {
        printf("  button allowed %d, keyboard %d\n", button_allowed, button_keyboard);
        printf("  tab focus %d, shortcut %d\n", tab_focus, tab_shortcut);
        printf(
            "  %d %d %d %d %d (open %d, focus %s, input %d, allowed %d, "
            "keyboard %d, attempted %d, typing %d)\n",
            button_focus,
            button_shortcut,
            dismissed,
            input_focus,
            input_protected,
            input_palette,
            input_focus_after ? rml_element_tag_name(input_focus_after) : "none",
            input_focus_after == input,
            input_allowed,
            input_keyboard,
            focused_input,
            s->typing
        );
    }
    test_check(
        button_focus && button_shortcut && dismissed && tab_focus &&
            tab_shortcut && input_focus && input_protected,
        "focused authored toolbar buttons and document tabs allow global "
        "shortcuts, and text inputs keep their keys but pass Studio's other "
        "chords"
    );
    fixture_free(&f);
}

static bool beside(const studio *s)
{
    kit_rect top = s->areas[STUDIO_AREA_TOP].rect,
             bottom = s->areas[STUDIO_AREA_BOTTOM].rect;
    return bottom.w > 0 && fabsf(bottom.y - top.y) < 1 && bottom.x >= top.x + top.w;
}

static bool stacked(const studio *s)
{
    kit_rect top = s->areas[STUDIO_AREA_TOP].rect,
             bottom = s->areas[STUDIO_AREA_BOTTOM].rect;
    return bottom.w > 0 && fabsf(bottom.x - top.x) < 1 && bottom.y >= top.y + top.h;
}

static bool side_shown(const studio *s)
{
    return s->areas[STUDIO_AREA_OUTLINER].rect.w > 0 &&
           s->areas[STUDIO_AREA_PROPERTIES].rect.w > 0;
}

static void select_tab(studio_fixture *f, int tab)
{
    studio_command_run_with(f->studio, STUDIO_COMMAND_SELECT_TAB, tab);
    frames(f, 3);
}

static void source_reload_write(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

/* A failed disk reload keeps the actual editor, including its view and modal
   state; a successful reload removes the draft and its saved checkpoint. */
static void source_reload_preserves_failed_draft()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    char folder[] = "/tmp/chibi-source-reload-XXXXXX";
    assert(mkdtemp(folder));
    char directory[PATH_MAX], path[PATH_MAX];
    snprintf(directory, sizeof directory, "%s/behaviors", folder);
    assert(mkdir(directory, 0700) == 0);
    snprintf(path, sizeof path, "%s/Teller.kdl", directory);
    char *saved = text_file_read_disk(PROJECT "/content/behaviors/Teller.kdl");
    assert(saved);
    source_reload_write(path, saved);
    assert(app_scene_authoring_set_content_root(f.scene, folder) == 0);
    game_live_definition *definition = game_live_find_definition(f.live, "Teller");
    assert(definition);
    game_live_definition_set_path(definition, path);
    scene_document_tab *tab = &f.scene->tabs[f.scene->active_tab];
    assert(tab->kind == APP_EDITOR_DOCUMENT_BEHAVIOR);
    free(tab->path);
    tab->path = strdup(path);
    assert(tab->path);
    size_t length = strlen(saved) + 32;
    char *draft = malloc(length);
    assert(draft);
    snprintf(draft, length, "// Unsaved source draft\n%s", saved);
    av_doc *doc = av_doc_new();
    av_value *state = av_obj(doc);
    av_doc_set_root(doc, state);
    av_obj_add_str(doc, state, "path", path);
    av_obj_add_str(doc, state, "disk", saved);
    av_obj_add_str(doc, state, "text", draft);
    av_obj_add_str(doc, state, "valid", draft);
    av_obj_add_bool(doc, state, "overlay", true);
    av_obj_add_int(doc, state, "step", 1);
    av_obj_add_int(doc, state, "cursor", 5);
    av_obj_add_int(doc, state, "anchor", 5);
    av_value *history = av_obj_add_arr(doc, state, "history");
    av_arr_add_str(doc, history, saved);
    av_arr_add_str(doc, history, draft);
    asset_code *code = asset_code_restore(state, &f.scene->registry);
    assert(code);
    apush(s->code.assets, code);
    av_value_free(tab->source_state);
    tab->source_state = asset_code_capture(code);
    tab->source_dirty = true;
    av_value *checkpoint = av_value_copy(tab->source_state);
    kit_code_editor *editor = asset_code_editor(code);
    source_reload_write(path, "name \"unfinished");
    app_disk_result failed = studio_code_reload_path(s, path);
    bool retained = failed == APP_DISK_FAILED && acount(s->code.assets) == 1 &&
                    s->code.assets[0] == code && asset_code_editor(code) == editor &&
                    !strcmp(kit_code_text(editor, NULL), draft) &&
                    kit_code_cursor(editor) == 5 && asset_code_dirty(code) &&
                    tab->source_dirty && av_equals(checkpoint, tab->source_state) &&
                    text_overlay_text(path) &&
                    !strcmp(text_overlay_text(path), draft);
    test_check(retained, "failed KDL reload preserves the live editor, draft, history, cursor and preview overlay");
    /* On Save keeps a newer valid draft separate from its last live preview.
       Checkpoint restoration can also replace the overlay's revision. */
    text_overlay_set(path, saved);
    failed = studio_code_reload_path(s, path);
    test_check(
        failed == APP_DISK_FAILED && asset_code_editor(code) == editor &&
            !strcmp(kit_code_text(editor, NULL), draft) &&
            text_overlay_text(path) && !strcmp(text_overlay_text(path), saved),
        "failed reload restores the actual preview bytes while keeping a newer "
        "deferred source draft"
    );
    source_reload_write(path, saved);
    app_disk_result loaded = studio_code_reload_path(s, path);
    bool cleared = loaded == APP_DISK_RELOADED && !acount(s->code.assets) &&
                   !tab->source_state && !tab->source_dirty &&
                   !text_overlay_text(path);
    s->maximized = STUDIO_AREA_TOP;
    studio_active_layout(s)->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_CODE;
    s->ui_schedule.wake = true;
    frames(&f, 3);
    asset_code *fresh = acount(s->code.assets) ? s->code.assets[0] : NULL;
    test_check(
        cleared && fresh && !asset_code_dirty(fresh) &&
            !strcmp(kit_code_text(asset_code_editor(fresh), NULL), saved),
        "successful KDL reload clears its checkpoint and redraws exact disk "
        "text without restoring the old draft"
    );
    /* A restored checkpoint publishes its preview before the Text Editor
       opens. Reload must bypass that overlay even with no asset_code object. */
    assert(fresh && acount(s->code.assets) == 1);
    asset_code_free(fresh);
    adel(s->code.assets, 0);
    av_value_free(tab->source_state);
    tab->source_state = av_value_copy(checkpoint);
    tab->source_dirty = true;
    text_overlay_set(path, draft);
    source_reload_write(path, "name \"unfinished");
    failed = studio_code_reload_path(s, path);
    test_check(
        failed == APP_DISK_FAILED && !acount(s->code.assets) &&
            tab->source_dirty && av_equals(checkpoint, tab->source_state) &&
            text_overlay_text(path) && !strcmp(text_overlay_text(path), draft),
        "failed checkpoint-only reload keeps the draft and restores preview "
        "without opening a source editor"
    );
    source_reload_write(path, saved);
    loaded = studio_code_reload_path(s, path);
    cleared = loaded == APP_DISK_RELOADED && !tab->source_state &&
              !tab->source_dirty && !text_overlay_text(path);
    s->ui_schedule.wake = true;
    frames(&f, 3);
    fresh = acount(s->code.assets) ? s->code.assets[0] : NULL;
    test_check(
        cleared && fresh && !asset_code_dirty(fresh) &&
            !strcmp(kit_code_text(asset_code_editor(fresh), NULL), saved),
        "successful checkpoint-only reload reads disk rather than preview and "
        "cannot restore the discarded draft"
    );
    av_value_free(checkpoint);
    av_doc_free(doc);
    free(draft);
    free(saved);
    fixture_free(&f);
    assert(unlink(path) == 0 && rmdir(directory) == 0 && rmdir(folder) == 0);
}

/* Selecting on Canvas keeps the same widget visible in its Hierarchy. */
static void designer_selection_reveals_hierarchy()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->prefs.code_apply_live = false;
    char path[PATH_MAX];
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    frames(&f, 3);
    ui_buffer *buffer = studio_ui_buffer(s);
    const char *markup = "<div id=\"panel\"><button "
                         "id=\"target\">Select</button></div>\n";
    text_edit edit = { 0, (int)strlen(ui_buffer_text(buffer, UI_PART_RML)), markup };
    ui_buffer_edit(buffer, UI_PART_RML, &edit, 1);
    frames(&f, 3);
    studio_designer *d = designer_state(s, &buffer);
    assert(acount(d->rml->elements) == 2);
    designer_select(s, 0);
    assert(studio_designer_key(s, STUDIO_EDITOR_OUTLINER, SDLK_LEFT, 0));
    snprintf(d->filter, sizeof d->filter, "absent");
    d->closed = true;
    s->ui_schedule.wake = true;
    frames(&f, 2);
    bool hidden = !studio_element(s, ".designer-tree .tree-row[data-selected] .name");
    designer_select(s, 1);
    s->ui_schedule.wake = true;
    frames(&f, 3);
    rml_element *name = studio_element(s, ".designer-tree .tree-row[data-selected] .name");
    char text[32] = "";
    if (name) {
        rml_element_inner_rml(name, text, sizeof text);
    }
    if (!(hidden && name && !strcmp(text, "button") && !d->closed &&
          !d->filter[0] && !acount(d->folded))) {
        printf(
            "  Hierarchy hidden %d, row %s, selected %d, closed %d, filter "
            "'%s', folded %d\n",
            hidden,
            name ? text : "missing",
            d->selected,
            d->closed,
            d->filter,
            acount(d->folded)
        );
    }
    test_check(
        hidden && name && !strcmp(text, "button") && !d->closed &&
            !d->filter[0] && !acount(d->folded),
        "Canvas selection reveals its Hierarchy row through collapsed "
        "ancestors and an excluding filter"
    );
    snprintf(d->filter, sizeof d->filter, "target");
    designer_select(s, 1);
    test_check(!strcmp(d->filter, "target"), "selection keeps a Hierarchy filter that already includes its row");
    fixture_free(&f);
}

/* Opening documents and changing tabs keep the selected split mode. Each mode
   remembers its arrangement, and switching modes preserves authoring state. */
static void layout_modes_are_explicit()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->maximized = -1;
    studio_layout *vertical = &s->prefs.layouts[STUDIO_WORKSPACE_VERTICAL_SPLIT];
    studio_layout *horizontal = &s->prefs.layouts[STUDIO_WORKSPACE_HORIZONTAL_SPLIT];
    vertical->side_width = 416;
    vertical->main_split = 0.66f;
    s->prefs.code_apply_live = false;
    s->applied = s->prefs;
    frames(&f, 2);
    select_instance(f.scene, 0);
    int actor = f.scene->selected;

    char path[PATH_MAX];
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    frames(&f, 4);
    bool opened_kept_mode = s->prefs.workspace == STUDIO_WORKSPACE_VERTICAL_SPLIT;
    int bank = tab_named(f.scene, "Bank"), scene = bank == 0 ? 1 : 0;
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_HORIZONTAL_SPLIT);
    frames(&f, 3);
    kit_rect r;
    bool horizontal_shown = s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT &&
                            beside(s) &&
                            kit_find(s->kit, "app/workspace/main/editors/bottom/body/code/text", &r) &&
                            !horizontal->drawer_open && vertical->drawer_open &&
                            vertical->editors[STUDIO_AREA_DRAWER] == STUDIO_EDITOR_CODE;
    ui_buffer *buffer = studio_ui_buffer(s);
    studio_designer *designer = designer_state(s, &buffer);
    assert(buffer && designer && acount(designer->rml->elements) > 0);
    designer_select(s, 0);
    int length = (int)strlen(ui_buffer_text(buffer, UI_PART_RML));
    text_edit edit = { length, length, "<!-- workspace -->\n" };
    ui_buffer_edit(buffer, UI_PART_RML, &edit, 1);
    frames(&f, 2);
    kit_code_editor *code = s->code.editor;
    int cursor = kit_code_cursor(code);
    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 2);

    select_tab(&f, scene);
    bool tab_kept_horizontal = s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT &&
                               beside(s);
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_VERTICAL_SPLIT);
    frames(&f, 3);
    bool vertical_shown = s->prefs.workspace == STUDIO_WORKSPACE_VERTICAL_SPLIT &&
                          stacked(s) && vertical->side_width == 416 &&
                          vertical->main_split == 0.66f && vertical->drawer_open;
    select_tab(&f, bank);
    bool tab_kept_vertical = s->prefs.workspace == STUDIO_WORKSPACE_VERTICAL_SPLIT &&
                             stacked(s);
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_HORIZONTAL_SPLIT);
    frames(&f, 3);
    bool returned = s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT &&
                    beside(s) && studio_ui_buffer(s) == buffer &&
                    s->code.editor == code && kit_code_cursor(code) == cursor &&
                    designer->selected == 0 && f.scene->selected == actor &&
                    app_scene_authoring_runtime_state(f.scene).playing;

    rml_element *toggles[2];
    assert(panel_elements(s, "TopBar", ".side-pane", toggles) == 1);
    press(s, rml_element_query_selector(toggles[0], ".icon"));
    frames(&f, 30);
    bool side_hidden = !horizontal->side_open && !side_shown(s) && vertical->side_open;
    rml_element *area_heads[8];
    bool side_headers_hidden = panel_elements(s, "AreaHeader", ".area-head", area_heads) ==
                               2;
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_VERTICAL_SPLIT);
    frames(&f, 30);
    bool side_kept = vertical->side_open && side_shown(s);
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_HORIZONTAL_SPLIT);
    frames(&f, 30);
    bool side_remembered = !horizontal->side_open && !side_shown(s);
    studio_show_editor(s, STUDIO_EDITOR_OUTLINER);
    frames(&f, 30);
    bool side_reopened = horizontal->side_open && side_shown(s);
    int headers = panel_elements(s, "AreaHeader", ".area-head", area_heads);
    char area_name[32], alias[32];
    snprintf(area_name, sizeof area_name, "editor.areas[%d]", STUDIO_AREA_OUTLINER);
    bool top_header_restored = false;
    for (int i = 0; i < headers; i++) {
        rml_element *host = rml_element_parent(area_heads[i]);
        if (rml_element_attribute(host, "data-alias-area", alias, sizeof alias) > 0 &&
            !strcmp(alias, area_name)) {
            rml_vec2 size = rml_element_box_size(area_heads[i], RML_BOX_BORDER);
            top_header_restored = rml_element_is_visible(area_heads[i], true) &&
                                  size.y > 0;
            break;
        }
    }
    bool side_header_restored = headers == 4 && top_header_restored;

    horizontal->main_split = 0.75f;
    studio_command_run(s, STUDIO_COMMAND_RESET_LAYOUT);
    frames(&f, 2);
    bool reset_one = horizontal->main_split == 0.5f &&
                     vertical->main_split == 0.66f && vertical->side_width == 416;
    studio_history(s, false);
    bool undone = !strstr(ui_buffer_text(buffer, UI_PART_RML), "<!-- workspace -->");
    studio_history(s, true);
    bool redone = strstr(ui_buffer_text(buffer, UI_PART_RML), "<!-- workspace -->") !=
                  NULL;
    bool playing = app_scene_authoring_runtime_state(f.scene).playing;

    select_tab(&f, scene);
    bool tab_kept_mode = s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT;
    studio_runtime(s, APP_RUNTIME_STOP);
    frames(&f, 1);
    if (!(opened_kept_mode && horizontal_shown && tab_kept_horizontal && vertical_shown &&
          tab_kept_vertical && returned && side_hidden && side_headers_hidden &&
          side_kept && side_remembered && side_reopened && side_header_restored &&
          reset_one && undone && redone && playing && tab_kept_mode)) {
        printf(
            "  %d %d %d %d %d %d | %d %d %d %d %d %d | %d %d %d %d %d\n",
            opened_kept_mode,
            horizontal_shown,
            tab_kept_horizontal,
            vertical_shown,
            tab_kept_vertical,
            returned,
            side_hidden,
            side_headers_hidden,
            side_kept,
            side_remembered,
            side_reopened,
            side_header_restored,
            reset_one,
            undone,
            redone,
            playing,
            tab_kept_mode
        );
    }
    test_check(
        opened_kept_mode && horizontal_shown && tab_kept_horizontal &&
            vertical_shown && tab_kept_vertical && returned,
        "opening documents and changing tabs keep the selected split mode"
    );
    test_check(
        side_hidden && side_headers_hidden && side_kept && side_remembered &&
            side_reopened && side_header_restored,
        "the top bar hides the side pane per mode and restores its editor "
        "headers"
    );
    test_check(
        reset_one && undone && redone && playing && tab_kept_mode,
        "Reset Layout resets the active mode only; switching keeps "
        "history and playback"
    );
    fixture_free(&f);
}

/* A font-size preference scales the graph as a whole. The DOM hit boxes,
   native socket positions and pointer conversion must still describe one
   coordinate system, while the saved canvas zoom stays user-controlled. */
static bool graph_hit_geometry(studio *s)
{
    studio_graph *canvas = &s->graph;
    graph *gr = sg_active_graph(s);
    bool found_socket = false;
    if (!canvas->kit || !gr || fabsf(canvas->in.zoom - sg_zoom(s)) > 0.001f ||
        fabsf(canvas->in.dp_ratio - studio_graph_pixel_ratio(s)) > 0.001f) {
        return false;
    }
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        if (!sg_node_visible(gr, n)) {
            continue;
        }
        float box[4];
        sg_node_box(s, n, box);
        char path[64];
        snprintf(path, sizeof path, "world/stage/n%llu", (unsigned long long)n->id);
        kit_rect rendered;
        bool found = kit_find(canvas->kit, path, &rendered);
        if (!found || fabsf(rendered.x - (box[0] - canvas->x)) > 0.5f ||
            fabsf(rendered.y - (box[1] - canvas->y)) > 0.5f ||
            fabsf(rendered.w - box[2]) > 0.5f || fabsf(rendered.h - box[3]) > 0.5f) {
            return false;
        }
        const node_layout *layout = sg_layout_of(s, n);
        for (int kind = ED_PIN_INPUT; kind <= ED_PIN_OUTPUT; kind++) {
            int count = kind == ED_PIN_INPUT ? n->input_count : n->output_count;
            char side = kind == ED_PIN_INPUT ? 'i' : 'o';
            for (int pin = 0; pin < count; pin++) {
                bool in_head = sg_in_head(layout, kind, pin);
                if (!in_head && !sg_pin_in_rows(gr, n, layout, kind, pin)) {
                    continue;
                }
                if (in_head) {
                    snprintf(
                        path,
                        sizeof path,
                        "world/stage/n%llu/h/%c%d/s",
                        (unsigned long long)n->id,
                        side,
                        pin
                    );
                } else if (sg_pin_exec(n, kind, pin)) {
                    snprintf(
                        path,
                        sizeof path,
                        "world/stage/n%llu/b/e%d/%c%d/s",
                        (unsigned long long)n->id,
                        sg_pin_order(gr, n, layout, kind, pin),
                        side,
                        pin
                    );
                } else {
                    snprintf(
                        path,
                        sizeof path,
                        "world/stage/n%llu/b/%c%d/s",
                        (unsigned long long)n->id,
                        side,
                        pin
                    );
                }
                kit_rect socket;
                float x, y, world_x, world_y;
                sg_socket_at(s, n, kind, pin, &x, &y);
                found = kit_find(canvas->kit, path, &socket);
                if (!found || fabsf(socket.x + socket.w / 2 - (x - canvas->x)) > 0.5f ||
                    fabsf(socket.y + socket.h / 2 - (y - canvas->y)) > 0.5f) {
                    return false;
                }
                found_socket = true;
                float screen_x = canvas->surface.rect.x +
                                 (x - canvas->x) * sg_zoom(s);
                float screen_y = canvas->surface.rect.y +
                                 (y - canvas->y) * sg_zoom(s);
                sg_to_world(s, screen_x, screen_y, &world_x, &world_y);
                if (fabsf(world_x - x) >= 0.01f || fabsf(world_y - y) >= 0.01f) {
                    return false;
                }
            }
        }
    }
    return found_socket;
}

static void graph_font_size_preserves_hit_geometry()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 3);
    studio_graph *g = &s->graph;
    g->camera.active = false;
    g->zoom = 0.8f;
    g->x = -40;
    g->y = -40;
    g->framed = true;
    frames(&f, 2);
    bool standard = fabsf(g->zoom - 0.8f) < 0.001f && graph_hit_geometry(s);
    s->prefs.standard_font_size = 18;
    frames(&f, 2);
    bool enlarged = fabsf(g->zoom - 0.8f) < 0.001f &&
                    fabsf(g->in.zoom - 1.2f) < 0.001f && graph_hit_geometry(s);
    test_check(
        standard && enlarged,
        "graph font scaling keeps rendered node and socket hit geometry "
        "aligned without changing the stored zoom"
    );
    fixture_free(&f);
}

/* ---------- arena2d end to end (wave 3, workstream X) ---------- */

static void text_event(const char *text)
{
    SDL_Event event = { .type = SDL_EVENT_TEXT_INPUT };
    event.text.windowID = SDL_GetWindowID(cf_app_get_window());
    event.text.text = text;
    assert(SDL_PushEvent(&event));
}

/* The value of the focused element in the Scene's game documents. */
static void game_field(studio_fixture *f, char *value, size_t size)
{
    rml_context *context = render_ui_context(engine_viewport_ui(f->viewport));
    rml_element *focus = rml_context_focus_element(context);
    value[0] = '\0';
    if (focus) {
        rml_element_value(focus, value, size);
    }
}

/* While the scene plays, a focused text field in its game documents takes
   the text typed in the focused Scene, SDL delivering text for it, and
   Studio keeps the keys with the primary modifier. */
static void scene_text_field()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    text_overlay_set(BANK, "<input id=\"name\" type=\"text\" autofocus/>\n");
    s->focus_area = STUDIO_AREA_TOP;
    frames(&f, 3);
    char value[32];
    text_event("x");
    frames(&f, 2);
    game_field(&f, value, sizeof value);
    bool editing = !value[0] && !engine_viewport_ui_typing(f.viewport);
    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 3);
    bool started = engine_viewport_ui_typing(f.viewport) && s->text_input;
    key_event(SDL_SCANCODE_A, true);
    text_event("a");
    key_event(SDL_SCANCODE_A, false);
    text_event("b");
    frames(&f, 2);
    game_field(&f, value, sizeof value);
    bool typed = !strcmp(value, "ab");
    SDL_Event select = { .type = SDL_EVENT_KEY_DOWN };
    select.key.windowID = SDL_GetWindowID(cf_app_get_window());
    select.key.scancode = SDL_SCANCODE_A;
    select.key.key = SDLK_A;
    select.key.mod = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    select.key.down = true;
    assert(SDL_PushEvent(&select));
    frames(&f, 1);
    bool kept = !acount(s->game_input);
    key_event(SDL_SCANCODE_A, false);
    studio_runtime(s, APP_RUNTIME_STOP);
    frames(&f, 2);
    bool stopped = !engine_viewport_ui_typing(f.viewport) && !s->text_input;
    if (!(editing && started && typed && kept && stopped)) {
        printf("  %d %d %d '%s' %d %d\n", editing, started, typed, value, kept, stopped);
    }
    test_check(
        editing && started && typed && kept && stopped,
        "a focused text field in the playing Scene's game UI takes typed "
        "text, and Studio keeps its chords"
    );
    text_overlay_set(BANK, NULL);
    fixture_free(&f);
}

/* ---------- Rename objects and prototype components ---------- */

static void tap(SDL_Scancode key)
{
    key_event(key, true);
    key_event(key, false);
}

/* The pointer to (x, y) in dp, as SDL reports it in window points. */
static void mouse_move(const studio *s, float x, float y)
{
    SDL_Event move = { .type = SDL_EVENT_MOUSE_MOTION };
    move.motion.windowID = SDL_GetWindowID(cf_app_get_window());
    move.motion.x = x * s->prefs.ui_scale;
    move.motion.y = y * s->prefs.ui_scale;
    assert(SDL_PushEvent(&move));
}

static void mouse_click(studio_fixture *f, float x, float y, Uint8 button)
{
    const studio *s = f->studio;
    mouse_move(s, x, y);
    for (int down = 1; down >= 0; down--) {
        SDL_Event press = { .type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                         : SDL_EVENT_MOUSE_BUTTON_UP };
        press.button.windowID = SDL_GetWindowID(cf_app_get_window());
        press.button.button = button;
        press.button.down = down;
        press.button.clicks = 1;
        press.button.x = x * s->prefs.ui_scale;
        press.button.y = y * s->prefs.ui_scale;
        assert(SDL_PushEvent(&press));
    }
    frames(f, 2);
}

static bool click_path(studio_fixture *f, const char *path, Uint8 button)
{
    kit_rect r;
    if (!kit_find(f->studio->kit, path, &r)) {
        return false;
    }
    mouse_click(f, r.x + r.w * 0.5f, r.y + r.h * 0.5f, button);
    return true;
}

static void mouse_over(studio_fixture *f, studio_area_slot area)
{
    kit_rect r = f->studio->areas[area].rect;
    mouse_move(f->studio, r.x + r.w * 0.5f, r.y + r.h * 0.5f);
    frames(f, 1);
}

static bool named(const app_scene_authoring *scene, const char *name)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (!strcmp(scene->actors[i].name, name)) {
            return true;
        }
    }
    return false;
}

/* Studio over the Vault scene with the Outliner and Properties beside the
   Scene, whatever layout was saved. */
static studio_fixture side_open()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->maximized = -1;
    s->focus_area = STUDIO_AREA_OUTLINER;
    frames(&f, 3);
    return f;
}

/* Renames one object at a time: F2 over the Outliner edits its row, Enter
   renames as one Undo step, Escape keeps the name and a name in use is
   refused; without the Outliner, F2 edits Properties' name field. */
static void rename_objects()
{
    studio_fixture f = side_open();
    studio *s = f.studio;
    app_scene_authoring *scene = f.scene;
    studio_add_object(s, "Vault");
    select_instance(scene, 0);
    frames(&f, 2);
    mouse_over(&f, STUDIO_AREA_OUTLINER);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    bool field = s->rename.entity && s->rename.outliner && kit_wants_keyboard(s->kit);
    text_event("Strongroom");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool renamed = named(scene, "Strongroom") && !named(scene, "Vault") &&
                   !s->rename.entity;
    studio_history(s, false);
    bool undone = named(scene, "Vault") && !named(scene, "Strongroom");
    studio_history(s, true);
    bool redone = named(scene, "Strongroom");
    frames(&f, 1);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    text_event("Nope");
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    bool kept = named(scene, "Strongroom") && !named(scene, "Nope") &&
                !s->rename.entity;
    select_instance(scene, 1);
    frames(&f, 1);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    text_event("Strongroom");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool refused = named(scene, "Vault_1") && strstr(scene->message, "Strongroom");
    studio_active_layout(s)->editors[STUDIO_AREA_OUTLINER] = STUDIO_EDITOR_ASSETS;
    frames(&f, 2);
    mouse_over(&f, STUDIO_AREA_TOP);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    bool properties = s->tab == STUDIO_TAB_OBJECT && kit_wants_keyboard(s->kit);
    text_event("Annex");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool inspector = named(scene, "Annex") && !named(scene, "Vault_1");
    if (!(field && renamed && undone && redone && kept && refused &&
          properties && inspector)) {
        printf("  %d %d %d %d %d %d %d %d\n", field, renamed, undone, redone, kept, refused, properties, inspector);
    }
    test_check(
        field && renamed && undone && redone && kept,
        "F2 over the Outliner renames its row in place as one Undo step, "
        "and Escape keeps the name"
    );
    test_check(
        refused && properties && inspector,
        "a name in use is refused, and F2 edits Properties' name field "
        "while no area shows the Outliner"
    );
    fixture_free(&f);
}

static bool prototype_has(app_scene_authoring *scene, const char *component)
{
    const game_prototype *prototype = find_prototype(scene, "Vault")->prototype;
    for (int i = 0; i < acount(prototype->components); i++) {
        if (!strcmp(prototype->components[i].desc->name, component)) {
            return true;
        }
    }
    return false;
}

/* Prototype Properties opens on its own context and keeps a valid component
   tab selected while that document remains active. */
static void prototype_properties_follow_document()
{
    studio_fixture f = side_open();
    studio *s = f.studio;
    s->maximized = s->focus_area = STUDIO_AREA_PROPERTIES;
    s->tab = STUDIO_TAB_SCENE;
    char error[256];
    assert(component_presentation_read(
        "components { GAME.GameUi { tab \"Gameplay\"; }; }\n",
        &f.scene->registry,
        &s->presentation.hints,
        error,
        sizeof error
    ));
    assert(app_scene_authoring_open_prototype(f.scene, "Vault") == 0);
    frames(&f, 3);
    kit_rect rect;
    bool object = s->tab == STUDIO_TAB_OBJECT &&
                  !kit_find(s->kit, "app/workspace/main/properties/body/properties/tabs/tab0", &rect);
    s->tab = STUDIO_TAB_GAMEPLAY;
    frames(&f, 2);
    bool retained = s->tab == STUDIO_TAB_GAMEPLAY &&
                    kit_find(s->kit, "app/workspace/main/properties/body/properties/tabs/tab4", &rect);
    test_check(object, "opening a prototype leaves the unrelated Scene tab and selects Object");
    test_check(retained, "Properties keeps a valid component tab selected for the active prototype");
    fixture_free(&f);
}

/* Add Component searches the components the prototype lacks and adds the
   chosen one; its panel's menu removes it, and the toast's Undo returns it. */
static void prototype_components()
{
    studio_fixture f = side_open();
    studio *s = f.studio;
    /* Properties fills the workspace. Scroll its final action into view. */
    s->maximized = s->focus_area = STUDIO_AREA_PROPERTIES;
    assert(app_scene_authoring_open_prototype(f.scene, "Vault") == 0);
    frames(&f, 3);
    const char *scroll = "app/workspace/main/properties/body/properties/scroll";
    char path[256];
    snprintf(path, sizeof path, "%s/add/component", scroll);
    rml_element *add_button = studio_element(s, ".prop-scroll .add-row .btn");
    assert(add_button);
    rml_element_scroll_into_view(add_button, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
    frames(&f, 2);
    bool button = click_path(&f, path, SDL_BUTTON_LEFT);
    bool palette = kit_command_is_open(s->kit) && s->components.open;
    text_event("Health");
    frames(&f, 2);
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    char added[128];
    snprintf(added, sizeof added, "%s", s->components.added);
    bool gained = added[0] && prototype_has(f.scene, added) && !s->components.open;
    snprintf(path, sizeof path, "%s/%s", scroll, added);
    kit_rect panel = { 0 };
    bool shown = kit_find(s->kit, path, &panel);
    mouse_click(&f, panel.x + 40, panel.y + 10, SDL_BUTTON_RIGHT);
    frames(&f, 8);
    bool menu = kit_popup_is_open(s->kit, "component-menu");
    click_path(&f, "layer/@component-menu/remove", SDL_BUTTON_LEFT);
    bool removed = !prototype_has(f.scene, added);
    rml_element *undo = studio_element(s, ".toast-action");
    if (undo) {
        rml_element_click(undo);
    }
    frames(&f, 2);
    bool restored = prototype_has(f.scene, added);
    studio_history(s, false);
    bool undone = !prototype_has(f.scene, added);
    studio_history(s, true);
    bool redone = prototype_has(f.scene, added);
    if (!(button && palette && gained && shown && menu && removed && undo &&
          restored && undone && redone)) {
        printf(
            "  %d %d %d %d %d %d %d %d %d %d\n",
            button,
            palette,
            gained,
            shown,
            menu,
            removed,
            undo != NULL,
            restored,
            undone,
            redone
        );
    }
    test_check(
        button && palette && gained,
        "Add Component searches the components a prototype lacks and adds "
        "the one chosen"
    );
    test_check(
        shown && menu && removed && undo && restored && undone && redone,
        "a component panel's menu removes it with Undo in a toast, and "
        "Undo and Redo step both"
    );
    fixture_free(&f);
}

/* Authored modal events travel through the mount and Dismiss behavior. */
static void authored_fields_keep_editing_keys()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    text_overlay_set(
        BANK,
        "<div data-keys=\"C:Parent Space:Parent Enter:Parent Up:Parent "
        "Down:Parent Escape:Close\">"
        "<input id=\"field\" type=\"text\" autofocus "
        "data-keys=\"Enter:Submit\"/>"
        "<textarea id=\"area\"/>"
        "<select id=\"pick\"><option value=\"a\">A</option><option "
        "value=\"b\">B</option></select>"
        "<input id=\"radio\" type=\"radio\"/>"
        "</div>\n"
    );
    frames(&f, 3);
    studio_preview_ui(s, "Bank");
    frames(&f, 8);
    rml_context *context = render_ui_context(engine_viewport_ui(s->canvas->viewport));
    rml_element *field[8] = { 0 };
    assert(canvas_elements(s, "#field", field) == 1);
    assert(rml_element_focus(field[0], true));
    int before = acount(s->canvas->actions);
    rml_context_process_key_down(context, RML_KEY_C, 0);
    rml_context_process_text_input(context, "c", 1);
    rml_context_process_key_down(context, RML_KEY_SPACE, 0);
    rml_context_process_text_input(context, " ", 1);
    frames(&f, 2);
    char value[32];
    rml_element_value(field[0], value, sizeof value);
    bool typed = !strcmp(value, "c ") && acount(s->canvas->actions) == before;
    rml_context_process_key_down(context, RML_KEY_RETURN, 0);
    frames(&f, 2);
    bool submitted = logged_last(s, "Submit") &&
                     acount(s->canvas->actions) == before + 1;
    assert(canvas_elements(s, "#area", field) == 1);
    assert(rml_element_focus(field[0], true));
    before = acount(s->canvas->actions);
    rml_context_process_text_input(context, "a", 1);
    rml_context_process_key_down(context, RML_KEY_RETURN, 0);
    rml_context_process_text_input(context, "\n", 1);
    frames(&f, 2);
    rml_element_value(field[0], value, sizeof value);
    bool newline = strchr(value, '\n') && acount(s->canvas->actions) == before;
    rml_context_process_key_down(context, RML_KEY_ESCAPE, 0);
    frames(&f, 2);
    bool escaped = logged_last(s, "Close");
    assert(canvas_elements(s, "#pick", field) == 1);
    assert(rml_element_focus(field[0], true));
    rml_element_click(field[0]);
    before = acount(s->canvas->actions);
    rml_context_process_key_down(context, RML_KEY_DOWN, 0);
    rml_context_process_key_down(context, RML_KEY_RETURN, 0);
    rml_context_process_key_down(context, RML_KEY_ESCAPE, 0);
    frames(&f, 2);
    rml_element_value(field[0], value, sizeof value);
    bool selected = !strcmp(value, "b") && acount(s->canvas->actions) == before;
    assert(canvas_elements(s, "#radio", field) == 1);
    assert(rml_element_focus(field[0], true));
    rml_context_process_key_down(context, RML_KEY_UP, 0);
    frames(&f, 2);
    bool radio = logged_last(s, "Parent");
    if (!(typed && submitted && newline && escaped && selected && radio)) {
        printf(
            "  field keys: typed %d, submitted %d, newline %d, Escape %d, "
            "select %d, radio %d\n",
            typed,
            submitted,
            newline,
            escaped,
            selected,
            radio
        );
    }
    test_check(typed && submitted && newline && escaped && selected && radio, "authored input bindings submit explicitly and inherit Escape while text, textarea and select keep editing keys; radios retain ancestor navigation");
    studio_preview_ui(s, NULL);
    fixture_free(&f);
    text_overlay_set(BANK, NULL);
}

static void authored_modals_and_collaboration()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 3);
    rml_element *found[8] = { 0 };
    bool toolbar = panel_elements(s, "TopBar", ".collaboration-trigger", found) == 1;
    assert(toolbar);
    press(s, found[0]);
    frames(&f, 4);
    bool opened = panel_elements(s, "Collaboration", ".collab-title", found) == 1 &&
                  !kit_popup_is_open(s->kit, "collaboration");
    bool focused = studio_element(s, "[data-ui=Collaboration] .close:focus") != NULL;
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 4);
    bool escape_closed = panel_elements(s, "Collaboration", ".collab-title", found) == 0;
    studio_command_run(s, STUDIO_COMMAND_COLLABORATION);
    frames(&f, 3);
    rml_element *close = studio_element(s, "[data-ui=Collaboration] .close");
    assert(close);
    press(s, close);
    frames(&f, 4);
    bool close_closed = panel_elements(s, "Collaboration", ".collab-title", found) == 0;
    studio_command_run(s, STUDIO_COMMAND_SETTINGS);
    frames(&f, 3);
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 4);
    bool settings_escape = studio_element(s, "[data-ui=Settings] .head-title") == NULL;
    studio_command_run(s, STUDIO_COMMAND_SETTINGS);
    frames(&f, 3);
    close = studio_element(s, "[data-ui=Settings] .close");
    assert(close);
    press(s, close);
    frames(&f, 4);
    bool settings_close = studio_element(s, "[data-ui=Settings] .head-title") == NULL;
    if (!(toolbar && opened && focused && escape_closed && close_closed &&
          settings_escape && settings_close)) {
        printf(
            "  authored modal checks: toolbar %d, opened %d, focused %d, "
            "Escape "
            "%d, X %d, Settings Escape %d, Settings X %d\n",
            toolbar,
            opened,
            focused,
            escape_closed,
            close_closed,
            settings_escape,
            settings_close
        );
    }
    test_check(
        toolbar && opened && focused && escape_closed && close_closed &&
            settings_escape && settings_close,
        "toolbar and command open one authored Collaboration dialog; both "
        "dialogs dismiss with Escape and X"
    );

    collab_start("room", "self", "editor");
    apush(
        s->collaboration.peers,
        ((studio_collab_peer){ .client = strdup("peer"),
                               .name = strdup("Ada"),
                               .presentation = studio_collab_capture(s),
                               .color = 0x7aa2f7ffu })
    );
    studio_command_run(s, STUDIO_COMMAND_COLLABORATION);
    frames(&f, 3);
    bool participant = panel_elements(s, "Collaboration", ".participant-name", found) == 1;
    rml_element *follow = studio_element(s, "[data-ui=Collaboration] .follow");
    assert(follow);
    press(s, follow);
    frames(&f, 3);
    bool following = s->collaboration.following &&
                     !strcmp(s->collaboration.follow_client, "peer");
    rml_element *unfollow = studio_element(s, "[data-ui=Collaboration] .unfollow");
    assert(unfollow);
    press(s, unfollow);
    frames(&f, 3);
    bool stopped = !s->collaboration.following;
    studio_command_run_with(s, STUDIO_COMMAND_COLLABORATION_FOLLOW, 0);
    studio_ui_collaboration_action(s, STUDIO_COMMAND_COLLABORATION_LEAVE);
    collab_tick();
    bool left = !s->collaboration.following && !s->collaboration.saved_prefs &&
                !collab_active();
    s->collaboration.busy = true;
    collab_feedback("Creating room...", 0);
    studio_ui_collaboration_action(s, STUDIO_COMMAND_COLLABORATION_JOIN);
    bool gated = !strcmp(collab_feedback_text(), "Creating room...") &&
                 studio_collab_busy(s);
    collab_feedback("Room service unavailable", 1);
    bool retry = !studio_collab_busy(s);
    if (!(participant && following && stopped && left && gated && retry)) {
        printf(
            "  authored room checks: participant %d, following %d, stopped %d, "
            "left %d, gated %d, retry %d\n",
            participant,
            following,
            stopped,
            left,
            gated,
            retry
        );
    }
    test_check(participant && following && stopped && left && gated && retry, "authored participants follow and stop following; Leave restores local preferences; pending room requests gate retries until failure");
    collab_shutdown();
    fixture_free(&f);
}

/* A large reflected enum keeps its name in Properties and can still be
   changed from the control. GameControl's Aim Stick is past the old cap. */
static void large_enum_property()
{
    studio_fixture f = fixture_open(PROJECT, PROJECT "/content/scenes/Binding.kdl", "Weapons");
    f.studio = studio_new(f.scene, f.viewport, "projects/studio");
    studio *s = f.studio;
    select_instance(f.scene, 0);
    show_object_properties(s);
    snprintf(s->property_search, sizeof s->property_search, "Source");
    frames(&f, 4);

    rml_element *tabs = studio_element(s, "[data-ui=Properties] #tabs");
    rml_element *scroll = studio_element(s, "[data-ui=Properties] #scroll");
    rml_element *row = property_row(s, "Source");
    if (row) {
        rml_element_scroll_into_view(row, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
        frames(&f, 2);
        row = property_row(s, "Source");
    }
    rml_element *label = row ? rml_element_query_selector(row, ".label span") : NULL;
    rml_element *field = row ? rml_element_query_selector(row, ".field.select") : NULL;
    rml_element *value = row ? rml_element_query_selector(row, ".field.select .value")
                             : NULL;
    char shown[64] = "";
    if (value) {
        rml_element_inner_rml(value, shown, sizeof shown);
    }
    bool mounted = tabs && scroll;
    bool readable = row && field && !strcmp(shown, "Aim Stick");
    float label_baseline = label ? text_baseline(label) : 0;
    float value_baseline = value ? text_baseline(value) : 0;
    bool aligned = label && value && fabsf(label_baseline - value_baseline) <= 1.5f;

    if (field) {
        press(s, field);
        frames(&f, 2);
    }
    rml_element *dropdown = studio_element(s, ".popup.dropdown");
    bool opened = dropdown != NULL;
    rml_element *groups[4], *icons[64];
    int group_count = dropdown
                        ? rml_element_query_selector_all(dropdown, ".menu-group", groups, 4)
                        : 0;
    int icon_count = dropdown
                       ? rml_element_query_selector_all(dropdown, ".menu-item .lead", icons, 64)
                       : 0;
    bool grouped = group_count == 3 && icon_count > 32;
    if (grouped) {
        const char *expected[] = { "Mouse", "Keyboard", "Controller" };
        for (int i = 0; i < group_count; i++) {
            char group[32] = "";
            rml_element_inner_rml(groups[i], group, sizeof group);
            grouped &= !strcmp(group, expected[i]);
        }
        for (int i = 0; i < icon_count; i++) {
            char icon[16] = "";
            rml_element_inner_rml(icons[i], icon, sizeof icon);
            grouped &= icon[0] != '\0';
        }
    }
    capture_properties_screenshot(&f, "properties-enum-dropdown");
    text_event("L");
    frames(&f, 2);
    int search_group_count = dropdown
                               ? rml_element_query_selector_all(dropdown, ".menu-group", groups, 4)
                               : 0;
    int search_icon_count = dropdown
                              ? rml_element_query_selector_all(dropdown, ".menu-item .lead", icons, 64)
                              : 0;
    bool all_search_groups = search_group_count == 3 && search_icon_count > 2;
    if (all_search_groups) {
        const char *expected[] = { "Mouse", "Keyboard", "Controller" };
        for (int i = 0; i < search_group_count; i++) {
            char group[32] = "";
            rml_element_inner_rml(groups[i], group, sizeof group);
            bool known = false;
            for (int j = 0; j < 3; j++) {
                known |= !strcmp(group, expected[j]);
            }
            all_search_groups &= known;
        }
        for (int i = 0; i < search_icon_count; i++) {
            char icon[16] = "";
            rml_element_inner_rml(icons[i], icon, sizeof icon);
            all_search_groups &= icon[0] != '\0';
        }
    }
    rml_element *search = dropdown
                            ? rml_element_query_selector(dropdown, ".field.search .value")
                            : NULL;
    char query[16] = "";
    if (search) {
        rml_element_value(search, query, sizeof query);
    }
    all_search_groups &= !strcmp(query, "L");
    capture_properties_screenshot(&f, "properties-grouped-search");
    tap(SDL_SCANCODE_BACKSPACE);
    frames(&f, 1);
    text_event("Mouse Left");
    frames(&f, 2);
    rml_element *mouse_left = dropdown
                                ? rml_element_query_selector(dropdown, ".dropdown-results .menu-item .label")
                                : NULL;
    char option[64] = "";
    if (mouse_left) {
        rml_element_inner_rml(mouse_left, option, sizeof option);
    }
    bool filtered = mouse_left && !strcmp(option, "Mouse Left");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    pin_value source = { 0 };
    bool editable = filtered &&
                    ecs_component_read_field(
                        f.scene->world,
                        f.scene->actors[0].entity,
                        GAME_COMPONENT_GameActionBinding.id,
                        0,
                        &source
                    ) == ECS_OK &&
                    source.type == PIN_ENUM && source.e == GAME_CONTROL_MOUSE_LEFT;
    if (!(mounted && readable && aligned && opened && filtered && editable &&
          grouped && all_search_groups)) {
        printf(
            "  %d %d %d %d %d %d %d %d (baselines %g / %g, groups %d, icons "
            "%d, search groups %d, icons %d, query '%s')\n",
            mounted,
            readable,
            aligned,
            opened,
            filtered,
            editable,
            grouped,
            all_search_groups,
            label_baseline,
            value_baseline,
            group_count,
            icon_count,
            search_group_count,
            search_icon_count,
            query
        );
    }
    test_check(mounted, "Properties uses its authored tab and scroll slots");
    test_check(aligned, "Properties label and value text baselines align");
    test_check(
        readable && opened && filtered && editable && grouped && all_search_groups,
        "a reflected enum beyond 32 options stays named, grouped, iconed "
        "and editable in Properties"
    );
    fixture_free(&f);
}

/* A reflected vector component uses one joined number control and keeps the
   rest of the vector intact through Undo and Redo. */
static void component_vector_property()
{
    studio_fixture f = fixture_open(PROJECT, PROJECT "/content/scenes/Binding.kdl", "Weapons");
    f.studio = studio_new(f.scene, f.viewport, "projects/studio");
    studio *s = f.studio;
    select_instance(f.scene, 0);
    show_object_properties(s);
    snprintf(s->property_search, sizeof s->property_search, "Target");
    frames(&f, 4);

    rml_element *row = property_row(s, "Target");
    if (row) {
        rml_element_scroll_into_view(row, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
        frames(&f, 2);
        row = property_row(s, "Target");
    }
    rml_element *axes[4];
    int axis_count = row ? rml_element_query_selector_all(row, ".field.number", axes, 4)
                         : 0;
    bool controls = axis_count == 2;
    bool clicked = false;
    if (controls) {
        press(s, axes[0]);
        clicked = rml_element_has_attribute(axes[0], "data-editing");
    }
    text_event("9");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    const GameDrive
        *drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool edited = drive && drive->target.x == 9.0f && drive->target.y == 4.0f;
    capture_properties_screenshot(&f, "properties-vector-edited");
    studio_history(s, false);
    drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool undone = drive && drive->target.x == 3.0f && drive->target.y == 4.0f;
    studio_history(s, true);
    drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool redone = drive && drive->target.x == 9.0f && drive->target.y == 4.0f;
    if (!(controls && clicked && edited && undone && redone)) {
        printf(
            "  %d %d %d %d %d (%g, %g)\n",
            controls,
            clicked,
            edited,
            undone,
            redone,
            drive ? drive->target.x : -1.0f,
            drive ? drive->target.y : -1.0f
        );
    }
    test_check(controls && clicked && edited, "a reflected component vector is editable by axis in Properties");
    test_check(
        undone && redone,
        "component vector edits retain owned before and "
        "after values for Undo and Redo"
    );
    fixture_free(&f);
}

/* While the scene runs, a Properties field edits the running world without
   marking the scene unsaved or writing history, and the header says Live;
   Keep Changes (K) applies the running value when the scene stops, as one
   Undo step. */
static void live_property_edit()
{
    studio_fixture f = fixture_open(PROJECT, PROJECT "/content/scenes/Binding.kdl", "Weapons");
    f.studio = studio_new(f.scene, f.viewport, "projects/studio");
    studio *s = f.studio;
    select_instance(f.scene, 0);
    show_object_properties(s);
    snprintf(s->property_search, sizeof s->property_search, "Target");
    scene_history_clear(f.scene->history);
    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 2);
    studio_runtime(s, APP_RUNTIME_PLAY);
    f.scene->scene_dirty = false;
    frames(&f, 3);
    bool live = studio_live(s) && studio_element(s, ".area-head.live") != NULL;
    rml_element *row = property_row(s, "Target");
    if (row) {
        rml_element_scroll_into_view(row, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
        frames(&f, 2);
        row = property_row(s, "Target");
    }
    rml_element *axes[4];
    int axis_count = row ? rml_element_query_selector_all(row, ".field.number", axes, 4)
                         : 0;
    bool editable = axis_count == 2 &&
                    !rml_element_has_attribute(axes[0], "data-readonly");
    if (editable) {
        press(s, axes[0]);
    }
    text_event("9");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    const GameDrive
        *drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool written = drive && drive->target.x == 9.0f;
    bool clean = !f.scene->scene_dirty && !scene_history_can_undo(f.scene->history);
    bool chord = studio_command_for_key(&s->prefs, SDLK_K, 0, STUDIO_EDITOR_PROPERTIES, s->mac) ==
                     STUDIO_COMMAND_KEEP_CHANGES &&
                 studio_command_for_key(&s->prefs, SDLK_K, 0, STUDIO_EDITOR_BEHAVIOR, s->mac) !=
                     STUDIO_COMMAND_KEEP_CHANGES;
    studio_command_run(s, STUDIO_COMMAND_KEEP_CHANGES);
    frames(&f, 2);
    bool kept = acount(f.scene->kept) == 1 && s->keep.toast;
    studio_runtime(s, APP_RUNTIME_STOP);
    frames(&f, 2);
    drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool applied = drive && drive->target.x == 9.0f && f.scene->scene_dirty &&
                   s->undo_toast && !studio_live(s);
    studio_history(s, false);
    drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool undone = drive && drive->target.x == 3.0f &&
                  !scene_history_can_undo(f.scene->history);
    if (!(live && editable && written && clean && chord && kept && applied && undone)) {
        printf("  %d %d %d %d %d %d %d %d\n", live, editable, written, clean, chord, kept, applied, undone);
    }
    test_check(
        live && editable && written && clean,
        "while the scene runs, Properties edits the running world without "
        "an unsaved mark or Undo step"
    );
    test_check(
        chord && kept && applied && undone,
        "Keep Changes (K) applies a running value when the scene stops, as "
        "one Undo step"
    );
    fixture_free(&f);
}

static int studio_documents_app(const char *program, bool run_properties_only)
{
    CF_Result result = cf_make_app(
        "Studio documents",
        0,
        0,
        0,
        1280,
        800,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "Studio documents app: %s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    app_input_initialize();
    if (run_properties_only) {
        large_enum_property();
        component_vector_property();
        live_property_edit();
    } else {
        graph_layout_does_not_wiggle();
        graph_calculations_follow_edits();
        graph_drag_retains_calculations_and_undo();
        host_ui_refresh_keeps_input_immediate();
        performance_snapshot_retains_intermediate_frames();
        graph_completion_tab_matches_highlight();
        replay_close_stop_reopen_keeps_graph_layout();
        graph_value_controls_use_metadata();
        graph_color_session_saves_undoes_and_replays();
        reload_keeps_viewed_graph_saved();
        test_ui_document_tabs();
        test_studio_new_assets();
        /* UI wiring (workstream K) */
        ui_wiring_checks();
        /* Authored status bar (workstream J) */
        status_bar_follows_studio();
        /* Workstream I: code intelligence in the Text Editor. */
        test_code_intelligence_studio();
        /* Wave 2 follow-ups (workstream P) */
        preview_hears_keys();
        settings_records_shortcut();
        palette_finds_settings();
        palette_recent_and_prefixes();
        virtual_list_rows();
        assets_cache_follows_explicit_refresh();
        shortcut_repeat_policy();
        keys_follow_pointer();
        readout_follows_camera();
        /* Authored top bar and area headers (workstream T) */
        chrome_follows_studio();
        settings_reopens_after_font_change();
        layout_modes_are_explicit();
        designer_selection_reveals_hierarchy();
        source_reload_preserves_failed_draft();
        graph_font_size_preserves_hit_geometry();
        /* arena2d end to end (wave 3, workstream X) */
        scene_text_field();
        /* Rename objects and prototype components */
        rename_objects();
        prototype_properties_follow_document();
        prototype_components();
        large_enum_property();
        component_vector_property();
        live_property_edit();
        toolbar_shortcuts_keep_text_focus();
        authored_modals_and_collaboration();
        authored_fields_keep_editing_keys();
    }
    app_input_shutdown();
    cf_destroy_app();
    return 0;
}

int studio_documents_run(const char *program)
{
    return studio_documents_app(program, false);
}

int studio_properties_run(const char *program)
{
    return studio_documents_app(program, true);
}
