#ifndef CHIBI_AUTHORING_MATERIAL_CREATE_H
#define CHIBI_AUTHORING_MATERIAL_CREATE_H
#include "assets/standard_material.h"

#include <stddef.h>
typedef enum
{
    MATERIAL_AUTHORING_MATTE,
    MATERIAL_AUTHORING_METAL,
    MATERIAL_AUTHORING_EMISSIVE,
    MATERIAL_AUTHORING_UNLIT,
    MATERIAL_AUTHORING_PRESET_COUNT,
} material_authoring_preset;
standard_material material_authoring_defaults(material_authoring_preset);
/* Validates factors and compiles before returning owned canonical editable KDL.
 * Color and emission values use linear RGB. No files are written. */
char *material_authoring_source(const char *name, const standard_material *, shader_error *);
/* Validate before writing; refuse existing files and declarations sharing the
 * renderer namespace (materials, shapes and models). Declaration scans refuse
 * symlinks/nonregular files, files above 16 MiB, and totals above 64 MiB or
 * 4096 directory entries. Returns owned path. */
char *material_authoring_create(
    const char *content,
    const char *name,
    const standard_material *,
    char *error,
    size_t size
);
#endif
