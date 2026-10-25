#ifndef CHIBI_LANGUAGE_NUMERIC_H
#define CHIBI_LANGUAGE_NUMERIC_H

/* Exact integer and rational constants for compile-time evaluation. */
#include "language/semantic.h"

#include <stddef.h>
#include <stdint.h>

typedef struct script_number script_number;
typedef struct script_numeric script_numeric;
typedef enum
{
    SCRIPT_NUMBER_INTEGER,
    SCRIPT_NUMBER_REAL,
} script_number_kind;
typedef enum
{
    SCRIPT_NUMERIC_OK,
    SCRIPT_NUMERIC_LITERAL,
    SCRIPT_NUMERIC_KIND,
    SCRIPT_NUMERIC_OPERATOR,
    SCRIPT_NUMERIC_ZERO_DIVISOR,
    SCRIPT_NUMERIC_NEGATIVE_SHIFT,
    SCRIPT_NUMERIC_RANGE,
    SCRIPT_NUMERIC_FRACTION,
    SCRIPT_NUMERIC_PRECISION,
    SCRIPT_NUMERIC_BITS,
    SCRIPT_NUMERIC_SHIFT_LIMIT,
    SCRIPT_NUMERIC_DEPTH,
    SCRIPT_NUMERIC_STEPS,
    SCRIPT_NUMERIC_WORK,
    SCRIPT_NUMERIC_STORAGE,
} script_numeric_code;
typedef struct
{
    script_numeric_code code;
    uint64_t needed, limit; /* needed == 0 when no exact requirement is available. */
} script_numeric_error;
enum
{
    SCRIPT_NUMERIC_MAX_BITS = 4096,
    SCRIPT_NUMERIC_MAX_SHIFT = 4096,
    SCRIPT_NUMERIC_MAX_DEPTH = 256,
    SCRIPT_NUMERIC_MAX_STEPS = 65536,
    SCRIPT_NUMERIC_MAX_WORK = 16777216,
    SCRIPT_NUMERIC_MAX_STORAGE = 1048576,
};
typedef struct
{
    unsigned steps;
    uint64_t work;
    size_t storage;
} script_numeric_limits;
typedef struct
{
    unsigned steps_remaining;
    uint64_t work_remaining;
    size_t retained_bytes;
    unsigned published_values;
} script_numeric_usage;

/* NULL selects fixed defaults. Explicit limits may only reduce them, for
 * deterministic private fixtures; these are not source-language options.
 * Allocation and all arithmetic scratch are charged before construction. */
script_numeric *script_numeric_new(const script_numeric_limits *, script_numeric_error *);
void script_numeric_free(script_numeric *);
script_numeric_usage script_numeric_usage_read(const script_numeric *);
/* Future dependency/proof/cache walks must charge every edge and validate
 * actual depth here. Operations also charge a visit, conservatively. */
bool script_numeric_visit(script_numeric *, unsigned depth, script_numeric_error *);

/* Caller classifies existing grammar tokens; no signs/suffixes/whitespace.
 * A leading sign belongs to unary evaluation. All returned facts belong to
 * this owner until free; all input facts must have the same owner. */
const script_number *script_number_read(
    script_numeric *,
    const char *,
    size_t,
    script_number_kind,
    script_numeric_error *
);
/* An integer scalar's exact value. */
const script_number *script_number_scalar(script_numeric *, pin_value, script_numeric_error *);
script_number_kind script_number_kind_read(const script_number *);
const script_number *script_number_unary(
    script_numeric *,
    script_operator,
    const script_number *,
    script_numeric_error *
);
const script_number *script_number_binary(
    script_numeric *,
    script_operator,
    const script_number *,
    const script_number *,
    script_numeric_error *
);
bool script_number_compare(
    script_numeric *,
    const script_number *,
    const script_number *,
    int *,
    script_numeric_error *
);
/* Contextual checked conversion only. This is not an explicit-cast policy.
 * On failure, out is untouched; a range/precision failure does not mutate
 * the exact fact or poison later uses. Budget exhaustion is sticky. */
bool script_number_materialize(script_numeric *, const script_number *, pin_type, pin_value *, script_numeric_error *);
#endif
