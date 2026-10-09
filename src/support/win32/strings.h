/* support/win32/strings.h -- POSIX case-insensitive comparison, from the
 * Microsoft C runtime. See posix.h. */
#ifndef CHIBI_SUPPORT_WIN32_STRINGS_H
#define CHIBI_SUPPORT_WIN32_STRINGS_H

#include <string.h>

#ifndef strcasecmp
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#endif
