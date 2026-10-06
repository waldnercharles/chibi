#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *program;
static const char *const selectors[] = { "--studio-documents", "--studio-shell" };
static const char *const variables[] = {
    "CHIBI_EXECUTION_PROFILE",
    "CHIBI_ALLOW_FULL_GPU",
    "CHIBI_IFRAME_PROFILE",
    "CHIBI_TEST_TIMINGS",
    "DISPLAY",
    "WAYLAND_DISPLAY",
    "SDL_VIDEO_DRIVER",
    "SDL_VIDEODRIVER",
};
static char *saved[sizeof variables / sizeof *variables];
static char directory[128], log_path[160];
static char *output;

static void setup()
{
    for (int i = 0; i < (int)(sizeof variables / sizeof *variables); i++) {
        const char *value = getenv(variables[i]);
        saved[i] = value ? strdup(value) : NULL;
        assert(!value || saved[i]);
    }
    assert(!setenv("CHIBI_EXECUTION_PROFILE", "iteration", 1));
    assert(!setenv("CHIBI_ALLOW_FULL_GPU", "0", 1));
    assert(!setenv("CHIBI_TEST_TIMINGS", "1", 1));
    assert(!setenv("CHIBI_IFRAME_PROFILE", "1", 1));
    assert(!setenv("DISPLAY", "", 1));
    assert(!setenv("WAYLAND_DISPLAY", "", 1));
    assert(!setenv("SDL_VIDEO_DRIVER", "chibi-no-such-video-driver", 1));
    assert(!setenv("SDL_VIDEODRIVER", "chibi-no-such-video-driver", 1));
    strcpy(directory, "/tmp/chibi-studio-selectors-XXXXXX");
    assert(mkdtemp(directory));
    snprintf(log_path, sizeof log_path, "%s/output", directory);
}

static void teardown()
{
    free(output);
    output = NULL;
    tool_remove_tree(directory);
    for (int i = 0; i < (int)(sizeof variables / sizeof *variables); i++) {
        if (saved[i]) {
            assert(!setenv(variables[i], saved[i], 1));
        } else {
            assert(!unsetenv(variables[i]));
        }
        free(saved[i]);
        saved[i] = NULL;
    }
}

static int invoke(const char *selector, const char *argument, const char *extra)
{
    const char *args[] = { program, selector, argument, extra, NULL };
    free(output);
    output = NULL;
    int result = tool_run(args, tool_root(), log_path);
    output = tool_read(log_path);
    assert(output);
    return result;
}

static bool no_studio_started()
{
    return !strstr(output, "STUDIO_BEGIN:") && !strstr(output, "PASS:") &&
           !strstr(output, "ALL CHECKS PASSED") &&
           !strstr(output, "RmlUi info:") && !strstr(output, "STUDIO_FRAME ") &&
           !strstr(output, "Studio documents app:");
}

TEST_CASE(studio_listing_needs_no_graphics)
{
    assert(!setenv("CHIBI_EXECUTION_PROFILE", "hardware", 1));
    assert(!setenv("CHIBI_ALLOW_FULL_GPU", "1", 1));
    const char *names[] = { "properties", "readout_follows_camera" };
    for (int i = 0; i < 2; i++) {
        REQUIRE(invoke(selectors[i], "list", NULL) == 0);
        REQUIRE(no_studio_started());
        REQUIRE(!strstr(output, "requires_hardware_or_classification"));
        char expected[64];
        snprintf(expected, sizeof expected, "%s\n", names[i]);
        const char *name = strstr(output, expected);
        REQUIRE(name && (name == output || name[-1] == '\n'));
        REQUIRE(!strstr(name + strlen(expected), expected));
    }
    return true;
}

TEST_CASE(studio_arguments_rejected_before_policy)
{
    for (int i = 0; i < 2; i++) {
        REQUIRE(invoke(selectors[i], "not-a-case", NULL) == 2);
        REQUIRE(strstr(output, "unknown Studio"));
        REQUIRE(!strstr(output, "requires_hardware_or_classification"));
        REQUIRE(no_studio_started());
        REQUIRE(invoke(selectors[i], "list", "extra") == 2);
        REQUIRE(strstr(output, "Studio selectors accept one"));
        REQUIRE(!strstr(output, "requires_hardware_or_classification"));
        REQUIRE(no_studio_started());
        REQUIRE(invoke(selectors[i], "list", "--other") == 2);
        REQUIRE(strstr(output, "multiple test_suite selectors"));
        REQUIRE(!strstr(output, "requires_hardware_or_classification"));
        REQUIRE(no_studio_started());
    }
    return true;
}

TEST_CASE(studio_execution_requires_opt_in)
{
    assert(!setenv("CHIBI_EXECUTION_PROFILE", "hardware", 1));
    const char *names[] = { "properties", "readout_follows_camera" };
    for (int i = 0; i < 2; i++) {
        REQUIRE(invoke(selectors[i], NULL, NULL) == 2);
        REQUIRE(strstr(output, "selector_requires_hardware_or_classification"));
        REQUIRE(no_studio_started());
        REQUIRE(invoke(selectors[i], names[i], NULL) == 2);
        REQUIRE(strstr(output, "selector_requires_hardware_or_classification"));
        REQUIRE(no_studio_started());
    }
    return true;
}

TEST_CASE(panel_lifecycle_selection_needs_no_graphics)
{
    const char *selector = "--studio-panel-lifecycle";
    const char *profiles[] = { "iteration", "hardware" };
    const char
        *expected = "{\"selector\":\"--studio-panel-lifecycle\","
                    "\"case\":\"area_panels_take_turns(false)\","
                    "\"expected_frames\":2016,\"graphics_started\":false,"
                    "\"validation_only\":true}\n";
    for (int i = 0; i < 2; i++) {
        assert(!setenv("CHIBI_EXECUTION_PROFILE", profiles[i], 1));
        assert(!setenv("CHIBI_ALLOW_FULL_GPU", i ? "1" : "0", 1));
        REQUIRE(invoke(selector, "validate", NULL) == 0);
        REQUIRE(!strcmp(output, expected));
        REQUIRE(no_studio_started());
    }
    const char *malformed[][2] = {
        { "typo", NULL },
        { "validate", "extra" },
        { "--studio-panels", NULL },
        { "validate", "--studio-panels" },
        { "false", NULL },
    };
    for (int i = 0; i < (int)(sizeof malformed / sizeof *malformed); i++) {
        REQUIRE(invoke(selector, malformed[i][0], malformed[i][1]) == 2);
        REQUIRE(strstr(output, "usage:"));
        REQUIRE(!strstr(output, "requires_hardware_or_classification"));
        REQUIRE(no_studio_started());
    }
    assert(!setenv("CHIBI_ALLOW_FULL_GPU", "0", 1));
    for (int i = 0; i < 2; i++) {
        assert(!setenv("CHIBI_EXECUTION_PROFILE", profiles[i], 1));
        REQUIRE(invoke(selector, NULL, NULL) == 2);
        REQUIRE(strstr(output, "requires_hardware_or_classification"));
        REQUIRE(no_studio_started());
    }
    assert(!setenv("CHIBI_ALLOW_FULL_GPU", "1", 1));
    REQUIRE(invoke("--studio-panel-lifecycle-typo", NULL, NULL) == 2);
    REQUIRE(strstr(output, "unknown test_suite selector:"));
    REQUIRE(no_studio_started());
    REQUIRE(invoke("--list-tests", NULL, NULL) == 0);
    REQUIRE(strstr(output, selector));
    REQUIRE(no_studio_started());
    return true;
}

void test_tool_studio_selectors(const char *binary)
{
    program = binary;
    pu_setup(setup, teardown);
    RUN_TEST_CASE(studio_listing_needs_no_graphics);
    RUN_TEST_CASE(studio_arguments_rejected_before_policy);
    RUN_TEST_CASE(studio_execution_requires_opt_in);
    RUN_TEST_CASE(panel_lifecycle_selection_needs_no_graphics);
    pu_clear_setup();
}
