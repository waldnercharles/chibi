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
    EXECUTION_INTRINSIC_LOCAL_RELEASE = 18,
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
    EXECUTION_INTRINSIC_SEQUENCE_SLICE = 51,
    EXECUTION_INTRINSIC_SEQUENCE_VIEW = 52,
    EXECUTION_INTRINSIC_SEQUENCE_CLONE = 53,
    EXECUTION_INTRINSIC_RESERVED_ARRAY_MUTATE = 54,
    EXECUTION_INTRINSIC_SEQUENCE_GET = 55,
    EXECUTION_INTRINSIC_SEQUENCE_LENGTH = 56,
    EXECUTION_INTRINSIC_VALUE_COPY = 57,
    EXECUTION_INTRINSIC_VALUE_DROP = 58,
    EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE = 59,
    EXECUTION_INTRINSIC_REFERENCE_UNION = 60,
    EXECUTION_INTRINSIC_MAP_DELETE_PLACE = 61,
    EXECUTION_INTRINSIC_SEQUENCE_BORROW = 62,
    /* 63-79 and 80-95 are free: the entity, component, query, action and
     * reduction operations that held them left with the ECS. */
} execution_intrinsic;
/* A host capability id with this bit runs its intrinsic in the caller's
 * storage, where a function builds the locals it returns: construct, array
 * push, store and extend, and union_keep. */
#define EXECUTION_INTRINSIC_IN_RESULT UINT64_C(0x100)
static inline execution_intrinsic execution_intrinsic_kind(uint64_t id)
{
    return (execution_intrinsic)(id & 255);
}
/* The capability id of intrinsic `kind`: what execution_intrinsic_id returns
 * for its call, as a constant. */
#define EXECUTION_INTRINSIC_ID(kind)                                           \
    (UINT64_C(0x4348494900000000) | (uint64_t)(kind))
/* Native capability ids are hashes, so only an intrinsic id carries the bit. */
static inline bool execution_intrinsic_in_result(uint64_t id)
{
    return (id & ~(EXECUTION_INTRINSIC_IN_RESULT | UINT64_C(255))) ==
               UINT64_C(0x4348494900000000) &&
           (id & EXECUTION_INTRINSIC_IN_RESULT) != 0;
}
extern const node_host_call EXECUTION_snapshot_call;
extern const node_host_call EXECUTION_value_copy_call,
    EXECUTION_value_drop_call, EXECUTION_array_mutate_place_call,
    EXECUTION_reference_union_call, EXECUTION_map_delete_place_call;
extern const node_host_call EXECUTION_string_bytes_call, EXECUTION_string_item_call,
    EXECUTION_string_next_call, EXECUTION_string_cursor_call;
extern const node_host_call EXECUTION_construct_call;
extern const node_host_call EXECUTION_value_equal_call;
/* Older header-address forms of append, element write and extension. */
/* Strict half-open ranges return a boxed optional: (receiver, start, end,
 * success tag). Clone takes one receiver and returns a detached outer copy. */
extern const node_host_call EXECUTION_sequence_slice_call,
    EXECUTION_sequence_view_call, EXECUTION_sequence_clone_call,
    EXECUTION_sequence_get_call, EXECUTION_sequence_length_call;
/* sequence_get without the element's snapshot: the result stays in the
 * array, for a reader that finishes before the array can change. */
extern const node_host_call EXECUTION_sequence_borrow_call;
typedef enum
{
    EXECUTION_ARRAY_APPEND = 1,
    EXECUTION_ARRAY_ADD_UNIQUE,
    EXECUTION_ARRAY_EXTEND,
    EXECUTION_ARRAY_SET,
    EXECUTION_ARRAY_INSERT,
    EXECUTION_ARRAY_REMOVE_INDEX,
    EXECUTION_ARRAY_REMOVE_ITEM,
    EXECUTION_ARRAY_CLEAR,
    EXECUTION_ARRAY_REVERSE,
    EXECUTION_ARRAY_SWAP,
    EXECUTION_ARRAY_RESIZE
} execution_array_mutation;
/* Place mutation takes a logical reference, an ordinal, and method arguments. */
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
extern const node_host_call EXECUTION_local_release_call;
extern const node_access EXECUTION_assignment_access, EXECUTION_value_access,
    EXECUTION_sequence_read_access;
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
