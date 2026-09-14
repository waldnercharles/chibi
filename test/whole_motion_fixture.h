#ifndef TEST_WHOLE_MOTION_FIXTURE_H
#define TEST_WHOLE_MOTION_FIXTURE_H
#include "graph/model/behavior_asset.h"
enum
{
    WHOLE_MOTION_SPRING,
    WHOLE_MOTION_ANGLE,
    WHOLE_MOTION_SMOOTH,
    WHOLE_MOTION_SQRT,
    WHOLE_MOTION_FLOOR,
    WHOLE_MOTION_CEIL,
    WHOLE_MOTION_ROUND,
    WHOLE_MOTION_COUNT
};
behavior_asset *whole_motion_fixture(int kind);
void whole_motion_smoke();
#endif
