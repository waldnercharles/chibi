#ifndef CHIBI_TOOLCHAIN_COMPILER_JSON_H
#define CHIBI_TOOLCHAIN_COMPILER_JSON_H

#include "runtime/native_definitions.h"

/* The retired JSON string natives, declared in compiler hosts so the native
 * list the prepared artifacts record stays the same. Every call fails; the
 * decoder is tools/compiler/json_string. */
extern const native_definition_pack COMPILER_JSON_DEFINITION_PACK;

#endif
