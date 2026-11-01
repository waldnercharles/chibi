#include "execution/artifact.h"

#include "execution/wasm_jit.h"
#include "support/xalloc.h"

#include <stdlib.h>
#if CHIBI_HAVE_SJLIT
#include <sljitLir.h>
#endif

void behavior_jit_free(void *code)
{
#if CHIBI_HAVE_SJLIT
    if (code) {
        sljit_free_code(code, NULL);
    }
#else
    (void)code;
#endif
}

execution_artifact *execution_artifact_new()
{
    execution_artifact *artifact = xcalloc(1, sizeof *artifact);
    atomic_init(&artifact->inspection_state, 0);
    atomic_init(&artifact->scoped_state, 0);
    return artifact;
}
void execution_artifact_free(execution_artifact *artifact)
{
    if (!artifact) {
        return;
    }
    behavior_jit_free(artifact->code);
    behavior_jit_free(artifact->dispatch);
    behavior_jit_free(artifact->inspection_code);
    behavior_jit_free(artifact->scoped_code);
    free(artifact->scoped_addresses);
    free(artifact->resume_addresses);
    free(artifact->inspection_addresses);
    execution_wasm_jit_free(artifact->wasm_jit);
    free(artifact);
}
