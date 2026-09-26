#ifndef CHIBI_SHADER_PROGRAM_H
#define CHIBI_SHADER_PROGRAM_H

#include "platform/cute.h"
#include "render/shader_catalog.h"

typedef enum
{
    RENDER_PROGRAM_ARENA,
    RENDER_PROGRAM_FRAGMENT,
    RENDER_PROGRAM_COMPUTE
} render_program_kind;
typedef struct render_programs render_programs;
render_programs *render_programs_new(render_program_kind kind);
void render_programs_free(render_programs *programs);
unsigned render_shader_compile_count();
/* Preflight all programs before submission; failure leaves previous output available. */
int render_programs_prepare(render_programs *programs, const render_shader_set *set);
const char *render_programs_error(const render_programs *programs);
void render_programs_bind3d(render_programs *programs, int program);
/* Cute keeps 3D texture bindings for every later mesh until they are rebound.
 * Unbind pops the shader and clears the program's images so they can be freed. */
void render_programs_unbind3d(render_programs *programs, int program);
void render_programs_bind2d(render_programs *programs, int program);
CF_ComputeShader render_programs_bind_compute(render_programs *programs, int program, CF_Material material);
char *render_shader_source(const char *name);

#endif
