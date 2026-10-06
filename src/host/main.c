#include "chibi/application.h"
#include "host/stacktrace.h"
#include "platform/cute.h"
#include "render/shader_dir.h"
#include "state_file.h"
#include "support/resources.h"
#include "support/xalloc.h"

#include <imgui/dcimgui.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

static volatile sig_atomic_t stop_requested;
static bool closing;
static void *application;
static const char *checkpoint_path;
static char *window_path;

static int result;

#ifndef __EMSCRIPTEN__
static void request_stop(int)
{
    stop_requested = 1;
}
#endif
static const char *option(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], name)) {
            return argv[i + 1];
        }
    }
    return NULL;
}
static bool flag(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], name)) {
            return true;
        }
    }
    return false;
}

static bool checkpoint()
{
    if (!checkpoint_path) {
        return true;
    }
    char *json = chibi_application_checkpoint(application);
    int x, y, width, height;
    cf_app_get_position(&x, &y);
    cf_app_get_size(&width, &height);
    char window[128];
    snprintf(window, sizeof window, "%d %d %d %d", x, y, width, height);
    bool ok = json && chibi_write_file(window_path, window) &&
              chibi_write_file(checkpoint_path, json);
    free(json);
    if (!ok) {
        fprintf(stderr, "Could not preserve workspace: %s\n", checkpoint_path);
    }
    return ok;
}

static bool preserve_rejected_checkpoint(const char *reason)
{
    size_t size = strlen(checkpoint_path) + sizeof ".rejected-XXXXXX";
    char *archive = xmalloc(size);
    snprintf(archive, size, "%s.rejected-XXXXXX", checkpoint_path);
    int fd = mkstemp(archive);
    if (fd < 0) {
        free(archive);
        return false;
    }
    close(fd);
    if (rename(checkpoint_path, archive) != 0) {
        unlink(archive);
        free(archive);
        return false;
    }
    fprintf(
        stderr,
        "Startup failed with saved workspace: %s\n"
        "Preserved workspace at %s. Retrying from project content.\n",
        reason,
        archive
    );
    free(archive);
    return true;
}

static void finish()
{
    chibi_application_destroy(application);
    free(window_path);
    cf_destroy_app();
}

static bool close_frame()
{
    if (!closing) {
        closing = true;
        if (!checkpoint()) {
            result = 1;
        }
    }
    return true;
}

static bool frame()
{
    if (stop_requested || closing) {
        return close_frame();
    }
    int status = chibi_application_frame(application);
    if (!status) {
        return false;
    }
    result = status < 0 ? 1 : 0;
    return close_frame();
}

#ifdef __EMSCRIPTEN__
EM_JS(void, web_startup_error, (const char *message), {
    Module['showStopped'](UTF8ToString(message));
})

static void web_frame()
{
    if (frame()) {
        emscripten_cancel_main_loop();
        finish();
    }
}
#endif

int main(int argc, char **argv)
{
    if (flag(argc, argv, "--engine-info")) {
        char *info = resource_info();
        if (!info) {
            return 1;
        }
        puts(info);
        free(info);
        return 0;
    }
    if (flag(argc, argv, "--help")) {
        puts(
            "chibi [--headless] [--frames N] [--fps N] [--screenshot "
            "PATH]\n"
            "             [--play [--scenario NAME]] [--project-root "
            "PATH|--project PATH] "
            "[--scene PATH] "
            "[--fixed-dt SECONDS]\n"
            "             [--open ASSET [--graph NAME]]\n"
            "             [--checkpoint PATH]\n"
            "             [--engine-info]\n"
            "Play: Escape pauses, Enter resumes, R restarts, Q quits while "
            "paused."
        );
        return 0;
    }
    stacktrace_init(argv[0]);

    cf_set_assert_handler(stacktrace_assert_handler);
    checkpoint_path = option(argc, argv, "--checkpoint");

    bool hidden = flag(argc, argv, "--headless") || flag(argc, argv, "--screenshot");
    int x = 0, y = 0, width = 1920, height = 1080;
    bool centered = true;
    if (checkpoint_path) {
        size_t size = strlen(checkpoint_path) + sizeof ".window";
        window_path = xmalloc(size);
        snprintf(window_path, size, "%s.window", checkpoint_path);
        char *window = chibi_read_file(window_path);
        int saved_x, saved_y, saved_width, saved_height;
        if (window &&
            sscanf(window, "%d %d %d %d", &saved_x, &saved_y, &saved_width, &saved_height) ==
                4 &&
            saved_width > 0 && saved_height > 0) {
            x = saved_x;
            y = saved_y;
            width = saved_width;
            height = saved_height;
            centered = false;
        }
        free(window);
    }
#ifdef __EMSCRIPTEN__
    // SDL tracks subsequent browser resizes from this CSS-sized canvas.
    double canvas_width, canvas_height;
    if (emscripten_get_element_css_size("#canvas", &canvas_width, &canvas_height) ==
            EMSCRIPTEN_RESULT_SUCCESS &&
        canvas_width >= 1 && canvas_height >= 1) {
        width = (int)canvas_width;
        height = (int)canvas_height;
    }
#endif
    CF_Result made = cf_make_app(
        flag(argc, argv, "--play") ? "Chibi | Play" : "Chibi",
        0,
        x,
        y,
        width,
        height,
        (centered ? CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT : 0) |
            CF_APP_OPTIONS_RESIZABLE_BIT | (hidden ? CF_APP_OPTIONS_NO_AUDIO_BIT : 0) |
            (hidden ? CF_APP_OPTIONS_HIDDEN_BIT : 0),
        argv[0]
    );
    if (cf_is_error(made)) {
        fprintf(stderr, "%s\n", made.details);
#ifdef __EMSCRIPTEN__
        web_startup_error(made.details);
#endif
        free(window_path);
        return 1;
    }
    cf_fs_mount(render_shader_dir(), "/chibi-render", true);
    cf_shader_directory("/chibi-render");
    cf_app_init_imgui();
    /* ImGui draws only game UI, which keeps no window settings. */
    ImGui_GetIO()->IniFilename = NULL;
    const char *fps = option(argc, argv, "--fps");
    if (fps && atoi(fps) > 0) {
        cf_set_target_framerate(atoi(fps));
    }
    char *saved = checkpoint_path ? chibi_read_file(checkpoint_path) : NULL;
    char error[1024] = { 0 };
    application = chibi_application_create(argc, argv, saved, error, sizeof error);
    if (!application && saved && preserve_rejected_checkpoint(error)) {
        error[0] = 0;
        application = chibi_application_create(argc, argv, NULL, error, sizeof error);
    }
    free(saved);
    if (!application) {
        fprintf(stderr, "%s\n", error);
#ifdef __EMSCRIPTEN__
        web_startup_error(error);
#endif
        free(window_path);
        cf_destroy_app();
        return 1;
    }
    chibi_application_activate(application);
#ifndef __EMSCRIPTEN__
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);
#endif
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(web_frame, 0, 1);
#else
    while (!frame()) {
    }
    finish();
#endif
    return result;
}
