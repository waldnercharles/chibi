#ifndef CHIBI_APPLICATION_CATALOG_H
#define CHIBI_APPLICATION_CATALOG_H
#include "runtime/native_capabilities.h"
/* Rendering authoring application composition, with optional Studio services.
 * The returned immutable table is owned by the caller. */
native_capabilities *authoring_application_catalog_new(bool studio, char *error, size_t size);
#endif
