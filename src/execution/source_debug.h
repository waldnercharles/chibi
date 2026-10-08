#ifndef CHIBI_EXECUTION_SOURCE_DEBUG_H
#define CHIBI_EXECUTION_SOURCE_DEBUG_H
#include "types/value.h"

enum
{
    BEHAVIOR_SOURCE_SYMBOL,
    BEHAVIOR_SOURCE_FUNCTION,
    BEHAVIOR_SOURCE_EXPRESSION,
    BEHAVIOR_SOURCE_STATEMENT,
    BEHAVIOR_SOURCE_QUERY,
    BEHAVIOR_SOURCE_BINDING,
    BEHAVIOR_SOURCE_KIND_COUNT
};
typedef struct
{
    uint64_t occurrence, meaning;
    int start, end, line, column;
    int kind;
    bool completion;
} behavior_source_point;
typedef struct
{
    behavior_source_point origin;
    pin_value initial;
    int ordinal;
    bool output, reference;
} behavior_source_watch;
struct behavior_function_context;
/* Backends pass an already evaluated payload through context.watched. */
int behavior_source_pause(struct behavior_function_context *, int point);
void behavior_source_capture(struct behavior_function_context *, int watch);
#endif
