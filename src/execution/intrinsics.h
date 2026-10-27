#ifndef CHIBI_EXECUTION_INTRINSICS_H
#define CHIBI_EXECUTION_INTRINSICS_H
#include "execution/function.h"
#include "execution/host.h"

/* Logical references retain producer contexts and re-resolve entity storage. */
typedef enum
{
    LANGUAGE_REFERENCE_VALUE,
    LANGUAGE_REFERENCE_STATE,
    LANGUAGE_REFERENCE_COMPONENT,
    LANGUAGE_REFERENCE_MANAGED
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
    EXECUTION_INTRINSIC_MAP_STORE = 36,
    EXECUTION_INTRINSIC_MAP_DELETE = 37,
    EXECUTION_INTRINSIC_STRING_BYTES = 40,
    EXECUTION_INTRINSIC_STRING_ITEM = 41,
    EXECUTION_INTRINSIC_STRING_NEXT = 42,
    EXECUTION_INTRINSIC_STRING_CURSOR = 43,
    EXECUTION_INTRINSIC_UNION_KEEP = 45,
    EXECUTION_INTRINSIC_POINTER_MAKE = 46,
    EXECUTION_INTRINSIC_POINTER_READ = 47,
    EXECUTION_INTRINSIC_POINTER_WRITE = 48,
    EXECUTION_INTRINSIC_POINTER_FIELD = 49,
    EXECUTION_INTRINSIC_POINTER_REFERENCE = 50,
} execution_intrinsic;
/* A host capability id with this bit runs its intrinsic in the caller's
 * storage, where a function builds the locals it returns: construct, array
 * push, store and extend, and union_keep. */
#define EXECUTION_INTRINSIC_IN_RESULT UINT64_C(0x100)
static inline execution_intrinsic execution_intrinsic_kind(uint64_t id)
{
    return (execution_intrinsic)(id & 255);
}
/* Native capability ids are hashes, so only an intrinsic id carries the bit. */
static inline bool execution_intrinsic_in_result(uint64_t id)
{
    return (id & ~(EXECUTION_INTRINSIC_IN_RESULT | UINT64_C(255))) ==
               UINT64_C(0x4348494900000000) &&
           (id & EXECUTION_INTRINSIC_IN_RESULT) != 0;
}
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
/* union_make for a returned union, which a function builds in its caller's
 * storage: (tag, keep mask, payload). It keeps each payload value the mask
 * names, the locals built there, and copies the others, so nothing else it
 * holds is the caller's or the function's own. */
extern const node_host_call EXECUTION_union_keep_call;
extern const node_host_call EXECUTION_pointer_make_call,
    EXECUTION_pointer_read_call, EXECUTION_pointer_write_call,
    EXECUTION_pointer_field_call, EXECUTION_pointer_reference_call;
extern const node_host_call EXECUTION_reference_make_call;
extern const node_host_call EXECUTION_reference_field_call;
extern const node_host_call EXECUTION_reference_key_call;
extern const node_host_call EXECUTION_reference_read_call;
extern const node_host_call EXECUTION_reference_write_call;
extern const node_host_call EXECUTION_reference_reduce_call;
extern const node_host_call EXECUTION_action_read_call;
extern const node_host_call EXECUTION_action_complete_call;
extern const node_host_call EXECUTION_break_request_call, EXECUTION_break_take_call;
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
/* Only pass a value whose verified kind is BEHAVIOR_REFERENCE. */
bool execution_reference_managed_pointer(pin_value, pin_value *);
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
