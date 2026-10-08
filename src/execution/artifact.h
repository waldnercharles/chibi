#ifndef CHIBI_EXECUTION_ARTIFACT_H
#define CHIBI_EXECUTION_ARTIFACT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct behavior_expression_wasm behavior_expression_wasm;
typedef struct behavior_expression_wasm_region behavior_expression_wasm_region;

/* Backend-owned machine code and address maps are compiled once and owned by
 * the immutable definition. Activations retain their owning definition. */
typedef struct
{
    bool compilation_attempted, compiled;
    void *code, *system_code, *dispatch, *inspection_code;
    uintptr_t *resume_addresses, *inspection_addresses;
    behavior_expression_wasm *wasm;
    behavior_expression_wasm_region *wasm_regions;
} execution_artifact;

execution_artifact *execution_artifact_new();
void execution_artifact_free(execution_artifact *artifact);

#endif
