#ifndef CHIBI_RENDER_SHADER_DIR_H
#define CHIBI_RENDER_SHADER_DIR_H

#include <stdlib.h>

// The environment override lets one binary serve every checkout of identical
// sources; the compiled path names the checkout that built it.
static inline const char *render_shader_dir()
{
    const char *dir = getenv("CHIBI_RENDER_SHADER_DIR");
    return dir ? dir : CHIBI_RENDER_SHADER_DIR;
}

#endif
