#ifndef CHIBI_RENDER_POST_2D_H
#define CHIBI_RENDER_POST_2D_H

#include "platform/cute.h"

#include <stdbool.h>

/* HDR presentation for the 2D scene: energy-conserving bloom, exposure and
 * AgX tone mapping from a linear RGBA16F target into a display canvas. */
typedef struct render_post_2d render_post_2d;

render_post_2d *render_post_2d_new();
void render_post_2d_free(render_post_2d *post);
/* Allocates the linear target and bloom levels; false when RGBA16F targets
 * are unavailable. */
bool render_post_2d_resize(render_post_2d *post, int width, int height);
CF_Canvas render_post_2d_target(const render_post_2d *post);
/* Exposure is in EV; bloom in [0, 1] mixes the blurred image into the scene.
 * The target must hold the finished linear frame. */
void render_post_2d_present(render_post_2d *post, CF_Canvas output, float exposure, float bloom, float dither_phase);

#endif
