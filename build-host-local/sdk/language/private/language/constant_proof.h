#ifndef CHIBI_LANGUAGE_CONSTANT_PROOF_H
#define CHIBI_LANGUAGE_CONSTANT_PROOF_H

#include "language/semantic.h"

#define SCRIPT_MAX_DEPTH 256

typedef struct
{
    unsigned generation;
    int depth;
} constant_proof;

/* Internal analysis facts and references retain their analyzer invariants.
 * The caller supplies one cache entry per expression and a nonzero generation
 * distinct from the previous query. NULL steps disables visit accounting.
 * Successful cache entries retain the exact depth at which they were proved. */
bool script_constant_prove(
    const script_analysis *,
    constant_proof *,
    unsigned generation,
    int expression,
    int *steps,
    bool scalar
);

#endif
