#ifndef CHIBI_EXECUTION_PREPARATION_PROFILE_H
#define CHIBI_EXECUTION_PREPARATION_PROFILE_H
#include <stddef.h>

typedef enum
{
    EXECUTION_PREPARATION_GAME,
    EXECUTION_PREPARATION_AUTHORED_TOOL
} execution_preparation_profile;

/* Trusted caller policy for retained canonical storage, never artifact data.
 * Encoded input and individual module limits remain 64 MiB. Scratch, generated
 * backend code, mutable simulation state and graphics are separate allocations. */
static inline size_t execution_preparation_budget(execution_preparation_profile profile)
{
    switch (profile) {
        case EXECUTION_PREPARATION_GAME: return 64u * 1024u * 1024u;
        case EXECUTION_PREPARATION_AUTHORED_TOOL: return 256u * 1024u * 1024u;
    }
    return 0;
}
#endif
