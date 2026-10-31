#ifndef CHIBI_TOOLCHAIN_COMPILER_SYNTAX_H
#define CHIBI_TOOLCHAIN_COMPILER_SYNTAX_H

#include "runtime/native_definitions.h"

/* The retired compiler_syntax_parse(text) declaration. It keeps its place in the
 * native list, and so in the prepared ABI, but always fails; the compiler reads
 * syntax with tools/compiler/source_parser. */
extern const native_definition_pack COMPILER_SYNTAX_DEFINITION_PACK;

#endif
