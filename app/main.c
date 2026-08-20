#include "../core/graph.h"
#include "../core/registry.h"
#include "../editor/editor.h"
#include "cute_host.h"
#include "demo_graph.h"
#include "stacktrace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        960,
        640,
        CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_RESIZABLE_BIT |
            CF_APP_OPTIONS_NO_AUDIO_BIT,
        argv[0]
    );
    if (cf_is_error(r)) {
        fprintf(stderr, "cf_make_app failed: %s\n", r.details ? r.details : "unknown error");
        return 1;
    }

    cf_app_init_imgui();

    graph g;
    graph_init(&g);
    registry_validate();
    editor *ed = editor_new(&g);

    if (seed) {
        if (flow_demo) demo_graph_seed_flow(&g);
        else demo_graph_seed(&g);
    }

    int frame = 0;
    while (cf_app_is_running()) {
        cf_app_update(NULL);
        editor_tick(ed, CF_SECONDS);
        editor_draw(ed);
        cf_app_draw_onto_screen(true);

        if (frames > 0 && ++frame >= frames) break;
    }

    editor_free(ed);
    graph_free(&g);
    cf_destroy_app();
    return 0;
}
