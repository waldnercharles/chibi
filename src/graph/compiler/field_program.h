#pragma once

#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"

typedef struct field_program field_program;
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
} field_grid;

/* The owned program contains only scalar instructions and copied parameters.
 * Compiling never attaches an ECS/behavior instance or borrows graph storage. */
field_program *field_program_compile(const behavior_asset *asset, char *error, size_t size);
void field_program_free(field_program *program);
bool field_program_bake(
    const field_program *program,
    field_context context,
    field_grid grid,
    float *out,
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
extern const registry_pack FIELD_NODE_PACK;
