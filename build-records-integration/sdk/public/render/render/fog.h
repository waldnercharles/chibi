#ifndef CHIBI_RENDER_FOG_H
#define CHIBI_RENDER_FOG_H

#include "render/scene3d.h"
#include "render/view.h"
#include "runtime/engine.h"

typedef struct render_fog render_fog;
render_fog *render_fog_new();
void render_fog_free(render_fog *fog);
bool render_fog_draw(
    render_fog *fog,
    const visibility *visibility,
    const engine_viewport_camera *frame,
    const render_scene3d_camera *camera,
    float time
);

#endif
