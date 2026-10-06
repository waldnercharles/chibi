#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "host/process.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_TRIALS 5
#define MAX_SECONDS 60
#define CLEANUP_SECONDS 2

static volatile sig_atomic_t interrupted;
static volatile sig_atomic_t active_group;
static char log_path[] = "/tmp/chibi-benchmark-XXXXXX";

static void deadline_signal(int signal)
{
    (void)signal;
    if (active_group) {
        kill(-(pid_t)active_group, SIGKILL);
    }
    unlink(log_path);
    _exit(124);
}

static void handle_signal(int signal)
{
    interrupted = signal;
}

static double seconds()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static void pause_poll()
{
    struct timespec pause = { .tv_nsec = 1000000 };
    nanosleep(&pause, NULL);
}

static int number(const char *text)
{
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || !*text || *end || value < 1 || value > MAX_SECONDS) {
        return 0;
    }
    return (int)value;
}

static bool replay(const char *path)
{
    FILE *log = fopen(path, "rb");
    if (!log) {
        perror("benchmark log");
        return false;
    }
    char buffer[8192];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), log)) > 0) {
        if (fwrite(buffer, 1, count, stdout) != count) {
            fclose(log);
            return false;
        }
    }
    bool ok = !ferror(log) && fflush(stdout) == 0;
    fclose(log);
    return ok;
}

static int usage(const char *name)
{
    fprintf(stderr, "Usage: %s [--trials 3|5] [--timeout 3..60] -- binary [arguments...]\n", name);
    return 2;
}

int main(int argc, char **argv)
{
    int trials = 3;
    int timeout = MAX_SECONDS;
    int arg = 1;
    while (arg < argc && strcmp(argv[arg], "--")) {
        if (arg + 1 >= argc) {
            return usage(argv[0]);
        }
        int value = number(argv[arg + 1]);
        if (!strcmp(argv[arg], "--trials") && (value == 3 || value == 5)) {
            trials = value;
        } else if (!strcmp(argv[arg], "--timeout") && value > CLEANUP_SECONDS) {
            timeout = value;
        } else {
            return usage(argv[0]);
        }
        arg += 2;
    }
    if (arg + 1 >= argc) {
        return usage(argv[0]);
    }
    arg++;

    struct sigaction action = { .sa_handler = handle_signal };
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGPIPE, &action, NULL);
    action.sa_handler = deadline_signal;
    sigaction(SIGALRM, &action, NULL);
    alarm((unsigned)timeout);

    sigset_t alarm_mask;
    sigemptyset(&alarm_mask);
    sigaddset(&alarm_mask, SIGALRM);

    int log = mkstemp(log_path);
    if (log < 0) {
        perror("benchmark log");
        return 1;
    }
    close(log);

    double times[MAX_TRIALS];
    double deadline = seconds() + timeout - CLEANUP_SECONDS;
    int status = 0;
    for (int trial = 0; trial < trials; trial++) {
        if (interrupted || seconds() >= deadline) {
            status = interrupted ? 128 + interrupted : 124;
            break;
        }
        fprintf(stderr, "benchmark trial %d/%d\n", trial + 1, trials);
        chibi_process process;
        double start = seconds();
        /* Publish the owned group before a pending hard deadline can fire. */
        sigset_t previous_mask;
        sigprocmask(SIG_BLOCK, &alarm_mask, &previous_mask);
        bool started = chibi_process_start(&process, (const char *const *)&argv[arg], ".", log_path);
        if (started) {
            active_group = process.pid;
        }
        sigprocmask(SIG_SETMASK, &previous_mask, NULL);
        if (!started) {
            perror(argv[arg]);
            status = 1;
            break;
        }
        bool expired = false;
        while (!chibi_process_poll(&process)) {
            if (interrupted || seconds() >= deadline) {
                expired = true;
                chibi_process_stop(&process);
            }
            pause_poll();
        }
        expired |= seconds() >= deadline;
        times[trial] = seconds() - start;
        /* Successful workloads must not leave owned descendants running. */
        kill(-process.pid, SIGKILL);
        active_group = 0;
        if (!replay(log_path)) {
            status = interrupted ? 128 + interrupted : 1;
            break;
        }
        fprintf(
            stderr,
            "benchmark trial %d wall=%.6f s exit=%d\n",
            trial + 1,
            times[trial],
            process.status
        );
        if (expired || interrupted || process.status) {
            status = interrupted ? 128 + interrupted
                   : expired     ? 124
                                 : process.status;
            break;
        }
    }
    unlink(log_path);
    if (status) {
        fprintf(stderr, "benchmark stopped with status %d\n", status);
        return status;
    }
    for (int i = 1; i < trials; i++) {
        double value = times[i];
        int j = i;
        while (j > 0 && times[j - 1] > value) {
            times[j] = times[j - 1];
            j--;
        }
        times[j] = value;
    }
    fprintf(
        stderr,
        "benchmark trials=%d median=%.6f s worst=%.6f s\n",
        trials,
        times[trials / 2],
        times[trials - 1]
    );
    alarm(0);
    return 0;
}
