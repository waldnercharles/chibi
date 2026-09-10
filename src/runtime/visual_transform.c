#include "runtime/visual_transform.h"

#include <math.h>

CF_Quat game_visual_rotation(float heading, CF_V3 rotation)
{
    CF_Quat
        yaw = cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, -heading - rotation.y);
    CF_Quat pitch = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, rotation.x);
    CF_Quat roll = cf_quat_from_axis_angle((CF_V3){ 1, 0, 0 }, rotation.z);
    return cf_mul_q(cf_mul_q(yaw, pitch), roll);
}

/* The NODE_READ declaration in runtime/interface.h generates the graph
 * adapter. This function keeps only the spatial policy and validation. */
const char *game_transform_visual_offset(
    CF_V2 offset,
    float elevation,
    float heading,
    GameVisualTransform visual,
    CF_V2 *result_offset,
    float *result_elevation
)
{
    if (!isfinite(
            offset.x + offset.y + elevation + heading + visual.pitch +
            visual.yaw + visual.roll + visual.scale
        ) ||
        visual.scale < 0) {
        return "Transform Visual Offset needs finite values and nonnegative "
               "scale";
    }
    CF_M4x4 frame = cf_m4_from_trs(
        (CF_V3){ 0 },
        game_visual_rotation(heading, (CF_V3){ visual.pitch, visual.yaw, visual.roll }),
        (CF_V3){ visual.scale, visual.scale, visual.scale }
    );
    CF_V3 transformed = cf_m4_transform_point(
        frame,
        (CF_V3){ offset.x, elevation, offset.y }
    );
    *result_offset = (CF_V2){ transformed.x, transformed.z };
    *result_elevation = transformed.y;
    return NULL;
}
