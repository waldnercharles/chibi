#ifndef CHIBI_EXECUTION_PREPARED_H
#define CHIBI_EXECUTION_PREPARED_H

/* Payload and semantic changes still require a version change even when the
 * stable instruction names and ordinals (checked separately) are unchanged. */
#define EXECUTION_PREPARED_VERSION 28
#include "execution/host.h"
#include "execution/module.h"
#include "types/type.h"

/* Prepared files contain canonical executable definitions, stable capability
 * and type identities, and compatibility requirements. Linking supplies native
 * implementations; no parser, compiler, process address or backend code is saved. */
typedef struct
{
    const node_host_call *call;
    const node_access *access;
    uint64_t abi;
} execution_prepared_host;
typedef struct
{
    const node_command *command;
    uint64_t abi;
} execution_prepared_command;
typedef struct
{
    node_query_fn read;
    const char *service;
    uint64_t abi;
} execution_prepared_query;
typedef struct
{
    const char *name;
    const type_desc *type; /* Reflected layout; managed values also use runtime type catalog. */
    pin_type storage;
    size_t size;
    uint64_t layout;
} execution_prepared_type;
typedef struct
{
    void *user;
    bool (*host)(void *, uint64_t id, execution_prepared_host *);
    bool (*command)(void *, uint64_t id, const char *reference, execution_prepared_command *);
    bool (*query)(void *, uint64_t id, execution_prepared_query *);
    /* When supplied, validates every declared event. External command event
     * requirements always require this callback. */
    bool (*event)(void *, uint64_t id, uint64_t abi);
    bool (*type)(void *, uint64_t id, execution_prepared_type *);
    /* Returns a retained immutable dependency matching the saved fingerprint. */
    execution_module *(*import)(void *, uint64_t id, uint64_t fingerprint);
    /* Optional bounds for each module read: its decoded storage, its decoded
     * records and its record-layout validation work. Zero keeps the defaults. */
    size_t decoded_bytes, decoded_records, layout_work;
} execution_prepared_linker;
/* The header's target ABI: this build's context layout and instruction names at
 * their ordinals. A producer outside C writes it as supplied. */
uint64_t execution_prepared_abi();
/* The instruction names that ABI hashes, in ordinal order. */
int execution_prepared_instruction_count();
const char *execution_prepared_instruction_name(int ordinal);
/* The caller frees the returned byte buffer with free(). Failure produces no
 * buffer. The source module stays immutable and retains its normal ownership. */
bool execution_prepared_write(
    const execution_module *,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
);
/* Serialized length and the FNV-1a fingerprint import checks use, without
 * materializing bytes. Equal results identify the same module generation. */
bool execution_prepared_measure(
    const execution_module *,
    size_t *length,
    uint64_t *fingerprint,
    char *error,
    size_t size
);
/* The reader owns all decoded payloads. Failure releases partial definitions
 * and retained dependencies. Built-in runtime intrinsics resolve automatically.
 * Unknown versions, missing identities, ABI/layout mismatches and malformed
 * input fail before publication; backend code is freshly generated. */
execution_module *execution_prepared_read(
    const void *data,
    size_t length,
    const execution_prepared_linker *,
    behavior_backend,
    char *error,
    size_t size
);
/* Shared canonical decoded-storage budget. Decode allocations are reserved
 * before growth; finalizer liveness/suspension storage also has a preflight.
 * Success charges the finalized module's actual owned canonical capacity;
 * failure leaves remaining unchanged. Imports are charged once by the owning
 * package, backend compiler/code caches and live instance banks separately. */
execution_module *execution_prepared_read_budget(
    const void *data,
    size_t length,
    const execution_prepared_linker *,
    behavior_backend,
    size_t *remaining,
    char *error,
    size_t size
);
#endif
