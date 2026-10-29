#include "assets/project.h"
#include "authoring/application_catalog.h"
#include "authoring/project_open.h"
#include "authoring/studio_project.h"
#include "automation/controller.h"
#include "automation/execution_policy.h"
#include "automation/render_preview.h"
#include "prepare.h"
#include "support/resources.h"

#include <SDL3/SDL_video.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *program)
{
    fprintf(
        stderr,
        "Usage: %s [--project-root PATH] [--render]\n"
        "       %s --project-root PATH --render-request FILE\n"
        "       %s --project-root PATH --format [--check]\n"
        "       %s --project-root PATH --migrate\n"
        "       %s --engine-info\n"
        "       %s --project-root PATH --prepare-project OUTPUT [--scene "
        "PATH]\n"
        "Read newline-delimited JSON automation requests from stdin.\n"
        "Each non-empty line produces exactly one JSON response on stdout.\n"
        "With --render, read one preview request and produce one response.\n"
        "Default iteration profile: bounded preview; see tools/agent.py list.\n"
        "Linux bounded rendering prefers SDL offscreen unless a video driver "
        "is explicit.\n"
        "Full rendering requires CHIBI_EXECUTION_PROFILE=hardware and "
        "CHIBI_ALLOW_FULL_GPU=1.\n"
        "With --format, rewrite authored assets in canonical form; --check\n"
        "only lists assets that differ and fails when any do.\n"
        "With --migrate, upgrade a project saved by an earlier build in "
        "place.\n",
        program,
        program,
        program,
        program,
        program,
        program
    );
}

static bool configure_render_video()
{
#ifdef __linux__
    const char *driver = chibi_preview_video_driver(true);
#else
    const char *driver = chibi_preview_video_driver(false);
#endif
    if (!driver) {
        return true;
    }
    for (int i = 0; i < SDL_GetNumVideoDrivers(); i++) {
        if (!strcmp(SDL_GetVideoDriver(i), driver)) {
            setenv("SDL_VIDEO_DRIVER", driver, 1);
            fprintf(stderr, "Chibi preview video driver request: %s\n", driver);
            return true;
        }
    }
    printf(
        "{\"ok\":false,\"status\":\"blocked\",\"full_verification\":false,"
        "\"error\":{\"code\":\"video_driver_unavailable\","
        "\"message\":\"SDL build does not include requested %s video backend; "
        "select a supported SDL_VIDEO_DRIVER or use the documented bounded "
        "Xvfb path. No fallback was attempted.\"}}\n",
        driver
    );
    return false;
}

static int render_request(const char *project_root, const char *path)
{
    char *request = NULL;
    size_t capacity = 0;
    FILE *input = path ? fopen(path, "r") : stdin;
    if (!input) {
        perror("chibi_ctl --render-request");
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    ssize_t length = getdelim(&request, &capacity, '\0', input);
    if (path) {
        fclose(input);
    }
    if (length <= 0) {
        free(request);
        fprintf(stderr, "chibi_ctl --render: expected one JSON request on stdin\n");
        return 2;
    }
    int exit_code = 0;
    fflush(stdout);
    int output = dup(STDOUT_FILENO);
    if (output < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
        abort();
    }
    char *response = chibi_render_preview_handle(request, project_root, &exit_code);
    fflush(stdout);
    if (dup2(output, STDOUT_FILENO) < 0) {
        abort();
    }
    close(output);
    free(request);
    if (!response) {
        fprintf(stderr, "chibi_ctl --render: could not create response\n");
        return 5;
    }
    fputs(response, stdout);
    fputc('\n', stdout);
    free(response);
    return exit_code;
}

static int run(int argc, char **argv)
{
    const char *project_root = ".";
    const char *request_path = NULL;
    const char *prepared_path = NULL, *scene = NULL;
    int render = 0;
    bool format = false, check = false, migrate = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--engine-info")) {
            char *info = resource_info();
            if (!info) {
                return 1;
            }
            puts(info);
            free(info);
            return 0;
        }
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--render")) {
            render = 1;
            continue;
        }
        if (!strcmp(argv[i], "--format") || !strcmp(argv[i], "--check")) {
            format = true;
            check |= !strcmp(argv[i], "--check");
            continue;
        }
        if (!strcmp(argv[i], "--migrate")) {
            migrate = true;
            continue;
        }
        if (!strcmp(argv[i], "--render-request") && i + 1 < argc) {
            render = 1;
            request_path = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--project-root") && i + 1 < argc) {
            project_root = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--prepare-project") && i + 1 < argc) {
            prepared_path = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--scene") && i + 1 < argc) {
            scene = argv[++i];
            continue;
        }
        fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        usage(argv[0]);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (migrate) {
        char error[512];
        native_capabilities *native = authoring_application_catalog_new(
            studio_project_is(project_root),
            error,
            sizeof error
        );
        int steps = native
                      ? authoring_project_migrate(project_root, native, error, sizeof error)
                      : -1;
        native_capabilities_free(native);
        if (steps < 0) {
            fprintf(stderr, "%s\n", error);
            return CHIBI_AUTOMATION_IO_ERROR;
        }
        printf("Upgraded %d version step%s to version %d\n", steps, steps == 1 ? "" : "s", CHIBI_PROJECT_VERSION);
        return CHIBI_AUTOMATION_OK;
    }
    if (prepared_path) {
        if (render || format) {
            fprintf(stderr, "Project preparation cannot be combined with rendering or formatting\n");
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
        return chibi_prepare_project(project_root, scene, prepared_path);
    }
    if (render) {
        chibi_render_deadline();
        if (!configure_render_video()) {
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
        return render_request(project_root, request_path);
    }
    if (format) {
        int differing = chibi_automation_format_assets(project_root, check);
        return differing < 0      ? CHIBI_AUTOMATION_IO_ERROR
             : check && differing ? CHIBI_AUTOMATION_VALIDATION_ERROR
                                  : CHIBI_AUTOMATION_OK;
    }

    char *line = NULL;
    size_t capacity = 0;
    int result = CHIBI_AUTOMATION_OK;
    while (getline(&line, &capacity, stdin) >= 0) {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (!length) {
            continue;
        }
        int exit_code = CHIBI_AUTOMATION_OK;
        char *response = chibi_automation_handle(line, project_root, &exit_code);
        if (!response) {
            fprintf(stderr, "chibi_ctl: could not create response\n");
            result = CHIBI_AUTOMATION_EXECUTION_ERROR;
            continue;
        }
        fputs(response, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        free(response);
        if (exit_code > result) {
            result = exit_code;
        }
    }
    free(line);
    return result;
}

int main(int argc, char **argv)
{
    return run(argc, argv);
}
