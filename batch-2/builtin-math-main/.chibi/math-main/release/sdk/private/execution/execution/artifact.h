#ifndef CHIBI_EXECUTION_ARTIFACT_H
#define CHIBI_EXECUTION_ARTIFACT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

enum
{
    EXECUTION_INSPECTION_UNCOMPILED = 0,
    EXECUTION_INSPECTION_COMPILING = 1,
    EXECUTION_INSPECTION_COMPILED = 2,
    EXECUTION_INSPECTION_FAILED = 3,
};

/* Backend-owned code belongs to the immutable definition. Inspection code is
 * compiled on first inspection; its atomic state publishes code/maps together.
 * Activations retain their owning definition. */
typedef struct
{
    bool compilation_attempted, compiled;
    /* Compiled, but runs on bytecode: the wasm JIT declines a definition past
     * its size budget. */
    bool interpreted;
    atomic_uint inspection_state, scoped_state;
    void *code, *system_code, *dispatch, *inspection_code, *scoped_code;
    uintptr_t *resume_addresses, *inspection_addresses, *scoped_addresses;
    struct execution_wasm_jit *wasm_jit;
} execution_artifact;

execution_artifact *execution_artifact_new();
void execution_artifact_free(execution_artifact *artifact);

#endif
