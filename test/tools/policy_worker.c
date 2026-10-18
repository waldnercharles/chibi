#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
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

/* A killed descendant adopted by a Linux subreaper stays a zombie until that
 * subreaper's cleanup reaps it; it has exited all the same. */
static bool zombie(int pid)
{
#ifdef __linux__
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/stat", pid);
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    char state = 0;
    bool parsed = fscanf(file, "%*d (%*[^)]) %c", &state) == 1;
    fclose(file);
    return parsed && state == 'Z';
#else
    (void)pid;
    return false;
#endif
}

/* Probe inside the driver's lifetime, before its caller performs group cleanup. */
static int cleanup_probe(const char *directory)
{
    char path[4096];
    int length = snprintf(path, sizeof path, "%s/started", directory);
    assert(length > 0 && length < (int)sizeof path);
    FILE *record = fopen(path, "r");
    if (!record) {
        return 2;
    }
    int pid = 0;
    bool valid = fscanf(record, "%d", &pid) == 1 && pid > 0;
    fclose(record);
    if (!valid) {
        return 2;
    }
    struct timespec start, now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &start));
    for (;;) {
        errno = 0;
        if (kill(pid, 0) && errno != ESRCH) {
            return 2;
        }
        if (errno == ESRCH || zombie(pid)) {
            snprintf(path, sizeof path, "%s/escaped", directory);
            if (!access(path, F_OK)) {
                return 1;
            }
            return errno == ENOENT ? 0 : 2;
        }
        assert(!clock_gettime(CLOCK_MONOTONIC, &now));
        double elapsed = (double)(now.tv_sec - start.tv_sec) +
                         (double)(now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= 3.1) {
            return 1;
        }
        pause_seconds(.001);
    }
}

int main(int argc, char **argv)
{
    assert(argc >= 3);
    if (!strcmp(argv[1], "-S")) {
        assert(argc == 6 && !strcmp(argv[0], "cmake"));
        assert(!strcmp(argv[2], ".") && !strcmp(argv[3], "-B"));
        assert(!strcmp(argv[4], "build self-hosting"));
        assert(!strcmp(argv[5], "-DCHIBI_NOTE=space \"quote\";$literal*"));
        puts("configure");
        return !access("fail-configure", F_OK) ? 37 : 0;
    }
    if (!strcmp(argv[1], "--build")) {
        assert(argc == 3 && !strcmp(argv[0], "cmake"));
        assert(!strcmp(argv[2], "build self-hosting"));
        puts("build");
        return !access("fail-build", F_OK) ? 38 : 0;
    }
    if (!strcmp(argv[1], "build-argv")) {
        char cwd[4096];
        assert(getcwd(cwd, sizeof cwd));
        printf("cwd=%s\n", cwd);
        printf("allowed=%s\n", getenv("CHIBI_BUILD_ALLOWED") ? getenv("CHIBI_BUILD_ALLOWED") : "");
        printf("secret=%s\n", getenv("CHIBI_BUILD_SECRET") ? getenv("CHIBI_BUILD_SECRET") : "");
        printf("deadline=%s\n", getenv("CHIBI_TEST_DEADLINE") ? getenv("CHIBI_TEST_DEADLINE") : "");
        for (int i = 2; i < argc; i++) {
            printf("arg=%zu:%s\n", strlen(argv[i]), argv[i]);
        }
        fputs("worker-stderr\n", stderr);
        return 23;
    }
    if (!strcmp(argv[1], "build-output")) {
        for (int i = 0; i < 16384; i++) {
            fputc('o', stdout);
            fputc('e', stderr);
        }
        return 0;
    }
    if (!strcmp(argv[1], "build-signal")) {
        raise(SIGTERM);
        return 99;
    }
    if (!strcmp(argv[1], "cleanup-probe")) {
        return cleanup_probe(argv[2]);
    }
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
        marker(argv[2], "sibling-ready");
        char path[4096];
        int length = snprintf(path, sizeof path, "%s/sibling-release", argv[2]);
        assert(length > 0 && length < (int)sizeof path);
        for (int i = 0; i < 6000 && access(path, F_OK); i++) {
            pause_seconds(.001);
        }
        assert(!access(path, F_OK));
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
