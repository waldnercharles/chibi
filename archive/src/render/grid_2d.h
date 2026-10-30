#ifndef CHIBI_RENDER_GRID_2D_H
#define CHIBI_RENDER_GRID_2D_H

#include <cute_math.h>

/* An editor's adaptive grid over `view`, in world units with Y up, for a
 * canvas showing `pixel` world units per pixel: lines every `increment` and
 * at each coarser level four times as far apart, each level fading in as its
 * lines spread on screen (Blender's and Unity's grids), with the X axis red
 * and the Y axis green. Its colors suit the scene's dark canvas. A positive
 * `linear` draws them as linear light scaled by it, for a backdrop that is
 * lit and exposed before it shows; zero draws display colors. */
void render_grid_2d_draw(float increment, CF_Aabb view, float pixel, float linear);

#endif
