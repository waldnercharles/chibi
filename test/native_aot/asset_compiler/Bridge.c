#include "authoring/asset_compiler_host.h"
#include "support/file_io.h"
#include "support/sha256.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct
{
    const char *request, *program;
    unsigned calls;
} reentrant_call;

static bool nested_compile(void *user)
{
    reentrant_call *call = user;
    if (++call->calls == 3) {
        asset_compiler_options options = { .program = call->program,
                                           .cache_directory = "" };
        asset_compiler_diagnostic diagnostic;
        size_t length;
        unsigned char *bytes = asset_compiler_compile_closure(
            call->request,
            strlen(call->request),
            &length,
            &options,
            &diagnostic
        );
        assert(bytes && length == 329 && !diagnostic.message[0]);
        free(bytes);
    }
    return false;
}

#ifdef _WIN32
static DWORD WINAPI parallel_compile(void *user)
#else
static void *parallel_compile(void *user)
#endif
{
    reentrant_call *call = user;
    asset_compiler_options options = { .program = call->program, .cache_directory = "" };
    asset_compiler_diagnostic diagnostic;
    size_t length;
    unsigned char *bytes = asset_compiler_compile_closure(
        call->request,
        strlen(call->request),
        &length,
        &options,
        &diagnostic
    );
    assert(bytes && length == 329 && !diagnostic.message[0]);
    free(bytes);
    return 0;
}

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
    reentrant_call nested = { .request = request, .program = argv[1] };
    options.cancelled = nested_compile;
    options.user = &nested;
    bytes = asset_compiler_compile_closure(request, strlen(request), &length, &options, &diagnostic);
    assert(bytes && length == 329 && !diagnostic.message[0] && nested.calls >= 3);
    free(bytes);
    options.cancelled = NULL;
    reentrant_call parallel = { .request = request, .program = argv[1] };
#ifdef _WIN32
    HANDLE first = CreateThread(NULL, 0, parallel_compile, &parallel, 0, NULL);
    HANDLE second = CreateThread(NULL, 0, parallel_compile, &parallel, 0, NULL);
    assert(first && second);
    DWORD first_wait = WaitForSingleObject(first, 30000);
    DWORD second_wait = WaitForSingleObject(second, 30000);
    assert(first_wait == WAIT_OBJECT_0 && second_wait == WAIT_OBJECT_0);
    BOOL first_closed = CloseHandle(first), second_closed = CloseHandle(second);
    assert(first_closed && second_closed);
#else
    pthread_t first, second;
    int first_started = pthread_create(&first, NULL, parallel_compile, &parallel);
    int second_started = pthread_create(&second, NULL, parallel_compile, &parallel);
    assert(!first_started && !second_started);
    int first_joined = pthread_join(first, NULL),
        second_joined = pthread_join(second, NULL);
    assert(!first_joined && !second_joined);
#endif
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
#ifdef _WIN32
    /* Windows preserves the existing no-persistent-cache behavior. */
    options.compile_budget = 0;
#else
    options.compile_budget = 1;
#endif
    bytes = asset_compiler_compile_closure_cached(
        request,
        strlen(request),
        "fixture",
        &length,
        &options,
        &diagnostic
    );
    assert(bytes && length == 329); /* POSIX hits do no compilation; Windows compiles again. */
    free(bytes);
#ifndef _WIN32
    /* The borrowed salt streams across multiple 64-KiB chunks without a
     * concatenated request or salt allocation in the facade. */
    char *salt = malloc(131074);
    assert(salt);
    memset(salt, 's', 131073);
    salt[131073] = 0;
    options.compile_budget = 0;
    bytes = asset_compiler_compile_closure_cached(request, strlen(request), salt, &length, &options, &diagnostic);
    assert(bytes && length == 329);
    free(bytes);
    options.compile_budget = 1;
    bytes = asset_compiler_compile_closure_cached(request, strlen(request), salt, &length, &options, &diagnostic);
    assert(bytes && length == 329);
    free(bytes);
    free(salt);
#endif
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
    bytes = asset_compiler_compile_closure(request, strlen(request), &length, NULL, &diagnostic);
    assert(bytes && length == 329); /* NULL options use the same discovery. */
    free(bytes);
    options.program = argv[5];
    bytes = asset_compiler_compile_closure(request, strlen(request), &length, &options, &diagnostic);
    assert(!bytes && !length && !strcmp(diagnostic.message, "The native package compiler failed"));
    free(rejected);
    free(request);
    puts("native C asset facade: closures, diagnostics, quota, cancellation, cache and relocation passed");
    return 0;
}
