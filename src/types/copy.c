#include "types/copy_internal.h"

#include <assert.h>

_Thread_local pin_copy_scope *pin_copy_current;

void pin_copy_scope_begin(pin_copy_scope *scope)
{
    assert(scope && !scope->active);
    scope->previous = pin_copy_current;
    scope->active = true;
    pin_copy_current = scope;
}

void pin_copy_scope_end(pin_copy_scope *scope)
{
    assert(scope && scope->active && pin_copy_current == scope);
    pin_copy_current = scope->previous;
    scope->previous = NULL;
    scope->active = false;
}

void pin_copy_scope_reset(pin_copy_scope *scope)
{
    assert(scope && !scope->active);
    scope->counts = (pin_copy_counts){ 0 };
}
