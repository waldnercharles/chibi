#ifndef CHIBI_RENDER_SHADER_DIR_H
#define CHIBI_RENDER_SHADER_DIR_H

#include "support/resources.h"

#include <stdlib.h>

// The shader-only override supports development and capture tooling.
static inline const char *render_shader_dir()
{
    const char *dir = getenv("CHIBI_RENDER_SHADER_DIR");
    return dir && dir[0] ? dir : resource_shaders();
}

#endif
