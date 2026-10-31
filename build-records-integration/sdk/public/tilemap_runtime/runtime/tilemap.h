#ifndef CHIBI_RUNTIME_TILEMAP_H
#define CHIBI_RUNTIME_TILEMAP_H

#include <cute_math.h>
#include <stdbool.h>

/* Zero-initialize and free explicitly. Rows increase along local +Y; the entity
 * origin is the corner of cell (0, 0). -1 denotes an unoccupied cell. */
typedef struct tilemap_grid
{
    int *frames;
    char *cells;
    int columns, rows;
    int frame_width, frame_height;
    bool initialized;
    const char *error;
} tilemap_grid;

/* Retains its own input copy; unchanged content and dimensions reuse the grid.
 * Invalid input clears frames and records a static error message. */
bool tilemap_grid_update(
    tilemap_grid *grid,
    const char *cells,
    int columns,
    int rows,
    int frame_width,
    int frame_height
);
void tilemap_grid_free(tilemap_grid *grid);
int tilemap_grid_frame(const tilemap_grid *grid, int column, int row);
int tilemap_grid_point(const tilemap_grid *grid, CF_V2 translation, float radians, CF_V2 world_point);
/* Counts occupied cells with positive-area intersection with a world AABB.
 * Rotation follows Orientation in gameplay coordinates (+Y down). */
int tilemap_grid_overlap_count(const tilemap_grid *grid, CF_V2 translation, float radians, CF_Aabb world_box);

/* Bounds the complete transformed grid, including empty cells. */
bool tilemap_bounds(
    int columns,
    int rows,
    int frame_width,
    int frame_height,
    CF_V2 translation,
    float radians,
    CF_V2 *min,
    CF_V2 *max
);

#endif
