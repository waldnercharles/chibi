#include "render/grid_2d.h"

#include <assert.h>
#include <cute_app.h>
#include <cute_draw.h>
#include <math.h>
#include <stdbool.h>

enum
{
    GRID_LEVEL = 4,  /* a level's spacing, in the next finer level's */
    GRID_LEVELS = 8, /* at most, finest first */
};
static const float GRID_MIN_PIXELS = 8; /* closer levels are not drawn */
static const float GRID_FULL_PIXELS = 160; /* levels this far apart draw at full strength */
static const float GRID_ALPHA = 0.16f;
static const float GRID_AXIS_ALPHA = 0.4f;
static const float GRID_AA = 1;
static const unsigned GRID_LINE = 0xFFFFFF;
static const unsigned GRID_X_AXIS = 0xE0727A;
static const unsigned GRID_Y_AXIS = 0x82C46B;

/* A display color, or as linear light scaled by `linear` when positive:
   over black, either shows the same. */
static CF_Color grid_color(unsigned rgb, float alpha, float linear)
{
    CF_Color color = { (float)(rgb >> 16 & 255) / 255,
                       (float)(rgb >> 8 & 255) / 255,
                       (float)(rgb & 255) / 255,
                       alpha };
    if (linear > 0) {
        color.r = powf(color.r, 2.2f);
        color.g = powf(color.g, 2.2f);
        color.b = powf(color.b, 2.2f);
        color.a = fminf(powf(alpha, 2.2f) * linear, 1);
    }
    return color;
}

/* The lines at multiples of `step` across `view`, except the axes and, but
   for the coarsest level, the next level's. */
static void level_lines(float step, bool coarsest, CF_Aabb view, float thickness)
{
    for (int axis = 0; axis < 2; axis++) {
        float low = axis ? view.min.y : view.min.x;
        float high = axis ? view.max.y : view.max.x;
        for (long i = lroundf(ceilf(low / step)); (float)i * step <= high; i++) {
            if (!i || (!coarsest && i % GRID_LEVEL == 0)) {
                continue;
            }
            float at = (float)i * step;
            CF_V2 a = axis ? (CF_V2){ view.min.x, at } : (CF_V2){ at, view.min.y };
            CF_V2 b = axis ? (CF_V2){ view.max.x, at } : (CF_V2){ at, view.max.y };
            cf_draw_line(a, b, thickness);
        }
    }
}

void render_grid_2d_draw(float increment, CF_Aabb view, float pixel, float linear)
{
    assert(increment > 0 && pixel > 0);
    /* A pixel's antialiasing at any camera zoom. */
    cf_draw_push_shape_aa(GRID_AA * pixel * cf_app_get_pixel_scale());
    float extent = fmaxf(view.max.x - view.min.x, view.max.y - view.min.y);
    float step = increment;
    while (step < GRID_MIN_PIXELS * pixel) {
        step *= GRID_LEVEL;
    }
    for (int level = 0; level < GRID_LEVELS && step <= extent; level++) {
        bool coarsest = level == GRID_LEVELS - 1 || step * GRID_LEVEL > extent;
        float t = (step / pixel - GRID_MIN_PIXELS) /
                  (GRID_FULL_PIXELS - GRID_MIN_PIXELS);
        t = fminf(fmaxf(t, 0), 1);
        cf_draw_push_color(grid_color(GRID_LINE, GRID_ALPHA * t * (2 - t), linear));
        level_lines(step, coarsest, view, pixel);
        cf_draw_pop_color();
        step *= GRID_LEVEL;
    }
    float axis = 1.5f * pixel;
    if (view.min.y <= 0 && view.max.y >= 0) {
        cf_draw_push_color(grid_color(GRID_X_AXIS, GRID_AXIS_ALPHA, linear));
        cf_draw_line((CF_V2){ view.min.x, 0 }, (CF_V2){ view.max.x, 0 }, axis);
        cf_draw_pop_color();
    }
    if (view.min.x <= 0 && view.max.x >= 0) {
        cf_draw_push_color(grid_color(GRID_Y_AXIS, GRID_AXIS_ALPHA, linear));
        cf_draw_line((CF_V2){ 0, view.min.y }, (CF_V2){ 0, view.max.y }, axis);
        cf_draw_pop_color();
    }
    cf_draw_pop_shape_aa();
}
