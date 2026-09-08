#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "runtime/generated.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static behavior_asset *read_library()
{
    FILE *f = fopen("content/shader-libraries/ShaderMath.json", "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *json = malloc((size_t)size + 1);
    assert(json);
    assert(fread(json, 1, (size_t)size, f) == (size_t)size);
    json[size] = 0;
    fclose(f);
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    assert(asset);
    return asset;
}
static void shader_scene_ui()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    game_live_definition *definition = game_live_definition_new(
        "Before shader",
        behavior_asset_new("Before shader")
    );
    assert(game_live_add_definition(live, definition) == 0);
    editor *graph = editor_new(behavior_asset_new("Before shader"));
    engine *runtime = engine_new(world, live);
    engine_viewport *viewport = engine_viewport_new(800, 600);
    app_scene_authoring *scene = app_scene_authoring_new(
        world,
        live,
        definition,
        editor_behavior_asset(graph),
        graph,
        NULL,
        NULL,
        runtime
    );
    assert(app_scene_authoring_load_content(scene) == 0);
    assert(app_scene_authoring_load(scene, "content/scenes/ShaderDemo.json") == 0);
    engine_viewport_set_render_callback(viewport, app_scene_authoring_render, scene);
    editor_ui_reset_layout();
    for (int frame = 0; frame < 30; frame++) {
        cf_app_update(NULL);
        app_input_frame();
        editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
        editor_tick(graph, frame / 60.0);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_SCENE, true);
        editor_prepare_docking();
        app_scene_authoring_draw_viewport(scene, viewport);
        app_scene_authoring_draw(scene);
        editor_draw(graph);
        cf_app_draw_onto_screen(true);
    }
    assert(app_save_screenshot("/tmp/shader-scene-native.png") == 0);
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    for (int i = 0; i < acount(scene->actors); i++) {
        const GameTransform
            *position = GAME_GameTransform_get_const(world, scene->actors[i].entity);
        assert(fabsf(position->x - camera.center_x) < camera.span_width * .5f);
        assert(fabsf(-position->y - camera.center_y) < camera.span_height * .5f);
    }
    app_scene_authoring_free(scene);
    engine_viewport_free(viewport);
    engine_free(runtime);
    game_live_world_free(live);
    editor_free(graph);
    ecs_world_free(world);
}

/* Focused native drawing regression. Uses the real Cute/ImGui frame and GPU preview. */
int shader_ui_run(const char *program)
{
    CF_Result result = cf_make_app(
        "Shader UI regression",
        0,
        0,
        0,
        2056,
        1286,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "Shader UI app: %s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    cf_app_init_imgui();
    app_input_initialize();
    editor_ui_initialize(false);
    ImGui_GetIO()->IniFilename = NULL;
    cf_app_set_imgui_render_target(cf_app_get_canvas());
    editor_ui_set_framebuffer_target(true);
    editor_ui_set_framebuffer_size(2056, 1286);
    assert(sizeof(ImDrawIdx) == 4);
    shader_scene_ui();
    behavior_asset *library = read_library();
    const behavior_asset *catalog[] = { library };
    editor *e = editor_new(behavior_asset_new("Before shader"));
    assert(e);
    editor_set_inspector_active(e, true);
    editor_set_graph_visible(e, 1);
    editor_set_ui_context(e, EDITOR_UI_CONTEXT_BEHAVIOR);
    const char *paths[] = { "content/materials/PulseRing.json",
                            "content/materials/OutlineGradient.json",
                            "content/shapes/SmoothUnion.json" };
    for (int frame = 0; frame < 150; frame++) {
        cf_app_update(NULL);
        app_input_frame();
        editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
        if (frame == 20 || frame == 60 || frame == 100) {
            int index = (frame - 20) / 40;
            assert(editor_load(e, paths[index]) == 0);
            editor_set_asset_catalog(e, catalog, 1);
        }
        editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, true);
        editor_set_inspector_active(e, true);
        editor_set_ui_context(e, EDITOR_UI_CONTEXT_BEHAVIOR);
        if (frame == 24 || frame == 64 || frame == 104) {
            ed_zoom_fit(e, false);
        }
        editor_tick(e, frame / 60.0);
        editor_prepare_docking();
        editor_draw(e);
        char *snapshot = behavior_asset_save_json(e->bp);
        assert(snapshot);
        free(snapshot);
        cf_app_draw_onto_screen(true);
        if (frame == 59 || frame == 99 || frame == 139) {
            assert(e->shader_preview && e->shader_viewport);
            char path[128];
            snprintf(path, sizeof path, "/tmp/shader-ui-native-%d.png", (frame - 59) / 40);
            assert(app_save_screenshot(path) == 0);
        }
    }
    editor_free(e);
    behavior_asset_free(library);
    app_input_shutdown();
    cf_destroy_app();
    puts("shader UI PASS: native material/shape document replacement, graph drawing and GPU previews");
    return 0;
}
