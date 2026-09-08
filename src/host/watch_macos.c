#ifdef __APPLE__
#include "host/watch_internal.h"
#include "host/watch_macos_flags.h"
#include "support/xalloc.h"

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    FSEventStreamRef stream;
    dispatch_queue_t queue;
} mac_watch;

static void changed(
    ConstFSEventStreamRef,
    void *context,
    size_t count,
    void *paths,
    const FSEventStreamEventFlags flags[],
    const FSEventStreamEventId[]
)
{
    bp_watch *watch = context;
    char **names = paths;
    for (size_t i = 0; i < count; i++) {
        bool lost = flags[i] & (kFSEventStreamEventFlagMustScanSubDirs |
                                kFSEventStreamEventFlagRootChanged |
                                kFSEventStreamEventFlagEventIdsWrapped);
        bool directory = flags[i] & kFSEventStreamEventFlagItemIsDir;
        /* FSEvents already watches new descendants. Their file events carry
         * source changes; directory creation flags may linger on metadata-only
         * notifications from configure tools. Moves/removals affect whole trees. */
        if (lost || (bp_watch_macos_content_event(flags[i]) &&
                     bp_watch_matches(watch, names[i], directory))) {
            atomic_fetch_add_explicit(&watch->revision, 1, memory_order_relaxed);
            return;
        }
    }
}

bool bp_watch_start(bp_watch *watch)
{
    mac_watch *backend = xcalloc(1, sizeof *backend);
    CFMutableArrayRef paths = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    for (int i = 0; i < watch->count; i++) {
        char *path = xstrdup(watch->roots[i].path);
        /* Observe the parent too: editors can replace the watched root itself. */
        char *slash = strrchr(path, '/');
        slash[slash == path ? 1 : 0] = 0;
        CFStringRef string = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
        free(path);
        if (!string) {
            CFRelease(paths);
            free(backend);
            errno = EINVAL;
            return false;
        }
        if (!CFArrayContainsValue(paths, CFRangeMake(0, CFArrayGetCount(paths)), string)) {
            CFArrayAppendValue(paths, string);
        }
        CFRelease(string);
    }
    FSEventStreamContext context = { .info = watch };
    backend->stream = FSEventStreamCreate(
        NULL,
        changed,
        &context,
        paths,
        kFSEventStreamEventIdSinceNow,
        0.05,
        kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot |
            kFSEventStreamCreateFlagNoDefer
    );
    CFRelease(paths);
    if (!backend->stream) {
        free(backend);
        errno = EIO;
        return false;
    }
    backend->queue = dispatch_queue_create("blueprints.file-events", DISPATCH_QUEUE_SERIAL);
    FSEventStreamSetDispatchQueue(backend->stream, backend->queue);
    if (!FSEventStreamStart(backend->stream)) {
        FSEventStreamInvalidate(backend->stream);
        FSEventStreamRelease(backend->stream);
        dispatch_release(backend->queue);
        free(backend);
        errno = EIO;
        return false;
    }
    watch->backend = backend;
    return true;
}

static void drained(void *) {}

void bp_watch_stop(bp_watch *watch)
{
    mac_watch *backend = watch->backend;
    FSEventStreamStop(backend->stream);
    FSEventStreamInvalidate(backend->stream);
    dispatch_sync_f(backend->queue, NULL, drained);
    FSEventStreamRelease(backend->stream);
    dispatch_release(backend->queue);
    free(backend);
}
#endif
