#ifndef CHIBI_MODEL_PROFILE_PATHS_H
#define CHIBI_MODEL_PROFILE_PATHS_H
#include "graph/shader/shader_graph.h"
#include "types/array.h"

/* Cubic sampling produces a polygonal approximation. Closed controls contain
 * 3N points with implicit final P0; open controls contain 3N+1 points.
 * The returned array owns its vector payloads; outputs are unchanged on error. */
bool model_profile_sample_cubic(
    const pin_array *controls,
    int width,
    bool closed,
    int steps,
    uint64_t node_id,
    pin_array **out,
    shader_error *error
);
void model_profile_samples_free(pin_array *samples);
#endif
