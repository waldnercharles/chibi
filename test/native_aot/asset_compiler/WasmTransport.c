#include "runtime/program_wasm.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const char expected[] = "ffffffffffffffff|1.25|true|   a\0b|{}|1.25";

static void succeeds(const program_wasm_options *options)
{
    program_wasm_diagnostic diagnostic;
    size_t length = 0;
    unsigned char *output = program_wasm_run("[]", 2, &length, options, &diagnostic);
    assert(output && length == sizeof expected - 1);
    assert(!memcmp(output, expected, length) && diagnostic.kind == PROGRAM_WASM_NONE);
    free(output);
    assert(program_wasm_test_allocations() == 0);
}

static bool cancelled(void *user)
{
    unsigned *calls = user;
    return ++*calls >= 2;
}

static bool nested(void *user)
{
    bool *called = user;
    if (!*called) {
        program_wasm_diagnostic diagnostic;
        size_t length = 99;
        unsigned char *output = program_wasm_run("[]", 2, &length, NULL, &diagnostic);
        assert(!output && !length && diagnostic.kind == PROGRAM_WASM_REENTRANT);
        *called = true;
    }
    return false;
}

/* Exercise the same generated program and instance after every failure. Its
 * entry copies the request through the foreign buffer ABI, can reject it with
 * a source location, and formats text containing a NUL before publishing. */
void test_wasm_transport(void)
{
    succeeds(NULL);
    program_wasm_diagnostic diagnostic;
    size_t length = 99;
    unsigned char *output = program_wasm_run(NULL, 0, &length, NULL, &diagnostic);
    assert(!output && !length && diagnostic.kind == PROGRAM_WASM_REQUEST_MISSING);
    succeeds(NULL);

    program_wasm_options options = { .max_request_bytes = 1 };
    length = 99;
    output = program_wasm_run("[]", 2, &length, &options, &diagnostic);
    assert(!output && !length && diagnostic.kind == PROGRAM_WASM_REQUEST_LIMIT);
    succeeds(NULL);

    options = (program_wasm_options){ .max_output_bytes = 1 };
    length = 99;
    output = program_wasm_run("[]", 2, &length, &options, &diagnostic);
    assert(!output && !length && diagnostic.kind == PROGRAM_WASM_OUTPUT_LIMIT);
    succeeds(NULL);

    output = program_wasm_run("reject", 6, &length, NULL, &diagnostic);
    assert(!output && !length && diagnostic.kind == PROGRAM_WASM_REJECTED);
    assert(diagnostic.document == 7 && diagnostic.line == 3 && diagnostic.column == 5);
    assert(!strcmp(diagnostic.message, "rejected by the transport fixture"));
    succeeds(NULL);

    options = (program_wasm_options){ .budget = 1 };
    output = program_wasm_run("[]", 2, &length, &options, &diagnostic);
    assert(!output && !length && diagnostic.kind == PROGRAM_WASM_BUDGET);
    succeeds(NULL);

    unsigned calls = 0;
    options = (program_wasm_options){ .cancelled = cancelled, .user = &calls };
    output = program_wasm_run("[]", 2, &length, &options, &diagnostic);
    assert(!output && !length && diagnostic.kind == PROGRAM_WASM_CANCELLED && calls == 2);
    succeeds(NULL);

    bool called = false;
    options = (program_wasm_options){ .cancelled = nested, .user = &called };
    succeeds(&options);
    assert(called);
}
