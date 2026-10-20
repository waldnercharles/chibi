#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "host/process.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#include "support/xalloc.h"

#include <windows.h>

/* Appends `arg` quoted as CommandLineToArgvW and the C runtime split it:
 * backslashes double only before a quote. */
static char *quote(char *out, const char *arg)
{
    if (arg[0] && !strpbrk(arg, " \t\n\v\"")) {
        size_t length = strlen(arg);
        memcpy(out, arg, length + 1);
        return out + length;
    }
    *out++ = '"';
    for (const char *c = arg;; c++) {
        size_t slashes = 0;
        for (; *c == '\\'; c++) {
            slashes++;
        }
        size_t doubled = !*c || *c == '"' ? 2 * slashes : slashes;
        memset(out, '\\', doubled);
        out += doubled;
        if (!*c) {
            break;
        }
        if (*c == '"') {
            *out++ = '\\';
        }
        *out++ = *c;
    }
    *out++ = '"';
    *out = 0;
    return out;
}

/* The command line CreateProcess takes for `argv`, owned by the caller. */
static char *command_line(char *const *argv)
{
    size_t size = 1;
    for (char *const *arg = argv; *arg; arg++) {
        size += 2 * strlen(*arg) + 3;
    }
    char *line = xmalloc(size), *out = line;
    for (char *const *arg = argv; *arg; arg++) {
        if (arg != argv) {
            *out++ = ' ';
        }
        out = quote(out, *arg);
    }
    *out = 0;
    return line;
}

int chibi_process_run(char *const *argv)
{
    char *line = command_line(argv);
    STARTUPINFOA startup = { .cb = sizeof startup };
    PROCESS_INFORMATION info;
    bool started = CreateProcessA(NULL, line, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &info);
    free(line);
    if (!started) {
        fprintf(stderr, "Cannot run %s: error %lu\n", argv[0], GetLastError());
        return 1;
    }
    CloseHandle(info.hThread);
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD status = 1;
    GetExitCodeProcess(info.hProcess, &status);
    CloseHandle(info.hProcess);
    return (int)status;
}
#else

#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static uint64_t milliseconds()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

double chibi_process_now()
{
    struct timespec now;
#ifdef __APPLE__
    /* Python's time.monotonic uses mach_absolute_time, excluding system sleep.
     * CLOCK_MONOTONIC includes sleep on macOS and has a different epoch. */
    const clockid_t clock = CLOCK_UPTIME_RAW;
#else
    const clockid_t clock = CLOCK_MONOTONIC;
#endif
    if (clock_gettime(clock, &now)) {
        return -1;
    }
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000;
}

#define CAPTURE_STRING_LIMIT 1024
#define CAPTURE_BYTE_LIMIT (1024 * 1024)
#define CAPTURE_DRAIN_LIMIT 16
#define CAPTURE_POLL_MS 10

static bool capture_strings(const char *const *strings, size_t *bytes)
{
    if (!strings) {
        return false;
    }
    for (int i = 0; i < CAPTURE_STRING_LIMIT; i++) {
        if (!strings[i]) {
            return true;
        }
        size_t size = strnlen(strings[i], CAPTURE_BYTE_LIMIT + 1);
        if (size > CAPTURE_BYTE_LIMIT || *bytes > CAPTURE_BYTE_LIMIT - size) {
            return false;
        }
        *bytes += size;
    }
    return false;
}

static void capture_close(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static bool capture_pipe(int fds[2])
{
    if (pipe(fds)) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        /* Keep spawn actions independent of closed parent standard streams. */
        if (fds[i] < 3) {
            int fd = fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
            if (fd < 0) {
                return false;
            }
            close(fds[i]);
            fds[i] = fd;
        }
        if (fcntl(fds[i], F_SETFD, FD_CLOEXEC)) {
            return false;
        }
    }
    return fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0;
}

static bool capture_read(int *fd, chibi_process_output *output)
{
    char buffer[4096];
    /* Bound each drain so continuous writers cannot delay deadline checks. */
    for (int i = 0; i < CAPTURE_DRAIN_LIMIT && *fd >= 0; i++) {
        ssize_t count = read(*fd, buffer, sizeof buffer);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return errno == EAGAIN || errno == EWOULDBLOCK;
        }
        if (!count) {
            capture_close(fd);
            return true;
        }
        size_t available = output->capacity ? output->capacity - 1 - output->size : 0;
        size_t copied = (size_t)count < available ? (size_t)count : available;
        if (copied) {
            memcpy(output->data + output->size, buffer, copied);
            output->size += copied;
            output->data[output->size] = 0;
        }
        output->truncated |= copied < (size_t)count;
    }
    return true;
}

chibi_process_capture_result chibi_process_capture(
    const char *executable,
    const char *const *argv,
    const char *cwd,
    const char *const *envp,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    chibi_process_output *out,
    chibi_process_output *err
)
{
    chibi_process_capture_result result = { .error = CHIBI_PROCESS_INVALID_INPUT };
    chibi_process_output *outputs[] = { out, err };
    for (int i = 0; i < 2; i++) {
        if (outputs[i]) {
            outputs[i]->size = 0;
            outputs[i]->truncated = false;
            if (outputs[i]->capacity && outputs[i]->data) {
                outputs[i]->data[0] = 0;
            }
        }
    }
    size_t bytes = 0;
    if (!executable || executable[0] != '/' || !argv || !argv[0] || !argv[0][0] ||
        !cwd || !cwd[0] || !isfinite(deadline) || deadline <= 0 || !out || !err ||
        out == err || (out->capacity && !out->data) || (err->capacity && !err->data) ||
        !capture_strings(argv, &bytes) || !capture_strings(envp, &bytes)) {
        return result;
    }
    double now = chibi_process_now();
    if (now < 0) {
        return (chibi_process_capture_result){ CHIBI_PROCESS_HOST_FAILED, 0, errno };
    }
    if (cancelled && *cancelled) {
        return (chibi_process_capture_result){ .error = CHIBI_PROCESS_CANCELLED };
    }
    if (now >= deadline) {
        return (chibi_process_capture_result){ .error = CHIBI_PROCESS_TIMEOUT };
    }
    int pipes[2][2] = { { -1, -1 }, { -1, -1 } };
    result.error = CHIBI_PROCESS_HOST_FAILED;
    if (!capture_pipe(pipes[0]) || !capture_pipe(pipes[1])) {
        result.system_error = errno;
        goto close_pipes;
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        result.system_error = error;
        goto close_pipes;
    }
    error = posix_spawnattr_init(&attributes);
    if (error) {
        posix_spawn_file_actions_destroy(&actions);
        result.system_error = error;
        goto close_pipes;
    }
    for (int i = 0; !error && i < 2; i++) {
        error = posix_spawn_file_actions_adddup2(&actions, pipes[i][1], i + 1);
    }
    if (!error) {
        error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    }
    if (!error) {
#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) &&           \
    __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
        error = posix_spawn_file_actions_addchdir(&actions, cwd);
#else
        error = posix_spawn_file_actions_addchdir_np(&actions, cwd);
#endif
    }
    if (!error) {
        error = posix_spawnattr_setpgroup(&attributes, 0);
    }
    sigset_t signals;
    sigemptyset(&signals);
    if (!error) {
        error = posix_spawnattr_setsigmask(&attributes, &signals);
    }
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGPIPE);
    if (!error) {
        error = posix_spawnattr_setsigdefault(&attributes, &signals);
    }
    if (!error) {
        error = posix_spawnattr_setflags(
            &attributes,
            POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF
        );
    }
    pid_t pid = 0;
    if (!error) {
        result.error = CHIBI_PROCESS_SPAWN_FAILED;
        error = posix_spawn(&pid, executable, &actions, &attributes, (char *const *)argv, (char *const *)envp);
    }
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error) {
        result.system_error = error;
        goto close_pipes;
    }
    capture_close(&pipes[0][1]);
    capture_close(&pipes[1][1]);
    result.error = CHIBI_PROCESS_OK;
    bool leader_finished = false;
    int status = 0;
    for (;;) {
        now = chibi_process_now();
        if (now < 0) {
            result.error = CHIBI_PROCESS_HOST_FAILED;
            result.system_error = errno;
        } else if (cancelled && *cancelled) {
            result.error = CHIBI_PROCESS_CANCELLED;
        } else if (now >= deadline) {
            result.error = CHIBI_PROCESS_TIMEOUT;
        }
        if (result.error != CHIBI_PROCESS_OK) {
            break;
        }
        siginfo_t info = { 0 };
        int waited = waitid(P_PID, (id_t)pid, &info, WEXITED | WNOHANG | WNOWAIT);
        if (!waited && info.si_pid == pid) {
            leader_finished = true;
            break;
        }
        if (waited < 0 && errno != EINTR) {
            result.error = CHIBI_PROCESS_HOST_FAILED;
            result.system_error = errno;
            break;
        }
        if (!capture_read(&pipes[0][0], out) || !capture_read(&pipes[1][0], err)) {
            result.error = CHIBI_PROCESS_HOST_FAILED;
            result.system_error = errno;
            break;
        }
        struct pollfd fds[2] = { { pipes[0][0], POLLIN, 0 },
                                 { pipes[1][0], POLLIN, 0 } };
        double remaining_ms = (deadline - now) * 1000;
        int timeout = remaining_ms < CAPTURE_POLL_MS ? (int)remaining_ms
                                                     : CAPTURE_POLL_MS;
        if (poll(fds, 2, timeout) < 0 && errno != EINTR) {
            result.error = CHIBI_PROCESS_HOST_FAILED;
            result.system_error = errno;
            break;
        }
    }
    /* Kill even after a successful leader exit: descendants belong to the step,
     * and may otherwise retain pipes or outlive cancellation indefinitely. */
    int kill_error = kill(-pid, SIGKILL) ? errno : 0;
#ifdef __APPLE__
    /* Darwin killpg skips zombies and may return EPERM for a zombie-only group.
     * WNOWAIT deliberately keeps our completed leader a zombie. */
    if (leader_finished && kill_error == EPERM) {
        kill_error = 0;
    }
#else
    (void)leader_finished;
#endif
    if (kill_error && kill_error != ESRCH) {
        result.error = CHIBI_PROCESS_HOST_FAILED;
        result.system_error = kill_error;
    }
    /* WNOWAIT above leaves the leader unreaped until its group is killed,
     * preventing its pid from being reused as an unrelated process group. */
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            if (result.error == CHIBI_PROCESS_OK) {
                result.error = CHIBI_PROCESS_HOST_FAILED;
                result.system_error = errno;
            }
            break;
        }
    }
    if ((!capture_read(&pipes[0][0], out) || !capture_read(&pipes[1][0], err)) &&
        result.error == CHIBI_PROCESS_OK) {
        result.error = CHIBI_PROCESS_HOST_FAILED;
        result.system_error = errno;
    }
    if (result.error == CHIBI_PROCESS_OK) {
        if (WIFEXITED(status)) {
            result.status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.status = -WTERMSIG(status);
        } else {
            result.error = CHIBI_PROCESS_HOST_FAILED;
        }
    }
close_pipes:
    for (int i = 0; i < 2; i++) {
        capture_close(&pipes[i][0]);
        capture_close(&pipes[i][1]);
    }
    return result;
}

bool chibi_process_start(chibi_process *process, const char *const *argv, const char *cwd, const char *logpath)
{
    assert(process && argv && argv[0] && cwd && logpath);
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        errno = error;
        return false;
    }
    error = posix_spawnattr_init(&attributes);
    if (error) {
        posix_spawn_file_actions_destroy(&actions);
        errno = error;
        return false;
    }
    error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    }
    if (!error) {
        error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    }
    if (!error) {
#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) &&           \
    __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
        error = posix_spawn_file_actions_addchdir(&actions, cwd);
#else
        error = posix_spawn_file_actions_addchdir_np(&actions, cwd);
#endif
    }
    if (!error) {
        error = posix_spawnattr_setpgroup(&attributes, 0);
    }
    sigset_t signals;
    sigemptyset(&signals);
    if (!error) {
        error = posix_spawnattr_setsigmask(&attributes, &signals);
    }
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGPIPE);
    if (!error) {
        error = posix_spawnattr_setsigdefault(&attributes, &signals);
    }
    if (!error) {
        error = posix_spawnattr_setflags(
            &attributes,
            POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF
        );
    }
    pid_t pid = 0;
    if (!error) {
        error = posix_spawnp(&pid, argv[0], &actions, &attributes, (char *const *)argv, environ);
    }
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error) {
        errno = error;
        return false;
    }
    *process = (chibi_process){ .pid = pid };
    return true;
}

int chibi_process_run(char *const *argv)
{
    pid_t pid;
    int error = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
    if (error) {
        fprintf(stderr, "Cannot run %s: %s\n", argv[0], strerror(error));
        return 1;
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return 1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

bool chibi_process_poll(chibi_process *process)
{
    assert(process->pid > 0);
    if (process->finished) {
        return true;
    }
    if (process->stop_started_ms && milliseconds() - process->stop_started_ms >= 2000) {
        kill(-process->pid, SIGKILL);
    }
    int status;
    pid_t result = waitpid(process->pid, &status, WNOHANG);
    if (result <= 0) {
        assert(result == 0 || errno == EINTR);
        return false;
    }
    process->status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    process->finished = true;
    if (process->stop_started_ms) {
        /* A cooperative leader can exit while a descendant ignores SIGTERM. */
        kill(-process->pid, SIGKILL);
    }
    return true;
}

void chibi_process_stop(chibi_process *process)
{
    if (chibi_process_poll(process) || process->stop_started_ms) {
        return;
    }
    process->stop_started_ms = milliseconds();
    kill(-process->pid, SIGTERM);
}
#endif
