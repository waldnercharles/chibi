#ifndef CHIBI_RENDER_SCENE3D_INFLUENCE_H
#define CHIBI_RENDER_SCENE3D_INFLUENCE_H

#include "ecs/ecs.h"

struct engine;
typedef struct render_influences render_influences;

render_influences *render_influences_new();
void render_influences_free(render_influences *influences);
void render_influences_reset(render_influences *influences);
/* Retains at most 64 planar source samples, ranked by remaining colored strength.
 * Time is simulation seconds; pause freezes decay and rewind clears history. */
void render_influences_collect(
    render_influences *influences,
    ecs_world *world,
    const struct engine *runtime,
    float time
);
/* One source at the origin, replacing any collected history. */
void render_influences_preview(render_influences *influences, const float color[3], float radius);
void render_influences_bind(render_influences *influences);
/* shader_sample_context resolver: ignores world Y, combines by channel maximum. */
void render_influences_sample(void *influences, const float position[3], float color[3]);

#endif
