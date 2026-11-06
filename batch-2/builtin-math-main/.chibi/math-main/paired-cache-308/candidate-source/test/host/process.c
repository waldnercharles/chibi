#include "host/process.h"

#include "third_party/pico_headers/pico_unit.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool await_exit(chibi_process *process)
{
    struct timespec delay = { .tv_nsec = 10000000 };
    for (int i = 0; i < 500 && !chibi_process_poll(process); i++) {
        nanosleep(&delay, NULL);
    }
    return process->finished;
}

TEST_CASE(process_arguments_output_and_cancellation)
{
    char root[] = "/tmp/chibi-process-XXXXXX";
    REQUIRE(mkdtemp(root));
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
    chibi_process process = { 0 };
    REQUIRE(chibi_process_start(&process, args, root, log));
    REQUIRE(await_exit(&process));
    REQUIRE(process.status == 7);
    FILE *file = fopen(log, "r");
    REQUIRE(file);
    char output[1024] = { 0 };
    REQUIRE(fread(output, 1, sizeof output - 1, file) > 0);
    fclose(file);
    REQUIRE(strstr(output, root) && strstr(output, args[4]) && strstr(output, "error"));
    const char *missing[] = { "/definitely/not/a/chibi-program", NULL };
    REQUIRE(!chibi_process_start(&process, missing, root, log));
    REQUIRE(errno == ENOENT);
    const char *sleeping[] = { "/bin/sleep", "30", NULL };
    REQUIRE(chibi_process_start(&process, sleeping, root, log));
    REQUIRE(!chibi_process_poll(&process));
    chibi_process_stop(&process);
    REQUIRE(await_exit(&process));
    REQUIRE(process.status == 128 + SIGTERM);
    const char *stubborn[] = {
        "/bin/sh",
        "-c",
        "trap '' TERM; echo ready; while :; do sleep 1; done",
        NULL
    };
    REQUIRE(chibi_process_start(&process, stubborn, root, log));
    struct timespec delay = { .tv_nsec = 10000000 };
    bool ready = false;
    for (int i = 0; i < 300 && !ready; i++) {
        file = fopen(log, "r");
        REQUIRE(file);
        ready = fgets(output, sizeof output, file) && strstr(output, "ready");
        fclose(file);
        nanosleep(&delay, NULL);
    }
    REQUIRE(ready);
    chibi_process_stop(&process);
    REQUIRE(await_exit(&process));
    REQUIRE(process.status == 128 + SIGKILL);
    REQUIRE(unlink(log) == 0);
    REQUIRE(rmdir(root) == 0);
    return true;
}

void test_host_process()
{
    RUN_TEST_CASE(process_arguments_output_and_cancellation);
}
