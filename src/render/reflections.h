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
typedef struct render_reflections render_reflections;
render_reflections *render_reflections_new();
void render_reflections_free(render_reflections *reflections);
/* Borrowed output, or the original source when there are no reflectors. */
CF_Canvas render_reflections_draw(
    render_reflections *reflections,
    CF_Canvas source,
    int width,
    int height,
    render_view_2d view,
    const render_reflection *surfaces,
    int count
);
#endif
