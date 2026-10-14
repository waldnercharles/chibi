#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "host/authoring_platform_internal.h"
#include "support/xalloc.h"

#include <SDL3/SDL_timer.h>
#include <assert.h>
#include <errno.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
static unsigned sequence;
static pid_t retired[128];
static int children;
static void reap_retired()
{
    for (int i = 0; i < 128; i++) {
        if (retired[i]) {
            int status;
            pid_t result = waitpid(retired[i], &status, WNOHANG);
            if (result == retired[i] || (result < 0 && errno == ECHILD)) {
                retired[i] = 0;
                children--;
            }
        }
    }
}
static bool safe_relative(const char *path)
{
    if (!path || !path[0] || path[0] == '/' || strchr(path, '\\') ||
        strlen(path) >= 4096) {
        return false;
    }
    for (const char *p = path; *p;) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        if (!n || (n == 1 && p[0] == '.') || (n == 2 && !memcmp(p, "..", 2))) {
            return false;
        }
        if (!slash) {
            return true;
        }
        p = slash + 1;
    }
    return false;
}
static int parent_at(int root, const char *path, char leaf[256])
{
    if (!safe_relative(path)) {
        return -1;
    }
    char copy[4096];
    snprintf(copy, sizeof copy, "%s", path);
    int fd = dup(root);
    char *at = copy, *slash;
    while (fd >= 0 && (slash = strchr(at, '/'))) {
        *slash = 0;
        int next = openat(fd, at, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(fd);
        fd = next;
        at = slash + 1;
    }
    if (strlen(at) >= 256) {
        if (fd >= 0) {
            close(fd);
        }
        return -1;
    }
    snprintf(leaf, 256, "%s", at);
    return fd;
}
static int create_directory_at(int root, const char *name)
{
    if (mkdirat(root, name, 0700) && errno != EEXIST) {
        return -1;
    }
    return openat(root, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}
static bool job_folder(authoring_platform *p, platform_request *r)
{
    int root = open(p->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int chibi = root >= 0 ? create_directory_at(root, ".chibi") : -1;
    int jobs = chibi >= 0 ? create_directory_at(chibi, "platform-jobs") : -1;
    if (root >= 0) {
        close(root);
    }
    if (chibi >= 0) {
        close(chibi);
    }
    if (jobs < 0) {
        return false;
    }
    char name[96];
    bool made = false;
    for (int i = 0; i < 64; i++) {
        snprintf(name, sizeof name, "job-%ld-%u", (long)getpid(), ++sequence);
        if (!mkdirat(jobs, name, 0700)) {
            made = true;
            break;
        }
        if (errno != EEXIST) {
            break;
        }
    }
    close(jobs);
    if (!made) {
        return false;
    }
    int n = snprintf(
        r->value,
        sizeof r->value,
        "%s/.chibi/platform-jobs/%s/%s",
        p->root,
        name,
        !strcmp(r->operation, "compile") ? "game.chibip" : "export"
    );
    return n > 0 && (size_t)n < sizeof r->value;
}
static bool spawn_job(authoring_platform *p, platform_request *r, const char *const *args)
{
    reap_retired();
    if (children == 128) {
        errno = EAGAIN;
        return false;
    }
    int pipes[2];
    if (pipe(pipes)) {
        return false;
    }
    int flags = fcntl(pipes[0], F_GETFL);
    if (flags < 0 || fcntl(pipes[0], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(pipes[1], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        int saved = errno;
        close(pipes[0]);
        close(pipes[1]);
        errno = saved;
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attrs;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        close(pipes[0]);
        close(pipes[1]);
        errno = error;
        return false;
    }
    error = posix_spawnattr_init(&attrs);
    if (error) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipes[0]);
        close(pipes[1]);
        return false;
    }
#define STEP(CALL)                                                             \
    if (!error) error = (CALL)
    STEP(posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO));
    STEP(posix_spawn_file_actions_adddup2(&actions, pipes[1], STDERR_FILENO));
    STEP(posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0));
    STEP(posix_spawn_file_actions_addclose(&actions, pipes[0]));
    STEP(posix_spawn_file_actions_addclose(&actions, pipes[1]));
#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) &&           \
    __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
    STEP(posix_spawn_file_actions_addchdir(&actions, p->root));
#else
    STEP(posix_spawn_file_actions_addchdir_np(&actions, p->root));
#endif
    STEP(posix_spawnattr_setpgroup(&attrs, 0));
    sigset_t mask, defaults;
    sigemptyset(&mask);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGPIPE);
    sigaddset(&defaults, SIGINT);
    STEP(posix_spawnattr_setsigmask(&attrs, &mask));
    STEP(posix_spawnattr_setsigdefault(&attrs, &defaults));
    STEP(
        posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF)
    );
    STEP(posix_spawn(&r->pid, args[0], &actions, &attrs, (char *const *)args, environ));
#undef STEP
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attrs);
    close(pipes[1]);
    if (error) {
        close(pipes[0]);
        r->pid = 0;
        errno = error;
        return false;
    }
    children++;
    r->output = pipes[0];
    r->text = xcalloc(AUTHORING_PLATFORM_LOG_MAX + 1, 1);
    return true;
}
static void start_import(authoring_platform *p, platform_request *r)
{
    platform_request *selected = NULL;
    for (int i = 0; i < AUTHORING_PLATFORM_REQUESTS_MAX; i++) {
        platform_request *candidate = &p->requests[i];
        if (candidate->handle == r->source &&
            candidate->state == AUTHORING_PLATFORM_SUCCEEDED &&
            !strcmp(candidate->operation, "choose-file")) {
            selected = candidate;
            break;
        }
    }
    if (!selected || !safe_relative(r->value)) {
        authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "invalid", "Import requires a completed selected-file handle and workspace-relative destination", NULL);
        return;
    }
    int root = open(p->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    r->import_parent = root >= 0 ? parent_at(root, r->value, r->import_name) : -1;
    if (root >= 0) {
        close(root);
    }
    r->import_source = open(
        selected->text ? selected->text : selected->value,
        O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC
    );
    struct stat info;
    if (r->import_parent < 0 || r->import_source < 0 ||
        fstat(r->import_source, &info) || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || info.st_size > AUTHORING_PLATFORM_FILE_MAX) {
        authoring_platform_complete(
            r,
            AUTHORING_PLATFORM_FAILED,
            "unsafe-file",
            "Selected source/destination must be bounded regular files without "
            "final symlink traversal",
            NULL
        );
        return;
    }
    r->import_size = (size_t)info.st_size;
    r->import_mtime = info.st_mtime;
    r->import_ctime = info.st_ctime;
#if defined(__APPLE__)
    r->import_mtime_ns = info.st_mtimespec.tv_nsec;
    r->import_ctime_ns = info.st_ctimespec.tv_nsec;
#else
    r->import_mtime_ns = info.st_mtim.tv_nsec;
    r->import_ctime_ns = info.st_ctim.tv_nsec;
#endif
    snprintf(
        r->import_temporary,
        sizeof r->import_temporary,
        ".platform-import-%ld-%u.tmp",
        (long)getpid(),
        ++sequence
    );
    r->import_output = openat(
        r->import_parent,
        r->import_temporary,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
        0600
    );
    if (r->import_output < 0) {
        authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "io", "Cannot create exclusive import temporary", NULL);
    }
}
void authoring_platform_start_job(authoring_platform *p, platform_request *r)
{
    if (!strcmp(r->operation, "migrate-project")) {
        if (!p->compiler[0] || p->compiler[0] != '/') {
            authoring_platform_complete(
                r,
                AUTHORING_PLATFORM_UNAVAILABLE,
                "unavailable",
                "Configured compiler is unavailable",
                NULL
            );
            return;
        }
        const char *args[] = { p->compiler,      "--project-root", r->input,
                               "--migrate-copy", r->value,         NULL };
        if (!spawn_job(p, r, args)) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "spawn", strerror(errno), NULL);
        }
        return;
    }
    if (!strcmp(r->operation, "import-file")) {
        start_import(p, r);
        return;
    }
    const char *tool = !strcmp(r->operation, "compile") ? p->compiler : p->launcher;
    if (!tool[0] || tool[0] != '/') {
        authoring_platform_complete(
            r,
            AUTHORING_PLATFORM_UNAVAILABLE,
            "unavailable",
            "Host has no explicit compiler/export executable",
            NULL
        );
        return;
    }
    if (!strcmp(r->operation, "compile") && r->input[0]) {
        int root = open(p->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        char leaf[256];
        int parent = root >= 0 ? parent_at(root, r->input, leaf) : -1;
        if (root >= 0) {
            close(root);
        }
        int file = parent >= 0
                     ? openat(parent, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
                     : -1;
        if (parent >= 0) {
            close(parent);
        }
        struct stat info;
        bool valid = file >= 0 && !fstat(file, &info) && S_ISREG(info.st_mode) &&
                     info.st_size >= 0 && info.st_size <= 1024 * 1024;
        if (file >= 0) {
            close(file);
        }
        if (!valid) {
            authoring_platform_complete(
                r,
                AUTHORING_PLATFORM_FAILED,
                "unsafe-scene",
                "Compile scene must be a bounded workspace-relative regular "
                "file without symlink traversal",
                NULL
            );
            return;
        }
    }
    char scene[8194] = "";
    if (!strcmp(r->operation, "compile") && r->input[0]) {
        int length = snprintf(scene, sizeof scene, "%s/%s", p->root, r->input);
        if (length < 0 || (size_t)length >= sizeof scene) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "limit", "Compile scene host path exceeds limit", NULL);
            return;
        }
    }
    if (!job_folder(p, r)) {
        authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "io", "Cannot create exclusive job output folder", NULL);
        return;
    }
    const char *compile[] = { tool,
                              "--project-root",
                              p->root,
                              "--output",
                              r->value,
                              r->input[0] ? "--scene" : NULL,
                              r->input[0] ? scene : NULL,
                              NULL };
    const char *export[] = { tool, "export", p->root, "--output", r->value, NULL };
    if (!spawn_job(p, r, !strcmp(r->operation, "compile") ? compile : export)) {
        authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "spawn", strerror(errno), NULL);
    }
}
static void poll_import(platform_request *r)
{
    if (r->import_output < 0 || r->state != AUTHORING_PLATFORM_RUNNING) {
        return;
    }
    unsigned char buffer[65536];
    size_t remaining = r->import_size - r->import_done;
    size_t take = remaining < sizeof buffer ? remaining : sizeof buffer;
    if (take) {
        ssize_t n = read(r->import_source, buffer, take);
        if (n < 0 && errno == EINTR) {
            return;
        }
        if (n <= 0) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "io", "Source changed or import read failed", NULL);
            return;
        }
        size_t at = 0;
        while (at < (size_t)n) {
            ssize_t written = write(r->import_output, buffer + at, (size_t)n - at);
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written <= 0) {
                authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "io", "Import write failed", NULL);
                return;
            }
            at += (size_t)written;
        }
        r->import_done += (size_t)n;
        r->progress = r->import_size ? (double)r->import_done / (double)r->import_size
                                     : 0;
        return;
    }
    unsigned char extra;
    struct stat current;
    bool unchanged = !fstat(r->import_source, &current) &&
                     current.st_size == (off_t)r->import_size &&
                     current.st_mtime == r->import_mtime &&
                     current.st_ctime == r->import_ctime;
#if defined(__APPLE__)
    unchanged = unchanged && current.st_mtimespec.tv_nsec == r->import_mtime_ns &&
                current.st_ctimespec.tv_nsec == r->import_ctime_ns;
#else
    unchanged = unchanged && current.st_mtim.tv_nsec == r->import_mtime_ns &&
                current.st_ctim.tv_nsec == r->import_ctime_ns;
#endif
    if (!unchanged || read(r->import_source, &extra, 1) != 0 ||
        fsync(r->import_output) ||
        linkat(r->import_parent, r->import_temporary, r->import_parent, r->import_name, 0)) {
        authoring_platform_complete(
            r,
            AUTHORING_PLATFORM_FAILED,
            errno == EEXIST ? "exists" : "io",
            "Import destination exists or source/publication failed",
            NULL
        );
        return;
    }
    authoring_platform_complete(r, AUTHORING_PLATFORM_SUCCEEDED, "ok", "Selected bytes copied exclusively; no decoding or conversion performed", NULL);
}
void authoring_platform_poll_job(platform_request *r)
{
    reap_retired();
    if (!r->handle) {
        return;
    }
    poll_import(r);
    if (r->output >= 0) {
        char bytes[4096];
        for (int i = 0; i < 8; i++) {
            ssize_t n = read(r->output, bytes, sizeof bytes);
            if (n == 0) {
                r->output_eof = true;
                close(r->output);
                r->output = -1;
                break;
            }
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    authoring_platform_complete(
                        r,
                        AUTHORING_PLATFORM_FAILED,
                        "io",
                        "Authoring job diagnostic pipe failed",
                        NULL
                    );
                    authoring_platform_cancel_job(r);
                }
                break;
            }
            size_t room = AUTHORING_PLATFORM_LOG_MAX - r->log_bytes,
                   keep = (size_t)n < room ? (size_t)n : room;
            memcpy(r->text + r->log_bytes, bytes, keep);
            r->log_bytes += keep;
            r->text[r->log_bytes] = 0;
            if ((size_t)n > room && !r->stop_started) {
                authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "log-limit", "Authoring job diagnostic output exceeded retained log budget", NULL);
                authoring_platform_cancel_job(r);
            }
        }
    }
    if (r->pid <= 0) {
        if (r->process_finished && r->output_eof &&
            r->state == AUTHORING_PLATFORM_RUNNING) {
            authoring_platform_complete(
                r,
                r->exit_code ? AUTHORING_PLATFORM_FAILED : AUTHORING_PLATFORM_SUCCEEDED,
                r->exit_code ? "process-error" : "ok",
                r->exit_code ? "Authoring job failed; inspect Log" : "Authoring job completed",
                NULL
            );
        }
        return;
    }
    if (r->stop_started && SDL_GetTicks() - r->stop_started >= 2000) {
        kill(-r->pid, SIGKILL);
    }
    int status;
    pid_t done = waitpid(r->pid, &status, WNOHANG);
    if (done == r->pid) {
        if (r->stop_started) {
            kill(-r->pid, SIGKILL);
        }
        children--;
        r->exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
                                         : 128 + WTERMSIG(status);
        r->pid = 0;
        r->process_finished = true;
        if (r->output_eof && r->state == AUTHORING_PLATFORM_RUNNING) {
            authoring_platform_complete(
                r,
                r->exit_code ? AUTHORING_PLATFORM_FAILED : AUTHORING_PLATFORM_SUCCEEDED,
                r->exit_code ? "process-error" : "ok",
                r->exit_code ? "Authoring job failed; inspect Log" : "Authoring job completed",
                NULL
            );
        }
    } else if (done < 0 && errno != EINTR) {
        children--;
        r->pid = 0;
        authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "wait", strerror(errno), NULL);
    }
}
void authoring_platform_cancel_job(platform_request *r)
{
    if (r->pid > 0 && !r->stop_started) {
        r->stop_started = SDL_GetTicks();
        if (!r->stop_started) {
            r->stop_started = 1;
        }
        kill(-r->pid, SIGTERM);
    }
}
void authoring_platform_free_job(platform_request *r)
{
    if (!r->handle) {
        return;
    }
    if (r->pid > 0) {
        kill(-r->pid, SIGKILL);
        int status;
        pid_t done = waitpid(r->pid, &status, WNOHANG);
        if (done == r->pid || (done < 0 && errno == ECHILD)) {
            children--;
        } else {
            int slot = -1;
            for (int i = 0; i < 128; i++) {
                if (!retired[i]) {
                    slot = i;
                    break;
                }
            }
            assert(slot >= 0);
            retired[slot] = r->pid;
        }
        r->pid = 0;
    }
    reap_retired();
    if (r->output >= 0) {
        close(r->output);
        r->output = -1;
    }
    if (r->import_source >= 0) {
        close(r->import_source);
        r->import_source = -1;
    }
    if (r->import_output >= 0) {
        close(r->import_output);
        r->import_output = -1;
    }
    if (r->import_parent >= 0) {
        if (r->import_temporary[0]) {
            unlinkat(r->import_parent, r->import_temporary, 0);
        }
        close(r->import_parent);
        r->import_parent = -1;
    }
}
#elif !defined(_WIN32)
void authoring_platform_start_job(authoring_platform *, platform_request *r)
{
    authoring_platform_complete(
        r,
        AUTHORING_PLATFORM_UNAVAILABLE,
        "unavailable",
        "Native compile/export/import backend unavailable on this host",
        NULL
    );
}
void authoring_platform_poll_job(platform_request *) {}
void authoring_platform_cancel_job(platform_request *) {}
void authoring_platform_free_job(platform_request *) {}
#endif
