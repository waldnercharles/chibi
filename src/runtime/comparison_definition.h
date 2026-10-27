#ifndef CHIBI_NATIVE_COMPARISON_DEFINITION_H
#define CHIBI_NATIVE_COMPARISON_DEFINITION_H
#include "runtime/native_definitions.h"
#define NATIVE_COMPARE_PORT(X, NAME, TYPE, TYPE_NAME)                          \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      TYPE,                                                                    \
      TYPE_NAME,                                                               \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = TYPE }))
#define NATIVE_COMPARE_REAL_INPUTS(X)                                          \
    NATIVE_COMPARE_PORT(X, "A", PIN_F32, "number")                             \
    NATIVE_COMPARE_PORT(X, "B", PIN_F32, "number")
#define NATIVE_COMPARE_INTEGER_INPUTS(X)                                       \
    NATIVE_COMPARE_PORT(X, "A", PIN_I32, "int")                                \
    NATIVE_COMPARE_PORT(X, "B", PIN_I32, "int")
#define NATIVE_COMPARE_FLAG_INPUTS(X)                                          \
    NATIVE_COMPARE_PORT(X, "Value", PIN_U32, "unsigned int")                   \
    NATIVE_COMPARE_PORT(X, "Flags", PIN_U32, "unsigned int")
#define NATIVE_COMPARE_BOOL_INPUTS(X)                                          \
    NATIVE_COMPARE_PORT(X, "A", PIN_BOOL, "bool")                              \
    NATIVE_COMPARE_PORT(X, "B", PIN_BOOL, "bool")
#define NATIVE_COMPARE_UNARY_INPUTS(X)                                         \
    NATIVE_COMPARE_PORT(X, "Value", PIN_BOOL, "bool")
#define NATIVE_COMPARE_OUTPUTS(X)                                              \
    NATIVE_COMPARE_PORT(X, "Result", PIN_BOOL, "bool")
#define NATIVE_COMPARE_REALS(X)                                                                                                                              \
    X(Less, "Less", "less", <, false, false, "compare less below <", "Whether A is less than B, compared as reals.")                                         \
    X(Less_Or_Equal,                                                                                                                                         \
      "Less Or Equal",                                                                                                                                       \
      "less_or_equal",                                                                                                                                       \
      <=,                                                                                                                                                    \
      false,                                                                                                                                                 \
      false,                                                                                                                                                 \
      "compare less equal at most <= ≤",                                                                                                                     \
      "Whether A is less than or equal to B, compared as reals.")                                                                                            \
    X(Greater, "Greater", "greater", >, false, false, "compare greater above >", "Whether A is greater than B, compared as reals.")                          \
    X(Greater_Or_Equal,                                                                                                                                      \
      "Greater Or Equal",                                                                                                                                    \
      "greater_or_equal",                                                                                                                                    \
      >=,                                                                                                                                                    \
      false,                                                                                                                                                 \
      false,                                                                                                                                                 \
      "compare greater equal at least >= ≥",                                                                                                                 \
      "Whether A is greater than or equal to B, compared as reals.")                                                                                         \
    X(Equal, "Equal", "equal", ==, true, false, "compare equal equality == =", "Whether A exactly equals B as reals, with no tolerance; NaN never matches.") \
    X(Not_Equal, "Not Equal", "not_equal", !=, true, true, "compare not equal inequality != ≠", "Whether A differs from B as reals, compared exactly; NaN always differs.")
#define NATIVE_COMPARE_INTEGERS(X)                                             \
    X(Integer_Less,                                                            \
      "Integer Less",                                                          \
      "integer_less",                                                          \
      <,                                                                       \
      "compare less below integer <",                                          \
      "Whether A is less than B, compared as 32-bit integers.")                \
    X(Integer_Less_Or_Equal,                                                   \
      "Integer Less Or Equal",                                                 \
      "integer_less_or_equal",                                                 \
      <=,                                                                      \
      "compare less equal at most integer <= ≤",                               \
      "Whether A is less than or equal to B, compared as 32-bit integers.")    \
    X(Integer_Greater,                                                         \
      "Integer Greater",                                                       \
      "integer_greater",                                                       \
      >,                                                                       \
      "compare greater above integer >",                                       \
      "Whether A is greater than B, compared as 32-bit integers.")             \
    X(Integer_Greater_Or_Equal,                                                \
      "Integer Greater Or Equal",                                              \
      "integer_greater_or_equal",                                              \
      >=,                                                                      \
      "compare greater equal at least integer >= ≥",                           \
      "Whether A is greater than or equal to B, compared as 32-bit integers.") \
    X(Integer_Equal,                                                           \
      "Integer Equal",                                                         \
      "integer_equal",                                                         \
      ==,                                                                      \
      "compare equal equality integer == =",                                   \
      "Whether A equals B, compared as 32-bit integers.")                      \
    X(Integer_Not_Equal,                                                       \
      "Integer Not Equal",                                                     \
      "integer_not_equal",                                                     \
      !=,                                                                      \
      "compare not equal inequality integer != ≠",                             \
      "Whether A differs from B, compared as 32-bit integers.")
#define NATIVE_COMPARE_VALUES(X)                                                                          \
    X(Has_Any_Flags,                                                                                      \
      "Has Any Flags",                                                                                    \
      "has_any_flags",                                                                                    \
      NATIVE_COMPARE_FLAG_INPUTS,                                                                         \
      "Logic/Flags",                                                                                      \
      "bits mask contains any category",                                                                  \
      "Whether Value and Flags share at least one set bit.")                                              \
    X(And, "And", "and", NATIVE_COMPARE_BOOL_INPUTS, "Logic", "and &&", "Whether A and B are both true.") \
    X(Or, "Or", "or", NATIVE_COMPARE_BOOL_INPUTS, "Logic", "or ||", "Whether A, B or both are true.")     \
    X(Not, "Not", "not", NATIVE_COMPARE_UNARY_INPUTS, "Logic", "not !", "Whether Value is false.")
extern const native_definition_pack NATIVE_COMPARISON_DEFINITION_PACK;
#endif
