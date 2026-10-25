#ifndef CHIBI_TOOLCHAIN_COMPILER_JSON_H
#define CHIBI_TOOLCHAIN_COMPILER_JSON_H

#include "runtime/native_definitions.h"

/* Private external codec bindings, composed explicitly in compiler hosts.
 * Both return valid, value and byte_length; strings preserve every decoded
 * byte including NUL. compiler_json_string checks Unicode for metadata;
 * compiler_json_value_string admits arbitrary value bytes like stage zero.
 * Metadata callers must separately reject NUL at their C-string boundaries.
 * Invalid JSON and nonstrings return false, empty text and zero length.
 * Invocation value storage owns the decoded descriptors and bytes. */
extern const native_definition_pack COMPILER_JSON_DEFINITION_PACK;

#endif
