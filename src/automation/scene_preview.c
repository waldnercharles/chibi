#include "automation/controller.h"
#include "automation/gpu_capture.h"
#include "automation/render_preview.h"
#include "diagnostics/tracy.h"
#include "platform/cute.h"
#include "render/gpu_timing.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
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
    double simulation_ms, render_ms;
    int samples, particles, entities;
    float scale;
    const char *backend;
    char *initial_state, *final_state;
} render_measurement;

typedef struct
{
    int width, height, frames, capture_frame;
    bool frozen, pass_timing;
    const char *capture_path;
    /* Optional raw RGBA8 file receiving every measured frame. */
    const char *frames_path;
} preview_options;

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
    const preview_options *options,
    render_measurement *measurement,
    char **png,
    char *error,
    size_t size
)
{
    int width = options->width, height = options->height,
        render_frames = options->frames;
    CF_Result made = cf_make_app(
        "Scene preview",
        0,
        0,
        0,
        width,
        height,
        CF_APP_OPTIONS_NO_AUDIO_BIT |
            (render_frames > 2 ? 0 : CF_APP_OPTIONS_HIDDEN_BIT) |
            (options->capture_path ? CF_APP_OPTIONS_GFX_METAL_BIT : 0),
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
    chibi_preview_run *run = NULL;
    bool ok = chibi_automation_preview(root, request, &run, &scene.perspective, error, size) ==
              CHIBI_AUTOMATION_OK;
    if (ok) {
        scene.preview = chibi_preview_runtime(run);
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
        engine *preview_runtime = game_prototype_preview_runtime(scene.preview);
        char content_root[1024];
        snprintf(content_root, sizeof content_root, "%s/content", root);
        engine_viewport_set_ui(
            scene.viewport,
            game_prototype_preview_world(scene.preview),
            game_prototype_preview_entities(scene.preview),
            engine_ui(preview_runtime),
            content_root
        );
    }
    /* Captures settle ImGui for two frames. Performance runs first warm the
     * renderer with simulation frozen, then measure GPU-complete frame work.
     * The 60 Hz pacing wait is outside the measured interval. */
    double timings[3600];
    int count = 0;
    double simulation_ms = 0, render_ms = 0;
    engine *runtime = ok ? game_prototype_preview_runtime(scene.preview) : NULL;
    int warmup = render_frames > 2 ? 240 : 0;
    render_gpu_timing_enable(options->pass_timing);
    FILE *frames = NULL;
    if (ok && options->frames_path && !(frames = fopen(options->frames_path, "wb"))) {
        snprintf(error, size, "Could not open frame capture file");
        ok = false;
    }
    if (ok) {
        measurement->initial_state = chibi_preview_state(run);
        measurement->backend = cf_backend_type_to_string(cf_query_backend());
    }
    for (int i = 0; ok && i < render_frames + warmup; i++) {
        bool capture = options->capture_path && i == warmup + options->capture_frame;
        if (capture && !chibi_gpu_capture_begin(options->capture_path, error, size)) {
            ok = false;
            break;
        }
        CHIBI_TRACE_ZONE_BEGIN(frame_zone, "Scene preview frame");
        double start = seconds();
        CHIBI_TRACE_ZONE_BEGIN(window_zone, "Preview window update");
        cf_app_update(NULL);
        CHIBI_TRACE_ZONE_END(window_zone);
        double simulation_start = seconds();
        bool advance = render_frames > 2 && i >= warmup && !options->frozen;
        if (advance) {
            CHIBI_TRACE_ZONE_BEGIN(simulation_zone, "Preview simulation");
            ok = chibi_preview_step(run, error, size) == CHIBI_AUTOMATION_OK;
            CHIBI_TRACE_ZONE_END(simulation_zone);
        }
        double simulation_elapsed = advance ? (seconds() - simulation_start) * 1000
                                            : 0;
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
        engine_viewport_image(scene.viewport, width, height);
        ImGui_End();
        ImGui_PopStyleVarEx(2);
        CHIBI_TRACE_ZONE_BEGIN(present_zone, "Preview present / GPU wait");
        if (render_frames > 2) {
            cf_gpu_sync();
        }
        cf_app_draw_onto_screen(true);
        CHIBI_TRACE_ZONE_END(present_zone);
        if (capture && !chibi_gpu_capture_end(options->capture_path, error, size)) {
            ok = false;
        }
        CHIBI_TRACE_FRAME_MARK("Scene preview frame");
        if (i == warmup + 15) {
            render_gpu_timing_clear();
        }
        if (frames && i >= warmup + 16 &&
            !chibi_preview_readback_raw(width, height, frames, error, size)) {
            ok = false;
        }
        if (i >= warmup + 16) {
            double elapsed = (seconds() - start) * 1000;
            timings[count++] = elapsed;
            simulation_ms += simulation_elapsed;
            render_ms += elapsed - simulation_elapsed;
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
        CHIBI_TRACE_ZONE_END(frame_zone);
    }
    if (frames && fclose(frames) && ok) {
        snprintf(error, size, "Could not finish frame capture file");
        ok = false;
    }
    if (count) {
        qsort(timings, count, sizeof *timings, compare_time);
        measurement->median_ms = timings[count / 2];
        measurement->p95_ms = timings[(count * 95 + 99) / 100 - 1];
        measurement->p99_ms = timings[(count * 99 + 99) / 100 - 1];
        measurement->simulation_ms = simulation_ms / count;
        measurement->render_ms = render_ms / count;
        measurement->samples = count;
    }
    if (ok) {
        measurement->final_state = chibi_preview_state(run);
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
    chibi_preview_free(run);
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
            yyjson_get_sint(value) > 4096) {
            snprintf(error, sizeof error, "%s must be an integer between 64 and 4096", keys[i]);
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
    yyjson_val *freeze = yyjson_obj_get(root, "freeze_simulation");
    yyjson_val *pass_timing = yyjson_obj_get(root, "pass_timing");
    yyjson_val *frames_value = yyjson_obj_get(root, "capture_frames");
    const char *frames_path = yyjson_get_str(frames_value);
    if (frames_value && (!frames_path || frames_path[0] != '/' ||
                         render_frames <= 2 || strlen(frames_path) >= PATH_MAX)) {
        snprintf(error, sizeof error, "capture_frames must be an absolute path and requires a performance run");
    }
    if (pass_timing && (!yyjson_is_bool(pass_timing) ||
                        (yyjson_get_bool(pass_timing) && render_frames <= 2))) {
        snprintf(error, sizeof error, "pass_timing must be a boolean and requires a performance run");
    }
    yyjson_val *capture = yyjson_obj_get(root, "metal_capture");
    yyjson_val *capture_frame = yyjson_obj_get(root, "capture_frame");
    const char *capture_path = yyjson_get_str(capture);
    int capture_at = capture_frame ? (int)yyjson_get_sint(capture_frame) : 16;
    if (freeze && !yyjson_is_bool(freeze)) {
        snprintf(error, sizeof error, "freeze_simulation must be a boolean");
    }
    if (capture && (!capture_path || capture_path[0] != '/' ||
                    strlen(capture_path) >= PATH_MAX || strlen(capture_path) < 10 ||
                    strcmp(capture_path + strlen(capture_path) - 9, ".gputrace"))) {
        snprintf(error, sizeof error, "metal_capture must be an absolute .gputrace path");
    }
    if ((capture_frame && (!capture || !yyjson_is_int(capture_frame) ||
                           yyjson_get_sint(capture_frame) < 0 ||
                           yyjson_get_sint(capture_frame) >= render_frames)) ||
        (capture && (render_frames <= 2 || capture_at >= render_frames))) {
        snprintf(error, sizeof error, "capture_frame must select a rendered frame in a performance run");
    }
    preview_options options = { .width = width,
                                .height = height,
                                .frames = render_frames,
                                .capture_frame = capture_at,
                                .frozen = yyjson_get_bool(freeze),
                                .pass_timing = yyjson_get_bool(pass_timing),
                                .capture_path = capture_path,
                                .frames_path = frames_path };
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
            ok = render_scene(canonical, request, &options, &measurement, &png, error, sizeof error);
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
        const char *states[] = { measurement.initial_state, measurement.final_state };
        const char *names[] = { "initial_state", "final_state" };
        for (int i = 0; i < 2; i++) {
            yyjson_doc *state = yyjson_read(states[i], strlen(states[i]), 0);
            yyjson_mut_obj_add_val(
                response,
                result,
                names[i],
                yyjson_val_mut_copy(response, yyjson_doc_get_root(state))
            );
            yyjson_doc_free(state);
        }
        yyjson_mut_obj_add_str(response, result, "backend", measurement.backend);
        if (capture_path) {
            yyjson_mut_obj_add_strcpy(response, result, "metal_capture", capture_path);
        }
        yyjson_mut_val *performance = yyjson_mut_obj_add_obj(response, result, "performance");
        bool instrumented = capture_path != NULL || options.pass_timing || frames_path;
#ifdef CHIBI_ENABLE_TRACY
        instrumented = true;
#endif
        yyjson_mut_obj_add_bool(response, performance, "frozen", options.frozen);
        yyjson_mut_obj_add_bool(response, performance, "instrumented", instrumented);
        yyjson_mut_obj_add_int(response, performance, "samples", measurement.samples);
        yyjson_mut_obj_add_int(response, performance, "warmup_frames", render_frames > 2 ? 240 : 0);
        yyjson_mut_obj_add_real(response, performance, "median_ms", measurement.median_ms);
        yyjson_mut_obj_add_real(response, performance, "p95_ms", measurement.p95_ms);
        yyjson_mut_obj_add_real(response, performance, "p99_ms", measurement.p99_ms);
        yyjson_mut_obj_add_real(
            response,
            performance,
            "simulation_average_ms",
            measurement.simulation_ms
        );
        yyjson_mut_obj_add_real(
            response,
            performance,
            "render_average_ms",
            measurement.render_ms
        );
        yyjson_mut_obj_add_int(response, performance, "particles", measurement.particles);
        yyjson_mut_obj_add_int(response, performance, "entities", measurement.entities);
        yyjson_mut_obj_add_real(response, performance, "hrc_scale", measurement.scale);
        if (options.pass_timing) {
            /* Average milliseconds per rendered frame, in submission order. */
            yyjson_mut_val *passes = yyjson_mut_obj_add_arr(response, performance, "passes");
            int count;
            const render_gpu_pass *pass = render_gpu_timing_passes(&count);
            for (int i = 0; i < count; i++) {
                yyjson_mut_val *item = yyjson_mut_arr_add_obj(response, passes);
                yyjson_mut_obj_add_str(response, item, "name", pass[i].name);
                yyjson_mut_obj_add_real(
                    response,
                    item,
                    "ms",
                    measurement.samples ? pass[i].total_ms / measurement.samples : 0
                );
            }
        }
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
    free(measurement.initial_state);
    free(measurement.final_state);
    yyjson_doc_free(doc);
    char *json = yyjson_mut_write(response, 0, NULL);
    yyjson_mut_doc_free(response);
    return json;
}
