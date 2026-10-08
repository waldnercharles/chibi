#ifndef CHIBI_RENDER_ARENA_PARTS_H
#define CHIBI_RENDER_ARENA_PARTS_H

#define RENDER_PARTS_PREPARED_VERSION 1

#include "runtime/components.h"

#include <cute_math.h>
#include <cute_math3d.h>
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

/* Copies supplied immutable parts; key is an opaque prepared identity. */
bool render_arena_parts_publish(const char *, const arena_parts *, char *, size_t);
bool render_arena_parts_read(const char *, arena_parts *, char *, size_t);
void render_arena_parts_clear();
/* malloc-owned portable snapshot of the complete resident part table. */
bool render_arena_parts_save(unsigned char **, size_t *, char *, size_t);
#endif
