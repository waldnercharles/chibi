#ifndef CHIBI_INTERFACE_H
#define CHIBI_INTERFACE_H

#include "runtime/components.h"
#include "types/annotate.h"

NODE_FN NODE_JIT("v2-move") NODE_DESCRIPTION(
    "Returns transform moved by x and y."
) Transform move_transform(Transform transform, float x, float y);
NODE_FN NODE_JIT("identity") NODE_CONVERTS(Transform, CF_V2) NODE_DESCRIPTION(
    "Converts a Transform to its vec2 position."
) CF_V2 transform_to_cf(Transform transform);
NODE_FN NODE_JIT("identity") NODE_CONVERTS(CF_V2, Transform) NODE_DESCRIPTION(
    "Converts a vec2 position to a Transform."
) Transform transform_from_cf(CF_V2 value);

NODE_READ NODE_NAME("Transform Visual Offset") NODE_CATEGORY("Math/Vector") NODE_KEYWORDS(
    "local world rotate scale spatial visual offset"
) NODE_DESCRIPTION("Rotates and scales a local Offset and Elevation by Heading and Visual, like the drawn model.") const
    char *transform_visual_offset(
        NODE_PIN_NAME("Offset") CF_V2 offset,
        NODE_PIN_NAME("Elevation") float elevation,
        NODE_PIN_NAME("Heading") float heading,
        NODE_PIN_NAME("Visual") VisualTransform visual,
        NODE_PIN_NAME("Offset") NODE_OUT CF_V2 *result_offset,
        NODE_PIN_NAME("Elevation") NODE_OUT float *result_elevation
    );

NODE_READ NODE_NAME("Primitive Part") NODE_CATEGORY("Visual/Geometry") NODE_KEYWORDS(
    "mesh piece frame breakup shatter local dimensions"
) NODE_DESCRIPTION("Returns part Index of a 3D primitive: its local Center, Elevation and size, and the part Count.") const
    char *visual_primitive_part(
        NODE_PIN_NAME("Appearance") Geometry3D shape,
        NODE_PIN_NAME("Index") int index,
        NODE_PIN_NAME("Count") NODE_OUT int *count,
        NODE_PIN_NAME("Primitive") NODE_OUT int *primitive,
        NODE_PIN_NAME("Center") NODE_OUT CF_V2 *center,
        NODE_PIN_NAME("Elevation") NODE_OUT float *elevation,
        NODE_PIN_NAME("Width") NODE_OUT float *width,
        NODE_PIN_NAME("Height") NODE_OUT float *height,
        NODE_PIN_NAME("Depth") NODE_OUT float *depth
    );

#endif
