#ifndef TEST_FLOW_SNAPSHOT_H
#define TEST_FLOW_SNAPSHOT_H
#include "graph/runtime/instance.h"
#include "support/arena.h"
typedef struct
{
    uint64_t node;
    int pin;
    bool output;
    pin_value value;
} test_flow_watch;
typedef struct
{
    int state, result;
    unsigned long steps;
    double wake;
    uint64_t node;
    flow_call_frame *stack;
    test_flow_watch *watches;
    flow_traversal *traversals;
    pin_value *variables;
    pin_value *leaf_inputs;
    flow_call_frame *leaf_stack;
    flow_traversal *leaf_traversals;
    int leaf_depth, leaf_state;
    unsigned long leaf_steps;
    bool leaf_root;
    bool host_wait, leaf_host_wait;
    flow_error error;
    arena storage;
} test_flow_snapshot;
test_flow_snapshot test_flow_snapshot_take(flow_run *run, const behavior_instance *instance, int result);
void test_flow_snapshot_free(test_flow_snapshot *snapshot);
void test_flow_snapshot_same(test_flow_snapshot actual, test_flow_snapshot expected);
#endif
