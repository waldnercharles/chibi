#include "automation/execution_policy.h"

#include "support/execution_policy.h"

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

static bool bounded_int(yyjson_val *root, const char *key, int fallback, int min, int max, int *out)
{
    yyjson_val *value = yyjson_obj_get(root, key);
    if (!value) {
        *out = fallback;
        return true;
    }
    if (!yyjson_is_int(value) || yyjson_get_sint(value) < min ||
        yyjson_get_sint(value) > max) {
        return false;
    }
    *out = (int)yyjson_get_sint(value);
    return true;
}

const char *chibi_render_policy(yyjson_val *root)
{
    if (chibi_execution_hardware()) {
        return NULL;
    }
    bool capture = chibi_execution_is("mp4");
    if (!capture && !chibi_execution_is("iteration")) {
        return "profile_requires_iteration_mp4_or_opted_in_hardware";
    }
    const char *op = yyjson_get_str(yyjson_obj_get(root, "operation"));
    bool scene = op && !strcmp(op, "preview_scene");
    if (!scene && (!op || strcmp(op, "preview_shader") || capture)) {
        return "operation_not_a_bounded_preview";
    }
    int width, height, frames, warmup, simulation;
    if (!bounded_int(root, "width", scene ? 640 : 256, 16, 640, &width) ||
        !bounded_int(root, "height", scene ? 360 : 256, 16, 640, &height) ||
        (int64_t)width * height > 640 * 360) {
        return "resolution_budget_640x360_pixels_max_side_640";
    }
    if (!bounded_int(root, "render_frames", 1, capture ? 3 : 1, capture ? 180 : 1, &frames) ||
        !bounded_int(root, "gpu_warmup_frames", scene ? 1 : 0, 0, scene ? 32 : 0, &warmup) ||
        !bounded_int(root, "frames", 1, 0, 60, &simulation)) {
        return "frame_budget_preview_1_output_capture_3_to_180_warmup_0_to_32_"
               "simulation_0_to_60";
    }
    if ((int64_t)width * height * 4 * frames > 256 * 1024 * 1024) {
        return "raw_capture_byte_budget_256_MiB";
    }
    yyjson_val *raw = yyjson_obj_get(root, "capture_frames");
    const char *path = yyjson_get_str(raw);
    if ((capture && (!path || path[0] != '/')) || (!capture && raw)) {
        return "raw_frames_require_explicit_mp4_profile_and_absolute_capture_"
               "path";
    }
    if (yyjson_obj_get(root, "metal_capture") ||
        yyjson_get_bool(yyjson_obj_get(root, "pass_timing")) ||
        yyjson_get_bool(yyjson_obj_get(root, "pass_timing_detail"))) {
        return "profiling_requires_opted_in_hardware";
    }
    return NULL;
}

static const char
    BLOCKED[] = "{\"ok\":false,\"status\":\"blocked\",\"reason\":\"render_"
                "time_budget_exceeded\",\"full_verification\":false}\n";

#ifdef _WIN32
/* TerminateProcess skips the DLL detach _exit runs, which a GPU driver hung
 * on the main thread can deadlock. */
static void CALLBACK deadline(void *, BOOLEAN)
{
    write(STDOUT_FILENO, BLOCKED, sizeof BLOCKED - 1);
    TerminateProcess(GetCurrentProcess(), 124);
}
#else
static void deadline(int)
{
    write(STDOUT_FILENO, BLOCKED, sizeof BLOCKED - 1);
    _exit(124);
}
#endif

bool chibi_preview_hidden(int render_frames)
{
    return !chibi_execution_hardware() || render_frames <= 2;
}

const char *chibi_preview_video_driver(bool linux_platform)
{
    if (!linux_platform ||
        (!chibi_execution_is("iteration") && !chibi_execution_is("mp4")) ||
        getenv("SDL_VIDEO_DRIVER") || getenv("SDL_VIDEODRIVER")) {
        return NULL;
    }
    const char *context = getenv("CHIBI_PREVIEW_DISPLAY_CONTEXT");
    return context && !strcmp(context, "xvfb") ? "x11" : "offscreen";
}
