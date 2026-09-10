#include "automation/controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(
        stderr,
        "Usage: %s [--project-root PATH]\n"
        "Read newline-delimited JSON automation requests from stdin.\n"
        "Each non-empty line produces exactly one JSON response on stdout.\n",
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
        return BLUEPRINTS_AUTOMATION_INVALID_REQUEST;
    }

    char *line = NULL;
    size_t capacity = 0;
    int result = BLUEPRINTS_AUTOMATION_OK;
    while (getline(&line, &capacity, stdin) >= 0) {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (!length) {
            continue;
        }
        int exit_code = BLUEPRINTS_AUTOMATION_OK;
        char *response = blueprints_automation_handle(line, project_root, &exit_code);
        if (!response) {
            fprintf(stderr, "blueprints_ctl: could not create response\n");
            result = BLUEPRINTS_AUTOMATION_EXECUTION_ERROR;
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
