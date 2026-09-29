#ifndef CHIBI_RENDER_POST_2D_H
#define CHIBI_RENDER_POST_2D_H

#include "platform/cute.h"
#include "render/view_2d.h"
#include "runtime/components.h"

#include <stdbool.h>

/* HDR presentation for the 2D scene: screen distortion, energy-conserving
 * bloom, exposure, AgX tone mapping and grading from a linear RGBA16F frame
 * into a display canvas. */
typedef struct render_post_2d render_post_2d;

enum
{
    RENDER_POST_2D_WAVES = 16,
    RENDER_POST_2D_HAZES = 16
};

/* Screen-space displacement of the linear frame. Offsets are output pixels. */
typedef struct
{
    /* The camera rectangle in Y-up world units, and where it lies in the
     * material textures. */
    render_view_2d view;
    /* Material normal (solid tilt) and cover (G refraction, B heat haze). */
    CF_Texture normal, cover;
    float time;
    /* Output pixels of shimmer per unit of the cover texture's heat haze. */
    float fluid_haze;
    /* Rings: center x, y, current radius and thickness in world units, then
     * strength in output pixels and three unused values. */
    float waves[RENDER_POST_2D_WAVES][8];
    int wave_count;
    /* Heat haze sources: center x, y and radius in world units, strength. */
    float hazes[RENDER_POST_2D_HAZES][4];
    int haze_count;
    /* Whether refractive solids may be on screen. */
    bool refraction;
} render_post_2d_distortion;

typedef struct
{
    /* Exposure in EV; bloom in [0, 1] mixes the blurred image in. */
    float exposure, bloom, dither_phase;
    GamePost2D look;
    /* Display RGB and weight of a full-screen flash. */
    float flash[4];
} render_post_2d_settings;

render_post_2d *render_post_2d_new();
void render_post_2d_free(render_post_2d *post);
/* Allocates the linear target and bloom levels; false when RGBA16F targets
 * are unavailable. */
bool render_post_2d_resize(render_post_2d *post, int width, int height);
/* The linear frame; passes that rewrite it draw into the spare and flip. */
CF_Canvas render_post_2d_target(const render_post_2d *post);
/* A second linear canvas the size of the target; NULL id when it cannot be
 * allocated. */
CF_Canvas render_post_2d_spare(render_post_2d *post);
/* Makes the spare the target after a pass rendered the frame into it. */
void render_post_2d_flip(render_post_2d *post);
/* Whether the distortion displaces anything. */
bool render_post_2d_distorts(const render_post_2d_distortion *distortion);
/* Displaces the target through the spare; false when the spare is missing. */
bool render_post_2d_distort(render_post_2d *post, const render_post_2d_distortion *distortion);
/* The target must hold the finished linear frame. */
void render_post_2d_present(render_post_2d *post, CF_Canvas output, const render_post_2d_settings *settings);

#endif
