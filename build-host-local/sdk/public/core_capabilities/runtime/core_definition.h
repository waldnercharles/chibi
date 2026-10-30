#ifndef CHIBI_CORE_DEFINITION_H
#define CHIBI_CORE_DEFINITION_H

#include "runtime/native_definitions.h"

#define CORE_BRANCH_NAME "Branch"
#define CORE_BRANCH_SOURCE "branch_if"
#define CORE_SEQUENCE_NAME "Sequence"
#define CORE_SEQUENCE_SOURCE "sequence"
#define CORE_RANGE_NAME "ForLoop"
#define CORE_RANGE_SOURCE "for_loop"
#define CORE_RANGE_BREAK_NAME "ForLoopWithBreak"
#define CORE_RANGE_BREAK_SOURCE "for_loop_with_break"
#define CORE_WHILE_NAME "WhileLoop"
#define CORE_WHILE_SOURCE "while_loop"
#define CORE_EACH_NAME "ForEach"
#define CORE_EACH_SOURCE "for_each"
#define CORE_EACH_BREAK_NAME "ForEachWithBreak"
#define CORE_EACH_BREAK_SOURCE "for_each_with_break"
#define CORE_BREAK_NAME "Break"
#define CORE_BREAK_SOURCE "break"
#define CORE_DELAY_NAME "Delay"
#define CORE_DELAY_SOURCE "delay"
#define CORE_ASSERT_NAME "Assert"
#define CORE_ASSERT_SOURCE "assert"
#define CORE_PRINT_NAME "Print String"
#define CORE_PRINT_SOURCE "print_string"
#define CORE_GET_NAME "Get Variable"
#define CORE_GET_SOURCE "get_variable"
#define CORE_SET_NAME "Set Variable"
#define CORE_SET_SOURCE "set_variable"
#define CORE_SELF_NAME "Self"
#define CORE_SELF_SOURCE "self"

#define CORE_EXEC_PORT(X, NAME)                                                \
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
#define CORE_DATA_PORT(X, NAME, STORAGE, TYPE_NAME)                            \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      STORAGE,                                                                 \
      TYPE_NAME,                                                               \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define CORE_EXEC_INPUTS(X) CORE_EXEC_PORT(X, "Exec")
#define CORE_THEN_OUTPUTS(X) CORE_EXEC_PORT(X, "Then")
#define CORE_CONDITION_INPUTS(X)                                               \
    CORE_EXEC_INPUTS(X) CORE_DATA_PORT(X, "Condition", PIN_BOOL, "bool")
#define CORE_BRANCH_OUTPUTS(X)                                                 \
    CORE_EXEC_PORT(X, "True") CORE_EXEC_PORT(X, "False")
#define CORE_SEQUENCE_OUTPUTS(X)                                               \
    CORE_EXEC_PORT(X, "Then0") CORE_EXEC_PORT(X, "Then1")
#define CORE_RANGE_DATA(X)                                                     \
    CORE_DATA_PORT(X, "FirstIndex", PIN_I32, "int")                            \
    CORE_DATA_PORT(X, "LastIndex", PIN_I32, "int")
#define CORE_RANGE_INPUTS(X) CORE_EXEC_INPUTS(X) CORE_RANGE_DATA(X)
#define CORE_RANGE_BREAK_INPUTS(X)                                             \
    CORE_EXEC_INPUTS(X) CORE_EXEC_PORT(X, "Break") CORE_RANGE_DATA(X)
#define CORE_RANGE_OUTPUTS(X)                                                  \
    CORE_EXEC_PORT(X, "Body")                                                  \
    CORE_DATA_PORT(X, "Index", PIN_I32, "int") CORE_EXEC_PORT(X, "Completed")
#define CORE_WHILE_OUTPUTS(X)                                                  \
    CORE_EXEC_PORT(X, "Body") CORE_EXEC_PORT(X, "Completed")
#define CORE_EACH_ARRAY(X)                                                     \
    X("Array",                                                                 \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_ARRAY,                                                     \
      SCRIPT_TYPE_UNTYPED,                                                     \
      1,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .container = PIN_CONTAINER_ARRAY }))
#define CORE_EACH_INPUTS(X) CORE_EXEC_INPUTS(X) CORE_EACH_ARRAY(X)
#define CORE_EACH_BREAK_INPUTS(X)                                              \
    CORE_EXEC_INPUTS(X) CORE_EXEC_PORT(X, "Break") CORE_EACH_ARRAY(X)
#define CORE_EACH_OUTPUTS(X)                                                   \
    CORE_EXEC_PORT(X, "Body")                                                  \
    X("Array Element",                                                         \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      SCRIPT_TYPE_UNTYPED,                                                     \
      1,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))                                                      \
    CORE_DATA_PORT(X, "Index", PIN_I32, "int") CORE_EXEC_PORT(X, "Completed")
#define CORE_DELAY_INPUTS(X)                                                   \
    CORE_EXEC_INPUTS(X) CORE_DATA_PORT(X, "Duration", PIN_F64, "f64")
#define CORE_DELAY_OUTPUTS(X) CORE_EXEC_PORT(X, "Completed")
#define CORE_PRINT_INPUTS(X)                                                   \
    CORE_EXEC_INPUTS(X) CORE_DATA_PORT(X, "InString", PIN_STR, "const char *")
#define CORE_ASSERT_INPUTS(X)                                                  \
    CORE_DATA_PORT(X, "Condition", PIN_BOOL, "bool")                           \
    CORE_DATA_PORT(X, "Message", PIN_STR, "const char *")
#define CORE_VARIABLE_DATA(X) CORE_DATA_PORT(X, "Value", PIN_I32, "int")
#define CORE_SET_INPUTS(X) CORE_EXEC_INPUTS(X) CORE_VARIABLE_DATA(X)
#define CORE_SET_OUTPUTS(X) CORE_THEN_OUTPUTS(X) CORE_VARIABLE_DATA(X)
#define CORE_SELF_OUTPUTS(X) CORE_DATA_PORT(X, "Self", PIN_HANDLE, "ecs.entity")

/* Generic component selectors share their declaration with graph presentation.
 * Entity/value ordinals below refer only to DATA inputs. */
#define CORE_COMPONENT_SELECTOR(X)                                             \
    X("Component",                                                             \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_NAME,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_COMPONENT,                                              \
      false,                                                                   \
      ((pin_value){ .type = PIN_NAME }))
#define CORE_COMPONENT_ENTITY(X)                                               \
    X("Entity",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      true,                                                                    \
      ((pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity" }))
#define CORE_COMPONENT_VALUE(X, FLAGS)                                         \
    X("Value",                                                                 \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_STRUCT,                                                              \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      FLAGS,                                                                   \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_STRUCT }))
#define CORE_COMPONENT_GET_INPUTS(X)                                           \
    CORE_COMPONENT_SELECTOR(X) CORE_COMPONENT_ENTITY(X)
#define CORE_COMPONENT_GET_OUTPUTS(X)                                          \
    CORE_COMPONENT_VALUE(X, SCRIPT_TYPE_REFERENCE)
#define CORE_COMPONENT_HAS_OUTPUTS(X)                                          \
    CORE_DATA_PORT(X, "Value", PIN_BOOL, "bool")
#define CORE_COMPONENT_SET_INPUTS(X)                                           \
    CORE_EXEC_INPUTS(X) CORE_COMPONENT_GET_INPUTS(X) CORE_COMPONENT_VALUE(X, 0)
#define CORE_COMPONENT_SET_OUTPUTS(X)                                          \
    CORE_THEN_OUTPUTS(X) CORE_COMPONENT_VALUE(X, 0)
#define CORE_COMPONENT_CHANGE_INPUTS(X)                                        \
    CORE_EXEC_INPUTS(X) CORE_COMPONENT_SELECTOR(X)
#define CORE_COMPONENT_CHANGE_OUTPUTS(X)                                       \
    CORE_THEN_OUTPUTS(X) CORE_COMPONENT_HAS_OUTPUTS(X)
#define CORE_COMPONENT_GET_NAME "Get Component"
#define CORE_COMPONENT_SET_NAME "Set Component"
#define CORE_COMPONENT_HAS_NAME "Has Component"
#define CORE_COMPONENT_ADD_NAME "Add Component"
#define CORE_COMPONENT_REMOVE_NAME "Remove Component"
#define CORE_COMPONENT_GET_PRESENTATION                                        \
    .category = "Components", .keywords = "component read field entity self",  \
    .description = "Reads a component from Entity, which defaults to Self."
#define CORE_COMPONENT_SET_PRESENTATION                                        \
    .category = "Components",                                                  \
    .keywords = "component write assign field entity self", .description = "Writes a component, or only the fields it shows, on Entity, which defaults to Self."
#define CORE_COMPONENT_HAS_PRESENTATION                                        \
    .category = "Components",                                                  \
    .keywords = "component present exists check entity self",                  \
    .description = "Whether Entity, which defaults to Self, has a component."
#define CORE_COMPONENT_ADD_PRESENTATION                                        \
    .category = "Components",                                                  \
    .keywords = "component add attach insert self", .description = "Adds a component with default values to Self; Value is whether it was added."
#define CORE_COMPONENT_REMOVE_PRESENTATION                                     \
    .category = "Components",                                                  \
    .keywords = "component remove detach delete self", .description = "Removes a component from Self; Value is whether it was there."

/* One semantic ordinal declaration; the graph adapter supplies its enum kind. */
#define CORE_BRANCH_FLOW(KIND)                                                 \
    { .kind = KIND,                                                            \
      .inputs = { 0, -1 },                                                     \
      .results = { -1, -1 },                                                   \
      .outcomes = { 0, 1 },                                                    \
      .break_entry = -1 }
#define CORE_SEQUENCE_FLOW(KIND)                                               \
    { .kind = KIND,                                                            \
      .inputs = { -1, -1 },                                                    \
      .results = { -1, -1 },                                                   \
      .outcomes = { -1, -1 },                                                  \
      .break_entry = -1 }
#define CORE_RANGE_FLOW(KIND, BREAK)                                           \
    { .kind = KIND,                                                            \
      .inputs = { 0, 1 },                                                      \
      .results = { 0, -1 },                                                    \
      .outcomes = { 0, 1 },                                                    \
      .break_entry = BREAK }
#define CORE_WHILE_FLOW(KIND)                                                  \
    { .kind = KIND,                                                            \
      .inputs = { 0, -1 },                                                     \
      .results = { -1, -1 },                                                   \
      .outcomes = { 0, 1 },                                                    \
      .break_entry = -1 }
#define CORE_EACH_FLOW(KIND, BREAK)                                            \
    { .kind = KIND,                                                            \
      .inputs = { 0, -1 },                                                     \
      .results = { 1, 0 },                                                     \
      .outcomes = { 0, 1 },                                                    \
      .break_entry = BREAK }
#define CORE_BREAK_FLOW(KIND) CORE_SEQUENCE_FLOW(KIND)

extern const native_definition_pack CORE_DEFINITION_PACK;

#define CORE_DELAY_PRESENTATION                                                \
    .keywords = "wait timer seconds after later",                              \
    .description = "Waits Duration seconds, then fires Completed; a Duration " \
                   "of zero or less continues at once."
#define CORE_ASSERT_PRESENTATION                                               \
    .keywords = "check expect test",                                           \
    .description = "Fails the call with Message, or Condition's source "       \
                   "text, when Condition is false."
#define CORE_PRINT_PRESENTATION                                                \
    .keywords = "log debug message",                                           \
    .description = "Writes InString to the behavior log, then fires Then."
#define CORE_SELF_PRESENTATION                                                 \
    .keywords = "owner this entity",                                           \
    .description = "Returns the entity that owns the running behavior."

#endif
