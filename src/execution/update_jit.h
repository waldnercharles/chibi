#ifndef CHIBI_EXECUTION_UPDATE_JIT_H
#define CHIBI_EXECUTION_UPDATE_JIT_H
#include "execution/update.h"
#include "support/arena.h"
#if CHIBI_HAVE_SJLIT
#include <sljitLir.h>
/* S0 row, S1 end, S2 world, S3 result, S4 live state, S5 binding/context.
 * Shared by the prepared dispatcher and the compiler's inlined system loop.
 * Guards append at most 16 exits. Binding returns the context in R0. */
int behavior_update_emit_guard(struct sljit_compiler *compiler, struct sljit_jump **stops);
void behavior_update_emit_bind(struct sljit_compiler *compiler);
#endif
#endif
