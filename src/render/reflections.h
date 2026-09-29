#ifndef CHIBI_RENDER_REFLECTIONS_H
#define CHIBI_RENDER_REFLECTIONS_H
#include "platform/cute.h"
#include "render/view_2d.h"

typedef struct
{
    CF_V2 corners[4];
    /* Y-up mirror anchor and unit tangent; source is the unreflected scene. */
    float plane[4], tint[4];
    float strength, roughness;
} render_reflection;

/* One pass of reflections over a finished frame: liquid surfaces mirror what
 * lies above them and planar reflectors mirror across their planes. */
typedef struct
{
    /* The frame, and the canvas that receives it with reflections. */
    CF_Canvas source, destination;
    /* The camera rectangle, and where it lies in the material textures. */
    render_view_2d view;
    /* Material normal, which marks liquid; see render_material_textures. */
    CF_Texture normal;
    /* Liquid reflection strength, zero for none, and simulation time for
     * its ripples. */
    float liquid, time;
    const render_reflection *surfaces;
    int count;
} render_reflection_frame;

typedef struct render_reflections render_reflections;
render_reflections *render_reflections_new();
void render_reflections_free(render_reflections *reflections);
/* A display canvas the size given, for frames without an HDR spare. */
CF_Canvas render_reflections_canvas(render_reflections *reflections, int width, int height);
/* False when a shader is missing. */
bool render_reflections_draw(render_reflections *reflections, const render_reflection_frame *frame);
#endif
