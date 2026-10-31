#ifndef CHIBI_SHADER_PROGRAM_H
#define CHIBI_SHADER_PROGRAM_H

#include "platform/cute.h"
#include "render/shader_catalog.h"

/* RASTER is the material raster's first batch, a fragment pass into four
 * render targets; RASTER_ACCUMULATE continues from the previous batch's. */
typedef enum
{
    RENDER_PROGRAM_SCENE3D,
    RENDER_PROGRAM_SCENE3D_MESH, /* The lit pass for imported meshes. */
    /* The lit passes while global illumination is on, reading its probes. */
    RENDER_PROGRAM_SCENE3D_GI,
    RENDER_PROGRAM_SCENE3D_MESH_GI,
    /* Masked materials in shadow passes, writing depth where they keep pixels. */
    RENDER_PROGRAM_SCENE3D_SHADOW,
    RENDER_PROGRAM_SCENE3D_MESH_SHADOW,
    RENDER_PROGRAM_FRAGMENT,
    RENDER_PROGRAM_RASTER,
    RENDER_PROGRAM_RASTER_ACCUMULATE,
    RENDER_PROGRAM_RASTER_MESH,
    RENDER_PROGRAM_SDF_MATERIAL,
    RENDER_PROGRAM_SDF_PREVIEW,
    RENDER_PROGRAM_MESH_MATERIAL,
    RENDER_PROGRAM_SKY /* A surface material drawn as the sky. */
} render_program_kind;
typedef struct render_programs render_programs;
/* The fragment samplers a program of `kind` binds for `set`, its ramp table
 * included. GLES3 gives a fragment shader 16 texture units. */
int render_program_samplers(render_program_kind kind, const render_shader_set *set);
render_programs *render_programs_new(render_program_kind kind);
void render_programs_free(render_programs *programs);
unsigned render_shader_compile_count();
/* Retained successful GPU programs, including last-good programs after an
   invalidation fails. Successful compilations are cumulative for this cache. */
int render_programs_count(const render_programs *programs);
unsigned render_programs_successful_compilations(const render_programs *programs);
/* Preflight all programs before submission; failure leaves previous output available. */
void render_programs_set_model_preview(render_programs *programs, int shape, int part);
int render_programs_prepare(render_programs *programs, const render_shader_set *set);
const char *render_programs_error(const render_programs *programs);
void render_programs_bind3d(render_programs *programs, int program);
/* Cute keeps 3D texture bindings for every later mesh until they are rebound.
 * Unbind pops the shader and clears the program's images so they can be freed. */
void render_programs_unbind3d(render_programs *programs, int program);
void render_programs_bind2d(render_programs *programs, int program);
/* Ends a bind2d draw: releases its images so shader_images can destroy them. */
void render_programs_unbind2d(render_programs *programs, int program);
/* Sets the program's images and ramps on material; draw with the returned
 * shader and hrc/pass.vert's pass mesh. */
CF_Shader render_programs_bind_raster(render_programs *programs, int program, CF_Material material);
char *render_shader_source(const char *name);
/* A fragment pass over hrc/pass.vert's triangle, or no shader with an error
 * naming the fragment source. */
CF_Shader render_pass_shader(const char *fragment_name, char *error, size_t size);

#endif
