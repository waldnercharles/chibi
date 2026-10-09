#ifndef CHIBI_RENDER_SCENE3D_SKY_H
#define CHIBI_RENDER_SCENE3D_SKY_H

#include "render/scene3d.h"
#include "runtime/components.h"

#include <stdbool.h>
#include <stddef.h>

/* The sky an Environment draws behind the 3D scene: an RGBA16F cube map built
 * from its resident equirectangular sky, drawn wherever no surface is. */
typedef struct render_scene3d_sky render_scene3d_sky;
render_scene3d_sky *render_scene3d_sky_new();
void render_scene3d_sky_free(render_scene3d_sky *sky);
/* Checks the Environment's sky fields and rebuilds the cube when its sky
 * changed; false with `error` saying why. An empty sky draws nothing. */
bool render_scene3d_sky_prepare(
    render_scene3d_sky *sky,
    const GameEnvironment *environment,
    char *error,
    size_t size
);
/* Queues the prepared sky into the current 3D draw, after opaque surfaces and
 * before translucent ones so they blend over it. */
void render_scene3d_sky_draw(const render_scene3d_sky *sky, const render_scene3d_camera *camera);

#endif
