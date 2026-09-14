#include "automation/render_preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *program)
{
    fprintf(
        stderr,
        "Usage: %s [--project-root PATH]\n"
        "Read one preview_shader or preview_scene JSON request from stdin and "
        "write one JSON "
        "response.\n",
        program
    );
}

int main(int argc, char **argv)
{
    const char *project_root = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--project-root") && i + 1 < argc) {
            project_root = argv[++i];
            continue;
        }
        fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        usage(argv[0]);
        return 2;
    }
    char *request = NULL;
    size_t capacity = 0;
    ssize_t length = getdelim(&request, &capacity, '\0', stdin);
    if (length <= 0) {
        free(request);
        fprintf(stderr, "chibi_render_ctl: expected one JSON request on stdin\n");
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
        fprintf(stderr, "chibi_render_ctl: could not create response\n");
        return 5;
    }
    fputs(response, stdout);
    fputc('\n', stdout);
    free(response);
    return exit_code;
}
