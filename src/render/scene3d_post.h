#ifndef CHIBI_SCENE3D_POST_H
#define CHIBI_SCENE3D_POST_H

#include "platform/cute.h"
#include "runtime/components.h"

typedef struct render_scene3d_post render_scene3d_post;

render_scene3d_post *render_scene3d_post_new();
void render_scene3d_post_free(render_scene3d_post *post);
/* Exposure is EV [-16,16], strength [0,8], scene-linear threshold [0,65504],
 * Gaussian radius [0,.25] times viewport height, and adaptation speed 0 or
 * [0.1,100] per second. Zero radius or strength disables bloom. Invalid
 * settings leave the previous output available. */
bool render_scene3d_post_prepare(render_scene3d_post *post, int width, int height, const Environment *settings);
/* After prepare, queue only the scene and cf_render_to(target, true).
 * process requires that draw queue to be flushed; it preserves draw state and
 * completes all of its own passes. Output is opaque, tone-mapped sRGB. Auto
 * exposure adapts over the scene time since the previous process, and snaps
 * on its first frame or when time runs backward. */
CF_Canvas render_scene3d_post_target(const render_scene3d_post *post);
bool render_scene3d_post_process(render_scene3d_post *post, float time);
CF_Canvas render_scene3d_post_output(const render_scene3d_post *post);
/* Forgets the adapted exposure, so the next process snaps, as for a new scene. */
void render_scene3d_post_reset(render_scene3d_post *post);
const char *render_scene3d_post_error(const render_scene3d_post *post);

#endif
