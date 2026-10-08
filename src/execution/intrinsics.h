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
    EXECUTION_INTRINSIC_LOCAL_GROUP_STORE = 14,
    EXECUTION_INTRINSIC_STATE_DEMAND = 15,
    EXECUTION_INTRINSIC_STATE_TRANSITION = 16,
    EXECUTION_INTRINSIC_RETRIGGER_ACTION = 17,
} execution_intrinsic;
extern const node_host_call EXECUTION_state_demand_call,
    EXECUTION_state_transition_call, EXECUTION_retrigger_action_call;
extern const node_host_call EXECUTION_snapshot_call;
extern const node_host_call EXECUTION_construct_call;
extern const node_host_call EXECUTION_reference_make_call;
extern const node_host_call EXECUTION_reference_field_call;
extern const node_host_call EXECUTION_reference_read_call;
extern const node_host_call EXECUTION_reference_write_call;
extern const node_host_call EXECUTION_action_read_call;
extern const node_host_call EXECUTION_action_complete_call;
extern const node_host_call EXECUTION_break_request_call, EXECUTION_break_take_call;
extern const node_host_call EXECUTION_event_read_call;
extern const node_host_call EXECUTION_local_group_store_call;
extern const node_host_call EXECUTION_component_has_call,
    EXECUTION_component_membership_call;
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
#endif
