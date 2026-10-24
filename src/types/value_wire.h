#ifndef CHIBI_VALUE_WIRE_H
#define CHIBI_VALUE_WIRE_H
#include "support/arena.h"
#include "types/value.h"

/* Bounded field-wise format helpers. Integers are variable-length. Writer bytes
 * use support/array.h; reader bytes are borrowed. Decoded strings and aggregates belong to storage. Errors
 * are sticky; callers discard the whole generation on failure. */
enum
{
    EXECUTION_WIRE_MAX_BYTES = 64 * 1024 * 1024,
    EXECUTION_WIRE_MAX_RECORDS = 1000000,
    EXECUTION_WIRE_MAX_DEPTH = 64
};
typedef struct execution_wire
{
    unsigned char *data;
    size_t size, offset;
    bool reading;
    arena *storage;
    const char *error;
    size_t owned_bytes, records;
    /* Decoded storage bound; 0 is EXECUTION_WIRE_MAX_BYTES. */
    size_t owned_limit;
    /* Optional shared remaining allocation budget. Arena charges include new
     * block capacity/headers; caller-owned allocations use reserve(). */
    size_t *storage_budget;
    size_t storage_charged;
    /* Optional writer hash sink receives exact encoded bytes without a buffer. */
    uint64_t *hash;
} execution_wire;
void execution_wire_u64(execution_wire *, uint64_t *);
void execution_wire_int(execution_wire *, int *);
void execution_wire_size(execution_wire *, size_t *);
void execution_wire_bool(execution_wire *, bool *);
/* NUL-free metadata/name text only; PIN_STR values use length-prefixed bytes. */
void execution_wire_string(execution_wire *, const char **);
/* minimum_fields bounds a decoded count by the bytes left: each field is at least one. */
void execution_wire_count(execution_wire *, int *, size_t minimum_fields);
/* Struct names/layouts must resolve in the supplied global type catalog. No
 * process pointer is encoded; HANDLE values must be zero. Collections preserve
 * ordered items. Maps encode full typed key/value headers and ordered pairs.
 * Failed reads leave the destination value unchanged; partial decoded storage
 * belongs to the reader's arena and is discarded with its generation. */
void execution_wire_value(execution_wire *, pin_value *);
/* Explicit execution-prepared variant. Each recursive value carries an authored
 * discriminator; resolve validates original writer headers or supplies an owned
 * reader layout. A missing authored layout is always an error, never fallback.
 * Native callers keep the historical entry point/bytes above. */
typedef const pin_record
    *(*execution_wire_resolver)(execution_wire *, void *, const pin_value *, bool authored);
void execution_wire_value_owned(execution_wire *, pin_value *, execution_wire_resolver, void *user);
/* Float bits preserve IEEE F32 values; owning codecs validate semantic ranges.
 * Heap allocations use free(), strings are copied from temporary wire storage.
 * Reserve charges caller-owned array payload before growth. Errors are sticky. */
/* Length-prefixed raw bytes; reader span is borrowed after bounds checking. */
void execution_wire_blob(execution_wire *, const unsigned char **, size_t *);
void execution_wire_float(execution_wire *, float *);
bool execution_wire_reserve(execution_wire *, size_t);
void *execution_wire_allocate(execution_wire *, size_t);
void *execution_wire_allocate_owned(execution_wire *, size_t);
void execution_wire_owned_string(execution_wire *, char **);
#endif
