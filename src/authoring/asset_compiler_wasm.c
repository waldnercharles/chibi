#include "authoring/asset_compiler_wasm.h"

#include "runtime/program_wasm.h"

#include <assert.h>
#include <stdio.h>

unsigned char *asset_compiler_wasm_compile(
    const char *request,
    size_t request_length,
    size_t *length,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    const program_wasm_options execution = {
        .budget = options ? options->compile_budget : 0,
        .cancelled = options ? options->cancelled : NULL,
        .user = options ? options->user : NULL,
        .max_request_bytes = ASSET_COMPILER_MAX_REQUEST_BYTES,
        .max_output_bytes = ASSET_COMPILER_MAX_OUTPUT_BYTES,
    };
    program_wasm_diagnostic failure;
    unsigned char *output = program_wasm_run(request, request_length, length, &execution, &failure);
    *diagnostic = (asset_compiler_diagnostic){ .document = failure.document,
                                               .line = failure.line,
                                               .column = failure.column };
    const char *message = failure.message;
    switch (failure.kind) {
        case PROGRAM_WASM_NONE:
        case PROGRAM_WASM_REJECTED: break;
        case PROGRAM_WASM_ENTRY_FAILED:
            message = "The package compiler rejected its request";
            break;
        case PROGRAM_WASM_REQUEST_MISSING:
            message = "The package compiler request is missing";
            break;
        case PROGRAM_WASM_REQUEST_LIMIT:
            message = "The package compiler request exceeds its byte limit";
            break;
        case PROGRAM_WASM_OUTPUT_LIMIT:
            message = "The package compiler output exceeds its byte limit";
            break;
        case PROGRAM_WASM_REENTRANT:
            message = "The package compiler is already running on this thread";
            break;
        case PROGRAM_WASM_BUDGET:
            message = "The package compile exceeded its step budget";
            break;
        case PROGRAM_WASM_CANCELLED:
            message = "The package compile was cancelled";
            break;
        case PROGRAM_WASM_NATIVE_FAILURE:
            message = "The native package compiler failed";
            break;
        default: assert(!"Invalid Wasm program failure");
    }
    snprintf(diagnostic->message, sizeof diagnostic->message, "%s", message);
    return output;
}

#ifdef CHIBI_ASSET_COMPILER_WASM_TEST
size_t asset_compiler_wasm_test_allocations(void)
{
    return program_wasm_test_allocations();
}
#endif
