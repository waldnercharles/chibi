#ifndef CHIBI_ASSETS_PROJECT_MIGRATE_H
#define CHIBI_ASSETS_PROJECT_MIGRATE_H

#include <stddef.h>

/* Upgrades the sources under `content`, and the canonical `start_scene` path
 * wherever it lies, from project version `from` to `from + 1`. Returns 0, or
 * -1 with `error` set. */
int chibi_project_migrate_content(
    const char *content,
    const char *start_scene,
    int from,
    char *error,
    size_t error_size
);

#endif
