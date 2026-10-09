/* support/win32/dirent.h -- POSIX directory listing over FindFirstFile.
 * Entries carry only their name; callers stat for the type. See posix.h. */
#ifndef CHIBI_SUPPORT_WIN32_DIRENT_H
#define CHIBI_SUPPORT_WIN32_DIRENT_H

#include "posix.h"

struct dirent
{
    char d_name[PATH_MAX];
};

typedef struct posix_win32_dir DIR;

DIR *posix_win32_opendir(const char *path);
struct dirent *posix_win32_readdir(DIR *dir);
int posix_win32_closedir(DIR *dir);

#define opendir(path) posix_win32_opendir(path)
#define readdir(dir) posix_win32_readdir(dir)
#define closedir(dir) posix_win32_closedir(dir)

#endif
