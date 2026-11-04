/* UNARMED source. One fixed synthetic fixture; no arbitrary-command interface.
 * fd 3: one-byte owner commands. fd 4: bounded root evidence. fd 6: births.
 * The future admission must bind the compiled ELF, /bin/sh, sleep, and closure.
 * No compilation or execution has been admitted by this source packet.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FIXTURE "/workspace/shared/chibi-graph-c-runtime-20261104-revision01/controls/artifacts/fixture"
#define WORK "/workspace/shared/chibi-graph-c-runtime-20261104-revision01/controls/work"
#define SCRIPT "\"" FIXTURE "\" --branch & wait"
#define REAL_SCRIPT "(sleep 0.2; printf late > \"$1\") & wait"
#define LIMIT 8

struct birth { int role; pid_t pid, parent; };
struct child { struct birth birth; int pidfd; };
static const char *const environment[] = {
    "PATH=/usr/bin:/bin", "LC_ALL=C", "LANG=C", "TZ=UTC", NULL
};

static void die(const char *operation) {
    dprintf(4, "{\"event\":\"fixture_error\",\"operation\":\"%s\",\"errno\":%d}\n", operation, errno);
    _exit(111);
}

static double now(clockid_t clock) {
    struct timespec value;
    if (clock_gettime(clock, &value)) die("clock_gettime");
    return value.tv_sec + value.tv_nsec / 1000000000.0;
}

/* Only synthetic roles wait here. The real deadline shell is never stopped. */
static void stay(void) {
    for (;;) pause();
}

static void birth(int role) {
    struct birth value = {role, getpid(), getppid()};
    if (write(6, &value, sizeof(value)) != (ssize_t)sizeof(value)) die("birth_write");
}

static void branch(int role) {
    birth(role);
    pid_t leaf = fork();
    if (leaf < 0) die("leaf_fork");
    if (!leaf) {
        birth(role + 1);
        stay();
    }
    stay();
}

static void identity(const struct birth *item) {
    char path[64], line[4096];
    snprintf(path, sizeof(path), "/proc/%d/stat", item->pid);
    FILE *stream = fopen(path, "re");
    if (!stream) die("identity_open");
    if (!fgets(line, sizeof(line), stream)) die("identity_read");
    if (fclose(stream)) die("identity_close");
    char *tail = strrchr(line, ')');
    if (!tail) die("identity_format");
    char state;
    long parent, group, session;
    unsigned long long start;
    /* stat field 22 is starttime, after state and 18 numeric fields. */
    if (sscanf(tail + 2,
               "%c %ld %ld %ld %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %llu",
               &state, &parent, &group, &session, &start) != 5) die("identity_parse");
    dprintf(4, "{\"event\":\"created\",\"role\":%d,\"pid\":%d,\"creator\":%d,"
               "\"ppid\":%ld,\"pgid\":%ld,\"sid\":%ld,\"start\":%llu,\"state\":\"%c\"}\n",
            item->role, item->pid, item->parent, parent, group, session, start, state);
}

static void wait_exact(pid_t pid) {
    int status;
    pid_t got;
    do { got = waitpid(pid, &status, 0); } while (got < 0 && errno == EINTR);
    if (got != pid) die("shell_wait");
    dprintf(4, "{\"event\":\"fixture_reaped\",\"pid\":%d,\"raw_status\":%d}\n", pid, status);
}

static void signal_exact(const struct child *child) {
    if (syscall(SYS_pidfd_send_signal, child->pidfd, SIGKILL, NULL, 0) && errno != ESRCH)
        die("fixture_pidfd_signal");
}

static void sleep_until(double endpoint) {
    struct timespec value = {(time_t)endpoint, (long)((endpoint - (time_t)endpoint) * 1e9)};
    int error;
    do { error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &value, NULL); } while (error == EINTR);
    if (error) { errno = error; die("fixture_timed_wait"); }
}

int main(int argc, char **argv) {
    /* Fake shell is the same admitted fixture ELF with the exact shell argv. */
    if (!strcmp(argv[0], "/bin/sh")) {
        pid_t child = fork();
        if (child < 0) die("fake_shell_fork");
        if (!child) branch(2);
        stay();
    }
    if (argc == 2 && !strcmp(argv[1], "--branch")) { branch(2); return 111; }
    if (argc != 3 || strcmp(argv[1], "--root")) return 64;
    char *end;
    long number = strtol(argv[2], &end, 10);
    if (*end || number < 1 || number > 38) return 64;
    int case_id = (int)number;
    /* Root must already have passed the owner's pre-exec setsid gate. */
    if (getsid(0) != getpid() || getpgrp() != getpid()) return 65;
    alarm(5);  /* Independent fixture ceiling, never an extra owner budget. */
    int report[2];
    if (pipe2(report, O_CLOEXEC)) die("birth_pipe");
    /* fd 6 is reserved by the fixed owner; duplicate without losing report[0]. */
    int report_read = fcntl(report[0], F_DUPFD_CLOEXEC, 10);
    int report_write = fcntl(report[1], F_DUPFD_CLOEXEC, 10);
    if (report_read < 0 || report_write < 0) die("birth_duplicate");
    close(report[0]); close(report[1]);
    if (dup2(report_write, 6) < 0) die("birth_fd6");
    close(report_write);
    int real = case_id == 18 || case_id == 19;
    int bare = case_id == 10 || case_id == 11 || case_id == 37;
    char marker[256];
    snprintf(marker, sizeof(marker), WORK "/case-%02d/late-child", case_id);
    char *shell_argv[] = {"/bin/sh", "-c", real ? REAL_SCRIPT : SCRIPT, "deadline", marker, NULL};
    if (case_id == 4) shell_argv[3] = "wrong-deadline";
    if (bare) shell_argv[2] = "read x <&5";
    int block[2];
    if (pipe2(block, O_CLOEXEC)) die("bare_pipe");
    int block_read = fcntl(block[0], F_DUPFD_CLOEXEC, 10);
    int block_write = fcntl(block[1], F_DUPFD_CLOEXEC, 10);
    if (block_read < 0 || block_write < 0) die("bare_duplicate");
    close(block[0]); close(block[1]);
    double begin_wall = now(CLOCK_REALTIME), begin_mono = now(CLOCK_MONOTONIC);
    pid_t shell = fork();
    if (shell < 0) die("shell_fork");
    if (!shell) {
        close(report_read); close(block_write);
        if (setpgid(0, 0)) die("shell_setpgid");
        if (bare && dup2(block_read, 5) < 0) die("bare_fd5");
        close(block_read);
        /* Fixed executable only. No argv or environment supplied by caller. */
        execve(case_id == 5 ? FIXTURE : "/bin/sh", shell_argv, (char *const *)environment);
        _exit(127);
    }
    close(block_read);
    if (setpgid(shell, shell) && errno != EACCES && errno != ESRCH) die("parent_setpgid");
    pid_t sibling = -1;
    if (case_id == 3) {
        sibling = fork();
        if (sibling < 0) die("sibling_fork");
        if (!sibling) {
            close(report_read); close(block_write);
            if (setpgid(0, shell)) die("sibling_setpgid");
            branch(4);
        }
    }
    close(6);
    dprintf(4, "{\"event\":\"shell_spawn\",\"pid\":%d,\"wall\":%.9f,\"mono\":%.9f}\n", shell, begin_wall, begin_mono);
    if (real) {
        /* Exact required shell behavior, no observer-ready handshake. */
        sleep_until(begin_mono + 0.020);
        double cancellation = now(CLOCK_MONOTONIC);
        if (kill(-shell, SIGKILL) && errno != ESRCH) die("real_cancel_group");
        wait_exact(shell);
        sleep_until(now(CLOCK_MONOTONIC) + 0.250);
        struct stat st;
        int absent = lstat(marker, &st) < 0 && errno == ENOENT;
        dprintf(4, "{\"event\":\"real20ms\",\"cancel_mono\":%.9f,\"check_mono\":%.9f,\"marker_absent\":%s}"
                   "\n", cancellation, now(CLOCK_MONOTONIC), absent ? "true" : "false");
        /* The real shell's internal child count is NOT manufactured here. */
        dprintf(4, "{\"event\":\"real_child_count\",\"complete\":false}\n");
        return absent ? 0 : 7;
    }
    struct child children[LIMIT];
    int count = 0;
    children[count].birth = (struct birth){1, shell, getpid()};
    children[count].pidfd = syscall(SYS_pidfd_open, shell, 0);
    if (children[count].pidfd < 0) die("shell_pidfd");
    identity(&children[count++].birth);
    int target = bare ? 1 : case_id == 3 ? 5 : 3;
    while (count < target) {
        struct birth next;
        struct pollfd pollfd = {report_read, POLLIN, 0};
        if (poll(&pollfd, 1, 1000) != 1) die("birth_timeout");
        if (read(report_read, &next, sizeof(next)) != (ssize_t)sizeof(next)) die("birth_read");
        if (next.pid <= 0 || next.role < 2 || next.role > 5) die("birth_invalid");
        for (int i = 0; i < count; ++i) if (children[i].birth.pid == next.pid) die("birth_duplicate_pid");
        children[count].birth = next;
        children[count].pidfd = syscall(SYS_pidfd_open, next.pid, 0);
        if (children[count].pidfd < 0) die("descendant_pidfd");
        identity(&children[count++].birth);
    }
    close(report_read);
    dprintf(4, "{\"event\":\"fixture_ready\",\"created_count\":%d,\"complete\":true}\n", count);
    for (int commands = 0; commands < 3; ++commands) {
        struct pollfd p = {3, POLLIN, 0};
        if (poll(&p, 1, 3000) != 1) die("command_timeout");
        char command;
        if (read(3, &command, 1) != 1) die("command_read");
        if (command == 'S') { /* Parent loss / live adoption, synthetic only. */
            signal_exact(&children[0]);
            wait_exact(shell);
            dprintf(4, "{\"event\":\"shell_retired\"}\n");
            continue;
        }
        if (command == 'L') { /* Finish the synthetic live-to-zombie transition. */
            for (int i = count - 1; i > 0; --i) signal_exact(&children[i]);
            return 0;
        }
        if (command == 'R' && bare) {
            signal_exact(&children[0]);
            wait_exact(shell);
            dprintf(4, "{\"event\":\"shell_retired\"}\n");
            return 0;
        }
        if (command == 'K') {
            if (kill(-shell, SIGKILL) && errno != ESRCH) die("synthetic_cancel_group");
            wait_exact(shell);
            if (sibling > 0) wait_exact(sibling);
            close(block_write);
            return case_id == 35 ? 7 : 0;
        }
        errno = EINVAL;
        die("unknown_command");
    }
    return 111;
}
