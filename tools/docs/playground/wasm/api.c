#include "runtime/program_wasm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One synchronous call per worker. Results stay owned here until the next
 * call; JavaScript copies them before submitting another request. */
static unsigned char *result;
static size_t result_length;
static program_wasm_diagnostic diagnostic;
static unsigned char *context;
static uint32_t context_length;

/* Project documents are immutable between calls. The setter owns a copy so
 * the JavaScript input allocation can be released immediately. */
int chibi_formatter_set_context(const unsigned char *bytes, uint32_t length)
{
    if (length > 16u * 1024u * 1024u || (length && !bytes)) {
        diagnostic = (program_wasm_diagnostic){ .kind = PROGRAM_WASM_REQUEST_LIMIT };
        snprintf(diagnostic.message, sizeof diagnostic.message,
                 "Invalid formatter project context: expected at most 16 MiB of JSON");
        return 1;
    }
    unsigned char *copy = NULL;
    if (length) {
        copy = malloc(length);
        if (!copy) abort();
        memcpy(copy, bytes, length);
    }
    free(context);
    context = copy;
    context_length = length;
    return 0;
}

int32_t chibi_formatter_context_length(void)
{
    return (int32_t)context_length;
}

int32_t chibi_formatter_context_copy(unsigned char *buffer, int32_t count)
{
    if (count < 0 || (uint32_t)count != context_length || (count && !buffer)) return 1;
    if (count) memcpy(buffer, context, context_length);
    return 0;
}

int chibi_formatter_format(const char *request, uint32_t length)
{
    free(result);
    result = NULL;
    result_length = 0;
    const program_wasm_options options = {
        .max_request_bytes = 432769,
        .max_output_bytes = 16u * 1024u * 1024u,
    };
    result = program_wasm_run(request, length, &result_length, &options, &diagnostic);
    return result ? 0 : 1;
}

const unsigned char *chibi_formatter_result(void)
{
    return result;
}

uint32_t chibi_formatter_result_length(void)
{
    return (uint32_t)result_length;
}

const char *chibi_formatter_error(void)
{
    return diagnostic.message;
}
