#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef __linux__
#include "host/watch_internal.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    int descriptor;
    char *path;
} directory_watch;

typedef struct
{
    int events;
    int wake[2];
    pthread_t thread;
    directory_watch *directories;
    int count;
} linux_watch;

static char *join(const char *path, const char *name)
{
    size_t size = strlen(path) + strlen(name) + 2;
    char *result = xmalloc(size);
    snprintf(result, size, "%s/%s", path, name);
    return result;
}

static bool add_tree(linux_watch *backend, const char *path, bool recursive)
{
    int descriptor = inotify_add_watch(
        backend->events,
        path,
        IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB | IN_CREATE | IN_DELETE |
            IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR
    );
    if (descriptor < 0) {
        return errno == ENOENT;
    }
    bool known = false;
    for (int i = 0; i < backend->count; i++) {
        if (backend->directories[i].descriptor == descriptor) {
            known = true;
            break;
        }
    }
    if (!known) {
        backend->directories = xrealloc(
            backend->directories,
            (size_t)(backend->count + 1) * sizeof *backend->directories
        );
        backend->directories[backend->count++] = (directory_watch){ descriptor,
                                                                    xstrdup(path) };
    }
    if (!recursive) {
        return true;
    }
    DIR *directory = opendir(path);
    if (!directory) {
        return errno == ENOENT;
    }
    bool ok = true;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            bp_watch_excluded(entry->d_name)) {
            continue;
        }
        char *child = join(path, entry->d_name);
        struct stat info;
        if (lstat(child, &info) == 0 && S_ISDIR(info.st_mode)) {
            ok &= add_tree(backend, child, true);
        }
        free(child);
    }
    closedir(directory);
    return ok;
}

static bool reconcile(bp_watch *watch)
{
    linux_watch *backend = watch->backend;
    /* Paths of moved watches may now refer to a different subtree. Remove all
     * registrations and rebuild after topology changes or queue overflow. */
    for (int i = 0; i < backend->count; i++) {
        inotify_rm_watch(backend->events, backend->directories[i].descriptor);
        free(backend->directories[i].path);
    }
    backend->count = 0;
    bool ok = true;
    for (int i = 0; i < watch->count; i++) {
        char *path = xstrdup(watch->roots[i].path);
        char *slash = strrchr(path, '/');
        slash[slash == path ? 1 : 0] = 0;
        ok &= add_tree(backend, path, false);
        if (watch->roots[i].directory) {
            ok &= add_tree(backend, watch->roots[i].path, true);
        }
        free(path);
    }
    return ok;
}

static void *run(void *context)
{
    bp_watch *watch = context;
    linux_watch *backend = watch->backend;
    struct pollfd descriptors[] = { { backend->events, POLLIN, 0 },
                                    { backend->wake[0], POLLIN, 0 } };
    for (;;) {
        if (poll(descriptors, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (descriptors[1].revents) {
            break;
        }
        union
        {
            struct inotify_event aligned;
            char bytes[65536];
        } buffer;
        ssize_t size = read(backend->events, buffer.bytes, sizeof buffer.bytes);
        bool topology = false, changed = false;
        for (ssize_t offset = 0; offset < size;) {
            struct inotify_event *event = (struct inotify_event *)(buffer.bytes + offset);
            offset += sizeof *event + event->len;
            if (event->mask & IN_Q_OVERFLOW) {
                topology = changed = true;
                continue;
            }
            if (event->mask & IN_IGNORED) {
                continue;
            }
            for (int i = 0; i < backend->count; i++) {
                if (backend->directories[i].descriptor != event->wd) {
                    continue;
                }
                char *path = event->len
                               ? join(backend->directories[i].path, event->name)
                               : xstrdup(backend->directories[i].path);
                bool directory = !event->len || (event->mask & IN_ISDIR);
                bool tree_changed = event->mask &
                                    (IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                                     IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF);
                bool contents = tree_changed ||
                                (event->mask & (IN_MODIFY | IN_CLOSE_WRITE));
                bool matches = (directory ? tree_changed : contents) &&
                               bp_watch_matches(watch, path, directory);
                changed |= matches;
                topology |= matches && directory && tree_changed;
                free(path);
                break;
            }
        }
        if (topology && !reconcile(watch)) {
            fprintf(
                stderr,
                "File watcher could not refresh all source directories: %s. "
                "Some changes may not reload; fix directory permissions or the "
                "inotify watch limit, then restart development.\n",
                strerror(errno)
            );
        }
        if (changed) {
            atomic_fetch_add_explicit(&watch->revision, 1, memory_order_relaxed);
        }
    }
    return NULL;
}

bool bp_watch_start(bp_watch *watch)
{
    linux_watch *backend = xcalloc(1, sizeof *backend);
    backend->events = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (backend->events < 0) {
        free(backend);
        return false;
    }
    if (pipe2(backend->wake, O_CLOEXEC) != 0) {
        close(backend->events);
        free(backend);
        return false;
    }
    watch->backend = backend;
    int error = 0;
    if (!reconcile(watch) ||
        (error = pthread_create(&backend->thread, NULL, run, watch))) {
        if (error) {
            errno = error;
        }
        int saved = errno;
        for (int i = 0; i < backend->count; i++) {
            free(backend->directories[i].path);
        }
        free(backend->directories);
        close(backend->events);
        close(backend->wake[0]);
        close(backend->wake[1]);
        free(backend);
        watch->backend = NULL;
        errno = saved;
        return false;
    }
    return true;
}

void bp_watch_stop(bp_watch *watch)
{
    linux_watch *backend = watch->backend;
    char byte = 1;
    while (write(backend->wake[1], &byte, 1) < 0 && errno == EINTR) {
    }
    pthread_join(backend->thread, NULL);
    for (int i = 0; i < backend->count; i++) {
        free(backend->directories[i].path);
    }
    free(backend->directories);
    close(backend->events);
    close(backend->wake[0]);
    close(backend->wake[1]);
    free(backend);
}
#endif
