#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "host/process.h"

#include <assert.h>
#include <errno.h>
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
        error = posix_spawn_file_actions_addchdir_np(&actions, cwd);
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
