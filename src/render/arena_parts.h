#ifndef CHIBI_RENDER_ARENA_PARTS_H
#define CHIBI_RENDER_ARENA_PARTS_H

#include "platform/cute.h"
#include "runtime/components.h"

#include <stddef.h>

enum
{
    ARENA_PART_LIMIT = 16,
    ARENA_PART_MATERIAL_MAX = 128
};

typedef struct
{
    int primitive;
    CF_V3 dimensions; /* Local project units: width, height, depth. */
    CF_V3 position;   /* Local X, elevation Y, gameplay Z. */
    CF_V3 rotation;   /* Pitch, yaw, roll in radians. */
    char material[ARENA_PART_MATERIAL_MAX]; /* Empty inherits the entity material. */
} arena_part;

typedef struct
{
    arena_part parts[ARENA_PART_LIMIT];
    int count;
} arena_parts;

/* JSON array of {primitive,size:[x,y,z],position:[x,y,z],rotation:[pitch,yaw,roll],material}.
 * Empty text means no override. Dimensions are required and positive; transforms default zero. */
bool render_arena_parts_parse(const char *json, arena_parts *parts, char *error, size_t size);
/* malloc-owned canonical JSON, or NULL for invalid parts. */
char *render_arena_parts_write(const arena_parts *parts);

#endif
