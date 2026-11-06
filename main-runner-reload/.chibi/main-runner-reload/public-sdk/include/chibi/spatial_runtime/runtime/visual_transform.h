#ifndef CHIBI_VISUAL_TRANSFORM_H
#define CHIBI_VISUAL_TRANSFORM_H

#include "runtime/components.h"

/* +X forward, +Y up; gameplay heading followed by local yaw, pitch and roll. */
CF_Quat visual_rotation(float heading, CF_V3 rotation);

#endif
