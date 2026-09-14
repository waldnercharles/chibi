#ifndef TEST_WHOLE_MATERIAL_FIXTURE_H
#define TEST_WHOLE_MATERIAL_FIXTURE_H
#include "render/shader_catalog.h"
enum
{
    WHOLE_MATERIAL_KINDS = 8
};
pin_value whole_material_value(int kind, bool nonfinite);
behavior_asset *whole_material_fixture(int kind, bool waits, bool arguments);
int whole_material_publish(int width);
ecs_world *whole_material_world(ecs_entity *entity);
#endif
