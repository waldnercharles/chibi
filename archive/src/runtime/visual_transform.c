#include "runtime/visual_transform.h"

#include "support/cos_sin.h"

#include <math.h>

CF_Quat visual_rotation(float heading, CF_V3 rotation)
{
    float yaw_half = (-heading - rotation.y) * .5f;
    float pitch_half = rotation.x * .5f;
    float roll_half = rotation.z * .5f;
    cos_sin yaw_trig = cos_sin_compute(yaw_half);
    cos_sin pitch_trig = cos_sin_compute(pitch_half);
    cos_sin roll_trig = cos_sin_compute(roll_half);
    CF_Quat yaw = cf_quat(0, yaw_trig.s, 0, yaw_trig.c);
    CF_Quat pitch = cf_quat(0, 0, pitch_trig.s, pitch_trig.c);
    CF_Quat roll = cf_quat(roll_trig.s, 0, 0, roll_trig.c);
    return cf_mul_q(cf_mul_q(yaw, pitch), roll);
}

/* The NODE_READ declaration in runtime/interface.h generates the graph
 * adapter. This function keeps only the spatial policy and validation. */
const char *transform_visual_offset(
    CF_V2 offset,
    float elevation,
    float heading,
    VisualTransform visual,
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
        visual_rotation(heading, (CF_V3){ visual.pitch, visual.yaw, visual.roll }),
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
