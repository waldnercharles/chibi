#include "assets/project.h"
#include "automation/controller.h"
#include "automation/execution_policy.h"
#include "automation/render_preview.h"
#include "commands.h"
#include "evidence.h"
#include "prepare.h"
#ifdef CHIBI_CTL_STANDALONE
#include "registry.h"
#endif
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
        "Direct commands: schema [KIND], assets, resources, validate [PATH], "
        "inspect PATH [--sample]\n"
        "  material create NAME --preset matte|metal|emissive|unlit "
        "[--roughness N] [--metallic N]\n"
        "  model plan SOURCE --name NAME\n"
        "  model import SOURCE --name NAME [--recipe]\n"
        "  model reimport NAME [--check]\n"
        "Use --project-root PATH and --evidence NEW_DIRECTORY before direct "
        "commands.\n"
        "Evidence supports NDJSON stdin and --render/--render-request FILE.\n"
        "Preview evidence replays the saved request bytes via --render stdin.\n"
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

static bool configure_render_video(char **blocked)
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
    char response[1024];
    snprintf(
        response,
        sizeof response,
        "{\"ok\":false,\"status\":\"blocked\",\"full_verification\":false,"
        "\"error\":{\"code\":\"video_driver_unavailable\","
        "\"message\":\"SDL build does not include requested %s video backend; "
        "select a supported SDL_VIDEO_DRIVER or use the documented bounded "
        "Xvfb path. No fallback was attempted.\"}}",
        driver
    );
    *blocked = strdup(response);
    return false;
}

static int render_response(chibi_ctl_evidence *evidence, const char *request, char *response, int status)
{
    if (!response) {
        response = strdup("{\"ok\":false,\"error\":{\"code\":\"response_unavailable\",\"message\":\"Could not create preview response\"}}");
        status = CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    fputs(response, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    bool saved = chibi_ctl_evidence_response(evidence, request, response, status);
    free(response);
    return saved ? status : CHIBI_AUTOMATION_IO_ERROR;
}

static int render_request(const char *project_root, const char *path, chibi_ctl_evidence *evidence)
{
    char *request = NULL;
    size_t capacity = 0;
    FILE *input = path ? fopen(path, "r") : stdin;
    if (!input) {
        perror("chibi_ctl --render-request");
        return render_response(
            evidence,
            "",
            strdup("{\"ok\":false,\"error\":{\"code\":\"request_unreadable\",\"message\":\"Could not open preview request\"}}"),
            CHIBI_AUTOMATION_IO_ERROR
        );
    }
    ssize_t length = getdelim(&request, &capacity, '\0', input);
    bool read_error = ferror(input);
    if (path && fclose(input)) {
        read_error = true;
    }
    if (length > 0 && !chibi_ctl_evidence_request(evidence, request, (size_t)length)) {
        free(request);
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    if (length <= 0 || read_error) {
        int status = read_error ? CHIBI_AUTOMATION_IO_ERROR
                                : CHIBI_AUTOMATION_INVALID_REQUEST;
        int result = render_response(
            evidence,
            request ? request : "",
            strdup("{\"ok\":false,\"error\":{\"code\":\"request_unreadable\",\"message\":\"Expected one readable JSON preview request\"}}"),
            status
        );
        free(request);
        return result;
    }
    char *blocked = NULL;
    if (!configure_render_video(&blocked)) {
        int result = render_response(evidence, request, blocked, CHIBI_AUTOMATION_INVALID_REQUEST);
        free(request);
        return result;
    }
    chibi_render_deadline();
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
    int result = render_response(evidence, request, response, exit_code);
    free(request);
    return result;
}

static int run(int argc, char **argv)
{
    const char *project_root = ".";
    const char *request_path = NULL;
    const char *evidence_path = NULL;
    char *direct = NULL;
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
        if (!strcmp(argv[i], "--evidence") && i + 1 < argc) {
            evidence_path = argv[++i];
            continue;
        }
        if (argv[i][0] != '-') {
            char error[512];
            direct = chibi_ctl_command(argc - i, argv + i, error, sizeof error);
            if (!direct) {
                fprintf(stderr, "%s\n", error);
                return CHIBI_AUTOMATION_INVALID_REQUEST;
            }
            break;
        }
        fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        usage(argv[0]);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if ((direct && render) ||
        ((direct || evidence_path) && (migrate || prepared_path || format))) {
        fprintf(stderr, "Direct commands/evidence cannot be combined with legacy execution flags\n");
        free(direct);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (migrate) {
        char error[512];
        int steps = chibi_project_migrate(project_root, error, sizeof error);
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
        chibi_ctl_evidence *
            evidence = evidence_path
                         ? chibi_ctl_evidence_begin(evidence_path, project_root, true, argc, argv)
                         : NULL;
        if (evidence_path && !evidence) {
            return CHIBI_AUTOMATION_IO_ERROR;
        }
        int status = render_request(project_root, request_path, evidence);
        return chibi_ctl_evidence_finish(evidence, status)
                 ? status
                 : CHIBI_AUTOMATION_IO_ERROR;
    }
    if (format) {
        int differing = chibi_automation_format_assets(project_root, check);
        return differing < 0      ? CHIBI_AUTOMATION_IO_ERROR
             : check && differing ? CHIBI_AUTOMATION_VALIDATION_ERROR
                                  : CHIBI_AUTOMATION_OK;
    }

    chibi_ctl_evidence *evidence = NULL;
    if (evidence_path) {
        evidence = chibi_ctl_evidence_begin(evidence_path, project_root, false, argc, argv);
        if (!evidence) {
            free(direct);
            return CHIBI_AUTOMATION_IO_ERROR;
        }
    }
    char *line = NULL;
    size_t capacity = 0;
    int result = CHIBI_AUTOMATION_OK;
    bool pending_direct = direct != NULL;
    ssize_t read_length = 0;
    while (pending_direct || (read_length = getline(&line, &capacity, stdin)) >= 0) {
        size_t length = (size_t)read_length;
        if (pending_direct) {
            line = direct;
            length = strlen(direct);
            pending_direct = false;
        }
        bool saved = chibi_ctl_evidence_request(evidence, line, length);
        if (direct) {
            saved = chibi_ctl_evidence_request(evidence, "\n", 1) && saved;
        }
        if (!saved) {
            result = CHIBI_AUTOMATION_IO_ERROR;
            break;
        }
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
            chibi_ctl_evidence_response(evidence, line, "null", CHIBI_AUTOMATION_EXECUTION_ERROR);
            result = CHIBI_AUTOMATION_EXECUTION_ERROR;
            if (direct) {
                break;
            }
            continue;
        }
        fputs(response, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        if (!chibi_ctl_evidence_response(evidence, line, response, exit_code)) {
            exit_code = CHIBI_AUTOMATION_IO_ERROR;
        }
        free(response);
        if (exit_code > result) {
            result = exit_code;
        }
        if (direct) {
            break;
        }
    }
    if (ferror(stdin) && !direct) {
        result = CHIBI_AUTOMATION_IO_ERROR;
    }
    free(line);
    if (!chibi_ctl_evidence_finish(evidence, result)) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    return result;
}

int main(int argc, char **argv)
{
#ifdef CHIBI_CTL_STANDALONE
    if (!chibi_ctl_registry_init()) {
        return 1;
    }
#endif
    int result = run(argc, argv);
#ifdef CHIBI_CTL_STANDALONE
    chibi_ctl_registry_free();
#endif
    return result;
}
