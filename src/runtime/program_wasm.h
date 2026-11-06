#ifndef CHIBI_RUNTIME_PROGRAM_WASM_H
#define CHIBI_RUNTIME_PROGRAM_WASM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    /* Zero selects four billion native CHECK boundaries. The authored entry
     * arms the scope around the work to which that budget applies. */
    uint64_t budget;
    bool (*cancelled)(void *user);
    void *user;
    /* Zero selects 64 MiB of input or 16 MiB of output. */
    size_t max_request_bytes, max_output_bytes;
} program_wasm_options;

typedef enum
{
    PROGRAM_WASM_NONE,
    PROGRAM_WASM_REJECTED,
    PROGRAM_WASM_ENTRY_FAILED,
    PROGRAM_WASM_REQUEST_MISSING,
    PROGRAM_WASM_REQUEST_LIMIT,
    PROGRAM_WASM_OUTPUT_LIMIT,
    PROGRAM_WASM_REENTRANT,
    PROGRAM_WASM_BUDGET,
    PROGRAM_WASM_CANCELLED,
    PROGRAM_WASM_NATIVE_FAILURE,
} program_wasm_failure;

typedef struct
{
    program_wasm_failure kind;
    int document, line, column;
    char message[256];
} program_wasm_diagnostic;

/* Calls the linked LLVM entry chibi_scalar synchronously, with borrowed input
 * and options. The returned bytes belong to the caller and are freed with
 * free(). Every temporary native allocation is released before returning,
 * including after quota exhaustion, cancellation and authored failures.
 * One call runs per browser thread; a nested call fails without changing the
 * active call. Output length is zero on failure. */
unsigned char *program_wasm_run(
    const char *request,
    size_t request_length,
    size_t *length,
    const program_wasm_options *options,
    program_wasm_diagnostic *diagnostic
);

/* The authored entry borrows these buffers only for each foreign call. The
 * published output and rejection message are copied before returning. */
int32_t chibi_program_request_length(void);
int32_t chibi_program_request_copy(unsigned char *buffer, int32_t count);
uint64_t chibi_program_budget(void);
int32_t chibi_program_publish(const unsigned char *bytes, int32_t count);
int32_t chibi_program_rejected(int32_t document, int32_t line, int32_t column, const char *message);

/* These names are the compiler's quota-scope ABI. Keeping them makes the
 * compiler emit CHECK boundaries in the authored call closure. */
int32_t chibi_native_scope_begin(uint64_t steps);
int32_t chibi_native_scope_end(void);

#ifdef CHIBI_PROGRAM_WASM_TEST
size_t program_wasm_test_allocations(void);
#endif

#endif
