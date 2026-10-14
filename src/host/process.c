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

static wchar_t *wide(const char *text)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count) {
        errno = EILSEQ;
        return NULL;
    }
    wchar_t *result = xmalloc((size_t)count * sizeof *result);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result, count)) {
        free(result);
        errno = EILSEQ;
        return NULL;
    }
    return result;
}
int chibi_process_run(char *const *argv)
{
    char *utf8 = command_line(argv);
    wchar_t *line = wide(utf8);
    free(utf8);
    if (!line) {
        return 1;
    }
    STARTUPINFOW startup = { .cb = sizeof startup };
    PROCESS_INFORMATION info;
    bool started = CreateProcessW(NULL, line, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &info);
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
static bool start(chibi_process *process, const char *const *argv, const char *cwd, const char *logpath)
{
    assert(process && argv && argv[0] && cwd);
    *process = (chibi_process){ 0 };
    char *utf8 = command_line((char *const *)argv);
    wchar_t *line = wide(utf8), *directory = wide(cwd),
            *path = logpath ? wide(logpath) : NULL;
    free(utf8);
    SECURITY_ATTRIBUTES security = { .nLength = sizeof security, .bInheritHandle = TRUE };
    HANDLE log = INVALID_HANDLE_VALUE, input = INVALID_HANDLE_VALUE,
           reader = INVALID_HANDLE_VALUE, job = NULL;
    STARTUPINFOEXW startup = { .StartupInfo.cb = sizeof startup };
    PROCESS_INFORMATION child = { 0 };
    bool attributes_ready = false;
    bool ok = line && directory && (!logpath || path) && wcslen(line) < 32767;
    if (ok && !logpath) {
        ok = CreatePipe(&reader, &log, &security, 65536) &&
             SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
    } else if (ok) {
        log = CreateFileW(
            path,
            GENERIC_WRITE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ,
            &security,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
            NULL
        );
        BY_HANDLE_FILE_INFORMATION info;
        ok = log != INVALID_HANDLE_VALUE && GetFileInformationByHandle(log, &info) &&
             !(info.dwFileAttributes &
               (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
             SetEndOfFile(log);
    }
    if (ok) {
        input = CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        job = CreateJobObjectW(NULL, NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = { 0 };
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ok = input != INVALID_HANDLE_VALUE && job &&
             SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    }
    if (ok) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
        startup.lpAttributeList = xmalloc(bytes);
        ok = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes);
        attributes_ready = ok;
        if (ok) {
            HANDLE inherited[] = { log, input };
            ok = UpdateProcThreadAttribute(
                startup.lpAttributeList,
                0,
                PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                inherited,
                sizeof inherited,
                NULL,
                NULL
            );
        }
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log;
        startup.StartupInfo.hStdInput = input;
        if (ok) {
            ok = CreateProcessW(
                NULL,
                line,
                NULL,
                NULL,
                TRUE,
                CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                NULL,
                directory,
                &startup.StartupInfo,
                &child
            );
        }
        if (ok) {
            ok = AssignProcessToJobObject(job, child.hProcess);
        }
        if (ok) {
            ok = ResumeThread(child.hThread) != (DWORD)-1;
        }
    }
    if (startup.lpAttributeList) {
        if (attributes_ready) {
            DeleteProcThreadAttributeList(startup.lpAttributeList);
        }
        free(startup.lpAttributeList);
    }
    if (log != INVALID_HANDLE_VALUE) {
        CloseHandle(log);
    }
    if (input != INVALID_HANDLE_VALUE) {
        CloseHandle(input);
    }
    if (child.hThread) {
        CloseHandle(child.hThread);
    }
    if (ok) {
        process->process = (uintptr_t)child.hProcess;
        process->job = (uintptr_t)job;
        if (reader != INVALID_HANDLE_VALUE) {
            process->output = (uintptr_t)reader;
            process->capturing = true;
        }
    } else {
        if (reader != INVALID_HANDLE_VALUE) {
            CloseHandle(reader);
        }
        if (child.hProcess) {
            TerminateProcess(child.hProcess, 1);
            CloseHandle(child.hProcess);
        }
        if (job) {
            CloseHandle(job);
        }
        errno = EIO;
    }
    free(line);
    free(directory);
    free(path);
    return ok;
}
bool chibi_process_start(chibi_process *process, const char *const *argv, const char *cwd, const char *logpath)
{
    assert(logpath);
    return start(process, argv, cwd, logpath);
}
bool chibi_process_start_capture(chibi_process *process, const char *const *argv, const char *cwd)
{
    return start(process, argv, cwd, NULL);
}
int64_t chibi_process_read(chibi_process *process, void *out, size_t size)
{
    if (!process->capturing || !size) {
        return 0;
    }
    DWORD available = 0;
    if (!PeekNamedPipe((HANDLE)process->output, NULL, 0, NULL, &available, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
            return 0;
        }
        errno = EIO;
        return -1;
    }
    if (!available) {
        errno = EAGAIN;
        return -1;
    }
    DWORD count = 0;
    DWORD take = size < available ? (DWORD)size : available;
    if (!ReadFile((HANDLE)process->output, out, take, &count, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
            return 0;
        }
        errno = EIO;
        return -1;
    }
    return count;
}
void chibi_process_close_output(chibi_process *process)
{
    if (process->capturing) {
        CloseHandle((HANDLE)process->output);
    }
    process->capturing = false;
    process->output = 0;
}
bool chibi_process_poll(chibi_process *process)
{
    assert(process);
    if (process->finished) {
        return true;
    }
    if (!process->process) {
        process->status = 1;
        process->finished = true;
        return true;
    }
    if (WaitForSingleObject((HANDLE)process->process, 0) != WAIT_OBJECT_0) {
        return false;
    }
    DWORD status = 1;
    GetExitCodeProcess((HANDLE)process->process, &status);
    process->status = (int)status;
    process->finished = true;
    CloseHandle((HANDLE)process->process);
    CloseHandle((HANDLE)process->job);
    process->process = process->job = 0;
    return true;
}
void chibi_process_stop(chibi_process *process)
{
    assert(process);
    if (!process->finished && process->job) {
        process->stop_started_ms = GetTickCount64();
        TerminateJobObject((HANDLE)process->job, 130);
    }
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

static bool start(chibi_process *process, const char *const *argv, const char *cwd, const char *logpath)
{
    assert(process && argv && argv[0] && cwd);
    int pipes[2] = { -1, -1 };
    if (!logpath) {
        if (pipe(pipes)) {
            return false;
        }
        for (int i = 0; i < 2; i++) {
            int moved = fcntl(pipes[i], F_DUPFD_CLOEXEC, 3);
            int error = errno;
            close(pipes[i]);
            pipes[i] = moved;
            if (moved < 0) {
                if (pipes[1 - i] >= 0) {
                    close(pipes[1 - i]);
                }
                errno = error;
                return false;
            }
        }
        int flags = fcntl(pipes[0], F_GETFL);
        if (flags < 0 || fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK)) {
            int error = errno;
            close(pipes[0]);
            close(pipes[1]);
            errno = error;
            return false;
        }
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        if (pipes[0] >= 0) {
            close(pipes[0]);
            close(pipes[1]);
        }
        errno = error;
        return false;
    }
    error = posix_spawnattr_init(&attributes);
    if (error) {
        posix_spawn_file_actions_destroy(&actions);
        if (pipes[0] >= 0) {
            close(pipes[0]);
            close(pipes[1]);
        }
        errno = error;
        return false;
    }
    error = logpath
              ? posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logpath, O_WRONLY | O_CREAT | O_TRUNC, 0600)
              : posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    if (!error && !logpath) {
        error = posix_spawn_file_actions_addclose(&actions, pipes[0]);
    }
    if (!error && !logpath) {
        error = posix_spawn_file_actions_addclose(&actions, pipes[1]);
    }
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
    if (pipes[1] >= 0) {
        close(pipes[1]);
    }
    if (error) {
        if (pipes[0] >= 0) {
            close(pipes[0]);
        }
        errno = error;
        return false;
    }
    *process = (chibi_process){ .pid = pid, .output = pipes[0], .capturing = !logpath };
    return true;
}

bool chibi_process_start(chibi_process *process, const char *const *argv, const char *cwd, const char *logpath)
{
    assert(logpath);
    return start(process, argv, cwd, logpath);
}
bool chibi_process_start_capture(chibi_process *process, const char *const *argv, const char *cwd)
{
    return start(process, argv, cwd, NULL);
}
int64_t chibi_process_read(chibi_process *process, void *out, size_t size)
{
    if (!process->capturing || !size) {
        return 0;
    }
    return read(process->output, out, size);
}
void chibi_process_close_output(chibi_process *process)
{
    if (process->capturing) {
        close(process->output);
    }
    process->capturing = false;
    process->output = -1;
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
