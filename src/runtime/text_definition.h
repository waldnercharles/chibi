#ifndef CHIBI_NATIVE_TEXT_DEFINITION_H
#define CHIBI_NATIVE_TEXT_DEFINITION_H
#include "runtime/native_definitions.h"
#define NATIVE_FORMAT_TEXT_NAME "Format Text"
#define NATIVE_FORMAT_TEXT_SOURCE "format_text"
#define NATIVE_FORMAT_TEXT_VARIADIC_OPTIONAL true
#define NATIVE_TEXT_EQUAL_NAME "Text Equal"
#define NATIVE_TEXT_EQUAL_SOURCE "text_equal"
#define NATIVE_TEXT_BYTE_LENGTH_NAME "Text Byte Length"
#define NATIVE_TEXT_BYTE_LENGTH_SOURCE "text_byte_length"
#define NATIVE_TEXT_BYTE_AT_NAME "Text Byte At"
#define NATIVE_TEXT_BYTE_AT_SOURCE "text_byte_at"
#define NATIVE_TEXT_SLICE_NAME "Text Slice"
#define NATIVE_TEXT_SLICE_SOURCE "text_slice"
#define NATIVE_TEXT_BYTES_NAME "Text Bytes"
#define NATIVE_TEXT_BYTES_SOURCE "text_bytes"
#define NATIVE_TEXT_FROM_BYTES_NAME "Text From Bytes"
#define NATIVE_TEXT_FROM_BYTES_SOURCE "text_from_bytes"
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
#define NATIVE_TEXT_VALUE_INPUTS(X)                                            \
    NATIVE_TEXT_PORT(X, "Value", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_BYTE_AT_INPUTS(X)                                          \
    NATIVE_TEXT_VALUE_INPUTS(X)                                                \
    NATIVE_TEXT_PORT(X, "Index", PIN_I32, "int", 0)
#define NATIVE_TEXT_SLICE_INPUTS(X)                                            \
    NATIVE_TEXT_VALUE_INPUTS(X)                                                \
    NATIVE_TEXT_PORT(X, "Start", PIN_I32, "int", 0)                            \
    NATIVE_TEXT_PORT(X, "Count", PIN_I32, "int", 0)
#define NATIVE_TEXT_INT_OUTPUTS(X)                                             \
    NATIVE_TEXT_PORT(X, "Result", PIN_I32, "int", 0)
#define NATIVE_TEXT_BYTES_OUTPUTS(X)                                           \
    X("Result",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_I32,                                                                 \
      "int",                                                                   \
      PIN_CONTAINER_ARRAY,                                                     \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY }))
#define NATIVE_TEXT_FROM_BYTES_INPUTS(X)                                       \
    X("Value",                                                                 \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_U8,                                                                  \
      "u8",                                                                    \
      PIN_CONTAINER_ARRAY,                                                     \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_U8, .container = PIN_CONTAINER_ARRAY }))
#define NATIVE_COMPILER_TEXT_OPERATIONS(X)                                     \
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
      false)                                                                   \
    X(TEXT_BYTE_LENGTH,                                                        \
      NATIVE_TEXT_BYTE_LENGTH_NAME,                                            \
      NATIVE_TEXT_BYTE_LENGTH_SOURCE,                                          \
      NATIVE_TEXT_VALUE_INPUTS,                                                \
      NATIVE_TEXT_INT_OUTPUTS,                                                 \
      false)                                                                   \
    X(TEXT_BYTE_AT,                                                            \
      NATIVE_TEXT_BYTE_AT_NAME,                                                \
      NATIVE_TEXT_BYTE_AT_SOURCE,                                              \
      NATIVE_TEXT_BYTE_AT_INPUTS,                                              \
      NATIVE_TEXT_INT_OUTPUTS,                                                 \
      false)                                                                   \
    X(TEXT_SLICE,                                                              \
      NATIVE_TEXT_SLICE_NAME,                                                  \
      NATIVE_TEXT_SLICE_SOURCE,                                                \
      NATIVE_TEXT_SLICE_INPUTS,                                                \
      NATIVE_FORMAT_TEXT_OUTPUTS,                                              \
      false)                                                                   \
    X(TEXT_BYTES,                                                              \
      NATIVE_TEXT_BYTES_NAME,                                                  \
      NATIVE_TEXT_BYTES_SOURCE,                                                \
      NATIVE_TEXT_VALUE_INPUTS,                                                \
      NATIVE_TEXT_BYTES_OUTPUTS,                                               \
      false)
#define NATIVE_TEXT_OPERATIONS(X)                                              \
    NATIVE_COMPILER_TEXT_OPERATIONS(X)                                         \
    X(TEXT_FROM_BYTES,                                                         \
      NATIVE_TEXT_FROM_BYTES_NAME,                                             \
      NATIVE_TEXT_FROM_BYTES_SOURCE,                                           \
      NATIVE_TEXT_FROM_BYTES_INPUTS,                                           \
      NATIVE_FORMAT_TEXT_OUTPUTS,                                              \
      false)
extern const native_definition_pack NATIVE_TEXT_DEFINITION_PACK;
/* The compiler seed retains its pinned native catalog. Public packages use
 * NATIVE_TEXT_DEFINITION_PACK, which also includes binary conversion. */
extern const native_definition_pack NATIVE_COMPILER_TEXT_DEFINITION_PACK;
#endif
