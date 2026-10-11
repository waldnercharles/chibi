#include "render/shader_text.h"

#include "render/particle_sources.h"
#include "render/shader_text_backend.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CF_ShaderCompilerResult render_shader_text_compile(
    const shader_text *text,
    const char *source,
    CF_ShaderCompilerStage stage,
    shader_error *error
)
{
    CF_ShaderCompilerFile files[SHADER_TEXT_INCLUDE_MAX + 1];
    files[0] = (CF_ShaderCompilerFile){ text->path, text->code };
    for (int i = 0; i < acount(text->includes); i++) {
        files[i + 1] = (CF_ShaderCompilerFile){ text->includes[i].path,
                                                text->includes[i].code };
    }
    CF_ShaderCompilerConfig config = { .builtin_includes = files,
                                       .num_builtin_includes = acount(text->includes) + 1 };
    size_t size = strlen(source) + 32;
    char *versioned = xmalloc(size);
    snprintf(versioned, size, "#version 450\n%s", source);
    CF_ShaderCompilerResult result = cute_shader_compile(versioned, stage, config);
    free(versioned);
    if (!result.success && error) {
        error->node = 0;
        snprintf(error->message, sizeof error->message, "%s", result.error_message);
    }
    return result;
}

static char *particle_vertex(const char *adapter)
{
    const char *marker = "// SG_PARTICLE_PROGRAM is replaced by the prepared "
                         "shader function.";
    const char *at = strstr(shader_particle_vertex, marker);
    assert(at);
    size_t prefix = (size_t)(at - shader_particle_vertex);
    size_t size = strlen(shader_particle_vertex) + strlen(adapter) + 1;
    char *source = xmalloc(size);
    memcpy(source, shader_particle_vertex, prefix);
    snprintf(source + prefix, size - prefix, "%s%s", adapter, at + strlen(marker));
    return source;
}

bool render_shader_text_validate(
    const shader_text *text,
    char *const names[32],
    const int widths[32],
    shader_error *error
)
{
    char *adapter = shader_text_particle_glsl(text, names, widths, error);
    if (!adapter) {
        return false;
    }
    char *source = particle_vertex(adapter);
    CF_ShaderCompilerResult vertex = render_shader_text_compile(text, source, CUTE_SHADER_STAGE_VERTEX, error);
    bool ok = vertex.success;
    cute_shader_free_result(vertex);
    free(source);
    free(adapter);
    if (!ok) {
        return false;
    }
    CF_ShaderCompilerResult fragment = render_shader_text_compile(
        text,
        shader_particle_fragment,
        CUTE_SHADER_STAGE_FRAGMENT,
        error
    );
    ok = fragment.success;
    cute_shader_free_result(fragment);
    return ok;
}
