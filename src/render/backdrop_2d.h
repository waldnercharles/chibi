#ifndef CHIBI_RENDER_BACKDROP_2D_H
#define CHIBI_RENDER_BACKDROP_2D_H

#include "ecs/ecs.h"
#include "platform/cute.h"
#include "render/radiance.h"
#include "render/view_2d.h"

/* Material-graph layers behind the 2D scene (GameBackdrop2D), drawn at half
 * the output resolution into a premultiplied linear canvas that the composite
 * lays over the starfield in open space. The canvas is reused while nothing a
 * layer reads changes. */
typedef struct render_backdrops render_backdrops;

typedef struct
{
    /* The camera rectangle, and where it lies in the material canvas. */
    render_view_2d camera;
    float time;
    render_radiance_light light;
    /* Output pixels; the canvas has half as many per side. */
    int width, height;
} render_backdrop_frame;

render_backdrops *render_backdrops_new();
void render_backdrops_free(render_backdrops *backdrops);
/* Reads the world's layers; false when there are none. */
bool render_backdrops_collect(render_backdrops *backdrops, ecs_world *world);
/* The layers; a zero id with render_backdrops_error set when a program
 * cannot compile or the canvas cannot be allocated. */
CF_Texture render_backdrops_draw(render_backdrops *backdrops, const render_backdrop_frame *frame);
const char *render_backdrops_error(const render_backdrops *backdrops);

#endif
