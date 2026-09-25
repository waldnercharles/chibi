#include "chibi/application.h"

#include "assets/project.h"
#include "assets/prototype.h"
#include "diagnostics/tracy.h"
#include "editor/studio/studio.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/game_presentation.h"
#include "editor/workspace/gameplay_input.h"
#include "editor/workspace/input.h"
#include "editor/workspace/scene_authoring.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define APP_DEFAULT_SCREENSHOT_PATH "/tmp/chibi-screenshot.png"
#define APP_DEFAULT_SCREENSHOT_FRAMES 3

typedef struct
{
    editor *graph_editor;
    engine *runtime;
    ecs_world *world;
    game_live_world *live;
    engine_viewport *viewport;
    app_scene_authoring *authoring;
    app_game_presentation *game;
    studio *studio;
    int frames;
    int frame;
    const char *screenshot_path;
    bool show_graph; /* --open with --screenshot: the graph canvas fills the window */
    int capture_error;
    int headless;
    double monotonic_time;
    double fixed_dt;
    const chibi_host *host;
} app_loop;

static void app_cleanup(app_loop *loop)
{
    app_game_presentation_free(loop->game);
    app_scene_authoring_free(loop->authoring);
    engine_free(loop->runtime);
    game_live_world_free(loop->live);
    editor_free(loop->graph_editor);
    render_shader_catalog_clear();
    ecs_world_free(loop->world);
    engine_viewport_free(loop->viewport);
    loop->host->release(loop);
}

static int app_frame(app_loop *loop)
{
    if (!cf_app_is_running()) {
        return 1;
    }
    CHIBI_TRACE_ZONE_BEGIN(frame_zone, "Application frame");
    CHIBI_TRACE_ZONE_BEGIN(window_zone, "Window update");
    cf_app_update(NULL);
    CHIBI_TRACE_ZONE_END(window_zone);
    CHIBI_TRACE_ZONE_BEGIN(input_zone, "Input");
    app_input_frame();
    CHIBI_TRACE_ZONE_END(input_zone);
    double dt = loop->fixed_dt ? loop->fixed_dt : CF_DELTA_TIME;
    loop->monotonic_time += dt;
    int status = 0;
    if (loop->game) {
        CHIBI_TRACE_ZONE_BEGIN(game_zone, "Game presentation");
        status = app_game_presentation_frame(loop->game, loop->monotonic_time);
        CHIBI_TRACE_ZONE_END(game_zone);
    } else {
        CHIBI_TRACE_ZONE_BEGIN(editor_zone, "Editor");
        editor_tick(loop->graph_editor, loop->monotonic_time);
        app_gameplay_input(loop->authoring, loop->runtime, loop->viewport);
        if (app_scene_authoring_frame(loop->authoring, loop->monotonic_time) != FLOW_OK) {
            fprintf(stderr, "runtime frame failed\n");
        }
        studio_frame(loop->studio, dt);
        CHIBI_TRACE_ZONE_END(editor_zone);
    }
    CHIBI_TRACE_ZONE_BEGIN(present_zone, "Present / GPU wait");
    cf_app_draw_onto_screen(true);
    CHIBI_TRACE_ZONE_END(present_zone);
    CHIBI_TRACE_FRAME_MARK("Application frame");
    if (status) {
        CHIBI_TRACE_ZONE_END(frame_zone);
        return status;
    }

    int finished = loop->frames > 0 && ++loop->frame >= loop->frames;
    if (finished && loop->screenshot_path) {
        CHIBI_TRACE_ZONE_BEGIN(screenshot_zone, "Screenshot");
        loop->capture_error = app_save_screenshot(loop->screenshot_path);
        CHIBI_TRACE_ZONE_END(screenshot_zone);
        CHIBI_TRACE_ZONE_END(frame_zone);
        return 1;
    }
    CHIBI_TRACE_ZONE_END(frame_zone);
    return finished;
}

/* --open names a behavior, event, library, material, shape or field asset;
   --graph selects one of its graphs by name. */
static int open_graph(
    app_scene_authoring *authoring,
    editor *graph_editor,
    const char *asset,
    const char *graph_name,
    char *error,
    size_t error_size
)
{
    if (app_scene_authoring_open_behavior(authoring, asset) != 0) {
        snprintf(error, error_size, "--open: no graph asset named %s", asset);
        return -1;
    }
    if (!graph_name) {
        return 0;
    }
    const graph *target = behavior_asset_find_graph_by_name(editor_behavior_asset(graph_editor), graph_name);
    if (!target || editor_open_graph(graph_editor, target->graph_id) != 0) {
        snprintf(error, error_size, "--graph: %s has no graph named %s", asset, graph_name);
        return -1;
    }
    return 0;
}

static void *application_create(const chibi_host *host, const char *checkpoint, char *error, size_t error_size)
{
    snprintf(error, error_size, "Could not initialize application");
    int argc = host->argc;
    char **argv = host->argv;
    int frames = 0;
    int headless = 0;
    bool play = false;
    double fixed_dt = 0;
    const char *scene_path = NULL;
    const char *project_path = NULL;
    const char *screenshot_path = NULL;
    const char *open = NULL;
    const char *graph_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);

        } else if (strcmp(argv[i], "--screenshot") == 0) {
            screenshot_path = APP_DEFAULT_SCREENSHOT_PATH;
            headless = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                screenshot_path = argv[++i];
            }
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        } else if (strcmp(argv[i], "--play") == 0) {
            play = true;
        } else if (strcmp(argv[i], "--scene") == 0) {
            if (i + 1 == argc || argv[i + 1][0] == '-') {
                snprintf(error, error_size, "--scene requires an authored scene path");
                return NULL;
            }
            scene_path = argv[++i];
        } else if ((strcmp(argv[i], "--project") == 0 || strcmp(argv[i], "--project-root") == 0)) {
            if (i + 1 == argc || argv[i + 1][0] == '-') {
                snprintf(error, error_size, "%s requires a project directory or project.yaml", argv[i]);
                return NULL;
            }
            project_path = argv[++i];
        } else if (strcmp(argv[i], "--open") == 0 || strcmp(argv[i], "--graph") == 0) {
            if (i + 1 == argc || argv[i + 1][0] == '-') {
                snprintf(error, error_size, "%s requires a name", argv[i]);
                return NULL;
            }
            *(argv[i][2] == 'o' ? &open : &graph_name) = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--fixed-dt") == 0) {
            if (i + 1 == argc) {
                snprintf(error, error_size, "--fixed-dt requires a positive number of seconds");
                return NULL;
            }
            char *end;
            fixed_dt = strtod(argv[++i], &end);
            if (*end || !isfinite(fixed_dt) || fixed_dt <= 0) {
                snprintf(error, error_size, "--fixed-dt requires a positive number of seconds");
                return NULL;
            }
        }
    }
    if (screenshot_path && frames == 0) {
        frames = APP_DEFAULT_SCREENSHOT_FRAMES;
    }

    chibi_project project = { 0 };
    if (chibi_project_open(&project, project_path, error, error_size) != 0) {
        return NULL;
    }
    char *resolved_scene = chibi_project_scene_path(&project, scene_path);
    if (!resolved_scene) {
        snprintf(
            error,
            error_size,
            "Project does not define a start scene: %s",
            project.root
        );
        chibi_project_free(&project);
        return NULL;
    }

    /* The host owns shader setup; image sources must precede graph registration. */
    if (!render_image_mount_content(project.content, error, error_size)) {
        free(resolved_scene);
        chibi_project_free(&project);
        return NULL;
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
        free(resolved_scene);
        chibi_project_free(&project);
        return NULL;
    }
    authoring = app_scene_authoring_new_with_content_root(
        world,
        live,
        definition,
        editor_behavior_asset(graph_editor),
        graph_editor,
        NULL,
        NULL,
        runtime,
        project.content
    );
    bool scene_ready = authoring &&
                       app_scene_authoring_load_content(authoring) == 0 &&
                       app_scene_authoring_load(authoring, resolved_scene) == 0;
    if (!scene_ready || app_scene_authoring_apply_pending(authoring) != 0) {
        snprintf(
            error,
            error_size,
            "Could not initialize scene: %s",
            authoring ? app_scene_authoring_message(authoring) : "authoring unavailable"
        );
        app_scene_authoring_free(authoring);
        engine_free(runtime);
        game_live_world_free(live);
        editor_free(graph_editor);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        free(resolved_scene);
        chibi_project_free(&project);
        return NULL;
    }
    free(resolved_scene);
    chibi_project_free(&project);
    /* Put the shipped movement graph on screen on first launch. The graph is
     * still reachable through Files after switching documents. */
    if (!play && !open && game_live_find_definition(live, "TwinStickInputBehavior")) {
        (void)app_scene_authoring_open_behavior(authoring, "TwinStickInputBehavior");
    }
    if (open &&
        open_graph(authoring, graph_editor, open, graph_name, error, error_size) != 0) {
        app_scene_authoring_free(authoring);
        engine_free(runtime);
        game_live_world_free(live);
        editor_free(graph_editor);
        ecs_world_free(world);
        engine_viewport_free(viewport);
        return NULL;
    }
    editor_set_event_catalog(graph_editor, GRAPH_HOST_EVENTS, GRAPH_HOST_EVENT_COUNT);
    editor_set_ui_context(graph_editor, EDITOR_UI_CONTEXT_BEHAVIOR);
    engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT);
    engine_viewport_set_render_callback(viewport, app_scene_authoring_render, authoring);

    app_loop *loop = host->allocate(sizeof *loop);
    *loop = (app_loop){
        .host = host,
        .graph_editor = graph_editor,
        .runtime = runtime,
        .world = world,
        .live = live,
        .viewport = viewport,
        .authoring = authoring,
        .frames = frames,
        .screenshot_path = screenshot_path,
        .show_graph = open && screenshot_path,
        .capture_error = 0,
        .headless = headless,
        .fixed_dt = fixed_dt,
    };
    if (play) {
        loop->game = app_game_presentation_new(authoring, viewport, !headless);
        if (!loop->game) {
            snprintf(error, error_size, "Could not start game: %s", app_scene_authoring_message(authoring));
            app_cleanup(loop);
            return NULL;
        }
        return loop;
    }
    if (!checkpoint) {
        return loop;
    }
    yyjson_doc *doc = yyjson_read(checkpoint, strlen(checkpoint), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    const char *scene_json = yyjson_get_str(yyjson_obj_get(root, "scene"));
    const char *graph_json = yyjson_get_str(yyjson_obj_get(root, "graph"));
    bool valid = yyjson_get_int(yyjson_obj_get(root, "version")) == 1 &&
                 scene_json && graph_json &&
                 app_scene_authoring_restore_checkpoint(authoring, scene_json) == 0 &&
                 editor_restore_checkpoint(graph_editor, graph_json);
    if (valid) {
        app_scene_authoring_set_editor_working(authoring, editor_behavior_asset(graph_editor));
    }
    yyjson_doc_free(doc);
    if (valid) {
        return loop;
    }
    snprintf(error, error_size, "Workspace checkpoint is incompatible or invalid");
    app_cleanup(loop);
    return NULL;
}

static char *application_checkpoint(void *state)
{
    app_loop *loop = state;
    if (loop->game) {
        /* Reload starts Play from authored content, with launch arguments retained by the host. */
        const char saved[] = "{\"version\":1,\"mode\":\"play\"}";
        char *json = loop->host->allocate(sizeof saved);
        memcpy(json, saved, sizeof saved);
        return json;
    }
    char *scene = app_scene_authoring_checkpoint(loop->authoring);
    char *graph = editor_checkpoint(loop->graph_editor);
    char *json = NULL;
    if (scene && graph) {
        yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_obj_add_int(doc, root, "version", 1);
        yyjson_mut_obj_add_str(doc, root, "scene", scene);
        yyjson_mut_obj_add_str(doc, root, "graph", graph);
        char *encoded = yyjson_mut_write(doc, 0, NULL);
        if (encoded) {
            size_t size = strlen(encoded) + 1;
            json = loop->host->allocate(size);
            memcpy(json, encoded, size);
            free(encoded);
        }
        yyjson_mut_doc_free(doc);
    }
    free(scene);
    free(graph);
    return json;
}

static void application_activate(void *state)
{
    app_loop *loop = state;
#ifndef __EMSCRIPTEN__
    if (loop->game) {
        cf_app_set_imgui_render_target(cf_app_get_canvas());
    }
#endif
    app_input_initialize();
    if (!loop->game) {
        loop->studio = studio_new(loop->authoring, loop->viewport);
        if (loop->show_graph) {
            studio_maximize_graph(loop->studio);
        }
    }
}

static void application_deactivate(void *state)
{
    app_loop *loop = state;
    studio_free(loop->studio);
    loop->studio = NULL;
    app_input_shutdown();
}

static int application_frame(void *state)
{
    app_loop *loop = state;
    int finished = app_frame(loop);
    if (finished < 0) {
        return -1;
    }
    return finished ? (loop->capture_error ? -1 : 1) : 0;
}

static void application_destroy(void *state)
{
    app_cleanup(state);
}

CHIBI_APPLICATION_EXPORT const chibi_application_api *chibi_application()
{
    static const chibi_application_api api = {
        .abi = CHIBI_APPLICATION_ABI,
        .create = application_create,
        .checkpoint = application_checkpoint,
        .activate = application_activate,
        .deactivate = application_deactivate,
        .frame = application_frame,
        .destroy = application_destroy,
    };
    return &api;
}
