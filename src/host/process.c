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

int chibi_process_run(char *const *argv, volatile sig_atomic_t *stop_signal)
{
    if (stop_signal && *stop_signal) {
        return 128 + *stop_signal;
    }
    HANDLE job = NULL;
    if (stop_signal) {
        job = CreateJobObjectA(NULL, NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = { 0 };
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job ||
            !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
            DWORD error = GetLastError();
            if (job) {
                CloseHandle(job);
            }
            fprintf(stderr, "Cannot own process job for %s: error %lu\n", argv[0], error);
            return 1;
        }
    }
    char *line = command_line(argv);
    STARTUPINFOA startup = { .cb = sizeof startup };
    PROCESS_INFORMATION info;
    DWORD flags = stop_signal ? CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP : 0;
    bool started = CreateProcessA(NULL, line, NULL, NULL, TRUE, flags, NULL, NULL, &startup, &info);
    free(line);
    if (!started) {
        DWORD error = GetLastError();
        if (job) {
            CloseHandle(job);
        }
        fprintf(stderr, "Cannot run %s: error %lu\n", argv[0], error);
        return 1;
    }
    if (job && (!AssignProcessToJobObject(job, info.hProcess) ||
                ResumeThread(info.hThread) == (DWORD)-1)) {
        DWORD error = GetLastError();
        TerminateProcess(info.hProcess, 1);
        WaitForSingleObject(info.hProcess, INFINITE);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        CloseHandle(job);
        fprintf(stderr, "Cannot own process %s: error %lu\n", argv[0], error);
        return 1;
    }
    CloseHandle(info.hThread);
    int interrupted = 0;
    DWORD waited;
    for (;;) {
        interrupted = stop_signal ? *stop_signal : 0;
        if (interrupted) {
            if (!TerminateJobObject(job, (UINT)(128 + interrupted))) {
                fprintf(stderr, "Cannot terminate process job for %s: error %lu\n", argv[0], GetLastError());
                /* Closing a kill-on-close job is also a termination request. */
                CloseHandle(job);
                job = NULL;
            }
            waited = WaitForSingleObject(info.hProcess, INFINITE);
            break;
        }
        waited = WaitForSingleObject(info.hProcess, stop_signal ? 10 : INFINITE);
        if (waited != WAIT_TIMEOUT) {
            break;
        }
    }
    DWORD status = 1;
    if (waited == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(info.hProcess, &status)) {
            fprintf(stderr, "Cannot read process status for %s: error %lu\n", argv[0], GetLastError());
        }
    } else {
        fprintf(stderr, "Cannot wait for %s: error %lu\n", argv[0], GetLastError());
    }
    if (job) {
        /* Closing the owned job also ends descendants left by an exited leader. */
        CloseHandle(job);
    }
    CloseHandle(info.hProcess);
    return interrupted ? 128 + interrupted : (int)status;
}
#else

#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
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

chibi_process_capture_result chibi_process_begin(
    chibi_process_job *job,
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
    *job = (chibi_process_job){ .pipes = { -1, -1 }, .out = out, .err = err };
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
    job->pid = pid;
    job->pipes[0] = pipes[0][0];
    job->pipes[1] = pipes[1][0];
    job->result.error = CHIBI_PROCESS_OK;
    return job->result;
close_pipes:
    for (int i = 0; i < 2; i++) {
        capture_close(&pipes[i][0]);
        capture_close(&pipes[i][1]);
    }
    return result;
}

bool chibi_process_running(const chibi_process_job *job)
{
    return job->pid && !job->leader_finished && job->result.error == CHIBI_PROCESS_OK;
}
static void job_fail(chibi_process_job *job, chibi_process_error error, int system_error)
{
    job->result.error = error;
    job->result.system_error = system_error;
}

void chibi_process_service(chibi_process_job *jobs, int count, double deadline, const volatile sig_atomic_t *cancelled)
{
    double now = chibi_process_now();
    int clock_error = errno;
    chibi_process_error stop = CHIBI_PROCESS_OK;
    if (now < 0) {
        stop = CHIBI_PROCESS_HOST_FAILED;
    } else if (cancelled && *cancelled) {
        stop = CHIBI_PROCESS_CANCELLED;
    } else if (now >= deadline) {
        stop = CHIBI_PROCESS_TIMEOUT;
    }
    struct pollfd fds[2 * CHIBI_PROCESS_SERVICE_JOBS];
    int polled = 0;
    bool exited = false;
    assert(count <= CHIBI_PROCESS_SERVICE_JOBS);
    for (int i = 0; i < count; i++) {
        chibi_process_job *job = &jobs[i];
        if (!chibi_process_running(job)) {
            continue;
        }
        if (stop != CHIBI_PROCESS_OK) {
            job_fail(job, stop, stop == CHIBI_PROCESS_HOST_FAILED ? clock_error : 0);
            continue;
        }
        siginfo_t info = { 0 };
        int waited = waitid(P_PID, (id_t)job->pid, &info, WEXITED | WNOHANG | WNOWAIT);
        if (!waited && info.si_pid == job->pid) {
            job->leader_finished = true;
            exited = true;
            continue;
        }
        if (waited < 0 && errno != EINTR) {
            job_fail(job, CHIBI_PROCESS_HOST_FAILED, errno);
            continue;
        }
        if (!capture_read(&job->pipes[0], job->out) ||
            !capture_read(&job->pipes[1], job->err)) {
            job_fail(job, CHIBI_PROCESS_HOST_FAILED, errno);
            continue;
        }
        for (int p = 0; p < 2; p++) {
            fds[polled++] = (struct pollfd){ job->pipes[p], POLLIN, 0 };
        }
    }
    if (stop != CHIBI_PROCESS_OK || exited || !polled) {
        return;
    }
    double remaining_ms = (deadline - now) * 1000;
    int timeout = remaining_ms < CAPTURE_POLL_MS ? (int)remaining_ms : CAPTURE_POLL_MS;
    if (poll(fds, (nfds_t)polled, timeout) < 0 && errno != EINTR) {
        for (int i = 0; i < count; i++) {
            if (chibi_process_running(&jobs[i])) {
                job_fail(&jobs[i], CHIBI_PROCESS_HOST_FAILED, errno);
            }
        }
    }
}

chibi_process_capture_result chibi_process_end(chibi_process_job *job)
{
    assert(job->pid);
    chibi_process_capture_result result = job->result;
    pid_t pid = job->pid;
    int status = 0;
    /* Kill even after a successful leader exit: descendants belong to the step,
     * and may otherwise retain pipes or outlive cancellation indefinitely. */
    int kill_error = kill(-pid, SIGKILL) ? errno : 0;
#ifdef __APPLE__
    /* Darwin killpg skips zombies and may return EPERM for a zombie-only group.
     * WNOWAIT deliberately keeps our completed leader a zombie. */
    if (job->leader_finished && kill_error == EPERM) {
        kill_error = 0;
    }
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
    if ((!capture_read(&job->pipes[0], job->out) ||
         !capture_read(&job->pipes[1], job->err)) &&
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
    capture_close(&job->pipes[0]);
    capture_close(&job->pipes[1]);
    job->pid = 0;
    return result;
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
    chibi_process_job job;
    chibi_process_capture_result
        started = chibi_process_begin(&job, executable, argv, cwd, envp, deadline, cancelled, out, err);
    if (started.error != CHIBI_PROCESS_OK) {
        return started;
    }
    while (chibi_process_running(&job)) {
        chibi_process_service(&job, 1, deadline, cancelled);
    }
    return chibi_process_end(&job);
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

/* A background parent needs SIGTTOU blocked while restoring terminal control.
 * Only the calling thread's mask changes; application worker masks stay intact. */
static bool foreground(int terminal, pid_t group)
{
    sigset_t block, previous;
    sigemptyset(&block);
    sigaddset(&block, SIGTTOU);
    int error = pthread_sigmask(SIG_BLOCK, &block, &previous);
    if (error) {
        errno = error;
        return false;
    }
    bool changed = !tcsetpgrp(terminal, group);
    int saved = errno;
    error = pthread_sigmask(SIG_SETMASK, &previous, NULL);
    errno = changed && error ? error : saved;
    return changed && !error;
}

static int child_status(int status)
{
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int chibi_process_run(char *const *argv, volatile sig_atomic_t *stop_signal)
{
    if (stop_signal && *stop_signal) {
        return 128 + *stop_signal;
    }
    /* Standard input may be redirected while an interactive child opens its
     * controlling terminal itself. This descriptor never replaces child IO. */
    int terminal = -1;
    pid_t parent_group = 0;
    bool handoff = false;
    if (stop_signal) {
        terminal = open("/dev/tty", O_RDWR | O_CLOEXEC);
        if (terminal >= 0 && terminal < 3) {
            int descriptor = fcntl(terminal, F_DUPFD_CLOEXEC, 3);
            close(terminal);
            terminal = descriptor;
        }
        parent_group = getpgrp();
        handoff = terminal >= 0 && tcgetpgrp(terminal) == parent_group;
    }
    posix_spawnattr_t attributes;
    int error = 0;
    if (stop_signal) {
        error = posix_spawnattr_init(&attributes);
        if (error) {
            fprintf(stderr, "Cannot prepare %s: %s\n", argv[0], strerror(error));
            if (terminal >= 0) {
                close(terminal);
            }
            return 1;
        }
        error = posix_spawnattr_setpgroup(&attributes, 0);
        sigset_t signals;
        sigemptyset(&signals);
        if (!error) {
            error = posix_spawnattr_setsigmask(&attributes, &signals);
        }
        sigaddset(&signals, SIGINT);
        sigaddset(&signals, SIGTERM);
        sigaddset(&signals, SIGHUP);
        sigaddset(&signals, SIGPIPE);
        if (!error) {
            error = posix_spawnattr_setsigdefault(&attributes, &signals);
        }
        if (!error) {
            short flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK |
                          POSIX_SPAWN_SETSIGDEF;
#ifdef __APPLE__
            /* Darwin returns EINTR after a background tty read sends SIGTTIN.
             * Keep user code suspended until it owns the foreground terminal. */
            if (handoff) {
                flags |= POSIX_SPAWN_START_SUSPENDED;
            }
#endif
            error = posix_spawnattr_setflags(&attributes, flags);
        }
    }
    pid_t pid;
    if (!error) {
        error = posix_spawnp(&pid, argv[0], NULL, stop_signal ? &attributes : NULL, argv, environ);
    }
    if (stop_signal) {
        posix_spawnattr_destroy(&attributes);
    }
    if (error) {
        fprintf(stderr, "Cannot run %s: %s\n", argv[0], strerror(error));
        if (terminal >= 0) {
            close(terminal);
        }
        return 1;
    }
    int status = 0;
    if (!stop_signal) {
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {
                perror("waitpid");
                return 1;
            }
        }
        return child_status(status);
    }

    bool failed = false;
    bool owned = true;
    if (handoff) {
        if (!foreground(terminal, pid)) {
            perror("Cannot give child terminal control");
            failed = true;
        } else if (kill(-pid, SIGCONT) && errno != ESRCH) {
            perror("Cannot continue foreground child");
            failed = true;
        }
    }
    int interrupted = 0;
    uint64_t stop_started = 0;
    while (!failed) {
        if (!interrupted && *stop_signal) {
            interrupted = *stop_signal;
            stop_started = milliseconds();
            kill(-pid, interrupted);
        }
        if (interrupted && milliseconds() - stop_started >= 2000) {
            break;
        }
        siginfo_t info = { 0 };
        /* Keep the leader's PID reserved until its remaining group is killed. */
        int waited = waitid(P_PID, (id_t)pid, &info, WEXITED | WSTOPPED | WCONTINUED | WNOHANG | WNOWAIT);
        if (!waited && info.si_pid == pid) {
            if (info.si_code == CLD_EXITED || info.si_code == CLD_KILLED ||
                info.si_code == CLD_DUMPED) {
                break;
            }
            /* Consume only the non-exit notification. The exited leader stays
             * waitable until its remaining process group has been terminated. */
            siginfo_t change = { 0 };
            int event = info.si_code == CLD_CONTINUED ? WCONTINUED : WSTOPPED;
            waited = waitid(P_PID, (id_t)pid, &change, event | WNOHANG);
            if (waited < 0 && errno == EINTR) {
                continue;
            }
            if (waited < 0) {
                owned = errno != ECHILD;
                perror("waitid");
                failed = true;
                break;
            }
            if (change.si_pid == pid &&
                (change.si_code == CLD_STOPPED || change.si_code == CLD_TRAPPED)) {
                if (!interrupted && !*stop_signal) {
                    if (terminal >= 0 && tcgetpgrp(terminal) == pid &&
                        !foreground(terminal, parent_group)) {
                        perror("Cannot restore terminal control");
                        failed = true;
                        break;
                    }
                    /* SIGSTOP also works when this wrapper's process group is
                     * orphaned. A shell or controller resumes the wrapper; it
                     * then resumes its separately owned foreground child. */
                    if (*stop_signal) {
                        kill(-pid, SIGCONT);
                        continue;
                    }
                    if (kill(getpid(), SIGSTOP)) {
                        perror("Cannot suspend process wrapper");
                        failed = true;
                        break;
                    }
                    if (terminal >= 0 && tcgetpgrp(terminal) == parent_group &&
                        !foreground(terminal, pid)) {
                        perror("Cannot give child terminal control");
                        failed = true;
                        break;
                    }
                }
                kill(-pid, SIGCONT);
            }
            continue;
        }
        if (waited < 0 && errno != EINTR) {
            owned = errno != ECHILD;
            perror("waitid");
            failed = true;
            break;
        }
        poll(NULL, 0, 10);
    }
    /* Also terminate descendants left behind after a leader exits normally. */
    if (owned) {
        kill(-pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {
                perror("waitpid");
                failed = true;
                break;
            }
        }
    }
    if (terminal >= 0) {
        if (tcgetpgrp(terminal) == pid && !foreground(terminal, parent_group)) {
            perror("Cannot restore terminal control");
            failed = true;
        }
        close(terminal);
    }
    if (!failed && !*stop_signal && WIFSIGNALED(status)) {
        int signal = WTERMSIG(status);
        if (signal == SIGINT || signal == SIGTERM || signal == SIGHUP) {
            *stop_signal = signal;
        }
    }
    if (!interrupted && *stop_signal) {
        interrupted = *stop_signal;
    }
    return failed ? 1 : interrupted ? 128 + interrupted : child_status(status);
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
