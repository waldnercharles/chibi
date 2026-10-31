#include "authoring/asset_compiler_host.h"

#include "support/binary_cache.h"
#include "support/sha256.h"
#include "support/xalloc.h"
#include "toolchain/compiler_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define CLOSURE_COMPILE_BUDGET UINT64_C(4000000000)
#define CLOSURE_CACHE_ENTRIES 16

static void failure(asset_compiler_diagnostic *diagnostic, const char *message)
{
    snprintf(diagnostic->message, sizeof diagnostic->message, "%s", message);
}

static void runtime_failure(
    asset_compiler_diagnostic *diagnostic,
    const char *message,
    const compiler_host_source *source
)
{
    failure(diagnostic, message);
    if (source->present) {
        diagnostic->start = source->start;
        diagnostic->end = source->end;
        diagnostic->line = source->line;
        diagnostic->column = source->column;
        memcpy(diagnostic->source, source->asset, sizeof diagnostic->source);
        diagnostic->source_truncated = source->truncated;
    }
}

static uintptr_t host_budget(uint64_t budget)
{
    return budget > UINTPTR_MAX ? UINTPTR_MAX : (uintptr_t)budget;
}

static const asset_compiler_options *effective_options(const asset_compiler_options *options)
{
    return options ? options : asset_compiler_packaged_options();
}

/* The compiler host this thread loaded from the artifact, kept for the next
 * compile: loading the artifact takes far longer than a small compile. */
static compiler_host *compiler_for(const asset_compiler_options *options, asset_compiler_diagnostic *diagnostic)
{
    static _Thread_local struct
    {
        compiler_host *host;
        const void *artifact;
        size_t length;
    } loaded;
    if (!options || !options->compiler || !options->compiler_length) {
        failure(diagnostic, "The packaged Chibi package compiler is unavailable");
        return NULL;
    }
    if (loaded.host && (loaded.artifact != options->compiler ||
                        loaded.length != options->compiler_length)) {
        compiler_host_free(loaded.host);
        loaded.host = NULL;
    }
    if (!loaded.host) {
        loaded.host = compiler_host_load(
            options->compiler,
            options->compiler_length,
            BEHAVIOR_BYTECODE,
            diagnostic->message,
            sizeof diagnostic->message
        );
        loaded.artifact = options->compiler;
        loaded.length = options->compiler_length;
    }
    return loaded.host;
}

/* The first diagnostic of a rejected package compile. */
static void closure_rejection(
    compiler_host *host,
    const char *request,
    size_t request_length,
    uint64_t budget,
    asset_compiler_diagnostic *diagnostic
)
{
    compiler_host_result result = { 0 };
    bool called = compiler_host_invoke(
        host,
        compiler_host_entry("project:asset_compiler:1:RejectPackages:callable"),
        request,
        request_length,
        host_budget(budget),
        &result
    );
    yyjson_doc *doc = called ? yyjson_read(result.json, result.length, COMPILER_HOST_JSON_READ_FLAGS)
                             : NULL;
    yyjson_val *values = yyjson_obj_get(yyjson_arr_get(yyjson_doc_get_root(doc), 0), "values");
    const char *message = yyjson_get_str(yyjson_arr_get(values, 3));
    if (message && *message) {
        failure(diagnostic, message);
        diagnostic->document = (int)yyjson_get_int(yyjson_arr_get(values, 0));
        diagnostic->line = (int)yyjson_get_int(yyjson_arr_get(values, 1));
        diagnostic->column = (int)yyjson_get_int(yyjson_arr_get(values, 2));
    } else {
        failure(diagnostic, result.diagnostic[0] ? result.diagnostic : "The package compiler rejected its input");
    }
    yyjson_doc_free(doc);
    free(result.json);
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

unsigned char *asset_compiler_compile_closure(
    const char *request,
    size_t request_length,
    size_t *length,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
    *length = 0;
    options = effective_options(options);
    compiler_host *host = compiler_for(options, diagnostic);
    if (!host) {
        return NULL;
    }
    uint64_t budget = options->compile_budget ? options->compile_budget
                                              : CLOSURE_COMPILE_BUDGET;
    behavior_execution_scope scope = { .remaining = budget,
                                       .prefer_compiled = true,
                                       .cancelled = options->cancelled,
                                       .user = options->user };
    compiler_host_bytes result = { 0 };
    behavior_execution_scope_begin(&scope);
    bool ok = compiler_host_invoke_bytes(
        host,
        compiler_host_entry("project:asset_compiler:1:CompilePackages:callable"),
        request,
        request_length,
        host_budget(budget),
        COMPILER_HOST_MAX_OUTPUT_BYTES,
        &result
    );
    if (!ok) {
        runtime_failure(diagnostic, result.diagnostic, &result.source);
    } else if (!result.length) {
        closure_rejection(host, request, request_length, budget, diagnostic);
    }
    behavior_execution_scope_end(&scope);
    *length = result.length;
    return result.bytes;
}

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
static void closure_key(
    const asset_compiler_options *options,
    const char *request,
    size_t request_length,
    const char *salt,
    char key[BINARY_CACHE_KEY_BYTES + 1]
)
{
    /* The artifact is immutable and about 15 MB: hash it once per thread. */
    static _Thread_local const void *hashed;
    static _Thread_local size_t hashed_length;
    static _Thread_local char compiler[65];
    if (hashed != options->compiler || hashed_length != options->compiler_length) {
        sha256_hex(options->compiler, options->compiler_length, compiler);
        hashed = options->compiler;
        hashed_length = options->compiler_length;
    }
    sha256 state;
    sha256_begin(&state);
    sha256_add(&state, compiler, 64);
    sha256_add(&state, salt, strlen(salt) + 1);
    sha256_add(&state, request, request_length);
    char digest[65];
    sha256_end(&state, digest);
    snprintf(key, BINARY_CACHE_KEY_BYTES + 1, "closure-%s", digest);
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
        binary_cache_write(directory, key, bytes, length, COMPILER_HOST_MAX_OUTPUT_BYTES);
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
    const asset_compiler_options *effective = effective_options(options);
    char *directory = salt && effective && effective->compiler
                        ? closure_directory(effective)
                        : NULL;
    char key[BINARY_CACHE_KEY_BYTES + 1];
    if (directory) {
        closure_key(effective, request, request_length, salt, key);
        unsigned char *stored = binary_cache_read(directory, key, COMPILER_HOST_MAX_OUTPUT_BYTES, length);
        if (stored) {
            *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
            free(directory);
            return stored;
        }
    }
    unsigned char *closure = asset_compiler_compile_closure(request, request_length, length, options, diagnostic);
    if (directory && closure && *length &&
        !(effective->cancelled && effective->cancelled(effective->user))) {
        closure_publish(directory, key, closure, *length);
    }
    free(directory);
    return closure;
}
