#include "platform/cute.h"
#include "third_party/pico_headers/pico_unit.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define STAGING_FRAMES 120
#define STAGING_LIMIT (UINT64_C(64) * 1024 * 1024)
#define STAGING_DEADLINE_MS 55000

int cf_webgpu_staging_orphans();
void cf_webgpu_staging_stats(uint64_t *allocated_bytes, int *pending, int *free_chunks);

static int frames;
static uint64_t peak;
static uint64_t halfway;
static uint64_t allocated;
static int pending;
static int free_chunks;
static bool reused;
static bool app_created;
static double started;
static double frame_total_ms;
static double frame_max_ms;
static int measured_frames;
static int initial_orphans;
static double cleanup_deadline;

EM_JS(
    void,
    report_result,
    (int status, int count, double bytes, int waiting, int available, double total_ms, double max_ms),
    {
        window.chibiStagingResult(
            status + ' ' + count + ' ' + bytes + ' ' + waiting + ' ' +
            available + ' ' + total_ms + ' ' + max_ms
        );
    }
);

TEST_CASE(staging_buffers_return_to_the_pool)
{
    REQUIRE(app_created);
    REQUIRE(cf_query_backend() == CF_BACKEND_TYPE_WEBGPU);
    REQUIRE(peak > 0);
    REQUIRE(peak <= STAGING_LIMIT);
    REQUIRE(frames == STAGING_FRAMES);
    REQUIRE(reused || (halfway > 0 && allocated <= halfway + UINT64_C(8) * 1024 * 1024));
    return true;
}

TEST_CASE(staging_callbacks_finish_after_destroy)
{
    REQUIRE(cf_webgpu_staging_orphans() == 0);
    return true;
}

static void complete(void *unused)
{
    (void)unused;
    if (cf_webgpu_staging_orphans() && emscripten_get_now() < cleanup_deadline) {
        emscripten_async_call(complete, NULL, 16);
        return;
    }
    RUN_TEST_CASE(staging_callbacks_finish_after_destroy);
    printf("WebGPU shutdown: initial_orphans=%d final_orphans=%d\n", initial_orphans, cf_webgpu_staging_orphans());
    report_result(pu_test_failed() ? 1 : 0, frames, (double)peak, pending, free_chunks, frame_total_ms, frame_max_ms);
}

static void finish()
{
    RUN_TEST_CASE(staging_buffers_return_to_the_pool);
    int status = pu_test_failed() ? 1 : 0;
    printf(
        "WebGPU staging: status=%d frames=%d peak=%" PRIu64
        " pending=%d free=%d reused=%d\n",
        status,
        frames,
        peak,
        pending,
        free_chunks,
        reused
    );
    printf(
        "WebGPU callback wall time: backend=%s samples=%d total_ms=%.6f "
        "mean_ms=%.6f max_ms=%.6f\n",
        app_created ? cf_backend_type_to_string(cf_query_backend()) : "uninitialized",
        measured_frames,
        frame_total_ms,
        measured_frames ? frame_total_ms / measured_frames : 0,
        frame_max_ms
    );
    if (app_created) {
        cf_destroy_app();
    }
    initial_orphans = cf_webgpu_staging_orphans();
    cleanup_deadline = emscripten_get_now() + 1000;
    if (cleanup_deadline > started + STAGING_DEADLINE_MS) {
        cleanup_deadline = started + STAGING_DEADLINE_MS;
    }
    emscripten_async_call(complete, NULL, 0);
}

static void frame(void *unused)
{
    (void)unused;
    if (emscripten_get_now() - started >= STAGING_DEADLINE_MS) {
        emscripten_cancel_main_loop();
        finish();
        return;
    }
    double frame_started = emscripten_get_now();
    cf_app_update(NULL);
    cf_draw_projection(cf_ortho_2d(0, 0, 64, 64));
    cf_draw_quad_fill(cf_make_aabb((CF_V2){ -16, -16 }, (CF_V2){ 16, 16 }), 0);
    cf_app_draw_onto_screen(false);
    frames++;
    if (frames > 30) {
        double elapsed = emscripten_get_now() - frame_started;
        frame_total_ms += elapsed;
        if (elapsed > frame_max_ms) {
            frame_max_ms = elapsed;
        }
        measured_frames++;
    }
    cf_webgpu_staging_stats(&allocated, &pending, &free_chunks);
    if (allocated > peak) {
        peak = allocated;
    }
    if (frames == STAGING_FRAMES / 2) {
        halfway = allocated;
    }
    if (free_chunks > 0) {
        reused = true;
    }
    if (frames == STAGING_FRAMES) {
        emscripten_cancel_main_loop();
        finish();
        return;
    }
}

int test_webgpu_staging_start()
{
    started = emscripten_get_now();
    CF_Result made = cf_make_app("webgpu-staging", 0, 0, 0, 64, 64, CF_APP_OPTIONS_NO_AUDIO_BIT, NULL);
    app_created = !cf_is_error(made);
    if (!app_created) {
        finish();
        return 1;
    }
    emscripten_set_main_loop_arg(frame, NULL, 0, false);
    return 0;
}
