#ifndef CHIBI_HOST_APPLICATION_TOOLS_H
#define CHIBI_HOST_APPLICATION_TOOLS_H

#include "runtime/native_capabilities.h"
#include "support/arena.h"

#include <stdint.h>

#define APPLICATION_TOOLS_SERVICE "application.tools"

typedef struct application_io application_io;

/* Paths and host objects are borrowed for the application lifetime. Native
 * results borrow scratch until the next call; the execution boundary copies
 * them into the caller's owned storage before then. */
typedef struct
{
    const char *bin, *core, *share;
    application_io *io;
    arena scratch;
} application_tools;

const native_capability_pack *application_tools_natives();
void application_tools_free(application_tools *);
/* Shared filesystem leaf for application filesystem capabilities and project
 * freshness, ignoring generated .chibi and source-control .git directories. */
bool application_tools_changed_since(const char *directory, int64_t since);

#endif
