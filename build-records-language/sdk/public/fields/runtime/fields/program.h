#pragma once
#include "runtime/fields/ir.h"
#include "runtime/tasks.h"
#include "types/value.h"

#define FIELD_PARAMETER_NAME_MAX 1024u

typedef struct field_program field_program;
typedef struct field_program_baker field_program_baker;
typedef struct
{
    const char *name;
    pin_value value;
} field_parameter;
typedef struct
{
    unsigned seed;
    const field_parameter *parameters;
    int parameter_count;
} field_context;
typedef struct
{
    int width, height;
    float cell_size;
    float center_x, center_y;
    /* Field Z for every sample: depth behind the play plane. 2D bakes leave
     * it zero, so a 3D field's play-plane slice is its ordinary 2D bake. */
    float depth;
} field_grid;

/* Validates scalar op arity/topology and snapshots code, parameter names and
 * finite scalar defaults. Caller storage can retire immediately. */
field_program *field_program_new(
    const field_ir *,
    int count,
    int output,
    const field_parameter *,
    int parameter_count,
    char *error,
    size_t size
);
field_program *field_program_new_budget(
    const field_ir *,
    int count,
    int output,
    const field_parameter *,
    int parameter_count,
    size_t *remaining,
    char *error,
    size_t size
);
field_ir field_program_instruction_at(const field_program *, int);
int field_program_output(const field_program *);
int field_program_parameter_count(const field_program *);
/* Borrowed names/value type names remain valid until the program is freed. */
field_parameter field_program_parameter_at(const field_program *, int);
void field_program_free(field_program *program);
/* Retained owner, array capacities and copied names. */
size_t field_program_storage_bytes(const field_program *);
/* Optional borrowed tasks split rows across workers; the caller owns the
 * submission scope. NULL bakes on the calling thread. */
bool field_program_bake(
    const field_program *program,
    field_context context,
    field_grid grid,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
);
/* Bakes only rows [first, first + count) of the grid into out, which holds the
 * whole grid; the values match field_program_bake's exactly, so a grid can be
 * baked a slice at a time. */
bool field_program_bake_rows(
    const field_program *program,
    field_context context,
    field_grid grid,
    int first,
    int count,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
);
/* Prepared row baker for repeated slices of one program and parameter set.
 * The program is borrowed and must remain compiled at the same revision until
 * the baker is freed. Calls are serialized; workspace is retained per worker. */
field_program_baker *field_program_baker_new(
    const field_program *program,
    field_context context,
    int workers,
    char *error,
    size_t size
);
void field_program_baker_free(field_program_baker *baker);
bool field_program_baker_bake_rows(
    field_program_baker *baker,
    field_grid grid,
    int first,
    int count,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
);
/* Evaluates arbitrary positions: x, y and depth z per point, matching what a
 * grid bake yields at the same coordinates. Points are batched and spread over
 * the baker's workers. */
bool field_program_baker_bake_points(
    field_program_baker *baker,
    const float *x,
    const float *y,
    const float *z,
    int count,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
);
bool field_program_sample(
    const field_program *program,
    field_context context,
    float x,
    float y,
    float *out,
    char *error,
    size_t size
);
int field_program_instruction_count(const field_program *program);
/* Operands an instruction of op reads. */
int field_op_arity(field_op op);
/* op over its field_op_arity operands, as field_program_sample computes it;
 * NAN where op is undefined. Seeded ops add seed, a field context's, to their
 * Seed Offset. Ops without operands read the sample and are not valued here. */
float field_op_value(field_op op, const float *operands, unsigned seed);
