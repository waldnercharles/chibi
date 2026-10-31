#ifndef CHIBI_MODEL_BAKE_H
#define CHIBI_MODEL_BAKE_H
#include "runtime/geometry/mesh_prepared.h"
#include "runtime/models/model_program.h"
#include "types/value_wire.h"

/* The most triangles a bake makes. Each baked triangle owns three vertices
 * and three indices, and a mesh holds at most EXECUTION_WIRE_MAX_RECORDS
 * vertices, indices and groups together. */
#define MODEL_BAKE_TRIANGLE_MAX                                                \
    ((EXECUTION_WIRE_MAX_RECORDS - RENDER_MESH_PREPARED_GROUP_MAX) / 6)

typedef struct
{
    float cell_size; /* Zero chooses longest bound extent / 48. */
    int max_axis_cells, max_triangles, edge_iterations;
    uint64_t max_samples, max_work;
} model_bake_options;
typedef struct
{
    render_mesh_asset mesh;
    model_bounds bounds;
    float parameters[SHADER_PARAMETER_MAX][4];
    uint64_t samples, work;
    float cell_size, cell_diagonal, max_residual;
    int cells[3], boundary_triangles;
} model_bake_result;
model_bake_options model_bake_defaults();
void model_bake_result_free(model_bake_result *);
/* Evaluates a fixed parameter snapshot. Result is zero initialized; failure
 * preserves it. Geometry and per-face material ownership are approximations;
 * sampling can lose features narrower than the grid. No appearance baking. */
bool model_bake(
    const model_program *,
    const float incoming[SHADER_PARAMETER_MAX][4],
    const model_bake_options *,
    model_bake_result *,
    shader_error *
);
#endif
