#ifndef TEST_WHOLE_MULTIGATE_FIXTURE_H
#define TEST_WHOLE_MULTIGATE_FIXTURE_H
#include "graph/model/behavior_asset.h"
enum
{
    WHOLE_MULTIGATE_WAIT = 1,
    WHOLE_MULTIGATE_LOOP = 2,
    WHOLE_MULTIGATE_CALL = 4,
    WHOLE_MULTIGATE_COUNT = 8
};
typedef struct
{
    int calls[1024], count, fail_at;
} whole_multigate_service;
behavior_asset *whole_multigate_fixture(int outputs, int kind, bool effects);
graph *whole_multigate_root(behavior_asset *asset);
void whole_multigate_smoke();
void test_compiled_multigate(const char *library);
void emit_multigate_c(const char *path);
int bench_multigate(const char *path, int count, int rotation);
#endif
