#include "../core/blueprint.h"
#include "../core/instance.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../editor/editor.h"
#include "../engine/engine.h"
#include "../game/generated.h"
#include "cute_host.h"
#include "demo_graph.h"
#include "scene_authoring.h"
#include "stacktrace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_WINDOW_WIDTH 1920
#define APP_WINDOW_HEIGHT 1080
#define APP_DEFAULT_SCREENSHOT_PATH "/tmp/c-blueprints-screenshot.png"
#define APP_DEFAULT_SCREENSHOT_FRAMES 3

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
    CF_Pixel *pixels = malloc((size_t)size);
    if (!pixels) {
        fprintf(stderr, "screenshot allocation failed\n");
        cf_destroy_readback(readback);
        return 1;
    }
    while (!cf_readback_ready(readback)) {
    }
    int copied = cf_readback_data(readback, pixels, size);
    CF_Image image = { width, height, pixels };
    void *png_data = NULL;
    int png_size = 0;
    CF_Result result = copied == size
                         ? cf_image_save_png_to_memory(&image, &png_data, &png_size)
                         : (CF_Result){ -1, NULL };
    free(pixels);
    cf_destroy_readback(readback);
    if (cf_is_error(result) || !png_data || png_size <= 0) {
        fprintf(
            stderr,
            "screenshot encoding failed: %s\n",
            result.details ? result.details : path
        );
        cf_free(png_data);
        return 1;
    }
    FILE *file = fopen(path, "wb");
    size_t written = file ? fwrite(png_data, 1, (size_t)png_size, file) : 0;
    if (file) {
        fclose(file);
    }
    cf_free(png_data);
    if (written != (size_t)png_size) {
        fprintf(stderr, "screenshot write failed: %s\n", path);
        return 1;
    }
    printf("screenshot saved: %s (%dx%d)\n", path, width, height);
    return 0;
}

static void demo_dispatch(editor *ed, const char *name, flow_inputs inputs)
{
    flow_error error = { 0 };
    (void)editor_dispatch_preview(ed, name, inputs, &error);
}

/* ENG-4: the host's whole action vocabulary. A held key sends one movement
 * delta per frame. Both keys for an action are equivalent. */
typedef struct
{
    CF_KeyButton key;
    CF_KeyButton alternate;
    const char *action;
} app_input_binding;

static const app_input_binding APP_INPUT_BINDINGS[] = {
    { CF_KEY_W, CF_KEY_UP, "MoveUp" },
    { CF_KEY_S, CF_KEY_DOWN, "MoveDown" },
    { CF_KEY_A, CF_KEY_LEFT, "MoveLeft" },
    { CF_KEY_D, CF_KEY_RIGHT, "MoveRight" },
    { CF_KEY_SPACE, CF_KEY_SPACE, "Jump" },
};

enum
{
    APP_INPUT_BINDING_COUNT = (int)(sizeof APP_INPUT_BINDINGS / sizeof *APP_INPUT_BINDINGS),
};

static const double APP_INPUT_SPEED = 180.0;

/* EVT-10: the events this host dispatches, with the exact signature each one
 * carries. These must match what demo_dispatch() and route_input() send, or an
 * authored parameter reads its default instead of the dispatched value. Core
 * assigns none of these names any meaning. */
static const editor_event_param APP_TICK_PARAMS[] = {
    { "DeltaSeconds", PIN_F64, "double" },
};

static const editor_event_param APP_INPUT_PARAMS[] = {
    { "Delta", PIN_STRUCT, "CF_V2" },
};

static const editor_event_signature APP_EVENT_CATALOG[] = {
    { "Begin", NULL, 0 },
    { "Tick", APP_TICK_PARAMS, 1 },
    { "Input", APP_INPUT_PARAMS, 1 },
    { "OnStart", NULL, 0 },
};

enum
{
    APP_EVENT_CATALOG_COUNT = (int)(sizeof APP_EVENT_CATALOG / sizeof *APP_EVENT_CATALOG),
};

static int app_action_just_pressed(const app_input_binding *binding)
{
    return cf_key_just_pressed(binding->key) ||
           (binding->alternate != binding->key &&
            cf_key_just_pressed(binding->alternate));
}

static int app_action_just_released(const app_input_binding *binding)
{
    return cf_key_just_released(binding->key) ||
           (binding->alternate != binding->key &&
            cf_key_just_released(binding->alternate));
}

static double app_action_value(const char *action, int down, double delta)
{
    if (!down) {
        return 0.0;
    }
    double value = APP_INPUT_SPEED * delta;
    if (strcmp(action, "MoveLeft") == 0 || strcmp(action, "MoveUp") == 0) {
        return -value;
    }
    return value;
}

static void app_input_delta(const char *action, double value, double *x, double *y)
{
    *x = 0.0;
    *y = 0.0;
    if (strcmp(action, "MoveLeft") == 0 || strcmp(action, "MoveRight") == 0) {
        *x = value;
    }
    if (strcmp(action, "MoveUp") == 0 || strcmp(action, "MoveDown") == 0) {
        *y = value;
    }
}

/* ENG-3: a panel editing text owns the keyboard outright. Otherwise the
 * focused viewport receives the action through the scene's generic Input event,
 * and the editor preview receives it when the viewport has no focus. */
static void route_input(
    editor *ed,
    app_scene_authoring *scene,
    blueprint_instance *preview,
    int viewport_focused,
    double delta
)
{
    if (editor_host_captures_keyboard()) {
        return;
    }
    for (int i = 0; i < APP_INPUT_BINDING_COUNT; i++) {
        const app_input_binding *binding = &APP_INPUT_BINDINGS[i];
        int down = cf_key_down(binding->key) || (binding->alternate != binding->key &&
                                                 cf_key_down(binding->alternate));
        int changed = app_action_just_pressed(binding) ||
                      app_action_just_released(binding);
        if (!down && !changed) {
            continue;
        }
        double value = app_action_value(binding->action, down, delta);
        if (viewport_focused) {
            (void)app_scene_authoring_input(scene, binding->action, value, down);
            continue;
        }
        if (!preview) {
            continue;
        }
        double x;
        double y;
        app_input_delta(binding->action, value, &x, &y);
        CF_V2 delta = { (float)x, (float)y };
        pin_value params[] = {
            { .type = PIN_STRUCT, .ptr = &delta, .type_name = "CF_V2" },
        };
        demo_dispatch(ed, "Input", (flow_inputs){ params, 1, NULL });
    }
}

int main(int argc, char **argv)
{
    stacktrace_init(argv[0]);
    cf_set_assert_handler(stacktrace_assert_handler);
    int frames = 0; /* 0 = run until the window closes */
    int fps = 0;    /* 0 = use the host default */
    int seed = 1;
    int flow_demo = 0;
    int event_demo = 0;
    int headless = 0;
    const char *screenshot_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--no-seed") == 0) {
            seed = 0;
        } else if (strcmp(argv[i], "--flow-demo") == 0) {
            flow_demo = 1;
        } else if (strcmp(argv[i], "--event-demo") == 0) {
            event_demo = 1;
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

    CF_Result r = cf_make_app(
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
    if (cf_is_error(r)) {
        fprintf(stderr, "cf_make_app failed: %s\n", r.details ? r.details : "unknown error");
        return 1;
    }
    if (fps > 0) {
        cf_set_target_framerate(fps);
    }

    cf_app_init_imgui();
    if (screenshot_path && !cf_app_set_imgui_render_target(cf_app_get_canvas())) {
        fprintf(stderr, "the active graphics backend cannot render ImGui offscreen\n");
        cf_destroy_app();
        return 1;
    }
    engine_viewport *viewport = engine_viewport_new(640, 360);

    blueprint *bp = blueprint_new("DemoBlueprint");
    registry_validate();
    editor *ed = editor_new(bp);
    editor_set_event_catalog(ed, APP_EVENT_CATALOG, APP_EVENT_CATALOG_COUNT);

    if (seed) {
        if (event_demo) {
            demo_graph_seed_event_demo(bp);
        } else if (flow_demo) {
            demo_graph_seed_flow(bp->graph);
        } else {
            demo_graph_seed(bp->graph);
        }
    }

    if (!bp->owner_type_name) {
        blueprint_set_owner_type(bp, "ecs.entity");
    }
    char *working_json = blueprint_save_json(bp);
    blueprint *working = working_json ? blueprint_load_json(working_json) : NULL;
    free(working_json);
    ecs_world *world = ecs_world_new();
    if (!working || !world ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) != ECS_OK ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL) != ECS_OK) {
        blueprint_free(working);
        editor_free(ed);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    game_live_world *live = game_live_world_new(world);
    game_live_definition *definition = game_live_definition_new("SceneBehavior", working);
    if (!live || !definition || game_live_add_definition(live, definition) != 0) {
        game_live_world_free(live);
        if (definition) {
            game_live_definition_free(definition);
        } else {
            blueprint_free(working);
        }
        editor_free(ed);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    engine *runtime = engine_new(world, live);
    app_scene_authoring *scene = app_scene_authoring_new(
        world,
        live,
        definition,
        editor_blueprint(ed),
        ed,
        NULL,
        NULL,
        runtime
    );
    if (!runtime || !scene || app_scene_authoring_load_content(scene) != 0) {
        app_scene_authoring_free(scene);
        engine_free(runtime);
        editor_free(ed);
        game_live_world_free(live);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    engine_set_focus(runtime, ENGINE_FOCUS_CANVAS);
    engine_viewport_set_render_callback(viewport, app_scene_authoring_render, scene);
    if (seed) {
        app_scene_authoring_seed_default(scene);
        if (!flow_demo && !event_demo &&
            app_scene_authoring_open_behavior(scene, "InputBehavior") != 0) {
            fprintf(stderr, "could not open InputBehavior\n");
            app_scene_authoring_free(scene);
            engine_free(runtime);
            editor_free(ed);
            game_live_world_free(live);
            ecs_world_free(world);
            engine_viewport_free(viewport);
            cf_destroy_app();
            return 1;
        }
    }

    /* EVT-10 demo conventions: the host drives one preview instance and
     * dispatches Begin() once per instance (re)creation for the event demo.
     * The flow demo starts its authored OnStart entry so its traversal records
     * are visible without a JSON edit or a separate setup step. Tick(Real
     * DeltaSeconds) runs once per frame, and Input(CF_V2 Delta)
     * comes from the keys in APP_INPUT_BINDINGS and is broadcast
     * to attached behaviors. None of these names are core
     * lifecycle entries; dispatch fails silently when the authored graph has no
     * such event. An authoring edit destroys the instance at the editor choke
     * point (UI-56), so the selected demo event fires again when the instance
     * is recreated. */
    if (seed && (flow_demo || event_demo)) {
        editor_preview_create(ed);
    }
    unsigned instance_gen = 0;
    double last_seconds = CF_SECONDS;

    int frame = 0;
    int capture_error = screenshot_path != NULL;
    while (cf_app_is_running()) {
        cf_app_update(NULL);
        double seconds = CF_SECONDS;
        editor_tick(ed, seconds);
        app_scene_authoring_set_editor_working(scene, editor_blueprint(ed));
        (void)app_scene_authoring_apply_pending(scene);

        blueprint_instance *bi = editor_preview_instance(ed);
        if (bi) {
            if (editor_preview_generation(ed) != instance_gen) {
                instance_gen = editor_preview_generation(ed);
                demo_dispatch(ed, flow_demo ? "OnStart" : "Begin", (flow_inputs){ 0 });
            }
            double delta = seconds - last_seconds;
            if (delta > 0.0 && delta < 1.0) {
                pin_value tick_params[] = { { .type = PIN_F64, .d = delta } };
                demo_dispatch(ed, "Tick", (flow_inputs){ tick_params, 1, NULL });
            }
        }
        /* ENG-3: the scene panel's own focus, sampled during last frame's draw,
         * decides the consumer. Escape hands focus back to the canvas by
         * defocusing every window, so no separate released state is needed. */
        int viewport_focused = engine_viewport_focused(viewport);
        if (viewport_focused && cf_key_just_pressed(CF_KEY_ESCAPE)) {
            engine_viewport_release_focus();
            (void)engine_route_escape(runtime);
            viewport_focused = 0;
        }
        engine_set_focus(runtime, viewport_focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_CANVAS);
        double delta = seconds - last_seconds;
        if (delta < 0.0 || delta >= 1.0) {
            delta = 0.0;
        }
        route_input(ed, scene, bi, viewport_focused, delta);
        (void)app_scene_authoring_frame(scene, seconds);
        last_seconds = seconds;

        editor_prepare_docking();
        engine_viewport_draw(viewport);
        app_scene_authoring_draw(scene);
        editor_draw(ed);
        cf_app_draw_onto_screen(true);

        int finished = frames > 0 && ++frame >= frames;
        if (finished && screenshot_path) {
            capture_error = save_screenshot(screenshot_path);
            break;
        }
        if (finished) {
            break;
        }
    }

    app_scene_authoring_free(scene);
    editor_free(ed);
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
    engine_viewport_free(viewport);
    cf_destroy_app();
    return capture_error;
}
