#include "blueprints/application.h"
#include "host/stacktrace.h"
#include "module.h"
#ifdef BP_DEVELOPMENT_HOST
#include "dev.h"
#endif
#include "platform/cute.h"
#include "state_file.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static volatile sig_atomic_t stop_requested;
static bool closing;
#ifdef BP_DEVELOPMENT_HOST
static bp_dev *development;
#endif
static bp_module application;
static bp_host host;
#ifndef BP_STATIC_APPLICATION
static const char *manifest;
#endif
static const char *checkpoint_path;
static char *window_path;

static char *last_generation;

static int result;

#ifndef __EMSCRIPTEN__
static void request_stop(int)
{
    stop_requested = 1;
}
#endif
static void *allocate(size_t size)
{
    void *memory = malloc(size);
    if (!memory) {
        abort();
    }
    return memory;
}

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
    char *json = application.api->checkpoint(application.state);
    int x, y, width, height;
    cf_app_get_position(&x, &y);
    cf_app_get_size(&width, &height);
    char window[128];
    snprintf(window, sizeof window, "%d %d %d %d", x, y, width, height);
    bool ok = json && bp_write_file(window_path, window) &&
              bp_write_file(checkpoint_path, json);
    free(json);
    if (!ok) {
        fprintf(stderr, "Could not preserve workspace: %s\n", checkpoint_path);
    }
    return ok;
}

static bool preserve_rejected_checkpoint(const char *reason)
{
    size_t size = strlen(checkpoint_path) + sizeof ".rejected-XXXXXX";
    char *archive = allocate(size);
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
    bp_module_close(&application);
    free(last_generation);
#ifdef BP_DEVELOPMENT_HOST
    bp_dev_free(development);
#endif
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
#ifdef BP_DEVELOPMENT_HOST
        if (development) {
            bp_dev_stop(development);
        }
#endif
    }
#ifdef BP_DEVELOPMENT_HOST
    if (development && !bp_dev_stopped(development)) {
        cf_sleep(10);
        return false;
    }
#endif
    return true;
}

#ifndef BP_STATIC_APPLICATION
static void replace_application(const char *generation)
{
    char error[1024] = { 0 };
    bool loaded = bp_module_replace(&application, generation, &host, NULL, error, sizeof error);
#ifdef BP_DEVELOPMENT_HOST
    if (development) {
        bp_dev_accept(development, loaded);
    }
#endif
    fprintf(
        stderr,
        loaded ? "Loaded application %s\n" : "Application rejected: %s\n",
        loaded ? generation : error
    );
}
#endif

static bool frame()
{
    if (stop_requested || closing) {
        return close_frame();
    }
#ifndef BP_STATIC_APPLICATION
#ifdef BP_DEVELOPMENT_HOST
    if (development) {
        const char *candidate = bp_dev_poll(development);
        if (candidate) {
            replace_application(candidate);
        }
    }
#endif
    if (manifest) {
        char *generation = bp_read_file(manifest);
        if (generation) {
            generation[strcspn(generation, "\r\n")] = 0;
            if (*generation &&
                (!last_generation || strcmp(last_generation, generation))) {
                replace_application(generation);
                free(last_generation);
                last_generation = generation;
                generation = NULL;
            }
            free(generation);
        }
    }
#endif
    int status = application.api->frame(application.state);
    if (!status) {
        return false;
    }
    result = status < 0 ? 1 : 0;
    return close_frame();
}

#ifdef __EMSCRIPTEN__
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
    if (flag(argc, argv, "--help")) {
        puts(
            "c-blueprints [--headless] [--frames N] [--fps N] [--screenshot "
            "PATH]\n"
            "             [--checkpoint PATH]\n"
            "Development: --bootstrap RESULT, or --module MANIFEST."
        );
        return 0;
    }
#ifdef BP_DEVELOPMENT_HOST
    const char *bootstrap = option(argc, argv, "--bootstrap");
    if (bootstrap) {
        return bp_dev_bootstrap(argc, argv, bootstrap);
    }
    const char *config = option(argc, argv, "--dev-config");
    if (config) {
        const char *jobs = option(argc, argv, "--jobs");
        development = bp_dev_create(config, option(argc, argv, "--generation"), jobs ? atoi(jobs) : 0);
        if (!development) {
            return 1;
        }
    }
#endif
    stacktrace_init(argv[0]);

    cf_set_assert_handler(stacktrace_assert_handler);
    host = (bp_host){ argc, argv, allocate, free };
#ifndef BP_STATIC_APPLICATION
    manifest = option(argc, argv, "--module");
#ifdef BP_DEVELOPMENT_HOST
    if (development) {
        manifest = NULL;
    }
#endif
#endif
    checkpoint_path = option(argc, argv, "--checkpoint");
#ifdef BP_DEVELOPMENT_HOST
    if (development && !checkpoint_path) {
        checkpoint_path = bp_dev_checkpoint_path(development);
    }
#endif

    bool hidden = flag(argc, argv, "--headless") || flag(argc, argv, "--screenshot");
    int x = 0, y = 0, width = 1920, height = 1080;
    bool centered = true;
    if (checkpoint_path) {
        size_t size = strlen(checkpoint_path) + sizeof ".window";
        window_path = allocate(size);
        snprintf(window_path, size, "%s.window", checkpoint_path);
        char *window = bp_read_file(window_path);
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
    CF_Result made = cf_make_app(
        "c-blueprints",
        0,
        x,
        y,
        width,
        height,
        (centered ? CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT : 0) |
            CF_APP_OPTIONS_RESIZABLE_BIT | CF_APP_OPTIONS_NO_AUDIO_BIT |
            (hidden ? CF_APP_OPTIONS_HIDDEN_BIT : 0),
        argv[0]
    );
    if (cf_is_error(made)) {
        fprintf(stderr, "%s\n", made.details);
#ifdef BP_DEVELOPMENT_HOST
        bp_dev_free(development);
#endif
        free(window_path);
        return 1;
    }
    if (cf_query_backend() != CF_BACKEND_TYPE_GLES3) {
        cf_fs_mount(CBLUEPRINT_RENDER_SHADER_DIR, "/cblueprint-render", true);
        cf_shader_directory("/cblueprint-render");
    }
    cf_app_init_imgui();
    const char *fps = option(argc, argv, "--fps");
    if (fps && atoi(fps) > 0) {
        cf_set_target_framerate(atoi(fps));
    }
    char *saved = checkpoint_path ? bp_read_file(checkpoint_path) : NULL;
    char error[1024] = { 0 };
#ifdef BP_STATIC_APPLICATION
    application.api = blueprints_application();
    application.state = application.api->create(&host, saved, error, sizeof error);
    if (!application.state && saved && preserve_rejected_checkpoint(error)) {
        error[0] = 0;
        application.state = application.api->create(&host, NULL, error, sizeof error);
    }
    if (application.state) {
        application.api->activate(application.state);
    }
#else
#ifdef BP_DEVELOPMENT_HOST
    if (development) {
        last_generation = strdup(bp_dev_initial_application(development));
    }
#endif
    if (!last_generation && manifest) {
        last_generation = bp_read_file(manifest);
    }

    if (last_generation) {
        last_generation[strcspn(last_generation, "\r\n")] = 0;
    }
    bool loaded = last_generation &&
                  bp_module_replace(&application, last_generation, &host, saved, error, sizeof error);
    if (!loaded && last_generation && saved && preserve_rejected_checkpoint(error)) {
        error[0] = 0;
        loaded = bp_module_replace(&application, last_generation, &host, NULL, error, sizeof error);
    }
    if (!loaded) {
        fprintf(stderr, "Could not load application: %s\n", *error ? error : "missing --module manifest");
        free(saved);
        free(last_generation);
#ifdef BP_DEVELOPMENT_HOST
        bp_dev_free(development);
#endif
        free(window_path);
        cf_destroy_app();
        return 1;
    }
#endif
#ifndef BP_STATIC_APPLICATION
#ifdef BP_DEVELOPMENT_HOST
    if (development) {
        bp_dev_accept(development, true);
    }
#endif
    fprintf(stderr, "Loaded application %s\n", last_generation);
#endif
    free(saved);
    if (!application.state) {
        fprintf(stderr, "%s\n", error);
#ifdef BP_DEVELOPMENT_HOST
        bp_dev_free(development);
#endif
        free(window_path);
        cf_destroy_app();
        return 1;
    }
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
