#ifndef CHIBI_MESH_PREPARE_H
#define CHIBI_MESH_PREPARE_H
#include "runtime/geometry/mesh_asset.h"
/* Explicit authored content root; copied until unmount or replacement. */
bool authoring_mesh_mount_content(const char *, char *, size_t);
void authoring_mesh_unmount_content();
bool render_mesh_asset_parse(const char *, render_mesh_asset *, char *, size_t);
const render_mesh_asset *authoring_mesh_prepare(const char *, char *, size_t);
#endif
