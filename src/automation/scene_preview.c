#include "automation/controller.h"
#include "automation/gpu_capture.h"
#include "automation/render_preview.h"
#include "diagnostics/tracy.h"
#include "platform/cute.h"
#include "render/gpu_clock.h"
#include "render/gpu_timing.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "render/ui.h"
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
            runtime,
            (render_skip){ 0 }
        );
        return;
    }
    scene->failed = !app_scene_renderer_draw(
        scene->renderer,
        world,
        particles,
        scene->viewport,
        runtime,
        (render_skip){ 0 }
    );
}

typedef struct
{
    double median_ms, p95_ms, p99_ms;
    double simulation_ms, render_ms;
    int samples, particles, entities, material_pieces;
    float resolution;
    double scene_gpu_ms;
    const char *backend;
    bool pass_timestamps;
    char *initial_state, *final_state;
    render_mesh_material_work mesh_work;
    render_model_stats model_before, model_measured, model_after;
    render_horizon_work horizon_work;
    render_surface_work surface_work;
} render_measurement;

typedef struct
{
    int width, height, frames, capture_frame, sdf_batch_limit;
    bool frozen, pass_timing, pass_detail;
    bool quality_override, shadow_override, sdf_trace_debug;
    bool hidden_rejection, spatial_scratch;
    GameSurfaceQuality quality;
    GameHRCShadowMode shadow_mode;
    unsigned horizon_reuse_mask;
    GameHRC2D horizon_reuse;
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
        app_scene_renderer_set_sdf_batch_limit(scene.renderer, options->sdf_batch_limit);
        if (options->quality_override) {
            app_scene_renderer_set_surface_quality(scene.renderer, options->quality);
        }
        if (options->shadow_override) {
            app_scene_renderer_set_shadow_mode(scene.renderer, options->shadow_mode);
        }
        if (options->horizon_reuse_mask) {
            app_scene_renderer_set_horizon_reuse(
                scene.renderer,
                options->horizon_reuse_mask,
                options->horizon_reuse
            );
        }
        app_scene_renderer_set_sdf_trace_debug(scene.renderer, options->sdf_trace_debug);
        app_scene_renderer_set_hidden_rejection(scene.renderer, options->hidden_rejection);
        app_scene_renderer_set_spatial_scratch(scene.renderer, options->spatial_scratch);
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
        ui_world ui = { .ecs = game_prototype_preview_world(scene.preview),
                        .page = chibi_preview_page(run),
                        .model = engine_ui(preview_runtime),
                        .live = game_prototype_preview_live(scene.preview) };
        engine_viewport_set_ui(scene.viewport, &ui, content_root);
    }
    /* Captures settle ImGui for two frames. Performance runs first warm the
     * renderer with simulation frozen, then measure GPU-complete frame work.
     * The 60 Hz pacing wait is outside the measured interval. */
    double timings[3600];
    int count = 0;
    double simulation_ms = 0, render_ms = 0;
    engine *runtime = ok ? game_prototype_preview_runtime(scene.preview) : NULL;
    int warmup = render_frames > 2 ? 240 : 0;
    /* Command buffer timestamps avoid the per-pass waits of sync timing. */
    measurement->pass_timestamps = options->pass_timing && render_gpu_clock_install();
    render_gpu_timing_enable(
        !options->pass_timing          ? RENDER_GPU_TIMING_OFF
        : measurement->pass_timestamps ? RENDER_GPU_TIMING_TIMESTAMPS
                                       : RENDER_GPU_TIMING_SYNC,
        options->pass_detail
    );
    FILE *frames = NULL;
    if (ok && options->frames_path && !(frames = fopen(options->frames_path, "wb"))) {
        snprintf(error, size, "Could not open frame capture file");
        ok = false;
    }
    if (ok) {
        measurement->initial_state = chibi_preview_state(run);
        measurement->backend = cf_backend_type_to_string(cf_query_backend());
        measurement->model_before = render_shader_model_stats();
    }
    for (int i = 0; ok && i < render_frames + warmup; i++) {
        if (i == warmup + 16) {
            measurement->model_measured = render_shader_model_stats();
        }
        bool capture = options->capture_path && i == warmup + options->capture_frame;
        if (capture && !chibi_gpu_capture_begin(options->capture_path, error, size)) {
            ok = false;
            break;
        }
        CHIBI_TRACE_ZONE_BEGIN(frame_zone, "Scene preview frame");
        double start = seconds();
        CHIBI_TRACE_ZONE_BEGIN(window_zone, "Preview window update");
        cf_app_update(NULL);
        /* Window DPI events can recreate the app canvas at display resolution.
         * A preview request specifies pixels, including per-frame readbacks. */
        if (cf_app_get_canvas_width() != width || cf_app_get_canvas_height() != height) {
            cf_app_set_canvas_size(width, height);
        }
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
        /* Its documents step a fixed frame each, as its simulation does. */
        engine_viewport_set_time(scene.viewport, i * ENGINE_FIXED_SECONDS);
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
        render_radiance_status lighting = { 0 };
        app_scene_renderer_lighting_status(scene.renderer, &lighting);
        measurement->resolution = lighting.resolution;
        measurement->scene_gpu_ms = lighting.gpu_ms;
        measurement->mesh_work = app_scene_renderer_mesh_work(scene.renderer);
        measurement->model_after = render_shader_model_stats();
        measurement->horizon_work = app_scene_renderer_horizon_work(scene.renderer);
        measurement->surface_work = app_scene_renderer_surface_work(scene.renderer);
        measurement->material_pieces = app_scene_renderer_material_pieces(scene.renderer);
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

static bool surface_quality_options(yyjson_val *root, preview_options *options, char *error, size_t size)
{
    yyjson_val *quality = yyjson_obj_get(root, "surface_quality");
    options->quality_override = quality != NULL;
    if (quality) {
        yyjson_val *samples = yyjson_obj_get(quality, "samples");
        yyjson_val *pixel = yyjson_obj_get(quality, "pixel_error");
        yyjson_val *normal = yyjson_obj_get(quality, "normal_samples");
        yyjson_val *lod = yyjson_obj_get(quality, "lod");
        if (!yyjson_is_obj(quality) ||
            (samples && (!yyjson_is_int(samples) || (yyjson_get_sint(samples) != 1 &&
                                                     yyjson_get_sint(samples) != 2))) ||
            (pixel && (!yyjson_is_num(pixel) || yyjson_get_num(pixel) < 0 ||
                       yyjson_get_num(pixel) > 1)) ||
            (normal && (!yyjson_is_int(normal) || (yyjson_get_sint(normal) != 0 &&
                                                   yyjson_get_sint(normal) != 4 &&
                                                   yyjson_get_sint(normal) != 6))) ||
            (lod && !yyjson_is_bool(lod))) {
            snprintf(error, size, "surface_quality requires samples 1 or 2, pixel_error 0 to 1, normal_samples 0, 4 or 6, and boolean lod");
            return false;
        }
        options->quality = (GameSurfaceQuality){
            .resolution = samples && yyjson_get_sint(samples) == 1
                            ? GAME_SURFACE_FULL
                            : GAME_SURFACE_SUPERSAMPLED,
            .sdf_pixel_error = pixel ? (float)yyjson_get_num(pixel) : 0,
            .sdf_normal_mode = normal && yyjson_get_sint(normal) == 0 ? GAME_SDF_NORMAL_ANALYTIC
                             : normal && yyjson_get_sint(normal) == 4
                                 ? GAME_SDF_NORMAL_TETRAHEDRAL
                                 : GAME_SDF_NORMAL_CENTRAL,
            .disable_sdf_lod = lod && !yyjson_get_bool(lod)
        };
    }
    yyjson_val *directions = yyjson_obj_get(root, "horizon_directions");
    options->shadow_override = directions != NULL;
    if (directions) {
        int64_t value = yyjson_get_sint(directions);
        if (!yyjson_is_int(directions) || (value != 0 && value != 4 && value != 8)) {
            snprintf(error, size, "horizon_directions must be 0, 4 or 8");
            return false;
        }
        options->shadow_mode = value == 8 ? GAME_HRC_SHADOW_HORIZON8
                             : value == 4 ? GAME_HRC_SHADOW_HORIZON4
                                          : GAME_HRC_SHADOW_REFERENCE;
    }
    yyjson_val *reuse = yyjson_obj_get(root, "horizon_reuse");
    if (reuse) {
        if (!yyjson_is_obj(reuse)) {
            snprintf(error, size, "horizon_reuse must be an object");
            return false;
        }
        const char *names[] = { "fuse_relief", "cache", "reproject", "hierarchy" };
        bool *values[] = { &options->horizon_reuse.horizon_fuse_relief,
                           &options->horizon_reuse.horizon_cache,
                           &options->horizon_reuse.horizon_reproject,
                           &options->horizon_reuse.horizon_hierarchy };
        size_t index, count;
        yyjson_val *key, *value;
        yyjson_obj_foreach(reuse, index, count, key, value)
        {
            int field = -1;
            for (int i = 0; i < 4; i++) {
                if (!strcmp(yyjson_get_str(key), names[i])) {
                    field = i;
                }
            }
            if (field < 0 || !yyjson_is_bool(value)) {
                snprintf(error, size, "horizon_reuse supports only boolean fuse_relief, cache, reproject and hierarchy");
                return false;
            }
            options->horizon_reuse_mask |= 1u << field;
            *values[field] = yyjson_get_bool(value);
        }
    }
    const char *names[] = { "hidden_rejection", "spatial_scratch" };
    bool *values[] = { &options->hidden_rejection, &options->spatial_scratch };
    for (int i = 0; i < 2; i++) {
        yyjson_val *value = yyjson_obj_get(root, names[i]);
        if (value && !yyjson_is_bool(value)) {
            snprintf(error, size, "%s must be a boolean", names[i]);
            return false;
        }
        *values[i] = value ? yyjson_get_bool(value) : true;
    }
    yyjson_val *debug = yyjson_obj_get(root, "sdf_trace_debug");
    if (debug && !yyjson_is_bool(debug)) {
        snprintf(error, size, "sdf_trace_debug must be a boolean");
        return false;
    }
    options->sdf_trace_debug = yyjson_get_bool(debug);
    return true;
}

static void add_model_work(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    render_model_stats before,
    render_model_stats after
)
{
    yyjson_mut_obj_add_int(
        doc,
        object,
        "source_compilations",
        after.compilations - before.compilations
    );
    yyjson_mut_obj_add_int(
        doc,
        object,
        "parameter_updates",
        after.parameter_updates - before.parameter_updates
    );
    yyjson_mut_obj_add_int(
        doc,
        object,
        "bounds_updates",
        after.bounds_updates - before.bounds_updates
    );
    yyjson_mut_obj_add_int(doc, object, "cache_hits", after.cache_hits - before.cache_hits);
    yyjson_mut_obj_add_int(
        doc,
        object,
        "setter_validations",
        after.setter_validations - before.setter_validations
    );
    yyjson_mut_obj_add_uint(doc, object, "program_revision_before", before.program_revision);
    yyjson_mut_obj_add_uint(doc, object, "program_revision_after", after.program_revision);
    yyjson_mut_obj_add_uint(doc, object, "parameter_revision_before", before.parameter_revision);
    yyjson_mut_obj_add_uint(doc, object, "parameter_revision_after", after.parameter_revision);
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
    yyjson_val *batch_limit = yyjson_obj_get(root, "sdf_batch_limit");
    if (batch_limit && (!yyjson_is_int(batch_limit) || yyjson_get_sint(batch_limit) < 0 ||
                        yyjson_get_sint(batch_limit) > RENDER_SDF_BATCH_LIMIT)) {
        snprintf(error, sizeof error, "sdf_batch_limit must be an integer between 0 and %d", RENDER_SDF_BATCH_LIMIT);
    }
    yyjson_val *pass_timing = yyjson_obj_get(root, "pass_timing");
    yyjson_val *pass_detail = yyjson_obj_get(root, "pass_timing_detail");
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
    if (pass_detail &&
        (!yyjson_is_bool(pass_detail) || !yyjson_get_bool(pass_timing))) {
        snprintf(error, sizeof error, "pass_timing_detail must be a boolean and requires pass_timing");
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
    preview_options options = {
        .width = width,
        .height = height,
        .frames = render_frames,
        .capture_frame = capture_at,
        .sdf_batch_limit = batch_limit ? (int)yyjson_get_sint(batch_limit) : 0,
        .frozen = yyjson_get_bool(freeze),
        .pass_timing = yyjson_get_bool(pass_timing),
        .pass_detail = yyjson_get_bool(pass_detail),
        .capture_path = capture_path,
        .frames_path = frames_path
    };
    render_measurement measurement = { 0 };
    if (!error[0]) {
        surface_quality_options(root, &options, error, sizeof error);
    }
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
        bool instrumented = capture_path != NULL || options.pass_timing ||
                            frames_path || options.sdf_trace_debug;
        const char *metal_hud = getenv("MTL_HUD_ENABLED");
        instrumented |= metal_hud && !strcmp(metal_hud, "1");
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
        /* The last frame's rasterized shapes and mask pieces: distant masks
         * draw coarser levels (render/material_lod.h). */
        yyjson_mut_obj_add_int(
            response,
            performance,
            "material_pieces",
            measurement.material_pieces
        );
        yyjson_mut_obj_add_real(
            response,
            performance,
            "hrc_resolution",
            measurement.resolution
        );
        /* The last frame's scene GPU time, or -1 without timestamps. */
        yyjson_mut_obj_add_real(
            response,
            performance,
            "scene_gpu_ms",
            measurement.scene_gpu_ms
        );
        yyjson_mut_val *work = yyjson_mut_obj_add_obj(response, performance, "mesh_work");
        yyjson_mut_obj_add_str(response, work, "scope", "last rendered frame surface submissions");
        yyjson_mut_obj_add_int(
            response,
            work,
            "sdf_batch_limit",
            options.sdf_batch_limit ? options.sdf_batch_limit : RENDER_SDF_BATCH_LIMIT
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "draw_calls",
            measurement.mesh_work.draw_calls
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "sdf_draw_calls",
            measurement.mesh_work.sdf_draw_calls
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "sdf_instances",
            measurement.mesh_work.sdf_instances
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "sdf_uploads",
            measurement.mesh_work.sdf_uploads
        );
        yyjson_mut_obj_add_uint(
            response,
            work,
            "sdf_upload_bytes",
            measurement.mesh_work.sdf_upload_bytes
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "surface_builds",
            measurement.mesh_work.surface_builds
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "surface_reuses",
            measurement.mesh_work.surface_reuses
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "sdf_program_prepares",
            measurement.mesh_work.sdf_program_prepares
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "sdf_lod_instances",
            measurement.mesh_work.sdf_lod_instances
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "surface_pixels",
            measurement.mesh_work.surface_pixels
        );
        yyjson_mut_obj_add_uint(
            response,
            work,
            "sdf_pair_probes",
            measurement.mesh_work.sdf_pair_probes
        );
        yyjson_mut_obj_add_uint(
            response,
            work,
            "sdf_parameter_probes",
            measurement.mesh_work.sdf_parameter_probes
        );
        yyjson_mut_obj_add_uint(
            response,
            work,
            "sdf_parameter_upload_bytes",
            measurement.mesh_work.sdf_parameter_upload_bytes
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "sdf_parameter_rows",
            measurement.mesh_work.sdf_parameter_rows
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "sdf_parameter_uploads",
            measurement.mesh_work.sdf_parameter_uploads
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "surface_capacity_pixels",
            measurement.mesh_work.surface_capacity_pixels
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "scratch_groups",
            measurement.mesh_work.scratch_groups
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "composition_pixels",
            measurement.mesh_work.composition_pixels
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "depth_copies",
            measurement.mesh_work.depth_copies
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "depth_copy_pixels",
            measurement.mesh_work.depth_copy_pixels
        );
        yyjson_mut_val *surface = yyjson_mut_obj_add_obj(response, performance, "surface_work");
        yyjson_mut_obj_add_str(response, surface, "scope", "submission_upper_bounds");
        yyjson_mut_obj_add_int(
            response,
            surface,
            "mode",
            measurement.surface_work.mode
        );
        yyjson_mut_obj_add_int(
            response,
            surface,
            "cells",
            measurement.surface_work.cells
        );
        yyjson_mut_obj_add_int(
            response,
            surface,
            "block",
            measurement.surface_work.block
        );
        yyjson_mut_obj_add_uint(
            response,
            surface,
            "candidates",
            measurement.surface_work.candidates
        );
        yyjson_mut_obj_add_uint(
            response,
            surface,
            "visibility_taps",
            measurement.surface_work.visibility_taps
        );
        yyjson_mut_obj_add_uint(
            response,
            surface,
            "blur_quadrant_fetches",
            measurement.surface_work.blur_quadrant_fetches
        );
        yyjson_mut_val *horizon = yyjson_mut_obj_add_obj(response, performance, "horizon_work");
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "builds",
            measurement.horizon_work.builds
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "pixels",
            measurement.horizon_work.pixels
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "directions",
            measurement.horizon_work.directions
        );
        yyjson_mut_obj_add_uint(
            response,
            horizon,
            "canvas_bytes",
            measurement.horizon_work.canvas_bytes
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "pack_passes",
            measurement.horizon_work.pack_passes
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "fused_packs",
            measurement.horizon_work.fused_packs
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "dirty_passes",
            measurement.horizon_work.dirty_passes
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "reuse_passes",
            measurement.horizon_work.reuse_passes
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "reproject_passes",
            measurement.horizon_work.reproject_passes
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "hierarchy_passes",
            measurement.horizon_work.hierarchy_passes
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "tiles_x",
            measurement.horizon_work.tiles_x
        );
        yyjson_mut_obj_add_int(
            response,
            horizon,
            "tiles_y",
            measurement.horizon_work.tiles_y
        );
        yyjson_mut_obj_add_int(
            response,
            work,
            "sdf_program_count",
            measurement.mesh_work.sdf_program_count
        );
        yyjson_mut_obj_add_uint(
            response,
            work,
            "sdf_program_compilations",
            measurement.mesh_work.sdf_program_compilations
        );
        yyjson_mut_val *model_work = yyjson_mut_obj_add_obj(response, performance, "model_work");
        yyjson_mut_obj_add_str(response, model_work, "scope", "render loop including warmup; loading and initial simulation excluded");
        yyjson_mut_obj_add_int(
            response,
            model_work,
            "compilations_before_render",
            measurement.model_before.compilations
        );
        add_model_work(
            response,
            model_work,
            measurement.model_before,
            measurement.model_after
        );
        if (measurement.samples) {
            yyjson_mut_val *measured = yyjson_mut_obj_add_obj(response, model_work, "measured_frames");
            add_model_work(
                response,
                measured,
                measurement.model_measured,
                measurement.model_after
            );
        }
        if (options.pass_timing) {
            /* Average milliseconds per rendered frame, in submission order. */
            yyjson_mut_obj_add_str(
                response,
                performance,
                "pass_timing_source",
                measurement.pass_timestamps ? "command_buffer_timestamps" : "gpu_sync"
            );
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
