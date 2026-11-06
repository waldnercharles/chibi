#ifndef CHIBI_TOOLCHAIN_COMPILER_HOST_H
#define CHIBI_TOOLCHAIN_COMPILER_HOST_H

#include "execution/backend.h"
#include "execution/function.h"

#define COMPILER_HOST_MAX_BYTES (64u * 1024u * 1024u)
/* A compiler's own closure is a large program, in and out. The package
 * compiler's module decodes to about 80 MB with its verification scratch and
 * needs about 113 M units of record-layout work. Loading allows each module,
 * and the closure together, this much storage, and each module this much
 * layout work. Its closure, 6.2 MB, is returned as a byte[] of that length;
 * a byte result may be COMPILER_HOST_MAX_OUTPUT_BYTES long. JSON invocations
 * keep COMPILER_HOST_MAX_BYTES. */
#define COMPILER_HOST_MAX_CLOSURE_BYTES (256u * 1024u * 1024u)
#define COMPILER_HOST_MAX_LAYOUT_WORK (256u * 1024u * 1024u)
#define COMPILER_HOST_MAX_OUTPUT_BYTES (16u * 1024u * 1024u)
#define COMPILER_HOST_MAX_VALUES 1000000
#define COMPILER_HOST_MAX_DEPTH 64
#define COMPILER_HOST_MAX_BATCH 64

typedef struct compiler_host compiler_host;
/* Diagnostic identity copies at most 511 bytes and reports truncation. Source
 * positions are the authored compiler's byte ranges and one-based coordinates. */
typedef struct
{
    bool present, truncated;
    int start, end, line, column;
    char asset[512];
} compiler_host_source;
typedef struct
{
    char *json; /* malloc-owned, byte-preserving JSON; release with free(). */
    size_t length;
    int flow_status;
    behavior_run_counts counts;
    char diagnostic[256];
    compiler_host_source source;
} compiler_host_result;

typedef struct
{
    unsigned char *bytes; /* malloc-owned; release with free(). */
    size_t length;
    int flow_status;
    behavior_run_counts counts;
    char diagnostic[256];
    compiler_host_source source;
} compiler_host_bytes;

/* Private runtime-only host. Loads a closed target-compatible prepared closure;
 * no source compilation/discovery occurs. Artifact bytes may retire on return.
 * Only text, arrays and the external JSON/raw-syntax bridges are admitted. */
compiler_host *compiler_host_load(const void *, size_t, behavior_backend, char *error, size_t error_size);
/* Pure value profile links only text/array capabilities. Raw syntax and JSON
 * parsing remain capabilities of the separately loaded compiler artifact. */
compiler_host *compiler_host_load_values(const void *, size_t, behavior_backend, char *, size_t);
void compiler_host_free(compiler_host *);
/* Resolve an owned-value callable without executing it. Requires the specified
 * argument count and exactly one primitive byte[] result. */
bool compiler_host_accepts_byte_entry(compiler_host *, uint64_t entry, int arguments, char *, size_t);
/* Compile's last six arguments for this host, which a compile for it takes:
 * the value layout, prepared ABI, instruction names, the natives it may call
 * (data functions with no flow, outcomes or suspension), their ports, and each
 * native's identity and signature ABI. Malloc-owned JSON; release with free(). */
char *compiler_host_target_json(const compiler_host *, size_t *length);
char *compiler_host_values_target_json(size_t *length, char *, size_t);
char *compiler_host_compiler_target_json(size_t *length, char *, size_t);
/* The entry ID of a package symbol, such as
 * "project:package_compiler:1:Compile:callable". */
uint64_t compiler_host_entry(const char *symbol);
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
 * [1, COMPILER_HOST_MAX_OUTPUT_BYTES]. Empty success is {NULL, 0}; every failure also
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
