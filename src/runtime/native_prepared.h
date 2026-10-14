#pragma once
#include "execution/prepared.h"
#include "runtime/event_catalog.h"
#include "runtime/native_capabilities.h"

typedef struct
{
    const native_capabilities *native;
    const event_catalog *events;
} native_prepared_context;
/* Explicit native implementations and original project event declarations.
 * Context is borrowed during read; table kernels/layouts and application type
 * catalog outlive the decoded modules. Package records resolve authored imports.
 * Unknown identities or schemas reject; there is no graph discovery fallback. */
execution_prepared_linker native_capabilities_prepared_linker(const native_prepared_context *);
