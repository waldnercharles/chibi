#pragma once
#include "runtime/material_field.h"
#include "runtime/material_mask.h"

enum
{
    MATERIAL_TERRAIN_CHUNK_CELLS = 128
};
enum
{
    MATERIAL_CONNECTIVITY_ALL,
    MATERIAL_CONNECTIVITY_FIRST_AIR
};
typedef struct
{
    material_mask mask;
    float x, y;
} material_terrain_chunk;

/* Connectivity is discrete occupancy cleanup and does not change field semantics. */
void material_terrain_connect(float *field, field_grid grid, int connectivity, int border_cells);
material_terrain_chunk *material_terrain_chunks(const float *field, field_grid grid);
void material_terrain_chunks_free(material_terrain_chunk *chunks);
