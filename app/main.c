#include "../core/registry.h"
#include "../core/serialize.h"
#include "../editor/editor.h"
#include "../editor/ui_shell.h"
#include "../engine/engine.h"
#include "../game/generated.h"
#include "../game/live.h"
#include "../game/prototype.h"
#include "authoring_session.h"
#include "cute_host.h"
#include "scene_authoring.h"
#include "stacktrace.h"
#include "web_loop.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define APP_WINDOW_WIDTH 1920
#define APP_WINDOW_HEIGHT 1080
#define APP_DEFAULT_SCREENSHOT_PATH "/tmp/c-blueprints-screenshot.png"
#define APP_DEFAULT_SCREENSHOT_FRAMES 3
#define APP_RESIZE_PANE_FRAMES 48
#define APP_RESIZE_PANE_PRESS_FRAME 12
#define APP_RESIZE_PANE_RELEASE_FRAME 36
#define APP_RESIZE_PANE_DELTA 240.0f
/* Pane widths are quantized to framebuffer pixels during the drag. */
#define APP_RESIZE_PANE_DELTA_TOLERANCE 1.0f
typedef enum
{
    APP_SCENARIO_NONE,
    APP_SCENARIO_RESIZE_PANE,
    APP_SCENARIO_AUTHORING,
} app_scenario;

typedef struct
{
    float framebuffer_x;
    float framebuffer_y;
    bool primary_down;
} app_mouse_input;

static void print_usage(const char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("\nOptions:\n");
    printf("  --help                 Show this help text\n");
    printf("  --headless             Run without showing a window\n");
    printf("  --frames <count>       Exit after count rendered frames\n");
    printf("  --scenario <name>      Headless assertion: resize-pane or authoring\n");
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
    int headless;
    app_scenario scenario;
    double monotonic_time;
    app_mouse_input input;
    float scenario_before_width;
    float scenario_after_width;
    float scenario_drag_x;
    int scenario_checked;
} app_loop;

static int app_run_authoring_workflow(void)
{
    app_authoring_session *session = app_authoring_session_new("content");
    char path[128];
    const compile_report *report;
    int diagnostic_count;
    int error_count;
    int warning_count;
    int result = -1;
    int temp_ready = 0;
    int fd = -1;
    if (snprintf(path, sizeof path, "/tmp/c-blueprints-authoring-XXXXXX") < 0 ||
        (fd = mkstemp(path)) < 0 || close(fd) != 0 || !(temp_ready = 1) ||
        !session || app_authoring_session_load_fixtures(session) <= 0 ||
        app_authoring_session_create_behavior(session, "ScenarioBehavior") != 0) {
        fprintf(stderr, "authoring scenario could not create a Behavior Asset\n");
        goto cleanup;
    }
    graph *function = app_authoring_session_add_graph(session, GRAPH_FUNCTION, "ScenarioGraph");
    uint64_t graph_id = function ? function->graph_id : 0;
    node *entry = function ? graph_node_by_id(function, function->entry_id) : NULL;
    node *ret = function ? graph_node_by_id(function, function->return_id) : NULL;
    if (!function || !entry || !ret ||
        app_authoring_session_connect_exec(session, entry->id, 0, ret->id, 0, 0) != 0 ||
        app_authoring_session_rename_graph(session, graph_id, "ScenarioGraphRenamed") != 0 ||
        app_authoring_session_select_graph(session, graph_id) != 0) {
        fprintf(stderr, "authoring scenario could not author a graph\n");
        goto cleanup;
    }
    uint64_t literal_id = app_authoring_session_add_node(session, "Real");
    uint64_t add_id = app_authoring_session_add_node(session, "Add");
    uint64_t branch_id = app_authoring_session_add_node(session, "Branch");
    uint64_t latent_id = app_authoring_session_add_node(session, "Delay");
    pin_value value = { .type = PIN_F64, .d = 2.5 };
    int reflected = 0;
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        const char *name = desc ? desc->name : NULL;
        if (name && strstr(name, "Add")) {
            reflected++;
        }
    }
    if (!literal_id || !add_id || !branch_id || !latent_id || reflected <= 0 ||
        app_authoring_session_set_pin_default(session, literal_id, 0, 0, &value) != 0 ||
        app_authoring_session_connect_data(session, literal_id, 0, add_id, 0) != 0 ||
        app_authoring_session_connect_data(session, literal_id, 0, branch_id, 1) == 0) {
        fprintf(stderr, "authoring scenario node, literal, search, or typed-edge check failed\n");
        goto cleanup;
    }
    if (app_authoring_session_compile(session) == 0) {
        fprintf(stderr, "authoring scenario did not report the intentional latent-node diagnostic\n");
        goto cleanup;
    }
    report = app_authoring_session_diagnostics(session);
    const compile_diagnostic *diagnostic = NULL;
    for (int i = 0; i < report->error_count + report->warning_count; i++) {
        if (report->items[i].rule == CMPL_RULE_FUNCTION_LATENT &&
            report->items[i].node_id == latent_id) {
            diagnostic = &report->items[i];
            break;
        }
    }
    diagnostic_count = report->error_count + report->warning_count;
    error_count = report->error_count;
    warning_count = report->warning_count;
    const graph *authored = app_authoring_session_active_graph(session);
    if (!diagnostic || diagnostic->graph_id != authored->graph_id ||
        app_authoring_session_save(session, path) != 0 ||
        app_authoring_session_dirty(session) ||
        app_authoring_session_reload(session, path) != 0 ||
        app_authoring_session_dirty(session)) {
        fprintf(stderr, "authoring scenario persistence or diagnostics check failed\n");
        goto cleanup;
    }
    authored = behavior_asset_find_graph_by_name(app_authoring_session_behavior(session), "ScenarioGraphRenamed");
    if (!authored) {
        fprintf(stderr, "authoring scenario reload lost authored graph\n");
        goto cleanup;
    }
    const node *reloaded_literal = graph_node_by_id(authored, literal_id);
    const node *reloaded_add = graph_node_by_id(authored, add_id);
    const node *reloaded_branch = graph_node_by_id(authored, branch_id);
    const node *reloaded_latent = graph_node_by_id(authored, latent_id);
    int has_valid_edge = 0;
    for (int i = 0; i < acount(authored->edges); i++) {
        const edge *edge = &authored->edges[i];
        if (edge->from_id == literal_id && edge->to_id == add_id &&
            edge->kind == GRAPH_EDGE_DATA) {
            has_valid_edge = 1;
            break;
        }
    }
    if (!reloaded_literal || !reloaded_add || !reloaded_branch ||
        !reloaded_latent || !has_valid_edge || reloaded_literal->input_count <= 0 ||
        reloaded_literal->input_pins[0].default_value.type != PIN_F64 ||
        reloaded_literal->input_pins[0].default_value.d != 2.5) {
        fprintf(stderr, "authoring scenario reload lost authored graph state\n");
        goto cleanup;
    }
    printf(
        "authoring scenario passed: reflected=%d diagnostics=%d errors=%d "
        "warnings=%d\n",
        reflected,
        diagnostic_count,
        error_count,
        warning_count
    );
    result = 0;
cleanup:
    if (temp_ready) {
        remove(path);
    }
    app_authoring_session_free(session);
    return result;
}

static int app_prepare_input(app_loop *loop)
{
    if (loop->scenario == APP_SCENARIO_NONE || loop->scenario == APP_SCENARIO_AUTHORING) {
        loop->input = (app_mouse_input){
            .framebuffer_x = cf_mouse_x(),
            .framebuffer_y = cf_mouse_y(),
            .primary_down = cf_mouse_down(CF_MOUSE_BUTTON_LEFT),
        };
        return 0;
    }

    if (loop->frame == 0) {
        loop->input = (app_mouse_input){
            .framebuffer_x = 120.0f,
            .framebuffer_y = 120.0f,
        };
        editor_ui_set_mouse_input(120.0f, 120.0f, false);
        return 0;
    }
    float splitter_x = editor_ui_left_pane_splitter_x();
    float center_y = editor_ui_left_pane_center_y();
    if (!splitter_x || !center_y) {
        fprintf(stderr, "resize-pane scenario could not find the left dock pane\n");
        return -1;
    }
    int frame = loop->frame;
    loop->input = (app_mouse_input){
        .framebuffer_x = splitter_x,
        .framebuffer_y = center_y,
    };
    if (frame == APP_RESIZE_PANE_PRESS_FRAME) {
        loop->input.framebuffer_x = splitter_x;
        loop->input.primary_down = true;
        loop->scenario_before_width = editor_ui_left_pane_width();
        loop->scenario_drag_x = splitter_x + APP_RESIZE_PANE_DELTA;
    } else if (frame > APP_RESIZE_PANE_PRESS_FRAME && frame < APP_RESIZE_PANE_RELEASE_FRAME) {
        loop->input.framebuffer_x = loop->scenario_drag_x;
        loop->input.primary_down = true;
    }
    if (frame == APP_RESIZE_PANE_RELEASE_FRAME) {
        loop->input.framebuffer_x = loop->scenario_drag_x;
        loop->input.primary_down = false;
    }
    editor_ui_set_mouse_input(
        loop->input.framebuffer_x,
        loop->input.framebuffer_y,
        loop->input.primary_down
    );
    return 0;
}

static void app_check_resize_pane(app_loop *loop)
{
    if (loop->scenario != APP_SCENARIO_RESIZE_PANE ||
        loop->frame != APP_RESIZE_PANE_RELEASE_FRAME) {
        return;
    }
    loop->scenario_checked = 1;
    loop->scenario_after_width = editor_ui_left_pane_width();
    float delta = loop->scenario_after_width - loop->scenario_before_width;
    const float expected_delta = APP_RESIZE_PANE_DELTA;
    const float delta_error = fabsf(delta - expected_delta);
    if (delta_error > APP_RESIZE_PANE_DELTA_TOLERANCE) {
        fprintf(
            stderr,
            "resize-pane assertion failed: width %.1f -> %.1f (delta %.1f)\n",
            loop->scenario_before_width,
            loop->scenario_after_width,
            delta
        );
        loop->capture_error = 1;
        return;
    }
    printf(
        "resize-pane assertion passed: width %.1f -> %.1f (delta %.1f)\n",
        loop->scenario_before_width,
        loop->scenario_after_width,
        delta
    );
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
    if (loop->headless) {
        editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
    }
    double seconds = loop->scenario != APP_SCENARIO_NONE ? 1.0 / 60.0 : CF_DELTA_TIME;
    if (app_prepare_input(loop) != 0) {
        loop->capture_error = 1;
        return 1;
    }
    loop->monotonic_time += seconds;
    editor_tick(loop->graph_editor, loop->monotonic_time);
    int viewport_focused = engine_viewport_focused(loop->viewport);
    if (viewport_focused && cf_key_just_pressed(CF_KEY_ESCAPE)) {
        engine_viewport_release_focus();
        engine_route_escape(loop->runtime);
        viewport_focused = 0;
    }
    engine_set_focus(loop->runtime, viewport_focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_CANVAS);
    if (viewport_focused && !editor_host_captures_keyboard()) {
        float distance = 180.0f * (float)seconds;
        app_scene_authoring_input(
            loop->authoring,
            "MoveLeft",
            distance,
            cf_key_down(CF_KEY_A) || cf_key_down(CF_KEY_LEFT)
        );
        app_scene_authoring_input(
            loop->authoring,
            "MoveRight",
            distance,
            cf_key_down(CF_KEY_D) || cf_key_down(CF_KEY_RIGHT)
        );
        app_scene_authoring_input(
            loop->authoring,
            "MoveUp",
            distance,
            cf_key_down(CF_KEY_W) || cf_key_down(CF_KEY_UP)
        );
        app_scene_authoring_input(
            loop->authoring,
            "MoveDown",
            distance,
            cf_key_down(CF_KEY_S) || cf_key_down(CF_KEY_DOWN)
        );
        if (cf_key_just_pressed(CF_KEY_SPACE)) {
            app_scene_authoring_input(loop->authoring, "Fire", 1.0, 1);
        }
    }
    if (app_scene_authoring_frame(loop->authoring, loop->monotonic_time) != FLOW_OK) {
        fprintf(stderr, "runtime frame failed\n");
    }

    editor_prepare_docking();
    engine_viewport_draw(loop->viewport);
    app_scene_authoring_draw(loop->authoring);
    editor_draw(loop->graph_editor);
    cf_app_draw_onto_screen(true);
    app_check_resize_pane(loop);

    int finished = loop->frames > 0 && ++loop->frame >= loop->frames;
    if (finished && loop->scenario == APP_SCENARIO_RESIZE_PANE &&
        !loop->scenario_checked) {
        fprintf(stderr, "resize-pane assertion did not run; increase --frames\n");
        loop->capture_error = 1;
    }
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
    app_scenario scenario = APP_SCENARIO_NONE;
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
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            if (strcmp(argv[++i], "resize-pane") == 0) {
                scenario = APP_SCENARIO_RESIZE_PANE;
            } else if (strcmp(argv[i], "authoring") == 0) {
                scenario = APP_SCENARIO_AUTHORING;
            } else {
                fprintf(stderr, "unknown scenario: %s\n", argv[i]);
                return 2;
            }
        }
    }
    if (scenario != APP_SCENARIO_NONE && !headless) {
        fprintf(stderr, "--scenario requires --headless\n");
        return 2;
    }
    if (scenario != APP_SCENARIO_NONE && frames == 0) {
        frames = APP_RESIZE_PANE_FRAMES;
    }
    if (scenario == APP_SCENARIO_AUTHORING && app_run_authoring_workflow() != 0) {
        return 1;
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
    editor_ui_set_framebuffer_target(screenshot_path != NULL);
    if (headless) {
        editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
    }
    if (screenshot_path) {
#ifdef __EMSCRIPTEN__
        fprintf(stderr, "screenshots are unavailable on web builds\n");
        cf_destroy_app();
        return 1;
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
    /* Put the shipped movement graph on screen on first launch. The graph is
     * still reachable through the Content Drawer after switching documents. */
    if (authoring && game_live_find_definition(live, "InputBehavior")) {
        (void)app_scene_authoring_open_behavior(authoring, "InputBehavior");
        editor_set_graph_visible(graph_editor, 1);
        editor_ui_request_focus(EDITOR_UI_FOCUS_NODE_CANVAS);
    }
    /* Start from a known dock tree so scripted scenarios do not depend on a
     * restored or previously resized layout. */
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
        .headless = headless,
        .scenario = scenario,
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
