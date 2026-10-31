#ifndef CHIBI_AUTHORING_NATIVE_BINDINGS_H
#define CHIBI_AUTHORING_NATIVE_BINDINGS_H

#include "language/lower.h"
#include "runtime/native_capabilities.h"

/* The table outlives preparation. Native kernel/access descriptors borrowed by
 * the table outlive every resulting executable module. */
script_lower_bindings authoring_native_bindings(const native_capabilities *);
#endif
