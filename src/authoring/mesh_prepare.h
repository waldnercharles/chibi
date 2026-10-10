#ifndef CHIBI_MESH_PREPARE_H
#define CHIBI_MESH_PREPARE_H
#include "runtime/geometry/mesh_asset.h"
/* Explicit authored content root; copied until unmount or replacement. */
bool authoring_mesh_mount_content(const char *, char *, size_t);
void authoring_mesh_unmount_content();
/* Reads a content/... `.mesh` file (the prepared mesh format) and publishes
 * it to the resident catalog, unless that source is already resident. */
const render_mesh_asset *authoring_mesh_prepare(const char *, char *, size_t);
#endif
