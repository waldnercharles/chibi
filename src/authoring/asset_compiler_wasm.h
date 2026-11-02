#ifndef CHIBI_ASSET_COMPILER_WASM_H
#define CHIBI_ASSET_COMPILER_WASM_H

#include "authoring/asset_compiler_host.h"

/* The LLVM-compiled authored browser compiler is linked into this module.
 * Input/options are borrowed synchronously; returned closure bytes belong to
 * the caller. Each call owns and releases every native temporary allocation. */
unsigned char *asset_compiler_wasm_compile(
    const char *request,
    size_t request_length,
    size_t *length,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
);

/* Exactly 64 hexadecimal bytes and a NUL, immutable for this linked artifact.
 * Generated from the compiled compiler/runtime payload before archiving. */
const char *asset_compiler_wasm_identity(void);

#ifdef CHIBI_ASSET_COMPILER_WASM_TEST
/* Counts temporary native allocations across real compilation calls. */
size_t asset_compiler_wasm_test_allocations(void);
#endif

#endif
