#include "capabilities/catalog.h"

/* These contracts describe existing engine implementations, not host claims.
 * Delay can lower to a command or suspend the current activation. Its live
 * values survive suspension; no allocation/copy bound is promised here. */
#define CONTRACT(NAME, INSTRUCTION, VERSION, EFFECTS, GUARANTEES)              \
    static const script_operation_contract NAME = { #NAME,                     \
                                                    #INSTRUCTION,              \
                                                    VERSION,                   \
                                                    EFFECTS,                   \
                                                    GUARANTEES };
SCRIPT_ENGINE_OPERATIONS(CONTRACT)
#undef CONTRACT

const script_operation_contract *script_operation_describe(script_operation operation)
{
    switch (operation) {
#define DESCRIBE(NAME, INSTRUCTION, VERSION, EFFECTS, GUARANTEES)              \
    case SCRIPT_OPERATION_##NAME: return &NAME;
        SCRIPT_ENGINE_OPERATIONS(DESCRIBE)
#undef DESCRIBE
        default: return NULL;
    }
}
static bool scalar(const script_catalog_parameter *parameter, pin_type storage, uint64_t nominal)
{
    return parameter && parameter->type.storage == storage &&
           parameter->type.container == PIN_CONTAINER_SINGLE &&
           parameter->type.nominal == nominal && !parameter->type.parameter &&
           !parameter->type.flags &&
           parameter->reference_kind == SCRIPT_REFERENCE_NONE && !parameter->member;
}
bool script_catalog_operation_valid(const script_catalog_capability *capability)
{
    if (capability->operation == SCRIPT_OPERATION_UNKNOWN) {
        return true;
    }
    const script_operation_contract *contract = script_operation_describe(
        capability->operation
    );
    if (!contract || capability->effects != contract->effects ||
        capability->state_access != SCRIPT_STATE_ACCESS_NONE ||
        capability->state_kind != NATIVE_STATE_NONE ||
        capability->component_access != SCRIPT_COMPONENT_ACCESS_NONE ||
        capability->struct_access != SCRIPT_STRUCT_ACCESS_NONE ||
        capability->flow.kind != SCRIPT_FLOW_ACCESS_NONE || capability->receiver ||
        capability->variadic || capability->variadic_optional ||
        capability->numeric_aggregate || capability->event_sender) {
        return false;
    }
    if (capability->operation == SCRIPT_OPERATION_DELAY) {
        return capability->kind == SCRIPT_CAPABILITY_ACTION &&
               !capability->pure && capability->parameter_count == 1 &&
               scalar(capability->parameters, PIN_F64, 0) &&
               capability->result_count == 0 && capability->entry_count == 1 &&
               capability->outcome_count == 1 && capability->completion_outcome == 0;
    }
    uint64_t nominal = capability->operation == SCRIPT_OPERATION_OWNER
                         ? script_catalog_id("type", "ecs.entity")
                         : 0;
    pin_type storage = capability->operation == SCRIPT_OPERATION_OWNER ? PIN_HANDLE
                                                                       : PIN_F32;
    return capability->kind == SCRIPT_CAPABILITY_FUNCTION && capability->pure &&
           capability->parameter_count == 0 && capability->result_count == 1 &&
           scalar(capability->results, storage, nominal) &&
           capability->entry_count == 0 && capability->outcome_count == 0 &&
           capability->completion_outcome == -1;
}
