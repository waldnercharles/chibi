#ifndef CHIBI_RENDER_SCENE3D_SKY_H
#define CHIBI_RENDER_SCENE3D_SKY_H

#include "render/scene3d.h"
#include "runtime/components.h"

#include <stdbool.h>
#include <stddef.h>

/* The sky an Environment draws behind the 3D scene and lights it with: an
 * RGBA16F cube map built from its resident equirectangular sky, or rendered
 * from its sky material, drawn wherever no surface is, plus its irradiance and
 * a GGX-prefiltered copy for surfaces' diffuse and specular light. A sky
 * material draws live; its light is its look at time zero, rebuilt when the
 * material, its parameters or the shader catalog change. */
typedef struct render_scene3d_sky render_scene3d_sky;
render_scene3d_sky *render_scene3d_sky_new();
void render_scene3d_sky_free(render_scene3d_sky *sky);
/* Checks the Environment's sky fields and rebuilds the cubes when its sky
 * changed; false with `error` saying why. An empty sky draws and lights
 * nothing. */
bool render_scene3d_sky_prepare(
    render_scene3d_sky *sky,
    const Environment *environment,
    char *error,
    size_t size
);
/* The sky's brightness, zero without a sky; its specular cube's last mip;
 * and the cosine and sine of its turn. */
CF_V4 render_scene3d_sky_light(const render_scene3d_sky *sky);
/* The nine spherical harmonics of the sky's irradiance, before its turn and
 * brightness. */
CF_V4 *render_scene3d_sky_irradiance(render_scene3d_sky *sky);
/* Sets the sky light uniforms and textures every 3D surface shader reads,
 * whether a sky shows or not; unbind clears the textures. */
void render_scene3d_sky_bind(render_scene3d_sky *sky);
void render_scene3d_sky_unbind();
/* Queues the prepared sky into the current 3D draw, after opaque surfaces and
 * before translucent ones so they blend over it. */
void render_scene3d_sky_draw(const render_scene3d_sky *sky, const render_scene3d_camera *camera);

#endif
