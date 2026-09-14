#ifndef TEST_WHOLE_ASYNC_FIXTURE_H
#define TEST_WHOLE_ASYNC_FIXTURE_H
#include "graph/model/behavior_asset.h"
#include "graph/runtime/flow.h"
typedef struct
{
    flow_host_request **requests;
    char **names;
    int *arguments;
    int calls, fail_at;
    bool immediate;
} whole_async_capture;
behavior_asset *whole_async_fixture();
node *whole_async_node(graph *graph);
void whole_async_clear(whole_async_capture *capture);
void whole_async_smoke();
#endif
