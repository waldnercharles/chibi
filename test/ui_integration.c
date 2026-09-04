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
// pi-lens-ignore: fatal error
#include <cute_defines.h>
#define CF_MATH_H
#include <imgui/dcimgui.h>
#undef CF_MATH_H

#define APP_WINDOW_WIDTH 1920
#define APP_WINDOW_HEIGHT 1080
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
} app_loop;

static int app_integration_check(app_loop *loop)
{
    app_editor_document_context document = app_scene_authoring_document(loop->authoring);
    editor_query query;
    editor_query_state(loop->graph_editor, &query);
    int nodes = query.active_graph ? acount(query.active_graph->nodes) : 0;
    const char *selected = app_scene_authoring_selected_content(loop->authoring);
    int expected_node = 0;
    for (int i = 0; i < nodes; i++) {
        const node *current = query.active_graph->nodes[i];
        if (current->desc && current->desc->name &&
            strcmp(current->desc->name, "Event") == 0 && current->reference_name &&
            strcmp(current->reference_name, "Input") == 0) {
            expected_node = 1;
            break;
        }
    }
    int ok = editor_ui_content_drawer_open() &&
             document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR && document.id &&
             strcmp(document.id, "InputBehavior") == 0 && query.graph_visible &&
             expected_node && app_scene_authoring_message(loop->authoring) &&
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
}

static int app_frame(app_loop *loop)
{
    if (!cf_app_is_running()) {
        return 1;
    }
    ImGui_GetIO()->DeltaTime = 1.0f / 60.0f;
    {
        float card_x = 0.0f;
        float card_y = 0.0f;
        const char *path = loop->frame <= 2
                             ? "content/behaviors"
                             : "content/behaviors/InputBehavior.json";
        int click_frame = loop->frame <= 2 ? loop->frame : loop->frame - 3;
        if (click_frame >= 1 && click_frame <= 4 &&
            !editor_ui_asset_center(path, &card_x, &card_y)) {
            fprintf(stderr, "ui-integration: card is not visible: %s\n", path);
            loop->integration_failed = 1;
            return 1;
        }
        if (click_frame == 1 || click_frame == 3) {
            printf("ui-input frame=%d press %s\n", loop->frame, path);
            inject_mouse_button(card_x, card_y, true);
        } else if (click_frame == 2 || click_frame == 4) {
            printf("ui-input frame=%d release %s\n", loop->frame, path);
            inject_mouse_button(card_x, card_y, false);
        }
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

    if (loop->frame >= 10 && app_integration_check(loop) != 0) {
        loop->integration_failed = 1;
    }

    return ++loop->frame >= loop->frames;
}

int ui_integration_run(const char *program)
{
    cf_set_assert_handler(stacktrace_assert_handler);
    const int frames = 12;
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
