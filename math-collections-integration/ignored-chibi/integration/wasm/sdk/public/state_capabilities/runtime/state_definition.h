#ifndef CHIBI_NATIVE_STATE_DEFINITION_H
#define CHIBI_NATIVE_STATE_DEFINITION_H

#include "runtime/native_definitions.h"

#define NATIVE_STATE_ONCE_NAME "DoOnce"
#define NATIVE_STATE_ONCE_SOURCE "do_once"
#define NATIVE_STATE_COUNT_NAME "DoN"
#define NATIVE_STATE_COUNT_SOURCE "do_n"
#define NATIVE_STATE_FLIP_NAME "FlipFlop"
#define NATIVE_STATE_FLIP_SOURCE "flip_flop"
#define NATIVE_STATE_GATE_NAME "Gate"
#define NATIVE_STATE_GATE_SOURCE "gate"
#define NATIVE_STATE_MULTI_NAME "MultiGate"
#define NATIVE_STATE_MULTI_SOURCE "multi_gate"
#define NATIVE_STATE_RETRIGGER_NAME "Retriggerable Delay"
#define NATIVE_STATE_RETRIGGER_SOURCE "retriggerable_delay"

#define NATIVE_STATE_ONCE_INPUTS(X)                                            \
    X("Exec",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Reset",                                                                 \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Start Closed",                                                          \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      "bool",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL }))

#define NATIVE_STATE_ONCE_OUTPUTS(X)                                           \
    X("Completed",                                                             \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))

#define NATIVE_STATE_COUNT_INPUTS(X)                                           \
    X("Exec",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Reset",                                                                 \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("N",                                                                     \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_I32,                                                                 \
      "int",                                                                   \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_I32 }))

#define NATIVE_STATE_COUNT_OUTPUTS(X)                                          \
    X("Exit",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Counter",                                                               \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_I32,                                                                 \
      "int",                                                                   \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_I32 }))

#define NATIVE_STATE_FLIP_INPUTS(X)                                            \
    X("Exec",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))

#define NATIVE_STATE_FLIP_OUTPUTS(X)                                           \
    X("A",                                                                     \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("B",                                                                     \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Is A",                                                                  \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      "bool",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL }))

#define NATIVE_STATE_GATE_INPUTS(X)                                            \
    X("Enter",                                                                 \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Open",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Close",                                                                 \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Toggle",                                                                \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Start Closed",                                                          \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      "bool",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL }))

#define NATIVE_STATE_GATE_OUTPUTS(X)                                           \
    X("Exit",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))

#define NATIVE_STATE_MULTI_INPUTS(X)                                           \
    X("Exec",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Reset",                                                                 \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Loop",                                                                  \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      "bool",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL }))                                       \
    X("Random",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      "bool",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL }))                                       \
    X("Start Index",                                                           \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_I32,                                                                 \
      "int",                                                                   \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_I32 }))

#define NATIVE_STATE_MULTI_OUTPUTS(X)                                          \
    X("Out0",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Out1",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))

#define NATIVE_STATE_RETRIGGER_INPUTS(X)                                       \
    X("Exec",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Duration",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_F64,                                                                 \
      "f64",                                                                   \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_F64 }))

#define NATIVE_STATE_RETRIGGER_OUTPUTS(X)                                      \
    X("Completed",                                                             \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))

#define NATIVE_STATE_OPERATIONS(X)                                             \
    X(once,                                                                    \
      NATIVE_STATE_ONCE_NAME,                                                  \
      NATIVE_STATE_ONCE_SOURCE,                                                \
      NATIVE_STATE_ONCE,                                                       \
      NATIVE_STATE_ONCE_INPUTS,                                                \
      NATIVE_STATE_ONCE_OUTPUTS)                                               \
    X(count,                                                                   \
      NATIVE_STATE_COUNT_NAME,                                                 \
      NATIVE_STATE_COUNT_SOURCE,                                               \
      NATIVE_STATE_COUNT,                                                      \
      NATIVE_STATE_COUNT_INPUTS,                                               \
      NATIVE_STATE_COUNT_OUTPUTS)                                              \
    X(flip,                                                                    \
      NATIVE_STATE_FLIP_NAME,                                                  \
      NATIVE_STATE_FLIP_SOURCE,                                                \
      NATIVE_STATE_FLIP,                                                       \
      NATIVE_STATE_FLIP_INPUTS,                                                \
      NATIVE_STATE_FLIP_OUTPUTS)                                               \
    X(gate,                                                                    \
      NATIVE_STATE_GATE_NAME,                                                  \
      NATIVE_STATE_GATE_SOURCE,                                                \
      NATIVE_STATE_GATE,                                                       \
      NATIVE_STATE_GATE_INPUTS,                                                \
      NATIVE_STATE_GATE_OUTPUTS)                                               \
    X(multi,                                                                   \
      NATIVE_STATE_MULTI_NAME,                                                 \
      NATIVE_STATE_MULTI_SOURCE,                                               \
      NATIVE_STATE_MULTI,                                                      \
      NATIVE_STATE_MULTI_INPUTS,                                               \
      NATIVE_STATE_MULTI_OUTPUTS)                                              \
    X(retrigger,                                                               \
      NATIVE_STATE_RETRIGGER_NAME,                                             \
      NATIVE_STATE_RETRIGGER_SOURCE,                                           \
      NATIVE_STATE_RETRIGGER,                                                  \
      NATIVE_STATE_RETRIGGER_INPUTS,                                           \
      NATIVE_STATE_RETRIGGER_OUTPUTS)

extern const native_definition_pack NATIVE_STATE_DEFINITION_PACK;

#define NATIVE_STATE_once_PRESENTATION                                         \
    .category = "Flow Control", .keywords = "stateful do once reset",          \
    .description = "Fires Completed on the first Exec only until Reset; "      \
                   "Start "                                                    \
                   "Closed blocks it until the first Reset."
#define NATIVE_STATE_count_PRESENTATION                                        \
    .category = "Flow Control", .keywords = "stateful do n counter reset",     \
    .description = "Fires Exit on the first N Execs until Reset; Counter is "  \
                   "how many times Exit fired before this Exec."
#define NATIVE_STATE_flip_PRESENTATION                                         \
    .category = "Flow Control", .keywords = "stateful alternating",            \
    .description = "Fires A and B alternately on each Exec, starting with A; " \
                   "Is A reports which one fired."
#define NATIVE_STATE_gate_PRESENTATION                                         \
    .category = "Flow Control", .keywords = "stateful gate open close toggle", \
    .description = "Passes Enter to Exit while open; Open, Close and Toggle "  \
                   "change the state, which starts open unless Start Closed."
#define NATIVE_STATE_multi_PRESENTATION                                          \
    .category = "Flow Control", .keywords = "stateful multigate sequence reset", \
    .description = "Fires one unused output per Exec, in order from Start "      \
                   "Index or at random; Loop restarts once all fire, Reset "     \
                   "clears."
#define NATIVE_STATE_retrigger_PRESENTATION                                    \
    .category = "Flow Control", .keywords = "latent delay retrigger",          \
    .description = "Waits Duration seconds, then fires Completed; triggering " \
                   "it again while waiting restarts the wait."

#endif
