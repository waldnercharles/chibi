#include "../app/cute_host.h"
#include "../app/event_catalog.h"
#include "../app/scene_authoring.h"
#include "../app/scene_renderer.h"
#include "../app/screenshot.h"
#include "../app/stacktrace.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../editor/editor.h"
#include "../editor/eval_view.h"
#include "../editor/ui_shell.h"
#include "../engine/engine.h"
#include "../game/generated.h"
#include "../game/live.h"
#include "../game/prototype.h"
#include "../game/scene_document.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// pi-lens-ignore: fatal error
#include <cute_defines.h>
#define CF_MATH_H
#include <imgui/dcimgui.h>
#undef CF_MATH_H
#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>

#define APP_WINDOW_WIDTH 1920
#define APP_WINDOW_HEIGHT 1080

static void fixed_ui_time(ImGuiContext *, ImGuiContextHook *)
{
    /* The SDL backend overwrites DeltaTime before ImGui starts its frame. */
    ImGui_GetIO()->DeltaTime = 1.0f / 60.0f;
}

extern int test_interior_border();
extern int test_radiance();
extern int test_sdf_coverage(app_sdf_shapes shapes);
static void inject_mouse_button(float x, float y, bool down)
{
    ImGuiIO *io = ImGui_GetIO();
    ImGuiIO_AddMousePosEvent(io, x, y);
    ImGuiIO_AddMouseButtonEvent(io, 0, down);
}

typedef struct
{
    editor *graph_editor;
    engine *runtime;
    ecs_world *world;
    game_live_world *live;
    engine_viewport *viewport;
    app_scene_authoring *authoring;
    int frames;
    int frame;
    int integration_failed;
    char content_root[64];
    char io_root[64];
    ecs_entity io_player;
    ecs_entity io_enemy;
    editor_ui_rect graph_before_focus;
    editor_ui_rect scene_before_expand;
    editor_ui_rect scene_before_play;
    engine_viewport_camera camera_before;
    engine_viewport_view scene_view;
    ImVec2 zoom_anchor;
    ImVec2 zoom_pointer;
    const graph *selection_graph;
    uint64_t selection_node;
    int selection_node_count;
    behavior_asset *preview_before;
    const behavior_instance *prototype_instance;
    uint64_t arrangement_graph;
    uint64_t previous_graph;
    float zoom_before;
    int projectiles_before;
    ecs_entity restart_projectile;
    uint64_t pursuit_call;
    editor_query pursuit_camera;
    float pursuit_click_x, pursuit_click_y;
    float pursuit_node_x, pursuit_node_y;
    float extract_x[2], extract_y[2];
    uint64_t extract_ids[2];
    char *extract_before;
    uint64_t field_node;
    uint64_t field_x_pin;
    ecs_entity scene_edit_entity;
    ecs_entity inspected_spawn;
    GameTransform inspected_spawn_position;
    ImVec2 inspected_spawn_pointer;
    ecs_entity lifecycle_entity;
    ecs_entity search_selection;
    float material_scroll;
    bool material_opening;
    bool narrow_fit_activated;
    GameTransform scene_edit_before;
    GameTransform scene_edit_after;
    ImVec2 scene_edit_pointer;
    bool scene_numeric_opening;
    float sidebar_before;
    ecs_entity interaction_actors[2];
    int live_attachment_count;
    char drop_root[64];
    ImVec2 drop_pointer;
    ImVec2 inspector_position;
    ImGuiID inspector_dock;
} app_loop;

static int runtime_projectiles(ecs_world *world)
{
    ecs_query query;
    ecs_component_id id = GAME_COMPONENT_GameProjectile.id;
    if (ecs_query_begin(world, &query, &id, 1) != ECS_OK) {
        return -1;
    }
    int count = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        count++;
    }
    ecs_query_end(&query);
    return count;
}

static bool workspace_preferences_roundtrip()
{
    ImGuiContext *host = ImGui_GetCurrentContext();
    char path[] = "/tmp/c-blueprints-workspace-XXXXXX";
    int file = mkstemp(path);
    if (file < 0) {
        return false;
    }
    close(file);
    ImGuiContext *first = ImGui_CreateContext(NULL);
    ImGui_SetCurrentContext(first);
    ImGui_GetIO()->IniFilename = NULL;
    editor_ui_initialize(false);
    ImGui_LoadIniSettingsFromMemory("[Blueprints][Workspace]\nFocus=0\nReduceMotion=1\nNavigatorOutline=1\nContent=0\nInspector=1\nGraph=1\nScene=1\n", 0);
    editor_ui_set_focus_mode(true);
    ImGui_SaveIniSettingsToDisk(path);
    ImGui_DestroyContext(first);
    ImGuiContext *second = ImGui_CreateContext(NULL);
    ImGui_SetCurrentContext(second);
    ImGui_GetIO()->IniFilename = NULL;
    editor_ui_initialize(false);
    ImGui_LoadIniSettingsFromDisk(path);
    bool ok = editor_ui_focus_mode() && editor_ui_reduced_motion() &&
              !editor_ui_pane_visible(EDITOR_UI_PANE_CONTENT) &&
              !editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR) &&
              editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS);
    const char *settings = ImGui_SaveIniSettingsToMemory(NULL);
    ok = ok && settings && strstr(settings, "NavigatorOutline=1") != NULL;
    editor_ui_set_focus_mode(false);
    ok = ok && editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR) &&
         !editor_ui_pane_visible(EDITOR_UI_PANE_CONTENT);
    editor_ui_set_scene_play_expanded(true);
    settings = ImGui_SaveIniSettingsToMemory(NULL);
    ok = ok && editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS) &&
         editor_ui_pane_visible(EDITOR_UI_PANE_SCENE) &&
         strstr(settings, "Graph=1") != NULL;
    editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, true);
    editor_ui_set_scene_play_expanded(true);
    ok = ok && editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS);
    editor_ui_set_scene_play_expanded(false);
    ok = ok && editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS);
    ImGui_LoadIniSettingsFromMemory("[Blueprints][Workspace]\nGraph=99\nUnknown=1\n", 0);
    ok = ok && editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS) &&
         !editor_ui_reduced_motion() && !editor_ui_focus_mode();
    ImGui_DestroyContext(second);
    ImGui_SetCurrentContext(host);
    unlink(path);
    printf("ui-workspace-preferences %s: fresh context restores Focus, pane choices and motion preference\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool saved_prototype_has_x(app_loop *loop, float expected)
{
    char path[256];
    snprintf(path, sizeof path, "%s/prototypes/NewPrototype1.json", loop->content_root);
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    char json[4096];
    size_t size = fread(json, 1, sizeof json - 1, file);
    json[size] = '\0';
    fclose(file);
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT,
                                         NULL,
                                         NULL };
    char error[256];
    game_prototype *prototype = game_prototype_load(json, &registry, error, sizeof error);
    pin_value value;
    bool ok = prototype &&
              game_prototype_read_field(prototype, "GameTransform", "x", &value) == 0 &&
              value.type == PIN_F32 && value.f == expected;
    game_prototype_free(prototype);
    return ok;
}

static bool ui_graph_scene_are_stacked()
{
    editor_ui_rect graph;
    editor_ui_rect scene;
    if (!editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &graph) ||
        !editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &scene)) {
        return false;
    }
    float overlap_min = graph.x > scene.x ? graph.x : scene.x;
    float overlap_max = graph.x + graph.width < scene.x + scene.width
                          ? graph.x + graph.width
                          : scene.x + scene.width;
    return graph.height > scene.height * 1.5f && scene.height > 200.0f &&
           graph.y + graph.height <= scene.y + 2.0f &&
           overlap_max - overlap_min > 600.0f;
}

static int app_integration_check(app_loop *loop)
{
    float x, y;
    if (!editor_ui_item_center("content/behaviors", &x, &y) ||
        !editor_ui_item_center("content/prototypes", &x, &y)) {
        fprintf(stderr, "ui-integration: sibling folders must remain navigable while an asset is open\n");
        return -1;
    }
    app_editor_document_context document = app_scene_authoring_document(loop->authoring);
    editor_query query;
    editor_query_state(loop->graph_editor, &query);
    int nodes = query.active_graph ? acount(query.active_graph->nodes) : 0;
    const char *selected = app_scene_authoring_selected_content(loop->authoring);
    bool quiet_diagnostics = !editor_ui_pane_visible(EDITOR_UI_PANE_COMPILER_RESULTS) &&
                             !editor_ui_pane_visible(EDITOR_UI_PANE_OUTPUT_LOG);
    bool graph_scene_stacked = ui_graph_scene_are_stacked();
    int expected_node = 0;
    int pure_reads = 0;
    for (int i = 0; i < nodes; i++) {
        const node *current = query.active_graph->nodes[i];
        if (current->desc && current->desc->name &&
            strcmp(current->desc->name, "Event") == 0 && current->reference_name &&
            strcmp(current->reference_name, "Input") == 0) {
            expected_node = 1;
        }
        if (current->desc == &GAME_NODE_Get_GameTransform ||
            current->desc == &GAME_NODE_Has_GameHealth) {
            editor_node_ui visible;
            bool drawn = editor_query_node_ui(loop->graph_editor, current->id, &visible);
            printf(
                "ui-node %s drawn=%d data=%d exec=%d\n",
                current->desc->name,
                drawn,
                visible.data_pins,
                visible.exec_pins
            );
            if (drawn && visible.exec_pins == 0 && visible.data_pins == 1) {
                pure_reads++;
            }
        }
    }
    int ok = editor_ui_content_drawer_open() &&
             document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR && document.id &&
             strcmp(document.id, "InputBehavior") == 0 && query.graph_visible &&
             expected_node && pure_reads == 2 && graph_scene_stacked &&
             quiet_diagnostics && app_scene_authoring_message(loop->authoring) &&
             strstr(app_scene_authoring_message(loop->authoring), "Editing behavior asset") !=
                 NULL;
    const char *result = ok ? "PASS" : "FAIL";
    printf(
        "ui-integration %s: selected=%s document=%s:%s node_canvas=%d "
        "nodes=%d event_node=%d message=%s\n",
        result,
        selected ? selected : "(none)",
        document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR ? "behavior" : "other",
        document.id ? document.id : "(none)",
        query.graph_visible,
        nodes,
        expected_node,
        app_scene_authoring_message(loop->authoring)
            ? app_scene_authoring_message(loop->authoring)
            : "(none)"
    );
    printf(
        "ui-pure-nodes %s: Get/Has each draw one data pin and no Exec pins\n",
        pure_reads == 2 ? "PASS" : "FAIL"
    );
    printf(
        "ui-layout %s: graph and scene occupy stacked document panes\n",
        graph_scene_stacked ? "PASS" : "FAIL"
    );
    printf("ui-quiet %s: diagnostics stay hidden until requested\n", quiet_diagnostics ? "PASS" : "FAIL");
    return ok ? 0 : -1;
}

static void app_cleanup(app_loop *loop)
{
    free(loop->extract_before);
    app_scene_authoring_free(loop->authoring);
    engine_free(loop->runtime);
    game_live_world_free(loop->live);
    editor_free(loop->graph_editor);
    ecs_world_free(loop->world);
    engine_viewport_free(loop->viewport);
    cf_destroy_app();
    if (loop->content_root[0]) {
        char path[256];
        snprintf(path, sizeof path, "%s/prototypes/NewPrototype1.json", loop->content_root);
        unlink(path);
        snprintf(path, sizeof path, "%s/prototypes", loop->content_root);
        rmdir(path);
        rmdir(loop->content_root);
    }
}

static void scene_edit_input(app_loop *loop, int step)
{
    if (step == 1738 || step == 1739) {
        editor_ui_rect rect;
        if (editor_ui_item_rect("viewport.image", &rect)) {
            inject_mouse_button(rect.x + 12, rect.y + rect.height - 12, step == 1738);
        }
    }
    if (step == 1740) {
        ecs_component_id required = GAME_COMPONENT_GameWeapon.id;
        ecs_query query;
        bool found = ecs_query_begin(loop->world, &query, &required, 1) == ECS_OK;
        if (found) {
            found = ecs_query_next(&query, &loop->scene_edit_entity);
            ecs_query_end(&query);
        }
        editor_ui_rect rect;
        const GameTransform *position = found
                                          ? GAME_GameTransform_get_const(loop->world, loop->scene_edit_entity)
                                          : NULL;
        if (!position || !editor_ui_item_rect("viewport.image", &rect)) {
            printf("ui-scene-edit FAIL: player and viewport must be available\n");
            loop->integration_failed = 1;
            return;
        }
        engine_viewport_camera camera;
        engine_viewport_get_camera(loop->viewport, &camera);
        float border = ImGui_GetStyle()->ImageBorderSize;
        float width = rect.width - 2 * border;
        float height = rect.height - 2 * border;
        loop->scene_edit_before = *position;
        loop->scene_edit_after = (GameTransform){
            position->x + 40 * camera.span_width / width,
            position->y + 20 * camera.span_height / height,
        };
        loop->scene_edit_pointer = (ImVec2){
            rect.x + border +
                ((position->x - camera.center_x) / camera.span_width + 0.5f) * width,
            rect.y + border +
                (0.5f - (-position->y - camera.center_y) / camera.span_height) * height,
        };
    }
    if (!loop->scene_edit_entity.slot) {
        return;
    }
    ImVec2 point = loop->scene_edit_pointer;
    switch (step) {
        case 1742: inject_mouse_button(point.x, point.y, true); break;
        case 1743:
            ImGuiIO_AddMousePosEvent(ImGui_GetIO(), point.x + 40, point.y + 20);
            break;
        case 1744:
            inject_mouse_button(point.x + 40, point.y + 20, false);
            break;
        case 1784: inject_mouse_button(point.x + 40, point.y + 20, true); break;
        case 1785:
            ImGuiIO_AddMousePosEvent(ImGui_GetIO(), point.x + 60, point.y + 30);
            break;
        case 1786:
            ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Escape, true);
            break;
        case 1787:
            ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Escape, false);
            break;
        case 1788:
            inject_mouse_button(point.x + 60, point.y + 30, false);
            break;
    }
    const char *id = NULL;
    bool down = false;
    switch (step) {
        case 1752:
        case 1753:
        case 1768:
        case 1769:
        case 1800:
        case 1801:
        case 1816:
        case 1817:
            id = "document.menu";
            down = step == 1752 || step == 1768 || step == 1800 || step == 1816;
            break;
        case 1758:
        case 1759:
        case 1806:
        case 1807:
            id = "document.undo";
            down = step == 1758 || step == 1806;
            break;
        case 1774:
        case 1775:
        case 1822:
        case 1823:
            id = "document.redo";
            down = step == 1774 || step == 1822;
            break;
    }
    if (id) {
        float x, y;
        if (editor_ui_item_center(id, &x, &y)) {
            inject_mouse_button(x, y, down);
        } else {
            printf("ui-scene-edit FAIL: missing %s at %d\n", id, step);
            loop->integration_failed = 1;
        }
    }
}

static void scene_edit_check(app_loop *loop, int step)
{
    if (step == 1741) {
        bool ok = app_scene_authoring_runtime_state(loop->authoring)
                      .selected_entity.slot == 0;
        printf("ui-scene-edit %s: empty scene click clears selection\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step != 1748 && step != 1764 && step != 1780 && step != 1792 &&
        step != 1812 && step != 1828) {
        return;
    }
    bool restored = step == 1764 || step == 1812;
    GameTransform expected = restored ? loop->scene_edit_before : loop->scene_edit_after;
    const GameTransform *position = GAME_GameTransform_get_const(loop->world, loop->scene_edit_entity);
    app_runtime_state runtime = app_scene_authoring_runtime_state(loop->authoring);
    bool ok = position && fabsf(position->x - expected.x) < 0.01f &&
              fabsf(position->y - expected.y) < 0.01f &&
              runtime.target == APP_RUNTIME_SCENE && !runtime.playing &&
              ecs_entity_to_u64(runtime.selected_entity) ==
                  ecs_entity_to_u64(loop->scene_edit_entity);
    const char
        *action = step == 1748
                    ? "direct drag converts screen delta to world transform"
                : step == 1764 ? "one Undo restores drag origin"
                : step == 1780 ? "Redo restores committed drag"
                : step == 1792 ? "Escape restores the active drag"
                : step == 1812 ? "cancel adds no history entry"
                               : "Redo survives canceled drag";
    printf("ui-scene-edit %s: %s\n", ok ? "PASS" : "FAIL", action);
    if (!ok && position) {
        printf(
            "ui-scene-edit position=(%g,%g) expected=(%g,%g)\n",
            position->x,
            position->y,
            expected.x,
            expected.y
        );
    }
    loop->integration_failed |= !ok;
    if (step == 1748) {
        const char *capture = getenv("CBLUEPRINT_UI_SCENE_EDIT_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
}

static int encounter_entities(app_loop *loop, unsigned category, ecs_entity *first)
{
    ecs_component_id required = GAME_COMPONENT_GameCollider.id;
    ecs_query query;
    int count = 0;
    if (first) {
        *first = (ecs_entity){ 0 };
    }
    if (ecs_query_begin(loop->world, &query, &required, 1) != ECS_OK) {
        return 0;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (!(GAME_GameCollider_get_const(loop->world, entity)->category & category)) {
            continue;
        }
        if (first && !count) {
            *first = entity;
        }
        count++;
    }
    ecs_query_end(&query);
    return count;
}

static bool encounter_save_preserves_start(app_loop *loop)
{
    char path[] = "/tmp/c-blueprints-scene-XXXXXX";
    int descriptor = mkstemp(path);
    if (descriptor < 0) {
        return false;
    }
    close(descriptor);
    bool ok = app_scene_authoring_save(loop->authoring, path) == 0;
    FILE *file = fopen(path, "rb");
    char json[8192] = { 0 };
    if (file) {
        size_t size = fread(json, 1, sizeof json - 1, file);
        ok &= !ferror(file) && size < sizeof json - 1;
        fclose(file);
    } else {
        ok = false;
    }
    unlink(path);
    char error[256];
    game_scene_document *document = game_scene_document_load(json, error, sizeof error);
    ok &= document && acount(document->entities) == 2;
    if (document && acount(document->entities) == 2) {
        const game_scene_entity *player = &document->entities[1];
        ok &= strcmp(player->name, "Player_1") == 0 &&
              strcmp(player->prototype, "Player") == 0 &&
              strcmp(player->components_json, "{}") == 0 &&
              acount(player->behaviors) == 6;
        const char *expected[] = {
            "InputBehavior",         "WeaponBehavior",
            "HealthBehavior",        "AutoWeaponBehavior",
            "PlayerContactBehavior", "MovementTrailBehavior",
        };
        for (int i = 0; i < 6; i++) {
            int matches = 0;
            for (int j = 0; j < acount(player->behaviors); j++) {
                matches += strcmp(player->behaviors[j], expected[i]) == 0;
            }
            ok &= matches == 1;
        }
    }
    game_scene_document_free(document);
    if (!ok) {
        printf("ui-encounter saved scene mismatch: %s\n", json);
    }
    return ok;
}

static void inspector_history_input(app_loop *loop, int step)
{
    static const struct
    {
        int press;
        const char *id;
    } actions[] = {
        { 2278, "scene.actor.Player_1" },
        { 2282, "instance.component.GameHealth" },
        { 2286, "instance.field.GameHealth.current" },
        { 2296, "document.menu" },
        { 2300, "document.undo" },
        { 2306, "document.menu" },
        { 2310, "document.redo" },
        { 2316, "instance.revert_all" },
        { 2322, "document.menu" },
        { 2326, "document.undo" },
        { 2332, "instance.component.GameMaterial" },
        { 2336, "instance.color.GameMaterial.scene" },
        { 2340, "color.hex" },
        { 2350, "color.done" },
        { 2356, "document.menu" },
        { 2360, "document.undo" },
    };
    for (int i = 0; i < (int)(sizeof actions / sizeof *actions); i++) {
        if (step != actions[i].press && step != actions[i].press + 1) {
            continue;
        }
        float x, y;
        if (editor_ui_item_center(actions[i].id, &x, &y)) {
            inject_mouse_button(x, y, step == actions[i].press);
        } else {
            printf("ui-instance-history FAIL: missing %s at %d\n", actions[i].id, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 2288 || step == 2289 || step == 2342 || step == 2343) {
        bool down = step == 2288 || step == 2342;
        ImGuiIO_AddKeyEvent(
            ImGui_GetIO(),
            ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl,
            down
        );
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, down);
    }
    if (step == 2290 || step == 2291) {
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), step == 2290 ? "7" : "3");
    }
    if (step == 2292 || step == 2293) {
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, step == 2292);
    }
    if (step == 2344) {
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "4488CCFF");
    }
    if (step == 2314) {
        ecs_entity player;
        if (encounter_entities(loop, 1, &player) == 1) {
            GAME_GameTransform_get(loop->world, player)->x = 321;
        } else {
            loop->integration_failed = 1;
        }
    }
    if (step == 2294 || step == 2304 || step == 2314 || step == 2320 ||
        step == 2330 || step == 2354 || step == 2364) {
        ecs_entity player;
        bool ok = encounter_entities(loop, 1, &player) == 1;
        if (ok) {
            const GameHealth *health = GAME_GameHealth_get_const(loop->world, player);
            int expected = step == 2304 || step == 2320 ? 100 : 73;
            ok &= health->current == expected;
            if (step == 2320 || step == 2330) {
                ok &= GAME_GameTransform_get_const(loop->world, player)->x ==
                      (step == 2320 ? 320 : 321);
            }
            if (step == 2354 || step == 2364) {
                ok &= GAME_GameMaterial_get_const(loop->world, player)->scene ==
                      (step == 2354 ? 0x4488CCFFu : 0xFFFFFFFFu);
            }
        }
        printf("ui-instance-history %s: scalar, batch revert and color transaction at %d\n", ok ? "PASS" : "FAIL", step);
        loop->integration_failed |= !ok;
    }
}

static void lifecycle_input(app_loop *loop, int step)
{
    static const struct
    {
        int press;
        const char *id;
    } actions[] = {
        { 2378, "scene.actor.Player_1" },
        { 2384, "scene.delete" },
        { 2390, "document.menu" },
        { 2394, "document.undo" },
        { 2400, "document.menu" },
        { 2404, "document.undo" },
        { 2410, "document.menu" },
        { 2414, "document.redo" },
        { 2420, "document.menu" },
        { 2424, "document.redo" },
        { 2430, "document.menu" },
        { 2434, "document.undo" },
        { 2440, "scene.add" },
        { 2444, "scene.add.Enemy" },
        { 2450, "document.menu" },
        { 2454, "document.undo" },
        { 2460, "document.menu" },
        { 2464, "document.redo" },
    };
    if (step == 2376) {
        loop->integration_failed |= encounter_entities(loop, 1, &loop->lifecycle_entity) !=
                                    1;
    }
    if (step == 2380 || step == 2381) {
        float x, y;
        if (editor_ui_item_center("scene.actor.Player_1", &x, &y)) {
            ImGuiIO_AddMousePosEvent(ImGui_GetIO(), x, y);
            ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 1, step == 2380);
        } else {
            loop->integration_failed = 1;
        }
    }
    for (int i = 0; i < (int)(sizeof actions / sizeof *actions); i++) {
        if (step != actions[i].press && step != actions[i].press + 1) {
            continue;
        }
        float x, y;
        if (editor_ui_item_center(actions[i].id, &x, &y)) {
            inject_mouse_button(x, y, step == actions[i].press);
        } else {
            printf("ui-lifecycle FAIL: missing %s at %d\n", actions[i].id, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 2388 || step == 2428) {
        bool ok = encounter_entities(loop, 1, NULL) == 0;
        printf("ui-lifecycle %s: Delete/Redo removes authored player at %d\n", ok ? "PASS" : "FAIL", step);
        loop->integration_failed |= !ok;
    }
    if (step == 2398 || step == 2408 || step == 2418 || step == 2438) {
        ecs_entity player;
        bool ok = encounter_entities(loop, 1, &player) == 1;
        if (ok) {
            const GameHealth *health = GAME_GameHealth_get_const(loop->world, player);
            const GameTransform *position = GAME_GameTransform_get_const(loop->world, player);
            ok &= health->current == (step == 2408 ? 100 : 73) && position->x == 321;
            ok &= game_behavior_attachment_count_for_entity(loop->live->behaviors, player) ==
                  6;
            ok &= ecs_entity_to_u64(player) !=
                  ecs_entity_to_u64(loop->lifecycle_entity);
        }
        printf(
            "ui-lifecycle %s: restored actor retains state, behaviors and "
            "earlier property history at %d\n",
            ok ? "PASS" : "FAIL",
            step
        );
        loop->integration_failed |= !ok;
    }
    if (step == 2448 || step == 2458 || step == 2468) {
        ecs_entity enemy;
        int expected = step == 2458 ? 0 : 1;
        bool ok = encounter_entities(loop, 2, &enemy) == expected;
        if (ok && expected) {
            ok &= game_behavior_attachment_count_for_entity(loop->live->behaviors, enemy) ==
                  2;
        }
        printf("ui-lifecycle %s: Add/Undo/Redo changes authored enemy at %d\n", ok ? "PASS" : "FAIL", step);
        loop->integration_failed |= !ok;
    }
}

static void encounter_input(app_loop *loop, int step)
{
    int result = 0;
    if (step == 1930) {
        result = app_scene_authoring_seed_default(loop->authoring);
        engine_viewport_reset_camera(loop->viewport);
    }
    if (step == 1932 || step == 2034) {
        if (step == 1932) {
            loop->integration_failed |= !editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &loop->scene_before_play);
        }
        result = app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY);
    }
    if (step == 1940 || step == 2134) {
        editor_ui_rect pane;
        bool playing = step == 1940;
        bool ok = editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &pane) &&
                  editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS);
        ok = ok && fabsf(pane.height - loop->scene_before_play.height) < 3;
        if (playing) {
            editor_ui_rect stop = { 0 }, more = { 0 }, status = { 0 };
            ok &= editor_ui_item_rect("viewport.stop", &stop) &&
                  editor_ui_item_rect("viewport.step", &more) &&
                  editor_ui_item_rect("runtime.status", &status);
            ok &= stop.x + stop.width <= more.x;
        }
        printf(
            "ui-play-workspace %s: %s\n",
            ok ? "PASS" : "FAIL",
            playing ? "Play preserves Graph and Scene" : "Stop preserves the editing split"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 2024 || step == 2025 || step == 2126 || step == 2127) {
        bool ok = encounter_entities(loop, 2, &loop->inspected_spawn) == 1;
        char id[96];
        snprintf(
            id,
            sizeof id,
            "scene.spawned.%u.%u",
            loop->inspected_spawn.slot,
            loop->inspected_spawn.generation
        );
        float x, y;
        ok = ok && editor_ui_item_center(id, &x, &y);
        if (ok) {
            inject_mouse_button(x, y, step == 2024 || step == 2126);
        }
        loop->integration_failed |= !ok;
    }
    if (step == 2027 || step == 2129) {
        app_runtime_state state = app_scene_authoring_runtime_state(loop->authoring);
        editor_ui_rect inspector;
        bool ok = ecs_entity_to_u64(state.selected_entity) ==
                      ecs_entity_to_u64(loop->inspected_spawn) &&
                  state.component_count > 0 && state.behavior_count > 0 &&
                  editor_ui_item_rect("inspector.spawned", &inspector);
        printf("ui-runtime-outline %s: spawned enemy opens live Inspector\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 2028) {
        ecs_entity player;
        bool ok = encounter_entities(loop, 2, NULL) == 1 &&
                  encounter_entities(loop, 1, &player) == 1;
        printf("ui-encounter %s: shipped scene starts the authored wave\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        if (player.slot) {
            GAME_GameTransform_get(loop->world, player)->x = 999;
            GAME_GameHealth_get(loop->world, player)->current = 1;
            ok = encounter_save_preserves_start(loop) &&
                 app_scene_authoring_runtime_state(loop->authoring).playing &&
                 GAME_GameTransform_get_const(loop->world, player)->x == 999 &&
                 GAME_GameHealth_get_const(loop->world, player)->current == 1;
            printf("ui-encounter %s: Save preserves authored values without resetting Play\n", ok ? "PASS" : "FAIL");
            loop->integration_failed |= !ok;
            game_behavior_destroy_entity(loop->live->behaviors, player);
            ok = encounter_save_preserves_start(loop) &&
                 app_scene_authoring_runtime_state(loop->authoring).playing &&
                 encounter_entities(loop, 1, NULL) == 0 &&
                 encounter_entities(loop, 2, NULL) == 1;
            printf("ui-encounter %s: Save retains destroyed actor and its authored behaviors\n", ok ? "PASS" : "FAIL");
            loop->integration_failed |= !ok;
        }
    }
    if (step == 2030) {
        result = app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_RESTART);
    }
    if (step == 2032 || step == 2130) {
        result = app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_STOP);
    }
    if (step == 2031 || step == 2134) {
        ecs_entity player;
        bool ok = encounter_entities(loop, 1, &player) == 1 &&
                  encounter_entities(loop, 2, NULL) == 0;
        const GameTransform *position = player.slot
                                          ? GAME_GameTransform_get_const(loop->world, player)
                                          : NULL;
        const GameHealth *health = player.slot
                                     ? GAME_GameHealth_get_const(loop->world, player)
                                     : NULL;
        ok &= position && fabsf(position->x - 320) < 0.001f &&
              fabsf(position->y + 180) < 0.001f;
        ok &= health && health->current == health->maximum;
        printf(
            "ui-encounter %s: %s restores player and clears runtime enemies\n",
            ok ? "PASS" : "FAIL",
            step == 2031 ? "Restart after player destruction" : "Stop"
        );
        loop->integration_failed |= !ok;
        if (step == 2031) {
            ok = app_scene_authoring_runtime_state(loop->authoring)
                     .selected_entity.slot == 0;
            printf("ui-runtime-outline %s: Restart clears the destroyed runtime selection\n", ok ? "PASS" : "FAIL");
            loop->integration_failed |= !ok;
        }
    }
    if (step == 2128) {
        bool ok = encounter_entities(loop, 2, NULL) == 1;
        printf("ui-encounter %s: replay starts a fresh wave cadence\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 2136) {
        result = app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY);
        result |= app_scene_authoring_set_prototype_field(
            loop->authoring,
            "Player",
            "GameTransform",
            "x",
            (pin_value){ .type = PIN_F32, .f = 321 }
        );
        ecs_entity player;
        bool ok = result == 0 &&
                  !app_scene_authoring_runtime_state(loop->authoring).playing &&
                  encounter_entities(loop, 1, &player) == 1;
        ok = ok && GAME_GameTransform_get_const(loop->world, player)->x == 321 &&
             encounter_save_preserves_start(loop);
        if (!ok) {
            printf(
                "ui-encounter prototype state: result=%d playing=%d "
                "players=%d\n",
                result,
                app_scene_authoring_runtime_state(loop->authoring).playing,
                encounter_entities(loop, 1, &player)
            );
            if (player.slot) {
                printf(
                    "ui-encounter prototype position: %f\n",
                    GAME_GameTransform_get_const(loop->world, player)->x
                );
            }
        }
        printf("ui-encounter %s: prototype edit ends simulation and preserves inherited defaults on Save\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        result |= app_scene_authoring_set_prototype_field(
            loop->authoring,
            "Player",
            "GameTransform",
            "x",
            (pin_value){ .type = PIN_F32, .f = 320 }
        );
    }
    if (step == 2137) {
        result = app_scene_authoring_preview_prototype(loop->authoring, "Player");
        result |= app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY);
        bool ok = editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS);
        result |= app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_PREVIEW, APP_RUNTIME_PAUSE);
        ok &= editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS);
        printf("ui-play-workspace %s: returning to prototype preview restores Graph\n", ok && !result ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 2138) {
        result = app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY);
        result |= app_scene_authoring_open_system(loop->authoring, "content/systems/MovementSystem.json");
        bool ok = editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS);
        printf("ui-play-workspace %s: opening a System reveals Graph during Play\n", ok && !result ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        result |= app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_STOP);
        app_scene_authoring_stop_prototype_preview(loop->authoring);
    }
    if (step == 2150) {
        result = app_scene_authoring_seed_default(loop->authoring);
        engine_viewport_reset_camera(loop->viewport);
    }
    if (step == 2152) {
        result = app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY);
    }
    if (step == 2246) {
        result = app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_PAUSE);
        bool ok = encounter_entities(loop, 2, &loop->inspected_spawn) == 1;
        editor_ui_rect rect;
        ok &= editor_ui_item_rect("viewport.image", &rect);
        if (ok) {
            engine_viewport_camera camera;
            engine_viewport_get_camera(loop->viewport, &camera);
            loop->inspected_spawn_position = *GAME_GameTransform_get_const(loop->world, loop->inspected_spawn);
            GameTransform p = loop->inspected_spawn_position;
            float border = ImGui_GetStyle()->ImageBorderSize;
            loop->inspected_spawn_pointer = (ImVec2){
                rect.x + border +
                    ((p.x - camera.center_x) / camera.span_width + 0.5f) *
                        (rect.width - 2 * border),
                rect.y + border +
                    (0.5f - (-p.y - camera.center_y) / camera.span_height) *
                        (rect.height - 2 * border),
            };
        }
        loop->integration_failed |= !ok;
    }
    if (step == 2250) {
        inject_mouse_button(
            loop->inspected_spawn_pointer.x,
            loop->inspected_spawn_pointer.y,
            true
        );
    }
    if (step == 2252 || step == 2254) {
        inject_mouse_button(
            loop->inspected_spawn_pointer.x + 40,
            loop->inspected_spawn_pointer.y + 20,
            step == 2252
        );
    }
    if (step == 2258) {
        app_runtime_state state = app_scene_authoring_runtime_state(loop->authoring);
        const GameTransform *p = GAME_GameTransform_get_const(loop->world, loop->inspected_spawn);
        bool ok = !state.playing &&
                  ecs_entity_to_u64(state.selected_entity) ==
                      ecs_entity_to_u64(loop->inspected_spawn) &&
                  p && p->x == loop->inspected_spawn_position.x &&
                  p->y == loop->inspected_spawn_position.y;
        printf("ui-runtime-picking %s: paused viewport selects spawned enemy without dragging it\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 2260 || step == 2261) {
        editor_ui_rect rect;
        if (editor_ui_item_rect("viewport.image", &rect)) {
            inject_mouse_button(rect.x + 12, rect.y + rect.height - 12, step == 2260);
        }
    }
    if (step == 2263) {
        bool ok = app_scene_authoring_runtime_state(loop->authoring)
                      .selected_entity.slot == 0;
        printf("ui-runtime-picking %s: empty viewport clears runtime selection\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 2264) {
        result = app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_STOP);
    }
    loop->integration_failed |= result != 0;
}

static void scene_numeric_input(app_loop *loop, int step)
{
    if (step == 1892) {
        GameOrientation *orientation = GAME_GameOrientation_get(loop->world, loop->scene_edit_entity);
        if (orientation) {
            orientation->radians = 1.57079632679f;
        } else {
            loop->integration_failed = 1;
        }
    }
    if (step >= 1896 && step <= 1899) {
        float x, y;
        if (editor_ui_item_center("scene.actor.Player_1", &x, &y)) {
            inject_mouse_button(x, y, step == 1896 || step == 1898);
        } else {
            loop->integration_failed = 1;
        }
    }
    const char *id = NULL;
    bool down = false;
    if (step == 1832) {
        float x, y;
        loop->scene_numeric_opening = !editor_ui_item_center("instance.transform.x", &x, &y);
    }
    if ((step == 1832 || step == 1833) && loop->scene_numeric_opening) {
        id = "instance.component.GameTransform";
        down = step == 1832;
    }
    if (step == 1840 || step == 1841) {
        editor_ui_rect rect;
        if (editor_ui_item_rect("instance.transform.x", &rect)) {
            inject_mouse_button(rect.x + rect.width * 0.25f, rect.y + rect.height * 0.5f, step == 1840);
        } else {
            printf("ui-scene-numeric FAIL: position input is unavailable\n");
            loop->integration_failed = 1;
        }
    }
    if (step == 1844 || step == 1845) {
        bool pressed = step == 1844;
        ImGuiIO_AddKeyEvent(
            ImGui_GetIO(),
            ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl,
            pressed
        );
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, pressed);
    }
    if (step >= 1846 && step <= 1850) {
        const char *characters[] = { "1", "2", "3", ".", "5" };
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), characters[step - 1846]);
    }
    switch (step) {
        case 1852:
        case 1853:
        case 1868:
        case 1869:
            id = "document.menu";
            down = step == 1852 || step == 1868;
            break;
        case 1858:
        case 1859:
            id = "document.undo";
            down = step == 1858;
            break;
        case 1874:
        case 1875:
            id = "document.redo";
            down = step == 1874;
            break;
    }
    if (id) {
        float x, y;
        if (editor_ui_item_center(id, &x, &y)) {
            inject_mouse_button(x, y, down);
        } else {
            printf("ui-scene-numeric FAIL: missing %s at %d\n", id, step);
            loop->integration_failed = 1;
        }
    }
}

static void scene_numeric_check(app_loop *loop, int step)
{
    if (step == 2128 || step == 2258) {
        const char *capture = getenv("CBLUEPRINT_UI_ENCOUNTER_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }

    if (step == 1920) {
        editor_ui_rect inspector, input, revert;
        bool fits = editor_ui_pane_rect(EDITOR_UI_PANE_INSPECTOR, &inspector) &&
                    editor_ui_item_rect("instance.transform.x", &input) &&
                    editor_ui_item_rect("instance.revert.transform.x", &revert) &&
                    input.x >= inspector.x && input.width > 60 &&
                    input.x + input.width <= revert.x &&
                    revert.x + revert.width <= inspector.x + inspector.width;
        printf("ui-property-layout %s: numeric input and Revert fit the Inspector\n", fits ? "PASS" : "FAIL");
        loop->integration_failed |= !fits;
        engine_viewport_camera camera;
        engine_viewport_get_camera(loop->viewport, &camera);
        const GameTransform *position = GAME_GameTransform_get_const(loop->world, loop->scene_edit_entity);
        bool ok = position && fabsf(camera.center_x - position->x) < 0.01f &&
                  fabsf(camera.center_y - (-position->y + 6)) < 0.01f;
        printf("ui-orientation %s: actor framing follows rotated asymmetric geometry\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        const char *capture = getenv("CBLUEPRINT_UI_ORIENTATION_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step != 1846 && step != 1847 && step != 1848 && step != 1851 &&
        step != 1864 && step != 1880) {
        return;
    }
    float expected = step == 1846 ? 1
                   : step == 1847 ? 12
                   : step == 1848 ? 123
                   : step == 1864 ? loop->scene_edit_after.x
                                  : 123.5f;
    const GameTransform *position = GAME_GameTransform_get_const(loop->world, loop->scene_edit_entity);
    bool ok = position && fabsf(position->x - expected) < 0.01f &&
              fabsf(position->y - loop->scene_edit_after.y) < 0.01f;
    printf(
        "ui-scene-numeric %s: %s at %d\n",
        ok ? "PASS" : "FAIL",
        step == 1864   ? "one Undo restores the complete typing transaction"
        : step == 1880 ? "Redo restores the numeric edit"
                       : "typing retains input identity",
        step
    );
    if (!ok && position) {
        printf("ui-scene-numeric x=%g expected=%g\n", position->x, expected);
    }
    loop->integration_failed |= !ok;
}

static bool register_deferred_begin_fixture(app_loop *loop)
{
    behavior_asset *asset = behavior_asset_new("DeferredBeginCounter");
    behavior_asset_set_owner_type(asset, "ecs.entity");
    graph *g = asset->graph;
    node *event = graph_add_node(g, registry_node_by_name("Event"));
    node *get = graph_add_node(g, registry_node_by_name("Get_GameHealth"));
    node *add = graph_add_node(g, registry_node_by_name("Add"));
    node *make = graph_add_node(g, registry_node_by_name("Make_GameHealth"));
    node *set = graph_add_node(g, registry_node_by_name("Set_GameHealth"));
    bool ok = event && get && add && make && set &&
              graph_set_event_name(g, event, "Begin") == 0 &&
              graph_expose_output_fields(g, get, 0) >= 0 &&
              graph_connect_data(g, get, 1, add, 0) == 0 &&
              graph_set_pin_type(g, add, 0, 1, PIN_I32, "int") == 0 &&
              graph_set_pin_default(g, add, 0, 1, &(pin_value){ .type = PIN_I32, .i = 1 }) ==
                  0 &&
              graph_connect_data(g, add, 0, make, 0) == 0 &&
              graph_connect_data(g, get, 2, make, 1) == 0 &&
              graph_connect_data(g, make, 0, set, 1) == 0 &&
              graph_connect_exec(g, event, 0, set, 0, 0) == 0;
    if (!ok) {
        printf("ui-deferred-begin FAIL: graph construction\n");
        behavior_asset_free(asset);
        return false;
    }
    game_live_definition *definition = game_live_definition_new("DeferredBeginCounter", asset);
    if (!definition || game_live_add_definition(loop->live, definition) != 0) {
        printf("ui-deferred-begin FAIL: definition registration\n");
        if (definition) {
            game_live_definition_free(definition);
        } else {
            behavior_asset_free(asset);
        }
        return false;
    }
    int result = app_scene_authoring_register_prototype(
        loop->authoring,
        "{\"name\":\"BeginFixture\",\"components\":{"
        "\"GameTransform\":{\"x\":0,\"y\":0},"
        "\"GameHealth\":{\"current\":50,\"maximum\":100},"
        "\"GameCollider\":{\"radius\":1,\"category\":16,\"mask\":0}},"
        "\"behaviors\":[\"DeferredBeginCounter\"]}",
        NULL
    );
    if (result != 0) {
        printf("ui-deferred-begin FAIL: prototype registration (%d)\n", result);
        return false;
    }
    result = app_scene_authoring_add_instance(loop->authoring, "BeginFixture");
    if (result != 0) {
        printf(
            "ui-deferred-begin FAIL: instance creation (%d): %s\n",
            result,
            app_scene_authoring_message(loop->authoring)
        );
    }
    return result == 0;
}

static void deferred_begin_input(app_loop *loop, int step)
{
    if (step == 2570) {
        bool ok = register_deferred_begin_fixture(loop);
        printf("ui-deferred-begin %s: fixture authored\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 2576 || step == 2578) {
        loop->integration_failed |= app_scene_authoring_set_instance_behavior(
                                        loop->authoring,
                                        "DeferredBeginCounter",
                                        step == 2578
                                    ) != 0;
    }
    static const struct
    {
        int press;
        const char *id;
    } actions[] = {
        { 2584, "document.menu" },
        { 2588, "document.undo" },
        { 2594, "document.menu" },
        { 2598, "document.redo" },
    };
    for (int i = 0; i < (int)(sizeof actions / sizeof *actions); i++) {
        if (step != actions[i].press && step != actions[i].press + 1) {
            continue;
        }
        float x, y;
        if (editor_ui_item_center(actions[i].id, &x, &y)) {
            inject_mouse_button(x, y, step == actions[i].press);
        } else {
            printf("ui-deferred-begin FAIL: missing %s at %d\n", actions[i].id, step);
            loop->integration_failed = 1;
        }
    }
    static const struct
    {
        int step;
        app_runtime_command command;
    } commands[] = {
        { 2604, APP_RUNTIME_PLAY }, { 2610, APP_RUNTIME_PAUSE },
        { 2614, APP_RUNTIME_PLAY }, { 2620, APP_RUNTIME_STOP },
        { 2626, APP_RUNTIME_PLAY }, { 2632, APP_RUNTIME_RESTART },
        { 2638, APP_RUNTIME_STOP }, { 2642, APP_RUNTIME_STEP },
        { 2648, APP_RUNTIME_STOP },
    };
    for (int i = 0; i < (int)(sizeof commands / sizeof *commands); i++) {
        if (step == commands[i].step) {
            loop->integration_failed |= app_scene_authoring_runtime_command(
                                            loop->authoring,
                                            APP_RUNTIME_SCENE,
                                            commands[i].command
                                        ) != 0;
        }
    }
    bool check = step == 2574 || step == 2582 || step == 2592 || step == 2602 ||
                 step == 2608 || step == 2618 || step == 2624 || step == 2630 ||
                 step == 2636 || step == 2646 || step == 2652;
    if (!check) {
        return;
    }
    int expected = step == 2608 || step == 2618 || step == 2630 ||
                           step == 2636 || step == 2646
                     ? 51
                     : 50;
    ecs_entity entity;
    bool ok = encounter_entities(loop, 16, &entity) == 1;
    if (ok) {
        const GameHealth *health = GAME_GameHealth_get_const(loop->world, entity);
        ok = health && health->current == expected && health->maximum == 100;
        ok &= game_behavior_attachment_count_for_entity(loop->live->behaviors, entity) ==
              (step == 2592 ? 0 : 1);
        if (!ok) {
            printf(
                "ui-deferred-begin detail: health=%d expected=%d at %d\n",
                health ? health->current : -1,
                expected,
                step
            );
        }
    }
    printf(
        "ui-deferred-begin %s: authoring, Undo and transport lifecycle at %d\n",
        ok ? "PASS" : "FAIL",
        step
    );
    loop->integration_failed |= !ok;
}

static bool io_write(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    bool ok = fwrite(text, 1, strlen(text), file) == strlen(text);
    return fclose(file) == 0 && ok;
}

static char *io_read(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = size >= 0 ? malloc((size_t)size + 1) : NULL;
    if (text) {
        size_t read = fread(text, 1, (size_t)size, file);
        text[read] = 0;
    }
    fclose(file);
    return text;
}

static bool io_fixture(app_loop *loop)
{
    strcpy(loop->io_root, "/tmp/c-blueprints-io-XXXXXX");
    if (!mkdtemp(loop->io_root)) {
        return false;
    }
    app_scene_authoring_free(loop->authoring);
    engine_free(loop->runtime);
    game_live_world_free(loop->live);
    editor_free(loop->graph_editor);
    ecs_world_free(loop->world);
    loop->world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        if (ecs_world_register_component(loop->world, GAME_REGISTRY_COMPONENTS[i], NULL) !=
            ECS_OK) {
            return false;
        }
    }
    behavior_asset *asset = behavior_asset_new("IOFixture");
    loop->graph_editor = editor_new(asset);
    loop->live = game_live_world_new(loop->world);
    game_live_definition *definition = game_live_definition_new("IOFixture", behavior_asset_new("IOFixture"));
    if (game_live_add_definition(loop->live, definition) != 0) {
        return false;
    }
    loop->runtime = engine_new(loop->world, loop->live);
    loop->authoring = app_scene_authoring_new(
        loop->world,
        loop->live,
        definition,
        asset,
        loop->graph_editor,
        NULL,
        NULL,
        loop->runtime
    );
    if (!loop->authoring) {
        return false;
    }
    engine_viewport_set_render_callback(loop->viewport, app_scene_authoring_render, loop->authoring);
    if (app_scene_authoring_set_content_root(loop->authoring, loop->io_root) != 0) {
        return false;
    }
    const char *player = "{\"version\":1,\"name\":\"Player\",\"components\":{"
                         "\"GameTransform\":{\"x\":0,\"y\":0},\"GameHealth\":{"
                         "\"current\":100,\"maximum\":100},\"GameCollider\":{"
                         "\"category\":1}},\"behaviors\":[]}";
    const char *enemy = "{\"version\":1,\"name\":\"Enemy\",\"components\":{"
                        "\"GameHealth\":{\"current\":50,\"maximum\":50},"
                        "\"GameCollider\":{\"category\":2}},\"behaviors\":[]}";
    char player_path[128], enemy_path[128];
    snprintf(player_path, sizeof player_path, "%s/Player.json", loop->io_root);
    snprintf(enemy_path, sizeof enemy_path, "%s/Enemy.json", loop->io_root);
    return app_scene_authoring_register_prototype(loop->authoring, player, NULL) == 0 &&
           app_scene_authoring_register_prototype(loop->authoring, enemy, NULL) == 0 &&
           app_scene_authoring_save_prototype(loop->authoring, "Player", player_path) ==
               0 &&
           app_scene_authoring_save_prototype(loop->authoring, "Enemy", enemy_path) == 0 &&
           app_scene_authoring_add_instance(loop->authoring, "Player") == 0;
}

static void document_io_input(app_loop *loop, int step)
{
    if (step < 2756) {
        return;
    }
    char scene_path[128], invalid_path[128], system_path[128];
    bool ok = true;
    if (step == 2756) {
        ok = io_fixture(loop);
    }
    snprintf(scene_path, sizeof scene_path, "%s/scene.json", loop->io_root);
    snprintf(invalid_path, sizeof invalid_path, "%s/invalid.json", loop->io_root);
    snprintf(system_path, sizeof system_path, "%s/system.json", loop->io_root);
    if (step == 2756) {
        ok = ok && app_scene_authoring_save(loop->authoring, scene_path) == 0 &&
             app_scene_authoring_load(loop->authoring, scene_path) == 0;
        encounter_entities(loop, 1, &loop->io_player);
        GameHealth *health = GAME_GameHealth_get(loop->world, loop->io_player);
        ok = ok && health;
        if (health) {
            health->current = 67;
        }
    }
    static const struct
    {
        int press;
        const char *id;
    } actions[] = {
        { 2760, "document.menu" }, { 2764, "document.save" },
        { 2770, "document.menu" }, { 2774, "document.reload" },
        { 2780, "document.menu" }, { 2784, "document.save" },
        { 2790, "document.menu" }, { 2794, "document.reload" },
        { 2804, "document.menu" }, { 2808, "document.undo" },
        { 2814, "document.menu" }, { 2818, "document.save" },
        { 2824, "document.menu" }, { 2828, "document.reload" },
        { 2838, "document.menu" }, { 2842, "document.save_all" },
        { 2850, "document.menu" }, { 2854, "document.reload" },
        { 2860, "document.menu" }, { 2864, "document.reload" },
    };
    for (int i = 0; i < (int)(sizeof actions / sizeof *actions); i++) {
        if (step != actions[i].press && step != actions[i].press + 1) {
            continue;
        }
        float x, y;
        if (editor_ui_item_center(actions[i].id, &x, &y)) {
            inject_mouse_button(x, y, step == actions[i].press);
        } else {
            printf("ui-document-io missing %s at %d\n", actions[i].id, step);
            ok = false;
        }
    }
    if (step == 2768 || step == 2788 || step == 2822) {
        encounter_entities(loop, 1, &loop->io_player);
        GameHealth *health = GAME_GameHealth_get(loop->world, loop->io_player);
        ok = health != NULL;
        if (health) {
            health->current = 3;
        }
    }
    if (step == 2778 || step == 2798 || step == 2832) {
        encounter_entities(loop, 1, &loop->io_player);
        const GameHealth *health = GAME_GameHealth_get_const(loop->world, loop->io_player);
        ok = health && health->current == (step == 2832 ? 68 : 67);
    }
    if (step == 2800) {
        ok = app_scene_authoring_add_instance(loop->authoring, "Enemy") == 0;
        loop->io_enemy = app_scene_authoring_runtime_state(loop->authoring).selected_entity;
        ok = ok &&
             io_write(invalid_path, "{\"version\":1,\"name\":\"Invalid\",\"entities\":[{\"name\":\"Player_1\",\"prototype\":\"Player\",\"components\":{\"GameHealth\":{\"current\":[]}},\"behaviors\":[]}]}") &&
             app_scene_authoring_load(loop->authoring, invalid_path) != 0 &&
             GAME_GameHealth_get_const(loop->world, loop->io_player) &&
             GAME_GameHealth_get_const(loop->world, loop->io_enemy) &&
             app_scene_authoring_runtime_state(loop->authoring).selected_entity.slot ==
                 loop->io_enemy.slot &&
             app_scene_authoring_runtime_state(loop->authoring).selected_entity.generation ==
                 loop->io_enemy.generation;
    }
    if (step == 2812) {
        ok = encounter_entities(loop, 2, NULL) == 0;
        GameHealth *health = GAME_GameHealth_get(loop->world, loop->io_player);
        ok = ok && health;
        if (health) {
            health->current = 68;
        }
    }
    if (step == 2834) {
        char *json = io_read("content/systems/MovementSystem.json");
        ok = json && io_write(system_path, json) &&
             app_scene_authoring_open_system(loop->authoring, system_path) == 0;
        free(json);
        if (ok) {
            behavior_asset_set_description(
                (behavior_asset *)editor_behavior_asset(loop->graph_editor),
                "Saved System working graph"
            );
        }
    }
    if (step == 2846) {
        char *json = io_read(system_path);
        behavior_asset *saved = json ? behavior_asset_load_json(json) : NULL;
        ok = saved && saved->description &&
             strcmp(saved->description, "Saved System working graph") == 0 &&
             strcmp(app_scene_authoring_message(loop->authoring), "Saved all dirty documents.") ==
                 0;
        behavior_asset_free(saved);
        free(json);
        behavior_asset_set_description(
            (behavior_asset *)editor_behavior_asset(loop->graph_editor),
            "Unsaved replacement"
        );
    }
    if (step == 2858 || step == 2868) {
        const behavior_asset *asset = editor_behavior_asset(loop->graph_editor);
        ok = asset && asset->description &&
             strcmp(asset->description, "Saved System working graph") == 0;
    }
    if (!ok || step == 2756 || step == 2778 || step == 2798 || step == 2800 ||
        step == 2812 || step == 2832 || step == 2846 || step == 2858 || step == 2868) {
        printf(
            "ui-document-io %s: save/load preservation at %d (%s)\n",
            ok ? "PASS" : "FAIL",
            step,
            app_scene_authoring_message(loop->authoring)
        );
    }
    loop->integration_failed |= !ok;
    if (step == 2868) {
        unlink(scene_path);
        unlink(invalid_path);
        unlink(system_path);
        char prototype_path[128];
        snprintf(prototype_path, sizeof prototype_path, "%s/Player.json", loop->io_root);
        unlink(prototype_path);
        snprintf(prototype_path, sizeof prototype_path, "%s/Enemy.json", loop->io_root);
        unlink(prototype_path);
        rmdir(loop->io_root);
    }
}

static void live_behavior_open_input(app_loop *loop, int step)
{
    if (step == 2724 || step == 2730) {
        ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector");
        if (inspector) {
            ImGui_SetScrollYImGuiWindowPtr(inspector, inspector->ScrollMax.y);
        }
    }
    if (step == 2728 || step == 2746) {
        loop->integration_failed |= app_scene_authoring_runtime_command(
                                        loop->authoring,
                                        APP_RUNTIME_SCENE,
                                        step == 2728 ? APP_RUNTIME_PLAY : APP_RUNTIME_STOP
                                    ) != 0;
    }
    static const struct
    {
        int press;
        const char *id;
    } actions[] = {
        { 2720, "scene.actor.Player_1" },
        { 2732, "instance.behavior.remove.InputBehavior" },
        { 2738, "instance.behavior.open.InputBehavior" },
    };
    for (int i = 0; i < (int)(sizeof actions / sizeof *actions); i++) {
        if (step != actions[i].press && step != actions[i].press + 1) {
            continue;
        }
        float x, y;
        if (editor_ui_item_center(actions[i].id, &x, &y)) {
            inject_mouse_button(x, y, step == actions[i].press);
        } else {
            printf("ui-live-behavior-open FAIL: missing %s at %d\n", actions[i].id, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 2730) {
        ecs_entity entity = app_scene_authoring_runtime_state(loop->authoring).selected_entity;
        loop->live_attachment_count = game_behavior_attachment_count_for_entity(
            loop->live->behaviors,
            entity
        );
    }
    if (step == 2736) {
        float x, y;
        ecs_entity entity = app_scene_authoring_runtime_state(loop->authoring).selected_entity;
        bool ok = editor_ui_item_center("instance.behavior.remove.InputBehavior", &x, &y) &&
                  game_behavior_attachment_count_for_entity(loop->live->behaviors, entity) ==
                      loop->live_attachment_count &&
                  loop->live_attachment_count > 0 &&
                  app_scene_authoring_runtime_state(loop->authoring).playing;
        printf("ui-live-behavior-open %s: attachment mutations remain disabled during Play\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 2742) {
        app_editor_document_context document = app_scene_authoring_document(loop->authoring);
        app_runtime_state runtime = app_scene_authoring_runtime_state(loop->authoring);
        const behavior_asset *asset = editor_behavior_asset(loop->graph_editor);
        bool ok = document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR &&
                  document.id && strcmp(document.id, "InputBehavior") == 0 &&
                  asset && strcmp(asset->name, "InputBehavior") == 0 &&
                  runtime.target == APP_RUNTIME_SCENE && runtime.playing;
        printf("ui-live-behavior-open %s: authored behavior opens without stopping Scene Play\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 2748) {
        loop->integration_failed |= app_scene_authoring_runtime_state(loop->authoring)
                                        .playing;
    }
}

static void material_disclosure_input(app_loop *loop, int step)
{
    ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector");
    if ((step == 2694 || step == 2705) && inspector) {
        ImGui_SetScrollYImGuiWindowPtr(inspector, inspector->ScrollMax.y);
    }
    if (step == 2710 && inspector) {
        ImGui_SetScrollYImGuiWindowPtr(inspector, loop->material_scroll);
    }
    const char *id = NULL;
    bool down = false;
    float x, y;
    if (step == 2696) {
        loop->material_opening = !editor_ui_item_center("instance.field.GameMaterial.scene", &x, &y);
    }
    if ((step == 2696 || step == 2697) && loop->material_opening) {
        id = "instance.component.GameMaterial";
        down = step == 2696;
    }
    if (step == 2702 || step == 2703 || step == 2712 || step == 2713) {
        id = "instance.material.advanced";
        down = step == 2702 || step == 2712;
    }
    if (id) {
        if (editor_ui_item_center(id, &x, &y)) {
            inject_mouse_button(x, y, down);
        } else {
            printf("ui-material-disclosure FAIL: missing %s at %d\n", id, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 2700 && inspector) {
        loop->material_scroll = inspector->Scroll.y;
    }
    if (step != 2700 && step != 2708 && step != 2716) {
        return;
    }
    bool advanced = step == 2708;
    bool albedo = editor_ui_item_center("instance.field.GameMaterial.albedo", &x, &y);
    bool absorption = editor_ui_item_center("instance.field.GameMaterial.absorption", &x, &y);
    bool ok = albedo == advanced && absorption == advanced;
    if (!advanced) {
        ok &= editor_ui_item_center("instance.field.GameMaterial.scene", &x, &y) &&
              editor_ui_item_center("instance.field.GameMaterial.emission_intensity", &x, &y);
    }
    printf(
        "ui-material-disclosure %s: primary fields remain accessible, advanced "
        "lighting %s at %d\n",
        ok ? "PASS" : "FAIL",
        advanced ? "shown" : "collapsed",
        step
    );
    loop->integration_failed |= !ok;
}

static void scene_search_input(app_loop *loop, int step)
{
    if (step == 2660) {
        editor_ui_request_focus(EDITOR_UI_FOCUS_WORLD_OUTLINER);
    }
    static const struct
    {
        int press;
        const char *id;
    } actions[] = {
        { 2664, "scene.actor.Player_1" },
        { 2668, "scene.search" },
        { 2684, "scene.clear_search" },
    };
    for (int i = 0; i < (int)(sizeof actions / sizeof *actions); i++) {
        if (step != actions[i].press && step != actions[i].press + 1) {
            continue;
        }
        float x, y;
        if (editor_ui_item_center(actions[i].id, &x, &y)) {
            inject_mouse_button(x, y, step == actions[i].press);
        } else {
            printf("ui-scene-search FAIL: missing %s at %d\n", actions[i].id, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 2666) {
        loop->search_selection = app_scene_authoring_runtime_state(loop->authoring)
                                     .selected_entity;
        loop->integration_failed |= !loop->search_selection.slot;
    }
    if (step == 2670 || step == 2678) {
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), step == 2670 ? "eNeMy" : "no-such-actor");
    }
    if (step == 2676 || step == 2677) {
        ImGuiIO_AddKeyEvent(
            ImGui_GetIO(),
            ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl,
            step == 2676
        );
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, step == 2676);
    }
    if (step != 2674 && step != 2682 && step != 2688) {
        return;
    }
    float x, y;
    bool enemy = editor_ui_item_center("scene.actor.Enemy_1", &x, &y);
    bool player = editor_ui_item_center("scene.actor.Player_1", &x, &y);
    bool empty = editor_ui_item_center("scene.search.empty", &x, &y);
    bool ok = enemy == (step != 2682) && player == (step == 2688) &&
              empty == (step == 2682) &&
              ecs_entity_to_u64(
                  app_scene_authoring_runtime_state(loop->authoring).selected_entity
              ) == ecs_entity_to_u64(loop->search_selection);
    printf("ui-scene-search %s: mixed-case filter, empty state and clear preserve selection at %d\n", ok ? "PASS" : "FAIL", step);
    loop->integration_failed |= !ok;
}

static void instance_behavior_input(app_loop *loop, int step)
{
    if (step == 2496) {
        editor_ui_reset_layout();
        editor_ui_set_pane_visible(EDITOR_UI_PANE_WORLD_OUTLINER, true);
        editor_ui_request_focus(EDITOR_UI_FOCUS_WORLD_OUTLINER);
    }
    if (step == 2504 || step == 2534) {
        ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector");
        if (inspector) {
            ImGui_SetScrollYImGuiWindowPtr(inspector, inspector->ScrollMax.y);
        }
    }
    static const struct
    {
        int press;
        const char *id;
    } actions[] = {
        { 2500, "scene.actor.Enemy_1" },
        { 2510, "instance.behavior.remove.EnemyBehavior" },
        { 2514, "document.menu" },
        { 2518, "document.undo" },
        { 2524, "document.menu" },
        { 2528, "document.redo" },
        { 2536, "instance.behavior.add" },
        { 2540, "instance.behavior.attach.EnemyBehavior" },
        { 2546, "document.menu" },
        { 2550, "document.undo" },
        { 2556, "document.menu" },
        { 2560, "document.redo" },
    };
    for (int i = 0; i < (int)(sizeof actions / sizeof *actions); i++) {
        if (step != actions[i].press && step != actions[i].press + 1) {
            continue;
        }
        float x, y;
        if (editor_ui_item_center(actions[i].id, &x, &y)) {
            inject_mouse_button(x, y, step == actions[i].press);
        } else {
            printf("ui-instance-behavior FAIL: missing %s at %d\n", actions[i].id, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 2505 || step == 2564) {
        editor_ui_rect name, more;
        bool ok = editor_ui_item_rect("instance.behavior.open.EnemyBehavior", &name) &&
                  editor_ui_item_rect("instance.behavior.remove.EnemyBehavior", &more);
        ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector");
        if (ok && inspector) {
            ok = name.x + name.width <= more.x + 1 &&
                 name.y < more.y + more.height && more.y < name.y + name.height &&
                 more.x + more.width <= inspector->Pos.x + inspector->Size.x;
        } else {
            ok = false;
        }
        printf("ui-instance-behavior %s: name and Remove share a non-overlapping row at %d\n", ok ? "PASS" : "FAIL", step);
        loop->integration_failed |= !ok;
    }
    if (step != 2512 && step != 2522 && step != 2532 && step != 2544 &&
        step != 2554 && step != 2564) {
        return;
    }
    bool attached = step == 2522 || step == 2544 || step == 2564;
    ecs_entity enemy;
    bool ok = encounter_entities(loop, 2, &enemy) == 1;
    if (ok) {
        int count = game_behavior_attachment_count_for_entity(loop->live->behaviors, enemy);
        bool health = false, pursuit = false;
        for (int i = 0; i < count; i++) {
            game_behavior_attachment_info info;
            if (game_behavior_attachment_info_at(loop->live->behaviors, enemy, i, &info) !=
                0) {
                ok = false;
                break;
            }
            health |= strcmp(info.asset_name, "HealthBehavior") == 0;
            pursuit |= strcmp(info.asset_name, "EnemyBehavior") == 0;
        }
        ok &= count == (attached ? 2 : 1) && health && pursuit == attached;
    }
    printf("ui-instance-behavior %s: attach/remove Undo/Redo preserves exact behavior set at %d\n", ok ? "PASS" : "FAIL", step);
    loop->integration_failed |= !ok;
}

static void narrow_viewport_input(app_loop *loop, int step)
{
    const char *id = NULL;
    bool down = false;
    if (step == 2482 || step == 2483) {
        id = "viewport.more";
        down = step == 2482;
    }
    if (step == 2486 || step == 2487) {
        id = "viewport.fit";
        down = step == 2486;
    }
    if (id) {
        float x, y;
        if (editor_ui_item_center(id, &x, &y)) {
            inject_mouse_button(x, y, down);
        } else {
            printf("ui-narrow-viewport FAIL: missing %s at %d\n", id, step);
            loop->integration_failed = 1;
        }
    }
}

static bool viewport_pending_resize_check()
{
    engine_viewport *viewport = engine_viewport_new(640, 360);
    if (!viewport) {
        return false;
    }
    uint32_t count = engine_viewport_recreate_count(viewport);
    int width, height;
    bool ok = engine_viewport_request_resize(viewport, 1000, 300) == 0;
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    engine_viewport_pixel_size(viewport, &width, &height);
    ok &= fabsf(camera.span_width / camera.span_height - 1000.0f / 300.0f) < 0.0001f &&
          width == 640 && height == 360 &&
          engine_viewport_recreate_count(viewport) == count;
    ok &= engine_viewport_commit_resize(viewport) == 0;
    engine_viewport_pixel_size(viewport, &width, &height);
    ok &= width == 1000 && height == 300 &&
          engine_viewport_recreate_count(viewport) == count + 1;
    engine_viewport_free(viewport);
    printf("ui-viewport-pending-resize %s: camera changes immediately; allocation waits for commit\n", ok ? "PASS" : "FAIL");
    return ok;
}

static void narrow_viewport_draw(app_loop *loop, int step)
{
    editor_ui_set_pane_visible(EDITOR_UI_PANE_SCENE, true);
    ImGui_SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui_SetNextWindowPos((ImVec2){ 600, 200 }, ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){ 240, 280 }, ImGuiCond_Always);
    engine_viewport_controls controls = {
        .title = "A deliberately long scene title",
        .session = true,
        .playing = false,
        .editing = true,
    };
    int old_width, old_height;
    engine_viewport_pixel_size(loop->viewport, &old_width, &old_height);
    uint32_t old_count = engine_viewport_recreate_count(loop->viewport);
    ImGui_PushStyleVar(ImGuiStyleVar_ImageBorderSize, 2.0f);
    engine_viewport_action action = engine_viewport_draw(loop->viewport, &controls);
    if (step == 2476) {
        editor_ui_rect image;
        engine_viewport_camera camera;
        engine_viewport_get_camera(loop->viewport, &camera);
        int width, height;
        engine_viewport_pixel_size(loop->viewport, &width, &height);
        bool ok = editor_ui_item_rect("viewport.image", &image);
        if (ok) {
            float border = ImGui_GetStyle()->ImageBorderSize * 2;
            int image_width = (int)(image.width - border);
            int image_height = (int)(image.height - border);
            ok = image_width > 0 && image_height > 0 &&
                 fabsf(
                     camera.span_width / camera.span_height - (float)image_width / image_height
                 ) < 0.0001f &&
                 width == old_width && height == old_height &&
                 engine_viewport_recreate_count(loop->viewport) == old_count;
        }
        printf("ui-viewport-first-resize %s: first narrow frame matches bordered image before reallocating\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    ImGui_PopStyleVar();
    loop->narrow_fit_activated |= action == ENGINE_VIEWPORT_FIT;
    if (step == 2480) {
        editor_ui_rect button;
        ImGuiWindow *window = ImGui_FindWindowByName("Scene");
        editor_ui_rect pane = { 0 };
        if (window) {
            pane = (editor_ui_rect){ window->Pos.x,
                                     window->Pos.y,
                                     window->Size.x,
                                     window->Size.y };
        }
        bool ok = window && pane.width <= 241;
        editor_ui_rect image;
        ok &= editor_ui_item_rect("viewport.image", &image);
        const char *ids[] = { "viewport.play", "viewport.stop", "viewport.step", "viewport.more" };
        for (int i = 0; i < 4; i++) {
            ok &= editor_ui_item_rect(ids[i], &button);
            if (ok) {
                ok &= button.x >= image.x && button.y >= image.y &&
                      button.x + button.width <= image.x + image.width &&
                      button.y + button.height <= image.y + image.height;
            }
        }
        ok &= !editor_ui_item_rect("viewport.fit", &button);
        printf("ui-narrow-viewport %s: direct transport stays inside 240px Scene; Fit moves to More\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 2490) {
        printf(
            "ui-narrow-viewport %s: More exposes working Fit Scene action\n",
            loop->narrow_fit_activated ? "PASS" : "FAIL"
        );
        loop->integration_failed |= !loop->narrow_fit_activated;
    }
}

static ImVec2 interaction_point(app_loop *loop, float x, float y)
{
    editor_ui_rect rect = { 0 };
    engine_viewport_camera camera;
    editor_ui_item_rect("viewport.image", &rect);
    engine_viewport_get_camera(loop->viewport, &camera);
    float border = ImGui_GetStyle()->ImageBorderSize;
    return (ImVec2){ rect.x + border +
                         ((x - camera.center_x) / camera.span_width + 0.5f) *
                             (rect.width - 2 * border),
                     rect.y + border +
                         (0.5f - (y - camera.center_y) / camera.span_height) *
                             (rect.height - 2 * border) };
}

static void interaction_input(app_loop *loop, int step)
{
    if (step < 2876) {
        return;
    }
    bool ok = true;
    if (step == 2876) {
        editor_ui_set_focus_mode(false);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_CONTENT, true);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, true);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, true);
        editor_ui_set_reduced_motion(false);
        const char
            *json = "{\"version\":1,\"name\":\"InteractionShape\","
                    "\"components\":{"
                    "\"GameTransform\":{\"x\":200,\"y\":0},\"GameAppearance\":{"
                    "\"width\":20,\"height\":18,\"color\":4294967295,\"shape\":"
                    "4,\"stroke_width\":1}},\"behaviors\":[]}";
        ok = app_scene_authoring_register_prototype(loop->authoring, json, NULL) == 0;
        for (int i = 0; ok && i < 2; i++) {
            ok = app_scene_authoring_add_instance(loop->authoring, "InteractionShape") ==
                 0;
            loop->interaction_actors[i] = app_scene_authoring_runtime_state(loop->authoring)
                                              .selected_entity;
            GameTransform *position = GAME_GameTransform_get(loop->world, loop->interaction_actors[i]);
            ok = ok && position;
            if (position) {
                position->x = 200 + i * 100;
            }
        }
        engine_viewport_view view = { 250, 0, 400, 200 };
        engine_viewport_set_view(loop->viewport, &view);
    }
    if (step == 2882) {
        loop->sidebar_before = editor_ui_left_pane_width();
    }
    const struct
    {
        int press;
        const char *id;
    } actions[] = {
        { 2884, "navigator.collapse" }, { 2890, "navigator.expand" },
        { 2910, "inspector.collapse" }, { 2914, "inspector.expand" },
        { 2950, "document.menu" },      { 2956, "document.undo" },
        { 2980, "document.menu" },      { 2986, "document.undo" },
        { 2994, "document.menu" },      { 3000, "document.redo" },
        { 3040, "graph.play" },         { 3048, "graph.play" },
        { 3056, "graph.step" },         { 3064, "graph.stop" },
        { 3090, "inspector.collapse" }, { 3094, "inspector.expand" },
    };
    for (int i = 0; i < (int)(sizeof actions / sizeof *actions); i++) {
        if (step != actions[i].press && step != actions[i].press + 1) {
            continue;
        }
        float x, y;
        if (editor_ui_item_center(actions[i].id, &x, &y)) {
            inject_mouse_button(x, y, step == actions[i].press);
        } else {
            ok = false;
        }
    }
    if (step == 2888) {
        float width = editor_ui_left_pane_width();
        editor_ui_rect rail;
        ok = width > 32 && width < loop->sidebar_before &&
             editor_ui_item_rect("navigator.expand", &rail);
    }
    if (step == 2907) {
        ok = fabsf(editor_ui_left_pane_width() - loop->sidebar_before) < 2;
        editor_ui_set_reduced_motion(true);
    }
    if (step == 2913) {
        editor_ui_rect rail;
        ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector Rail");
        ok = inspector && fabsf(inspector->Size.x - 32) < 1 &&
             editor_ui_item_rect("inspector.expand", &rail);
    }
    if (step == 2918) {
        editor_ui_rect inspector;
        ok = editor_ui_pane_rect(EDITOR_UI_PANE_INSPECTOR, &inspector) &&
             inspector.width > 160;
        editor_ui_set_reduced_motion(false);
    }
    if (step == 2920 || step == 2921) {
        ImVec2 p = interaction_point(loop, 204, 0);
        inject_mouse_button(p.x, p.y, step == 2920);
    }
    if (step == 2924) {
        ecs_entity selected = app_scene_authoring_runtime_state(loop->authoring).selected_entity;
        ok = selected.slot == loop->interaction_actors[0].slot &&
             selected.generation == loop->interaction_actors[0].generation;
    }
    if (step >= 2930 && step <= 2933) {
        ImVec2 p = interaction_point(loop, step == 2930 ? 180 : 325, step == 2930 ? -30 : 30);
        inject_mouse_button(p.x, p.y, step != 2933);
    }
    if (step == 2936) {
        ok = app_scene_authoring_selection_count(loop->authoring) == 2;
    }
    if (step == 2940 || step == 2941) {
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Delete, step == 2940);
    }
    if (step == 2945) {
        ok = !ecs_entity_is_alive(loop->world, loop->interaction_actors[0]) &&
             !ecs_entity_is_alive(loop->world, loop->interaction_actors[1]) &&
             app_scene_authoring_selection_count(loop->authoring) == 0;
    }
    if (step == 2974) {
        loop->scene_edit_entity = app_scene_authoring_runtime_state(loop->authoring)
                                      .selected_entity;
        ok = loop->scene_edit_entity.slot &&
             app_scene_authoring_set_instance_component(loop->authoring, "GameOrientation", true) ==
                 0 &&
             ecs_component_has(
                 loop->world,
                 loop->scene_edit_entity,
                 GAME_COMPONENT_GameOrientation.id
             );
    }
    if (step == 2990 || step == 3004) {
        ok = ecs_component_has(
                 loop->world,
                 loop->scene_edit_entity,
                 GAME_COMPONENT_GameOrientation.id
             ) == (step == 3004);
    }
    if (step == 3010) {
        snprintf(loop->drop_root, sizeof loop->drop_root, "/tmp/c-blueprints-drop-XXXXXX");
        ok = mkdtemp(loop->drop_root) != NULL;
        char path[128];
        snprintf(path, sizeof path, "%s/InteractionShape.json", loop->drop_root);
        ok = ok &&
             app_scene_authoring_save_prototype(loop->authoring, "InteractionShape", path) ==
                 0 &&
             app_scene_authoring_set_content_root(loop->authoring, loop->drop_root) == 0;
        editor_ui_request_focus(EDITOR_UI_FOCUS_CONTENT_DRAWER);
    }
    if (step == 3018) {
        char path[128];
        snprintf(path, sizeof path, "%s/InteractionShape.json", loop->drop_root);
        ok = editor_ui_item_center(
            path,
            &loop->drop_pointer.x,
            &loop->drop_pointer.y
        );
        if (ok) {
            inject_mouse_button(loop->drop_pointer.x, loop->drop_pointer.y, true);
        }
    }
    if (step == 3019) {
        inject_mouse_button(loop->drop_pointer.x + 12, loop->drop_pointer.y, true);
    }
    if (step == 3020 || step == 3021 || step == 3022) {
        ImVec2 p = interaction_point(loop, 250, 50);
        inject_mouse_button(p.x, p.y, step != 3022);
    }
    if (step == 3026) {
        ecs_entity entity = app_scene_authoring_runtime_state(loop->authoring).selected_entity;
        const GameTransform *position = GAME_GameTransform_get_const(loop->world, entity);
        ok = position && fabsf(position->x - 250) < 2 && fabsf(position->y + 50) < 2;
        char path[128];
        snprintf(path, sizeof path, "%s/InteractionShape.json", loop->drop_root);
        unlink(path);
        rmdir(loop->drop_root);
    }
    if (step == 3032) {
        ok = app_scene_authoring_open_behavior(loop->authoring, "IOFixture") == 0;
    }
    if (step == 3044 || step == 3052 || step == 3060 || step == 3068) {
        app_runtime_state state = app_scene_authoring_runtime_state(loop->authoring);
        editor_ui_rect graph, scene, play, stop, advance;
        ok = state.target == APP_RUNTIME_SCENE && state.playing == (step == 3044) &&
             editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &graph) &&
             editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &scene) &&
             editor_ui_item_rect("graph.play", &play) &&
             editor_ui_item_rect("graph.stop", &stop) &&
             editor_ui_item_rect("graph.step", &advance);
        if (ok) {
            ok = play.x + play.width <= stop.x && stop.x + stop.width <= advance.x;
        }
        if (!ok) {
            printf(
                "ui-graph-transport FAIL: step=%d target=%d playing=%d "
                "message=%s\n",
                step,
                state.target,
                state.playing,
                app_scene_authoring_message(loop->authoring)
            );
            (void)app_save_screenshot("/tmp/c-blueprints-graph-transport-failure.png");
        }
    }
    if (step == 3068) {
        const char *capture = getenv("CBLUEPRINT_UI_OVERLAY_SCREENSHOT");
        if (capture) {
            ok &= app_save_screenshot(capture) == 0;
        }
    }
    if (step == 3076) {
        ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector");
        ok = inspector && !(inspector->Flags & (ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoDocking));
        if (inspector) {
            loop->inspector_dock = inspector->DockId;
            ImGui_DockContextQueueUndockWindow(ImGui_GetCurrentContext(), inspector);
        }
    }
    if (step == 3080) {
        ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector");
        ok = inspector && !inspector->DockNode;
        if (inspector) {
            loop->inspector_position = inspector->Pos;
            loop->drop_pointer = (ImVec2){ inspector->Pos.x + 60,
                                           inspector->Pos.y + 10 };
        }
    }
    if (step == 3082 || step == 3083 || step == 3084) {
        inject_mouse_button(
            loop->drop_pointer.x - (step > 3082 ? 80 : 0),
            loop->drop_pointer.y + (step > 3082 ? 40 : 0),
            step != 3084
        );
    }
    if (step == 3088) {
        ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector");
        ok = inspector && !inspector->DockNode &&
             fabsf(inspector->Pos.x - (loop->inspector_position.x - 80)) < 3 &&
             fabsf(inspector->Pos.y - (loop->inspector_position.y + 40)) < 3;
        if (inspector) {
            loop->inspector_position = inspector->Pos;
        }
    }
    if (step == 3098) {
        ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector");
        ok = inspector && !inspector->DockNode &&
             fabsf(inspector->Pos.x - loop->inspector_position.x) < 1 &&
             fabsf(inspector->Pos.y - loop->inspector_position.y) < 1;
    }
    if (step == 3100 && loop->inspector_dock) {
        ImGui_DockBuilderDockWindow("Inspector", loop->inspector_dock);
    }
    if (step == 3104) {
        ImGuiWindow *inspector = ImGui_FindWindowByName("Inspector");
        ok = inspector && inspector->DockNode &&
             inspector->DockId == loop->inspector_dock;
    }
    if (!ok || step == 3076 || step == 3088 || step == 3098 || step == 3104 ||
        step == 3044 || step == 3052 || step == 3060 || step == 3068 ||
        step == 3026 || step == 2974 || step == 2990 || step == 3004 ||
        step == 2888 || step == 2907 || step == 2913 || step == 2918 ||
        step == 2924 || step == 2936 || step == 2945) {
        printf(
            "ui-direct-interaction %s: sidebar/picking/marquee/Delete at %d\n",
            ok ? "PASS" : "FAIL",
            step
        );
    }
    loop->integration_failed |= !ok;
}

static int app_frame(app_loop *loop)
{
    if (!cf_app_is_running()) {
        return 1;
    }
    int step = loop->frame - 3;
    scene_edit_input(loop, step);
    scene_numeric_input(loop, step);
    narrow_viewport_input(loop, step);
    instance_behavior_input(loop, step);
    deferred_begin_input(loop, step);
    scene_search_input(loop, step);
    material_disclosure_input(loop, step);
    live_behavior_open_input(loop, step);
    document_io_input(loop, step);
    interaction_input(loop, step);
    if (step == 509) {
        ImGuiIO_AddMousePosEvent(ImGui_GetIO(), 900.0f, 170.0f);
    }
    if (step == 480) {
        loop->integration_failed |= editor_open_graph(loop->graph_editor, 0) != 0;
    }
    if (step == 488 || step == 489 || step == 498 || step == 499 ||
        step == 506 || step == 507) {
        char id[64];
        snprintf(id, sizeof id, "fields.%llu", (unsigned long long)loop->field_node);
        float x, y;
        if (editor_ui_item_center(id, &x, &y)) {
            inject_mouse_button(x, y, step == 488 || step == 498 || step == 506);
        } else {
            printf("ui-fields missing disclosure at step %d\n", step);
            loop->integration_failed = 1;
        }
    }

    if (step == 570 || step == 571 || step == 577 || step == 578) {
        const graph *active = editor_active_graph(loop->graph_editor);
        char item[96];
        if (step < 577) {
            snprintf(
                item,
                sizeof item,
                "fields.%llu",
                (unsigned long long)active->entry_id
            );
        } else {
            snprintf(
                item,
                sizeof item,
                "fields.option.%llu.1",
                (unsigned long long)active->entry_id
            );
        }
        float x, y;
        if (editor_ui_item_center(item, &x, &y)) {
            inject_mouse_button(x, y, step == 570 || step == 577);
        } else {
            printf("ui-function-fields missing %s at %d\n", item, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 822 || step == 852) {
        editor_select_node(loop->graph_editor, NULL);
    }
    if (step == 832 || step == 844) {
        editor_select_node(
            loop->graph_editor,
            graph_node_by_id(editor_active_graph(loop->graph_editor), 11)
        );
    }
    if (step == 840 || step == 852) {
        editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, step == 852);
    }
    if (step == 864 || step == 865 || step == 879 || step == 880 ||
        step == 896 || step == 897 || step == 904 || step == 905) {
        const char *item = step < 870 ? "navigator.document"
                         : step < 890 ? "outline.settings"
                         : step < 900 ? "settings.close"
                                      : "graph.actions";
        float x, y;
        if (editor_ui_item_center(item, &x, &y)) {
            inject_mouse_button(x, y, step == 864 || step == 879 || step == 896 || step == 904);
        } else {
            printf("ui-settings missing %s at %d\n", item, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 870 || step == 871) {
        float x, y;
        if (editor_ui_item_center("navigator.document", &x, &y)) {
            inject_mouse_button(x, y, step == 870);
        } else {
            loop->integration_failed = 1;
        }
    }
    if (step == 884 || step == 885) {
        float x, y;
        if (editor_ui_item_center("settings.name", &x, &y)) {
            inject_mouse_button(x, y, step == 884);
        } else {
            loop->integration_failed = 1;
        }
    }
    if (step == 888) {
        ImGuiIO_AddKeyEvent(
            ImGui_GetIO(),
            ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl,
            true
        );
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, true);
    }
    if (step == 889) {
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, false);
        ImGuiIO_AddKeyEvent(
            ImGui_GetIO(),
            ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl,
            false
        );
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "Automatic Weapon");
    }
    if (step == 912 || step == 913) {
        float x, y;
        if (editor_ui_item_center("graph.undo", &x, &y)) {
            inject_mouse_button(x, y, step == 912);
        } else {
            loop->integration_failed = 1;
        }
    }
    const char *navigator_action = NULL;
    bool navigator_down = false;
    switch (step) {
        case 210:
        case 211:
            navigator_action = "navigator.scene";
            navigator_down = step == 210;
            break;
        case 214:
        case 215:
            navigator_action = "navigator.scene";
            navigator_down = step == 214;
            break;
        case 754:
        case 755:
            navigator_action = "navigator.document";
            navigator_down = step == 754;
            break;
        case 759:
        case 760:
            navigator_action = "navigator.document";
            navigator_down = step == 759;
            break;
        case 774:
        case 775:
            navigator_action = "navigator.scene";
            navigator_down = step == 774;
            break;
        case 784:
        case 785:
            navigator_action = "navigator.files";
            navigator_down = step == 784;
            break;
    }
    if (navigator_action) {
        float x, y;
        if (editor_ui_item_center(navigator_action, &x, &y)) {
            inject_mouse_button(x, y, navigator_down);
        } else {
            printf("ui-navigator missing %s at %d\n", navigator_action, step);
            loop->integration_failed = 1;
        }
    }
    const char *flag_action = NULL;
    bool flag_down = false;
    switch (step) {
        case 704:
        case 705:
            flag_action = "flags.11.0.2";
            flag_down = step == 704;
            break;
        case 712:
        case 713:
            flag_action = "flags.choice.1";
            flag_down = step == 712;
            break;
        case 726:
        case 727:
            flag_action = "graph.actions";
            flag_down = step == 726;
            break;
        case 734:
        case 735:
            flag_action = "graph.undo";
            flag_down = step == 734;
            break;
    }
    if (flag_action) {
        float x, y;
        if (editor_ui_item_center(flag_action, &x, &y)) {
            inject_mouse_button(x, y, flag_down);
        } else {
            printf("ui-flags missing %s at %d\n", flag_action, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 720 || step == 721) {
        inject_mouse_button(900, 170, step == 720);
    }
    if (step == 1260) {
        loop->integration_failed |= app_scene_authoring_set_content_root(loop->authoring, "content") !=
                                    0;
        app_scene_authoring_refresh_content(loop->authoring);
    }
    if (step == 1650 || step == 1690 || step == 1710) {
        const char *name = step == 1690 ? "Enemy" : "Projectile";
        loop->integration_failed |= app_scene_authoring_preview_prototype(loop->authoring, name) !=
                                    0;
        loop->integration_failed |= app_scene_authoring_runtime_command(
                                        loop->authoring,
                                        APP_RUNTIME_PREVIEW,
                                        APP_RUNTIME_PAUSE
                                    ) != 0;
    }
    if (step == 1662 || step == 1702) {
        engine_viewport_view view = step == 1662
                                      ? (engine_viewport_view){ 500, 300, 240, 120 }
                                      : (engine_viewport_view){ 70, -20, 100, 100 };
        engine_viewport_set_view(loop->viewport, &view);
    }
    if (step == 1675 || step == 1725) {
        app_scene_authoring_stop_prototype_preview(loop->authoring);
    }
    const char *camera_action = NULL;
    bool camera_down = false;
    switch (step) {
        case 1508:
        case 1509:
            camera_action = "viewport.fit";
            camera_down = step == 1508;
            break;
        case 1568:
        case 1569:
            camera_action = "viewport.reset_camera";
            camera_down = step == 1568;
            break;
        case 1600:
        case 1601:
            camera_action = "navigator.scene";
            camera_down = step == 1600;
            break;
        case 1608:
        case 1609:
            camera_action = "navigator.scene";
            camera_down = step == 1608;
            break;
        case 1616:
        case 1617:
        case 1618:
        case 1619:
            camera_action = "scene.actor.Player_1";
            camera_down = step == 1616 || step == 1618;
            break;
    }
    if (camera_action) {
        float x, y;
        if (editor_ui_item_center(camera_action, &x, &y)) {
            inject_mouse_button(x, y, camera_down);
        } else {
            printf("ui-scene-camera missing %s at %d\n", camera_action, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 1520 || step == 1544 || step == 1545 || step == 1546) {
        float x, y;
        if (editor_ui_item_center("viewport.image", &x, &y)) {
            if (step == 1520) {
                editor_ui_rect rect;
                editor_ui_item_rect("viewport.image", &rect);
                float border = ImGui_GetStyle()->ImageBorderSize;
                x = floorf(rect.x + rect.width * 0.7f);
                y = floorf(rect.y + rect.height * 0.35f);
                loop->zoom_pointer = (ImVec2){ x, y };
                float u = (x - rect.x - border) / (rect.width - 2 * border) - 0.5f;
                float v = 0.5f - (y - rect.y - border) / (rect.height - 2 * border);
                loop->zoom_anchor = (ImVec2){
                    loop->camera_before.center_x + u * loop->camera_before.span_width,
                    loop->camera_before.center_y + v * loop->camera_before.span_height,
                };
            }
            ImGuiIO_AddMousePosEvent(ImGui_GetIO(), x + (step >= 1545 ? 50 : 0), y);
            if (step == 1520) {
                ImGuiIO_AddMouseWheelEvent(ImGui_GetIO(), 0, 2);
            }
            if (step == 1544 || step == 1546) {
                ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 2, step == 1544);
            }
        } else {
            loop->integration_failed = 1;
        }
    }
    const char *shape_action = NULL;
    bool shape_down = false;
    switch (step) {
        case 1452:
        case 1453:
            shape_action = "prototype.shape";
            shape_down = step == 1452;
            break;
        case 1460:
        case 1461:
            shape_action = "choice.1";
            shape_down = step == 1460;
            break;
        case 1474:
        case 1475:
            shape_action = "document.menu";
            shape_down = step == 1474;
            break;
        case 1482:
        case 1483:
            shape_action = "document.undo";
            shape_down = step == 1482;
            break;
    }
    if (shape_action) {
        float x, y;
        if (editor_ui_item_center(shape_action, &x, &y)) {
            inject_mouse_button(x, y, shape_down);
        } else {
            printf("ui-shape missing %s at %d\n", shape_action, step);
            loop->integration_failed = 1;
        }
    }
    const char *appearance_action = NULL;
    bool appearance_down = false;
    switch (step) {
        case 1264:
        case 1265:
            appearance_action = "navigator.files";
            appearance_down = step == 1264;
            break;
        case 1272:
        case 1273:
        case 1274:
        case 1275:
            appearance_action = "content/prototypes/Player.json";
            appearance_down = step == 1272 || step == 1274;
            break;
        case 1284:
        case 1285:
            appearance_action = "prototype.component.GameAppearance";
            appearance_down = step == 1284;
            break;
        case 1288:
        case 1289:
            appearance_action = "prototype.component.GameMaterial";
            appearance_down = step == 1288;
            break;
        case 1292:
        case 1293:
            appearance_action = "prototype.color.GameMaterial.scene";
            appearance_down = step == 1292;
            break;
        case 1300:
        case 1301:
            appearance_action = "color.hex";
            appearance_down = step == 1300;
            break;
        case 1316:
        case 1317:
            appearance_action = "color.done";
            appearance_down = step == 1316;
            break;
        case 1330:
        case 1331:
            appearance_action = "document.menu";
            appearance_down = step == 1330;
            break;
        case 1338:
        case 1339:
            appearance_action = "document.undo";
            appearance_down = step == 1338;
            break;
    }
    if (appearance_action) {
        float x, y;
        if (editor_ui_item_center(appearance_action, &x, &y)) {
            inject_mouse_button(x, y, appearance_down);
        } else {
            printf("ui-appearance missing %s at %d\n", appearance_action, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 1304) {
        ImGuiIO_AddKeyEvent(
            ImGui_GetIO(),
            ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl,
            true
        );
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, true);
    }
    if (step == 1305) {
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, false);
        ImGuiIO_AddKeyEvent(
            ImGui_GetIO(),
            ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl,
            false
        );
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "3366CC80");
    }
    if (step == 1358) {
        editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &loop->graph_before_focus);
        editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &loop->scene_before_expand);
    }
    if (step == 1385) {
        editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, true);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, true);
    }
    if (step == 1360) {
        editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, false);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, false);
        app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_PAUSE);
        ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                        GAME_COMPONENT_GameAppearance.id };
        ecs_query entities;
        if (ecs_query_begin(loop->world, &entities, required, 2) == ECS_OK) {
            ecs_entity entity;
            int index = 0;
            while (ecs_query_next(&entities, &entity)) {
                GameTransform *position = GAME_GameTransform_get(loop->world, entity);
                *position = (GameTransform){ 80.0f + 100.0f * index++, -180.0f };
            }
            ecs_query_end(&entities);
        }
    }
    if (step == 1045 || step == 1142) {
        editor_select_node(loop->graph_editor, NULL);
    }
    if (step == 1200) {
        editor_ui_set_reduced_motion(true);
    }
    if (step == 1052 || step == 1053 || step == 1202 || step == 1203) {
        float x, y;
        if (editor_ui_item_center("graph.fit", &x, &y)) {
            inject_mouse_button(x, y, step == 1052 || step == 1202);
        } else {
            loop->integration_failed = 1;
        }
    }
    if (step == 1082 || step == 1083 || step == 1220 || step == 1221) {
        editor_node_ui drawn;
        if (editor_query_node_ui(loop->graph_editor, 35, &drawn)) {
            inject_mouse_button(drawn.title_x, drawn.title_y, step == 1082 || step == 1220);
        } else {
            loop->integration_failed = 1;
        }
    }
    if (step == 1120) {
        editor_ui_rect pane;
        editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &pane);
        ImGuiIO_AddMousePosEvent(ImGui_GetIO(), pane.x + 20, pane.y + pane.height - 20);
        ImGuiIO_AddMouseWheelEvent(ImGui_GetIO(), 0, -2);
    }
    if (step == 1015) {
        editor_select_node(
            loop->graph_editor,
            graph_node_by_id(editor_active_graph(loop->graph_editor), 35)
        );
    }
    if (step == 930) {
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "ProjectileHitBehavior") !=
                                    0;
        editor_select_node(loop->graph_editor, NULL);
    }
    const char *color_action = NULL;
    bool color_down = false;
    switch (step) {
        case 940:
        case 941:
        case 978:
        case 979:
            color_action = "color.35.0.2";
            color_down = step == 940 || step == 978;
            break;
        case 948:
        case 949:
            color_action = "color.hex";
            color_down = step == 948;
            break;
        case 964:
        case 965:
        case 986:
        case 987:
            color_action = "color.done";
            color_down = step == 964 || step == 986;
            break;
        case 992:
        case 993:
            color_action = "graph.actions";
            color_down = step == 992;
            break;
        case 998:
        case 999:
            color_action = "graph.undo";
            color_down = step == 998;
            break;
    }
    if (color_action) {
        float x, y;
        if (editor_ui_item_center(color_action, &x, &y)) {
            inject_mouse_button(x, y, color_down);
        } else {
            printf("ui-color missing %s at %d\n", color_action, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 952) {
        ImGuiIO_AddKeyEvent(
            ImGui_GetIO(),
            ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl,
            true
        );
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, true);
    }
    if (step == 953) {
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, false);
        ImGuiIO_AddKeyEvent(
            ImGui_GetIO(),
            ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl,
            false
        );
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "3366CC80");
    }
    if (step == 600) {
        loop->integration_failed |= editor_open_graph(loop->graph_editor, 0) != 0;
    }
    const char *navigation = NULL;
    bool navigation_down = false;
    switch (step) {
        case 610:
        case 611:
            navigation = "graph.call.open.197";
            navigation_down = step == 610;
            break;
        case 620:
        case 621:
            navigation = "graph.call.open.182";
            navigation_down = step == 620;
            break;
        case 635:
        case 636:
            navigation = "graph.breadcrumb.0";
            navigation_down = step == 635;
            break;
        case 650:
        case 651:
            navigation = "graph.picker";
            navigation_down = step == 650;
            break;
        case 672:
        case 673:
            navigation = "graph.picker";
            navigation_down = step == 672;
            break;
        case 679:
        case 680:
            navigation = "graph.picker.0";
            navigation_down = step == 679;
            break;
        case 657:
        case 658:
            navigation = "graph.picker.173";
            navigation_down = step == 657;
            break;
    }
    if (navigation) {
        float x, y;
        if (editor_ui_item_center(navigation, &x, &y)) {
            inject_mouse_button(x, y, navigation_down);
        } else {
            printf("ui-navigation missing %s at %d\n", navigation, step);
            loop->integration_failed = 1;
        }
    }
    if (step == 520) {
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "AutoWeaponBehavior") !=
                                    0;
    }
    if (step == 524 || step == 542) {
        const char *name = step == 524 ? "Fire Ready Projectiles" : "Fire Projectile";
        graph *target = behavior_asset_find_graph_by_name(editor_behavior_asset(loop->graph_editor), name);
        loop->integration_failed |= !target ||
                                    editor_open_graph(loop->graph_editor, target->graph_id) !=
                                        0;
    }
    if (step == 420) {
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "EnemyBehavior") !=
                                    0;
    }
    if (step == 424) {
        const behavior_asset *asset = editor_behavior_asset(loop->graph_editor);
        graph *function = behavior_asset_find_graph_by_name(asset, "Move Toward");
        loop->integration_failed |= !function || editor_open_graph(
                                                     loop->graph_editor,
                                                     function->graph_id
                                                 ) != 0;
    }
    if (step >= 429 && step <= 432) {
        int index = step >= 431;
        if (step == 431) {
            ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_LeftCtrl, true);
        }
        inject_mouse_button(loop->extract_x[index], loop->extract_y[index], step == 429 || step == 431);
    }
    if (step == 433) {
        ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_LeftCtrl, false);
    }
    if (step == 444) {
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "Direction");
    }
    const char *extract_action = NULL;
    bool extract_down = false;
    switch (step) {
        case 434:
        case 435:
        case 454:
        case 455:
        case 466:
        case 467:
            extract_action = "graph.actions";
            extract_down = step == 434 || step == 454 || step == 466;
            break;
        case 439:
        case 440:
            extract_action = "graph.extract";
            extract_down = step == 439;
            break;
        case 447:
        case 448:
            extract_action = "graph.extract.confirm";
            extract_down = step == 447;
            break;
        case 459:
        case 460:
            extract_action = "graph.undo";
            extract_down = step == 459;
            break;
        case 471:
        case 472:
            extract_action = "graph.redo";
            extract_down = step == 471;
            break;
    }
    if (extract_action) {
        float x, y;
        if (editor_ui_item_center(extract_action, &x, &y)) {
            inject_mouse_button(x, y, extract_down);
        } else {
            printf("ui-extract missing %s at step %d\n", extract_action, step);
            loop->integration_failed = 1;
        }
    }
    if (step >= 385 && step <= 388) {
        inject_mouse_button(loop->pursuit_click_x, loop->pursuit_click_y, step == 385 || step == 387);
    }
    if (step == 393 || step == 394) {
        float x, y;
        if (editor_ui_item_center("graph.back", &x, &y)) {
            inject_mouse_button(x, y, step == 393);
        } else {
            loop->integration_failed = 1;
        }
    }

    if (step == 410) {
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "AutoWeaponBehavior") !=
                                    0;
    }
    if (step == 380) {
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "EnemyBehavior") !=
                                    0;
    }
    if (step == 370) {
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "ProjectileHitBehavior") !=
                                    0;
    }
    if (step == 310) {
        ecs_query query;
        ecs_component_id id = GAME_COMPONENT_GameProjectile.id;
        bool found = false;
        if (ecs_query_begin(loop->world, &query, &id, 1) == ECS_OK) {
            found = ecs_query_next(&query, &loop->restart_projectile);
            ecs_query_end(&query);
        }
        printf("ui-viewport-restart-fixture %s: live projectile exists before Restart\n", found ? "PASS" : "FAIL");
        loop->integration_failed |= !found;
    }
    if (step == 329) {
        loop->integration_failed |= app_scene_authoring_preview_prototype(loop->authoring, "Player") !=
                                    0;
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "WeaponBehavior") !=
                                    0;
        loop->integration_failed |= app_scene_authoring_runtime_command(
                                        loop->authoring,
                                        APP_RUNTIME_PREVIEW,
                                        APP_RUNTIME_PAUSE
                                    ) != 0;
    }
    if (step == 285) {
        loop->projectiles_before = runtime_projectiles(loop->world);
        app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY);
        engine_set_focus(loop->runtime, ENGINE_FOCUS_VIEWPORT);
        loop->integration_failed |= app_scene_authoring_input(loop->authoring, "Fire", 1.0, 1) <
                                    0;
    }
    if (step == 289) {
        app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_SCENE, APP_RUNTIME_PAUSE);
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "WeaponBehavior") !=
                                    0;
    }
    if (step == 252 || step == 273 || step == 277) {
        editor_ui_rect pane;
        editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &pane);
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        loop->zoom_before = query.zoom;
        editor_ui_set_reduced_motion(step == 273);
        ImGuiIO_AddMousePosEvent(ImGui_GetIO(), pane.x + pane.width * 0.5f, pane.y + pane.height * 0.3f);
        ImGuiIO_AddMouseWheelEvent(ImGui_GetIO(), 0.0f, -2.0f);
    }
    if (step == 279 || step == 280) {
        editor_ui_rect pane;
        editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &pane);
        inject_mouse_button(pane.x + pane.width * 0.5f, pane.y + pane.height * 0.3f, step == 279);
    }
    if (step == 236) {
        loop->previous_graph = editor_active_graph(loop->graph_editor)->graph_id;
        loop->integration_failed |= editor_command_apply(
                                        loop->graph_editor,
                                        (editor_command){
                                            .kind = EDITOR_COMMAND_CREATE_GRAPH,
                                            .data.create_graph = { GRAPH_EVENT, "ArrangeFixture" } }
                                    ) != 0;
        const graph *fixture = behavior_asset_find_graph_by_name(
            editor_behavior_asset(loop->graph_editor),
            "ArrangeFixture"
        );
        loop->arrangement_graph = fixture->graph_id;
        loop->integration_failed |= editor_open_graph(loop->graph_editor, loop->arrangement_graph) !=
                                    0;
        for (int i = 0; i < 2; i++) {
            loop->integration_failed |= editor_command_apply(
                                            loop->graph_editor,
                                            (editor_command){
                                                .kind = EDITOR_COMMAND_SPAWN_NODE,
                                                .data.spawn_node = { &GAME_NODE_Has_GameHealth,
                                                                     200.0f,
                                                                     200.0f } }
                                        ) != 0;
        }
    }
    if (step == 239) {
        editor_undo(loop->graph_editor);
    }
    if (step == 242) {
        editor_open_graph(loop->graph_editor, loop->arrangement_graph);
    }
    if (step == 244 || step == 245) {
        editor_open_graph(loop->graph_editor, step == 244 ? loop->previous_graph : loop->arrangement_graph);
    }
    if (step == 230) {
        loop->integration_failed |= app_scene_authoring_preview_prototype(loop->authoring, "Player") !=
                                    0;
        app_scene_authoring_runtime_command(loop->authoring, APP_RUNTIME_PREVIEW, APP_RUNTIME_PAUSE);
    }
    if (step == 232) {
        game_live_definition *definition = game_live_find_definition(loop->live, "InputBehavior");
        loop->preview_before = definition->applied;
        loop->integration_failed |= editor_command_apply(
                                        loop->graph_editor,
                                        (editor_command){
                                            .kind = EDITOR_COMMAND_SPAWN_NODE,
                                            .data.spawn_node = { &GAME_NODE_Has_GameHealth,
                                                                 200.0f,
                                                                 200.0f } }
                                    ) != 0;
    }
    if (step == 203) {
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "InputBehavior") !=
                                    0;
        loop->selection_graph = editor_active_graph(loop->graph_editor);
        loop->selection_node_count = acount(loop->selection_graph->nodes);
        for (int i = 0; i < loop->selection_node_count; i++) {
            const node *n = loop->selection_graph->nodes[i];
            if (n->desc == &GAME_NODE_Has_GameHealth) {
                loop->selection_node = n->id;
                editor_reveal_node_by_id(
                    loop->graph_editor,
                    loop->selection_graph->graph_id,
                    n->id
                );
                break;
            }
        }
    }
    if (step == 212 || step == 213 || step == 226 || step == 227) {
        editor_node_ui item;
        if (!editor_query_node_ui(loop->graph_editor, loop->selection_node, &item)) {
            fprintf(stderr, "ui-selection: authored node must remain visible\n");
            loop->integration_failed = 1;
            return 1;
        }
        inject_mouse_button(item.title_x, item.title_y, step == 212 || step == 226);
    }
    if (step == 8) {
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        for (int i = 0; query.active_graph && i < acount(query.active_graph->nodes);
             i++) {
            const node *n = query.active_graph->nodes[i];
            if (n->desc != &GAME_NODE_Get_GameTransform) {
                continue;
            }
            editor_command command = {
                .kind = EDITOR_COMMAND_SPAWN_NODE,
                .data.spawn_node = { &GAME_NODE_Has_GameHealth, n->x, n->y + 120.0f },
            };
            loop->integration_failed |= editor_command_apply(loop->graph_editor, command) !=
                                        0;
            loop->integration_failed |= editor_reveal_node_by_id(
                                            loop->graph_editor,
                                            query.active_graph->graph_id,
                                            n->id
                                        ) != 0;
            break;
        }
    }
    if (step == 40) {
        strcpy(loop->content_root, "/tmp/c-blueprints-ui-XXXXXX");
        if (!mkdtemp(loop->content_root) ||
            app_scene_authoring_set_content_root(loop->authoring, loop->content_root) != 0 ||
            app_scene_authoring_set_prototype_field(
                loop->authoring,
                "NewPrototype1",
                "GameTransform",
                "x",
                (pin_value){ .type = PIN_F32, .f = 42.0f }
            ) != 0) {
            loop->integration_failed = 1;
            return 1;
        }
    }
    if (step == 50 && app_scene_authoring_set_prototype_field(
                          loop->authoring,
                          "NewPrototype1",
                          "GameTransform",
                          "x",
                          (pin_value){ .type = PIN_F32, .f = 7.0f }
                      ) != 0) {
        loop->integration_failed = 1;
        return 1;
    }
    {
        float card_x = 0.0f;
        float card_y = 0.0f;
        const char *path = "content/behaviors";
        char saved_path[256];
        int click_frame = 0;
        switch (step) {
            case 1:
            case 2: click_frame = step; break;
            case 4:
            case 5:
            case 6:
            case 7:
                path = "content/behaviors/InputBehavior.json";
                click_frame = step - 3;
                break;
            case 12:
            case 13: click_frame = step - 11; break;
            case 16:
            case 17: click_frame = step - 15; break;
            case 20:
            case 21:
                path = "content/prototypes";
                click_frame = step - 19;
                break;
            case 24:
            case 25:
            case 26:
            case 27:
                path = "content/prototypes/Player.json";
                click_frame = step - 23;
                break;
            case 28:
            case 29:
            case 33:
            case 34:
                path = "content.create";
                click_frame = step < 30 ? step - 27 : step - 32;
                break;
            case 31:
            case 32:
                path = "content.new_behavior";
                click_frame = step - 30;
                break;
            case 36:
            case 37:
                path = "content.new_prototype";
                click_frame = step - 35;
                break;
            case 41:
            case 42:
            case 51:
            case 52:
                path = "document.menu";
                click_frame = step < 50 ? step - 40 : step - 50;
                break;
            case 46:
            case 47:
                path = "document.save_as";
                click_frame = step - 45;
                break;
            case 54:
            case 55:
                path = "document.reload";
                click_frame = step - 53;
                break;
            case 59:
            case 60:
                snprintf(saved_path, sizeof saved_path, "%s/prototypes", loop->content_root);
                path = saved_path;
                click_frame = step - 58;
                break;
            case 62:
            case 63:
            case 64:
            case 65:
                snprintf(saved_path, sizeof saved_path, "%s/prototypes/NewPrototype1.json", loop->content_root);
                path = saved_path;
                click_frame = step - 61;
                break;
            case 69:
            case 70:
                path = "content.search";
                click_frame = step - 68;
                break;
            case 77:
            case 78:
            case 82:
            case 83:
                path = step < 80 ? "navigator.collapse" : "navigator.expand";
                click_frame = step < 80 ? step - 76 : step - 81;
                break;
            case 88:
            case 89:
                path = "view.menu";
                click_frame = step - 87;
                break;
            case 91:
            case 92:
                path = "view.navigator";
                click_frame = step - 90;
                break;
            case 97:
            case 98:
                path = "view.menu";
                click_frame = step - 96;
                break;
            case 100:
            case 101:
                path = "view.navigator";
                click_frame = step - 99;
                break;
            case 114:
            case 115:
            case 125:
            case 126:
                path = "view.menu";
                click_frame = step < 120 ? step - 113 : step - 124;
                break;
            case 119:
            case 120:
            case 128:
            case 129:
                path = "view.focus";
                click_frame = step < 125 ? step - 118 : step - 127;
                break;
            case 141:
            case 142:
                path = "graph.actions";
                click_frame = step - 140;
                break;
            case 151:
            case 152:
            case 155:
            case 156:
                path = "content.clear_search";
                click_frame = step < 155 ? step - 150 : step - 154;
                break;
            case 163:
            case 164:
            case 165:
            case 166:
                snprintf(saved_path, sizeof saved_path, "%s/prototypes/NewPrototype1.json", loop->content_root);
                path = saved_path;
                click_frame = step - 162;
                break;
            case 171:
            case 172:
                path = "prototype.component.GameTransform";
                click_frame = step - 170;
                break;
            case 188:
            case 189:
                path = "document.menu";
                click_frame = step - 187;
                break;
            case 192:
            case 193:
                path = "document.undo";
                click_frame = step - 191;
                break;
            case 176:
            case 177:
                path = "prototype.field.GameTransform.x";
                click_frame = step - 175;
                break;
            case 218:
            case 219:
                path = "scene.actor.Player_1";
                click_frame = step - 217;
                break;
        }
        static const struct
        {
            int press;
            const char *path;
        } transport_clicks[] = {
            { 295, "viewport.play" }, { 299, "viewport.play" },
            { 306, "viewport.step" }, { 313, "viewport.restart" },
            { 317, "viewport.play" }, { 324, "viewport.stop" },
            { 332, "viewport.play" }, { 336, "viewport.play" },
            { 343, "viewport.step" }, { 350, "viewport.restart" },
            { 354, "viewport.play" }, { 361, "viewport.stop" },
        };
        for (int i = 0;
             i < (int)(sizeof(transport_clicks) / sizeof(transport_clicks[0]));
             i++) {
            int offset = step - transport_clicks[i].press;
            if (offset == 0 || offset == 1) {
                path = transport_clicks[i].path;
                click_frame = offset + 1;
                break;
            }
        }
        if (click_frame >= 1 && click_frame <= 4 &&
            !editor_ui_item_center(path, &card_x, &card_y)) {
            fprintf(stderr, "ui-integration: item is not visible: %s\n", path);
            const char *failure = getenv("CBLUEPRINT_UI_APPEARANCE_SCREENSHOT");
            if (failure) {
                app_save_screenshot(failure);
            }
            const char *capture = getenv("CBLUEPRINT_UI_SCREENSHOT");
            if (capture) {
                (void)app_save_screenshot(capture);
            }
            loop->integration_failed = 1;
            return 1;
        }
        if (click_frame == 1 || click_frame == 3) {
            printf("ui-input frame=%d press %s\n", step, path);
            inject_mouse_button(card_x, card_y, true);
        } else if (click_frame == 2 || click_frame == 4) {
            printf("ui-input frame=%d release %s\n", step, path);
            inject_mouse_button(card_x, card_y, false);
        }
    }
    if (step == 72) {
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "no-such-asset");
    }
    if (step == 180) {
        ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "73");
    }
    cf_app_update(NULL);
    editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
    encounter_input(loop, step);
    inspector_history_input(loop, step);
    lifecycle_input(loop, step);
    double seconds = (double)(loop->frame + 1) / 60.0;
    editor_tick(loop->graph_editor, seconds);
    int viewport_focused = engine_viewport_focused(loop->viewport);
    if (viewport_focused && cf_key_just_pressed(CF_KEY_ESCAPE)) {
        engine_viewport_release_focus();
        engine_route_escape(loop->runtime);
        viewport_focused = 0;
    }
    engine_set_focus(loop->runtime, viewport_focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_CANVAS);
    app_scene_authoring_input(loop->authoring, "viewport", 0.0, viewport_focused != 0);
    if (app_scene_authoring_frame(loop->authoring, seconds) != FLOW_OK) {
        fprintf(stderr, "runtime frame failed\n");
    }

    editor_prepare_docking();
    if (step >= 2476 && step <= 2492) {
        narrow_viewport_draw(loop, step);
    } else {
        app_scene_authoring_draw_viewport(loop->authoring, loop->viewport);
    }
    app_scene_authoring_draw(loop->authoring);
    editor_draw(loop->graph_editor);
    cf_app_draw_onto_screen(true);
    if (step == 2742) {
        const char *capture = getenv("CBLUEPRINT_UI_LIVE_GRAPH_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 2716) {
        const char *capture = getenv("CBLUEPRINT_UI_MATERIAL_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 2674) {
        const char *capture = getenv("CBLUEPRINT_UI_SCENE_SEARCH_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 2564) {
        const char *capture = getenv("CBLUEPRINT_UI_ATTACHMENTS_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 2485) {
        const char *capture = getenv("CBLUEPRINT_UI_COMPACT_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }

    if ((step == 10 || step == 11) && app_integration_check(loop) != 0) {
        loop->integration_failed = 1;
    }
    float x, y;
    if (step == 14 || step == 18) {
        bool visible = editor_ui_item_center("content/behaviors/InputBehavior.json", &x, &y);
        bool expected = step == 18;
        printf(
            "ui-tree %s: folder %s hides/restores its children\n",
            visible == expected ? "PASS" : "FAIL",
            expected ? "expanded" : "collapsed"
        );
        loop->integration_failed |= visible != expected;
    }
    if (step == 29 || step == 34 || step == 39) {
        app_editor_document_context document = app_scene_authoring_document(loop->authoring);
        const char *expected = step == 29 ? "Player"
                             : step == 34 ? "NewBehavior1"
                                          : "NewPrototype1";
        app_editor_document_kind kind = step == 34 ? APP_EDITOR_DOCUMENT_BEHAVIOR
                                                   : APP_EDITOR_DOCUMENT_PROTOTYPE;
        bool ok = document.kind == kind && document.id &&
                  strcmp(document.id, expected) == 0;
        printf(
            "ui-content %s: open/create %s (actual=%s)\n",
            ok ? "PASS" : "FAIL",
            expected,
            document.id ? document.id : "none"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 44) {
        bool ok = editor_ui_item_center("document.save_as", &x, &y) &&
                  editor_ui_item_center("document.reload", &x, &y) &&
                  editor_ui_item_center("document.undo", &x, &y);
        editor_ui_rect menu;
        ImGuiWindow *navigator = ImGui_FindWindowByName("Navigator");
        ok = ok && editor_ui_item_rect("document.menu", &menu) && navigator &&
             navigator->Pos.y * ImGui_GetIO()->DisplayFramebufferScale.y >=
                 menu.y + menu.height;
        printf("ui-document %s: menu is unobstructed; save, reload and undo are reachable\n", ok ? "PASS" : "FAIL");
        if (!ok) {
            (void)app_save_screenshot("/tmp/c-blueprints-document-menu-failure.png");
        }
        loop->integration_failed |= !ok;
    }
    if (step == 49 || step == 57) {
        if (step == 57) {
            char path[256];
            snprintf(path, sizeof path, "%s/prototypes/NewPrototype1.json", loop->content_root);
            loop->integration_failed |= app_scene_authoring_save_prototype(
                                            loop->authoring,
                                            "NewPrototype1",
                                            path
                                        ) != 0;
        }
        bool ok = saved_prototype_has_x(loop, 42.0f);
        printf(
            "ui-persistence %s: %s preserves authored x=42\n",
            ok ? "PASS" : "FAIL",
            step == 49 ? "Save As" : "Reload after an unsaved edit"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 67) {
        app_editor_document_context document = app_scene_authoring_document(loop->authoring);
        char path[256];
        snprintf(path, sizeof path, "%s/prototypes/NewPrototype1.json", loop->content_root);
        const char *selected = app_scene_authoring_selected_content(loop->authoring);
        bool ok = document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE &&
                  document.id && strcmp(document.id, "NewPrototype1") == 0 &&
                  selected && strcmp(selected, path) == 0;
        printf("ui-content %s: saved Prototype is discoverable in the tree\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 75) {
        bool ok = editor_ui_item_center("content.empty", &x, &y);
        printf("ui-search %s: an unmatched query visibly reports no files\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 80 || step == 85) {
        bool expected = step == 85;
        bool visible = editor_ui_item_center("content.search", &x, &y);
        bool ok = editor_ui_content_drawer_open() == expected && visible == expected;
        printf(
            "ui-navigation %s: Navigator can be %s from the menu\n",
            ok ? "PASS" : "FAIL",
            expected ? "reopened" : "closed"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 95 || step == 104) {
        bool expected = step == 104;
        editor_ui_rect content_rect;
        bool item_visible = editor_ui_item_center("content.search", &x, &y);
        bool rect_visible = editor_ui_pane_rect(EDITOR_UI_PANE_CONTENT, &content_rect);
        bool ok = editor_ui_content_drawer_open() == expected &&
                  item_visible == expected && rect_visible == expected;
        printf("ui-view %s: View menu %s Navigator\n", ok ? "PASS" : "FAIL", expected ? "reopens" : "hides");
        loop->integration_failed |= !ok;
    }
    if (step == 90) {
        bool ok = editor_ui_item_center("view.graph_and_scene", &x, &y);
        printf("ui-layout-menu %s: workspace reset action is reachable\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 105 &&
        app_scene_authoring_open_behavior(loop->authoring, "InputBehavior") != 0) {
        loop->integration_failed = 1;
    }
    if (step == 105) {
        editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, false);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_SCENE, false);
        editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
        bool focus_pending = editor_ui_take_focus(EDITOR_UI_FOCUS_SCENE);
        printf("ui-hidden-focus %s: hidden Scene discards focus requests\n", focus_pending ? "FAIL" : "PASS");
        loop->integration_failed |= focus_pending;
    }
    if (step == 106) {
        editor_ui_show_graph_and_scene();
    }
    if (step == 113) {
        bool ok = ui_graph_scene_are_stacked();
        printf("ui-layout-action %s: View action restores graph above scene\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }

    if (step == 116) {
        editor_ui_set_pane_visible(EDITOR_UI_PANE_COMPILER_RESULTS, false);
    }
    if (step == 118) {
        editor_ui_set_reduced_motion(true);
        loop->integration_failed |= !editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &loop->graph_before_focus);
    }
    if (step == 124 || step == 133) {
        bool focused = step == 124;
        editor_ui_rect content;
        bool content_drawn = editor_ui_pane_rect(EDITOR_UI_PANE_CONTENT, &content);
        bool ok = editor_ui_focus_mode() == focused && content_drawn == !focused &&
                  !editor_ui_pane_visible(EDITOR_UI_PANE_COMPILER_RESULTS) &&
                  !editor_ui_pane_visible(EDITOR_UI_PANE_OUTPUT_LOG) &&
                  ui_graph_scene_are_stacked();
        printf(
            "ui-focus %s: %s preserves central split and hidden-pane choices\n",
            ok ? "PASS" : "FAIL",
            focused ? "enter" : "exit"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 134) {
        editor_ui_rect graph;
        bool ok = editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &graph) &&
                  fabsf(graph.width - loop->graph_before_focus.width) < 3.0f &&
                  fabsf(graph.x - loop->graph_before_focus.x) < 3.0f;
        printf("ui-focus-geometry %s: pane widths restored\n", ok ? "PASS" : "FAIL");
        editor_ui_set_reduced_motion(false);
        loop->integration_failed |= !ok;
        loop->integration_failed |= app_scene_authoring_open_behavior(loop->authoring, "InputBehavior") !=
                                    0;
    }
    if (step == 145) {
        bool ok = editor_ui_item_center("graph.arrange", &x, &y);
        printf("ui-graph-menu %s: arrangement action is visible in the split workspace\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 183) {
        const char *capture = getenv("CBLUEPRINT_UI_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
        char path[256];
        snprintf(path, sizeof path, "%s/prototypes/NewPrototype1.json", loop->content_root);
        bool ok = app_scene_authoring_save_prototype(loop->authoring, "NewPrototype1", path) ==
                      0 &&
                  saved_prototype_has_x(loop, 73.0f);
        printf("ui-prototype-field %s: Inspector widget edit persists x=73\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 196) {
        char path[256];
        snprintf(path, sizeof path, "%s/prototypes/NewPrototype1.json", loop->content_root);
        bool ok = app_scene_authoring_save_prototype(loop->authoring, "NewPrototype1", path) ==
                      0 &&
                  saved_prototype_has_x(loop, 42.0f);
        printf("ui-prototype-undo %s: Document Undo restores the Inspector edit to x=42\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 216 || step == 223 || step == 231) {
        app_editor_document_context document = app_scene_authoring_document(loop->authoring);
        editor_node_ui item;
        const graph *current = editor_active_graph(loop->graph_editor);
        bool scene_selected = step == 223;
        bool ok = document.kind == (scene_selected ? APP_EDITOR_DOCUMENT_SCENE
                                                   : APP_EDITOR_DOCUMENT_BEHAVIOR) &&
                  current == loop->selection_graph &&
                  acount(current->nodes) == loop->selection_node_count &&
                  editor_ui_item_center(scene_selected ? "inspector.scene" : "inspector.graph", &x, &y) &&
                  editor_query_node_ui(loop->graph_editor, loop->selection_node, &item) &&
                  item.selected && ui_graph_scene_are_stacked();
        printf(
            "ui-selection %s: %s preserves the authored graph, node selection "
            "and split\n",
            ok ? "PASS" : "FAIL",
            scene_selected ? "scene inspection" : "graph inspection"
        );
        loop->integration_failed |= !ok;
        const char *capture = getenv("CBLUEPRINT_UI_SCREENSHOT");
        if (scene_selected && capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 231) {
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        loop->prototype_instance = query.inspection_instance;
    }
    if (step == 234) {
        game_live_definition *definition = game_live_find_definition(loop->live, "InputBehavior");
        bool ok = definition->applied != loop->preview_before &&
                  !definition->dirty && definition->unsaved &&
                  acount(definition->applied->graph->nodes) ==
                      loop->selection_node_count + 1;
        printf("ui-live-preview %s: unsaved graph edit reaches scene snapshot automatically\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        ok = query.inspection_instance &&
             query.inspection_instance == loop->prototype_instance &&
             query.inspection_instance->bp != definition->applied &&
             acount(query.inspection_instance->bp->graph->nodes) ==
                 loop->selection_node_count + 1;
        printf("ui-prototype-live %s: graph inspection follows the updated isolated instance\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 238 || step == 241 || step == 243 || step == 247) {
        const graph *g = editor_active_graph(loop->graph_editor);
        int count = acount(g->nodes);
        bool arranged = count >= 2 &&
                        (g->nodes[count - 1]->x != g->nodes[count - 2]->x ||
                         g->nodes[count - 1]->y != g->nodes[count - 2]->y);
        bool expected = step == 238 || step == 247;
        bool ok = count >= 2 && arranged == expected;
        printf(
            "ui-open-arrange %s: %s at step %d\n",
            ok ? "PASS" : "FAIL",
            expected ? "opening arranges overlapping nodes"
                     : "Undo and same-graph inspection preserve positions",
            step
        );
        loop->integration_failed |= !ok;
    }
    if (step == 254 || step == 270 || step == 275) {
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        bool moving = step == 254;
        bool ok = query.zoom < loop->zoom_before && query.camera_moving == moving;
        printf(
            "ui-camera-motion %s: %s\n",
            ok ? "PASS" : "FAIL",
            moving ? "wheel zoom eases toward its target" : "camera settles or honors reduced motion"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 279 || step == 283) {
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        if (step == 279) {
            loop->zoom_before = query.zoom;
        }
        bool ok = !query.camera_moving && query.zoom == loop->zoom_before;
        printf("ui-camera-interrupt %s: canvas press cancels zoom without later drift\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 287) {
        bool ok = runtime_projectiles(loop->world) > loop->projectiles_before;
        printf("ui-authored-fire %s: editor play and Fire produce a graph-authored projectile\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 293) {
        app_editor_document_context document = app_scene_authoring_document(loop->authoring);
        bool ok = document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR &&
                  strcmp(document.id, "WeaponBehavior") == 0 &&
                  acount(editor_active_graph(loop->graph_editor)->nodes) == 5;
        printf("ui-weapon-document %s: authored spawning graph opens in the editor\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 297 || step == 301 || step == 308 || step == 315 ||
        step == 319 || step == 326 || step == 334 || step == 338 ||
        step == 345 || step == 352 || step == 356 || step == 363) {
        app_runtime_state state = app_scene_authoring_runtime_state(loop->authoring);
        app_editor_document_context document = app_scene_authoring_document(loop->authoring);
        bool preview = step >= 334 && step < 363;
        bool playing = step == 297 || step == 315 || step == 334 || step == 352;
        bool ok = state.target == (preview ? APP_RUNTIME_PREVIEW : APP_RUNTIME_SCENE) &&
                  state.playing == playing &&
                  document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR &&
                  strcmp(document.id, "WeaponBehavior") == 0;
        if (step == 315) {
            ok &= loop->restart_projectile.slot != 0 &&
                  !ecs_entity_is_alive(loop->world, loop->restart_projectile);
        }
        printf(
            "ui-viewport-transport %s: step %d controls %s while graph "
            "inspection stays active\n",
            ok ? "PASS" : "FAIL",
            step,
            preview ? "isolated preview" : "scene"
        );
        if (!ok) {
            printf(
                "  actual: target=%d playing=%d document=%d id=%s "
                "projectiles=%d old_projectile_alive=%d\n",
                state.target,
                state.playing,
                document.kind,
                document.id ? document.id : "(none)",
                runtime_projectiles(loop->world),
                ecs_entity_is_alive(loop->world, loop->restart_projectile)
            );
        }
        loop->integration_failed |= !ok;
    }
    if (step == 365) {
        const char *capture = getenv("CBLUEPRINT_UI_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 374) {
        app_editor_document_context document = app_scene_authoring_document(loop->authoring);
        const graph *active = editor_active_graph(loop->graph_editor);
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        bool contact = false, particles = false;
        for (int i = 0; i < acount(active->nodes); i++) {
            node *current = active->nodes[i];
            particles |= strcmp(current->desc->name, "Emit Particles") == 0;
            contact |= current->reference_name &&
                       strcmp(current->reference_name, "Contact Begin") == 0 &&
                       strcmp(current->desc->name, "Event") == 0;
        }
        bool ok = document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR && document.id &&
                  strcmp(document.id, "ProjectileHitBehavior") == 0 &&
                  contact && particles && query.graph_visible &&
                  query.zoom > 0.0f && !query.camera_moving &&
                  query.preview_error && !query.preview_error[0];
        printf("ui-contact-document %s: authored contact graph opens and its camera settles\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        const char *capture = getenv("CBLUEPRINT_UI_COMBAT_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 384) {
        app_editor_document_context document = app_scene_authoring_document(loop->authoring);
        const graph *active = editor_active_graph(loop->graph_editor);
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        bool tick = false, nearest = false, move_toward = false;
        for (int i = 0; i < acount(active->nodes); i++) {
            node *current = active->nodes[i];
            tick |= current->reference_name &&
                    strcmp(current->reference_name, "Tick") == 0 &&
                    strcmp(current->desc->name, "Event") == 0;
            nearest |= strcmp(current->desc->name, "Find Nearest Entity") == 0;
            if (current->desc->pure_kind == NODE_PURE_FUNCTION) {
                move_toward = true;
                loop->pursuit_call = current->id;
                loop->pursuit_node_x = current->x;
                loop->pursuit_node_y = current->y;
                editor_node_ui drawn;
                move_toward &= editor_query_node_ui(loop->graph_editor, current->id, &drawn);
                loop->pursuit_click_x = drawn.title_x;
                loop->pursuit_click_y = drawn.title_y;
            }
        }
        bool ok = document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR &&
                  document.id && strcmp(document.id, "EnemyBehavior") == 0 &&
                  tick && nearest && move_toward && query.graph_visible &&
                  query.zoom >= 0.85f && !query.camera_moving &&
                  query.preview_error && !query.preview_error[0];
        printf("ui-pursuit-document %s: pursuit graph opens arranged without preview errors\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        loop->pursuit_camera = query;
        const char *capture = getenv("CBLUEPRINT_UI_PURSUIT_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 392) {
        const graph *active = editor_active_graph(loop->graph_editor);
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        bool normalize = false;
        for (int i = 0; i < acount(active->nodes); i++) {
            normalize |= strcmp(active->nodes[i]->desc->name, "Normalize Vector 2") == 0;
        }
        bool ok = active->kind == GRAPH_FUNCTION && active->name &&
                  strcmp(active->name, "Move Toward") == 0 && normalize &&
                  query.can_go_back && !query.camera_moving &&
                  !query.preview_error[0];
        printf("ui-function-open %s: double click opens the authored calculation\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        const char *capture = getenv("CBLUEPRINT_UI_FUNCTION_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 396) {
        const graph *active = editor_active_graph(loop->graph_editor);
        node *call = graph_node_by_id(active, loop->pursuit_call);
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        editor_node_ui drawn;
        bool ok = active->graph_id == 0 && call && !query.can_go_back &&
                  editor_query_node_ui(loop->graph_editor, call->id, &drawn) &&
                  drawn.selected &&
                  fabsf(query.zoom - loop->pursuit_camera.zoom) < 0.001f &&
                  fabsf(query.pan_x - loop->pursuit_camera.pan_x) < 0.01f &&
                  fabsf(query.pan_y - loop->pursuit_camera.pan_y) < 0.01f &&
                  call->x == loop->pursuit_node_x && call->y == loop->pursuit_node_y;
        printf("ui-function-back %s: Back restores the caller camera, selection and layout\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 414) {
        app_editor_document_context document = app_scene_authoring_document(loop->authoring);
        const graph *active = editor_active_graph(loop->graph_editor);
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        const behavior_asset *asset = editor_behavior_asset(loop->graph_editor);
        const graph *direction = behavior_asset_find_graph_by_name(asset, "Direction To");
        const graph *burst = behavior_asset_find_graph_by_name(asset, "Fire Ready Projectiles");
        bool tick = false, nearest = false, normalize = false, fire = false;
        bool all_pins_visible = true;
        for (int i = 0; i < acount(active->nodes); i++) {
            node *current = active->nodes[i];
            editor_node_ui drawn;
            all_pins_visible &= editor_query_node_ui(loop->graph_editor, current->id, &drawn) &&
                                drawn.exec_pins + drawn.data_pins ==
                                    current->input_count + current->output_count;
            tick |= current->reference_name &&
                    strcmp(current->reference_name, "Tick") == 0 &&
                    strcmp(current->desc->name, "Event") == 0;
            nearest |= strcmp(current->desc->name, "Find Nearest Entity") == 0;
            normalize |= direction && current->desc->pure_kind == NODE_PURE_FUNCTION &&
                         current->reference_id == direction->graph_id;
            fire |= burst && current->reference_id == burst->graph_id;
        }
        bool ok = document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR && document.id &&
                  strcmp(document.id, "AutoWeaponBehavior") == 0 && tick &&
                  nearest && normalize && fire && acount(active->nodes) == 8 &&
                  all_pins_visible &&
                  editor_graph_wires_visible(loop->graph_editor) &&
                  query.graph_visible && query.zoom > 0.0f && !query.camera_moving &&
                  query.preview_error && !query.preview_error[0];
        printf("ui-auto-weapon-document %s: automatic weapon graph fits every pin without preview errors\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        const char *capture = getenv("CBLUEPRINT_UI_AUTO_WEAPON_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 428) {
        const graph *active = editor_active_graph(loop->graph_editor);
        const char *names[] = { "Subtract", "Normalize Vector 2" };
        for (int which = 0; which < 2; which++) {
            for (int i = 0; i < acount(active->nodes); i++) {
                node *n = active->nodes[i];
                if (strcmp(n->desc->name, names[which]) == 0) {
                    editor_node_ui drawn;
                    loop->integration_failed |= !editor_query_node_ui(loop->graph_editor, n->id, &drawn);
                    loop->extract_ids[which] = n->id;
                    loop->extract_x[which] = drawn.title_x;
                    loop->extract_y[which] = drawn.title_y;
                }
            }
            loop->integration_failed |= !loop->extract_ids[which];
        }
        loop->extract_before = behavior_asset_save_json(
            editor_behavior_asset(loop->graph_editor)
        );
    }
    if (step == 451 || step == 475) {
        const behavior_asset *asset = editor_behavior_asset(loop->graph_editor);
        const graph *active = editor_active_graph(loop->graph_editor);
        graph *function = behavior_asset_find_graph_by_name(asset, "Direction");
        bool call = false;
        for (int i = 0; i < acount(active->nodes); i++) {
            node *n = active->nodes[i];
            call |= function && n->desc->pure_kind == NODE_PURE_FUNCTION &&
                    n->reference_id == function->graph_id;
        }
        bool ok = function && acount(function->nodes) == 4 &&
                  acount(active->nodes) == 7 && call &&
                  !graph_node_by_id(active, loop->extract_ids[0]) &&
                  !graph_node_by_id(active, loop->extract_ids[1]);
        printf(
            "ui-extract %s: %s replaces selected calculation with a function "
            "call\n",
            ok ? "PASS" : "FAIL",
            step == 451 ? "Extract" : "Redo"
        );
        loop->integration_failed |= !ok;
        if (step == 475) {
            const char *capture = getenv("CBLUEPRINT_UI_EXTRACT_SCREENSHOT");
            if (capture) {
                loop->integration_failed |= app_save_screenshot(capture);
            }
        }
    }
    if (step == 463) {
        char *current = behavior_asset_save_json(editor_behavior_asset(loop->graph_editor));
        bool ok = current && loop->extract_before &&
                  strcmp(current, loop->extract_before) == 0;
        printf("ui-extract-undo %s: Undo restores the complete original asset\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        free(current);
    }
    if (step == 484) {
        const graph *g = editor_active_graph(loop->graph_editor);
        for (int i = 0; i < acount(g->nodes); i++) {
            if (strcmp(g->nodes[i]->desc->name, "Get_GameTransform") == 0) {
                loop->field_node = g->nodes[i]->id;
                loop->integration_failed |= g->nodes[i]->output_count != 1;
            }
        }
        loop->integration_failed |= !loop->field_node;
    }
    if (step == 493 || step == 503 || step == 511) {
        const graph *g = editor_active_graph(loop->graph_editor);
        node *n = graph_node_by_id(g, loop->field_node);
        int expected = step == 503 ? 2 : 3;
        bool ok = n && n->output_count == expected && !n->output_pins[0].field_source;
        int x = -1;
        for (int p = 0; n && p < n->output_count; p++) {
            if (n->output_pins[p].field_name &&
                strcmp(n->output_pins[p].field_name, "x") == 0) {
                x = p;
            }
        }
        ok &= x >= 0;
        if (ok && step == 493) {
            loop->field_x_pin = n->output_pins[x].id;
            node *target = NULL;
            for (int i = 0; i < acount(g->nodes); i++) {
                if (strcmp(g->nodes[i]->desc->name, "Find Nearest Entity") == 0) {
                    target = g->nodes[i];
                }
            }
            ok &= target &&
                  editor_command_apply(
                      loop->graph_editor,
                      (editor_command){ .kind = EDITOR_COMMAND_CONNECT_DATA,
                                        .data.connect = { n->id, x, target->id, 1 } }
                  ) == 0;
        } else if (ok) {
            ok &= n->output_pins[x].id == loop->field_x_pin;
            bool wired = false;
            for (int i = 0; i < acount(g->edges); i++) {
                wired |= g->edges[i].from_pin_id == loop->field_x_pin;
            }
            ok &= wired;
        }
        if (ok && step == 511) {
            node *self = NULL;
            for (int i = 0; i < acount(g->nodes); i++) {
                if (g->nodes[i]->desc->pure_kind == NODE_PURE_SELF) {
                    self = g->nodes[i];
                }
            }
            editor_node_ui source_ui, self_ui;
            ok &= self &&
                  editor_query_node_ui(loop->graph_editor, n->id, &source_ui) &&
                  editor_query_node_ui(loop->graph_editor, self->id, &self_ui);
            if (ok) {
                ok &= source_ui.bottom < self_ui.top;
            }
        }
        printf(
            "ui-fields %s: %s retains whole output and connected field IDs\n",
            ok ? "PASS" : "FAIL",
            step == 493   ? "Expand"
            : step == 503 ? "Collapse"
                          : "Re-expand"
        );
        loop->integration_failed |= !ok;
        if (step == 511) {
            const char *capture = getenv("CBLUEPRINT_UI_FIELDS_SCREENSHOT");
            if (capture) {
                loop->integration_failed |= app_save_screenshot(capture);
            }
        }
    }
    if (step == 539 || step == 558) {
        const graph *active = editor_active_graph(loop->graph_editor);
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        bool visible = true;
        for (int i = 0; i < acount(active->nodes); i++) {
            editor_node_ui drawn;
            const node *n = active->nodes[i];
            visible &= editor_query_node_ui(loop->graph_editor, n->id, &drawn) &&
                       drawn.exec_pins + drawn.data_pins ==
                           n->input_count + n->output_count;
        }
        bool ok = visible && !query.camera_moving &&
                  editor_graph_wires_visible(loop->graph_editor) &&
                  query.preview_error && !query.preview_error[0];
        printf(
            "ui-weapon-function %s: %s fits all pins and wires at %.0f%%\n",
            ok ? "PASS" : "FAIL",
            step == 539 ? "burst" : "projectile",
            query.zoom * 100.0f
        );
        loop->integration_failed |= !ok;
        const char *capture = getenv(
            step == 539 ? "CBLUEPRINT_UI_BURST_SCREENSHOT" : "CBLUEPRINT_UI_SPAWN_SCREENSHOT"
        );
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 587) {
        const graph *active = editor_active_graph(loop->graph_editor);
        const node *entry = graph_node_by_id(active, active->entry_id);
        int origin_fields = 0, direction_fields = 0;
        for (int i = 0; entry && i < entry->output_count; i++) {
            origin_fields += entry->output_pins[i].field_source ==
                             entry->output_pins[1].id;
            direction_fields += entry->output_pins[i].field_source ==
                                entry->output_pins[2].id;
        }
        bool ok = origin_fields == 2 && direction_fields == 2;
        printf("ui-function-fields %s: choose Origin fields without changing Direction fields\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 606) {
        editor_query_state(loop->graph_editor, &loop->pursuit_camera);
        const node *call = graph_node_by_id(editor_active_graph(loop->graph_editor), 197);
        loop->pursuit_node_x = call->x;
        loop->pursuit_node_y = call->y;
    }
    if (step == 630) {
        bool ok = editor_active_graph(loop->graph_editor)->graph_id == 173 &&
                  editor_graph_history_count(loop->graph_editor) == 2;
        printf("ui-navigation %s: visible call controls open nested functions\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        const char *capture = getenv("CBLUEPRINT_UI_NAVIGATION_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 644) {
        editor_query query;
        editor_query_state(loop->graph_editor, &query);
        const node *call = graph_node_by_id(editor_active_graph(loop->graph_editor), 197);
        editor_node_ui drawn;
        bool ok = query.active_graph->graph_id == 0 && call &&
                  editor_query_node_ui(loop->graph_editor, call->id, &drawn) &&
                  drawn.selected &&
                  fabsf(query.zoom - loop->pursuit_camera.zoom) < 0.001f &&
                  fabsf(query.pan_x - loop->pursuit_camera.pan_x) < 0.01f &&
                  fabsf(query.pan_y - loop->pursuit_camera.pan_y) < 0.01f &&
                  call->x == loop->pursuit_node_x && call->y == loop->pursuit_node_y;
        printf("ui-navigation %s: ancestor breadcrumb restores root camera and call selection\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 668) {
        bool ok = editor_active_graph(loop->graph_editor)->graph_id == 173;
        printf("ui-navigation %s: graph picker opens a local function\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 688) {
        bool ok = editor_active_graph(loop->graph_editor)->graph_id == 0;
        printf("ui-navigation %s: graph picker returns to Events\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 718 || step == 740) {
        const node *query = graph_node_by_id(editor_active_graph(loop->graph_editor), 11);
        unsigned expected = step == 718 ? 3 : 2;
        bool ok = query && query->input_count > 2 &&
                  query->input_pins[2].default_value.u == expected;
        printf(
            "ui-flags %s: %s query categories\n",
            ok ? "PASS" : "FAIL",
            step == 718 ? "toggle named" : "Undo restores"
        );
        loop->integration_failed |= !ok;
        if (step == 740) {
            const char *capture = getenv("CBLUEPRINT_UI_FLAGS_SCREENSHOT");
            if (capture) {
                loop->integration_failed |= app_save_screenshot(capture);
            }
        }
    }
    if (step == 768 || step == 780 || step == 790) {
        float x, y;
        const char *item = step == 768 ? "navigator.document.outline"
                         : step == 780 ? "scene.actor.Player_1"
                                       : "content.search";
        editor_ui_rect navigator, graph_rect, scene_rect;
        bool ok = editor_ui_item_center(item, &x, &y) &&
                  editor_ui_pane_rect(
                      step == 768   ? EDITOR_UI_PANE_MY_BEHAVIOR_ASSET
                      : step == 780 ? EDITOR_UI_PANE_WORLD_OUTLINER
                                    : EDITOR_UI_PANE_CONTENT,
                      &navigator
                  ) &&
                  editor_ui_pane_rect(EDITOR_UI_PANE_NODE_CANVAS, &graph_rect) &&
                  editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &scene_rect) &&
                  navigator.height > graph_rect.height &&
                  navigator.y + navigator.height >=
                      scene_rect.y + scene_rect.height - 3 &&
                  editor_active_graph(loop->graph_editor)->graph_id == 0;
        printf("ui-navigator %s: %s uses one full-height navigator and preserves the graph\n", ok ? "PASS" : "FAIL", item);
        loop->integration_failed |= !ok;
        if (step == 768) {
            editor_ui_rect files, scene_tab, document;
            bool tabs = editor_ui_item_rect("navigator.files", &files) &&
                        editor_ui_item_rect("navigator.scene", &scene_tab) &&
                        editor_ui_item_rect("navigator.document", &document);
            tabs = tabs && fabsf(files.y - scene_tab.y) < 1 &&
                   fabsf(files.y - document.y) < 1 &&
                   files.x + files.width <= scene_tab.x + 1 &&
                   scene_tab.x + scene_tab.width <= document.x + 1;
            printf("ui-navigator %s: Files, Scene and Document are directly accessible in one row\n", tabs ? "PASS" : "FAIL");
            loop->integration_failed |= !tabs;
            const char *capture = getenv("CBLUEPRINT_UI_NAVIGATOR_SCREENSHOT");
            if (capture) {
                loop->integration_failed |= app_save_screenshot(capture);
            }
        }
    }
    if (step == 828 || step == 838 || step == 848 || step == 858) {
        editor_ui_rect inspector;
        bool expected_drawn = step != 848;
        bool ok = editor_ui_pane_rect(EDITOR_UI_PANE_INSPECTOR, &inspector) ==
                      expected_drawn &&
                  editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR) == (step != 848);
        printf("ui-context-inspector %s: selection availability respects visibility preference at %d\n", ok ? "PASS" : "FAIL", step);
        loop->integration_failed |= !ok;
    }
    if (step == 890) {
        const char *capture = getenv("CBLUEPRINT_UI_SETTINGS_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 900 || step == 920) {
        const char *expected = step == 900 ? "Automatic Weapon" : "AutoWeaponBehavior";
        bool ok = strcmp(editor_behavior_asset(loop->graph_editor)->name, expected) == 0;
        printf(
            "ui-settings %s: %s behavior name through popup (actual=%s)\n",
            ok ? "PASS" : "FAIL",
            step == 900 ? "edit" : "Undo restores",
            editor_behavior_asset(loop->graph_editor)->name
        );
        loop->integration_failed |= !ok;
    }
    if (step == 960 || step == 972 || step == 984 || step == 1004) {
        const node *burst = graph_node_by_id(editor_active_graph(loop->graph_editor), 35);
        unsigned expected = step == 1004 ? 0xF0CD69FFu : 0x3366CC80u;
        float x, y;
        bool picker = editor_ui_item_center("color.hex", &x, &y);
        bool ok = burst && burst->input_pins[2].default_value.u == expected &&
                  picker == (step == 960 || step == 984);
        printf("ui-color %s: edit, close, reopen and Undo preserve RGBA at %d\n", ok ? "PASS" : "FAIL", step);
        loop->integration_failed |= !ok;
        if (step == 960) {
            const char *capture = getenv("CBLUEPRINT_UI_COLOR_SCREENSHOT");
            if (capture) {
                loop->integration_failed |= app_save_screenshot(capture);
            }
        }
    }
    if (step == 1026) {
        float x, y;
        bool ok = editor_ui_item_center("inspector.input.35.1", &x, &y) &&
                  editor_ui_item_center("inspector.connection.35.1", &x, &y) &&
                  editor_ui_item_center("inspector.input.35.2", &x, &y) &&
                  editor_ui_item_center("color.details.35.0.2", &x, &y) &&
                  !editor_ui_item_center("inspector.input.35.0", &x, &y) &&
                  editor_ui_item_center("inspector.advanced", &x, &y);
        editor_ui_rect previous = { 0 };
        editor_node_ui drawn;
        bool rows = editor_query_node_ui(loop->graph_editor, 35, &drawn);
        for (int pin = 3; pin <= 5; pin++) {
            char id[64];
            snprintf(id, sizeof id, "graph.input.35.%d", pin);
            editor_ui_rect widget;
            bool present = editor_ui_item_rect(id, &widget);
            rows &= present;
            if (present) {
                rows &= widget.y >= drawn.top - 1 &&
                        widget.y + widget.height <= drawn.bottom + 1;
                if (pin > 3) {
                    rows &= previous.y + previous.height <= widget.y + 1;
                }
                previous = widget;
            }
        }
        ok &= rows;
        printf("ui-properties %s: connected source, editable color, and no execution property\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        const char *capture = getenv("CBLUEPRINT_UI_PROPERTIES_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 1080 || step == 1140) {
        editor_query_state(loop->graph_editor, &loop->pursuit_camera);
    }
    if (step == 1090 || step == 1180) {
        editor_query state;
        editor_query_state(loop->graph_editor, &state);
        bool ok = fabsf(state.zoom - loop->pursuit_camera.zoom) < 0.0001f &&
                  fabsf(state.pan_x - loop->pursuit_camera.pan_x) < 0.01f &&
                  fabsf(state.pan_y - loop->pursuit_camera.pan_y) < 0.01f;
        printf(
            "ui-responsive-fit %s: %s\n",
            ok ? "PASS" : "FAIL",
            step == 1090
                ? "selection preserves camera during double-click interval"
                : "manual zoom survives Inspector resize"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 1118 || step == 1250) {
        const graph *g = editor_active_graph(loop->graph_editor);
        editor_query state;
        editor_query_state(loop->graph_editor, &state);
        bool ok = !state.camera_moving &&
                  editor_graph_wires_visible(loop->graph_editor);
        for (int i = 0; i < acount(g->nodes); i++) {
            editor_node_ui drawn;
            ok &= editor_query_node_ui(loop->graph_editor, g->nodes[i]->id, &drawn) &&
                  drawn.exec_pins + drawn.data_pins ==
                      g->nodes[i]->input_count + g->nodes[i]->output_count;
        }
        printf(
            "ui-responsive-fit %s: %s keeps full graph visible\n",
            ok ? "PASS" : "FAIL",
            step == 1118 ? "settled Inspector resize" : "reduced motion"
        );
        loop->integration_failed |= !ok;
        if (step == 1118) {
            const char *capture = getenv("CBLUEPRINT_UI_RESPONSIVE_SCREENSHOT");
            if (capture) {
                loop->integration_failed |= app_save_screenshot(capture);
            }
        }
    }
    if (step == 1324 || step == 1348) {
        ecs_component_id required = GAME_COMPONENT_GameWeapon.id;
        ecs_query entities;
        bool ok = false;
        if (ecs_query_begin(loop->world, &entities, &required, 1) == ECS_OK) {
            ecs_entity entity;
            if (ecs_query_next(&entities, &entity)) {
                const GameMaterial *material = GAME_GameMaterial_get_const(loop->world, entity);
                ok = material && material->scene ==
                                     (step == 1324 ? 0x3366CC80u : 0xFFFFFFFFu);
            }
            ecs_query_end(&entities);
        }
        printf(
            "ui-appearance %s: prototype color %s updates scene instance\n",
            ok ? "PASS" : "FAIL",
            step == 1324 ? "edit" : "Undo"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 1380) {
        editor_ui_rect scene;
        bool ok = editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &scene) &&
                  scene.y <= loop->graph_before_focus.y + 3.0f &&
                  scene.height >= loop->scene_before_expand.y +
                                      loop->scene_before_expand.height -
                                      loop->graph_before_focus.y - 3.0f;
        printf("ui-scene-expand %s: hidden graph releases document space to Scene\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
        const char *capture = getenv("CBLUEPRINT_UI_APPEARANCE_SCREENSHOT");
        if (capture) {
            loop->integration_failed |= app_save_screenshot(capture);
        }
    }
    if (step == 1440) {
        editor_ui_rect scene;
        bool ok = ui_graph_scene_are_stacked() &&
                  editor_ui_pane_rect(EDITOR_UI_PANE_SCENE, &scene) &&
                  fabsf(scene.height - loop->scene_before_expand.height) < 3.0f;
        printf("ui-scene-expand %s: showing graph restores the original split\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 1468 || step == 1492) {
        ecs_component_id required = GAME_COMPONENT_GameWeapon.id;
        ecs_query entities;
        bool ok = false;
        if (ecs_query_begin(loop->world, &entities, &required, 1) == ECS_OK) {
            ecs_entity entity;
            if (ecs_query_next(&entities, &entity)) {
                const GameAppearance *appearance = GAME_GameAppearance_get_const(loop->world, entity);
                ok = appearance && appearance->shape ==
                                       (step == 1468 ? GAME_SHAPE_CIRCLE
                                                     : GAME_SHAPE_STROKED_CHEVRON);
            }
            ecs_query_end(&entities);
        }
        printf(
            "ui-shape %s: named shape %s updates scene instance\n",
            ok ? "PASS" : "FAIL",
            step == 1468 ? "selection" : "Undo"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 1516) {
        engine_viewport_get_camera(loop->viewport, &loop->camera_before);
        const engine_viewport_camera *camera = &loop->camera_before;
        bool ok = camera->center_x - camera->span_width * 0.5f <= 75.5f &&
                  camera->center_x + camera->span_width * 0.5f >= 496 &&
                  camera->center_y - camera->span_height * 0.5f <= 164 &&
                  camera->center_y + camera->span_height * 0.5f >= 196;
        printf("ui-scene-camera %s: Fit includes all authored bodies\n", ok ? "PASS" : "FAIL");
        loop->integration_failed |= !ok;
    }
    if (step == 1540 || step == 1554 || step == 1580 || step == 1640) {
        engine_viewport_camera camera;
        engine_viewport_get_camera(loop->viewport, &camera);
        bool ok = false;
        if (step == 1540) {
            editor_ui_rect rect;
            editor_ui_item_rect("viewport.image", &rect);
            float border = ImGui_GetStyle()->ImageBorderSize;
            float u = (loop->zoom_pointer.x - rect.x - border) /
                          (rect.width - 2 * border) -
                      0.5f;
            float v = 0.5f - (loop->zoom_pointer.y - rect.y - border) /
                                 (rect.height - 2 * border);
            ok = camera.span_width < loop->camera_before.span_width &&
                 fabsf(
                     camera.center_x + u * camera.span_width - loop->zoom_anchor.x
                 ) < 0.01f &&
                 fabsf(
                     camera.center_y + v * camera.span_height -
                     loop->zoom_anchor.y
                 ) < 0.01f;
            loop->camera_before = camera;
        }
        if (step == 1554) {
            editor_ui_rect rect;
            editor_ui_item_rect("viewport.image", &rect);
            float image_width = rect.width - 2 * ImGui_GetStyle()->ImageBorderSize;
            float expected_x = loop->camera_before.center_x -
                               50 * loop->camera_before.span_width / image_width;
            ok = fabsf(camera.center_x - expected_x) < 0.01f &&
                 fabsf(camera.center_y - loop->camera_before.center_y) < 0.01f &&
                 fabsf(camera.span_width - loop->camera_before.span_width) < 0.01f;
        }
        if (step == 1580) {
            ok = fabsf(camera.center_x - 320) < 0.01f &&
                 fabsf(camera.center_y - 180) < 0.01f;
        }
        if (step == 1640) {
            ok = fabsf(camera.center_x - 86) < 0.01f &&
                 fabsf(camera.center_y - 180) < 0.01f;
            engine_viewport_get_view(loop->viewport, &loop->scene_view);
        }
        printf(
            "ui-scene-camera %s: %s\n",
            ok ? "PASS" : "FAIL",
            step == 1540   ? "wheel holds cursor anchor"
            : step == 1554 ? "middle pan changes camera only"
            : step == 1580 ? "Reset restores default view"
                           : "double-click frames selected actor"
        );
        loop->integration_failed |= !ok;
    }
    if (step == 1660 || step == 1685 || step == 1700 || step == 1720 || step == 1735) {
        engine_viewport_view view;
        engine_viewport_get_view(loop->viewport, &view);
        engine_viewport_view expected = step == 1720
                                          ? (engine_viewport_view){ 500, 300, 240, 120 }
                                          : loop->scene_view;
        bool first_preview = step == 1660 || step == 1700;
        bool ok = first_preview
                    ? view.width > 0 && view.width < 100 && view.height > 0 &&
                          view.height < 100
                    : fabsf(view.center_x - expected.center_x) < 0.01f &&
                          fabsf(view.center_y - expected.center_y) < 0.01f &&
                          fabsf(view.width - expected.width) < 0.01f &&
                          fabsf(view.height - expected.height) < 0.01f;
        printf(
            "ui-scene-camera %s: %s at %d\n",
            ok ? "PASS" : "FAIL",
            first_preview ? "new prototype frames its own content"
                          : "target restores its logical camera",
            step
        );
        loop->integration_failed |= !ok;
    }
    scene_edit_check(loop, step);
    scene_numeric_check(loop, step);
    return ++loop->frame >= loop->frames;
}

static bool asset_preview_without_instance()
{
    behavior_asset *asset = behavior_asset_new("PreviewWithoutInstance");
    behavior_asset_set_owner_type(asset, "ecs.entity");
    graph *g = asset->graph;
    node *self = behavior_asset_add_self_node(asset);
    node *reroute = graph_add_node(g, registry_node_by_name("Reroute Data"));
    bool connected = graph_connect_data(g, self, 0, reroute, 0) == 0;
    node *literal = graph_add_node(g, registry_node_by_name("Integer"));
    pin_value integer = { .type = PIN_I32, .i = 42 };
    bool authored = graph_set_pin_default(g, literal, 0, 0, &integer) == 0;
    eval_view *view = eval_view_new();
    eval_view_refresh(view, g);
    bool ok = connected && authored && !eval_view_error(view)[0] &&
              !eval_view_output(view, self, 0)[0] &&
              !eval_view_output(view, reroute, 0)[0] &&
              strcmp(eval_view_output(view, literal, 0), "42") == 0;
    printf("ui-asset-preview %s: missing instance hides owner values and preserves literal previews\n", ok ? "PASS" : "FAIL");
    if (!ok) {
        printf(
            "preview details: connected=%d authored=%d error=%s self=%s "
            "reroute=%s literal=%s\n",
            connected,
            authored,
            eval_view_error(view),
            eval_view_output(view, self, 0),
            eval_view_output(view, reroute, 0),
            eval_view_output(view, literal, 0)
        );
    }
    eval_view_free(view);
    behavior_asset_free(asset);
    return ok;
}

int ui_integration_run(const char *program)
{
    cf_set_assert_handler(stacktrace_assert_handler);
    const int frames = 3112;
    CF_Result result = cf_make_app(
        "c-blueprints",
        0,
        0,
        0,
        APP_WINDOW_WIDTH,
        APP_WINDOW_HEIGHT,
        CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_RESIZABLE_BIT |
            CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "cf_make_app failed: %s\n", result.details ? result.details : "unknown error");
        return 1;
    }
    app_scene_renderer_initialize();
    bool border_ok = test_interior_border() == 0;
    cf_app_init_imgui();
    editor_ui_initialize(false);
    bool preferences_ok = workspace_preferences_roundtrip();
    if (getenv("CBLUEPRINT_UI_OVERLAY_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_ENCOUNTER_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_ORIENTATION_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_SCENE_EDIT_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_APPEARANCE_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_RESPONSIVE_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_LIVE_GRAPH_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_PLAY_HINT_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_MATERIAL_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_COMPACT_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_ATTACHMENTS_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_SCENE_SEARCH_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_PROPERTIES_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_COLOR_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_SETTINGS_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_NAVIGATOR_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_FLAGS_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_NAVIGATION_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_BURST_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_SPAWN_SCREENSHOT") || getenv("CBLUEPRINT_UI_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_COMBAT_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_FIELDS_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_PURSUIT_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_AUTO_WEAPON_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_FUNCTION_SCREENSHOT") ||
        getenv("CBLUEPRINT_UI_EXTRACT_SCREENSHOT")) {
        cf_app_set_imgui_render_target(cf_app_get_canvas());
    }
    ImGuiContextHook clock = { .Type = ImGuiContextHookType_NewFramePre,
                               .Callback = fixed_ui_time };
    ImGui_AddContextHook(ImGui_GetCurrentContext(), &clock);
    ImGui_GetIO()->IniFilename = NULL;
    editor_ui_set_framebuffer_target(true);
    editor_ui_set_framebuffer_size(APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT);
    registry_validate();
    bool asset_preview_ok = asset_preview_without_instance();
    behavior_asset *editor_definition = behavior_asset_new("AppBehavior");
    ecs_world *world = ecs_world_new();
    engine_viewport *viewport = engine_viewport_new(640, 360);
    editor *graph_editor = editor_definition ? editor_new(editor_definition) : NULL;
    game_live_world *live = world ? game_live_world_new(world) : NULL;
    char *definition_json = editor_definition
                              ? behavior_asset_save_json(editor_definition)
                              : NULL;
    behavior_asset *definition_behavior_asset = definition_json
                                                  ? behavior_asset_load_json(definition_json)
                                                  : NULL;
    free(definition_json);
    game_live_definition *definition = definition_behavior_asset
                                         ? game_live_definition_new("AppBehavior", definition_behavior_asset)
                                         : NULL;
    engine *runtime = world ? engine_new(world, live) : NULL;
    app_scene_authoring *authoring = NULL;
    int components_ok = world != NULL;
    for (int i = 0; components_ok && i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        components_ok = ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) ==
                        ECS_OK;
    }
    if (!world || !components_ok || !viewport || !graph_editor || !live ||
        !definition || !runtime || game_live_add_definition(live, definition) != 0) {
        app_scene_authoring_free(authoring);
        engine_free(runtime);
        game_live_definition_free(definition);
        game_live_world_free(live);
        editor_free(graph_editor);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    authoring = app_scene_authoring_new(
        world,
        live,
        definition,
        editor_behavior_asset(graph_editor),
        graph_editor,
        NULL,
        NULL,
        runtime
    );
    bool coverage_ok = authoring &&
                       test_sdf_coverage(app_scene_authoring_shapes(authoring)) == 0;
    bool radiance_ok = authoring && test_radiance() == 0;
    bool scene_ready = authoring &&
                       app_scene_authoring_load_content(authoring) == 0 &&
                       app_scene_authoring_load(authoring, "test/fixtures/editor_scene.json") ==
                           0;
    if (!scene_ready || app_scene_authoring_apply_pending(authoring) != 0) {
        fprintf(
            stderr,
            "Could not initialize scene: %s\n",
            authoring ? app_scene_authoring_message(authoring) : "authoring unavailable"
        );
        app_scene_authoring_free(authoring);
        engine_free(runtime);
        game_live_world_free(live);
        editor_free(graph_editor);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    editor_set_event_catalog(
        graph_editor,
        APP_EVENT_CATALOG,
        (int)(sizeof APP_EVENT_CATALOG / sizeof *APP_EVENT_CATALOG)
    );
    editor_set_ui_context(graph_editor, EDITOR_UI_CONTEXT_BEHAVIOR);
    engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT);
    editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
    engine_viewport_set_render_callback(viewport, app_scene_authoring_render, authoring);

    bool viewport_resize_ok = viewport_pending_resize_check();
    app_loop loop = {
        .graph_editor = graph_editor,
        .runtime = runtime,
        .world = world,
        .live = live,
        .viewport = viewport,
        .authoring = authoring,
        .frames = frames,
        .integration_failed = !preferences_ok || !asset_preview_ok || !border_ok ||
                              !coverage_ok || !radiance_ok || !viewport_resize_ok,
    };
    while (!app_frame(&loop)) {
    }
    if (loop.frame < frames) {
        loop.integration_failed = 1;
    }
    app_cleanup(&loop);
    return loop.integration_failed;
}
