#ifndef CHIBI_NATIVE_TEXT_DEFINITION_H
#define CHIBI_NATIVE_TEXT_DEFINITION_H
#include "runtime/native_definitions.h"
#define NATIVE_FORMAT_TEXT_NAME "Format Text"
#define NATIVE_FORMAT_TEXT_SOURCE "format_text"
#define NATIVE_FORMAT_TEXT_VARIADIC_OPTIONAL true
#define NATIVE_TEXT_EQUAL_NAME "Text Equal"
#define NATIVE_TEXT_EQUAL_SOURCE "text_equal"
#define NATIVE_TEXT_PORT(X, NAME, STORAGE, NOMINAL, FLAGS)                     \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      STORAGE,                                                                 \
      NOMINAL,                                                                 \
      PIN_CONTAINER_SINGLE,                                                    \
      FLAGS,                                                                   \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define NATIVE_FORMAT_TEXT_INPUTS(X)                                           \
    NATIVE_TEXT_PORT(X, "Format", PIN_STR, "const char *", 0)                  \
    NATIVE_TEXT_PORT(X, "Arg0", PIN_FLOW, NULL, SCRIPT_TYPE_UNTYPED)
#define NATIVE_FORMAT_TEXT_OUTPUTS(X)                                          \
    NATIVE_TEXT_PORT(X, "Result", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_EQUAL_INPUTS(X)                                            \
    NATIVE_TEXT_PORT(X, "A", PIN_STR, "const char *", 0)                       \
    NATIVE_TEXT_PORT(X, "B", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_EQUAL_OUTPUTS(X)                                           \
    NATIVE_TEXT_PORT(X, "Result", PIN_BOOL, "bool", 0)
#define NATIVE_TEXT_OPERATIONS(X)                                              \
    X(FORMAT_TEXT,                                                             \
      NATIVE_FORMAT_TEXT_NAME,                                                 \
      NATIVE_FORMAT_TEXT_SOURCE,                                               \
      NATIVE_FORMAT_TEXT_INPUTS,                                               \
      NATIVE_FORMAT_TEXT_OUTPUTS,                                              \
      NATIVE_FORMAT_TEXT_VARIADIC_OPTIONAL)                                    \
    X(TEXT_EQUAL,                                                              \
      NATIVE_TEXT_EQUAL_NAME,                                                  \
      NATIVE_TEXT_EQUAL_SOURCE,                                                \
      NATIVE_TEXT_EQUAL_INPUTS,                                                \
      NATIVE_TEXT_EQUAL_OUTPUTS,                                               \
      false)
extern const native_definition_pack NATIVE_TEXT_DEFINITION_PACK;
#endif
