#ifndef CHIBI_EXECUTION_ARTIFACT_H
#define CHIBI_EXECUTION_ARTIFACT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* Backend-owned code belongs to the immutable definition. Inspection code is
 * compiled on first inspection; its atomic state publishes code/maps together.
 * Activations retain their owning definition. */
typedef struct
{
    bool compilation_attempted, compiled;
    atomic_uint inspection_state;
    void *code, *system_code, *dispatch, *inspection_code;
    uintptr_t *resume_addresses, *inspection_addresses;
    struct execution_wasm_jit *wasm_jit;
} execution_artifact;

execution_artifact *execution_artifact_new();
void execution_artifact_free(execution_artifact *artifact);

#endif
