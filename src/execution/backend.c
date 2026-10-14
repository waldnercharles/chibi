#include "execution/backend.h"

static _Thread_local behavior_backend debug_backend;

behavior_backend behavior_debug_backend(behavior_backend backend)
{
    behavior_backend previous = debug_backend;
    debug_backend = backend;
    return previous;
}

behavior_backend behavior_default_backend()
{
    if (debug_backend) {
        return debug_backend;
    }
#if defined(__EMSCRIPTEN__)
    return BEHAVIOR_WASM;
#elif !CHIBI_HAVE_SJLIT
    return BEHAVIOR_BYTECODE;
#else
    return BEHAVIOR_SLJIT;
#endif
}

bool behavior_backend_available(behavior_backend backend)
{
    if (backend == BEHAVIOR_BYTECODE) {
        return true;
    }
#if defined(__EMSCRIPTEN__)
    return backend == BEHAVIOR_WASM;
#elif CHIBI_HAVE_SJLIT
    return backend == BEHAVIOR_SLJIT;
#else
    return false;
#endif
}

const char *behavior_backend_name(behavior_backend backend)
{
    switch (backend) {
        case BEHAVIOR_WASM: return "WASM";
        case BEHAVIOR_SLJIT: return "SLJIT";
        case BEHAVIOR_BYTECODE: return "BYTECODE";
        case BEHAVIOR_ORACLE: return "ORACLE";
        case BEHAVIOR_UNAVAILABLE: return "UNAVAILABLE";
    }
    return "UNAVAILABLE";
}
