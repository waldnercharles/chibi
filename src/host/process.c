#ifndef _GNU_SOURCE
#define _GNU_SOURCE
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
