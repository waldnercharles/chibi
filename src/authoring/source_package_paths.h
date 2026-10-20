#ifndef CHIBI_AUTHORING_SOURCE_PACKAGE_PATHS_H
#define CHIBI_AUTHORING_SOURCE_PACKAGE_PATHS_H

#include "support/arena.h"

#include <stdbool.h>

enum
{
    SOURCE_PACKAGE_PATH_MAX = 4096
};

/* Private lexical path mechanics shared by snapshot root/file/import policy.
 * No filesystem, symlink, environment or cwd inspection occurs here. */
bool source_package_path_bounded(const char *path);
/* root and path are non-NULL, terminated strings. Snapshot callers supply
 * canonical absolute roots; this predicate does not normalize its inputs. */
bool source_package_path_within(const char *root, const char *path);
/* Returns an owned arena string or NULL. Only path is checked for NULL,
 * backslashes and the byte limit. A base used for a relative path and a present
 * floor must be borrowed, terminated canonical absolute paths. Absolute paths
 * ignore base. The helper does not independently validate base or floor.
 * It rejects an intermediate .. escape as well as a final escape from a
 * supplied floor. */
char *source_package_path_normalize(arena *, const char *base, const char *path, const char *floor);
#endif
