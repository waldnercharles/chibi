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

/* Processing-owned temporary geometry/connectivity storage, shared across masks.
 * No borrowed work storage survives these calls or a host callback. */
typedef struct material_mask_work material_mask_work;
material_mask_work *material_mask_work_new();
void material_mask_work_free(material_mask_work *work);
void material_mask_rebuild_work(material_mask *mask, material_mask_work *work);
/* Copies occupancy and distances only; rebuild before exposing geometry. */
material_mask material_mask_copy_cells(int width, int height, float cell_size, const unsigned char *cells);
/* Connectivity edits source occupancy without rebuilding its geometry. Returned
 * owned fragment masks have ready geometry; rebuild source before callbacks. */
material_mask *material_mask_detach_fragments(
    material_mask *mask,
    int minimum,
    bool anchored,
    int *dust,
    material_mask_work *work
);

/* Borrowed immutable views. Damage/rebuild or free invalidates their storage. */
typedef struct
{
    const unsigned char *cells;
    const float *distance;
    int width, height, count;
    float cell_size;
    unsigned revision;
} material_occupancy_view;
typedef struct
{
    const material_rect *rects;
    int count;
    unsigned revision;
} material_collision_view;
typedef struct
{
    const material_polygon *polygons;
    int count;
    unsigned revision;
} material_surface_view;
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
bool material_mask_contains(const material_mask *mask, float x, float y);

/* Retains the largest connected component in mask, or with anchored every
 * component touching a chunk edge. Returns an owned array of the other
 * components with at least minimum cells; smaller components are discarded.
 * The caller frees each mask and then the array with afree. */
material_mask *material_mask_extract_fragments(material_mask *mask, int minimum, bool anchored, int *dust);

#endif
