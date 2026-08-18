// ckit.h implementation TU.
//
// The vendored ckit.h uses the MSVC `_stricmp` name in map_ssort, which does not
// exist on POSIX systems; alias it to strcasecmp (unchanged vendored header).
#include <strings.h> /* strcasecmp */
#ifndef _MSC_VER
#define _stricmp strcasecmp
#endif

#define CKIT_IMPLEMENTATION
#include "ckit.h"
