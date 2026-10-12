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

static char *particle_source(const char *fixed, const char *adapter, bool stateful)
{
    const char *marker = "// SG_PARTICLE_PROGRAM is replaced by the prepared "
                         "shader function.";
    const char *at = strstr(fixed, marker);
    assert(at);
    const char *define = stateful ? "#define SG_PARTICLE_STATEFUL 1\n" : "";
    size_t prefix = (size_t)(at - fixed), definitions = strlen(define);
    size_t size = strlen(fixed) + strlen(adapter) + definitions + 1;
    char *source = xmalloc(size);
    memcpy(source, define, definitions);
    memcpy(source + definitions, fixed, prefix);
    prefix += definitions;
    snprintf(source + prefix, size - prefix, "%s%s", adapter, at + strlen(marker));
    return source;
}

char *render_shader_particle_vertex_source(const char *adapter, bool stateful)
{
    return particle_source(shader_particle_vertex, adapter, stateful);
}

char *render_shader_particle_update_source(const char *adapter)
{
    return particle_source(shader_particle_update_fragment, adapter, false);
}

const char *render_shader_particle_update_vertex()
{
    return shader_particle_update_vertex;
}

static bool compile_stage(
    const shader_text *text,
    const char *source,
    CF_ShaderCompilerStage stage,
    shader_error *error
)
{
    CF_ShaderCompilerResult result = render_shader_text_compile(text, source, stage, error);
    bool ok = result.success;
    cute_shader_free_result(result);
    return ok;
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
    char *source = render_shader_particle_vertex_source(adapter, text->update != NULL);
    bool ok = compile_stage(text, source, CUTE_SHADER_STAGE_VERTEX, error);
    free(source);
    free(adapter);
    if (!ok) {
        return false;
    }
    if (!compile_stage(text, shader_particle_fragment, CUTE_SHADER_STAGE_FRAGMENT, error)) {
        return false;
    }
    if (!text->update) {
        return true;
    }
    adapter = shader_text_particle_update_glsl(text, names, widths, error);
    if (!adapter) {
        return false;
    }
    source = render_shader_particle_update_source(adapter);
    ok = compile_stage(text, shader_particle_update_vertex, CUTE_SHADER_STAGE_VERTEX, error) &&
         compile_stage(text, source, CUTE_SHADER_STAGE_FRAGMENT, error);
    free(source);
    free(adapter);
    return ok;
}
