#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/event_catalog.h"
#include "editor/workspace/input.h"
#include "editor/workspace/scene_authoring.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "host/stacktrace.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "render/shader_catalog.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"

#include <assert.h>
#include <cute_defines.h>
#define CF_MATH_H
#include <imgui/dcimgui.h>
#undef CF_MATH_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    const char *paths[] = {
        "test/fixtures/prototypes/InputEntity.json",
        "test/fixtures/prototypes/StaticEntity.json",
        "test/fixtures/content/prototypes/Enemy.json",
        "test/fixtures/content/prototypes/Player.json",
        "test/fixtures/content/prototypes/Projectile.json"
    };
    for (int i = 0; i < (int)(sizeof paths / sizeof *paths); i++) {
        char *json = NULL;
        if (!read_file(paths[i], &json)) {
            fprintf(stderr, "native UI fixture: could not read prototype %s\n", paths[i]);
            return false;
        }
        int result = app_scene_authoring_register_prototype(scene, json, paths[i]);
        free(json);
        if (result != 0) {
            fprintf(stderr, "native UI fixture: could not register prototype %s\n", paths[i]);
            return false;
        }
    }
    return true;
}

static bool register_fixture_behaviors(app_scene_authoring *scene)
{
    const char *paths[] = {
        "test/fixtures/content/behaviors/AutoWeaponBehavior.json",
        "test/fixtures/content/behaviors/InputBehavior.json",
        "test/fixtures/content/behaviors/MovementTrailBehavior.json",
        "test/fixtures/content/behaviors/WeaponBehavior.json"
    };
    for (int i = 0; i < (int)(sizeof paths / sizeof *paths); i++) {
        char *json = NULL;
        if (!read_file(paths[i], &json)) {
            return false;
        }
        behavior_asset *asset = behavior_asset_load_json(json);
        free(json);
        if (!asset) {
            return false;
        }
        game_live_definition *definition = game_live_definition_new(asset->name, asset);
        if (!definition) {
            behavior_asset_free(asset);
            return false;
        }
        if (game_live_add_definition(scene->live, definition) != 0) {
            game_live_definition_free(definition);
            return false;
        }
        if (game_prototype_preview_register_behavior(
                scene->prototype_preview,
                asset->name,
                asset
            ) != 0) {
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
                 register_fixture_behaviors(fixture->scene) &&
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
        "chibi native UI checks",
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
    bool ok = ui_frame(&fixture) && ui_frame(&fixture);
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

static int indexed_reference_targeting_graph(
    const asset_index *index,
    int target,
    asset_index_reference_kind kind,
    uint64_t graph_id
)
{
    for (int i = 0; i < asset_index_reference_count(index); i++) {
        const asset_index_reference *reference = asset_index_reference_at(index, i);
        if (reference->target == target && reference->kind == kind &&
            reference->graph_id == graph_id && reference->node_id) {
            return i;
        }
    }
    return -1;
}

static int indexed_reference_from(const asset_index *index, int source, asset_index_reference_kind kind)
{
    for (int i = 0; i < asset_index_reference_count(index); i++) {
        const asset_index_reference *reference = asset_index_reference_at(index, i);
        if (reference->source == source && reference->kind == kind) {
            return i;
        }
    }
    return -1;
}

static bool open_indexed_asset(ui_fixture *fixture, int position)
{
    return app_scene_authoring_open_asset(fixture->scene, position) == 0 &&
           ui_frame(fixture) && ui_frame(fixture);
}

static bool selected_reference_node(const editor *graph_editor, uint64_t graph_id, uint64_t node_id)
{
    const graph *active = editor_active_graph(graph_editor);
    if (!active || active->graph_id != graph_id) {
        return false;
    }
    editor_location location = editor_capture_location(graph_editor);
    bool selected = false;
    for (int i = 0; i < location.node_count; i++) {
        if (location.nodes[i] == node_id) {
            selected = true;
            break;
        }
    }
    editor_node_ui node = { 0 };
    bool visible = editor_query_node_ui(graph_editor, node_id, &node) && node.selected;
    free(location.nodes);
    return selected && visible;
}

static bool visible_graph_node(const editor *graph_editor)
{
    const graph *active = editor_active_graph(graph_editor);
    for (int i = 0; active && i < acount(active->nodes); i++) {
        editor_node_ui node = { 0 };
        if (editor_query_node_ui(graph_editor, active->nodes[i]->id, &node)) {
            return true;
        }
    }
    return false;
}

static void capture_asset_navigation(const char *name)
{
    const char *directory = getenv("CHIBI_ASSET_NAV_CAPTURE");
    if (!directory || !*directory) {
        return;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", directory, name);
    assert(app_save_screenshot(path) == 0);
}

static editor_ui_rect inspector_input_rect()
{
    editor_ui_rect inspector;
    assert(editor_ui_pane_rect(EDITOR_UI_PANE_INSPECTOR, &inspector));
    ImGuiIO *io = ImGui_GetIO();
    assert(io->DisplaySize.x > 0 && io->DisplaySize.y > 0);
    float scale_x = cf_app_get_canvas_width() / io->DisplaySize.x;
    float scale_y = cf_app_get_canvas_height() / io->DisplaySize.y;
    assert(scale_x > 0 && scale_y > 0);
    return (editor_ui_rect){ inspector.x / scale_x,
                             inspector.y / scale_y,
                             inspector.width / scale_x,
                             inspector.height / scale_y };
}

static bool reveal_inspector_item(ui_fixture *fixture, const char *id)
{
    editor_ui_rect inspector = inspector_input_rect();
    bool reduced_motion = editor_ui_reduced_motion();
    editor_ui_set_reduced_motion(true);
    ImGuiIO_AddMousePosEvent(
        ImGui_GetIO(),
        inspector.x + inspector.width * 0.5f,
        inspector.y + inspector.height * 0.5f
    );
    if (!ui_frame(fixture)) {
        editor_ui_set_reduced_motion(reduced_motion);
        return false;
    }
    for (int scroll = 0; scroll < 50; scroll++) {
        editor_ui_rect item;
        if (editor_ui_item_rect(id, &item) && item.y > inspector.y + 35.0f &&
            item.y + item.height < inspector.y + inspector.height - 10.0f) {
            editor_ui_set_reduced_motion(reduced_motion);
            return true;
        }
        ImGuiIO_AddMouseWheelEvent(ImGui_GetIO(), 0, -3);
        if (!ui_frame(fixture)) {
            editor_ui_set_reduced_motion(reduced_motion);
            return false;
        }
    }
    editor_ui_set_reduced_motion(reduced_motion);
    return false;
}

static bool binding_source_compatible(const pin_spec *target, const type_field *field)
{
    pin_spec source = { .type = field->type,
                        .type_name = field->type_name,
                        .kind = PIN_DATA,
                        .container = PIN_CONTAINER_SINGLE };
    return pin_compatible(&source, target);
}

static int binding_choice_index(
    const behavior_asset *asset,
    const pin_spec *target,
    const char *component_key,
    const char *field_name
)
{
    int choice = 0;
    for (int i = 0; i < acount(asset->variables); i++) {
        const behavior_variable *variable = &asset->variables[i];
        if (variable->container == target->container) {
            pin_spec source = { .type = variable->type,
                                .type_name = variable->type_name,
                                .kind = PIN_DATA,
                                .container = variable->container };
            if (pin_compatible(&source, target)) {
                if (!component_key && !field_name) {
                    return choice;
                }
                choice++;
            }
        }
    }
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (component && component->key && component->type && component->type->name) {
            pin_spec source = { .type = PIN_STRUCT,
                                .type_name = component->type->name,
                                .kind = PIN_DATA,
                                .container = PIN_CONTAINER_SINGLE };
            if (pin_compatible(&source, target)) {
                if (component_key && !field_name &&
                    !strcmp(component->key, component_key)) {
                    return choice;
                }
                choice++;
            }
        }
        for (int field = 0;
             component && component->type && field < component->type->field_count;
             field++) {
            const type_field *member = &component->type->fields[field];
            if (!member->name || !binding_source_compatible(target, member)) {
                continue;
            }
            if (component_key && field_name && component->key &&
                !strcmp(component->key, component_key) &&
                !strcmp(member->name, field_name)) {
                return choice;
            }
            choice++;
        }
    }
    if (component_key && !component_key[0] && target->container == PIN_CONTAINER_SINGLE &&
        (target->type == PIN_F32 || target->type == PIN_F64)) {
        return choice;
    }
    return -1;
}

static bool choose_binding_source(ui_fixture *fixture, node *target, const char *search, int choice_index)
{
    editor_select_node(fixture->graph, target);
    char open_id[112];
    snprintf(
        open_id,
        sizeof open_id,
        "graph.input.binding.open.%llu.%d",
        (unsigned long long)target->id,
        1
    );
    bool frame = ui_frame(fixture) && ui_frame(fixture) && ui_frame(fixture);
    bool visible = editor_ui_item_rect(open_id, &(editor_ui_rect){ 0 });
    bool opened = visible && click_item(fixture, open_id);
    bool popup_frame = opened && ui_frame(fixture);
    bool search_visible = popup_frame &&
                          editor_ui_item_rect("graph.binding.search", &(editor_ui_rect){ 0 });
    if (search_visible) {
        capture_asset_navigation("binding-picker");
    }
    bool search_clicked = search_visible &&
                          click_item(fixture, "graph.binding.search");
    if (!frame || !visible || !opened || !search_clicked) {
        return false;
    }
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), search);
    if (!ui_frame(fixture)) {
        return false;
    }
    char id[112];
    snprintf(
        id,
        sizeof id,
        "graph.binding.choice.%llu.%d.%d",
        (unsigned long long)target->id,
        1,
        choice_index
    );
    bool selected = click_item(fixture, id);
    return selected;
}

static bool open_binding_popup_at(ui_fixture *fixture, node *target, int pin)
{
    editor_select_node(fixture->graph, target);
    bool frames = ui_frame(fixture) && ui_frame(fixture) && ui_frame(fixture);
    if (!frames) {
        return false;
    }
    char id[112];
    snprintf(
        id,
        sizeof id,
        "graph.input.binding.%llu.%d",
        (unsigned long long)target->id,
        pin
    );
    bool visible = editor_ui_item_rect(id, &(editor_ui_rect){ 0 });
    bool clicked = visible && click_item(fixture, id);
    if (!visible || !clicked) {
        return false;
    }
    bool settled = true;
    bool search = editor_ui_item_rect("graph.binding.search", &(editor_ui_rect){ 0 });
    if (!search) {
        settled = ui_frame(fixture);
        search = settled &&
                 editor_ui_item_rect("graph.binding.search", &(editor_ui_rect){ 0 });
    }
    if (!search) {
    }
    return search;
}

static bool open_binding_popup(ui_fixture *fixture, node *target)
{
    return open_binding_popup_at(fixture, target, 1);
}

static bool binding_popup_action(ui_fixture *fixture, const char *filter, const char *action)
{
    if (!click_item(fixture, "graph.binding.search")) {
        return false;
    }
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), filter);
    if (!ui_frame(fixture) || !editor_ui_item_rect(action, &(editor_ui_rect){ 0 })) {
        return false;
    }
    return click_item(fixture, action);
}

static bool choose_binding_search_enter(ui_fixture *fixture, node *target, int pin, const char *search)
{
    editor_select_node(fixture->graph, target);
    char open_id[112];
    snprintf(
        open_id,
        sizeof open_id,
        "graph.input.binding.open.%llu.%d",
        (unsigned long long)target->id,
        pin
    );
    bool frame = ui_frame(fixture) && ui_frame(fixture) && ui_frame(fixture);
    bool visible = editor_ui_item_rect(open_id, &(editor_ui_rect){ 0 });
    bool opened = visible && click_item(fixture, open_id);
    bool search_visible = opened &&
                          editor_ui_item_rect("graph.binding.search", &(editor_ui_rect){ 0 });
    if (!search_visible) {
        search_visible = opened && ui_frame(fixture) &&
                         editor_ui_item_rect("graph.binding.search", &(editor_ui_rect){ 0 });
    }
    bool search_clicked = search_visible &&
                          click_item(fixture, "graph.binding.search");
    if (!frame || !visible || !opened || !search_clicked) {
        return false;
    }
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), search);
    if (!ui_frame(fixture)) {
        return false;
    }
    capture_asset_navigation("movement-binding-picker");
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, true);
    if (!ui_frame(fixture)) {
        return false;
    }
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, false);
    return ui_frame(fixture);
}

static bool movement_component_binding_scenario()
{
    ui_fixture fixture;
    if (!ui_fixture_new(&fixture)) {
        return false;
    }
    graph *active = (graph *)editor_active_graph(fixture.graph);
    node *call = NULL;
    for (int i = 0; active && i < acount(active->nodes); i++) {
        node *candidate = active->nodes[i];
        if (candidate->reference_asset && candidate->reference_name &&
            !strcmp(candidate->reference_asset, "MovementActions") &&
            !strcmp(candidate->reference_name, "Movement Velocity")) {
            call = candidate;
            break;
        }
    }
    bool call_shape = call && call->input_count > 1 &&
                      call->input_pins[1].kind == PIN_DATA &&
                      call->input_pins[1].type == PIN_STRUCT &&
                      call->input_pins[1].type_name &&
                      !strcmp(call->input_pins[1].type_name, "GameMovement");
    node *source = NULL;
    int source_pin = -1;
    bool connected = call_shape &&
                     graph_input_source(active, call, 1, &source, &source_pin);
    bool source_shape = connected && source && source->desc && source->desc->name &&
                        !strcmp(source->desc->name, "Get_GameMovement") &&
                        source_pin == 0;
    bool disconnected = source_shape &&
                        graph_remove_edge(active, source, source_pin, call, 1) == 0;
    bool chosen = disconnected &&
                  choose_binding_search_enter(&fixture, call, 1, "Get Movement");
    const pin_binding *binding = call ? graph_pin_binding(active, call, 0, 1) : NULL;
    bool whole_component = chosen && binding &&
                           binding->kind == PIN_BINDING_COMPONENT &&
                           binding->component_key &&
                           !strcmp(
                               binding->component_key,
                               GAME_COMPONENT_GameMovement.key
                           ) &&
                           !binding->field_name;
    uint64_t call_id = call ? call->id : 0;
    bool convert_open = whole_component && open_binding_popup(&fixture, call);
    bool converted = convert_open &&
                     binding_popup_action(&fixture, "Get Movement", "graph.binding.convert");
    node *converted_source = NULL;
    int converted_pin = -1;
    bool getter = converted && !graph_pin_binding(active, call, 0, 1) &&
                  graph_input_source(active, call, 1, &converted_source, &converted_pin) &&
                  converted_source && converted_source->desc &&
                  converted_source->desc->name &&
                  !strcmp(converted_source->desc->name, "Get_GameMovement") &&
                  converted_pin == 0;
    if (converted) {
        editor_undo(fixture.graph);
    }
    bool undo_frame = converted && ui_frame(&fixture);
    active = (graph *)editor_active_graph(fixture.graph);
    call = call_id ? graph_node_by_id(active, call_id) : NULL;
    node *undo_source = NULL;
    int undo_pin = -1;
    const pin_binding *restored = call ? graph_pin_binding(active, call, 0, 1) : NULL;
    bool restored_binding = undo_frame && restored &&
                            restored->kind == PIN_BINDING_COMPONENT &&
                            restored->component_key &&
                            !strcmp(
                                restored->component_key,
                                GAME_COMPONENT_GameMovement.key
                            ) &&
                            !restored->field_name &&
                            !graph_input_source(active, call, 1, &undo_source, &undo_pin);
    bool ok = call_shape && source_shape && disconnected && chosen &&
              whole_component && convert_open && converted && getter &&
              restored_binding;
    printf(
        "ui-native movement binding %s: MovementActions speed picker, whole "
        "component, "
        "getter conversion, Undo "
        "(call=%d/source=%d/disconnected=%d/choice=%d/whole=%d/convert=%d/"
        "getter=%d/undo=%d)\n",
        ok ? "PASS" : "FAIL",
        call_shape,
        source_shape,
        disconnected,
        chosen,
        whole_component,
        converted,
        getter,
        restored_binding
    );
    ui_fixture_free(&fixture);
    return ok;
}

static node *find_node_by_desc(const graph *active, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    for (int i = 0; active && i < acount(active->nodes); i++) {
        node *candidate = active->nodes[i];
        if (candidate->desc == desc) {
            return candidate;
        }
    }
    return NULL;
}

static bool inline_source_shape(
    const graph *active,
    node *target,
    int target_pin,
    const char *source_name,
    node **source_out
)
{
    node *source = NULL;
    int source_pin = -1;
    node *edge_source = NULL;
    int edge_pin = -1;
    bool inline_source = graph_inline_source(active, target, target_pin, &source, &source_pin) ==
                         1;
    bool edge = graph_input_source(active, target, target_pin, &edge_source, &edge_pin) ==
                1;
    bool shape = inline_source && edge && source == edge_source &&
                 source_pin == edge_pin && source->desc && source->desc->name &&
                 !strcmp(source->desc->name, source_name) &&
                 source->input_count == 0 && graph_node_is_inline(active, source);
    if (source_out) {
        *source_out = shape ? source : NULL;
    }
    return shape;
}

static bool inline_function_shape(
    const graph *active,
    node *target,
    int target_pin,
    const char *function_name,
    node **source_out
)
{
    node *source = NULL;
    bool shape = inline_source_shape(active, target, target_pin, "Call Pure Function", &source) &&
                 source->reference_name &&
                 !strcmp(source->reference_name, function_name);
    if (source_out) {
        *source_out = shape ? source : NULL;
    }
    return shape;
}

static bool inline_source_rendering_shape(ui_fixture *fixture, node *target, node *source, int target_pin)
{
    char chip[112];
    snprintf(
        chip,
        sizeof chip,
        "graph.input.binding.%llu.%d",
        (unsigned long long)target->id,
        target_pin
    );
    editor_node_ui source_ui = { 0 };
    editor_node_ui target_ui = { 0 };
    return source && !editor_query_node_ui(fixture->graph, source->id, &source_ui) &&
           editor_query_node_ui(fixture->graph, target->id, &target_ui) &&
           editor_ui_item_rect(chip, &(editor_ui_rect){ 0 });
}

static bool binding_search_has_no_choice(ui_fixture *fixture, node *target, int target_pin, const char *search)
{
    editor_select_node(fixture->graph, target);
    editor_reveal_node(fixture->graph, target);
    if (!ui_frame(fixture) || !ui_frame(fixture) || !ui_frame(fixture)) {
        return false;
    }
    char open_id[112];
    snprintf(
        open_id,
        sizeof open_id,
        "graph.input.binding.open.%llu.%d",
        (unsigned long long)target->id,
        target_pin
    );
    if (!click_item(fixture, open_id) || !ui_frame(fixture)) {
        return false;
    }
    if (!click_item(fixture, "graph.binding.search")) {
        return false;
    }
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), search);
    if (!ui_frame(fixture)) {
        return false;
    }
    bool choice = false;
    for (int index = 0; index < 1024; index++) {
        char id[112];
        snprintf(
            id,
            sizeof id,
            "graph.binding.choice.%llu.%d.%d",
            (unsigned long long)target->id,
            target_pin,
            index
        );
        choice |= editor_ui_item_rect(id, &(editor_ui_rect){ 0 });
    }
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Escape, true);
    bool closed = ui_frame(fixture);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Escape, false);
    closed = closed && ui_frame(fixture);
    return !choice && closed;
}

static bool generic_inline_source_scenario()
{
    ui_fixture fixture;
    if (!ui_fixture_new(&fixture)) {
        return false;
    }
    graph *active = (graph *)editor_active_graph(fixture.graph);
    node *movement = find_node_by_desc(active, "Call Pure Function");
    behavior_asset *asset = (behavior_asset *)editor_behavior_asset(fixture.graph);
    graph *zero_function = asset
                             ? behavior_asset_add_function(asset, "Zero Scalar", true)
                             : NULL;
    node *zero_return = zero_function
                          ? graph_node_by_id(zero_function, zero_function->return_id)
                          : NULL;
    pin_spec zero_result = {
        .name = "Result",
        .type = PIN_F32,
        .type_name = "float",
        .kind = PIN_DATA,
    };
    bool zero_function_ready = zero_function && zero_return &&
                               graph_add_pin(zero_function, zero_return, 0, &zero_result) >=
                                   0;
    node *health_target = editor_spawn(
        fixture.graph,
        registry_node_by_name("Break_GameHealth"),
        250.0f,
        -300.0f
    );
    node *entity_target = editor_spawn(
        fixture.graph,
        registry_node_by_name("GetEntity_GameHealth"),
        250.0f,
        -100.0f
    );
    node *wildcard_target = editor_spawn(fixture.graph, registry_node_by_name("Reroute Data"), 250.0f, 100.0f);
    node *scalar_target = editor_spawn(
        fixture.graph,
        registry_node_by_name("Set_GameHealth_current"),
        250.0f,
        300.0f
    );
    bool targets = movement && health_target && entity_target &&
                   wildcard_target && scalar_target && zero_function_ready &&
                   movement->input_count > 1 &&
                   movement->input_pins[0].kind == PIN_DATA &&
                   movement->input_pins[0].type_name &&
                   !strcmp(movement->input_pins[0].type_name, "CF_V2") &&
                   health_target->input_count > 0 && entity_target->input_count > 0 &&
                   wildcard_target->input_count > 0;
    bool inputful_source_hidden = false;
    if (targets) {
        editor_reveal_node(fixture.graph, health_target);
        inputful_source_hidden = binding_search_has_no_choice(&fixture, health_target, 0, "Get Entity Health");
        editor_reveal_node(fixture.graph, scalar_target);
        inputful_source_hidden &= binding_search_has_no_choice(&fixture, scalar_target, 1, "Array Length");
    }
    bool removed_direction = false;
    if (targets) {
        node *old_source = NULL;
        int old_pin = -1;
        removed_direction = graph_input_source(active, movement, 0, &old_source, &old_pin)
                              ? graph_remove_edge(active, old_source, old_pin, movement, 0) == 0
                              : true;
    }

    if (targets && removed_direction) {
        editor_reveal_node(fixture.graph, movement);
    }
    bool transform_chosen = targets && removed_direction &&
                            choose_binding_search_enter(&fixture, movement, 0, "Value GameTransform");
    active = (graph *)editor_active_graph(fixture.graph);
    node *transform_source = NULL;
    bool transform_inline = transform_chosen &&
                            inline_source_shape(active, movement, 0, "Get_GameTransform", &transform_source);
    bool transform_rendered = transform_inline &&
                              inline_source_rendering_shape(&fixture, movement, transform_source, 0);

    if (transform_inline) {
        editor_reveal_node(fixture.graph, entity_target);
    }
    bool self_chosen = transform_inline &&
                       choose_binding_search_enter(&fixture, entity_target, 0, "Self Self ecs.entity");
    active = (graph *)editor_active_graph(fixture.graph);
    node *self_source = NULL;
    bool self_inline = self_chosen &&
                       inline_source_shape(active, entity_target, 0, "Self", &self_source);
    bool self_rendered = self_inline &&
                         inline_source_rendering_shape(&fixture, entity_target, self_source, 0);

    if (self_inline) {
        editor_reveal_node(fixture.graph, wildcard_target);
    }
    bool wildcard_chosen = self_inline &&
                           choose_binding_search_enter(&fixture, wildcard_target, 0, "Seconds float");
    active = (graph *)editor_active_graph(fixture.graph);
    node *delta_source = NULL;
    bool wildcard_inline = wildcard_chosen &&
                           inline_source_shape(active, wildcard_target, 0, "Delta Time", &delta_source) &&
                           wildcard_target->input_pins[0].type == PIN_F32;
    bool wildcard_rendered = wildcard_inline &&
                             inline_source_rendering_shape(&fixture, wildcard_target, delta_source, 0);

    if (wildcard_inline) {
        editor_reveal_node(fixture.graph, scalar_target);
    }
    bool scalar_chosen = wildcard_inline &&
                         choose_binding_search_enter(&fixture, scalar_target, 1, "Zero Scalar");
    active = (graph *)editor_active_graph(fixture.graph);
    node *scalar_source = NULL;
    bool scalar_inline = scalar_chosen &&
                         inline_function_shape(active, scalar_target, 1, "Zero Scalar", &scalar_source) &&
                         graph_input_conversion(active, scalar_target, 1) != NULL;
    bool scalar_rendered = scalar_inline &&
                           inline_source_rendering_shape(&fixture, scalar_target, scalar_source, 1);

    if (scalar_inline) {
        editor_reveal_node(fixture.graph, health_target);
    }
    bool health_chosen = scalar_inline &&
                         choose_binding_search_enter(&fixture, health_target, 0, "Value GameHealth");
    active = (graph *)editor_active_graph(fixture.graph);
    node *health_source = NULL;
    bool health_inline = health_chosen &&
                         inline_source_shape(active, health_target, 0, "Get_GameHealth", &health_source);
    bool health_rendered = health_inline &&
                           inline_source_rendering_shape(&fixture, health_target, health_source, 0);

    uint64_t health_id = health_target ? health_target->id : 0;
    char *json = asset ? behavior_asset_save_json(asset) : NULL;
    behavior_asset *roundtrip = json ? behavior_asset_load_json(json) : NULL;
    graph *loaded = roundtrip ? roundtrip->graph : NULL;
    node *loaded_transform = loaded ? graph_node_by_id(loaded, movement->id) : NULL;
    node *loaded_self = loaded ? graph_node_by_id(loaded, entity_target->id) : NULL;
    node *loaded_wildcard = loaded ? graph_node_by_id(loaded, wildcard_target->id)
                                   : NULL;
    node *loaded_scalar = loaded ? graph_node_by_id(loaded, scalar_target->id) : NULL;
    node *loaded_health = loaded ? graph_node_by_id(loaded, health_target->id) : NULL;
    bool roundtrip_ok = loaded && loaded_transform && loaded_self &&
                        loaded_wildcard && loaded_scalar && loaded_health &&
                        inline_source_shape(loaded, loaded_transform, 0, "Get_GameTransform", NULL) &&
                        inline_source_shape(loaded, loaded_self, 0, "Self", NULL) &&
                        inline_source_shape(loaded, loaded_wildcard, 0, "Delta Time", NULL) &&
                        inline_function_shape(loaded, loaded_scalar, 1, "Zero Scalar", NULL) &&
                        inline_source_shape(loaded, loaded_health, 0, "Get_GameHealth", NULL);
    behavior_asset_free(roundtrip);
    free(json);

    bool undo_frame = false;
    bool undo_cleared = false;
    bool redo_frame = false;
    bool redo_restored = false;
    if (health_inline) {
        editor_undo(fixture.graph);
        undo_frame = ui_frame(&fixture);
        active = (graph *)editor_active_graph(fixture.graph);
        node *undo_target = graph_node_by_id(active, health_id);
        node *undo_source = NULL;
        int undo_pin = -1;
        undo_cleared = undo_target &&
                       !graph_inline_source(active, undo_target, 0, &undo_source, &undo_pin);
        editor_redo(fixture.graph);
        redo_frame = ui_frame(&fixture);
        active = (graph *)editor_active_graph(fixture.graph);
        health_target = graph_node_by_id(active, health_id);
        redo_restored = health_target &&
                        inline_source_shape(active, health_target, 0, "Get_GameHealth", &health_source);
    }

    bool expand_open = redo_restored &&
                       open_binding_popup_at(&fixture, health_target, 0);
    bool expanded = expand_open &&
                    binding_popup_action(&fixture, "Value GameHealth", "graph.binding.convert");
    if (expanded) {
        expanded = ui_frame(&fixture) && ui_frame(&fixture);
    }
    active = (graph *)editor_active_graph(fixture.graph);
    node *expanded_source = NULL;
    int expanded_pin = -1;
    bool edge_preserved = expanded &&
                          graph_input_source(active, health_target, 0, &expanded_source, &expanded_pin) &&
                          expanded_source && expanded_source->desc &&
                          !strcmp(expanded_source->desc->name, "Get_GameHealth") &&
                          expanded_pin == 0 &&
                          !graph_inline_source(active, health_target, 0, &expanded_source, &expanded_pin) &&
                          !graph_node_is_inline(active, expanded_source);
    editor_node_ui expanded_ui = { 0 };
    bool expanded_rendered = expanded && editor_query_node_ui(
                                             fixture.graph,
                                             expanded_source ? expanded_source->id : 0,
                                             &expanded_ui
                                         );
    bool ok = targets && inputful_source_hidden && removed_direction &&
              transform_inline && wildcard_inline && scalar_inline && health_inline &&
              transform_rendered && self_rendered && wildcard_rendered &&
              scalar_rendered && health_rendered && roundtrip_ok &&
              undo_frame && undo_cleared && redo_frame && redo_restored &&
              expand_open && expanded && edge_preserved && expanded_rendered;
    printf(
        "ui-native generic inline sources %s: component, alias, Self, "
        "wildcard, "
        "zero-arg function, scalar conversion, "
        "save/reload, Undo/Redo, Expand source "
        "(targets=%d/inputful=%d/removed=%d/transform=%d/%d/self=%d/%d/"
        "wildcard=%d/%d/"
        "scalar=%d/%d/health=%d/%d/roundtrip=%d/undo=%d/%d/redo=%d/%d/"
        "expand=%d/%d/%d/%d)\n",
        ok ? "PASS" : "FAIL",
        targets,
        inputful_source_hidden,
        removed_direction,
        transform_inline,
        transform_rendered,
        self_inline,
        self_rendered,
        wildcard_inline,
        wildcard_rendered,
        scalar_inline,
        scalar_rendered,
        health_inline,
        health_rendered,
        roundtrip_ok,
        undo_frame,
        undo_cleared,
        redo_frame,
        redo_restored,
        expand_open,
        expanded,
        edge_preserved,
        expanded_rendered
    );
    ui_fixture_free(&fixture);
    return ok;
}

static bool material_inline_source_scenario()
{
    ui_fixture fixture;
    if (!ui_fixture_new(&fixture)) {
        return false;
    }
    bool ready = ui_frame(&fixture) && ui_frame(&fixture);
    const asset_index *index = app_scene_authoring_asset_index(fixture.scene);
    int position = asset_index_find_name(index, ASSET_INDEX_MATERIAL, "ArenaFloor");
    bool opened = ready && position >= 0 && open_indexed_asset(&fixture, position);
    graph *active = (graph *)editor_active_graph(fixture.graph);
    node *surface = opened ? find_node_by_desc(active, "Shader Surface Output") : NULL;
    if (!surface) {
        fprintf(
            stderr,
            "material source setup failed: position=%d opened=%d asset=%s "
            "nodes=%d\n",
            position,
            opened,
            editor_behavior_asset(fixture.graph)->name,
            active ? acount(active->nodes) : -1
        );
        ui_fixture_free(&fixture);
        return false;
    }
    node *previous = NULL;
    int previous_pin = -1;
    if (graph_input_source(active, surface, 2, &previous, &previous_pin)) {
        graph_remove_edge(active, previous, previous_pin, surface, 2);
    }
    editor_reveal_node(fixture.graph, surface);
    bool chosen = opened && choose_binding_search_enter(&fixture, surface, 2, "Time");
    node *source = NULL;
    int source_pin = -1;
    bool compact = chosen &&
                   graph_inline_source(active, surface, 2, &source, &source_pin) &&
                   !strcmp(source->desc->name, "Shader Time") &&
                   inline_source_rendering_shape(&fixture, surface, source, 2);
    int node_count = acount(active->nodes);
    bool cleared = compact && open_binding_popup_at(&fixture, surface, 2) &&
                   binding_popup_action(&fixture, "", "graph.binding.clear") &&
                   acount(active->nodes) == node_count - 1 &&
                   !graph_input_source(active, surface, 2, &source, &source_pin);
    bool constant = cleared &&
                    choose_binding_search_enter(&fixture, surface, 2, "Constant") &&
                    graph_inline_source(active, surface, 2, &source, &source_pin) &&
                    !strcmp(source->desc->name, "Shader Constant") &&
                    source->output_pins[source_pin].type == PIN_F32;
    if (constant) {
        capture_asset_navigation("material-inline-source");
    }
    bool ok = compact && cleared && constant;
    printf(
        "ui-native material inline sources %s: Time, clear ownership, "
        "initialized Constant (%d/%d/%d)\n",
        ok ? "PASS" : "FAIL",
        compact,
        cleared,
        constant
    );
    ui_fixture_free(&fixture);
    return ok;
}

static node *spawn_delay_input(editor *editor)
{
    node *delay = editor_spawn(editor, registry_node_by_name("Delay"), 0.0f, 0.0f);
    if (delay) {
        editor_select_node(editor, delay);
    }
    return delay;
}

static bool binding_scenario()
{
    ui_fixture fixture;
    if (!ui_fixture_new(&fixture)) {
        return false;
    }
    behavior_asset *asset = (behavior_asset *)editor_behavior_asset(fixture.graph);
    pin_value initial = { .type = PIN_F64, .type_name = "double", .d = 0.0 };
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "WaitSeconds",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        &initial,
        "Timing"
    );
    uint64_t variable_id = variable ? variable->id : 0;
    char variable_name[128];
    snprintf(
        variable_name,
        sizeof variable_name,
        "%s",
        variable && variable->name ? variable->name : ""
    );
    char asset_name[256];
    snprintf(asset_name, sizeof asset_name, "%s", asset && asset->name ? asset->name : "");
    node *variable_target = spawn_delay_input(fixture.graph);
    bool ok = variable && variable_target;
    const pin_spec *target = variable_target ? &variable_target->input_pins[1] : NULL;
    int variable_choice = target ? binding_choice_index(asset, target, NULL, NULL)
                                 : -1;
    ok &= variable_choice == 0;
    if (ok) {
        bool chosen = choose_binding_source(&fixture, variable_target, "WaitSeconds", variable_choice);
        ok &= chosen;
        uint64_t variable_target_id = variable_target->id;
        const pin_binding *binding = graph_pin_binding(
            editor_active_graph(fixture.graph),
            variable_target,
            0,
            1
        );
        ok &= binding && binding->kind == PIN_BINDING_VARIABLE &&
              binding->variable_id == variable->id && binding->source_asset &&
              !strcmp(binding->source_asset, asset_name);
        char chip[96];
        snprintf(
            chip,
            sizeof chip,
            "graph.input.binding.%llu.1",
            (unsigned long long)variable_target->id
        );
        bool chip_frame = ui_frame(&fixture) && ui_frame(&fixture);
        bool chip_rect = chip_frame &&
                         editor_ui_item_rect(chip, &(editor_ui_rect){ 0 });
        if (chip_rect) {
            capture_asset_navigation("binding-chip");
        }
        bool chip_clicked = chip_rect && click_item(&fixture, chip);
        bool popup_settled = chip_clicked;
        bool search_visible = popup_settled &&
                              editor_ui_item_rect("graph.binding.search", &(editor_ui_rect){ 0 });
        if (!search_visible) {
            popup_settled = popup_settled && ui_frame(&fixture);
            search_visible = popup_settled &&
                             editor_ui_item_rect("graph.binding.search", &(editor_ui_rect){ 0 });
        }
        ok &= chip_rect && chip_clicked && search_visible;
        node *clipboard_nodes[] = { variable_target };
        char *binding_clipboard = graph_save_nodes_json(
            editor_active_graph(fixture.graph),
            clipboard_nodes,
            1
        );
        bool clipboard_saved = binding_clipboard != NULL;
        ok &= clipboard_saved;
        bool jump_clicked = chip_clicked &&
                            binding_popup_action(&fixture, variable_name, "graph.binding.jump");
        bool jump_inspector = jump_clicked &&
                              editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR);
        bool clear_open = jump_clicked &&
                          open_binding_popup(&fixture, variable_target);
        bool clear_clicked = clear_open &&
                             binding_popup_action(&fixture, variable_name, "graph.binding.clear");
        bool cleared = graph_pin_binding(editor_active_graph(fixture.graph), variable_target, 0, 1) ==
                       NULL;
        ok &= jump_clicked && jump_inspector && clear_clicked && cleared;
        editor_undo(fixture.graph);
        bool clear_undo_frame = ui_frame(&fixture) && ui_frame(&fixture);
        ok &= clear_undo_frame;
        variable_target = graph_node_by_id(editor_active_graph(fixture.graph), variable_target_id);
        binding = variable_target
                    ? graph_pin_binding(editor_active_graph(fixture.graph), variable_target, 0, 1)
                    : NULL;
        bool restored = binding && binding->kind == PIN_BINDING_VARIABLE;
        ok &= restored;
        bool refreshed = app_scene_authoring_refresh_asset_index(fixture.scene) == 0;
        ok &= refreshed;
        const asset_index *index = app_scene_authoring_asset_index(fixture.scene);
        int source_position = index
                                ? asset_index_find_name(index, ASSET_INDEX_BEHAVIOR, asset_name)
                                : -1;
        bool indexed = false;
        for (int i = 0; index && i < asset_index_binding_usage_count(index); i++) {
            const asset_index_binding_usage *usage = asset_index_binding_usage_at(index, i);
            if (usage && usage->source == source_position &&
                usage->kind == PIN_BINDING_VARIABLE &&
                usage->node_id == variable_target_id &&
                usage->pin_id == variable_target->input_pins[1].id &&
                usage->variable_id == variable_id && usage->variable_name &&
                !strcmp(usage->variable_name, variable_name)) {
                indexed = true;
                break;
            }
        }
        ok &= indexed;
        editor_undo(fixture.graph);
        bool undo_frame = ui_frame(&fixture);
        editor_redo(fixture.graph);
        bool redo_frame = ui_frame(&fixture);
        variable_target = graph_node_by_id(editor_active_graph(fixture.graph), variable_target_id);
        int append_result = variable_target && binding_clipboard
                              ? graph_append_json(
                                    (graph *)editor_active_graph(fixture.graph),
                                    binding_clipboard,
                                    24.0f,
                                    24.0f
                                )
                              : -1;
        ok &= undo_frame && redo_frame && variable_target && append_result == 0;
        free(binding_clipboard);
    }
    if (ok) {
        ok &= open_binding_popup(&fixture, variable_target) &&
              binding_popup_action(&fixture, variable_name, "graph.binding.convert");
        ok &= ui_frame(&fixture);
        variable_target = graph_node_by_id(
            editor_active_graph(fixture.graph),
            variable_target ? variable_target->id : 0
        );
        node *getter = NULL;
        for (int i = 0; i < acount(editor_active_graph(fixture.graph)->nodes); i++) {
            node *candidate = editor_active_graph(fixture.graph)->nodes[i];
            if (candidate->desc == registry_node_by_name("Get Variable")) {
                getter = candidate;
                break;
            }
        }
        node *source = NULL;
        int source_pin = -1;
        ok &= variable_target && !variable_target->input_pins[1].binding && getter &&
              graph_input_source(editor_active_graph(fixture.graph), variable_target, 1, &source, &source_pin) &&
              source == getter && source_pin == 0;
        asset = (behavior_asset *)editor_behavior_asset(fixture.graph);
    }
    node *component_target = ok ? spawn_delay_input(fixture.graph) : NULL;
    const ecs_component_desc *component = NULL;
    const type_field *component_field = NULL;
    int component_choice = -1;
    if (component_target) {
        for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT && !component_field; i++) {
            component = GAME_REGISTRY_COMPONENTS[i];
            for (int field = 0; component && component->type &&
                                field < component->type->field_count;
                 field++) {
                if (binding_source_compatible(
                        &component_target->input_pins[1],
                        &component->type->fields[field]
                    )) {
                    component_field = &component->type->fields[field];
                    break;
                }
            }
        }
        component_choice = component && component_field
                             ? binding_choice_index(
                                   asset,
                                   &component_target->input_pins[1],
                                   component->key,
                                   component_field->name
                               )
                             : -1;
        char search[128];
        snprintf(
            search,
            sizeof search,
            "%s.%s",
            component ? component->name : "",
            component_field ? component_field->name : ""
        );
        ok &= component_choice >= 0 &&
              choose_binding_source(&fixture, component_target, search, component_choice);
        const pin_binding *binding = graph_pin_binding(
            editor_active_graph(fixture.graph),
            component_target,
            0,
            1
        );
        ok &= binding && binding->kind == PIN_BINDING_COMPONENT_FIELD &&
              binding->component_key && component &&
              !strcmp(binding->component_key, component->key) && binding->field_name &&
              !strcmp(binding->field_name, component_field->name);
        ok &= app_scene_authoring_refresh_asset_index(fixture.scene) == 0;
        const asset_index *index = app_scene_authoring_asset_index(fixture.scene);
        int source_position = index ? asset_index_find_name(
                                          index,
                                          ASSET_INDEX_BEHAVIOR,
                                          asset->name
                                      )
                                    : -1;
        bool indexed = false;
        for (int i = 0; index && i < asset_index_binding_usage_count(index); i++) {
            const asset_index_binding_usage *usage = asset_index_binding_usage_at(index, i);
            if (usage && usage->source == source_position &&
                usage->kind == PIN_BINDING_COMPONENT_FIELD &&
                usage->node_id == component_target->id &&
                usage->pin_id == component_target->input_pins[1].id && usage->component &&
                component->key && !strcmp(usage->component, component->key) &&
                usage->field && !strcmp(usage->field, component_field->name)) {
                indexed = true;
                break;
            }
        }
        ok &= indexed;
        ok &= open_binding_popup(&fixture, component_target) &&
              binding_popup_action(&fixture, search, "graph.binding.convert");
        ok &= ui_frame(&fixture);
        ok &= graph_pin_binding(editor_active_graph(fixture.graph), component_target, 0, 1) ==
              NULL;
    }
    node *delta_target = ok ? spawn_delay_input(fixture.graph) : NULL;
    if (delta_target) {
        int delta_choice = binding_choice_index(asset, &delta_target->input_pins[1], "", NULL);
        ok &= delta_choice >= 0 &&
              choose_binding_source(&fixture, delta_target, "Delta Time", delta_choice);
        const pin_binding *binding = graph_pin_binding(
            editor_active_graph(fixture.graph),
            delta_target,
            0,
            1
        );
        ok &= binding && binding->kind == PIN_BINDING_DELTA_TIME;
        ok &= open_binding_popup(&fixture, delta_target) &&
              binding_popup_action(&fixture, "Delta Time", "graph.binding.convert");
        ok &= ui_frame(&fixture);
        ok &= graph_pin_binding(editor_active_graph(fixture.graph), delta_target, 0, 1) ==
              NULL;
    }
    printf(
        "ui-native input bindings %s: search, stable source identity, chip, "
        "Clear/Undo, "
        "variable/component/Delta Time conversion\n",
        ok ? "PASS" : "FAIL"
    );
    ui_fixture_free(&fixture);
    return ok;
}

static bool asset_navigation_scenario()
{
    ui_fixture fixture;
    if (!ui_fixture_new(&fixture)) {
        return false;
    }
    bool ok = ui_frame(&fixture) && ui_frame(&fixture);
    const asset_index *index = app_scene_authoring_asset_index(fixture.scene);
    int event = index ? asset_index_find_name(index, ASSET_INDEX_EVENT, "Damage")
                      : -1;
    int library = index ? asset_index_find_name(index, ASSET_INDEX_LIBRARY, "WeaponActions")
                        : -1;
    int prototype = index ? asset_index_find_name(index, ASSET_INDEX_PROTOTYPE, "ArenaPlayer")
                          : -1;
    bool event_title = false;
    bool event_reference = false;
    bool event_source = false;
    bool event_root_graph = false;
    bool event_exact_node = false;
    bool event_back = false;
    bool event_restored = false;
    bool library_title = false;
    bool library_function = false;
    bool library_function_open = false;
    bool library_function_node = false;
    bool library_back = false;
    bool library_overview_after = false;
    bool library_import = true;
    bool prototype_open = false;
    bool prototype_material_open = false;
    bool prototype_back = false;
    bool global_quick_open = false;
    bool quick_search = false;
    bool quick_result = false;
    bool quick_shortcut = false;
    ok &= event >= 0 && library >= 0 && prototype >= 0;

    if (event >= 0) {
        ok &= open_indexed_asset(&fixture, event);
        event_title = editor_ui_item_rect("navigator.event.title", &(editor_ui_rect){ 0 });
        capture_asset_navigation("event-overview");
        ok &= event_title;
        index = app_scene_authoring_asset_index(fixture.scene);
        int reference = indexed_reference_targeting_graph(index, event, ASSET_INDEX_EVENT_CONSUMER, 0);
        if (reference >= 0) {
            const asset_index_reference indexed = *asset_index_reference_at(index, reference);
            char id[64];
            snprintf(id, sizeof id, "asset.reference.%d", reference);
            event_reference = editor_ui_item_rect(id, &(editor_ui_rect){ 0 });
            ok &= event_reference;
            ok &= click_item(&fixture, id);
            ok &= ui_frame(&fixture);
            const behavior_asset *source = editor_behavior_asset(fixture.graph);
            event_source = source && source->domain == BEHAVIOR_DOMAIN_EVENT;
            event_root_graph = indexed.graph_id == 0 && indexed.node_id != 0;
            event_exact_node = selected_reference_node(
                fixture.graph,
                indexed.graph_id,
                indexed.node_id
            );
            ok &= event_root_graph && event_exact_node;
            ok &= event_source;
            if (event_source) {
                event_back = app_scene_authoring_can_navigate(fixture.scene, false) &&
                             app_scene_authoring_navigate(fixture.scene, false) == 0;
                ok &= event_back;
                ok &= ui_frame(&fixture) && ui_frame(&fixture);
                const behavior_asset *restored = editor_behavior_asset(fixture.graph);
                event_restored = restored && restored->domain ==
                                                 BEHAVIOR_DOMAIN_EVENT_DEFINITION;
                capture_asset_navigation("event-overview-back");
                ok &= event_restored;
            }
        } else {
            ok = false;
        }
    }

    if (library >= 0) {
        ok &= open_indexed_asset(&fixture, library);
        library_title = editor_ui_item_rect("navigator.library.title", &(editor_ui_rect){ 0 });
        ok &= library_title;
        const behavior_asset *asset = editor_behavior_asset(fixture.graph);
        int function = -1;
        for (int i = 0; asset && i < acount(asset->graphs); i++) {
            if (asset->graphs[i] && asset->graphs[i]->kind == GRAPH_FUNCTION &&
                !asset->graphs[i]->math_expression) {
                function = i;
                break;
            }
        }
        if (function >= 0) {
            capture_asset_navigation("library-overview");
            char id[96];
            snprintf(
                id,
                sizeof id,
                "library.function.%llu",
                (unsigned long long)asset->graphs[function]->graph_id
            );
            library_function = editor_ui_item_rect(id, &(editor_ui_rect){ 0 });
            ok &= library_function;
            uint64_t function_id = asset->graphs[function]->graph_id;
            ok &= click_item(&fixture, id);
            const graph *active = editor_active_graph(fixture.graph);
            library_function_open = active && active->graph_id == function_id;
            ok &= library_function_open;
            ok &= ui_frame(&fixture);
            library_function_node = visible_graph_node(fixture.graph);
            ok &= library_function_node;
            library_back = editor_ui_item_rect("workspace.back", &(editor_ui_rect){ 0 }) &&
                           click_item(&fixture, "workspace.back");
            ok &= library_back;
            ok &= ui_frame(&fixture);
            library_overview_after = editor_ui_item_rect(
                "navigator.library.title",
                &(editor_ui_rect){ 0 }
            );
            capture_asset_navigation("library-overview-back");
            ok &= library_overview_after;
        } else {
            ok = false;
        }
        index = app_scene_authoring_asset_index(fixture.scene);
        int import = indexed_reference_from(index, library, ASSET_INDEX_GRAPH_CALL);
        if (import >= 0) {
            char id[96];
            snprintf(id, sizeof id, "asset.reference.target.%d", import);
            library_import = editor_ui_item_rect(id, &(editor_ui_rect){ 0 });
            ok &= library_import;
            ok &= click_item(&fixture, id);
            const behavior_asset *target = editor_behavior_asset(fixture.graph);
            ok &= target && target->domain == BEHAVIOR_DOMAIN_LIBRARY;
        }
    }

    if (prototype >= 0) {
        ok &= open_indexed_asset(&fixture, prototype);
        capture_asset_navigation("prototype-overview");
        const scene_prototype_asset *selected = selected_prototype(fixture.scene);
        prototype_open = fixture.scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE &&
                         selected && selected->prototype &&
                         !strcmp(selected->prototype->name, "ArenaPlayer");
        ok &= prototype_open;
        const char
            *material_button = "prototype.field.open.GameShader.material";
        bool material_button_visible = reveal_inspector_item(&fixture, material_button);
        ok &= material_button_visible;
        if (material_button_visible) {
            ok &= click_item(&fixture, material_button);
            ok &= ui_frame(&fixture);
            capture_asset_navigation("prototype-material");
            const behavior_asset *material = editor_behavior_asset(fixture.graph);
            prototype_material_open = material &&
                                      material->domain == BEHAVIOR_DOMAIN_MATERIAL &&
                                      material->name &&
                                      !strcmp(material->name, "ArenaPlayer");
            ok &= prototype_material_open;
            prototype_back = app_scene_authoring_can_navigate(fixture.scene, false) &&
                             app_scene_authoring_navigate(fixture.scene, false) == 0;
            ok &= prototype_back;
            ok &= ui_frame(&fixture) && ui_frame(&fixture);
            selected = selected_prototype(fixture.scene);
            capture_asset_navigation("prototype-overview-back");
            prototype_back = prototype_back &&
                             fixture.scene->document.kind ==
                                 APP_EDITOR_DOCUMENT_PROTOTYPE &&
                             selected && selected->prototype &&
                             !strcmp(selected->prototype->name, "ArenaPlayer");
            ok &= prototype_back;
        }
    }

    editor_ui_set_pane_visible(EDITOR_UI_PANE_CONTENT, false);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_WORLD_OUTLINER, false);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_MY_BEHAVIOR_ASSET, false);
    editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
    ok &= ui_frame(&fixture) && ui_frame(&fixture);
    global_quick_open = !editor_ui_navigator_visible() &&
                        editor_ui_item_rect("content.quick_open", &(editor_ui_rect){ 0 });
    ok &= global_quick_open;
    ImGuiIO *io = ImGui_GetIO();
    ImGuiKey modifier = io->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
    ImGuiIO_AddKeyEvent(io, modifier, true);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_P, true);
    ok &= ui_frame(&fixture);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_P, false);
    ImGuiIO_AddKeyEvent(io, modifier, false);
    ok &= ui_frame(&fixture) && ui_frame(&fixture);
    quick_shortcut = fixture.scene->quick_open &&
                     editor_ui_item_rect("quick.open.search", &(editor_ui_rect){ 0 });
    ok &= quick_shortcut;
    ok &= ui_frame(&fixture);
    capture_asset_navigation("quick-open");
    quick_search = editor_ui_item_rect("quick.open.search", &(editor_ui_rect){ 0 });
    ok &= quick_search;
    ok &= click_item(&fixture, "quick.open.search");
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "Damage");
    ok &= ui_frame(&fixture);
    quick_result = editor_ui_item_rect("quick.open.result.0", &(editor_ui_rect){ 0 });
    ok &= quick_result;
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, true);
    ok &= ui_frame(&fixture);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, false);
    ok &= ui_frame(&fixture);
    const behavior_asset *quick = editor_behavior_asset(fixture.graph);
    ok &= !fixture.scene->quick_open && quick && quick->name &&
          !strcmp(quick->name, "Damage");

    bool migrated_open = false;
    bool migrated_arranged = false;
    bool migrated_fit = false;
    bool migrated_canvas = false;
    index = app_scene_authoring_asset_index(fixture.scene);
    int migrated = index ? asset_index_find_name(index, ASSET_INDEX_BEHAVIOR, "ArenaShipMotionBehavior")
                         : -1;
    if (migrated >= 0) {
        migrated_open = open_indexed_asset(&fixture, migrated);
        if (migrated_open) {
            migrated_arranged = editor_ui_item_rect("graph.arrange", &(editor_ui_rect){ 0 }) &&
                                click_item(&fixture, "graph.arrange");
            ok &= ui_frame(&fixture) && ui_frame(&fixture);
            migrated_fit = editor_ui_item_rect("graph.fit", &(editor_ui_rect){ 0 }) &&
                           click_item(&fixture, "graph.fit");
            ok &= ui_frame(&fixture) && ui_frame(&fixture);
            migrated_canvas = visible_graph_node(fixture.graph);
            capture_asset_navigation("ship-motion-root");
        }
    }
    ok &= migrated_open && migrated_arranged && migrated_fit && migrated_canvas;

    printf(
        "ui-native asset navigation %s: Event/Library overviews, provenance "
        "Back, Prototype material routing, and Quick Open "
        "(assets=%d/%d/%d event=%d/%d/%d/%d/%d/%d/%d "
        "library=%d/%d/%d/%d/%d/%d/%d "
        "prototype=%d/%d/%d quick=%d/%d/%d/%d ship=%d/%d/%d/%d doc=%d/%s)\n",
        ok ? "PASS" : "FAIL",
        event,
        library,
        prototype,
        event_title,
        event_reference,
        event_source,
        event_root_graph,
        event_exact_node,
        event_back,
        event_restored,
        library_title,
        library_function,
        library_function_open,
        library_function_node,
        library_back,
        library_overview_after,
        library_import,
        prototype_open,
        prototype_material_open,
        prototype_back,
        global_quick_open,
        quick_search,
        quick_result,
        quick_shortcut,
        migrated_open,
        migrated_arranged,
        migrated_fit,
        migrated_canvas,
        fixture.scene->document.kind,
        editor_behavior_asset(fixture.graph)->name
    );
    ui_fixture_free(&fixture);
    return ok;
}

int ui_native_run(const char *program)
{
    cf_set_assert_handler(stacktrace_assert_handler);
    if (!native_app_new(program)) {
        return 1;
    }
    bool ok = workspace_scenario() && graph_scenario() && scene_scenario() &&
              asset_navigation_scenario() && binding_scenario() &&
              movement_component_binding_scenario() &&
              generic_inline_source_scenario() && material_inline_source_scenario();
    app_input_shutdown();
    cf_destroy_app();
    return ok ? 0 : 1;
}
