#ifndef CHIBI_RENDER_SCENE3D_GI_H
#define CHIBI_RENDER_SCENE3D_GI_H

#include "runtime/components.h"

#include <cute_math3d.h>
#include <stddef.h>

/* Global illumination for the lit pass: a grid of probes over the scene's
 * opaque solids, each tracing rays through a simplified copy of them every
 * frame and keeping the light those rays find, from the scene's lights, its
 * emissive surfaces and the sky, and from the probes again where they land,
 * which bounces it on. Reference: Majercik et al., Dynamic Diffuse Global
 * Illumination with Ray-Traced Irradiance Fields,
 * https://jcgt.org/published/0008/02/01/ */
typedef struct render_scene3d_gi render_scene3d_gi;

/* A solid that rays can hit: a built-in shape, or the box around an imported
 * mesh, placed by the frame of its unit shape, dimensions included. */
typedef struct
{
    CF_M4x4 frame;
    int primitive;
    CF_V3 albedo, emission; /* Linear. */
    /* Whether it is a mesh's box, which stands in for the mesh only seen from
     * outside: rays starting within it pass through it. */
    bool bounds;
    /* The box around the drawn solid, as a frame of the unit cube, which a
     * Box Frame's beams share: as in the lit pass, a light within it casts no
     * shadow from the solid (render_scene3d_light_shadow_near). */
    CF_M4x4 box;
} render_scene3d_gi_solid;

/* The lights the lit pass reads, and so the rays. */
enum
{
    RENDER_SCENE3D_GI_LIGHTS = 32
};

/* What lights the rays: the scene's lights, as the lit pass reads them, and
 * the sky, or else the ambient colors, where they reach no solid. Solids
 * block only the lights whose shadows the lit pass draws. */
typedef struct
{
    CF_V4 sun_direction, sun_color;
    CF_V4 ambient_sky, ambient_ground;
    CF_V4 sky_light;       /* As the lit pass's scene_sky_light. */
    CF_V4 *sky_irradiance; /* Its nine spherical harmonics. */
    /* RENDER_SCENE3D_GI_LIGHTS each, of which light_count are lit. */
    CF_V4 *light_positions, *light_colors, *light_cones;
    int light_count;
    int shadowed_lights; /* The first lights, which cast shadows. */
    bool sun_shadows;
} render_scene3d_gi_lighting;

render_scene3d_gi *render_scene3d_gi_new();
void render_scene3d_gi_free(render_scene3d_gi *gi);
/* Forgets the light the probes gathered, as after a scene loads. */
void render_scene3d_gi_reset(render_scene3d_gi *gi);
/* Takes the Environment's quality, releasing the probes and their light
 * while it is Off. Returns false with an error for an unknown one. */
bool render_scene3d_gi_prepare(
    render_scene3d_gi *gi,
    const Environment *environment,
    char *error,
    size_t size
);
bool render_scene3d_gi_active(const render_scene3d_gi *gi);
/* Fits the probes to the solids, traces their rays and updates the light
 * they hold. Returns false with an error past the solid limit or when its
 * shaders do not compile. */
bool render_scene3d_gi_update(
    render_scene3d_gi *gi,
    const render_scene3d_gi_solid *solids,
    int count,
    render_scene3d_gi_lighting *lighting,
    char *error,
    size_t size
);
/* Binds the probes for the lit pass's next draws, or none while it is off. */
void render_scene3d_gi_bind(render_scene3d_gi *gi);
void render_scene3d_gi_unbind();

#endif
