#include "host/process.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void await_exit(bp_process *process)
{
    struct timespec delay = { .tv_nsec = 10000000 };
    for (int i = 0; i < 500 && !bp_process_poll(process); i++) {
        nanosleep(&delay, NULL);
    }
    assert(process->finished);
}

void test_dev_process()
{
    char root[] = "/tmp/blueprints-process-XXXXXX";
    assert(mkdtemp(root));
    char log[512];
    snprintf(log, sizeof log, "%s/output.log", root);
    const char *args[] = {
        "/bin/sh",
        "-c",
        "printf '%s\\n' \"$PWD\" \"$1\"; printf error >&2; exit 7",
        "test",
        "literal spaces ; $() `quotes`",
        NULL
    };
    bp_process process = { 0 };
    assert(bp_process_start(&process, args, root, log));
    await_exit(&process);
    assert(process.status == 7);
    FILE *file = fopen(log, "r");
    assert(file);
    char output[1024] = { 0 };
    assert(fread(output, 1, sizeof output - 1, file) > 0);
    fclose(file);
    assert(strstr(output, root) && strstr(output, args[4]) && strstr(output, "error"));
    const char *missing[] = { "/definitely/not/a/blueprints-program", NULL };
    assert(!bp_process_start(&process, missing, root, log));
    assert(errno == ENOENT);
    const char *sleeping[] = { "/bin/sleep", "30", NULL };
    assert(bp_process_start(&process, sleeping, root, log));
    assert(!bp_process_poll(&process));
    bp_process_stop(&process);
    await_exit(&process);
    assert(process.status == 128 + SIGTERM);
    const char *stubborn[] = {
        "/bin/sh",
        "-c",
        "trap '' TERM; echo ready; while :; do sleep 1; done",
        NULL
    };
    assert(bp_process_start(&process, stubborn, root, log));
    struct timespec delay = { .tv_nsec = 10000000 };
    bool ready = false;
    for (int i = 0; i < 300 && !ready; i++) {
        file = fopen(log, "r");
        assert(file);
        ready = fgets(output, sizeof output, file) && strstr(output, "ready");
        fclose(file);
        nanosleep(&delay, NULL);
    }
    assert(ready);
    bp_process_stop(&process);
    await_exit(&process);
    assert(process.status == 128 + SIGKILL);
    assert(unlink(log) == 0);
    assert(rmdir(root) == 0);
    puts("PASS: native processes preserve argv/cwd, capture output, report failure and cancel without blocking");
}
