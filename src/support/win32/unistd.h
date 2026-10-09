/* support/win32/unistd.h -- the unistd.h names the engine uses, from the
 * Microsoft C runtime's io.h, direct.h and process.h. See posix.h. */
#ifndef CHIBI_SUPPORT_WIN32_UNISTD_H
#define CHIBI_SUPPORT_WIN32_UNISTD_H

#include <direct.h>
#include <io.h>
#include <process.h>

#ifndef F_OK
#define F_OK 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#endif
