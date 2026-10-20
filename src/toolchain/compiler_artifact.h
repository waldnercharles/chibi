#ifndef CHIBI_TOOLCHAIN_COMPILER_ARTIFACT_H
#define CHIBI_TOOLCHAIN_COMPILER_ARTIFACT_H

#include "runtime/behaviors/package.h"

/* Private target-compatible executable closure container. No source text or
 * native addresses are stored. Output is malloc-owned. Records are sorted by
 * identity, so caller order does not affect bytes. */
bool compiler_artifact_write(
    const package_module *,
    int count,
    uint64_t root,
    unsigned char **out,
    size_t *length,
    char *error,
    size_t error_size
);
/* Input is borrowed only during this call. The returned collection owns its
 * modules and names. Missing imports cannot resolve outside the container. */
package_collection *compiler_artifact_read(
    const void *,
    size_t,
    const execution_prepared_linker *,
    behavior_backend,
    size_t *remaining,
    uint64_t *root,
    char *error,
    size_t error_size
);

#endif
