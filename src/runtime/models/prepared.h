#ifndef CHIBI_MODEL_PREPARED_H
#define CHIBI_MODEL_PREPARED_H
#include "runtime/models/model_program.h"
#define MODEL_PREPARED_VERSION UINT64_C(3)
/* Field-wise canonical model program and compiled construction/shader snapshots.
 * Owned resource/material keys and parameter declarations are retained. Runtime
 * image callbacks are unset after decode; the resident host supplies them. */
bool model_program_prepared_write(const model_program *, unsigned char **, size_t *, shader_error *);
/* Initialized output and remaining budget stay unchanged on any failure. */
bool model_program_prepared_read_budget(const void *, size_t, size_t *remaining, model_program *, shader_error *);
bool model_program_prepared_read(const void *, size_t, model_program *, shader_error *);
#endif
