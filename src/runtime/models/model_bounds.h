#ifndef CHIBI_MODEL_BOUNDS_H
#define CHIBI_MODEL_BOUNDS_H
#include "runtime/models/model_program_internal.h"
#define MODEL_BOUND_INPUT_MAX 8
#define MODEL_BOUND_POINT_MAX 33
/* Prepared construction rows own only scalar/vector literals and child indices.
 * Numeric inputs refer to the immutable numeric shader program. */
typedef struct
{
    int child, expression, resource, width;
    pin_type type;
    float value[4];
    int choice;
} model_bound_input;
typedef struct
{
    uint64_t id, part;
    model_op op;
    int input_count, point_count;
    model_bound_input inputs[MODEL_BOUND_INPUT_MAX];
    float points[MODEL_BOUND_POINT_MAX][3];
} model_bound_node;
typedef struct model_bounds_plan
{
    model_bound_node *nodes;
    char **resources; /* Owned immutable resource keys, indexed by prepared inputs. */
    int root;
} model_bounds_plan;
void model_bounds_plan_free(model_bounds_plan *);
bool model_bounds_plan_equal(const model_bounds_plan *, const model_bounds_plan *);
bool model_bounds_plan_evaluate(
    const model_bounds_plan *,
    const float (*)[4],
    int,
    int,
    model_bounds *,
    shader_error *
);
#endif
