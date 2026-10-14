#ifndef CHIBI_APPLICATION_CATALOG_H
#define CHIBI_APPLICATION_CATALOG_H
#include "runtime/native_capabilities.h"
/* Rendering application composition, with optional reusable workspace services.
 * The returned immutable table is owned by the caller. */
native_capabilities *authoring_application_catalog_new(bool workspace, char *error, size_t size);
#endif
