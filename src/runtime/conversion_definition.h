#ifndef CHIBI_NATIVE_CONVERSION_DEFINITION_H
#define CHIBI_NATIVE_CONVERSION_DEFINITION_H
#include "runtime/native_definitions.h"
#define NATIVE_CONVERSION_PORT(X, NAME, TYPE, TYPE_NAME)                       \
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
#define NATIVE_CONVERSION_SCALARS(X)                                                                   \
    X(To_String, "To String", "to_string", PIN_STR, "const char *", "String", PIN_STR, "const char *") \
    X(To_Name, "To Name", "to_name", PIN_STR, "const char *", "Name", PIN_NAME, "name")                \
    X(To_Real, "To Real", "to_real", PIN_I32, "int", "Real", PIN_F32, "number")                        \
    X(To_Byte, "To Byte", "to_byte", PIN_F64, "f64", "Byte", PIN_U8, "u8")                             \
    X(To_Integer, "To Integer", "to_integer", PIN_F64, "f64", "Integer", PIN_I32, "int")               \
    X(To_Integer64, "To Integer64", "to_integer64", PIN_F64, "f64", "Integer64", PIN_I64, "i64")
#define NATIVE_CONVERSION_VECTORS(X)                                                                               \
    X(vec2, 2, "Scalar to vec2", "scalar_to_vec2", "Builds a vector with x and y both set to Value, as floats.")   \
    X(vec3, 3, "Scalar to vec3", "scalar_to_vec3", "Builds a vector with x, y and z all set to Value, as floats.") \
    X(ShaderVec2,                                                                                                  \
      2,                                                                                                           \
      "Scalar to ShaderVec2",                                                                                      \
      "scalar_to_shader_vec2",                                                                                     \
      "Builds a shader vector with x and y both set to Value.")                                                    \
    X(ShaderVec3,                                                                                                  \
      3,                                                                                                           \
      "Scalar to ShaderVec3",                                                                                      \
      "scalar_to_shader_vec3",                                                                                     \
      "Builds a shader vector with x, y and z all set to Value.")                                                  \
    X(ShaderVec4,                                                                                                  \
      4,                                                                                                           \
      "Scalar to ShaderVec4",                                                                                      \
      "scalar_to_shader_vec4",                                                                                     \
      "Builds a shader vector with x, y, z and w all set to Value.")
extern const native_definition_pack NATIVE_CONVERSION_DEFINITION_PACK;
#endif
