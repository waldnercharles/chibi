#include "posix.h"

#include "dirent.h"
#include "support/xalloc.h"
#include "sys/file.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#define TEMPORARY_SUFFIX "XXXXXX"
#define TEMPORARY_ATTEMPTS 100
#define GETDELIM_MIN_SIZE 128
/* FILETIME counts 100 ns ticks from 1601. */
#define FILETIME_TICKS_PER_SECOND 10000000u
#define FILETIME_UNIX_EPOCH 116444736000000000u

struct posix_win32_dir
{
    HANDLE find;
    WIN32_FIND_DATAA found;
    bool pending; /* found holds an entry readdir has not returned */
    struct dirent entry;
};

static int errno_from_windows()
{
    switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: return ENOENT;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS: return EEXIST;
        case ERROR_DIRECTORY: return ENOTDIR;
        default: return EACCES;
    }
}

int posix_win32_lstat(const char *path, struct stat *info)
{
    if (stat(path, info) != 0) {
        return -1;
    }
    DWORD attributes = GetFileAttributesA(path);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        info->st_mode = (info->st_mode & ~S_IFMT) | S_IFLNK;
    }
    return 0;
}

char *posix_win32_realpath(const char *path, char *resolved)
{
    char *full = _fullpath(resolved, path, PATH_MAX);
    if (!full) {
        return NULL;
    }
    if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) {
        errno = errno_from_windows();
        if (!resolved) {
            free(full);
        }
        return NULL;
    }
    for (char *c = full; *c; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    /* Drop a trailing separator, except a drive root's. */
    size_t length = strlen(full);
    if (length > 3 && full[length - 1] == '/') {
        full[length - 1] = 0;
    }
    return full;
}

/* The pattern's trailing XXXXXX, or NULL with EINVAL when it has none. */
static char *temporary_suffix(char *pattern)
{
    size_t length = strlen(pattern), suffix = sizeof TEMPORARY_SUFFIX - 1;
    if (length < suffix || strcmp(pattern + length - suffix, TEMPORARY_SUFFIX)) {
        errno = EINVAL;
        return NULL;
    }
    return pattern + length - suffix;
}

static void randomize(char *suffix)
{
    static const char LETTERS[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static uint64_t seed;
    if (!seed) {
        seed = GetTickCount64() ^ (uint64_t)GetCurrentProcessId() << 32;
    }
    for (size_t i = 0; i < sizeof TEMPORARY_SUFFIX - 1; i++) {
        seed = seed * 6364136223846793005u + 1442695040888963407u;
        suffix[i] = LETTERS[(seed >> 33) % (sizeof LETTERS - 1)];
    }
}

char *posix_win32_mkdtemp(char *pattern)
{
    char *suffix = temporary_suffix(pattern);
    if (!suffix) {
        return NULL;
    }
    for (int attempt = 0; attempt < TEMPORARY_ATTEMPTS; attempt++) {
        randomize(suffix);
        if (_mkdir(pattern) == 0) {
            return pattern;
        }
        if (errno != EEXIST) {
            return NULL;
        }
    }
    return NULL;
}

int posix_win32_mkstemp(char *pattern)
{
    char *suffix = temporary_suffix(pattern);
    if (!suffix) {
        return -1;
    }
    for (int attempt = 0; attempt < TEMPORARY_ATTEMPTS; attempt++) {
        randomize(suffix);
        int fd = _open(pattern, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (fd >= 0 || errno != EEXIST) {
            return fd;
        }
    }
    return -1;
}

int posix_win32_rename(const char *from, const char *to)
{
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) {
        return 0;
    }
    /* POSIX replaces an empty folder; RemoveDirectory fails on a full one. */
    DWORD attributes = GetFileAttributesA(to);
    if (attributes != INVALID_FILE_ATTRIBUTES && attributes & FILE_ATTRIBUTE_DIRECTORY &&
        RemoveDirectoryA(to) && MoveFileExA(from, to, 0)) {
        return 0;
    }
    errno = errno_from_windows();
    return -1;
}

int posix_win32_setenv(const char *name, const char *value, int overwrite)
{
    if (!overwrite && getenv(name)) {
        return 0;
    }
    int error = _putenv_s(name, value);
    if (error) {
        errno = error;
        return -1;
    }
    return 0;
}

ssize_t posix_win32_getdelim(char **line, size_t *size, int delimiter, FILE *file)
{
    assert(*line || !*size);
    size_t length = 0;
    for (int c; (c = fgetc(file)) != EOF;) {
        if (length + 2 > *size) {
            *size = *size < GETDELIM_MIN_SIZE ? GETDELIM_MIN_SIZE : 2 * *size;
            *line = xrealloc(*line, *size);
        }
        (*line)[length++] = (char)c;
        if (c == delimiter) {
            break;
        }
    }
    if (!length || ferror(file)) {
        return -1;
    }
    (*line)[length] = 0;
    return (ssize_t)length;
}

int posix_win32_clock_gettime(int clock, struct timespec *time)
{
    if (clock == CLOCK_REALTIME) {
        FILETIME now;
        GetSystemTimePreciseAsFileTime(&now);
        uint64_t ticks = ((uint64_t)now.dwHighDateTime << 32 | now.dwLowDateTime) -
                         FILETIME_UNIX_EPOCH;
        time->tv_sec = (time_t)(ticks / FILETIME_TICKS_PER_SECOND);
        time->tv_nsec = (long)(ticks % FILETIME_TICKS_PER_SECOND * 100);
        return 0;
    }
    assert(clock == CLOCK_MONOTONIC);
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    time->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
    time->tv_nsec = (long)(counter.QuadPart % frequency.QuadPart * 1000000000 /
                           frequency.QuadPart);
    return 0;
}

char *posix_win32_stpcpy(char *to, const char *from)
{
    size_t length = strlen(from);
    memcpy(to, from, length + 1);
    return to + length;
}

char *posix_win32_stpncpy(char *to, const char *from, size_t size)
{
    size_t length = strnlen(from, size);
    memcpy(to, from, length);
    memset(to + length, 0, size - length);
    return to + length;
}

int posix_win32_flock(int fd, int operation)
{
    HANDLE file = (HANDLE)_get_osfhandle(fd);
    OVERLAPPED whole = { 0 };
    bool done = operation & LOCK_UN
                  ? UnlockFileEx(file, 0, MAXDWORD, MAXDWORD, &whole)
                  : LockFileEx(
                        file,
                        (operation & LOCK_EX ? LOCKFILE_EXCLUSIVE_LOCK : 0) |
                            (operation & LOCK_NB ? LOCKFILE_FAIL_IMMEDIATELY : 0),
                        0,
                        MAXDWORD,
                        MAXDWORD,
                        &whole
                    );
    if (done) {
        return 0;
    }
    errno = GetLastError() == ERROR_LOCK_VIOLATION ? EWOULDBLOCK : EACCES;
    return -1;
}

DIR *posix_win32_opendir(const char *path)
{
    char pattern[PATH_MAX];
    if (snprintf(pattern, sizeof pattern, "%s/*", path) >= (int)sizeof pattern) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    DIR *dir = xmalloc(sizeof *dir);
    dir->find = FindFirstFileA(pattern, &dir->found);
    if (dir->find == INVALID_HANDLE_VALUE) {
        errno = errno_from_windows();
        free(dir);
        return NULL;
    }
    dir->pending = true;
    return dir;
}

struct dirent *posix_win32_readdir(DIR *dir)
{
    if (!dir->pending && !FindNextFileA(dir->find, &dir->found)) {
        return NULL;
    }
    dir->pending = false;
    memcpy(dir->entry.d_name, dir->found.cFileName, sizeof dir->found.cFileName);
    return &dir->entry;
}

int posix_win32_closedir(DIR *dir)
{
    FindClose(dir->find);
    free(dir);
    return 0;
}
