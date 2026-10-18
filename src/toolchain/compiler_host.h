#ifndef CHIBI_TOOLCHAIN_COMPILER_HOST_H
#define CHIBI_TOOLCHAIN_COMPILER_HOST_H

#include "execution/backend.h"
#include "execution/function.h"

#define COMPILER_HOST_MAX_BYTES (64u * 1024u * 1024u)
#define COMPILER_HOST_MAX_VALUES 1000000
#define COMPILER_HOST_MAX_DEPTH 64
#define COMPILER_HOST_MAX_BATCH 64

typedef struct compiler_host compiler_host;
typedef struct
{
    char *json; /* malloc-owned, byte-preserving JSON; release with free(). */
    size_t length;
    int flow_status;
    behavior_run_counts counts;
    char diagnostic[256];
} compiler_host_result;

typedef struct
{
    unsigned char *bytes; /* malloc-owned; release with free(). */
    size_t length;
    int flow_status;
    behavior_run_counts counts;
    char diagnostic[256];
} compiler_host_bytes;

/* Private runtime-only host. Loads a closed target-compatible prepared closure;
 * no source compilation/discovery occurs. Artifact bytes may retire on return.
 * Only text, arrays and the external JSON/raw-syntax bridges are admitted. */
compiler_host *compiler_host_load(const void *, size_t, behavior_backend, char *error, size_t error_size);
void compiler_host_free(compiler_host *);
/* Explicit entry ID in the artifact's root module. Arguments are a JSON array,
 * packed against decoded declarations. Address/reference argument and result
 * banks reject before JSON decoding; only owned-value contracts are admitted. Invalid UTF-8 bytes are preserved by the
 * private transport, including rejected compiler inputs. Integers retain full
 * uint64 precision; union results project as {tag,values}, not source metadata.
 * Each call owns its result JSON. Instruction budget is finite/nonzero; the
 * supervising parent supplies the hard wall deadline (also during load/JIT). */
bool compiler_host_invoke(
    compiler_host *,
    uint64_t entry,
    const char *json,
    size_t length,
    uintptr_t budget,
    compiler_host_result *
);

/* Exactly one owned primitive byte[] result, copied before runtime teardown.
 * Input and instruction-budget rules match invoke. max_bytes must be in
 * [1, COMPILER_HOST_MAX_VALUES]. Empty success is {NULL, 0}; every failure also
 * has null/zero output, with a diagnostic and actual execution counts/status.
 * This bounds represented output storage, not all runtime arena allocations. */
bool compiler_host_invoke_bytes(
    compiler_host *,
    uint64_t entry,
    const char *json,
    size_t length,
    uintptr_t budget,
    size_t max_bytes,
    compiler_host_bytes *
);

/* Outer argument array, one same-entry invocation per item; atomic output on
 * failure, shared transport quotas and aggregate actual execution counters. */
bool compiler_host_invoke_batch(
    compiler_host *,
    uint64_t entry,
    const char *json,
    size_t length,
    uintptr_t budget,
    compiler_host_result *
);

#endif
