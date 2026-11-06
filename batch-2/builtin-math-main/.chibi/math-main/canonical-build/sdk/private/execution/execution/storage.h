#pragma once
#include "execution/function.h"

#include <stddef.h>

/* Canonical definition allocations including owned child definitions and fast
 * definitions. Borrowed call/import targets and backend artifact/code caches
 * are excluded. SIZE_MAX reports overflow. No activation storage is counted. */
size_t behavior_function_storage_bytes(const behavior_function *);
