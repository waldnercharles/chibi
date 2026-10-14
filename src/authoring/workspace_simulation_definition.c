#include "authoring/workspace_simulation_definition.h"

#include "execution/function.h"
#define P(N, K, T)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = K, .type_name = T }
#define A(N, K, T)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = K, .type_name = T, .container = PIN_CONTAINER_ARRAY }
#define I(N) P(N, PIN_I32, "int")
#define F(N) P(N, PIN_F32, "float")
#define D(N) P(N, PIN_F64, "double")
#define B(N) P(N, PIN_BOOL, "bool")
#define S(N) P(N, PIN_STR, "const char *")
#define E(N) { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
static const native_parameter_definition SAMPLE[] = { E("Exec"),
                                                      I("Session"),
                                                      A("Keys", PIN_STR, "const char *"),
                                                      A("Connected", PIN_BOOL, "bool"),
                                                      A("Buttons", PIN_I32, "int"),
                                                      F("MoveX"),
                                                      F("MoveY"),
                                                      F("AimX"),
                                                      F("AimY"),
                                                      B("AimPoint"),
                                                      B("Firing"),
                                                      F("OriginX"),
                                                      F("OriginY"),
                                                      F("OriginZ"),
                                                      F("DirectionX"),
                                                      F("DirectionY"),
                                                      F("DirectionZ"),
                                                      B("RayActive") };
static const native_parameter_definition CLEAR[] = { E("Exec"), I("Session") };
static const native_parameter_definition TIMELINE[] = { E("Exec"),
                                                        I("Session"),
                                                        S("Operation"),
                                                        I("Seed"),
                                                        S("Timeline") };
static const native_parameter_definition SCALE[] = { E("Exec"),
                                                     I("Session"),
                                                     B("Write"),
                                                     D("Scale") };
static const native_parameter_definition MATERIALS[] = {
    E("Exec"), I("Session"), S("Operation"), F("X"), F("Y"), F("Radius")
};
static const native_parameter_definition UI[] = {
    E("Exec"), I("Session"), S("Operation"), S("Key"),    S("Text"),     F("X"),
    F("Y"),    F("WheelX"),  F("WheelY"),    I("Button"), I("Modifiers")
};
static const native_parameter_definition RESULT[] = {
    E("Then"),      B("Ok"),        B("Admitted"), B("Recording"),
    B("Replaying"), B("Truncated"), B("Pending"),  I("Keys"),
    I("Steps"),     D("Scale"),     S("Timeline"), S("Error"),
    S("Hash"),      I("Captured"),  B("Typing"),   I("EndStep")
};
#define OP(N, SRC, IN)                                                         \
    { .canonical_name = N,                                                     \
      .source_name = SRC,                                                      \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = IN,                                                            \
      .input_count = NATIVE_COUNT(IN),                                         \
      .outputs = RESULT,                                                       \
      .output_count = NATIVE_COUNT(RESULT),                                    \
      .completion_output = 0 }
const native_capability_definition WORKSPACE_SIMULATION_DEFINITIONS[WORKSPACE_SIMULATION_OPERATION_COUNT] = {
    OP("Sample Workspace Simulation", "workspace_simulation_sample", SAMPLE),
    OP("Clear Workspace Simulation Input", "workspace_simulation_clear", CLEAR),
    OP("Workspace Simulation Timeline", "workspace_simulation_timeline", TIMELINE),
    OP("Workspace Simulation Time Scale", "workspace_simulation_time_scale", SCALE),
    OP("Workspace Material Generation", "workspace_simulation_materials", MATERIALS),
    OP("Workspace Target UI Input", "workspace_simulation_ui", UI)
};
static const char *requires_host(
    const behavior_function_context *c,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    (void)c;
    (void)service;
    (void)in;
    (void)out;
    return "Workspace simulation requires an authoring host";
}
static const node_host_call CALL = {
    .service = "workspace_simulation",
    .missing_service = "Workspace simulation requires an authoring host",
    .context = requires_host,
    .optional_service = true
};
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define C(N)                                                                   \
    {                                                                          \
        .canonical_name = N, .implementation = {                               \
            .call = &CALL,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    C("Sample Workspace Simulation"),   C("Clear Workspace Simulation Input"),
    C("Workspace Simulation Timeline"), C("Workspace Simulation Time Scale"),
    C("Workspace Material Generation"), C("Workspace Target UI Input")
};
const native_definition_pack WORKSPACE_SIMULATION_DECLARATION_PACK = {
    .definitions = WORKSPACE_SIMULATION_DEFINITIONS,
    .definition_count = WORKSPACE_SIMULATION_OPERATION_COUNT,
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
