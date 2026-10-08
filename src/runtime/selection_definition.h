#ifndef CHIBI_NATIVE_SELECTION_DEFINITION_H
#define CHIBI_NATIVE_SELECTION_DEFINITION_H

#include "runtime/native_definitions.h"

#define NATIVE_SELECT_NAME "Select"
#define NATIVE_SELECT_SOURCE "select"
#define NATIVE_SELECT_OPTION(X, NAME)                                          \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      SCRIPT_TYPE_UNTYPED,                                                     \
      1,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define NATIVE_SELECT_INPUTS(X)                                                \
    X("Index",                                                                 \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_I32,                                                                 \
      "int",                                                                   \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))                                                      \
    NATIVE_SELECT_OPTION(X, "Option0")                                         \
    NATIVE_SELECT_OPTION(X, "Option1")
#define NATIVE_SELECT_OUTPUTS(X) NATIVE_SELECT_OPTION(X, "Return Value")

#define NATIVE_SWITCH_EXEC(X, NAME)                                            \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define NATIVE_SWITCH_INPUTS(X, STORAGE, NAME)                                 \
    NATIVE_SWITCH_EXEC(X, "Exec")                                              \
    X("Selection",                                                             \
      NATIVE_PARAMETER_DATA,                                                   \
      STORAGE,                                                                 \
      NAME,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define NATIVE_SWITCH_INT_INPUTS(X) NATIVE_SWITCH_INPUTS(X, PIN_I32, "int")
#define NATIVE_SWITCH_STRING_INPUTS(X)                                         \
    NATIVE_SWITCH_INPUTS(X, PIN_STR, "const char *")
#define NATIVE_SWITCH_NAME_INPUTS(X) NATIVE_SWITCH_INPUTS(X, PIN_NAME, "name")
#define NATIVE_SWITCH_OUTPUTS(X)                                               \
    NATIVE_SWITCH_EXEC(X, "Case0")                                             \
    NATIVE_SWITCH_EXEC(X, "Case1") NATIVE_SWITCH_EXEC(X, "Default")
#define NATIVE_SWITCH_INT_NAME "Switch on Int"
#define NATIVE_SWITCH_STRING_NAME "Switch on String"
#define NATIVE_SWITCH_NAME_NAME "Switch on Name"
#define NATIVE_SWITCH_ENUM_NAME "Switch on Enum"
#define NATIVE_SWITCH_OPERATIONS(X)                                                             \
    X(Int, NATIVE_SWITCH_INT_NAME, "switch_on_int", NATIVE_SWITCH_INT_INPUTS, INT)              \
    X(String, NATIVE_SWITCH_STRING_NAME, "switch_on_string", NATIVE_SWITCH_STRING_INPUTS, TEXT) \
    X(Name, NATIVE_SWITCH_NAME_NAME, "switch_on_name", NATIVE_SWITCH_NAME_INPUTS, TEXT)         \
    X(Enum, NATIVE_SWITCH_ENUM_NAME, "switch_on_enum", NATIVE_SWITCH_INT_INPUTS, INT)

extern const native_definition_pack NATIVE_SELECTION_DEFINITION_PACK;

#define NATIVE_SELECT_PRESENTATION                                             \
    .category = "Utilities",                                                   \
    .keywords = "select choose pick value ternary if else bool true false "    \
                "option index",                                                \
    .description = "Returns the Option input chosen by zero-based Index. "     \
                   "A bool Index picks Option1 when true and Option0 when "    \
                   "false. An out-of-range Index fails evaluation."

#define NATIVE_SWITCH_Int_PRESENTATION                                         \
    .category = "Flow Control", .keywords = "switch int cases default",        \
    .description = "Fires the case output numbered Selection, counting from "  \
                   "0, "                                                       \
                   "or Default when out of range."
#define NATIVE_SWITCH_String_PRESENTATION                                      \
    .category = "Flow Control", .keywords = "switch string cases default",     \
    .description = "Fires the case whose label exactly matches Selection, or " \
                   "Default when none does."
#define NATIVE_SWITCH_Name_PRESENTATION                                        \
    .category = "Flow Control", .keywords = "switch name cases default",       \
    .description = "Fires the case whose label matches the Selection name, "   \
                   "or "                                                       \
                   "Default when none does."
#define NATIVE_SWITCH_Enum_PRESENTATION                                        \
    .category = "Flow Control", .keywords = "switch enum cases default",       \
    .description = "Fires the case output at Selection's enum value, "         \
                   "counting "                                                 \
                   "from 0, or Default when out of range."

#endif
