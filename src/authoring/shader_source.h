#ifndef CHIBI_AUTHORING_SHADER_SOURCE_H
#define CHIBI_AUTHORING_SHADER_SOURCE_H
#include "runtime/shaders/prepared.h"

/* A descriptor lives below content_root and names project-local GLSL files.
 * NULL descriptor_text reads the saved descriptor. Failure preserves result. */
bool authoring_shader_source_load(
    const char *content_root,
    const char *descriptor_path,
    const char *descriptor_text,
    shader_prepared_asset *result,
    char *error,
    size_t size
);
/* Compile the actual particle vertex program without creating a display host. */
bool authoring_shader_source_validate(const shader_prepared_asset *, char *error, size_t size);
/* Validate every descriptor before publishing the collection. Removed source
 * declarations retire only after the complete replacement collection succeeds. */
bool authoring_shader_sources_prepare(const char *content_root, bool publish, char *error, size_t size);
#endif
