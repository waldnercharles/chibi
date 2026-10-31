#ifndef CHIBI_MESH_PREPARED_H
#define CHIBI_MESH_PREPARED_H
#include "runtime/geometry/mesh_asset.h"
/* Portable field-wise indexed triangle/part/material payload. It is also the
 * `.mesh` content format, so changing the version or the value wire codec
 * needs a project migration step that rewrites projects' `.mesh` files.
 * Resident identity is assigned by the catalog after publication, never
 * persisted as a process ID. Writer bytes use free(); initialized result and
 * budget survive failed reads. */
#define RENDER_MESH_PREPARED_VERSION UINT64_C(3)
#define RENDER_MESH_PREPARED_GROUP_MAX 1024
/* Whether a mesh of `vertices` and `indices` stays within what a reader
 * decodes: its vertex limit, and its storage limit before groups. A baker
 * can check it as a mesh grows, before the mesh outgrows memory. */
bool render_mesh_prepared_fits(int vertices, int indices);
bool render_mesh_asset_prepared_write(
    const render_mesh_asset *,
    unsigned char **,
    size_t *,
    char *error,
    size_t
);
bool render_mesh_asset_prepared_read_budget(
    const void *,
    size_t,
    size_t *remaining,
    render_mesh_asset *,
    char *error,
    size_t
);
#endif
