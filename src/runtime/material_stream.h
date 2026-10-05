#pragma once

#include "runtime/material_field.h"
#include "runtime/material_mask.h"

#include <stdint.h>

typedef struct material_stream material_stream;
typedef struct
{
    uint64_t key;
    field_grid grid;
    /* Owned by the stream. Its address is stable until this tile unloads. */
    material_mask *mask;
} material_stream_tile;
typedef struct
{
    /* Tiles within radius prepare under the budget. Tiles within a positive
     * required extent prepare immediately, so visible terrain never pops in. */
    float x, y, radius, required;
} material_stream_view;
typedef struct
{
    /* Zero width and height generate unbounded signed tile coordinates. */
    field_grid grid;
    const char *field, *parameters;
    unsigned seed;
    int island_cells;
    /* Optional borrowed executor for generation. The caller opens its
     * submission scope around material_stream_update. */
    const game_task_system *tasks;
} material_stream_desc;
typedef struct
{
    int resident, stored, loaded, unloaded;
    size_t resident_bytes, stored_bytes;
} material_stream_state;

/* Fields are borrowed and must outlive the stream; configuration strings are
 * copied. Recreate on field revision or generation changes. Cold masks live in
 * a temporary file owned by the run. */
material_stream *material_stream_new(material_stream_desc desc);
void material_stream_free(material_stream *stream);
/* Outside mask consumers: evict distant tiles, prepare every missing required
 * tile, then at most budget further tiles, nearest view first. Hysteresis
 * prevents boundary thrashing. */
bool material_stream_update(
    material_stream *stream,
    const material_fields *fields,
    const material_stream_view *views,
    int view_count,
    int budget,
    char *error,
    size_t size
);
const material_stream_tile *material_stream_tiles(const material_stream *stream, int *count);
material_stream_state material_stream_inspect(const material_stream *stream);
/* Whether the last update left a tile its views want unprepared. */
bool material_stream_waiting(const material_stream *stream);
