#include "automation/execution_policy.h"

#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void profile(const char *name, const char *allow)
{
    setenv("CHIBI_EXECUTION_PROFILE", name, 1);
    setenv("CHIBI_ALLOW_FULL_GPU", allow, 1);
}

static bool allowed(const char *json)
{
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    assert(doc);
    bool result = !chibi_render_policy(yyjson_doc_get_root(doc));
    yyjson_doc_free(doc);
    return result;
}

TEST_CASE(preview_budgets)
{
    profile("iteration", "0");
    REQUIRE(allowed("{\"operation\":\"preview_scene\"}"));
    REQUIRE(allowed("{\"operation\":\"preview_shader\"}"));
    REQUIRE(allowed("{\"operation\":\"preview_scene\",\"gpu_warmup_frames\":32,\"frames\":60}"));
    REQUIRE(allowed("{\"operation\":\"preview_scene\",\"width\":360,\"height\":640}"));
    const char *requests[] = {
        "{\"operation\":\"test_suite\"}",
        "{\"operation\":\"preview_scene\",\"render_frames\":2}",
        "{\"operation\":\"preview_scene\",\"gpu_warmup_frames\":33}",
        "{\"operation\":\"preview_shader\",\"gpu_warmup_frames\":1}",
        "{\"operation\":\"preview_scene\",\"frames\":61}",
        "{\"operation\":\"preview_scene\",\"width\":641}",
        "{\"operation\":\"preview_scene\",\"width\":640,\"height\":640}",
        "{\"operation\":\"preview_scene\",\"width\":15}",
        "{\"operation\":\"preview_scene\",\"gpu_warmup_frames\":-1}",
    };
    for (int i = 0; i < (int)(sizeof requests / sizeof *requests); i++) {
        REQUIRE(!allowed(requests[i]));
    }
    return true;
}

TEST_CASE(malformed_numeric_fields)
{
    profile("iteration", "0");
    const char *requests[] = {
        "{\"operation\":\"preview_scene\",\"render_frames\":true}",
        "{\"operation\":\"preview_scene\",\"width\":\"640\"}",
        "{\"operation\":\"preview_scene\",\"height\":null}",
        "{\"operation\":\"preview_scene\",\"frames\":1.5}",
        "{\"operation\":\"preview_scene\",\"gpu_warmup_frames\":[]}",
        "{\"operation\":\"preview_scene\",\"width\":18446744073709551615}",
    };
    for (int i = 0; i < (int)(sizeof requests / sizeof *requests); i++) {
        REQUIRE(!allowed(requests[i]));
    }
    return true;
}

TEST_CASE(capture_requires_explicit_profile_and_path)
{
    const char *request = "{\"operation\":\"preview_scene\",\"render_frames\":"
                          "180,\"capture_frames\":\"/tmp/frames\"}";
    profile("mp4", "0");
    REQUIRE(allowed(request));
    REQUIRE(
        allowed("{\"operation\":\"preview_scene\",\"render_frames\":3,\"capture_frames\":\"/tmp/frames\"}")
    );
    const char *requests[] = {
        "{\"operation\":\"preview_scene\"}",
        "{\"operation\":\"preview_scene\",\"render_frames\":2,\"capture_"
        "frames\":\"/tmp/frames\"}",
        "{\"operation\":\"preview_scene\",\"render_frames\":181,\"capture_"
        "frames\":\"/tmp/frames\"}",
        "{\"operation\":\"preview_scene\",\"render_frames\":3,\"capture_"
        "frames\":\"relative\"}",
        "{\"operation\":\"preview_scene\",\"render_frames\":3,\"capture_"
        "frames\":true}",
        "{\"operation\":\"preview_shader\",\"render_frames\":3,\"capture_"
        "frames\":\"/tmp/frames\"}",
    };
    for (int i = 0; i < (int)(sizeof requests / sizeof *requests); i++) {
        REQUIRE(!allowed(requests[i]));
    }
    profile("iteration", "0");
    REQUIRE(!allowed(request));
    REQUIRE(!allowed("{\"operation\":\"preview_shader\",\"capture_frames\":\"/tmp/frames\"}"));
    return true;
}

TEST_CASE(profiling_and_hardware_opt_in)
{
    const char *requests[] = {
        "{\"operation\":\"preview_scene\",\"metal_capture\":\"/tmp/gpu\"}",
        "{\"operation\":\"preview_scene\",\"pass_timing\":true}",
        "{\"operation\":\"preview_scene\",\"pass_timing_detail\":true}",
        ("{\"operation\":\"preview_scene\",\"render_frames\":1000,\"width\":"
         "2048}"),
    };
    for (int i = 0; i < (int)(sizeof requests / sizeof *requests); i++) {
        profile("iteration", "0");
        REQUIRE(!allowed(requests[i]));
        profile("hardware", "0");
        REQUIRE(!allowed(requests[i]));
        profile("hardware", "1");
        REQUIRE(allowed(requests[i]));
        profile("unknown", "1");
        REQUIRE(!allowed(requests[i]));
    }
    profile("hardware", "true");
    REQUIRE(!allowed(requests[0]));
    return true;
}

TEST_CASE(preview_window_mode)
{
    profile("iteration", "0");
    REQUIRE(chibi_preview_hidden(1));
    profile("mp4", "0");
    REQUIRE(chibi_preview_hidden(180));
    profile("hardware", "0");
    REQUIRE(chibi_preview_hidden(180));
    profile("hardware", "1");
    REQUIRE(chibi_preview_hidden(2));
    REQUIRE(!chibi_preview_hidden(180));
    return true;
}

TEST_CASE(preview_video_driver)
{
    unsetenv("SDL_VIDEO_DRIVER");
    unsetenv("SDL_VIDEODRIVER");
    unsetenv("CHIBI_PREVIEW_DISPLAY_CONTEXT");
    profile("iteration", "0");
    REQUIRE(!strcmp(chibi_preview_video_driver(true), "offscreen"));
    REQUIRE(!chibi_preview_video_driver(false));
    profile("mp4", "0");
    REQUIRE(!strcmp(chibi_preview_video_driver(true), "offscreen"));
    setenv("CHIBI_PREVIEW_DISPLAY_CONTEXT", "xvfb", 1);
    REQUIRE(!strcmp(chibi_preview_video_driver(true), "x11"));
    setenv("SDL_VIDEO_DRIVER", "wayland", 1);
    REQUIRE(!chibi_preview_video_driver(true));
    setenv("SDL_VIDEO_DRIVER", "", 1);
    REQUIRE(!chibi_preview_video_driver(true));
    unsetenv("SDL_VIDEO_DRIVER");
    setenv("SDL_VIDEODRIVER", "x11", 1);
    REQUIRE(!chibi_preview_video_driver(true));
    unsetenv("SDL_VIDEODRIVER");
    profile("hardware", "1");
    REQUIRE(!chibi_preview_video_driver(true));
    profile("gpu-check", "0");
    REQUIRE(!chibi_preview_video_driver(true));
    profile("unknown", "0");
    REQUIRE(!chibi_preview_video_driver(true));
    return true;
}

void test_tool_execution_policy()
{
    const char *keys[] = { "CHIBI_EXECUTION_PROFILE",
                           "CHIBI_ALLOW_FULL_GPU",
                           "SDL_VIDEO_DRIVER",
                           "SDL_VIDEODRIVER",
                           "CHIBI_PREVIEW_DISPLAY_CONTEXT" };
    char *saved[sizeof keys / sizeof *keys];
    for (int i = 0; i < (int)(sizeof keys / sizeof *keys); i++) {
        const char *value = getenv(keys[i]);
        saved[i] = value ? strdup(value) : NULL;
        assert(!value || saved[i]);
    }
    RUN_TEST_CASE(preview_budgets);
    RUN_TEST_CASE(malformed_numeric_fields);
    RUN_TEST_CASE(capture_requires_explicit_profile_and_path);
    RUN_TEST_CASE(profiling_and_hardware_opt_in);
    RUN_TEST_CASE(preview_window_mode);
    RUN_TEST_CASE(preview_video_driver);
    for (int i = 0; i < (int)(sizeof keys / sizeof *keys); i++) {
        if (saved[i]) {
            setenv(keys[i], saved[i], 1);
        } else {
            unsetenv(keys[i]);
        }
        free(saved[i]);
    }
}
