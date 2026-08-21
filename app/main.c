#include "../core/blueprint.h"
#include "../core/instance.h"
#include "../core/registry.h"
#include "../editor/editor.h"
#include "cute_host.h"
#include "demo_graph.h"
#include "stacktrace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_WINDOW_WIDTH 1920
#define APP_WINDOW_HEIGHT 1080

static void demo_dispatch(blueprint_instance *bi, const char *name, flow_inputs inputs)
{
    flow_error error = { 0 };
    (void)blueprint_instance_dispatch(bi, name, inputs, &error);
}

int main(int argc, char **argv)
{
    stacktrace_init(argv[0]);
    cf_set_assert_handler(stacktrace_assert_handler);
    int frames = 0; /* 0 = run until the window closes */
    int seed = 1;
    int flow_demo = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--no-seed") == 0) {
            seed = 0;
        } else if (strcmp(argv[i], "--flow-demo") == 0) {
            flow_demo = 1;
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

    cf_app_init_imgui();

    blueprint *bp = blueprint_new("DemoBlueprint");
    registry_validate();
    editor *ed = editor_new(bp);

    if (seed) {
        if (flow_demo) demo_graph_seed_flow(bp->graph);
        else demo_graph_seed(bp->graph);
    }

    /* EVT-10 demo conventions: the host drives one preview instance and
     * dispatches Begin() once per instance (re)creation, Tick(Real
     * DeltaSeconds) once per frame, and Input(Name Action, Real Value, Bool
     * Pressed) from the space key. None of these names are core lifecycle
     * entries; dispatch fails silently when the authored graph has no such
     * event. An authoring edit destroys the instance at the editor choke
     * point (UI-56), so Begin() fires again when the instance is recreated. */
    editor_preview_create(ed);
    unsigned instance_gen = editor_preview_generation(ed);
    double last_seconds = CF_SECONDS;

    int frame = 0;
    while (cf_app_is_running()) {
        cf_app_update(NULL);
        double seconds = CF_SECONDS;
        editor_tick(ed, seconds);

        blueprint_instance *bi = editor_preview_instance(ed);
        if (bi) {
            if (editor_preview_generation(ed) != instance_gen) {
                instance_gen = editor_preview_generation(ed);
                demo_dispatch(bi, "Begin", (flow_inputs){ 0 });
            }
            double delta = seconds - last_seconds;
            if (delta > 0.0 && delta < 1.0) {
                pin_value tick_params[] = { { .type = PIN_F64, .d = delta } };
                demo_dispatch(bi, "Tick", (flow_inputs){ tick_params, 1, NULL });
            }
            if (cf_key_just_pressed(CF_KEY_SPACE)) {
                pin_value input_params[] = {
                    { .type = PIN_NAME, .str = "Jump" },
                    { .type = PIN_F64, .d = 1.0 },
                    { .type = PIN_BOOL, .b = true },
                };
                demo_dispatch(bi, "Input", (flow_inputs){ input_params, 3, NULL });
            } else if (cf_key_just_released(CF_KEY_SPACE)) {
                pin_value input_params[] = {
                    { .type = PIN_NAME, .str = "Jump" },
                    { .type = PIN_F64, .d = 0.0 },
                    { .type = PIN_BOOL, .b = false },
                };
                demo_dispatch(bi, "Input", (flow_inputs){ input_params, 3, NULL });
            }
        }
        last_seconds = seconds;

        editor_draw(ed);
        cf_app_draw_onto_screen(true);

        if (frames > 0 && ++frame >= frames) break;
    }

    editor_free(ed);
    cf_destroy_app();
    return 0;
}
