#include "authoring/source_format_file.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static int usage()
{
    fputs(
        "usage: chibi-format [--check] [--config FILE] PATH...\n"
        "       chibi-format --stdin FILE [--config FILE]\n",
        stderr
    );
    return 2;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    /* The buffer contract preserves comment/literal bytes, even CRLF. */
    if (_setmode(_fileno(stdin), _O_BINARY) < 0 ||
        _setmode(_fileno(stdout), _O_BINARY) < 0) {
        return 1;
    }
#endif
    bool check = false;
    const char *config = NULL, *input = NULL;
    const char **paths = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check") && !check) {
            check = true;
        } else if (!strcmp(argv[i], "--config") && !config && i + 1 < argc) {
            config = argv[++i];
        } else if (!strcmp(argv[i], "--stdin") && !input && i + 1 < argc) {
            input = argv[++i];
        } else if (argv[i][0] == '-') {
            afree(paths);
            return usage();
        } else {
            apush(paths, argv[i]);
        }
    }
    if (input) {
        if (check || acount(paths)) {
            afree(paths);
            return usage();
        }
        char *text = NULL;
        int c;
        while ((c = getchar()) != EOF) {
            if (!c || acount(text) == 8 * 1024 * 1024) {
                fputs("format: input contains NUL or exceeds byte limit\n", stderr);
                afree(text);
                return 1;
            }
            apush(text, (char)c);
        }
        if (ferror(stdin)) {
            afree(text);
            return 1;
        }
        apush(text, 0);
        char error[512];
        char *result = authoring_format_file_text(input, text, config, error, sizeof error);
        afree(text);
        if (!result) {
            fprintf(stderr, "format: %s: %s\n", input, error);
            return 1;
        }
        bool ok = fputs(result, stdout) >= 0 && !fflush(stdout);
        free(result);
        return ok ? 0 : 1;
    }
    if (!acount(paths)) {
        afree(paths);
        return usage();
    }
    int status = 0;
    for (int i = 0; i < acount(paths); i++) {
        int changes = authoring_format_path(paths[i], config, check);
        if (changes < 0 || (check && changes)) {
            status = 1;
        }
    }
    afree(paths);
    return status;
}
