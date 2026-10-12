#ifndef CHIBI_SHADER_TEXT_BACKEND_H
#define CHIBI_SHADER_TEXT_BACKEND_H
#include "runtime/shaders/source.h"

#include <cute_shader.h>
/* Caller releases the result with cute_shader_free_result. Serial use only:
 * the upstream compiler owns process-global compilation storage. */
CF_ShaderCompilerResult render_shader_text_compile(
    const shader_text *,
    const char *,
    CF_ShaderCompilerStage,
    shader_error *
);
/* Assembled fixed templates used by both validation and runtime creation.
 * The assembled sources use free(); the update vertex is borrowed. */
char *render_shader_particle_vertex_source(const char *adapter, bool stateful);
char *render_shader_particle_update_source(const char *adapter);
const char *render_shader_particle_update_vertex();
#endif
