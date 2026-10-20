#ifndef CHIBI_TOOLCHAIN_COMPILER_JSON_H
#define CHIBI_TOOLCHAIN_COMPILER_JSON_H

#include "runtime/native_definitions.h"

/* Private external codec binding; compose explicitly in compiler hosts.
 * compiler_json_string(literal) has named results valid, value, byte_length.
 * Its context callback uses invocation value storage and preserves the three
 * caller-supplied result headers; it requires no host service.
 * Valid JSON strings retain their full decoded byte length, including NULs.
 * Ordinary engine string operations see the C-string prefix; callers must
 * compare its length before treating the value as a complete engine string.
 * Invalid JSON and nonstrings return false, an empty string and zero length. */
extern const native_definition_pack COMPILER_JSON_DEFINITION_PACK;

#endif
