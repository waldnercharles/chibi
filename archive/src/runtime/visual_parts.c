#include "runtime/visual_parts.h"

#include <assert.h>
#include <math.h>

int visual_part_count(int primitive)
{
    assert(primitive >= PRIMITIVE_BOX && primitive < PRIMITIVE_COUNT);
    return primitive == PRIMITIVE_BOX_FRAME ? 12 : 1;
}

visual_part visual_part_at(Geometry3D shape, int index)
{
    assert(index >= 0 && index < visual_part_count(shape.primitive));
    visual_part part = { .primitive = shape.primitive,
                         .dimensions = { shape.width, shape.height, shape.depth } };
    if (shape.primitive != PRIMITIVE_BOX_FRAME) {
        return part;
    }
    const float thickness = .18f;
    const float center = .5f - thickness * .5f, span = 1 - 2 * thickness;
    int corner = index / 3;
    float a = corner & 1 ? center : -center, b = corner & 2 ? center : -center;
    CF_V3 size, offset;
    switch (index % 3) {
        case 0:
            size = (CF_V3){ 1, thickness, thickness };
            offset = (CF_V3){ 0, a, b };
            break;
        case 1:
            size = (CF_V3){ thickness, span, thickness };
            offset = (CF_V3){ a, 0, b };
            break;
        default:
            size = (CF_V3){ thickness, thickness, span };
            offset = (CF_V3){ a, b, 0 };
            break;
    }
    part.primitive = PRIMITIVE_BOX;
    part.center = (CF_V3){ offset.x * shape.width,
                           offset.y * shape.height,
                           offset.z * shape.depth };
    part.dimensions = (CF_V3){ size.x * shape.width,
                               size.y * shape.height,
                               size.z * shape.depth };
    return part;
}

/* The NODE_READ declaration in runtime/interface.h generates the graph
 * adapter. This function keeps the geometry policy and validation. */
const char *visual_primitive_part(
    Geometry3D shape,
    int index,
    int *count,
    int *primitive,
    CF_V2 *center,
    float *elevation,
    float *width,
    float *height,
    float *depth
)
{
    if (shape.primitive < PRIMITIVE_BOX || shape.primitive >= PRIMITIVE_COUNT ||
        !isfinite(shape.width + shape.height + shape.depth) ||
        shape.width <= 0 || shape.height <= 0 || shape.depth <= 0) {
        return "Primitive Part needs a valid primitive and positive finite "
               "dimensions";
    }
    *count = visual_part_count(shape.primitive);
    if (index < 0 || index >= *count) {
        return "Primitive Part index is outside the primitive's part count";
    }
    visual_part part = visual_part_at(shape, index);
    *primitive = part.primitive;
    *center = (CF_V2){ part.center.x, part.center.z };
    *elevation = part.center.y;
    *width = part.dimensions.x;
    *height = part.dimensions.y;
    *depth = part.dimensions.z;
    return NULL;
}
