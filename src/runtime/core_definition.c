#include "runtime/core_definition.h"

#include "execution/log.h"

#include <stdio.h>
#include <string.h>

#define PORTS(NAME, LIST)                                                      \
    static const native_parameter_definition NAME[] = { LIST(NATIVE_PORT) };
PORTS(EXEC, CORE_EXEC_INPUTS)
PORTS(THEN, CORE_THEN_OUTPUTS)
PORTS(CONDITION, CORE_CONDITION_INPUTS)
PORTS(BRANCH, CORE_BRANCH_OUTPUTS)
PORTS(SEQUENCE, CORE_SEQUENCE_OUTPUTS)
PORTS(RANGE, CORE_RANGE_INPUTS)
PORTS(RANGE_BREAK, CORE_RANGE_BREAK_INPUTS)
PORTS(RANGE_OUT, CORE_RANGE_OUTPUTS)
PORTS(WHILE_OUT, CORE_WHILE_OUTPUTS)
PORTS(EACH, CORE_EACH_INPUTS)
PORTS(EACH_BREAK, CORE_EACH_BREAK_INPUTS)
PORTS(EACH_OUT, CORE_EACH_OUTPUTS)
PORTS(DELAY, CORE_DELAY_INPUTS)
PORTS(DELAY_OUT, CORE_DELAY_OUTPUTS)
PORTS(PRINT, CORE_PRINT_INPUTS)
PORTS(EXPECT, CORE_EXPECT_INPUTS)
PORTS(VALUE, CORE_VARIABLE_DATA)
PORTS(SET, CORE_SET_INPUTS)
PORTS(SET_OUT, CORE_SET_OUTPUTS)
PORTS(SELF, CORE_SELF_OUTPUTS)
PORTS(DELTA, CORE_DELTA_OUTPUTS)
PORTS(COMPONENT_GET, CORE_COMPONENT_GET_INPUTS)
PORTS(COMPONENT_VALUE, CORE_COMPONENT_GET_OUTPUTS)
PORTS(COMPONENT_HAS, CORE_COMPONENT_HAS_OUTPUTS)
PORTS(COMPONENT_SET, CORE_COMPONENT_SET_INPUTS)
PORTS(COMPONENT_SET_OUT, CORE_COMPONENT_SET_OUTPUTS)
PORTS(COMPONENT_CHANGE, CORE_COMPONENT_CHANGE_INPUTS)
PORTS(COMPONENT_CHANGE_OUT, CORE_COMPONENT_CHANGE_OUTPUTS)
static const native_parameter_definition PI[] = {
    CORE_DATA_PORT(NATIVE_PORT, "Pi", PIN_F64, "double")
};
#undef PORTS
#define IN(ARRAY) .inputs = ARRAY, .input_count = NATIVE_COUNT(ARRAY)
#define OUT(ARRAY) .outputs = ARRAY, .output_count = NATIVE_COUNT(ARRAY)
#define ACTION(NAME, SOURCE, COMPLETION)                                       \
    .canonical_name = NAME, .source_name = SOURCE,                             \
    .kind = SCRIPT_CAPABILITY_ACTION, .completion_output = COMPLETION
#define FUNCTION(NAME, SOURCE)                                                 \
    .canonical_name = NAME, .source_name = SOURCE,                             \
    .kind = SCRIPT_CAPABILITY_FUNCTION, .pure = true, .completion_output = -1
static const native_capability_definition DEFINITIONS[] = {
    { ACTION(CORE_BRANCH_NAME, CORE_BRANCH_SOURCE, -1),
      IN(CONDITION),
      OUT(BRANCH),
      .flow = CORE_BRANCH_FLOW(SCRIPT_FLOW_ACCESS_BRANCH) },
    { ACTION(CORE_SEQUENCE_NAME, CORE_SEQUENCE_SOURCE, -1),
      IN(EXEC),
      OUT(SEQUENCE),
      .flow = CORE_SEQUENCE_FLOW(SCRIPT_FLOW_ACCESS_SEQUENCE) },
    { ACTION(CORE_RANGE_NAME, CORE_RANGE_SOURCE, 2),
      IN(RANGE),
      OUT(RANGE_OUT),
      .flow = CORE_RANGE_FLOW(SCRIPT_FLOW_ACCESS_RANGE, -1) },
    { ACTION(CORE_RANGE_BREAK_NAME, CORE_RANGE_BREAK_SOURCE, 2),
      IN(RANGE_BREAK),
      OUT(RANGE_OUT),
      .flow = CORE_RANGE_FLOW(SCRIPT_FLOW_ACCESS_RANGE, 1) },
    { ACTION(CORE_WHILE_NAME, CORE_WHILE_SOURCE, 1),
      IN(CONDITION),
      OUT(WHILE_OUT),
      .flow = CORE_WHILE_FLOW(SCRIPT_FLOW_ACCESS_WHILE) },
    { ACTION(CORE_EACH_NAME, CORE_EACH_SOURCE, 3),
      IN(EACH),
      OUT(EACH_OUT),
      .flow = CORE_EACH_FLOW(SCRIPT_FLOW_ACCESS_EACH, -1) },
    { ACTION(CORE_EACH_BREAK_NAME, CORE_EACH_BREAK_SOURCE, 3),
      IN(EACH_BREAK),
      OUT(EACH_OUT),
      .flow = CORE_EACH_FLOW(SCRIPT_FLOW_ACCESS_EACH, 1) },
    { ACTION(CORE_BREAK_NAME, CORE_BREAK_SOURCE, -1),
      IN(EXEC),
      .flow = CORE_BREAK_FLOW(SCRIPT_FLOW_ACCESS_BREAK) },
    { ACTION(CORE_DELAY_NAME, CORE_DELAY_SOURCE, 0),
      CORE_DELAY_PRESENTATION,
      IN(DELAY),
      OUT(DELAY_OUT),
      .effects = SCRIPT_EFFECT_SUSPEND },
    { ACTION(CORE_PRINT_NAME, CORE_PRINT_SOURCE, 0),
      CORE_PRINT_PRESENTATION,
      IN(PRINT),
      OUT(THEN) },
    /* Not pure: a call whose only effect is failing must not be dropped. */
    { .canonical_name = CORE_EXPECT_NAME,
      .source_name = CORE_EXPECT_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .completion_output = -1,
      CORE_EXPECT_PRESENTATION,
      IN(EXPECT) },
    { FUNCTION(CORE_GET_NAME, CORE_GET_SOURCE), OUT(VALUE), .state_access = SCRIPT_STATE_ACCESS_READ },
    { ACTION(CORE_SET_NAME, CORE_SET_SOURCE, 0),
      IN(SET),
      OUT(SET_OUT),
      .state_access = SCRIPT_STATE_ACCESS_WRITE },
    { FUNCTION(CORE_COMPONENT_GET_NAME, "get_component"),
      CORE_COMPONENT_GET_PRESENTATION,
      IN(COMPONENT_GET),
      OUT(COMPONENT_VALUE),
      .component_access = SCRIPT_COMPONENT_ACCESS_READ,
      .component_entity = 1,
      .component_value = -1 },
    { ACTION(CORE_COMPONENT_SET_NAME, "set_component", 0),
      CORE_COMPONENT_SET_PRESENTATION,
      IN(COMPONENT_SET),
      OUT(COMPONENT_SET_OUT),
      .component_access = SCRIPT_COMPONENT_ACCESS_WRITE,
      .component_entity = 1,
      .component_value = 2 },
    { FUNCTION(CORE_COMPONENT_HAS_NAME, "has_component"),
      CORE_COMPONENT_HAS_PRESENTATION,
      IN(COMPONENT_GET),
      OUT(COMPONENT_HAS),
      .component_access = SCRIPT_COMPONENT_ACCESS_HAS,
      .component_entity = 1,
      .component_value = -1 },
    { ACTION(CORE_COMPONENT_ADD_NAME, "add_component", 0),
      CORE_COMPONENT_ADD_PRESENTATION,
      IN(COMPONENT_CHANGE),
      OUT(COMPONENT_CHANGE_OUT),
      .component_access = SCRIPT_COMPONENT_ACCESS_ADD,
      .component_entity = -1,
      .component_value = -1 },
    { ACTION(CORE_COMPONENT_REMOVE_NAME, "remove_component", 0),
      CORE_COMPONENT_REMOVE_PRESENTATION,
      IN(COMPONENT_CHANGE),
      OUT(COMPONENT_CHANGE_OUT),
      .component_access = SCRIPT_COMPONENT_ACCESS_REMOVE,
      .component_entity = -1,
      .component_value = -1 },
    { FUNCTION(CORE_SELF_NAME, CORE_SELF_SOURCE), CORE_SELF_PRESENTATION, OUT(SELF) },
    { FUNCTION(CORE_DELTA_NAME, CORE_DELTA_SOURCE), OUT(DELTA) },
};
static const node_access PRINT_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
/* A false condition fails the call with the message, which the run's value
 * storage keeps for the failure report. */
static const char *expect_check(
    const behavior_function_context *context,
    void *,
    const pin_value *arguments,
    pin_value *
)
{
    if (arguments[0].b) {
        return NULL;
    }
    const char *message = arguments[1].str ? arguments[1].str : "";
    size_t size = strlen(message) + sizeof "Expectation failed: ";
    char *text = arena_alloc(context->value_storage, size);
    snprintf(text, size, "Expectation failed: %s", message);
    return text;
}
static const node_host_call EXPECT_CALL = { .context = expect_check };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = CORE_DELAY_NAME, .implementation = { .intrinsic = BF_DELAY } },
    { .canonical_name = CORE_PRINT_NAME,
      .implementation = { .call = &EXECUTION_PrintString, .access = &PRINT_ACCESS } },
    { .canonical_name = CORE_EXPECT_NAME,
      .implementation = { .call = &EXPECT_CALL, .access = &PRINT_ACCESS } },
    { .canonical_name = CORE_SELF_NAME, .implementation = { .intrinsic = BF_OWNER } },
    { .canonical_name = CORE_DELTA_NAME, .implementation = { .intrinsic = BF_DELTA } },
};
static const native_global_definition GLOBALS[] = {
    { .canonical_name = "self", .type = SELF },
    { .canonical_name = CORE_DELTA_NAME,
      .source_name = "delta",
      .type = DELTA,
      .identity = NATIVE_GLOBAL_IDENTITY_CAPABILITY,
      .unit = "s" },
    { .canonical_name = "dt", .type = DELTA, .unit = "s" },
    { .canonical_name = "pi", .type = PI },
};
static const native_definition_global_binding GLOBAL_BINDINGS[] = {
    { .canonical_name = "self", .instruction = { .op = BF_OWNER, .type = PIN_HANDLE } },
    { .canonical_name = CORE_DELTA_NAME,
      .instruction = { .op = BF_DELTA, .type = PIN_F32 } },
    { .canonical_name = "dt", .instruction = { .op = BF_DELTA, .type = PIN_F32 } },
    { .canonical_name = "pi",
      .instruction = { .op = BF_CONSTANT,
                       .type = PIN_F64,
                       .value = { .type = PIN_F64,
                                  .d = 3.14159265358979323846264338327950288 } } },
};
const native_definition_pack CORE_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
    .global_definitions = GLOBALS,
    .global_count = NATIVE_COUNT(GLOBALS),
    .global_bindings = GLOBAL_BINDINGS,
    .global_binding_count = NATIVE_COUNT(GLOBAL_BINDINGS),
};
#undef FUNCTION
#undef ACTION
#undef OUT
#undef IN
