#ifndef CHIBI_MATERIAL_MASK_H
#define CHIBI_MATERIAL_MASK_H

#include <stdbool.h>

/* Local-space rectangles exactly cover the surviving cells, without overlap. */
typedef struct
{
    float x, y, width, height;
} material_rect;

/* Marching-squares visual coverage. Collision retains the exact cell rectangles. */
typedef struct
{
    float points[12];
    int count;
} material_polygon;

typedef struct
{
    int width, height, count;
    float cell_size;
    unsigned revision;
    unsigned char *cells;
    float *damage;
    /* Signed samples at cell centers: negative inside. */
    float *distance;
    material_rect *rects;
    material_polygon *surface;
} material_mask;

/* Borrowed immutable views. Damage/rebuild or free invalidates their storage. */
typedef struct { const unsigned char *cells; const float *distance; int width,height,count; float cell_size; unsigned revision; } material_occupancy_view;
typedef struct { const material_rect *rects; int count; unsigned revision; } material_collision_view;
typedef struct { const material_polygon *polygons; int count; unsigned revision; } material_surface_view;
material_occupancy_view material_mask_occupancy(const material_mask *mask);
material_collision_view material_mask_collision(const material_mask *mask);
material_surface_view material_mask_surface(const material_mask *mask);

material_mask material_mask_new(int width, int height, float cell_size, bool ellipse);
/* Copies a row-major occupancy grid. */
material_mask material_mask_from_cells(int width, int height, float cell_size, const unsigned char *cells);
material_mask material_mask_from_field(int width, int height, float cell_size, const float *distance);
void material_mask_free(material_mask *mask);
/* Carving and splitting retain the original local origin. */
int material_mask_carve(material_mask *mask, float x, float y, float radius);
/* Batch damage edits cells locally. Rebuild once before using rectangles. */
int material_mask_apply_damage(material_mask *mask, float x, float y, float radius, float energy, float hardness);
void material_mask_rebuild(material_mask *mask);
/* Retains the largest connected component in mask. Returns an owned array of
 * other components with at least min_cells; smaller components are discarded.
 * The caller frees each mask and then the array with afree. */
material_mask *material_mask_split(material_mask *mask, int min_cells, int *dust);
/* Terrain keeps every component touching a chunk edge; internal islands detach. */
material_mask *material_mask_split_anchored(material_mask *mask, int min_cells, int *dust);
bool material_mask_contains(const material_mask *mask, float x, float y);


material_mask *material_mask_extract_fragments(material_mask *mask, int minimum, bool anchored, int *dust);

#endif
