#ifndef TEST_WHOLE_COMMAND_FIXTURE_H
#define TEST_WHOLE_COMMAND_FIXTURE_H
#include "graph/model/behavior_asset.h"
#include "support/arena.h"

enum
{
    WHOLE_COMMAND_ARGUMENTS,
    WHOLE_COMMAND_STORES,
    WHOLE_COMMAND_WRAPPED,
    WHOLE_COMMAND_LOOP,
    WHOLE_COMMAND_MIXED,
    WHOLE_COMMAND_PRODUCERS,
    WHOLE_COMMAND_BINDINGS,
    WHOLE_COMMAND_ARRAY,
    WHOLE_COMMAND_LITERAL,
    WHOLE_COMMAND_COUNT
};
typedef struct
{
    arena storage;
    pin_value *values;
    uint64_t checksum;
    int calls, error, fail_at;
    bool record;
} whole_command_capture;
const node_desc *whole_command_node();
behavior_asset *whole_command_fixture(int kind, bool missing_reference, bool bad_input);
void whole_command_clear(whole_command_capture *capture);
void whole_command_smoke();
#endif
