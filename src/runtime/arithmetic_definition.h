#ifndef CHIBI_NATIVE_ARITHMETIC_DEFINITION_H
#define CHIBI_NATIVE_ARITHMETIC_DEFINITION_H
#include "runtime/native_definitions.h"
#define NATIVE_ARITHMETIC_PORT(X, NAME, TYPE, NOMINAL, FLAGS, GROUP)           \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      TYPE,                                                                    \
      NOMINAL,                                                                 \
      PIN_CONTAINER_SINGLE,                                                    \
      FLAGS,                                                                   \
      GROUP,                                                                   \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define NATIVE_ARITHMETIC_GENERIC(X, NAME, GROUP)                              \
    NATIVE_ARITHMETIC_PORT(X, NAME, PIN_F32, "number", SCRIPT_TYPE_UNTYPED, GROUP)
#define NATIVE_ARITHMETIC_UNARY_INPUTS(X)                                      \
    NATIVE_ARITHMETIC_GENERIC(X, "Value", 1)
#define NATIVE_ARITHMETIC_BINARY_INPUTS(X)                                     \
    NATIVE_ARITHMETIC_GENERIC(X, "A", 1) NATIVE_ARITHMETIC_GENERIC(X, "B", 2)
#define NATIVE_ARITHMETIC_CLAMP_INPUTS(X)                                      \
    NATIVE_ARITHMETIC_GENERIC(X, "Value", 1)                                   \
    NATIVE_ARITHMETIC_GENERIC(X, "Min", 2)                                     \
    NATIVE_ARITHMETIC_GENERIC(X, "Max", 3)
#define NATIVE_ARITHMETIC_OUTPUTS(X) NATIVE_ARITHMETIC_GENERIC(X, "Result", 1)
#define NATIVE_ARITHMETIC_Add_NAME "Add"
#define NATIVE_ARITHMETIC_Add_OPERATOR "+"
#define NATIVE_ARITHMETIC_Multiply_NAME "Multiply"
#define NATIVE_ARITHMETIC_Multiply_OPERATOR "*"
#define NATIVE_ARITHMETIC_Subtract_NAME "Subtract"
#define NATIVE_ARITHMETIC_Subtract_OPERATOR "-"
#define NATIVE_ARITHMETIC_Divide_NAME "Divide"
#define NATIVE_ARITHMETIC_Divide_OPERATOR "/"
#define NATIVE_ARITHMETIC_Min_NAME "Min"
#define NATIVE_ARITHMETIC_Min_OPERATOR NULL
#define NATIVE_ARITHMETIC_Max_NAME "Max"
#define NATIVE_ARITHMETIC_Max_OPERATOR NULL
#define NATIVE_ARITHMETIC_Modulo_NAME "Modulo"
#define NATIVE_ARITHMETIC_Modulo_OPERATOR "%"
#define NATIVE_ARITHMETIC_Clamp_NAME "Clamp"
#define NATIVE_ARITHMETIC_Clamp_OPERATOR NULL
#define NATIVE_ARITHMETIC_Negate_NAME "Negate"
#define NATIVE_ARITHMETIC_Negate_OPERATOR NULL
#define NATIVE_ARITHMETIC_Abs_NAME "Abs"
#define NATIVE_ARITHMETIC_Abs_OPERATOR NULL
#define NATIVE_ARITHMETIC_Bool_NAME "Bool"
#define NATIVE_ARITHMETIC_Bool_OPERATOR NULL
#define NATIVE_ARITHMETIC_Bool_PORTS(X)                                        \
    NATIVE_ARITHMETIC_PORT(X, "Value", PIN_BOOL, "bool", 0, 0)
#define NATIVE_ARITHMETIC_Byte_NAME "Byte"
#define NATIVE_ARITHMETIC_Byte_OPERATOR NULL
#define NATIVE_ARITHMETIC_Byte_PORTS(X)                                        \
    NATIVE_ARITHMETIC_PORT(X, "Value", PIN_U8, "u8", 0, 0)
#define NATIVE_ARITHMETIC_Integer_NAME "Integer"
#define NATIVE_ARITHMETIC_Integer_OPERATOR NULL
#define NATIVE_ARITHMETIC_Integer_PORTS(X)                                     \
    NATIVE_ARITHMETIC_PORT(X, "Value", PIN_I32, "int", 0, 0)
#define NATIVE_ARITHMETIC_Integer64_NAME "Integer64"
#define NATIVE_ARITHMETIC_Integer64_OPERATOR NULL
#define NATIVE_ARITHMETIC_Integer64_PORTS(X)                                   \
    NATIVE_ARITHMETIC_PORT(X, "Value", PIN_I64, "i64", 0, 0)
#define NATIVE_ARITHMETIC_Real_NAME "Real"
#define NATIVE_ARITHMETIC_Real_OPERATOR NULL
#define NATIVE_ARITHMETIC_Real_PORTS(X)                                        \
    NATIVE_ARITHMETIC_PORT(X, "Value", PIN_F64, "f64", 0, 0)
#define NATIVE_ARITHMETIC_Name_NAME "Name"
#define NATIVE_ARITHMETIC_Name_OPERATOR NULL
#define NATIVE_ARITHMETIC_Name_PORTS(X)                                        \
    NATIVE_ARITHMETIC_PORT(X, "Value", PIN_NAME, "name", 0, 0)
#define NATIVE_ARITHMETIC_String_NAME "String"
#define NATIVE_ARITHMETIC_String_OPERATOR NULL
#define NATIVE_ARITHMETIC_String_PORTS(X)                                      \
    NATIVE_ARITHMETIC_PORT(X, "Value", PIN_STR, "const char *", 0, 0)
#define NATIVE_ARITHMETIC_OPERATIONS(X)                                        \
    X(Add,                                                                     \
      add,                                                                     \
      NATIVE_ARITHMETIC_Add_NAME,                                              \
      "add",                                                                   \
      NATIVE_ARITHMETIC_Add_OPERATOR,                                          \
      NATIVE_ARITHMETIC_BINARY_INPUTS,                                         \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      true)                                                                    \
    X(Multiply,                                                                \
      multiply,                                                                \
      NATIVE_ARITHMETIC_Multiply_NAME,                                         \
      "multiply",                                                              \
      NATIVE_ARITHMETIC_Multiply_OPERATOR,                                     \
      NATIVE_ARITHMETIC_BINARY_INPUTS,                                         \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      true)                                                                    \
    X(Subtract,                                                                \
      subtract,                                                                \
      NATIVE_ARITHMETIC_Subtract_NAME,                                         \
      "subtract",                                                              \
      NATIVE_ARITHMETIC_Subtract_OPERATOR,                                     \
      NATIVE_ARITHMETIC_BINARY_INPUTS,                                         \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      true)                                                                    \
    X(Divide,                                                                  \
      divide,                                                                  \
      NATIVE_ARITHMETIC_Divide_NAME,                                           \
      "divide",                                                                \
      NATIVE_ARITHMETIC_Divide_OPERATOR,                                       \
      NATIVE_ARITHMETIC_BINARY_INPUTS,                                         \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      true)                                                                    \
    X(Min,                                                                     \
      min,                                                                     \
      NATIVE_ARITHMETIC_Min_NAME,                                              \
      "min",                                                                   \
      NATIVE_ARITHMETIC_Min_OPERATOR,                                          \
      NATIVE_ARITHMETIC_BINARY_INPUTS,                                         \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      true)                                                                    \
    X(Max,                                                                     \
      max,                                                                     \
      NATIVE_ARITHMETIC_Max_NAME,                                              \
      "max",                                                                   \
      NATIVE_ARITHMETIC_Max_OPERATOR,                                          \
      NATIVE_ARITHMETIC_BINARY_INPUTS,                                         \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      true)                                                                    \
    X(Modulo,                                                                  \
      modulo,                                                                  \
      NATIVE_ARITHMETIC_Modulo_NAME,                                           \
      "modulo",                                                                \
      NATIVE_ARITHMETIC_Modulo_OPERATOR,                                       \
      NATIVE_ARITHMETIC_BINARY_INPUTS,                                         \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      true)                                                                    \
    X(Clamp,                                                                   \
      clamp,                                                                   \
      NATIVE_ARITHMETIC_Clamp_NAME,                                            \
      "clamp",                                                                 \
      NATIVE_ARITHMETIC_Clamp_OPERATOR,                                        \
      NATIVE_ARITHMETIC_CLAMP_INPUTS,                                          \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      true)                                                                    \
    X(Negate,                                                                  \
      negate,                                                                  \
      NATIVE_ARITHMETIC_Negate_NAME,                                           \
      "negate",                                                                \
      NATIVE_ARITHMETIC_Negate_OPERATOR,                                       \
      NATIVE_ARITHMETIC_UNARY_INPUTS,                                          \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      false)                                                                   \
    X(Abs,                                                                     \
      abs,                                                                     \
      NATIVE_ARITHMETIC_Abs_NAME,                                              \
      "abs",                                                                   \
      NATIVE_ARITHMETIC_Abs_OPERATOR,                                          \
      NATIVE_ARITHMETIC_UNARY_INPUTS,                                          \
      NATIVE_ARITHMETIC_OUTPUTS,                                               \
      false)                                                                   \
    X(Bool,                                                                    \
      literal_identity,                                                        \
      NATIVE_ARITHMETIC_Bool_NAME,                                             \
      "bool",                                                                  \
      NATIVE_ARITHMETIC_Bool_OPERATOR,                                         \
      NATIVE_ARITHMETIC_Bool_PORTS,                                            \
      NATIVE_ARITHMETIC_Bool_PORTS,                                            \
      false)                                                                   \
    X(Byte,                                                                    \
      literal_identity,                                                        \
      NATIVE_ARITHMETIC_Byte_NAME,                                             \
      "byte_value",                                                            \
      NATIVE_ARITHMETIC_Byte_OPERATOR,                                         \
      NATIVE_ARITHMETIC_Byte_PORTS,                                            \
      NATIVE_ARITHMETIC_Byte_PORTS,                                            \
      false)                                                                   \
    X(Integer,                                                                 \
      literal_identity,                                                        \
      NATIVE_ARITHMETIC_Integer_NAME,                                          \
      "integer",                                                               \
      NATIVE_ARITHMETIC_Integer_OPERATOR,                                      \
      NATIVE_ARITHMETIC_Integer_PORTS,                                         \
      NATIVE_ARITHMETIC_Integer_PORTS,                                         \
      false)                                                                   \
    X(Integer64,                                                               \
      literal_identity,                                                        \
      NATIVE_ARITHMETIC_Integer64_NAME,                                        \
      "integer64",                                                             \
      NATIVE_ARITHMETIC_Integer64_OPERATOR,                                    \
      NATIVE_ARITHMETIC_Integer64_PORTS,                                       \
      NATIVE_ARITHMETIC_Integer64_PORTS,                                       \
      false)                                                                   \
    X(Real,                                                                    \
      literal_identity,                                                        \
      NATIVE_ARITHMETIC_Real_NAME,                                             \
      "real",                                                                  \
      NATIVE_ARITHMETIC_Real_OPERATOR,                                         \
      NATIVE_ARITHMETIC_Real_PORTS,                                            \
      NATIVE_ARITHMETIC_Real_PORTS,                                            \
      false)                                                                   \
    X(Name,                                                                    \
      literal_identity,                                                        \
      NATIVE_ARITHMETIC_Name_NAME,                                             \
      "name",                                                                  \
      NATIVE_ARITHMETIC_Name_OPERATOR,                                         \
      NATIVE_ARITHMETIC_Name_PORTS,                                            \
      NATIVE_ARITHMETIC_Name_PORTS,                                            \
      false)                                                                   \
    X(String,                                                                  \
      literal_identity,                                                        \
      NATIVE_ARITHMETIC_String_NAME,                                           \
      "string",                                                                \
      NATIVE_ARITHMETIC_String_OPERATOR,                                       \
      NATIVE_ARITHMETIC_String_PORTS,                                          \
      NATIVE_ARITHMETIC_String_PORTS,                                          \
      false)
extern const native_definition_pack NATIVE_ARITHMETIC_DEFINITION_PACK;
#endif
