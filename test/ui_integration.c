#include "../app/cute_host.h"
#include "../app/scene_authoring.h"
#include "../app/stacktrace.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../editor/editor.h"
#include "../editor/ui_shell.h"
#include "../engine/engine.h"
#include "../game/generated.h"
#include "../game/live.h"
#include "../game/prototype.h"

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
static const editor_event_param APP_TICK_PARAMS[] = {
    { "DeltaSeconds", PIN_F32, "float" },
};

static const editor_event_signature APP_EVENT_CATALOG[] = {
    { "Tick", APP_TICK_PARAMS, 1 },
};

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
} app_loop;

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
             expected_node && pure_reads == 2 &&
             app_scene_authoring_message(loop->authoring) &&
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
    return ok ? 0 : -1;
}

static void app_cleanup(app_loop *loop)
{
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

static int app_frame(app_loop *loop)
{
    if (!cf_app_is_running()) {
        return 1;
    }
    int step = loop->frame - 3;
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
                path = "content.toggle";
                click_frame = step < 80 ? step - 76 : step - 81;
                break;
        }
        if (click_frame >= 1 && click_frame <= 4 &&
            !editor_ui_item_center(path, &card_x, &card_y)) {
            fprintf(stderr, "ui-integration: item is not visible: %s\n", path);
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
    cf_app_update(NULL);
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
    engine_viewport_draw(loop->viewport);
    app_scene_authoring_draw(loop->authoring);
    editor_draw(loop->graph_editor);
    cf_app_draw_onto_screen(true);

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
        printf("ui-document %s: save, reload and undo are reachable\n", ok ? "PASS" : "FAIL");
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
            "ui-navigation %s: Content can be %s from the menu\n",
            ok ? "PASS" : "FAIL",
            expected ? "reopened" : "closed"
        );
        loop->integration_failed |= !ok;
    }

    return ++loop->frame >= loop->frames;
}

int ui_integration_run(const char *program)
{
    cf_set_assert_handler(stacktrace_assert_handler);
    const int frames = 89;
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
    cf_app_init_imgui();
    ImGuiContextHook clock = { .Type = ImGuiContextHookType_NewFramePre,
                               .Callback = fixed_ui_time };
    ImGui_AddContextHook(ImGui_GetCurrentContext(), &clock);
    ImGui_GetIO()->IniFilename = NULL;
    editor_ui_set_framebuffer_target(true);
    editor_ui_set_framebuffer_size(APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT);
    if (!cf_app_set_imgui_render_target(cf_app_get_canvas())) {
        fprintf(stderr, "could not set the UI render target\n");
        cf_destroy_app();
        return 1;
    }
    registry_validate();
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
    int components_ok = ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) ==
                            ECS_OK &&
                        ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL) ==
                            ECS_OK &&
                        ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) ==
                            ECS_OK &&
                        ecs_world_register_component(world, &GAME_COMPONENT_GameInput, NULL) ==
                            ECS_OK &&
                        ecs_world_register_component(world, &GAME_COMPONENT_GameWeapon, NULL) ==
                            ECS_OK &&
                        ecs_world_register_component(world, &GAME_COMPONENT_GameProjectile, NULL) ==
                            ECS_OK;
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
    if (authoring) {
        (void)app_scene_authoring_load_content(authoring);
        (void)app_scene_authoring_seed_default(authoring);
    }
    if (!authoring || app_scene_authoring_apply_pending(authoring) != 0) {
        app_scene_authoring_free(authoring);
        engine_free(runtime);
        game_live_world_free(live);
        editor_free(graph_editor);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    editor_ui_reset_layout();
    editor_set_event_catalog(
        graph_editor,
        APP_EVENT_CATALOG,
        (int)(sizeof APP_EVENT_CATALOG / sizeof *APP_EVENT_CATALOG)
    );
    editor_set_ui_context(graph_editor, EDITOR_UI_CONTEXT_BEHAVIOR);
    engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT);
    editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
    engine_viewport_set_render_callback(viewport, app_scene_authoring_render, authoring);

    app_loop loop = {
        .graph_editor = graph_editor,
        .runtime = runtime,
        .world = world,
        .live = live,
        .viewport = viewport,
        .authoring = authoring,
        .frames = frames,
    };
    while (!app_frame(&loop)) {
    }
    if (loop.frame < frames) {
        loop.integration_failed = 1;
    }
    app_cleanup(&loop);
    return loop.integration_failed;
}
