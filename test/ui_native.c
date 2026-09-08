#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/event_catalog.h"
#include "editor/workspace/input.h"
#include "editor/workspace/scene_authoring.h"
#include "host/stacktrace.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"

#include <assert.h>
#include <cute_defines.h>
#define CF_MATH_H
#include <imgui/dcimgui.h>
#undef CF_MATH_H
#include <stdio.h>
#include <stdlib.h>

enum
{
    UI_WIDTH = 1280,
    UI_HEIGHT = 720,
};

typedef struct
{
    editor *graph;
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    engine_viewport *viewport;
    app_scene_authoring *scene;
    double time;
} ui_fixture;

static bool read_file(const char *path, char **text)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    char *result = malloc((size_t)size + 1);
    bool ok = result && fread(result, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (!ok) {
        free(result);
        return false;
    }
    result[size] = '\0';
    *text = result;
    return true;
}

static bool register_fixture_prototypes(app_scene_authoring *scene)
{
    const char *paths[] = { "test/fixtures/prototypes/InputEntity.json",
                            "test/fixtures/prototypes/StaticEntity.json" };
    for (int i = 0; i < (int)(sizeof paths / sizeof *paths); i++) {
        char *json = NULL;
        if (!read_file(paths[i], &json)) {
            return false;
        }
        int result = app_scene_authoring_register_prototype(scene, json, paths[i]);
        free(json);
        if (result != 0) {
            return false;
        }
    }
    return true;
}

static void ui_fixture_free(ui_fixture *fixture)
{
    app_scene_authoring_free(fixture->scene);
    engine_free(fixture->runtime);
    game_live_world_free(fixture->live);
    editor_free(fixture->graph);
    ecs_world_free(fixture->world);
    engine_viewport_free(fixture->viewport);
    render_shader_catalog_clear();
    editor_ui_deactivate();
    *fixture = (ui_fixture){ 0 };
}

static bool ui_fixture_new(ui_fixture *fixture)
{
    *fixture = (ui_fixture){ 0 };
    fixture->world = ecs_world_new();
    fixture->viewport = engine_viewport_new(640, 360);
    fixture->graph = editor_new(behavior_asset_new("Native UI"));
    fixture->live = fixture->world ? game_live_world_new(fixture->world) : NULL;
    game_live_definition *definition = fixture->live
                                         ? game_live_definition_new("Native UI", behavior_asset_new("Native UI"))
                                         : NULL;
    fixture->runtime = fixture->world && fixture->live
                         ? engine_new(fixture->world, fixture->live)
                         : NULL;
    bool components_ok = fixture->world != NULL;
    for (int i = 0; components_ok && i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        components_ok = ecs_world_register_component(fixture->world, GAME_REGISTRY_COMPONENTS[i], NULL) ==
                        ECS_OK;
    }
    if (!fixture->world || !fixture->viewport || !fixture->graph ||
        !fixture->live || !definition || !fixture->runtime || !components_ok ||
        game_live_add_definition(fixture->live, definition) != 0) {
        game_live_definition_free(definition);
        ui_fixture_free(fixture);
        return false;
    }
    fixture->scene = app_scene_authoring_new(
        fixture->world,
        fixture->live,
        definition,
        editor_behavior_asset(fixture->graph),
        fixture->graph,
        NULL,
        NULL,
        fixture->runtime
    );
    bool ready = fixture->scene &&
                 app_scene_authoring_load_content(fixture->scene) == 0 &&
                 register_fixture_prototypes(fixture->scene) &&
                 app_scene_authoring_load(fixture->scene, "test/fixtures/editor_scene.json") ==
                     0 &&
                 app_scene_authoring_apply_pending(fixture->scene) == 0;
    if (!ready) {
        ui_fixture_free(fixture);
        return false;
    }
    editor_set_event_catalog(
        fixture->graph,
        APP_EVENT_CATALOG,
        (int)(sizeof APP_EVENT_CATALOG / sizeof *APP_EVENT_CATALOG)
    );
    editor_set_ui_context(fixture->graph, EDITOR_UI_CONTEXT_BEHAVIOR);
    editor_set_graph_visible(fixture->graph, 1);
    editor_set_inspector_active(fixture->graph, true);
    editor_ui_reset_layout();
    engine_set_focus(fixture->runtime, ENGINE_FOCUS_VIEWPORT);
    engine_viewport_set_render_callback(
        fixture->viewport,
        app_scene_authoring_render,
        fixture->scene
    );
    if (app_scene_authoring_open_behavior(fixture->scene, "InputBehavior") != 0) {
        ui_fixture_free(fixture);
        return false;
    }
    return true;
}

static bool ui_frame(ui_fixture *fixture)
{
    if (!cf_app_is_running()) {
        return false;
    }
    cf_app_update(NULL);
    app_input_frame();
    editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
    fixture->time += CF_DELTA_TIME;
    editor_tick(fixture->graph, fixture->time);
    if (app_scene_authoring_frame(fixture->scene, fixture->time) != FLOW_OK) {
        return false;
    }
    editor_prepare_docking();
    if (app_scene_authoring_draw_viewport(fixture->scene, fixture->viewport) != 0) {
        return false;
    }
    app_scene_authoring_draw(fixture->scene);
    editor_draw(fixture->graph);
    cf_app_draw_onto_screen(true);
    return true;
}

static bool move_to_item(ui_fixture *fixture, const char *id)
{
    float x, y;
    if (!editor_ui_item_center(id, &x, &y)) {
        return false;
    }
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), x, y);
    return ui_frame(fixture);
}

static bool click_item(ui_fixture *fixture, const char *id)
{
    float x, y;
    if (!editor_ui_item_center(id, &x, &y)) {
        return false;
    }
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), x, y);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
    if (!ui_frame(fixture)) {
        return false;
    }
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
    return ui_frame(fixture);
}

static bool native_app_new(const char *program)
{
    CF_Result result = cf_make_app(
        "c-blueprints native UI checks",
        0,
        0,
        0,
        UI_WIDTH,
        UI_HEIGHT,
        CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_RESIZABLE_BIT |
            CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "native UI app: %s\n", result.details ? result.details : "unknown error");
        return false;
    }
    app_scene_renderer_initialize();
    cf_app_init_imgui();
    app_input_initialize();
    editor_ui_initialize(false);
    cf_app_set_imgui_render_target(cf_app_get_canvas());
    editor_ui_set_framebuffer_target(true);
    editor_ui_set_framebuffer_size(UI_WIDTH, UI_HEIGHT);
    return true;
}

static bool workspace_scenario()
{
    ui_fixture fixture;
    if (!ui_fixture_new(&fixture)) {
        return false;
    }
    bool ok = ui_frame(&fixture);
    editor_ui_rect content, inspector, graph, scene;
    bool content_pane = editor_ui_pane_rect(EDITOR_UI_PANE_CONTENT, &content);
    bool inspector_pane = editor_ui_pane_rect(EDITOR_UI_PANE_INSPECTOR, &inspector);
    bool graph_pane = editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &graph);
    bool scene_pane = editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &scene);
    bool viewport = editor_ui_item_rect("viewport.image", &content);
    ok &= content_pane && inspector_pane && graph_pane && scene_pane && viewport;
    float scene_height = scene.height;
    editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, false);
    ok &= ui_frame(&fixture) && ui_frame(&fixture);
    ok &= editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &scene);
    bool expanded = scene.height > scene_height + 20.0f;
    ok &= expanded;
    editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, true);
    ok &= ui_frame(&fixture) && ui_frame(&fixture);
    bool graph_restored = editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &graph);
    bool scene_restored = editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &scene);
    ok &= graph_restored && scene_restored && scene.height < UI_HEIGHT;
    if (!ok) {
        printf(
            "ui-native workspace detail: frame=%d panes=%d/%d/%d/%d "
            "viewport=%d "
            "expanded=%d restored=%d/%d heights=%g/%g\n",
            (int)fixture.time,
            content_pane,
            inspector_pane,
            graph_pane,
            scene_pane,
            viewport,
            expanded,
            graph_restored,
            scene_restored,
            scene_height,
            scene.height
        );
    }
    printf("ui-native workspace %s: pane layout and graph/scene resizing\n", ok ? "PASS" : "FAIL");
    ui_fixture_free(&fixture);
    return ok;
}

static bool graph_scenario()
{
    ui_fixture fixture;
    if (!ui_fixture_new(&fixture)) {
        return false;
    }
    bool ok = ui_frame(&fixture) && ui_frame(&fixture);
    const graph *active = editor_active_graph(fixture.graph);
    editor_node_ui drawn = { 0 };
    ok &= active && acount(active->nodes) > 0;
    if (ok) {
        ok &= editor_query_node_ui(fixture.graph, active->nodes[0]->id, &drawn);
        ok &= drawn.exec_pins + drawn.data_pins > 0;
        ImGuiIO_AddMousePosEvent(ImGui_GetIO(), drawn.title_x, drawn.title_y);
        ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
        ok &= ui_frame(&fixture);
        ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
        ok &= ui_frame(&fixture);
        editor_node_ui selected = { 0 };
        ok &= editor_query_node_ui(fixture.graph, active->nodes[0]->id, &selected) &&
              selected.selected;
    }
    ok &= move_to_item(&fixture, "graph.fit");
    ok &= editor_ui_item_rect("graph.fit", &(editor_ui_rect){ 0 });
    printf("ui-native graph %s: rendered node selection and graph toolbar input\n", ok ? "PASS" : "FAIL");
    ui_fixture_free(&fixture);
    return ok;
}

static bool scene_scenario()
{
    ui_fixture fixture;
    if (!ui_fixture_new(&fixture)) {
        return false;
    }
    editor_ui_request_focus(EDITOR_UI_FOCUS_WORLD_OUTLINER);
    editor_ui_restore_navigator_tab(EDITOR_UI_PANE_WORLD_OUTLINER);
    bool ok = ui_frame(&fixture);
    bool player_visible = editor_ui_item_rect("scene.actor.Player_1", &(editor_ui_rect){ 0 });
    ok &= player_visible && click_item(&fixture, "scene.actor.Player_1");
    app_runtime_state state = app_scene_authoring_runtime_state(fixture.scene);
    ok &= ecs_entity_is_alive(fixture.world, state.selected_entity);
    ok &= editor_ui_item_rect("viewport.image", &(editor_ui_rect){ 0 });
    if (!ok) {
        printf(
            "ui-native scene detail: player_visible=%d selected=%llu "
            "viewport=%d\n",
            player_visible,
            (unsigned long long)ecs_entity_to_u64(state.selected_entity),
            editor_ui_item_rect("viewport.image", &(editor_ui_rect){ 0 })
        );
    }
    printf("ui-native scene %s: Scene selection reaches the live Inspector and viewport\n", ok ? "PASS" : "FAIL");
    ui_fixture_free(&fixture);
    return ok;
}

int ui_native_run(const char *program)
{
    cf_set_assert_handler(stacktrace_assert_handler);
    if (!native_app_new(program)) {
        return 1;
    }
    bool ok = workspace_scenario() && graph_scenario() && scene_scenario();
    app_input_shutdown();
    cf_destroy_app();
    return ok ? 0 : 1;
}
