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

TEST_CASE(process_captured_output_drains_with_backpressure_and_preserves_exit)
{
    const char *args[] = {
        "/bin/sh",
        "-c",
        "dd if=/dev/zero bs=4096 count=64 2>/dev/null; printf end; exit 9",
        NULL
    };
    chibi_process process = { 0 };
    REQUIRE(chibi_process_start_capture(&process, args, "/tmp"));
    size_t total = 0;
    char tail[3] = { 0 };
    bool eof = false, payload = true;
    struct timespec delay = { .tv_nsec = 1000000 };
    for (int turn = 0; turn < 5000 && !(eof && process.finished); turn++) {
        unsigned char block[4096];
        int64_t count = chibi_process_read(&process, block, sizeof block);
        if (count > 0) {
            for (int64_t i = 0; i < count; i++) {
                if (total < 262144) {
                    payload = payload && block[i] == 0;
                } else if (total < 262147) {
                    tail[total - 262144] = (char)block[i];
                }
                total++;
            }
        } else if (!count) {
            eof = true;
        } else {
            REQUIRE(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
        }
        if (!process.finished) {
            chibi_process_poll(&process);
        }
        if (count <= 0 && !process.finished) {
            nanosleep(&delay, NULL);
        }
    }
    REQUIRE(eof && process.finished && process.status == 9);
    REQUIRE(payload && total == 262147 && !memcmp(tail, "end", 3));
    chibi_process_close_output(&process);
    REQUIRE(!process.capturing);
    const char *missing[] = { "/definitely/not/a/chibi-program", NULL };
    REQUIRE(!chibi_process_start_capture(&process, missing, "/tmp"));
    REQUIRE(errno == ENOENT);
    return true;
}

void test_host_process()
{
    RUN_TEST_CASE(process_arguments_output_and_cancellation);
    RUN_TEST_CASE(process_captured_output_drains_with_backpressure_and_preserves_exit);
}
