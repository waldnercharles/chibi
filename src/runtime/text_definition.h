#ifndef CHIBI_NATIVE_TEXT_DEFINITION_H
#define CHIBI_NATIVE_TEXT_DEFINITION_H
#include "runtime/native_definitions.h"
#define NATIVE_FORMAT_TEXT_NAME "Format Text"
#define NATIVE_FORMAT_TEXT_SOURCE "format_text"
#define NATIVE_FORMAT_TEXT_VARIADIC_OPTIONAL true
#define NATIVE_TEXT_EQUAL_NAME "Text Equal"
#define NATIVE_TEXT_EQUAL_SOURCE "text_equal"
#define NATIVE_TEXT_CONTAINS_NAME "Text Contains"
#define NATIVE_TEXT_CONTAINS_SOURCE "text_contains"
#define NATIVE_TEXT_RANK_NAME "Text Rank"
#define NATIVE_TEXT_RANK_SOURCE "text_rank"
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
#define NATIVE_TEXT_SEARCH_INPUTS(X)                                           \
    NATIVE_TEXT_PORT(X, "Query", PIN_STR, "const char *", 0)                   \
    NATIVE_TEXT_PORT(X, "Text", PIN_STR, "const char *", 0)                    \
    NATIVE_TEXT_PORT(X, "IgnoreCase", PIN_BOOL, "bool", 0)
#define NATIVE_TEXT_CONTAINS_OUTPUTS(X)                                        \
    NATIVE_TEXT_PORT(X, "Result", PIN_BOOL, "bool", 0)
#define NATIVE_TEXT_RANK_OUTPUTS(X)                                            \
    NATIVE_TEXT_PORT(X, "Result", PIN_I32, "int", 0)
#define NATIVE_TEXT_CURSOR_INPUTS(X)                                           \
    NATIVE_TEXT_PORT(X, "Text", PIN_STR, "const char *", 0)                    \
    NATIVE_TEXT_PORT(X, "Offset", PIN_I32, "int", 0)                           \
    NATIVE_TEXT_PORT(X, "Unit", PIN_STR, "const char *", 0)                    \
    NATIVE_TEXT_PORT(X, "Count", PIN_I32, "int", 0)                            \
    NATIVE_TEXT_PORT(X, "GoalColumn", PIN_I32, "int", 0)
#define NATIVE_TEXT_CURSOR_OUTPUTS(X)                                          \
    NATIVE_TEXT_PORT(X, "Offset", PIN_I32, "int", 0)                           \
    NATIVE_TEXT_PORT(X, "Ok", PIN_BOOL, "bool", 0)                             \
    NATIVE_TEXT_PORT(X, "Error", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_LINE_INPUTS(X)                                             \
    NATIVE_TEXT_PORT(X, "Text", PIN_STR, "const char *", 0)                    \
    NATIVE_TEXT_PORT(X, "Offset", PIN_I32, "int", 0)
#define NATIVE_TEXT_LINE_OUTPUTS(X)                                            \
    NATIVE_TEXT_PORT(X, "Start", PIN_I32, "int", 0)                            \
    NATIVE_TEXT_PORT(X, "End", PIN_I32, "int", 0)                              \
    NATIVE_TEXT_PORT(X, "Next", PIN_I32, "int", 0)                             \
    NATIVE_TEXT_PORT(X, "Line", PIN_I32, "int", 0)                             \
    NATIVE_TEXT_PORT(X, "Column", PIN_I32, "int", 0)                           \
    NATIVE_TEXT_PORT(X, "Ok", PIN_BOOL, "bool", 0)                             \
    NATIVE_TEXT_PORT(X, "Error", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_SLICE_INPUTS(X)                                            \
    NATIVE_TEXT_PORT(X, "Text", PIN_STR, "const char *", 0)                    \
    NATIVE_TEXT_PORT(X, "Start", PIN_I32, "int", 0)                            \
    NATIVE_TEXT_PORT(X, "End", PIN_I32, "int", 0)
#define NATIVE_TEXT_REPLACE_INPUTS(X)                                          \
    NATIVE_TEXT_SLICE_INPUTS(X)                                                \
    NATIVE_TEXT_PORT(X, "Replacement", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_EDIT_OUTPUTS(X)                                            \
    NATIVE_TEXT_PORT(X, "Text", PIN_STR, "const char *", 0)                    \
    NATIVE_TEXT_PORT(X, "Ok", PIN_BOOL, "bool", 0)                             \
    NATIVE_TEXT_PORT(X, "Error", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_DIFFERENCE_INPUTS(X)                                       \
    NATIVE_TEXT_PORT(X, "Before", PIN_STR, "const char *", 0)                  \
    NATIVE_TEXT_PORT(X, "After", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_DIFFERENCE_OUTPUTS(X)                                      \
    NATIVE_TEXT_PORT(X, "Start", PIN_I32, "int", 0)                            \
    NATIVE_TEXT_PORT(X, "End", PIN_I32, "int", 0)                              \
    NATIVE_TEXT_PORT(X, "Replacement", PIN_STR, "const char *", 0)             \
    NATIVE_TEXT_PORT(X, "Removed", PIN_I32, "int", 0)                          \
    NATIVE_TEXT_PORT(X, "Inserted", PIN_I32, "int", 0)                         \
    NATIVE_TEXT_PORT(X, "Ok", PIN_BOOL, "bool", 0)                             \
    NATIVE_TEXT_PORT(X, "Error", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_PREFIX_LINES_INPUTS(X)                                     \
    NATIVE_TEXT_SLICE_INPUTS(X)                                                \
    NATIVE_TEXT_PORT(X, "Prefix", PIN_STR, "const char *", 0)                  \
    NATIVE_TEXT_PORT(X, "Mode", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_PREFIX_LINES_OUTPUTS(X)                                    \
    NATIVE_TEXT_PORT(X, "Text", PIN_STR, "const char *", 0)                    \
    NATIVE_TEXT_PORT(X, "Start", PIN_I32, "int", 0)                            \
    NATIVE_TEXT_PORT(X, "End", PIN_I32, "int", 0)                              \
    NATIVE_TEXT_PORT(X, "Ok", PIN_BOOL, "bool", 0)                             \
    NATIVE_TEXT_PORT(X, "Error", PIN_STR, "const char *", 0)
#define NATIVE_TEXT_EDITING_OPERATIONS(X)                                                                \
    X(TEXT_PREFIX_LINES,                                                                                 \
      "Text Prefix Lines",                                                                               \
      "text_prefix_lines",                                                                               \
      NATIVE_TEXT_PREFIX_LINES_INPUTS,                                                                   \
      NATIVE_TEXT_PREFIX_LINES_OUTPUTS,                                                                  \
      false)                                                                                             \
    X(TEXT_DIFFERENCE,                                                                                   \
      "Text Difference",                                                                                 \
      "text_difference",                                                                                 \
      NATIVE_TEXT_DIFFERENCE_INPUTS,                                                                     \
      NATIVE_TEXT_DIFFERENCE_OUTPUTS,                                                                    \
      false)                                                                                             \
    X(TEXT_CURSOR,                                                                                       \
      "Text Cursor",                                                                                     \
      "text_cursor",                                                                                     \
      NATIVE_TEXT_CURSOR_INPUTS,                                                                         \
      NATIVE_TEXT_CURSOR_OUTPUTS,                                                                        \
      false)                                                                                             \
    X(TEXT_LINE, "Text Line", "text_line", NATIVE_TEXT_LINE_INPUTS, NATIVE_TEXT_LINE_OUTPUTS, false)     \
    X(TEXT_SLICE, "Text Slice", "text_slice", NATIVE_TEXT_SLICE_INPUTS, NATIVE_TEXT_EDIT_OUTPUTS, false) \
    X(TEXT_REPLACE,                                                                                      \
      "Text Replace",                                                                                    \
      "text_replace",                                                                                    \
      NATIVE_TEXT_REPLACE_INPUTS,                                                                        \
      NATIVE_TEXT_EDIT_OUTPUTS,                                                                          \
      false)
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
      false)                                                                   \
    X(TEXT_CONTAINS,                                                           \
      NATIVE_TEXT_CONTAINS_NAME,                                               \
      NATIVE_TEXT_CONTAINS_SOURCE,                                             \
      NATIVE_TEXT_SEARCH_INPUTS,                                               \
      NATIVE_TEXT_CONTAINS_OUTPUTS,                                            \
      false)                                                                   \
    X(TEXT_RANK,                                                               \
      NATIVE_TEXT_RANK_NAME,                                                   \
      NATIVE_TEXT_RANK_SOURCE,                                                 \
      NATIVE_TEXT_SEARCH_INPUTS,                                               \
      NATIVE_TEXT_RANK_OUTPUTS,                                                \
      false)                                                                   \
    NATIVE_TEXT_EDITING_OPERATIONS(X)
extern const native_definition_pack NATIVE_TEXT_DEFINITION_PACK;
#endif
