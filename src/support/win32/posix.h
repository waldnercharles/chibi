/* support/win32/posix.h -- the POSIX calls the engine uses, on Windows.
 *
 * Windows builds use clang with the Microsoft C runtime. The SDK force-includes
 * this header into every C source that links Chibi::support on Windows, and
 * puts unistd.h, dirent.h and strings.h from this directory on the include
 * path. Together they supply what POSIX declares and the runtime lacks or
 * spells differently. open uses binary mode, lstat reports symbolic links and
 * junctions as links, and rename replaces an existing file or empty folder,
 * as POSIX's do. */
#ifndef CHIBI_SUPPORT_WIN32_POSIX_H
#define CHIBI_SUPPORT_WIN32_POSIX_H
#ifdef _WIN32

/* Declare the runtime's POSIX names (mkdir, unlink, fdopen...) without
 * deprecation warnings. */
#ifndef _CRT_DECLARE_NONSTDC_NAMES
#define _CRT_DECLARE_NONSTDC_NAMES 1
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#endif

/* Declare the runtime's own spellings before the macros below rename them. */
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifdef _MSC_VER
typedef ptrdiff_t ssize_t;
typedef unsigned short mode_t;
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif
#ifndef S_IFLNK
#define S_IFLNK 0xa000
#endif
#ifndef S_ISLNK
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#endif
/* Creating with O_EXCL already refuses an existing link. */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#endif

/* The runtime's open in binary mode, keeping only the owner's read and write
 * bits, the only ones it accepts. */
int posix_win32_open(const char *path, int flags, ...);
/* stat, with symbolic links and junctions as S_IFLNK instead of their target. */
int posix_win32_lstat(const char *path, struct stat *info);
/* The absolute path with forward slashes, into `resolved` (PATH_MAX bytes)
 * or a new buffer for NULL; NULL when it does not exist. Links stay as named. */
char *posix_win32_realpath(const char *path, char *resolved);
char *posix_win32_mkdtemp(char *pattern);
int posix_win32_mkstemp(char *pattern);
int posix_win32_rename(const char *from, const char *to);
/* An empty value removes the variable, as the runtime's _putenv_s does. */
int posix_win32_setenv(const char *name, const char *value, int overwrite);
ssize_t posix_win32_getdelim(char **line, size_t *size, int delimiter, FILE *file);
int posix_win32_clock_gettime(int clock, struct timespec *time);
char *posix_win32_stpcpy(char *to, const char *from);
char *posix_win32_stpncpy(char *to, const char *from, size_t size);
char *posix_win32_strndup(const char *text, size_t size);

/* Object-like, so members named open are renamed alike where they are
 * declared and called. */
#define open posix_win32_open
#define mkdir(path, mode) _mkdir(path)
#define lstat(path, info) posix_win32_lstat(path, info)
#define fsync(fd) _commit(fd)
#define realpath(path, resolved) posix_win32_realpath(path, resolved)
#define mkdtemp(pattern) posix_win32_mkdtemp(pattern)
#define mkstemp(pattern) posix_win32_mkstemp(pattern)
#define rename(from, to) posix_win32_rename(from, to)
#define setenv(name, value, overwrite)                                         \
    posix_win32_setenv(name, value, overwrite)
#define getdelim(line, size, delimiter, file)                                  \
    posix_win32_getdelim(line, size, delimiter, file)
#define getline(line, size, file) posix_win32_getdelim(line, size, '\n', file)
#define clock_gettime(clock, time) posix_win32_clock_gettime(clock, time)
#define stpcpy(to, from) posix_win32_stpcpy(to, from)
#define stpncpy(to, from, size) posix_win32_stpncpy(to, from, size)
#define strndup(text, size) posix_win32_strndup(text, size)
/* Windows files have no POSIX modes to copy. */
#define fchmod(fd, mode) 0

#endif
#endif
