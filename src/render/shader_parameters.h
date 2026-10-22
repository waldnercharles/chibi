#ifndef CHIBI_SHADER_PARAMETERS_H
#define CHIBI_SHADER_PARAMETERS_H
#include "runtime/native_definitions.h"
#define RENDER_SHADER_PARAMETER_INPUTS(X)                                                            \
    X("Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, 0, 0, ((pin_value){ .type = PIN_FLOW }), false) \
    X("Entity",                                                                                      \
      NATIVE_PARAMETER_DATA,                                                                         \
      PIN_HANDLE,                                                                                    \
      "ecs.entity",                                                                                  \
      0,                                                                                             \
      0,                                                                                             \
      ((pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity" }),                                \
      false)                                                                                         \
    X("Name", NATIVE_PARAMETER_DATA, PIN_STR, NULL, 0, 0, ((pin_value){ .type = PIN_STR }), true)    \
    X("Value",                                                                                       \
      NATIVE_PARAMETER_DATA,                                                                         \
      PIN_F32,                                                                                       \
      NULL,                                                                                          \
      SCRIPT_TYPE_GENERIC,                                                                           \
      1,                                                                                             \
      ((pin_value){ .type = PIN_F32 }),                                                              \
      false)
#define RENDER_SHADER_PARAMETER_OUTPUTS(X)                                     \
    X("Then", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, 0, 0, ((pin_value){ .type = PIN_FLOW }), false)
extern const node_host_call RENDER_MATERIAL_PARAMETER_CALL, RENDER_SHAPE_PARAMETER_CALL;
extern const node_access RENDER_SHADER_PARAMETER_ACCESS;
extern const native_definition_pack RENDER_SHADER_PARAMETER_DEFINITION_PACK;
#endif
