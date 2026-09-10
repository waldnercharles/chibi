#define _GNU_SOURCE
#include "host/process.h"

#include <assert.h>
#include <glob.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static chibi_process host;
static char directory[PATH_MAX];
static char host_log[PATH_MAX];
static const char *cmake;

static void pause_poll()
{
    struct timespec delay = { .tv_nsec = 10000000 };
    nanosleep(&delay, NULL);
}
static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 0 && fseek(file, 0, SEEK_SET) == 0);
    char *text = calloc((size_t)size + 1, 1);
    assert(text);
    assert(fread(text, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    return text;
}
static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}
static void failed(const char *message)
{
    fprintf(stderr, "FAIL: %s; artifacts: %s\n", message, directory);
    char *log = read_file(host_log);
    if (log) {
        fputs(log, stderr);
        free(log);
    }
    if (host.pid && !chibi_process_poll(&host)) {
        chibi_process_stop(&host);
        while (!chibi_process_poll(&host)) {
            pause_poll();
        }
    }
    exit(1);
}
static void run(const char *const *args)
{
    chibi_process process = { 0 };
    char log[PATH_MAX];
    snprintf(log, sizeof log, "%s/command.log", directory);
    if (!chibi_process_start(&process, args, directory, log)) {
        failed("could not start command");
    }
    int ticks = 0;
    while (!chibi_process_poll(&process) && ticks++ < 12000) {
        pause_poll();
    }
    if (!process.finished) {
        chibi_process_stop(&process);
        while (!chibi_process_poll(&process)) {
            pause_poll();
        }
    }
    if (process.status) {
        char *output = read_file(log);
        if (output) {
            fputs(output, stderr);
            free(output);
        }
        failed("command failed");
    }
}
static int occurrences(const char *text, const char *needle)
{
    int count = 0;
    while (text && (text = strstr(text, needle))) {
        count++;
        text += strlen(needle);
    }
    return count;
}
static void await_file_log(const char *path, const char *needle, int count)
{
    for (int i = 0; i < 12000; i++) {
        if (chibi_process_poll(&host)) {
            failed("host exited while waiting for log");
        }
        char *text = read_file(path);
        bool found = occurrences(text, needle) >= count;
        free(text);
        if (found) {
            return;
        }
        pause_poll();
    }
    failed(needle);
}
static void await_log(const char *needle, int count)
{
    await_file_log(host_log, needle, count);
}
static void close_host()
{
    if (kill(host.pid, SIGINT)) {
        failed("could not close host");
    }
    for (int i = 0; i < 1000 && !chibi_process_poll(&host); i++) {
        pause_poll();
    }
    if (!host.finished || host.status) {
        failed("host failed graceful close");
    }
}

static void check_rejected_checkpoint(const char *checkpoint, const char *expected, bool warning)
{
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof pattern, "%s.rejected-*", checkpoint);
    glob_t matches = { 0 };
    if (glob(pattern, 0, NULL, &matches) || matches.gl_pathc != 1) {
        failed("rejected checkpoint was not preserved exactly once");
    }
    char *saved = read_file(matches.gl_pathv[0]);
    if (!saved || strcmp(saved, expected)) {
        failed("rejected checkpoint contents changed");
    }
    free(saved);
    if (warning) {
        char *log = read_file(host_log);
        if (!log || !strstr(log, matches.gl_pathv[0])) {
            failed("startup warning did not identify preserved checkpoint");
        }
        free(log);
    }
    globfree(&matches);
}

int main(int argc, char **argv)
{
    if (argc != 5 || (strcmp(argv[4], "controller") && strcmp(argv[4], "editor"))) {
        fprintf(stderr, "usage: %s ROOT BUILD_DIR CMAKE controller|editor\n", argv[0]);
        return 2;
    }
    bool controller = !strcmp(argv[4], "controller");
    cmake = argv[3];
    snprintf(directory, sizeof directory, "/tmp/chibi-dev-%s-XXXXXX", argv[4]);
    assert(mkdtemp(directory));
    snprintf(host_log, sizeof host_log, "%s/host.log", directory);
    char root_arg[PATH_MAX], build_arg[PATH_MAX], test_arg[PATH_MAX], script[PATH_MAX];
    snprintf(root_arg, sizeof root_arg, "-DROOT=%s", argv[1]);
    snprintf(build_arg, sizeof build_arg, "-DBUILD_DIR=%s", argv[2]);
    snprintf(test_arg, sizeof test_arg, "-DTEST_DIR=%s", directory);
    snprintf(script, sizeof script, "%s/cmake/tests/dev_stage.cmake", argv[1]);
    const char *stage[] = { cmake,
                            root_arg,
                            build_arg,
                            test_arg,
                            "-DPREPARE=ON",
                            controller ? "-DCONTROLLER=ON" : "-DCONTROLLER=OFF",
                            "-P",
                            script,
                            NULL };
    run(stage);
    char path[PATH_MAX], manifest[PATH_MAX], checkpoint[PATH_MAX],
        generation[PATH_MAX];
    snprintf(path, sizeof path, "%s/host.txt", directory);
    char *executable = read_file(path);
    snprintf(path, sizeof path, "%s/directory.txt", directory);
    char *libraries = read_file(path);
    assert(executable && libraries);
#ifdef __APPLE__
    assert(setenv("DYLD_LIBRARY_PATH", libraries, 1) == 0);
#else
    assert(setenv("LD_LIBRARY_PATH", libraries, 1) == 0);
#endif
    assert(setenv("MallocNanoZone", "0", 1) == 0);
    snprintf(manifest, sizeof manifest, "%s/module.txt", directory);
    snprintf(generation, sizeof generation, "%s/generation.json", directory);
    snprintf(checkpoint, sizeof checkpoint, controller ? "%s/.chibi/workspace.json" : "%s/workspace.json", directory);
    char window[PATH_MAX];
    snprintf(window, sizeof window, "%s.window", checkpoint);
    const char
        *rejected = "{\"version\":999,\"unsaved\":\"preserve this work\"}\n";
    if (!controller) {
        write_file(window, "10 20 1280 720");
        write_file(checkpoint, rejected);
    }
    const char *launch[] = { executable,
                             controller ? "--bootstrap" : "--module",
                             controller ? generation : manifest,
                             "--checkpoint",
                             checkpoint,
                             "--headless",
                             "--fps",
                             "60",
                             NULL };
    if (!chibi_process_start(&host, launch, argv[1], host_log)) {
        failed("could not launch host");
    }
    pid_t initial_pid = host.pid;
    await_log("Loaded application ", 1);
    if (!controller) {
        check_rejected_checkpoint(checkpoint, rejected, true);
    }
    if (controller) {
        snprintf(path, sizeof path, "%s/src/application.c", directory);
        char *source = read_file(path);
        assert(source);
        write_file(path, "invalid C;\n");
        await_log("Build rejected;", 1);
        if (host.pid != initial_pid || kill(initial_pid, 0)) {
            failed("compile failure replaced host");
        }
        size_t length = strlen(source);
        char *changed = malloc(length + 64);
        assert(changed);
        snprintf(changed, length + 64, "%s\n/* valid new generation */\n", source);
        write_file(path, changed);
        free(changed);
        free(source);
        await_log("Loaded application ", 2);
        /* Edit again after compilation began. The first completed build must
         * be discarded; only the subsequent generation may be loaded. */
        source = read_file(path);
        assert(source);
        length = strlen(source);
        changed = malloc(length + 256);
        assert(changed);
        char build_log[PATH_MAX];
        snprintf(build_log, sizeof build_log, "%s/.chibi/build.log", directory);
        unlink(build_log);
        snprintf(
            changed,
            length + 256,
            "%s\n__attribute__((constructor)) static void "
            "stale_generation(void) "
            "{ fputs(\"Fixture stale generation loaded\\n\", stderr); }\n",
            source
        );
        write_file(path, changed);
        await_log("Building application;", 3);
        await_file_log(build_log, "Fixture build pause", 1);
        snprintf(
            changed,
            length + 256,
            "%s\n__attribute__((constructor)) static void "
            "newest_generation(void) "
            "{ fputs(\"Fixture newest generation loaded\\n\", stderr); }\n",
            source
        );
        write_file(path, changed);
        free(changed);
        free(source);
        await_log("Loaded application ", 3);
        await_log("Fixture newest generation loaded", 1);
        snprintf(path, sizeof path, "%s/src/resident.glsl", directory);
        write_file(path, "changed resident resource\n");
        await_log("Restart required:", 1);
        for (int i = 0; i < 100; i++) {
            pause_poll();
        }
        char *log = read_file(host_log);
        if (occurrences(log, "Loaded application ") != 3 ||
            strstr(log, "Fixture stale generation loaded")) {
            failed("stale or resident generation was loaded");
        }
        free(log);
    } else {
        for (int i = 0; i < 3; i++) {
            stage[4] = "-DPREPARE=OFF";
            run(stage);
            await_log("Loaded application ", i + 2);
        }
    }
    if (host.pid != initial_pid || kill(initial_pid, 0)) {
        failed("reload replaced host process");
    }
    close_host();
    char *saved = read_file(checkpoint);
    if (!saved ||
        (controller ? atoi(saved) <= 0
                    : !strstr(saved, "\"scene\"") || !strstr(saved, "\"graph\""))) {
        failed("close did not save workspace checkpoint");
    }
    if (!controller) {
        char *geometry = read_file(window);
        int x, y, width, height;
        if (!geometry || sscanf(geometry, "%d %d %d %d", &x, &y, &width, &height) != 4 ||
            width != 1280 || height != 720) {
            failed("window geometry changed");
        }
        free(geometry);
    }
    if (controller) {
        snprintf(path, sizeof path, "%s/.chibi/build.log", directory);
        unlink(path);
    }
    if (!chibi_process_start(&host, launch, argv[1], host_log)) {
        failed("could not restart host");
    }
    await_log("Loaded application ", 1);
    if (controller) {
        char marker[64];
        snprintf(marker, sizeof marker, "Fixture restored %d", atoi(saved));
        await_log(marker, 1);
        /* A pending compile must be cancelled before the resident host exits. */
        snprintf(path, sizeof path, "%s/src/application.c", directory);
        char *source = read_file(path);
        assert(source);
        FILE *file = fopen(path, "a");
        assert(file && fputs("\nint cancel_this_build = 1;\n", file) >= 0);
        assert(fclose(file) == 0);
        free(source);
        char build_log[PATH_MAX];
        snprintf(build_log, sizeof build_log, "%s/.chibi/build.log", directory);
        await_log("Building application;", 1);
        await_file_log(build_log, "Fixture build pause", 1);
    }
    close_host();
    if (!controller) {
        check_rejected_checkpoint(checkpoint, rejected, false);
    }
    free(saved);
    free(executable);
    free(libraries);
    const char *cleanup[] = { cmake, "-E", "remove_directory", directory, NULL };
    /* The cleanup child needs a cwd outside the directory it removes. */
    chibi_process cleanup_process = { 0 };
    assert(chibi_process_start(&cleanup_process, cleanup, "/tmp", "/dev/null"));
    while (!chibi_process_poll(&cleanup_process)) {
        pause_poll();
    }
    assert(cleanup_process.status == 0);
    printf("PASS: %s reload preserves process, graceful checkpoint and manual restart\n", argv[4]);
    return 0;
}
