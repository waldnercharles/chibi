#include "render/shader_text_create.h"

#include "render/shader_text_backend.h"

CF_Shader render_shader_text_create(
    const shader_text *text,
    const char *vertex,
    const char *fragment,
    shader_error *error
)
{
    CF_Shader shader = { 0 };
    CF_ShaderCompilerResult vs = render_shader_text_compile(text, vertex, CUTE_SHADER_STAGE_VERTEX, error);
    if (!vs.success) {
        cute_shader_free_result(vs);
        return shader;
    }
    CF_ShaderCompilerResult fs = render_shader_text_compile(text, fragment, CUTE_SHADER_STAGE_FRAGMENT, error);
    if (fs.success) {
        shader = cf_make_shader_from_bytecode(vs.bytecode, fs.bytecode);
    }
    cute_shader_free_result(vs);
    cute_shader_free_result(fs);
    return shader;
}
