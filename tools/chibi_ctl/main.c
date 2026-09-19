#include "automation/controller.h"
#include "automation/render_preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *program)
{
    fprintf(
        stderr,
        "Usage: %s [--project-root PATH] [--render]\n"
        "Read newline-delimited JSON automation requests from stdin.\n"
        "Each non-empty line produces exactly one JSON response on stdout.\n"
        "With --render, read one preview request and produce one response.\n",
        program
    );
}

static int render_request(const char *project_root)
{
    char *request = NULL;
    size_t capacity = 0;
    ssize_t length = getdelim(&request, &capacity, '\0', stdin);
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

int main(int argc, char **argv)
{
    const char *project_root = ".";
    int render = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--render")) {
            render = 1;
            continue;
        }
        if (!strcmp(argv[i], "--project-root") && i + 1 < argc) {
            project_root = argv[++i];
            continue;
        }
        fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        usage(argv[0]);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (render) {
        return render_request(project_root);
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
