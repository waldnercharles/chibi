#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void pause_seconds(double seconds)
{
    struct timespec delay = { .tv_sec = (time_t)seconds,
                              .tv_nsec = (long)((seconds - (time_t)seconds) * 1000000000) };
    while (nanosleep(&delay, &delay)) {
    }
}

static void marker(const char *directory, const char *name)
{
    char path[4096];
    int length = snprintf(path, sizeof path, "%s/%s", directory, name);
    assert(length > 0 && length < (int)sizeof path);
    FILE *file = fopen(path, "w");
    assert(file);
    const char *deadline = getenv("CHIBI_TEST_DEADLINE");
    fprintf(file, "%d %s\n", getpid(), deadline ? deadline : "0");
    assert(!fclose(file));
}

int main(int argc, char **argv)
{
    assert(argc >= 3);
    if (!strcmp(argv[1], "exit")) {
        return atoi(argv[2]);
    }
    if (!strcmp(argv[1], "sleep")) {
        pause_seconds(strtod(argv[2], NULL));
        return 0;
    }
    if (!strcmp(argv[1], "mark")) {
        marker(argv[2], "last-command");
        return 0;
    }
    if (!strcmp(argv[1], "sibling")) {
        pause_seconds(3);
        marker(argv[2], "sibling");
        return 0;
    }
    assert(!strcmp(argv[1], "descendant") && argc == 5);
    int child = fork();
    assert(child >= 0);
    if (!child) {
        if (atoi(argv[3])) {
            assert(setsid() >= 0);
        }
        marker(argv[2], "started");
        pause_seconds(3);
        marker(argv[2], "escaped");
        return 0;
    }
    if (atoi(argv[4])) {
        char path[4096];
        snprintf(path, sizeof path, "%s/started", argv[2]);
        for (int i = 0; i < 200 && access(path, F_OK); i++) {
            pause_seconds(.01);
        }
        assert(!access(path, F_OK));
        return 0;
    }
    pause_seconds(6);
    return 0;
}
