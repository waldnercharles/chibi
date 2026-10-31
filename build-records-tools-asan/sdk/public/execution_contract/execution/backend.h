#ifndef CHIBI_EXECUTION_BACKEND_H
#define CHIBI_EXECUTION_BACKEND_H
#include <stdbool.h>

typedef enum
{
    BEHAVIOR_UNAVAILABLE,
    BEHAVIOR_SLJIT,
    BEHAVIOR_BYTECODE,
    BEHAVIOR_ORACLE,
    BEHAVIOR_WASM,
} behavior_backend;

/* Thread-local override for newly prepared programs/instances. UNAVAILABLE
 * restores platform selection; existing executable generations are unchanged. */
behavior_backend behavior_debug_backend(behavior_backend);
behavior_backend behavior_default_backend();
/* Actual compiled backend support, independent of the default override. */
bool behavior_backend_available(behavior_backend);
const char *behavior_backend_name(behavior_backend);

#endif
