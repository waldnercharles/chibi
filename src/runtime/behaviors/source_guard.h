#ifndef CHIBI_SOURCE_GUARD_H
#define CHIBI_SOURCE_GUARD_H

#include "runtime/behaviors/source.h"

typedef struct
{
    const char *name;
    const execution_module *module;
} source_module_view;

/* Optional host contract at the final registration boundary. Views borrow
 * immutable modules for the callback only. Callbacks must not dispatch or
 * mutate the registry, ECS, services or source publication. The user object
 * outlives the registry. Validation is read-only and may reject; published is
 * notification of an already committed, previously validated generation. */
typedef struct
{
    void *user;
    bool (*validate)(
        void *,
        const source_module_view *current,
        int current_count,
        const source_module_view *candidate,
        int candidate_count,
        char *error,
        size_t size
    );
    void (*published)(void *, const source_module_view *, int);
} source_publication_guard;

/* Install once at a quiescent boundary. Validates the current registrations
 * first; rejection changes nothing. Reinstalling the identical guard is a
 * no-op. A different guard cannot replace an installed contract. */
bool source_set_publication_guard(source_registry *, source_publication_guard, char *error, size_t size);

#endif
