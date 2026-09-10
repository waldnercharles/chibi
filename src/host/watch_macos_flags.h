#ifndef CHIBI_HOST_WATCH_MACOS_FLAGS_H
#define CHIBI_HOST_WATCH_MACOS_FLAGS_H

#include <CoreServices/CoreServices.h>
#include <stdbool.h>

static inline bool chibi_watch_macos_content_event(FSEventStreamEventFlags flags)
{
    bool topology = flags & (kFSEventStreamEventFlagItemRemoved |
                             kFSEventStreamEventFlagItemRenamed);
    return topology || (!(flags & kFSEventStreamEventFlagItemIsDir) &&
                        (flags & (kFSEventStreamEventFlagItemCreated |
                                  kFSEventStreamEventFlagItemModified)));
}

#endif
