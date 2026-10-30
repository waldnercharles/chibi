#ifndef CHIBI_QUERY_VALUE_H
#define CHIBI_QUERY_VALUE_H
#include "types/value.h"

typedef enum
{
    PIN_QUERY_DESCRIPTION,
    PIN_QUERY_ROW,
} pin_query_kind;

/* Query descriptions capture values; rows keep their individually typed fields.
 * Both are identities in the owning program's managed heap. The allocation's
 * cleanup retains its immutable module generation independently of replacement. */
typedef struct pin_query
{
    pin_query_kind kind;
    arena *storage;
    const struct execution_module *module;
    int description;
    pin_value *values;
    int count;
} pin_query;
#endif
