#pragma once

#include "runtime/material_stream.h"

#define MATERIAL_CHECKPOINT_VERSION 1
#define MATERIAL_CHECKPOINT_MAX_BYTES (64u * 1024u * 1024u)
#define MATERIAL_CHECKPOINT_MAX_TILES 256
#define MATERIAL_CHECKPOINT_ID_MAX 1024

/* Optional portable terrain snapshot codec. Tiles own masks, including empty
 * masks. Geometry, ECS handles, fragments, fluids and actors are excluded. */
typedef struct material_checkpoint
{
    material_stream_tile *tiles;
} material_checkpoint;

typedef struct
{
    const material_fields *fields;
    const char *field;
    field_context context;
    field_grid grid;
    int connectivity, border_cells, island_cells;
    bool streamed;
} material_checkpoint_recipe;

bool material_checkpoint_fingerprint(material_checkpoint_recipe, uint64_t *, char *error, size_t size);
bool material_checkpoint_write(
    const material_checkpoint *,
    const char *identity,
    uint64_t recipe,
    unsigned char **bytes,
    size_t *length,
    char *error,
    size_t size
);
bool material_checkpoint_read(
    const void *bytes,
    size_t length,
    const char *identity,
    uint64_t recipe,
    material_checkpoint *,
    char *error,
    size_t size
);
void material_checkpoint_free(material_checkpoint *);
/* Copy canonical cell state, without retaining or constructing derived geometry. */
material_mask *material_checkpoint_copy_mask(const material_mask *, field_grid);
