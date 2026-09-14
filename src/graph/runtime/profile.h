#ifndef CHIBI_BEHAVIOR_PROFILE_H
#define CHIBI_BEHAVIOR_PROFILE_H

#include <stdint.h>

/* Thread-local nesting makes setup and execution intervals disjoint, including
 * synchronously dispatched events on another instance. */
typedef struct behavior_profile_scope
{
    struct behavior_profile_scope *parent;
    uint64_t start, children;
} behavior_profile_scope;

void behavior_profile_enter(behavior_profile_scope *scope);
uint64_t behavior_profile_leave(behavior_profile_scope *scope);

#endif
