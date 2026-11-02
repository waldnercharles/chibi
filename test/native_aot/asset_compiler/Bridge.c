#include "authoring/asset_compiler_host.h"
#include "support/file_io.h"
#include "support/sha256.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cancel_after_start(void *user)
{
    unsigned *calls = user;
    return ++*calls >= 3;
}
static bool cancel_during_cache_read(void *user)
{
    unsigned *calls = user;
    return ++*calls >= 2;
}
static bool cancelled(void *user)
{
    (void)user;
    return true;
}
int main(int argc, char **argv)
{
    assert(argc == 6);
    char *request = file_read_text(argv[2]);
    char *rejected = file_read_text(argv[3]);
    assert(request && rejected);
    asset_compiler_options options = { .program = argv[1],
                                       .cache_directory = argv[4] };
    asset_compiler_diagnostic diagnostic;
    size_t length = 0;
    unsigned char *bytes = asset_compiler_compile_closure(
        request,
        strlen(request),
        &length,
        &options,
        &diagnostic
    );
    assert(bytes && length == 329 && !diagnostic.message[0]);
    char digest[65];
    sha256_hex(bytes, length, digest);
    assert(!strcmp(digest, "08b7ec6ca8d209820e44ddbda3151b5f8b866334fd45cc7f06124794279f3dc9"));
    free(bytes);
    bytes = asset_compiler_compile_closure(rejected, strlen(rejected), &length, &options, &diagnostic);
    assert(
        !bytes && !length && diagnostic.document == 0 && diagnostic.line == 2 &&
        diagnostic.column == 28
    );
    assert(!strcmp(diagnostic.message, "Unknown name 'missing'"));
    options.compile_budget = 1;
    bytes = asset_compiler_compile_closure(request, strlen(request), &length, &options, &diagnostic);
    assert(
        !bytes && !length &&
        !strcmp(diagnostic.message, "The package compile exceeded its step budget")
    );
    options.compile_budget = 0;
    unsigned calls = 0;
    options.cancelled = cancel_after_start;
    options.user = &calls;
    bytes = asset_compiler_compile_closure(request, strlen(request), &length, &options, &diagnostic);
    assert(!bytes && !length && !strcmp(diagnostic.message, "The package compile was cancelled"));
    options.cancelled = NULL;
    bytes = asset_compiler_compile_closure_cached(
        request,
        strlen(request),
        "fixture",
        &length,
        &options,
        &diagnostic
    );
    assert(bytes && length == 329);
    free(bytes);
    options.compile_budget = 1;
    bytes = asset_compiler_compile_closure_cached(
        request,
        strlen(request),
        "fixture",
        &length,
        &options,
        &diagnostic
    );
    assert(bytes && length == 329); /* A cache hit performs no compilation. */
    free(bytes);
    calls = 0;
    options.cancelled = cancel_during_cache_read;
    options.user = &calls;
    bytes = asset_compiler_compile_closure_cached(
        request,
        strlen(request),
        "fixture",
        &length,
        &options,
        &diagnostic
    );
    assert(!bytes && !length && !strcmp(diagnostic.message, "The package compile was cancelled"));
    options.cancelled = cancelled;
    bytes = asset_compiler_compile_closure_cached(
        request,
        strlen(request),
        "fixture",
        &length,
        &options,
        &diagnostic
    );
    assert(!bytes && !length && !strcmp(diagnostic.message, "The package compile was cancelled"));
    options.cancelled = NULL;
    bytes = asset_compiler_compile_closure_cached(
        "x",
        (size_t)ASSET_COMPILER_MAX_REQUEST_BYTES + 1,
        "fixture",
        &length,
        &options,
        &diagnostic
    );
    assert(
        !bytes && !length &&
        !strcmp(diagnostic.message, "The package compiler request exceeds its byte limit")
    );
    bytes = asset_compiler_compile_closure_cached(NULL, 0, "fixture", &length, &options, &diagnostic);
    assert(
        !bytes && !length &&
        !strcmp(diagnostic.message, "The package compiler request is missing")
    );
    options.compile_budget = 0;
    options.program = NULL;
    bytes = asset_compiler_compile_closure(request, strlen(request), &length, &options, &diagnostic);
    assert(bytes && length == 329); /* SDK-relative service discovery. */
    free(bytes);
    options.program = argv[5];
    bytes = asset_compiler_compile_closure(request, strlen(request), &length, &options, &diagnostic);
    assert(!bytes && !length && !strcmp(diagnostic.message, "The native package compiler failed"));
    free(rejected);
    free(request);
    puts("native C asset facade: closures, diagnostics, quota, cancellation, cache and relocation passed");
    return 0;
}
