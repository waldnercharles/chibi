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

static void demo_dispatch(editor *ed, const char *name, flow_inputs inputs)
{
    flow_error error = { 0 };
    (void)editor_dispatch(ed, name, inputs, &error);
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
        }
    }

    CF_Result r = cf_make_app(
        "c-blueprints",
        0,
        0,
        0,
        APP_WINDOW_WIDTH,
        APP_WINDOW_HEIGHT,
        CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_RESIZABLE_BIT |
            CF_APP_OPTIONS_NO_AUDIO_BIT,
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
    engine_viewport *viewport = engine_viewport_new(640, 360);

    blueprint *bp = blueprint_new("DemoBlueprint");
    registry_validate();
    editor *ed = editor_new(bp);

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
    app_scene_authoring *
        scene = app_scene_authoring_new(world, live, definition, editor_blueprint(ed), runtime);
    if (!runtime || !scene) {
        app_scene_authoring_free(scene);
        engine_free(runtime);
        editor_free(ed);
        game_live_world_free(live);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        cf_destroy_app();
        return 1;
    }
    engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT);
    engine_viewport_set_render_callback(viewport, app_scene_authoring_render, scene);
    if (seed) {
        app_scene_authoring_seed_default(scene);
    }

    /* EVT-10 demo conventions: the host drives one preview instance and
     * dispatches Begin() once per instance (re)creation for the event demo.
     * The flow demo starts its authored OnStart entry so its traversal records
     * are visible without a JSON edit or a separate setup step. Tick(Real
     * DeltaSeconds) runs once per frame, and Input(Name Action, Real Value,
     * Bool Pressed) comes from the space key. None of these names are core
     * lifecycle entries; dispatch fails silently when the authored graph has no
     * such event. An authoring edit destroys the instance at the editor choke
     * point (UI-56), so the selected demo event fires again when the instance
     * is recreated. */
    if (seed) {
        editor_preview_create(ed);
    }
    unsigned instance_gen = 0;
    double last_seconds = CF_SECONDS;

    int frame = 0;
    while (cf_app_is_running()) {
        cf_app_update(NULL);
        double seconds = CF_SECONDS;
        editor_tick(ed, seconds);
        app_scene_authoring_set_editor_working(scene, editor_blueprint(ed));

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
            if (cf_key_just_pressed(CF_KEY_SPACE)) {
                pin_value input_params[] = {
                    { .type = PIN_NAME, .str = "Jump" },
                    { .type = PIN_F64, .d = 1.0 },
                    { .type = PIN_BOOL, .b = true },
                };
                demo_dispatch(ed, "Input", (flow_inputs){ input_params, 3, NULL });
            } else if (cf_key_just_released(CF_KEY_SPACE)) {
                pin_value input_params[] = {
                    { .type = PIN_NAME, .str = "Jump" },
                    { .type = PIN_F64, .d = 0.0 },
                    { .type = PIN_BOOL, .b = false },
                };
                demo_dispatch(ed, "Input", (flow_inputs){ input_params, 3, NULL });
            }
        }
        if (cf_key_just_pressed(CF_KEY_SPACE)) {
            (void)app_scene_authoring_input(scene, "Jump", 1.0, 1);
        } else if (cf_key_just_released(CF_KEY_SPACE)) {
            (void)app_scene_authoring_input(scene, "Jump", 0.0, 0);
        }
        (void)app_scene_authoring_frame(scene, seconds);
        last_seconds = seconds;

        editor_draw(ed);
        engine_viewport_draw(viewport);
        app_scene_authoring_draw(scene);
        cf_app_draw_onto_screen(true);

        if (frames > 0 && ++frame >= frames) {
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
    return 0;
}
