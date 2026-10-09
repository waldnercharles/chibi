#ifndef CHIBI_SCENE3D_POST_H
#define CHIBI_SCENE3D_POST_H

#include "platform/cute.h"
#include "runtime/components.h"

typedef struct render_scene3d_post render_scene3d_post;

render_scene3d_post *render_scene3d_post_new();
void render_scene3d_post_free(render_scene3d_post *post);
/* Exposure is EV [-16,16], strength [0,8], scene-linear threshold [0,65504],
 * and Gaussian radius [0,.25] times viewport height. Zero radius or strength
 * disables bloom. Invalid settings leave the previous output available. */
bool render_scene3d_post_prepare(render_scene3d_post *post, int width, int height, const GameEnvironment *settings);
/* After prepare, queue only the scene and cf_render_to(target, true).
 * process requires that draw queue to be flushed; it preserves draw state and
 * completes all of its own passes. Output is opaque, tone-mapped sRGB. */
CF_Canvas render_scene3d_post_target(const render_scene3d_post *post);
bool render_scene3d_post_process(render_scene3d_post *post);
CF_Canvas render_scene3d_post_output(const render_scene3d_post *post);
const char *render_scene3d_post_error(const render_scene3d_post *post);

#endif
