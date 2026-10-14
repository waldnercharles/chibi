#ifndef CHIBI_ROOTED_FILES_INTERNAL_H
#define CHIBI_ROOTED_FILES_INTERNAL_H
#include "support/rooted_files.h"

struct rooted_handle
{
    intptr_t native;
    bool directory;
};
typedef enum
{
    ROOTED_CREATE_NONE,
    ROOTED_CREATE_OPEN,
    ROOTED_CREATE_EXCLUSIVE
} rooted_create;
bool rooted_native_name_valid(const char *);
bool rooted_native_name_equal(rooted_files *, const char *, const char *);
rooted_files *rooted_native_root(const char *);
rooted_files *rooted_native_duplicate(rooted_files *);
rooted_files *rooted_native_child(
    rooted_files *,
    const char *,
    bool directory,
    rooted_create create,
    unsigned permissions
);
bool rooted_native_stat(rooted_files *, const char *, rooted_file_info *);
bool rooted_native_list(rooted_files *, int limit, rooted_file_entry **, int *);
bool rooted_native_move(rooted_files *, const char *, rooted_files *, const char *, bool replace);
bool rooted_native_remove(rooted_files *, const char *);
bool rooted_native_remove_directory(rooted_files *, const char *);
bool rooted_native_permissions(rooted_stream *, unsigned permissions);
#endif
