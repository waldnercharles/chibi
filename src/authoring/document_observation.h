#ifndef CHIBI_DOCUMENT_OBSERVATION_H
#define CHIBI_DOCUMENT_OBSERVATION_H
#include "authoring/document_semantics.h"
typedef struct
{
    uint64_t module, function, occurrence, fallback_occurrence;
    int index, fallback_index;
    bool output, fallback_output, has_fallback;
} document_observation;
/* Maps a source port to captured execution keys. Reads meaning only; never
 * evaluates a producer. Input ports may also have an expression-output fallback. */
bool document_observation_query(
    const document_semantics *,
    uint64_t graph,
    uint64_t node,
    uint64_t pin,
    bool output,
    document_observation *,
    char *error,
    size_t
);
typedef struct
{
    uint64_t module, function, occurrence;
} document_execution;
/* Canonical source node keys for joining retained execution activity. This
 * only maps authored meaning; callers must separately match frozen source,
 * retained runtime generation and owner before presenting activity. */
bool document_execution_query(
    const document_semantics *,
    uint64_t graph,
    uint64_t node,
    document_execution *,
    char *error,
    size_t
);
#endif
