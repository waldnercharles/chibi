#ifndef CHIBI_TOOLCHAIN_COMPILER_SYNTAX_H
#define CHIBI_TOOLCHAIN_COMPILER_SYNTAX_H

#include "runtime/native_definitions.h"

/* Private external parser bridge. compiler_syntax_parse(text) returns root and
 * aligned arrays kinds, starts, ends, rows, columns, parents, first_children,
 * next_siblings, errors, missing, has_errors, fields, is_named and is_extra.
 * Flags come directly from Tree-sitter. Every public node is retained, including
 * unnamed tokens, extras and ERROR descendants. Positions
 * count zero-based source bytes; absent links are -1 and absent fields are "".
 * All arrays and strings belong to invocation value storage. No Tree-sitter
 * pointer escapes. Diagnostic selection remains ordinary authored policy;
 * the generated grammar and Chibi-owned scanner are still stage zero. */
extern const native_definition_pack COMPILER_SYNTAX_DEFINITION_PACK;

#endif
