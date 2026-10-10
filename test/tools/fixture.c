#include "test/tools/fixture.h"

#include "host/process.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double deadline;
static volatile sig_atomic_t owned_group;

static double seconds()
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return now.tv_sec + now.tv_nsec / 1000000000.0;
}

static void expired(int signal)
{
    if (owned_group) {
        kill(-owned_group, SIGKILL);
    }
    static const char
        message[] = "FAIL: native tooling aggregate exceeded 60 seconds\n";
    if (signal == SIGALRM) {
        write(STDERR_FILENO, message, sizeof message - 1);
    }
    _exit(signal == SIGALRM ? 124 : 128 + signal);
}

void tool_init()
{
    deadline = seconds() + 60;
    signal(SIGALRM, expired);
    signal(SIGINT, expired);
    signal(SIGTERM, expired);
    alarm(60);
}

void tool_own_process(int pid)
{
    assert(pid > 0 && !owned_group);
    owned_group = pid;
}

void tool_release_process(int pid)
{
    assert(owned_group == pid);
    owned_group = 0;
}

const char *tool_root()
{
    return CHIBI_SOURCE_ROOT;
}

int tool_run(const char *const argv[], const char *cwd, const char *logpath)
{
    return tool_run_within(argv, cwd, logpath, TOOL_RUN_SECONDS);
}

int tool_run_within(const char *const argv[], const char *cwd, const char *logpath, double allowed)
{
    double start = seconds(), limit = start + allowed;
    if (limit > deadline - 2) {
        limit = deadline - 2;
    }
    if (seconds() >= limit) {
        return 124;
    }
    chibi_process process;
    if (!chibi_process_start(&process, argv, cwd, logpath)) {
        perror(argv[0]);
        return 127;
    }
    tool_own_process(process.pid);
    bool timeout = false;
    struct timespec pause = { .tv_nsec = 1000000 };
    while (!chibi_process_poll(&process)) {
        if (seconds() >= limit) {
            timeout = true;
            chibi_process_stop(&process);
        }
        nanosleep(&pause, NULL);
    }
    timeout |= seconds() >= limit;
    kill(-process.pid, SIGKILL);
    tool_release_process(process.pid);
    if (timeout || process.status) {
        char *output = tool_read(logpath);
        if (output) {
            fputs(output, stderr);
            free(output);
        }
    }
    if (timeout) {
        fprintf(stderr, "stopped after %.0f seconds:", limit - start);
        for (const char *const *arg = argv; *arg; arg++) {
            fprintf(stderr, " %s", *arg);
        }
        fputc('\n', stderr);
    }
    return timeout ? 124 : process.status;
}

void tool_write(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    size_t length = strlen(text);
    assert(fwrite(text, 1, length, file) == length);
    assert(!fclose(file));
}

char *tool_read(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length >= 0 && !fseek(file, 0, SEEK_SET));
    char *text = xmalloc((size_t)length + 1);
    assert(fread(text, 1, (size_t)length, file) == (size_t)length);
    text[length] = '\0';
    assert(!fclose(file));
    return text;
}

void tool_remove_tree(const char *path)
{
    struct stat info;
    assert(!lstat(path, &info));
    if (!S_ISDIR(info.st_mode)) {
        assert(!unlink(path));
        return;
    }
    DIR *directory = opendir(path);
    assert(directory);
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        char child[PATH_MAX];
        int size = snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
        assert(size > 0 && size < (int)sizeof child);
        tool_remove_tree(child);
    }
    assert(!closedir(directory));
    assert(!rmdir(path));
}
