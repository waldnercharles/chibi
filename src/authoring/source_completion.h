#ifndef CHIBI_AUTHORING_SOURCE_COMPLETION_H
#define CHIBI_AUTHORING_SOURCE_COMPLETION_H

#include "authoring/source_navigation.h"

typedef enum
{
    AUTHORING_COMPLETE_VARIABLE,
    AUTHORING_COMPLETE_CALLABLE,
    AUTHORING_COMPLETE_EVENT,
    AUTHORING_COMPLETE_TYPE,
    AUTHORING_COMPLETE_FIELD,
    AUTHORING_COMPLETE_KEYWORD,
} authoring_completion_kind;
typedef struct
{
    authoring_target target; /* Zero ID for a grammar keyword. */
    char *label, *detail, *insert;
    authoring_completion_kind kind;
    script_type type;
} authoring_completion;
typedef struct
{
    script_range range; /* Complete token replacement range, UTF-8 byte offsets. */
    authoring_completion *items;
    int count;
} authoring_completions;

void authoring_completions_free(authoring_completions *);

#endif
