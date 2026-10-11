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
#endif
