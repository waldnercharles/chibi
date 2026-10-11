#ifndef CHIBI_SHADER_SOURCE_H
#define CHIBI_SHADER_SOURCE_H

#include "runtime/shaders/program.h"

#define SHADER_TEXT_INCLUDE_MAX 32
#define SHADER_TEXT_BYTES_MAX (4u * 1024u * 1024u)

typedef enum
{
    SHADER_ASSET_GRAPH,
    SHADER_ASSET_PARTICLE_TEXT
} shader_asset_kind;

typedef struct
{
    char *path, *code;
} shader_text_include;

/* Owning source snapshot. Include paths are literal GLSL include names; all
 * source needed by a prepared asset travels with it, without filesystem reads. */
typedef struct
{
    char *path, *entry, *code;
    shader_text_include *includes;
} shader_text;

void shader_text_free(shader_text *);
shader_text shader_text_clone(const shader_text *);
bool shader_text_validate(
    const shader_text *,
    char *const names[SHADER_PARAMETER_MAX],
    const int widths[SHADER_PARAMETER_MAX],
    shader_error *
);
/* GLSL adapter includes the named authored source. Callers pass that source and
 * its include records to their compiler. The returned string uses free(). */
char *shader_text_particle_glsl(
    const shader_text *,
    char *const names[SHADER_PARAMETER_MAX],
    const int widths[SHADER_PARAMETER_MAX],
    shader_error *
);

#endif
