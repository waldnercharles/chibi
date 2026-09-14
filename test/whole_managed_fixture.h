#ifndef TEST_WHOLE_MANAGED_FIXTURE_H
#define TEST_WHOLE_MANAGED_FIXTURE_H
#include "ecs/ecs.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
typedef struct
{
    const char *text;
    float weight;
} whole_managed_inner;
typedef struct
{
    const char *label;
    whole_managed_inner nested;
    int count;
} whole_managed;
extern const registry_pack WHOLE_MANAGED_PACK;
extern const ecs_component_desc WHOLE_MANAGED_COMPONENT;
behavior_asset *whole_managed_fixture(bool waits, bool components);
void whole_managed_smoke();
void test_compiled_managed(const char *path);
void emit_managed_c(const char *path);
int bench_managed(const char *path, int count, int rotation);
#endif
