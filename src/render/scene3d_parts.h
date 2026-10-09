#ifndef CHIBI_RENDER_SCENE3D_PARTS_H
#define CHIBI_RENDER_SCENE3D_PARTS_H

#define RENDER_PARTS_PREPARED_VERSION 2

#include "runtime/components.h"

#include <cute_math.h>
#include <cute_math3d.h>
#include <stddef.h>

enum
{
    SCENE3D_PART_LIMIT = 16,
    SCENE3D_PART_MATERIAL_MAX = 128
};

typedef struct
{
    int primitive;
    CF_V3 dimensions; /* Local project units: width, height, depth. */
    CF_V3 position;   /* Local X, elevation Y, gameplay Z. */
    CF_V3 rotation;   /* Pitch, yaw, roll in radians. */
    char material[SCENE3D_PART_MATERIAL_MAX]; /* Empty inherits the entity material. */
} scene3d_part;

typedef struct
{
    scene3d_part parts[SCENE3D_PART_LIMIT];
    int count;
} scene3d_parts;

/* Copies supplied immutable parts; key is an opaque prepared identity. */
bool render_scene3d_parts_publish(const char *, const scene3d_parts *, char *, size_t);
bool render_scene3d_parts_read(const char *, scene3d_parts *, char *, size_t);
void render_scene3d_parts_clear();
/* malloc-owned portable snapshot of the complete resident part table. */
bool render_scene3d_parts_save(unsigned char **, size_t *, char *, size_t);
#endif
