#include "authoring/asset_compiler_wasm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cancel_during_compile(void *user)
{
    unsigned *calls = user;
    return ++*calls >= 16;
}

static int reused(const char *request, size_t count)
{
    asset_compiler_diagnostic diagnostic;
    asset_compiler_options options = { 0 };
    size_t length = 0;
    unsigned char *reference = asset_compiler_wasm_compile(request, count, &length, &options, &diagnostic);
    assert(reference && length == 329);
    assert(asset_compiler_wasm_test_allocations() == 0);
    for (unsigned round = 0; round < 16; round++) {
        options.compile_budget = 1;
        unsigned char *bytes = asset_compiler_wasm_compile(request, count, &length, &options, &diagnostic);
        assert(
            !bytes && !length &&
            !strcmp(diagnostic.message, "The package compile exceeded its step budget")
        );
        assert(asset_compiler_wasm_test_allocations() == 0);
        unsigned calls = 0;
        options = (asset_compiler_options){ .cancelled = cancel_during_compile,
                                            .user = &calls };
        bytes = asset_compiler_wasm_compile(request, count, &length, &options, &diagnostic);
        assert(!bytes && !length && !strcmp(diagnostic.message, "The package compile was cancelled"));
        assert(asset_compiler_wasm_test_allocations() == 0);
        options = (asset_compiler_options){ 0 };
        bytes = asset_compiler_wasm_compile(request, count, &length, &options, &diagnostic);
        assert(bytes && length == 329 && !memcmp(bytes, reference, length));
        assert(asset_compiler_wasm_test_allocations() == 0);
        free(bytes);
    }
    free(reference);
    puts("one Wasm instance recovers after quota and cancellation with balanced allocations");
    return 0;
}

/* A filesystem protocol adapter for the same authored verifier used against
 * the desktop process. Files are a Node test convenience, not compiler imports. */
int main(int argc, char **argv)
{
    bool reuse = argc == 3 && !strcmp(argv[1], "--reuse");
    assert(reuse || argc == 4 || argc == 5);
    uint64_t budget = UINT64_C(4000000000);
    if (argc == 5) {
        budget = 0;
        const char *p = argv[4];
        if (!*p) {
            return 2;
        }
        while (*p) {
            unsigned digit = (unsigned char)*p++ - '0';
            if (digit > 9 || budget > (UINT64_MAX - digit) / 10) {
                return 2;
            }
            budget = budget * 10 + digit;
        }
        if (!budget) {
            return 2;
        }
    }
    FILE *input = fopen(argv[reuse ? 2 : 1], "rb");
    if (!input) {
        return 2;
    }
    if (fseek(input, 0, SEEK_END)) {
        fclose(input);
        return 2;
    }
    long count = ftell(input);
    if (count < 0 || (unsigned long)count > ASSET_COMPILER_MAX_REQUEST_BYTES ||
        fseek(input, 0, SEEK_SET)) {
        fclose(input);
        return 2;
    }
    char *request = malloc((size_t)count ? (size_t)count : 1);
    assert(request);
    bool read = fread(request, 1, (size_t)count, input) == (size_t)count;
    if (fclose(input) || !read) {
        free(request);
        return 2;
    }
    if (reuse) {
        int status = reused(request, (size_t)count);
        free(request);
        return status;
    }
    asset_compiler_options options = { .compile_budget = budget };
    asset_compiler_diagnostic diagnostic;
    size_t length = 0;
    unsigned char *bytes = asset_compiler_wasm_compile(request, (size_t)count, &length, &options, &diagnostic);
    free(request);
    if (!bytes) {
        if (diagnostic.line > 0) {
            FILE *output = fopen(argv[3], "wb");
            if (!output) {
                return 2;
            }
            bool written = fprintf(
                               output,
                               "%d\n%d\n%d\n%s",
                               diagnostic.document,
                               diagnostic.line,
                               diagnostic.column,
                               diagnostic.message
                           ) >= 0;
            if (fclose(output) || !written) {
                return 2;
            }
            return 1;
        }
        fprintf(stderr, "%s\n", diagnostic.message);
        return !strcmp(diagnostic.message, "The package compile exceeded its step budget")
                 ? 125
                 : 2;
    }
    size_t size = strlen(argv[2]) + sizeof ".partial";
    char *partial = malloc(size);
    assert(partial);
    snprintf(partial, size, "%s.partial", argv[2]);
    FILE *output = fopen(partial, "wb");
    bool written = false;
    if (output) {
        written = fwrite(bytes, 1, length, output) == length;
        if (fclose(output)) {
            written = false;
        }
    }
    free(bytes);
    if (written) {
        written = rename(partial, argv[2]) == 0;
    }
    if (!written) {
        remove(partial);
    }
    free(partial);
    return written ? 0 : 2;
}
