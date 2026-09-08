#ifndef BLUEPRINTS_HOST_WATCH_MACOS_FLAGS_H
#define BLUEPRINTS_HOST_WATCH_MACOS_FLAGS_H

#include <CoreServices/CoreServices.h>
#include <stdbool.h>

static inline bool bp_watch_macos_content_event(FSEventStreamEventFlags flags)
{
    bool topology = flags & (kFSEventStreamEventFlagItemRemoved |
                             kFSEventStreamEventFlagItemRenamed);
    return topology || (!(flags & kFSEventStreamEventFlagItemIsDir) &&
                        (flags & (kFSEventStreamEventFlagItemCreated |
                                  kFSEventStreamEventFlagItemModified)));
}

#endif
