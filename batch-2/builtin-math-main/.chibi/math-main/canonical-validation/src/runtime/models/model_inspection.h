#ifndef CHIBI_MODEL_INSPECTION_H
#define CHIBI_MODEL_INSPECTION_H
#include "runtime/models/model_program.h"

typedef struct
{
    uint64_t node;
    char *asset, *name, *path;
    int distance_nodes; /* Inclusive reachable IR; shared work overlaps other parts. */
    int material;
} model_part_inspection;
typedef struct
{
    int distance_nodes, hit_nodes, public_slots, private_slots;
    int material_slots, images, ramp_vec4s;
    int distance_limit, hit_limit, parameter_limit, material_limit, image_limit,
        ramp_limit;
    model_bounds bounds;
    model_part_inspection *parts;
} model_inspection;

void model_inspection_free(model_inspection *report);
/* Copies resource counts and labels before IR ownership moves to GPU catalog.
 * Report must start zeroed; replacement releases its previous owned labels. */
void model_program_inspect(const model_program *program, model_inspection *report);
#endif
