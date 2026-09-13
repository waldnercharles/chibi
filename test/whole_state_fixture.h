#ifndef TEST_WHOLE_STATE_FIXTURE_H
#define TEST_WHOLE_STATE_FIXTURE_H
#include "graph/model/behavior_asset.h"
enum
{
    WHOLE_STATE_WAIT = 4,
    WHOLE_STATE_LOOP = 8,
    WHOLE_STATE_CALL = 16,
    WHOLE_STATE_COUNT = 32
};
typedef struct
{
    int calls[2], fail_at[2];
} whole_state_service;
behavior_asset *whole_state_fixture(int kind, bool effects);
graph *whole_state_root(behavior_asset *asset);
void whole_state_smoke();
void test_compiled_states(const char *library);
void emit_state_c(const char *path);
int bench_state(const char *path, int count, int rotation);
#endif
