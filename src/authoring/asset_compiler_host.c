#include "authoring/asset_compiler_host.h"

#include "support/binary_cache.h"
#include "support/sha256.h"
#include "support/xalloc.h"
#ifdef __EMSCRIPTEN__
#include "authoring/asset_compiler_wasm.h"
#else
#include "authoring/asset_compiler_native.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLOSURE_CACHE_ENTRIES 16

static void failure(asset_compiler_diagnostic *diagnostic, const char *message)
{
    snprintf(diagnostic->message, sizeof diagnostic->message, "%s", message);
}

static const asset_compiler_options *effective_options(const asset_compiler_options *options)
{
    static const asset_compiler_options defaults = { 0 };
    return options ? options : &defaults;
}

static char *persistent_directory(const asset_compiler_options *options)
{
    const char *path = options->cache_directory;
    if (!path) {
        path = getenv("CHIBI_ASSET_CACHE_DIR");
    }
    if (path) {
        return *path && strnlen(path, 4097) <= 4096 ? xstrdup(path) : NULL;
    }
#ifdef __EMSCRIPTEN__
    return xstrdup("/cache/chibi-assets");
#else
    const char *base;
    const char *suffix;
#ifdef _WIN32
    base = getenv("LOCALAPPDATA");
    suffix = "/Chibi/assets";
#elif defined(__APPLE__)
    base = getenv("HOME");
    suffix = "/Library/Caches/Chibi/assets";
#else
    base = getenv("XDG_CACHE_HOME");
    suffix = "/chibi/assets";
    if (!base || !*base) {
        base = getenv("HOME");
        suffix = "/.cache/chibi/assets";
    }
#endif
    if (!base || !*base || strnlen(base, 4097) > 4096) {
        return NULL;
    }
    size_t size = strlen(base) + strlen(suffix) + 1;
    char *directory = xmalloc(size);
    snprintf(directory, size, "%s%s", base, suffix);
    return directory;
#endif
}

#ifdef __EMSCRIPTEN__
unsigned char *asset_compiler_compile_closure(
    const char *request,
    size_t request_length,
    size_t *length,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    return asset_compiler_wasm_compile(request, request_length, length, options, diagnostic);
}
#endif

static char *closure_directory(const asset_compiler_options *options)
{
    char *base = persistent_directory(options);
    if (!base) {
        return NULL;
    }
    size_t size = strlen(base) + sizeof "/closures";
    char *directory = xmalloc(size);
    snprintf(directory, size, "%s/closures", base);
    free(base);
    return directory;
}
static bool closure_key(
    const asset_compiler_options *options,
    const char *request,
    size_t request_length,
    const char *salt,
    char key[BINARY_CACHE_KEY_BYTES + 1]
)
{
    char compiler[65];
#ifdef __EMSCRIPTEN__
    (void)options;
    /* This immutable identity hashes the compiled compiler/runtime payload. */
    memcpy(compiler, asset_compiler_wasm_identity(), sizeof compiler);
#else
    if (!asset_compiler_native_digest(options, compiler)) {
        return false;
    }
#endif
    sha256 state;
    sha256_begin(&state);
    sha256_add(&state, compiler, 64);
    sha256_add(&state, salt, strlen(salt) + 1);
    sha256_add(&state, request, request_length);
    char digest[65];
    sha256_end(&state, digest);
    snprintf(key, BINARY_CACHE_KEY_BYTES + 1, "closure-%s", digest);
    return true;
}
static void closure_publish(const char *directory, const char *key, const unsigned char *bytes, size_t length)
{
    int lock = binary_cache_lock(directory);
    if (lock < 0) {
        return;
    }
    binary_cache_entry *entries = NULL;
    int count = 0;
    if (binary_cache_list(directory, &entries, &count)) {
        while (count >= CLOSURE_CACHE_ENTRIES) {
            int oldest = 0;
            for (int i = 1; i < count; i++) {
                oldest = entries[i].stamp < entries[oldest].stamp ? i : oldest;
            }
            if (!binary_cache_remove(directory, entries[oldest].key)) {
                break;
            }
            entries[oldest] = entries[--count];
        }
        binary_cache_write(directory, key, bytes, length, ASSET_COMPILER_MAX_OUTPUT_BYTES);
    }
    free(entries);
    binary_cache_unlock(lock);
}
unsigned char *asset_compiler_compile_closure_cached(
    const char *request,
    size_t request_length,
    const char *salt,
    size_t *length,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
    *length = 0;
    if (!request) {
        failure(diagnostic, "The package compiler request is missing");
        return NULL;
    }
    if (request_length > ASSET_COMPILER_MAX_REQUEST_BYTES) {
        failure(diagnostic, "The package compiler request exceeds its byte limit");
        return NULL;
    }
    const asset_compiler_options *effective = effective_options(options);
    bool cacheable = salt && effective &&
                     !(effective->cancelled && effective->cancelled(effective->user));
    char *directory = cacheable ? closure_directory(effective) : NULL;
    char key[BINARY_CACHE_KEY_BYTES + 1];
    if (directory) {
        if (!closure_key(effective, request, request_length, salt, key)) {
            free(directory);
            directory = NULL;
        }
    }
    if (directory) {
        unsigned char *stored = binary_cache_read(directory, key, ASSET_COMPILER_MAX_OUTPUT_BYTES, length);
        if (stored) {
            free(directory);
            if (effective->cancelled && effective->cancelled(effective->user)) {
                free(stored);
                *length = 0;
                failure(diagnostic, "The package compile was cancelled");
                return NULL;
            }
            return stored;
        }
    }
    unsigned char *closure = asset_compiler_compile_closure(request, request_length, length, options, diagnostic);
    if (directory && closure && *length &&
        !(effective->cancelled && effective->cancelled(effective->user))) {
        char current[BINARY_CACHE_KEY_BYTES + 1];
        if (closure_key(effective, request, request_length, salt, current) &&
            !strcmp(current, key)) {
            closure_publish(directory, key, closure, *length);
        }
    }
    free(directory);
    return closure;
}
