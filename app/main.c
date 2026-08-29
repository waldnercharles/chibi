#include "../core/registry.h"
#include "../core/serialize.h"
#include "../core/text_file.h"
#include "../editor/ui_shell.h"
#include "../engine/engine.h"
#include "../game/generated.h"
#include "../game/live.h"
#include "../game/prototype.h"
#include "cute_host.h"
#include "movement_authoring.h"
#include "stacktrace.h"
#include "web_loop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_WINDOW_WIDTH 1920
#define APP_WINDOW_HEIGHT 1080
#define APP_DEFAULT_SCREENSHOT_PATH "/tmp/c-blueprints-screenshot.png"
#define APP_DEFAULT_SCREENSHOT_FRAMES 3
#define MOVEMENT_SYSTEM_PATH "content/systems/MovementSystem.json"

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
    game_system *system;
    engine *runtime;
    game_live_world *live;
    ecs_world *world;
    engine_viewport *viewport;
    movement_authoring *authoring;
    int frames;
    int frame;
    const char *screenshot_path;
    int capture_error;
} app_loop;

static void app_cleanup(app_loop *loop)
{
    movement_authoring_free(loop->authoring);
    engine_free(loop->runtime);
    game_live_world_free(loop->live);
    game_system_free(loop->system);
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
    double seconds = CF_SECONDS;
    editor_tick(loop->graph_editor, seconds);
    movement_authoring_sync(loop->authoring);
    int viewport_focused = engine_viewport_focused(loop->viewport);
    if (viewport_focused && cf_key_just_pressed(CF_KEY_ESCAPE)) {
        engine_viewport_release_focus();
        engine_route_escape(loop->runtime);
        viewport_focused = 0;
    }
    engine_set_focus(loop->runtime, viewport_focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_CANVAS);
    movement_authoring_input(loop->authoring, viewport_focused != 0);
    if (engine_frame(loop->runtime, seconds) != FLOW_OK) {
        fprintf(stderr, "runtime frame failed\n");
    }

    editor_prepare_docking();
    engine_viewport_draw(loop->viewport);
    movement_authoring_draw(loop->authoring);
    editor_draw(loop->graph_editor);
    cf_app_draw_onto_screen(true);

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
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot") == 0) {
            screenshot_path = APP_DEFAULT_SCREENSHOT_PATH;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                screenshot_path = argv[++i];
            }
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        }
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
            fprintf(stderr, "the active graphics backend cannot render ImGui offscreen\n");
            cf_destroy_app();
            return 1;
        }
        editor_ui_set_framebuffer_target(true);
#endif
    }

    registry_validate();
    char *json = text_file_read(MOVEMENT_SYSTEM_PATH);
    blueprint *runtime_definition = json ? blueprint_load_json(json) : NULL;
    free(json);
    char *behavior_editor_json = text_file_read("content/behaviors/InputBehavior.json");
    blueprint *editor_definition = behavior_editor_json
                                     ? blueprint_load_json(behavior_editor_json)
                                     : NULL;
    free(behavior_editor_json);
    ecs_world *world = ecs_world_new();
    engine_viewport *viewport = engine_viewport_new(640, 360);
    editor *graph_editor = editor_definition ? editor_new(editor_definition) : NULL;
    game_system *system = world && runtime_definition
                            ? game_system_new(world, runtime_definition)
                            : NULL;
    char *behavior_json = text_file_read("content/behaviors/InputBehavior.json");
    blueprint *behavior_working = behavior_json ? blueprint_load_json(behavior_json)
                                                : NULL;
    free(behavior_json);
    game_live_world *live = world ? game_live_world_new(world) : NULL;
    game_live_definition *behavior_definition = behavior_working
                                                  ? game_live_definition_new("InputBehavior", behavior_working)
                                                  : NULL;
    if (!live || !behavior_definition ||
        game_live_add_definition(live, behavior_definition) != 0) {
        game_live_definition_free(behavior_definition);
        behavior_definition = NULL;
    }
    engine *runtime = world && live ? engine_new(world, live) : NULL;
    movement_authoring *authoring = NULL;
    game_prototype *player_prototype = NULL;
    ecs_entity player = { 0 };
    if (!world || !viewport || !graph_editor || !system || !runtime ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) != ECS_OK ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) != ECS_OK ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameInput, NULL) != ECS_OK) {
        movement_authoring_free(authoring);
        engine_free(runtime);
        game_live_world_free(live);
        game_system_free(system);
        editor_free(graph_editor);
        game_prototype_free(player_prototype);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    const game_prototype_registry registry = {
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
        NULL,
        NULL,
    };
    char player_error[256] = { 0 };
    char *player_json = text_file_read("content/prototypes/Player.json");
    player_prototype = player_json
                         ? game_prototype_load(player_json, &registry, player_error, sizeof player_error)
                         : NULL;
    free(player_json);
    if (!player_prototype ||
        game_prototype_spawn(player_prototype, world, NULL, &player, player_error, sizeof player_error) !=
            ECS_OK) {
        fprintf(stderr, "could not spawn Player: %s\\n", player_error);
        engine_free(runtime);
        game_system_free(system);
        editor_free(graph_editor);
        game_prototype_free(player_prototype);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    authoring = movement_authoring_new(world, player, system, graph_editor, runtime);
    if (!authoring ||
        game_behavior_attach(live->behaviors, player, "InputBehavior") != 0) {
        movement_authoring_free(authoring);
        engine_free(runtime);
        game_live_world_free(live);
        game_system_free(system);
        editor_free(graph_editor);
        game_prototype_free(player_prototype);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    game_prototype_free(player_prototype);
    editor_ui_reset_layout();
    editor_set_event_catalog(
        graph_editor,
        APP_EVENT_CATALOG,
        (int)(sizeof APP_EVENT_CATALOG / sizeof *APP_EVENT_CATALOG)
    );
    editor_set_ui_context(graph_editor, EDITOR_UI_CONTEXT_BEHAVIOR);
    editor_set_save_handler(graph_editor, movement_authoring_save, authoring);
    engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT);
    editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
    engine_viewport_set_render_callback(viewport, movement_authoring_render, authoring);

    app_loop loop = {
        .graph_editor = graph_editor,
        .system = system,
        .runtime = runtime,
        .live = live,
        .world = world,
        .viewport = viewport,
        .authoring = authoring,
        .frames = frames,
        .screenshot_path = screenshot_path,
        .capture_error = screenshot_path != NULL,
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
    return loop.capture_error;
}
