#include "../core/registry.h"
#include "../core/serialize.h"
#include "../editor/editor.h"
#include "../editor/ui_shell.h"
#include "../engine/engine.h"
#include "../game/generated.h"
#include "../game/live.h"
#include "../game/prototype.h"
#include "cute_host.h"
#include "scene_authoring.h"
#include "stacktrace.h"
#include "web_loop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_WINDOW_WIDTH 1920
#define APP_WINDOW_HEIGHT 1080
#define APP_DEFAULT_SCREENSHOT_PATH "/tmp/c-blueprints-screenshot.png"
#define APP_DEFAULT_SCREENSHOT_FRAMES 3
#define APP_INTEGRATION_ARTIFACTS "build/ui-integration-artifacts"

static void print_usage(const char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("\nOptions:\n");
    printf("  --help                 Show this help text\n");
    printf("  --headless             Run without showing a window\n");
    printf("  --frames <count>       Exit after count rendered frames\n");
    printf("  --fps <rate>           Set the target frame rate\n");
    printf("  --screenshot [path]    Capture a PNG after %d frames and exit\n", APP_DEFAULT_SCREENSHOT_FRAMES);
}

static int save_screenshot(const char *path)
{
    CF_Canvas canvas = cf_app_get_canvas();
    int width = cf_app_get_canvas_width();
    int height = cf_app_get_canvas_height();
    CF_Readback readback = cf_canvas_readback(canvas);
    if (!readback.id) {
        fprintf(stderr, "screenshot readback could not be started\n");
        return 1;
    }
    int size = cf_readback_size(readback);
    size_t expected = (size_t)width * (size_t)height * sizeof(CF_Pixel);
    if (size <= 0 || width <= 0 || height <= 0 || (size_t)size != expected) {
        fprintf(stderr, "screenshot readback has invalid dimensions\n");
        cf_destroy_readback(readback);
        return 1;
    }
    CF_Pixel *pixels = (CF_Pixel *)malloc((size_t)size);
    if (!pixels) {
        fprintf(stderr, "screenshot allocation failed\n");
        cf_destroy_readback(readback);
        return 1;
    }
    while (!cf_readback_ready(readback)) {
    }
    int copied = cf_readback_data(readback, pixels, size);
    int result = 1;
    if (copied == size) {
        CF_Image image = { width, height, pixels };
        void *png_data = NULL;
        int png_size = 0;
        if (!cf_is_error(cf_image_save_png_to_memory(&image, &png_data, &png_size)) &&
            png_data && png_size > 0) {
            FILE *file = fopen(path, "wb");
            if (file && fwrite(png_data, 1, (size_t)png_size, file) == (size_t)png_size) {
                result = 0;
            }
            if (file) {
                fclose(file);
            }
        }
        cf_free(png_data);
    }
    free(pixels);
    cf_destroy_readback(readback);
    if (result != 0) {
        fprintf(stderr, "could not save screenshot: %s\n", path);
        return 1;
    }
    printf("screenshot saved: %s (%dx%d)\n", path, width, height);
    return 0;
}

static const editor_event_param APP_TICK_PARAMS[] = {
    { "DeltaSeconds", PIN_F32, "float" },
};

static const editor_event_signature APP_EVENT_CATALOG[] = {
    { "Tick", APP_TICK_PARAMS, 1 },
};

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
    const char *screenshot_path;
    int capture_error;
    int integration;
    int integration_failed;
} app_loop;

static int app_integration_check(app_loop *loop)
{
    app_editor_document_context document = app_scene_authoring_document(loop->authoring);
    editor_query query;
    editor_query_state(loop->graph_editor, &query);
    int nodes = query.active_graph ? acount(query.active_graph->nodes) : 0;
    const node *first_node = nodes > 0 ? query.active_graph->nodes[0] : NULL;
    const char *selected = app_scene_authoring_selected_content(loop->authoring);
    int expected_node = first_node && first_node->desc && first_node->desc->name &&
                        strcmp(first_node->desc->name, "Event") == 0 &&
                        first_node->reference_name &&
                        strcmp(first_node->reference_name, "Input") == 0;
    int ok = editor_ui_content_drawer_open() && selected &&
             strcmp(selected, "content/behaviors/InputBehavior.json") == 0 &&
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
    if (loop->integration) {
        char path[512];
        snprintf(path, sizeof path, "%s/interaction-trace.txt", APP_INTEGRATION_ARTIFACTS);
        FILE *trace = fopen(path, "ab");
        if (trace) {
            fprintf(
                trace,
                "frame=%d %s selected=%s document=%s node_canvas=%d nodes=%d "
                "event_node=%d\n",
                loop->frame,
                result,
                selected ? selected : "(none)",
                document.id ? document.id : "(none)",
                query.graph_visible,
                nodes,
                expected_node
            );
            fclose(trace);
        }
    }
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
    cf_app_update(NULL);
    if (loop->integration && loop->frame == 1) {
        editor_ui_test_click("content/behaviors", false);
    } else if (loop->integration && loop->frame == 2) {
        editor_ui_test_click("content/behaviors/InputBehavior.json", true);
    }
    double seconds = loop->integration ? (double)(loop->frame + 1) / 60.0 : CF_SECONDS;
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

    if (loop->integration && loop->frame >= 3 && app_integration_check(loop) != 0) {
        loop->integration_failed = 1;
    }

    int finished = loop->frames > 0 && ++loop->frame >= loop->frames;
    if (finished && loop->screenshot_path) {
        loop->capture_error = save_screenshot(loop->screenshot_path);
        return 1;
    }
    return finished;
}

#ifdef __EMSCRIPTEN__
static app_loop APP_LOOP_STATE;
static app_loop *APP_LOOP;

static void app_web_frame(void)
{
    if (!app_frame(APP_LOOP)) {
        return;
    }
    app_cleanup(APP_LOOP);
    app_web_cancel_main_loop();
}
#endif

int main(int argc, char **argv)
{
    stacktrace_init(argv[0]);
    cf_set_assert_handler(stacktrace_assert_handler);
    int frames = 0;
    int fps = 0;
    int headless = 0;
    const char *screenshot_path = NULL;
    int integration = getenv("CBLUEPRINT_UI_INTEGRATION") != NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot") == 0) {
            screenshot_path = APP_DEFAULT_SCREENSHOT_PATH;
            headless = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                screenshot_path = argv[++i];
            }
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        }
    }
    if (integration) {
        headless = 1;
        frames = 5;
        static char integration_screenshot[256];
        snprintf(integration_screenshot, sizeof integration_screenshot, "%s/screenshot.png", APP_INTEGRATION_ARTIFACTS);
        screenshot_path = integration_screenshot;
    }
    if (screenshot_path && frames == 0) {
        frames = APP_DEFAULT_SCREENSHOT_FRAMES;
    }

    CF_Result result = cf_make_app(
        "c-blueprints",
        0,
        0,
        0,
        APP_WINDOW_WIDTH,
        APP_WINDOW_HEIGHT,
        CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_RESIZABLE_BIT |
            CF_APP_OPTIONS_NO_AUDIO_BIT | (headless ? CF_APP_OPTIONS_HIDDEN_BIT : 0),
        argv[0]
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "cf_make_app failed: %s\n", result.details ? result.details : "unknown error");
        return 1;
    }
    if (fps > 0) {
        cf_set_target_framerate(fps);
    }
    cf_app_init_imgui();
    if (screenshot_path) {
#ifdef __EMSCRIPTEN__
        fprintf(stderr, "screenshots are unavailable on web builds\n");
        cf_destroy_app();
        return 1;
#else
        if (!cf_app_set_imgui_render_target(cf_app_get_canvas())) {
            fprintf(stderr, "could not set the screenshot render target\n");
            cf_destroy_app();
            return 1;
        }
#endif
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
        .screenshot_path = screenshot_path,
        .capture_error = 0,
        .integration = integration,
    };
#ifdef __EMSCRIPTEN__
    /* Browser event processing must return to the browser between frames. */
    APP_LOOP_STATE = loop;
    APP_LOOP = &APP_LOOP_STATE;
    app_web_set_main_loop(app_web_frame);
#else
    while ((headless && (frames == 0 || loop.frame < frames)) ||
           (!headless && cf_app_is_running())) {
        if (app_frame(&loop)) {
            break;
        }
    }
    app_cleanup(&loop);
#endif
    return loop.capture_error || loop.integration_failed;
}
