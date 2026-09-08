#define _GNU_SOURCE
#include "host/process.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum
{
    PROCESS_LIMIT = 32,
    TIMEOUT_SECONDS = 120
};

static double seconds()
{
    struct timespec time;
    assert(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return time.tv_sec + time.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: concurrent_runner COUNT COMMAND [ARGUMENT ...]\n");
        return 2;
    }
    int count = atoi(argv[1]);
    if (count < 1 || count > PROCESS_LIMIT) {
        return 2;
    }
    char directory[] = "/tmp/blueprints-concurrent-XXXXXX";
    char cwd[PATH_MAX];
    assert(mkdtemp(directory) && getcwd(cwd, sizeof cwd));
    bp_process processes[PROCESS_LIMIT] = { 0 };
    char logs[PROCESS_LIMIT][PATH_MAX];
    int started = 0, result = 0;
    for (int i = 0; i < count; i++) {
        snprintf(logs[i], sizeof logs[i], "%s/%d.log", directory, i);
        if (!bp_process_start(&processes[i], (const char *const *)(argv + 2), cwd, logs[i])) {
            perror("spawn concurrent command");
            result = 1;
            break;
        }
        started++;
    }
    double deadline = seconds() + TIMEOUT_SECONDS;
    int pending;
    do {
        pending = 0;
        if (seconds() > deadline) {
            result = 1;
        }
        for (int i = 0; i < started; i++) {
            if (result && !processes[i].finished) {
                bp_process_stop(&processes[i]);
            }
            if (!bp_process_poll(&processes[i])) {
                pending++;
            } else if (processes[i].status) {
                result = 1;
            }
        }
        if (pending) {
            nanosleep(&(struct timespec){ .tv_nsec = 10000000 }, NULL);
        }
    } while (pending);
    for (int i = 0; i < count; i++) {
        if (result) {
            FILE *log = fopen(logs[i], "r");
            if (log) {
                int byte;
                while ((byte = fgetc(log)) != EOF) {
                    fputc(byte, stderr);
                }
                fclose(log);
            }
        }
        unlink(logs[i]);
    }
    rmdir(directory);
    return result;
}
