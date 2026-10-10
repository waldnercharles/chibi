#include "platform/cute.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STAGING_FRAMES 120
#define STAGING_LIMIT (UINT64_C(64) * 1024 * 1024)
#define STAGING_DEADLINE_MS 55000
#define TRACKED_POINTERS 8192
#define CALLBACK_LIMIT_MS 1000

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
static void *live_pointers[TRACKED_POINTERS];
static int live_count;
static int pending_readbacks;

static int live_index(void *pointer)
{
    for (int i = 0; i < live_count; i++) {
        if (live_pointers[i] == pointer) {
            return i;
        }
    }
    return -1;
}

static void track(void *pointer)
{
    if (pointer) {
        assert(live_count < TRACKED_POINTERS);
        live_pointers[live_count++] = pointer;
    }
}

static void untrack(void *pointer)
{
    int index = live_index(pointer);
    if (index >= 0) {
        live_pointers[index] = live_pointers[--live_count];
    }
}

static void *tracked_alloc(size_t size, void *unused)
{
    (void)unused;
    void *pointer = malloc(size);
    assert(pointer || !size);
    track(pointer);
    return pointer;
}

static void tracked_free(void *pointer, void *unused)
{
    (void)unused;
    untrack(pointer);
    free(pointer);
}

static void *tracked_calloc(size_t size, size_t count, void *unused)
{
    (void)unused;
    void *pointer = calloc(count, size);
    assert(pointer || !size || !count);
    track(pointer);
    return pointer;
}

static void *tracked_realloc(void *pointer, size_t size, void *unused)
{
    (void)unused;
    int index = live_index(pointer);
    void *resized = realloc(pointer, size);
    assert(resized || !size);
    if (index >= 0) {
        live_pointers[index] = live_pointers[--live_count];
    }
    track(resized);
    return resized;
}

static double callback_deadline()
{
    double deadline = emscripten_get_now() + CALLBACK_LIMIT_MS;
    double limit = started + STAGING_DEADLINE_MS;
    return deadline < limit ? deadline : limit;
}

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

TEST_CASE(destroy_releases_pending_readback_owner)
{
    CF_Readback readback = cf_canvas_readback(cf_app_get_canvas());
    REQUIRE(readback.id);
    void *owner = (void *)(uintptr_t)readback.id;
    REQUIRE(live_index(owner) >= 0);
    bool ready = cf_readback_ready(readback);
    pending_readbacks += !ready;
#if CHIBI_TEST_ADOPT_DEVICE
    REQUIRE(!ready);
#endif
    cf_destroy_readback(readback);
    REQUIRE(live_index(owner) < 0);
    return true;
}

TEST_CASE(shutdown_releases_pending_readback_owner)
{
    CF_Readback readback = cf_canvas_readback(cf_app_get_canvas());
    REQUIRE(readback.id);
    void *owner = (void *)(uintptr_t)readback.id;
    REQUIRE(live_index(owner) >= 0);
    bool ready = cf_readback_ready(readback);
    pending_readbacks += !ready;
#if CHIBI_TEST_ADOPT_DEVICE
    REQUIRE(!ready);
#endif
    cf_destroy_app();
    app_created = false;
    REQUIRE(live_index(owner) < 0);
    return true;
}

static void report_complete(void *unused)
{
    (void)unused;
    if (cf_webgpu_staging_orphans() && emscripten_get_now() < cleanup_deadline) {
        emscripten_async_call(report_complete, NULL, 16);
        return;
    }
    RUN_TEST_CASE(staging_callbacks_finish_after_destroy);
    pu_print_stats();
    printf("WebGPU shutdown: initial_orphans=%d final_orphans=%d\n", initial_orphans, cf_webgpu_staging_orphans());
    report_result(pu_test_failed() ? 1 : 0, frames, (double)peak, pending, free_chunks, frame_total_ms, frame_max_ms);
}

#if CHIBI_TEST_ADOPT_DEVICE
bool test_webgpu_device_is_lost();
int test_webgpu_error_count();
static int errors_before;
static double event_deadline;

EM_JS(void, restore_device_loss, (), { window.chibiStagingRestoreDeviceLoss(); });
EM_JS(void, resolve_old_loss, (), { window.chibiStagingResolveOldLoss(); });
EM_JS(int, old_loss_delivered, (), {
    return window.chibiStagingOldLossDelivered();
});
EM_JS(void, validation_error, (), { window.chibiStagingValidationError(); });
EM_JS(int, validation_delivered, (), {
    return window.chibiStagingValidationDelivered();
});
EM_JS(void, lose_device, (), { window.chibiStagingLoseDevice(); });
EM_JS(int, loss_delivered, (), { return window.chibiStagingLossDelivered(); });

TEST_CASE(recreated_app_ignores_old_device_loss)
{
    REQUIRE(app_created);
    REQUIRE(cf_query_backend() == CF_BACKEND_TYPE_WEBGPU);
    REQUIRE(old_loss_delivered());
    REQUIRE(errors_before == 0);
    REQUIRE(!test_webgpu_device_is_lost());
    REQUIRE(test_webgpu_error_count() == errors_before);
    return true;
}

TEST_CASE(imported_device_forwards_validation_errors)
{
    REQUIRE(validation_delivered());
    REQUIRE(test_webgpu_error_count() == errors_before + 1);
    REQUIRE(!test_webgpu_device_is_lost());
    return true;
}

TEST_CASE(imported_device_forwards_real_device_loss)
{
    REQUIRE(loss_delivered());
    REQUIRE(test_webgpu_device_is_lost());
    REQUIRE(test_webgpu_error_count() == errors_before + 1);
    return true;
}

static void complete_loss(void *unused)
{
    (void)unused;
    if ((!loss_delivered() || !test_webgpu_device_is_lost()) &&
        emscripten_get_now() < event_deadline) {
        emscripten_async_call(complete_loss, NULL, 16);
        return;
    }
    RUN_TEST_CASE(imported_device_forwards_real_device_loss);
    cf_destroy_app();
    app_created = false;
    cleanup_deadline = callback_deadline();
    emscripten_async_call(report_complete, NULL, 0);
}

static void complete_validation(void *unused)
{
    (void)unused;
    if ((!validation_delivered() || test_webgpu_error_count() == errors_before) &&
        emscripten_get_now() < event_deadline) {
        emscripten_async_call(complete_validation, NULL, 16);
        return;
    }
    RUN_TEST_CASE(imported_device_forwards_validation_errors);
    lose_device();
    event_deadline = callback_deadline();
    emscripten_async_call(complete_loss, NULL, 0);
}

static void complete_stale_loss(void *unused)
{
    (void)unused;
    RUN_TEST_CASE(recreated_app_ignores_old_device_loss);
    validation_error();
    event_deadline = callback_deadline();
    emscripten_async_call(complete_validation, NULL, 0);
}
#endif

static void complete_shutdown(void *unused)
{
    (void)unused;
    if (cf_webgpu_staging_orphans() && emscripten_get_now() < cleanup_deadline) {
        emscripten_async_call(complete_shutdown, NULL, 16);
        return;
    }
    printf("WebGPU readback ownership: pending=%d\n", pending_readbacks);
#if CHIBI_TEST_ADOPT_DEVICE
    if (!pu_test_failed() && !cf_webgpu_staging_orphans()) {
        restore_device_loss();
        CF_Result made = cf_make_app("webgpu-recreated", 0, 0, 0, 64, 64, CF_APP_OPTIONS_NO_AUDIO_BIT, NULL);
        app_created = !cf_is_error(made);
        if (app_created) {
            errors_before = test_webgpu_error_count();
            resolve_old_loss();
            emscripten_async_call(complete_stale_loss, NULL, 16);
            return;
        }
        RUN_TEST_CASE(recreated_app_ignores_old_device_loss);
    }
#endif
    report_complete(NULL);
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
    if (app_created && frames == STAGING_FRAMES) {
        cf_allocator_override((CF_Allocator){
            .alloc_fn = tracked_alloc,
            .free_fn = tracked_free,
            .calloc_fn = tracked_calloc,
            .realloc_fn = tracked_realloc,
        });
        RUN_TEST_CASE(destroy_releases_pending_readback_owner);
        RUN_TEST_CASE(shutdown_releases_pending_readback_owner);
    }
    if (app_created) {
        cf_destroy_app();
        app_created = false;
    }
    // Restore the caller's allocator before delayed callbacks can run. Every
    // readback owner must already have left the custom allocator at shutdown.
    cf_allocator_restore_default();
    initial_orphans = cf_webgpu_staging_orphans();
    cleanup_deadline = callback_deadline();
    emscripten_async_call(complete_shutdown, NULL, 0);
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
