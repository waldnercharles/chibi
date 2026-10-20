#ifndef CHIBI_EXECUTION_INTRINSICS_H
#define CHIBI_EXECUTION_INTRINSICS_H
#include "execution/function.h"
#include "execution/host.h"

/* Logical references retain producer contexts and re-resolve entity storage. */
typedef enum
{
    LANGUAGE_REFERENCE_VALUE,
    LANGUAGE_REFERENCE_STATE,
    LANGUAGE_REFERENCE_COMPONENT
} language_reference_kind;
typedef enum
{
    EXECUTION_INTRINSIC_SNAPSHOT = 1,
    EXECUTION_INTRINSIC_CONSTRUCT = 2,
    EXECUTION_INTRINSIC_REFERENCE_MAKE = 3,
    EXECUTION_INTRINSIC_REFERENCE_FIELD = 4,
    EXECUTION_INTRINSIC_REFERENCE_READ = 5,
    EXECUTION_INTRINSIC_REFERENCE_WRITE = 6,
    EXECUTION_INTRINSIC_ACTION_READ = 7,
    EXECUTION_INTRINSIC_ACTION_COMPLETE = 8,
    EXECUTION_INTRINSIC_COMPONENT_HAS = 9,
    EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP = 10,
    EXECUTION_INTRINSIC_BREAK_REQUEST = 11,
    EXECUTION_INTRINSIC_BREAK_TAKE = 12,
    EXECUTION_INTRINSIC_EVENT_READ = 13,
    EXECUTION_INTRINSIC_STATE_DEMAND = 15,
    EXECUTION_INTRINSIC_STATE_TRANSITION = 16,
    EXECUTION_INTRINSIC_RETRIGGER_ACTION = 17,
    EXECUTION_INTRINSIC_LOCAL_RELEASE = 18,
    EXECUTION_INTRINSIC_QUERY_SOURCE = 19,
    EXECUTION_INTRINSIC_QUERY_TARGET = 23,
    EXECUTION_INTRINSIC_REFERENCE_REDUCE = 25,
    EXECUTION_INTRINSIC_UNION_MAKE = 20,
    EXECUTION_INTRINSIC_UNION_TEST = 21,
    EXECUTION_INTRINSIC_UNION_GET = 22,
    EXECUTION_INTRINSIC_VALUE_EQUAL = 26,
    EXECUTION_INTRINSIC_ARRAY_PUSH = 27,
    EXECUTION_INTRINSIC_ARRAY_STORE = 28,
    EXECUTION_INTRINSIC_ARRAY_EXTEND = 29,
    EXECUTION_INTRINSIC_MAP_MAKE = 30,
    EXECUTION_INTRINSIC_MAP_GET = 31,
    EXECUTION_INTRINSIC_MAP_HAS = 32,
    EXECUTION_INTRINSIC_MAP_REMOVE = 33,
    EXECUTION_INTRINSIC_MAP_ITEM = 34,
    EXECUTION_INTRINSIC_REFERENCE_KEY = 35,
    EXECUTION_INTRINSIC_STRING_BYTES = 40,
    EXECUTION_INTRINSIC_STRING_ITEM = 41,
    EXECUTION_INTRINSIC_STRING_NEXT = 42,
    EXECUTION_INTRINSIC_STRING_CURSOR = 43,
} execution_intrinsic;
extern const node_host_call EXECUTION_state_demand_call,
    EXECUTION_state_transition_call, EXECUTION_retrigger_action_call;
extern const node_host_call EXECUTION_snapshot_call;
extern const node_host_call EXECUTION_string_bytes_call, EXECUTION_string_item_call,
    EXECUTION_string_next_call, EXECUTION_string_cursor_call;
extern const node_host_call EXECUTION_construct_call;
extern const node_host_call EXECUTION_value_equal_call;
/* In-place append, element write and extension of a local's own array:
 * (header address, item), (header address, index, item) and (header address,
 * items). Every store into a local snapshots, so no other value shares the
 * array these update. */
extern const node_host_call EXECUTION_array_push_call,
    EXECUTION_array_store_call, EXECUTION_array_extend_call;
extern const node_host_call EXECUTION_union_make_call,
    EXECUTION_union_test_call, EXECUTION_union_get_call;
extern const node_host_call EXECUTION_reference_make_call;
extern const node_host_call EXECUTION_reference_field_call;
extern const node_host_call EXECUTION_reference_key_call;
extern const node_host_call EXECUTION_reference_read_call;
extern const node_host_call EXECUTION_reference_write_call;
extern const node_host_call EXECUTION_reference_reduce_call;
extern const node_host_call EXECUTION_action_read_call;
extern const node_host_call EXECUTION_action_complete_call;
extern const node_host_call EXECUTION_break_request_call, EXECUTION_break_take_call;
extern const node_host_call EXECUTION_event_read_call;
extern const node_host_call EXECUTION_local_release_call;
extern const node_host_call EXECUTION_component_has_call,
    EXECUTION_component_membership_call;
extern const node_host_call EXECUTION_query_source_call;
extern const node_host_call EXECUTION_query_target_call;
extern const node_access EXECUTION_component_read_access,
    EXECUTION_component_membership_access;
extern const node_access EXECUTION_assignment_access, EXECUTION_value_access;
size_t execution_value_size(pin_value);
const node_host_call *execution_intrinsic_call(execution_intrinsic);
uint64_t execution_intrinsic_id(const node_host_call *);
uint64_t execution_intrinsic_abi(uint64_t);
const node_host_call *execution_intrinsic_resolve(uint64_t);
const node_access *execution_intrinsic_access(uint64_t);
pin_value execution_reference_value(arena *, behavior_function_context *, pin_value *, pin_value type);
/* A local that a task keeps by reference. While the declaring frame runs, the
 * box names the frame's slot, so both see each other's writes; once the frame
 * leaves or declares the local again, the box keeps the last value. */
typedef struct
{
    int holders;
    behavior_function_context *owner;
    pin_value *value;
    behavior_function_context detached;
    pin_value kept;
    arena storage;
} execution_box;
/* The frame and slot of a reference to a local, or false for state and
 * components. `box` is the box it already uses, if any. */
bool execution_reference_local(
    pin_value,
    behavior_function_context **owner,
    pin_value **slot,
    execution_box **box
);
/* Copies a reference into `storage` for a task that `owner` runs. A reference
 * to a local reaches it through `box`. */
void execution_reference_capture(
    arena *storage,
    behavior_function_context *owner,
    pin_value *value,
    execution_box *box
);
#endif
