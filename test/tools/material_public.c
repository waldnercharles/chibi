/* This translation unit intentionally consumes only material_runtime's public
 * usage requirements. Broad engine linkage would hide missing header owners. */
#include "runtime/geometry/material_surface_mesh.h"

_Static_assert(sizeof(MaterialSurface3D) > 0, "public surface options");
_Static_assert(sizeof(MaterialFrame) > 0, "public material coordinates");
