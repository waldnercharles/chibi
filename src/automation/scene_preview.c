#include "automation/controller.h"
#include "automation/render_preview.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "render/widgets.h"
#include "runtime/camera.h"
#include "runtime/scenes/prototype_preview.h"
#include "third_party/yyjson/yyjson.h"

#include <imgui/dcimgui.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct
{
    game_prototype_preview *preview;
    app_scene_renderer *renderer;
    engine_viewport *viewport;
    bool perspective, failed;
} scene_preview;

static void draw_scene(void *user)
{
    scene_preview *scene = user;
    ecs_world *world = game_prototype_preview_world(scene->preview);
    engine *runtime = game_prototype_preview_runtime(scene->preview);
    const game_particles *particles = game_prototype_preview_particles(scene->preview);
    if (scene->perspective) {
        render_arena_camera camera;
        engine_viewport_get_arena_camera(scene->viewport, &camera);
        scene->failed = !app_scene_renderer_draw3d(
            scene->renderer,
            world,
            particles,
            &camera,
            scene->viewport,
            runtime
        );
        return;
    }
    scene->failed = !app_scene_renderer_draw(
        scene->renderer,
        world,
        particles,
        scene->viewport,
        runtime
    );
}

typedef struct
{
    double median_ms, p95_ms, p99_ms;
    int samples, particles, entities;
    float scale;
} render_measurement;

static double seconds()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec * 1e-9;
}

static int compare_time(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static bool render_scene(
    const char *root,
    const char *request,
    int width,
    int height,
    int render_frames,
    render_measurement *measurement,
    char **png,
    char *error,
    size_t size
)
{
    CF_Result made = cf_make_app(
        "Scene preview",
        0,
        0,
        0,
        width,
        height,
        CF_APP_OPTIONS_NO_AUDIO_BIT | (render_frames > 2 ? 0 : CF_APP_OPTIONS_HIDDEN_BIT),
        NULL
    );
    if (cf_is_error(made)) {
        snprintf(error, size, "%s", made.details);
        return false;
    }
    cf_app_set_present_mode(CF_PRESENT_MODE_IMMEDIATE);
    cf_app_init_imgui();
    ImGui_GetIO()->IniFilename = NULL;
    ImGuiIO_SetAppAcceptingEvents(ImGui_GetIO(), false);
    cf_app_set_canvas_size(width, height);
    cf_app_set_imgui_render_target(cf_app_get_canvas());
    app_scene_renderer_initialize();
    scene_preview scene = { 0 };
    bool ok = chibi_automation_preview(root, request, &scene.preview, &scene.perspective, error, size) ==
              CHIBI_AUTOMATION_OK;
    if (ok) {
        scene.renderer = app_scene_renderer_new();
        scene.viewport = engine_viewport_new(width, height);
        ok = scene.renderer && scene.viewport;
        if (!ok) {
            snprintf(error, size, "Could not create scene renderer");
        }
    }
    if (ok) {
        engine_viewport_set_perspective(scene.viewport, scene.perspective);
        engine_viewport_view view;
        int camera = game_camera_view(
            game_prototype_preview_world(scene.preview),
            game_prototype_preview_runtime(scene.preview),
            &view
        );
        if (camera < 0) {
            snprintf(error, size, "GameCamera requires one enabled camera, a transform and positive view dimensions");
            ok = false;
        }
        if (camera > 0) {
            engine_viewport_set_view(scene.viewport, &view);
        }
        engine_viewport_set_render_callback(scene.viewport, draw_scene, &scene);
    }
    /* Captures settle ImGui for two frames. Performance runs first warm the
     * renderer with simulation frozen, then measure GPU-complete frame work.
     * The 60 Hz pacing wait is outside the measured interval. */
    double timings[3600];
    int count = 0;
    engine *runtime = ok ? game_prototype_preview_runtime(scene.preview) : NULL;
    double start_time = runtime ? engine_last_timing(runtime)->simulation_time : 0;
    int warmup = render_frames > 2 ? 240 : 0;
    for (int i = 0; ok && i < render_frames + warmup; i++) {
        double start = seconds();
        cf_app_update(NULL);
        if (render_frames > 2 && i >= warmup) {
            ok = game_prototype_preview_tick(scene.preview, start_time + (i - warmup + 1) / 60.0) ==
                 FLOW_OK;
        }
        engine_viewport_view camera_view;
        if (game_camera_view(game_prototype_preview_world(scene.preview), runtime, &camera_view) >
            0) {
            engine_viewport_set_view(scene.viewport, &camera_view);
        }
        app_scene_renderer_set_time(
            scene.renderer,
            (float)engine_last_timing(runtime)->simulation_time
        );
        /* The requested image dimensions are pixels, independent of the host display DPI. */
        ImGui_GetIO()->DisplayFramebufferScale = (ImVec2){ 1, 1 };
        ImGui_SetNextWindowPos((ImVec2){ 0, 0 }, ImGuiCond_Always);
        ImGui_SetNextWindowSize((ImVec2){ width, height }, ImGuiCond_Always);
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 0, 0 });
        ImGui_PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui_Begin("Scene", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImVec2 origin = ImGui_GetCursorScreenPos();
        engine_viewport_image(scene.viewport, width, height);
        engine *runtime = game_prototype_preview_runtime(scene.preview);
        render_widgets(
            game_prototype_preview_world(scene.preview),
            game_prototype_preview_entities(scene.preview),
            engine_visibility(runtime),
            (CF_V2){ origin.x, origin.y },
            (CF_V2){ width, height },
            true
        );
        ImGui_End();
        ImGui_PopStyleVarEx(2);
        if (render_frames > 2) {
            cf_gpu_sync();
        }
        cf_app_draw_onto_screen(true);
        if (i >= warmup + 16) {
            timings[count++] = (seconds() - start) * 1000;
        }

        if (render_frames > 2) {
            double spare_ms = (1.0 / 60 - (seconds() - start)) * 1000;
            if (spare_ms >= 1) {
                cf_sleep((int)spare_ms);
            }
        }

        if (scene.failed) {
            snprintf(error, size, "%s", app_scene_renderer_error(scene.renderer));
            if (!error[0]) {
                snprintf(error, size, "Scene or fog rendering failed");
            }
            ok = false;
        }
    }
    if (count) {
        qsort(timings, count, sizeof *timings, compare_time);
        *measurement = (render_measurement){ timings[count / 2],
                                             timings[(int)((count - 1) * .95)],
                                             timings[(int)((count - 1) * .99)],
                                             count };
    }
    if (ok) {
        measurement->particles = game_particles_count(
            game_prototype_preview_particles(scene.preview)
        );
        measurement->entities = ecs_world_live_count(
            game_prototype_preview_world(scene.preview)
        );
        measurement->scale = app_scene_renderer_hrc_scale(scene.renderer);
        ok = chibi_preview_readback(width, height, png, error, size);
    }
    if (scene.viewport) {
        engine_viewport_free(scene.viewport);
    }
    if (scene.renderer) {
        app_scene_renderer_free(scene.renderer);
    }
    game_prototype_preview_free(scene.preview);
    render_shader_catalog_clear();
    cf_destroy_app();
    return ok;
}

char *chibi_scene_preview_handle(const char *request, const char *default_root, int *exit_code)
{
    yyjson_doc *doc = yyjson_read(request, strlen(request), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    char error[512] = { 0 }, directory[PATH_MAX], canonical[PATH_MAX];
    yyjson_val *contract = yyjson_obj_get(root, "contract");
    yyjson_val *project_value = yyjson_obj_get(root, "project_root");
    if (!yyjson_is_obj(root) ||
        (contract && (!yyjson_is_int(contract) ||
                      yyjson_get_sint(contract) != CHIBI_AUTOMATION_CONTRACT))) {
        snprintf(error, sizeof error, "preview_scene requires an object with the supported contract version");
    }
    if (project_value &&
        (!yyjson_is_str(project_value) || !yyjson_get_len(project_value))) {
        snprintf(error, sizeof error, "project_root must be a non-empty string");
    }
    const char *project = yyjson_get_str(project_value);
    if (!project) {
        project = default_root;
    }
    int width = 640, height = 360;
    const char *keys[] = { "width", "height" };
    int *sides[] = { &width, &height };
    for (int i = 0; i < 2; i++) {
        yyjson_val *value = yyjson_obj_get(root, keys[i]);
        if (!value) {
            continue;
        }
        if (!yyjson_is_int(value) || yyjson_get_sint(value) < 64 ||
            yyjson_get_sint(value) > 1024) {
            snprintf(error, sizeof error, "%s must be an integer between 64 and 1024", keys[i]);
            break;
        }
        *sides[i] = (int)yyjson_get_sint(value);
    }
    int render_frames = 2;
    yyjson_val *render_count = yyjson_obj_get(root, "render_frames");
    if (render_count) {
        if (!yyjson_is_int(render_count) || yyjson_get_sint(render_count) < 2 ||
            yyjson_get_sint(render_count) > 3600) {
            snprintf(error, sizeof error, "render_frames must be between 2 and 3600");
        } else {
            render_frames = (int)yyjson_get_sint(render_count);
        }
    }
    render_measurement measurement = { 0 };
    char *png = NULL;
    bool ok = false;
    int status = error[0] ? CHIBI_AUTOMATION_INVALID_REQUEST
                          : CHIBI_AUTOMATION_EXECUTION_ERROR;
    if (!error[0]) {
        if (!project || !realpath(project, canonical) ||
            !getcwd(directory, sizeof directory) || chdir(canonical)) {
            snprintf(error, sizeof error, "Could not open project root");
        } else {
            ok = render_scene(canonical, request, width, height, render_frames, &measurement, &png, error, sizeof error);
            if (chdir(directory)) {
                abort();
            }
        }
    }
    yyjson_mut_doc *response = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *object = yyjson_mut_obj(response);
    yyjson_mut_doc_set_root(response, object);
    yyjson_mut_obj_add_bool(response, object, "ok", ok);
    yyjson_mut_obj_add_int(response, object, "contract", CHIBI_AUTOMATION_CONTRACT);
    yyjson_mut_obj_add_str(response, object, "operation", "preview_scene");
    if (ok) {
        yyjson_mut_val *result = yyjson_mut_obj_add_obj(response, object, "result");
        yyjson_mut_val *performance = yyjson_mut_obj_add_obj(response, result, "performance");
        yyjson_mut_obj_add_int(response, performance, "samples", measurement.samples);
        yyjson_mut_obj_add_int(response, performance, "warmup_frames", render_frames > 2 ? 240 : 0);
        yyjson_mut_obj_add_real(response, performance, "median_ms", measurement.median_ms);
        yyjson_mut_obj_add_real(response, performance, "p95_ms", measurement.p95_ms);
        yyjson_mut_obj_add_real(response, performance, "p99_ms", measurement.p99_ms);
        yyjson_mut_obj_add_int(response, performance, "particles", measurement.particles);
        yyjson_mut_obj_add_int(response, performance, "entities", measurement.entities);
        yyjson_mut_obj_add_real(response, performance, "hrc_scale", measurement.scale);
        yyjson_mut_val *preview = yyjson_mut_obj_add_obj(response, result, "preview");
        yyjson_mut_obj_add_str(response, preview, "mime_type", "image/png");
        yyjson_mut_obj_add_int(response, preview, "width", width);
        yyjson_mut_obj_add_int(response, preview, "height", height);
        yyjson_mut_obj_add_strcpy(response, preview, "data", png);
    } else {
        yyjson_mut_obj_add_strcpy(response, object, "error", error);
    }
    if (exit_code) {
        *exit_code = ok ? CHIBI_AUTOMATION_OK : status;
    }
    free(png);
    yyjson_doc_free(doc);
    char *json = yyjson_mut_write(response, 0, NULL);
    yyjson_mut_doc_free(response);
    return json;
}
