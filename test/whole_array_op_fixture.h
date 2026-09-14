#ifndef TEST_WHOLE_ARRAY_OP_FIXTURE_H
#define TEST_WHOLE_ARRAY_OP_FIXTURE_H
#include "graph/model/behavior_asset.h"
#include "support/arena.h"
enum
{
    ARRAY_OP_MAKE,
    ARRAY_OP_GET,
    ARRAY_OP_IDENTICAL,
    ARRAY_OP_ADD,
    ARRAY_OP_UNIQUE,
    ARRAY_OP_APPEND,
    ARRAY_OP_SET,
    ARRAY_OP_INSERT,
    ARRAY_OP_REMOVE_INDEX,
    ARRAY_OP_REMOVE_ITEM,
    ARRAY_OP_CLEAR,
    ARRAY_OP_REVERSE,
    ARRAY_OP_SWAP,
    ARRAY_OP_RESIZE,
    ARRAY_OP_CONTAINS,
    ARRAY_OP_FIND,
    ARRAY_OP_MAKE_SET,
    ARRAY_OP_SET_ADD,
    ARRAY_OP_SET_CONTAINS,
    ARRAY_OP_SET_LENGTH,
    ARRAY_OP_COUNT
};
enum
{
    ARRAY_OP_INTS,
    ARRAY_OP_STRINGS,
    ARRAY_OP_NAMES,
    ARRAY_OP_STRUCTS,
    ARRAY_OP_KINDS
};
behavior_asset *whole_array_op_fixture(int kind, int operation, bool waits);
/* Payload-free Tick appends two elements through a nested call and counts runs. */
behavior_asset *whole_owned_tick_fixture(int kind, bool failure);
void test_owned_ticks();
void whole_array_op_inputs(int kind, pin_value arguments[5], arena *storage);
void whole_array_op_arguments(int kind, int operation, pin_value arguments[5], arena *storage);
void whole_array_op_smoke();
void test_array_ops(const char *path);
void emit_array_ops_c(const char *path);
int bench_array_ops(const char *path, int count, int rotation);
#endif
