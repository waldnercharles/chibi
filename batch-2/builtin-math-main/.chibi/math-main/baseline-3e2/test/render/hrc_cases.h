#ifndef CHIBI_TEST_RENDER_HRC_CASES_H
#define CHIBI_TEST_RENDER_HRC_CASES_H

#include "render/material_raster.h"
#include "runtime/components.h"

/* What the HRC render cases share: the lighting they were calibrated with,
 * the entities they light and block with, and the view they look through. */

/* The finest grid, no unlit color and bright emitters: the authoring defaults
 * the cases were calibrated with, whatever defaults authors start from. */
static inline HRC2D test_reference_lighting()
{
    HRC2D lighting = hrc2d_defaults();
    lighting.resolution = 1;
    lighting.ambient = 0;
    lighting.intensity = 7.5f;
    return lighting;
}

/* A green disc with a red border, emitting both, at world (-28, 0). */
static inline render_material_entity test_emitter()
{
    return (render_material_entity){
        .inv_rot = { 1, 0, 0, 1 },
        .pos = { -28, 0 },
        .extent = { 10, 10 },
        .shape_type = RENDER_SHAPE_CIRCLE,
        .radius = 8,
        .aa = 1,
        .border_thickness = 2,
        .scene = { 0, 1, 0, 1 },
        .emission = { 0, .08f, 0, 1 },
        .border_scene = { 1, 0, 0, 1 },
        .border_emission = { .2f, 0, 0, 1 },
    };
}

/* An opaque black wall through the origin, 128 units tall. */
static inline render_material_entity test_wall()
{
    return (render_material_entity){
        .inv_rot = { 1, 0, 0, 1 },
        .extent = { 5, 66 },
        .shape_type = RENDER_SHAPE_BOX,
        .aa = 1,
        .points = { 4, 64 },
        .scene = { 0, 0, 0, 1 },
        .absorption = { 1, 1, 1, 1 },
    };
}

/* A view of `world` units square around the origin. */
static inline render_view_2d test_centered(float world)
{
    return (render_view_2d){ .min_x = -world * .5f,
                             .max_y = world * .5f,
                             .width = world,
                             .height = world,
                             .crop_max_u = 1,
                             .crop_max_v = 1 };
}

#endif
