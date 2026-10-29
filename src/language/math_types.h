#ifndef CHIBI_LANGUAGE_MATH_TYPES_H
#define CHIBI_LANGUAGE_MATH_TYPES_H

#include "capabilities/catalog.h"
#include "support/arena.h"

/* Source math categories, never runtime storage or prepared-format ordinals. */
typedef enum
{
    SCRIPT_MATH_NONE,
    SCRIPT_MATH_SCALAR,
    SCRIPT_MATH_VEC2,
    SCRIPT_MATH_VEC3,
    SCRIPT_MATH_VEC4,
    SCRIPT_MATH_QUAT,
    SCRIPT_MATH_MAT2,
    SCRIPT_MATH_MAT3,
    SCRIPT_MATH_MAT4,
} script_math_kind;

const char *script_math_name(script_math_kind);
script_math_kind script_math_named(const char *);
bool script_math_is_vector(script_math_kind);
bool script_math_is_matrix(script_math_kind);
int script_math_width(script_math_kind);
int script_math_components(script_math_kind);
script_math_kind script_math_of(const script_catalog *, script_type);
/* Reserved identity plus exact compact f32 fields, offsets, size and hash.
 * A native catalog may leave its layout hash zero until linking fills it. */
bool script_math_layout_valid(const script_catalog_type *);
bool script_math_layout_equivalent(const script_catalog_type *, const script_catalog_type *);
/* Owned descriptor for a missing compiler builtin. Matrix fields are flat,
 * column-first c0r0/c0r1..., so no native nested layout is needed. */
script_catalog_type script_math_type(arena *, script_math_kind);

#endif
