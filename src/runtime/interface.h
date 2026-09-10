#ifndef CHIBI_GAME_INTERFACE_H
#define CHIBI_GAME_INTERFACE_H

#include "graph/model/annotate.h"
#include "runtime/components.h"

NODE_EXEC void game_component_probe(void) NODE_READS(GameHealth) NODE_ENTITY_SCOPED;
NODE_FN NODE_JIT(
    "v2-move"
) GameTransform game_move_transform(GameTransform transform, float x, float y);
NODE_FN NODE_CONVERTS(GameTransform, CF_V2)
CF_V2 game_transform_to_cf(GameTransform transform);
NODE_FN NODE_CONVERTS(CF_V2, GameTransform)
GameTransform game_transform_from_cf(CF_V2 value);

NODE_READ NODE_NAME("Transform Visual Offset") NODE_CATEGORY(
    "Math/Vector"
) NODE_KEYWORDS("local world rotate scale spatial visual offset") const
    char *game_transform_visual_offset(
        NODE_PIN_NAME("Offset") CF_V2 offset,
        NODE_PIN_NAME("Elevation") float elevation,
        NODE_PIN_NAME("Heading") float heading,
        NODE_PIN_NAME("Visual") GameVisualTransform visual,
        NODE_PIN_NAME("Offset") NODE_OUT CF_V2 *result_offset,
        NODE_PIN_NAME("Elevation") NODE_OUT float *result_elevation
    );

NODE_READ NODE_NAME("Primitive Part") NODE_CATEGORY(
    "Visual/Geometry"
) NODE_KEYWORDS("mesh piece frame breakup shatter local dimensions") const
    char *game_visual_primitive_part(
        NODE_PIN_NAME("Appearance") GameGeometry3D shape,
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
