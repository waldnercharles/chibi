#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "vec3.h"

typedef struct Transform
{
    float scale;
    int flags;
    Vec3 position;
    const char *name;
} Transform;

#endif
