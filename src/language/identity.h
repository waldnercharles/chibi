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

/* Occurrences survive formatting. Structural edits reconcile changed record
 * ordinals explicitly. Invalid records in incomplete drafts return zero. */
uint64_t script_record_id(const script_analysis *, script_record_kind, int record);
uint64_t script_record_meaning(const script_analysis *, script_record_kind, int record);
#endif
