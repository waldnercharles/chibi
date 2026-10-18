#ifndef CHIBI_TOOLCHAIN_COMPILER_PREPARED_SCALAR_H
#define CHIBI_TOOLCHAIN_COMPILER_PREPARED_SCALAR_H

#include "types/value.h"

enum
{
    COMPILER_PREPARED_SCALAR_MAX_ENTRY = 240,
    COMPILER_PREPARED_SCALAR_MAX_BYTES = 64 * 1024
};
typedef struct
{
    uint64_t module, function;
    const char *entry;
    pin_type result;
} compiler_prepared_scalar_declaration;

/* Transitional, private PRODUCER dependency. Decodes an authored schema-1
 * scalar_lowering.Plan with the unchanged scalar_lowering adapter, verifies
 * exactly zero arguments/state and one ordinary Bool/I32 result, and wraps the
 * body as one callable. ready_tag comes from trusted Plan export metadata.
 * Identity and result declaration are authored choices; this bridge derives
 * neither from source, names, packages, syntax or semantic records.
 *
 * Native builder verification/optimization/finalization and prepared writing
 * remain explicit compiler dependencies. No target execution or backend JIT
 * occurs here. The returned bytes are one INNER prepared module, never a
 * CHIBICLO envelope. Success transfers malloc storage to the caller (free()).
 * Failure clears bytes/length; inputs are borrowed only for this call.
 * bytes, length and a nonempty error buffer are required.
 * Do not link this producer into the runtime-only compiler_host. */
bool compiler_prepared_scalar_write(
    const pin_value *plan,
    uint64_t ready_tag,
    const compiler_prepared_scalar_declaration *,
    unsigned char **bytes,
    size_t *length,
    char *error,
    size_t error_size
);

#endif
