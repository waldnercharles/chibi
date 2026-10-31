#ifndef CHIBI_MODEL_VALIDATION_H
#define CHIBI_MODEL_VALIDATION_H

#include <stdbool.h>
#include <stddef.h>

/* Read and compile saved model sources with their project dependencies.
 * Does not publish a generation or change authored files. Diagnostics include
 * the source path and authored node when available. */
bool model_content_validate(const char *content_root, char *error, size_t size);

#endif
