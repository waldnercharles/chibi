#ifndef CHIBI_MESH_PREPARED_H
#define CHIBI_MESH_PREPARED_H
#include "runtime/geometry/mesh_asset.h"
#define RENDER_MESH_PREPARED_VERSION UINT64_C(3)
/* Portable field-wise indexed triangle/part/material payload. It is also the
 * `.mesh` content format. Resident identity is assigned by the catalog after
 * publication, never persisted as a process ID. Writer bytes use free();
 * initialized result and budget survive failed reads. */
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
