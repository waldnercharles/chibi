#ifndef CHIBI_LANGUAGE_IDENTITY_H
#define CHIBI_LANGUAGE_IDENTITY_H
#include "language/semantic.h"

typedef enum
{
    SCRIPT_RECORD_SYMBOL,
    SCRIPT_RECORD_FUNCTION,
    SCRIPT_RECORD_EXPRESSION,
    SCRIPT_RECORD_STATEMENT,
    SCRIPT_RECORD_QUERY,
    SCRIPT_RECORD_BINDING
} script_record_kind;

/* The source occurrences of an analysis's records. An occurrence names a
 * record by its source document, its kind and its range counted in bytes that
 * are not whitespace, a symbol by its declaration. Whitespace-only formatting
 * keeps it, and any compiler that reads the same source computes the same one
 * (script_occurrence). Records of one kind with one range share it. Invalid
 * records in incomplete drafts return zero. */
typedef struct script_occurrences script_occurrences;
script_occurrences *script_occurrences_new(const script_analysis *);
void script_occurrences_free(script_occurrences *);
uint64_t script_record_id(script_occurrences *, script_record_kind, int record);
/* The occurrence of a record of `kind` in the document `key` whose range
 * starts after `start` and ends after `end` bytes that are not whitespace. */
uint64_t script_occurrence(const char *key, script_record_kind kind, int start, int end);
uint64_t script_record_meaning(const script_analysis *, script_record_kind, int record);
#endif
