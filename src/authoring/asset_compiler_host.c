#include "authoring/asset_compiler_host_internal.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/binary_cache.h"
#include "support/sha256.h"
#include "support/xalloc.h"
#include "toolchain/compiler_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define ASSET_COMPILE_BUDGET UINT64_C(1000000000)
#define ASSET_EVALUATION_BUDGET UINT64_C(10000000)
#define ASSET_CACHE_ENTRIES 128
#define ASSET_CACHE_BYTES (128u * 1024u * 1024u)
#define ASSET_DOCUMENT_MAX_DEPTH 1024

static av_doc *inspect(const char *source, const char *function, const asset_compiler_options *options)
{
    av_doc *args = av_doc_new();
    av_value *values = av_arr(args);
    av_doc_set_root(args, values);
    av_arr_add_strcpy(args, values, source);
    av_arr_add_strcpy(args, values, function);
    asset_compiler_diagnostic diagnostic = { 0 };
    av_doc *result = asset_source_invoke(
        "project:asset_compiler:1:Inspect:callable",
        values,
        options,
        &diagnostic
    );
    av_doc_free(args);
    return result;
}

static const av_value *bindings(const av_doc *result)
{
    const av_value *values = av_obj_get(av_arr_get(av_doc_root(result), 0), "values");
    if (av_arr_size(values) != 2) {
        return NULL;
    }
    return av_arr_get(values, 0);
}

static size_t offset(const av_value *binding, const char *name)
{
    int value = av_get_int(av_obj_get(binding, name));
    return value < 0 ? SIZE_MAX : (size_t)value;
}

static void attach(const char *source, const char *function, av_value *root, const asset_compiler_options *options)
{
    if (!root) {
        return;
    }
    av_doc *result = inspect(source, function, options);
    const av_value *items = result ? bindings(result) : NULL;
    av_value **mapped = NULL;
    int *ordinals = NULL;
    for (size_t i = 0; i < av_arr_size(items); i++) {
        const av_value *binding = av_arr_get(items, i);
        int parent = av_get_int(av_obj_get(binding, "parent"));
        av_value *value = NULL;
        if (parent < 0) {
            value = root;
        } else if (parent < acount(mapped) && mapped[parent]) {
            av_value *owner = mapped[parent];
            value = av_is_obj(owner)
                      ? av_obj_get(owner, av_get_str(av_obj_get(binding, "key")))
                      : av_arr_get(owner, (size_t)ordinals[parent]);
            ordinals[parent]++;
        }
        apush(mapped, value);
        apush(ordinals, 0);
        if (value) {
            size_t start = offset(binding, "literal_start");
            size_t end = offset(binding, "literal_end");
            if (start == SIZE_MAX) {
                start = offset(binding, "start");
                end = offset(binding, "end");
            }
            av_set_source(
                value,
                (av_source){ start, end, offset(binding, "name_start"), offset(binding, "name_end") }
            );
        }
    }
    afree(mapped);
    afree(ordinals);
    av_doc_free(result);
}

static void metadata_attach(const char *source, av_value *root, const asset_compiler_options *options)
{
    attach(source, "Build", root, options);
    attach(source, "Parameters", av_obj_get(root, "parameters"), options);
    attach(source, "Overrides", av_obj_get(root, "overrides"), options);
}

typedef struct host_lease
{
    compiler_host *host;
    size_t references, charge;
} host_lease;
/* Detached active leases remain charged until the last invocation returns. */
static _Thread_local size_t asset_cache_bytes;
static _Thread_local uint64_t cache_generation;
static host_lease *host_new(compiler_host *host)
{
    if (!host) {
        return NULL;
    }
    host_lease *lease = xcalloc(1, sizeof *lease);
    lease->host = host;
    lease->references = 1;
    return lease;
}
static host_lease *host_retain(host_lease *lease)
{
    if (lease) {
        lease->references++;
    }
    return lease;
}
static void host_release(host_lease *lease)
{
    if (lease && !--lease->references) {
        asset_cache_bytes -= lease->charge;
        compiler_host_free(lease->host);
        free(lease);
    }
}
typedef struct
{
    char *path, *source;
    unsigned char *bytes;
    host_lease *decoded;
    size_t length, charge;
    uint64_t entry, used;
} compiled_asset;
static _Thread_local struct
{
    asset_compiler_options snapshot;
    host_lease *compiler;
    char compiler_sha[65];
    compiled_asset assets[ASSET_CACHE_ENTRIES];
    uint64_t clock;
    asset_compiler_cache_stats stats;
} cache;
asset_compiler_cache_stats asset_source_cache_stats()
{
    asset_compiler_cache_stats stats = cache.stats;
    stats.resident_bytes = asset_cache_bytes;
    return stats;
}
static void cache_count(uint64_t *counter)
{
    if (*counter != UINT64_MAX) {
        (*counter)++;
    }
}

static const asset_compiler_options *defaults;
static void clear_asset(compiled_asset *asset)
{
    asset_cache_bytes -= asset->charge;
    host_release(asset->decoded);
    free(asset->path);
    free(asset->source);
    free(asset->bytes);
    *asset = (compiled_asset){ 0 };
}
void asset_source_release_thread()
{
    cache_generation++;
    for (int i = 0; i < ASSET_CACHE_ENTRIES; i++) {
        clear_asset(&cache.assets[i]);
    }
    host_release(cache.compiler);
    memset(&cache, 0, sizeof cache);
}
void asset_source_set_default(const asset_compiler_options *options)
{
    asset_source_release_thread();
    defaults = options;
}
static bool same_snapshot(const asset_compiler_options *options)
{
    return cache.snapshot.compiler == options->compiler &&
           cache.snapshot.compiler_length == options->compiler_length &&
           cache.snapshot.libraries == options->libraries &&
           cache.snapshot.library_count == options->library_count &&
           cache.snapshot.collections == options->collections &&
           cache.snapshot.collection_count == options->collection_count;
}
static compiled_asset *find_asset(const char *source, const char *path)
{
    for (int i = 0; i < ASSET_CACHE_ENTRIES; i++) {
        compiled_asset *asset = &cache.assets[i];
        if (asset->bytes && !strcmp(asset->source, source) &&
            !strcmp(asset->path, path)) {
            asset->used = ++cache.clock;
            return asset;
        }
    }
    return NULL;
}
static int oldest_asset()
{
    int oldest = -1;
    for (int i = 0; i < ASSET_CACHE_ENTRIES; i++) {
        if (cache.assets[i].bytes &&
            (oldest < 0 || cache.assets[i].used < cache.assets[oldest].used)) {
            oldest = i;
        }
    }
    return oldest;
}
static bool keep_asset(
    const char *source,
    const char *path,
    unsigned char *bytes,
    size_t length,
    uint64_t entry,
    host_lease *host
)
{
    size_t key_bytes = strlen(source) + strlen(path) + 2;
    size_t decoded = compiler_host_storage_bytes(host->host);
    if (decoded > ASSET_CACHE_BYTES - sizeof *host) {
        return false;
    }
    decoded += sizeof *host;
    if (length > ASSET_CACHE_BYTES || key_bytes > ASSET_CACHE_BYTES - length ||
        decoded > ASSET_CACHE_BYTES - length - key_bytes) {
        return false;
    }
    size_t charge = length + key_bytes;
    while (charge + decoded > ASSET_CACHE_BYTES - asset_cache_bytes) {
        int oldest = oldest_asset();
        if (oldest < 0) {
            return false;
        }
        clear_asset(&cache.assets[oldest]);
    }
    int slot = -1;
    for (int i = 0; i < ASSET_CACHE_ENTRIES; i++) {
        if (!cache.assets[i].bytes) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = oldest_asset();
        clear_asset(&cache.assets[slot]);
    }
    host->charge = decoded;
    asset_cache_bytes += charge + decoded;
    cache.assets[slot] = (compiled_asset){ .source = xstrdup(source),
                                           .path = xstrdup(path),
                                           .bytes = bytes,
                                           .decoded = host_retain(host),
                                           .length = length,
                                           .charge = charge,
                                           .entry = entry,
                                           .used = ++cache.clock };
    return true;
}
static void failure(asset_compiler_diagnostic *diagnostic, const char *message)
{
    snprintf(diagnostic->message, sizeof diagnostic->message, "%s", message);
}
typedef struct
{
    yyjson_val *value;
    int depth;
} json_document_item;

/* Match game:assets.Encode before recursive native value construction. Object
 * keys are visited values too, at the same depth as their member values. */
static bool document_json_quota(yyjson_val *root, asset_compiler_diagnostic *diagnostic)
{
    json_document_item *pending = NULL;
    apush(pending, ((json_document_item){ root, 0 }));
    size_t visited = 0;
    bool valid = true;
    while (acount(pending)) {
        json_document_item current = pending[acount(pending) - 1];
        asetlen(pending, acount(pending) - 1);
        yyjson_val *value = current.value;
        if (current.depth > ASSET_DOCUMENT_MAX_DEPTH ||
            ++visited > COMPILER_HOST_MAX_VALUES) {
            failure(diagnostic, "Asset document exceeds value/depth limits");
            valid = false;
            break;
        }
        if (yyjson_is_str(value) &&
            memchr(yyjson_get_str(value), 0, yyjson_get_len(value))) {
            failure(diagnostic, "Asset document text contains unsupported NUL bytes");
            valid = false;
            break;
        }
        bool object = yyjson_is_obj(value);
        size_t children = object               ? yyjson_obj_size(value)
                        : yyjson_is_arr(value) ? yyjson_arr_size(value)
                                               : 0;
        size_t remaining = COMPILER_HOST_MAX_VALUES - visited - (size_t)acount(pending);
        if (children > remaining / (object ? 2u : 1u) ||
            (children && current.depth == ASSET_DOCUMENT_MAX_DEPTH)) {
            failure(diagnostic, "Asset document exceeds value/depth limits");
            valid = false;
            break;
        }
        size_t index, count;
        yyjson_val *key, *child;
        if (object) {
            yyjson_obj_foreach(value, index, count, key, child)
            {
                if (memchr(yyjson_get_str(key), 0, yyjson_get_len(key))) {
                    failure(diagnostic, "Asset document key contains unsupported NUL bytes");
                    valid = false;
                    break;
                }
                visited++;
                apush(pending, ((json_document_item){ child, current.depth + 1 }));
            }
            if (!valid) {
                break;
            }
        } else if (yyjson_is_arr(value)) {
            yyjson_arr_foreach(value, index, count, child)
            {
                apush(pending, ((json_document_item){ child, current.depth + 1 }));
            }
        }
    }
    afree(pending);
    return valid;
}

static av_doc *read_document_transport(const char *json, size_t length, asset_compiler_diagnostic *diagnostic)
{
    if (length > COMPILER_HOST_MAX_OUTPUT_BYTES) {
        failure(diagnostic, "Asset document transport exceeds 16 MiB");
        return NULL;
    }
    yyjson_doc *decoded = yyjson_read(json, length, 0);
    if (!decoded) {
        failure(diagnostic, "Asset document transport is invalid JSON");
        return NULL;
    }
    av_doc *document = NULL;
    yyjson_val *root = yyjson_doc_get_root(decoded);
    if (document_json_quota(root, diagnostic)) {
        document = av_doc_new();
        av_doc_set_root(document, av_from_json(document, root));
    }
    yyjson_doc_free(decoded);
    return document;
}

typedef struct
{
    av_value *value, *parent;
    size_t index;
} numeric_transport_item;

static bool restore_numeric_kinds(av_doc *document, const char *kinds, size_t length, asset_compiler_diagnostic *diagnostic)
{
    numeric_transport_item *pending = NULL;
    apush(pending, ((numeric_transport_item){ .value = av_doc_root(document) }));
    size_t number = 0;
    bool valid = true;
    while (acount(pending)) {
        numeric_transport_item item = pending[acount(pending) - 1];
        asetlen(pending, acount(pending) - 1);
        av_value *value = item.value, *replacement = NULL;
        if (av_is_num(value)) {
            if (number == length) {
                valid = false;
                break;
            }
            char kind = kinds[number++];
            if (kind == 'i') {
                if (av_is_uint(value) && av_get_uint(value) <= INT64_MAX) {
                    replacement = av_sint(document, (int64_t)av_get_uint(value));
                } else if (!av_is_sint(value)) {
                    valid = false;
                    break;
                }
            } else if (kind == 'u') {
                if (av_is_sint(value) && av_get_sint(value) >= 0) {
                    replacement = av_uint(document, (uint64_t)av_get_sint(value));
                } else if (!av_is_uint(value)) {
                    valid = false;
                    break;
                }
            } else if (kind != 'r' || av_get_type(value) != AV_REAL) {
                valid = false;
                break;
            }
            if (replacement) {
                if (!item.parent) {
                    av_doc_set_root(document, replacement);
                } else if (av_is_arr(item.parent)) {
                    av_arr_replace(item.parent, item.index, replacement);
                } else {
                    av_obj_set(
                        document,
                        item.parent,
                        av_get_str(av_obj_key_at(item.parent, item.index)),
                        replacement
                    );
                }
            }
        } else {
            bool object = av_is_obj(value);
            size_t count = object           ? av_obj_size(value)
                         : av_is_arr(value) ? av_arr_size(value)
                                            : 0;
            for (size_t i = count; i; i--) {
                av_value *child = object ? av_obj_value_at(value, i - 1)
                                         : av_arr_get(value, i - 1);
                apush(pending, ((numeric_transport_item){ child, value, i - 1 }));
            }
        }
    }
    afree(pending);
    if (!valid || number != length) {
        failure(diagnostic, "Asset numeric transport kinds do not match the value");
        return false;
    }
    return true;
}

static av_doc *read_asset_transport(const char *bytes, size_t length, asset_compiler_diagnostic *diagnostic)
{
    static const char prefix[] = "CHIBI-ASSET1\n";
    size_t header = sizeof prefix - 1;
    if (length < header || memcmp(bytes, prefix, header)) {
        return read_document_transport(bytes, length, diagnostic);
    }
    if (length > COMPILER_HOST_MAX_OUTPUT_BYTES) {
        failure(diagnostic, "Asset document transport exceeds 16 MiB");
        return NULL;
    }
    yyjson_doc *decoded = yyjson_read(bytes + header, length - header, 0);
    yyjson_val *root = decoded ? yyjson_doc_get_root(decoded) : NULL;
    yyjson_val *value = yyjson_obj_get(root, "value");
    yyjson_val *numbers = yyjson_obj_get(root, "numbers");
    av_doc *document = NULL;
    if (!yyjson_is_obj(root) || yyjson_obj_size(root) != 2 || !value ||
        !yyjson_is_str(numbers) || yyjson_get_len(numbers) > COMPILER_HOST_MAX_VALUES) {
        failure(diagnostic, "Asset numeric transport requires value and numbers fields");
    } else {
        const char *kinds = yyjson_get_str(numbers);
        size_t count = yyjson_get_len(numbers);
        bool valid = true;
        for (size_t i = 0; i < count; i++) {
            if (kinds[i] != 'i' && kinds[i] != 'u' && kinds[i] != 'r') {
                valid = false;
                break;
            }
        }
        if (!valid) {
            failure(diagnostic, "Asset numeric transport has an invalid kind");
        } else if (document_json_quota(value, diagnostic)) {
            document = av_doc_new();
            av_doc_set_root(document, av_from_json(document, value));
            if (!restore_numeric_kinds(document, kinds, count, diagnostic)) {
                av_doc_free(document);
                document = NULL;
            }
        }
    }
    yyjson_doc_free(decoded);
    return document;
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
    return options  ? options
         : defaults ? defaults
                    : asset_compiler_packaged_options();
}
const asset_compiler_options *asset_source_default_options()
{
    return effective_options(NULL);
}
static host_lease *compiler_for(const asset_compiler_options *options, asset_compiler_diagnostic *diagnostic)
{
    if (!options || !options->compiler || !options->compiler_length) {
        failure(diagnostic, "The packaged Chibi asset compiler is unavailable");
        return NULL;
    }
    if (!same_snapshot(options)) {
        asset_source_release_thread();
        cache.snapshot = *options;
        sha256_hex(options->compiler, options->compiler_length, cache.compiler_sha);
    }
    if (!cache.compiler) {
        cache.compiler = host_new(compiler_host_load(
            options->compiler,
            options->compiler_length,
            BEHAVIOR_BYTECODE,
            diagnostic->message,
            sizeof diagnostic->message
        ));
    }
    return host_retain(cache.compiler);
}
static char *request(
    const char *source,
    const char *path,
    const asset_compiler_options *options,
    size_t *length,
    asset_compiler_diagnostic *diagnostic
)
{
    size_t target_length;
    char *target = compiler_host_values_target_json(
        &target_length,
        diagnostic->message,
        sizeof diagnostic->message
    );
    yyjson_doc *decoded = target ? yyjson_read(target, target_length, 0) : NULL;
    free(target);
    if (!decoded) {
        failure(diagnostic, "Pure asset target could not be described");
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *args = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, args);
    yyjson_mut_arr_add_str(doc, args, "/project");
    yyjson_mut_val *names = yyjson_mut_arr(doc), *roots = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_val(args, names);
    yyjson_mut_arr_add_val(args, roots);
    for (int i = 0; i < options->collection_count; i++) {
        yyjson_mut_arr_add_str(doc, names, options->collections[i].name);
        yyjson_mut_arr_add_str(doc, roots, options->collections[i].path);
    }
    yyjson_mut_val *paths = yyjson_mut_arr(doc), *sources = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_val(args, paths);
    yyjson_mut_arr_add_val(args, sources);
    yyjson_mut_arr_add_str(doc, paths, path);
    yyjson_mut_arr_add_str(doc, sources, source);
    for (int i = 0; i < options->library_count; i++) {
        yyjson_mut_arr_add_str(doc, paths, options->libraries[i].path);
        yyjson_mut_arr_add_str(doc, sources, options->libraries[i].source);
    }
    yyjson_mut_arr_add_bool(doc, args, false);
    yyjson_val *item;
    size_t i, count;
    yyjson_arr_foreach(yyjson_doc_get_root(decoded), i, count, item)
    {
        yyjson_mut_arr_add_val(args, yyjson_val_mut_copy(doc, item));
    }
    char *json = yyjson_mut_write(doc, 0, length);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(decoded);
    return json;
}
#define COMPILE_HEADER_BYTES 29
static uint64_t read_le(const unsigned char *bytes, int count)
{
    uint64_t value = 0;
    for (int i = 0; i < count; i++) {
        value |= (uint64_t)bytes[i] << (8 * i);
    }
    return value;
}
static int read_position(const unsigned char *bytes)
{
    uint32_t bits = (uint32_t)read_le(bytes, 4);
    return bits <= INT32_MAX ? (int)bits : (int)((int64_t)bits - INT64_C(4294967296));
}
static unsigned char *compiled_bytes(
    compiler_host_bytes *result,
    uint64_t *entry,
    size_t *length,
    asset_compiler_diagnostic *diagnostic
)
{
    if (result->length < COMPILE_HEADER_BYTES || result->bytes[0] > 1) {
        failure(diagnostic, "Asset compiler returned an invalid byte envelope");
        return NULL;
    }
    unsigned char *bytes = result->bytes;
    *entry = read_le(bytes + 1, 8);
    *length = result->length - COMPILE_HEADER_BYTES;
    if (bytes[0]) {
        if (!*entry || !*length) {
            failure(diagnostic, "Asset compiler produced an empty artifact or entry");
            return NULL;
        }
        memmove(bytes, bytes + COMPILE_HEADER_BYTES, *length);
        result->bytes = NULL;
        return bytes;
    }
    if (*entry || !*length || memchr(bytes + COMPILE_HEADER_BYTES, 0, *length)) {
        failure(diagnostic, "Asset compiler returned an invalid rejection envelope");
        return NULL;
    }
    diagnostic->document = read_position(bytes + 9);
    diagnostic->start = read_position(bytes + 13);
    diagnostic->end = read_position(bytes + 17);
    diagnostic->line = read_position(bytes + 21);
    diagnostic->column = read_position(bytes + 25);
    if (diagnostic->document < -1 || diagnostic->start < -1 ||
        diagnostic->end < -1 || diagnostic->line < -1 || diagnostic->column < -1 ||
        (diagnostic->start >= 0 && diagnostic->end >= 0 &&
         diagnostic->end < diagnostic->start)) {
        failure(diagnostic, "Asset compiler returned invalid diagnostic positions");
        return NULL;
    }
    size_t copied = *length < sizeof diagnostic->message - 1
                      ? *length
                      : sizeof diagnostic->message - 1;
    memcpy(diagnostic->message, bytes + COMPILE_HEADER_BYTES, copied);
    diagnostic->message[copied] = 0;
    return NULL;
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
static char *persistent_key(const char *json, size_t length, const asset_compiler_options *options)
{
    char digest[65];
    sha256_hex(json, length, digest);
    av_doc *args = av_doc_new();
    av_value *values = av_arr(args);
    av_doc_set_root(args, values);
    av_arr_add_strcpy(args, values, cache.compiler_sha);
    av_arr_add_strcpy(args, values, digest);
    asset_compiler_diagnostic ignored = { 0 };
    av_doc *result = asset_source_invoke(
        "project:asset_compiler:1:CacheKey:callable",
        values,
        options,
        &ignored
    );
    const av_value *key = result ? av_arr_get(av_doc_root(result), 0) : NULL;
    char *text = av_is_str(key) && av_get_str(key)[0] ? xstrdup(av_get_str(key)) : NULL;
    av_doc_free(result);
    av_doc_free(args);
    return text;
}
static bool persistent_accept(const unsigned char *bytes, size_t length, const asset_compiler_options *options)
{
    if (length < COMPILE_HEADER_BYTES) {
        return false;
    }
    av_doc *args = av_doc_new();
    av_value *values = av_arr(args);
    av_doc_set_root(args, values);
    av_arr_append(values, av_sint(args, bytes[0]));
    av_arr_append(values, av_uint(args, read_le(bytes + 1, 8)));
    av_arr_append(values, av_uint(args, length - COMPILE_HEADER_BYTES));
    asset_compiler_diagnostic ignored = { 0 };
    av_doc *result = asset_source_invoke(
        "project:asset_compiler:1:CacheAccept:callable",
        values,
        options,
        &ignored
    );
    bool accepted = result && av_get_bool(av_arr_get(av_doc_root(result), 0));
    av_doc_free(result);
    av_doc_free(args);
    return accepted;
}
static void persistent_publish(
    const char *directory,
    const char *key,
    const unsigned char *bytes,
    size_t length,
    const asset_compiler_options *options
)
{
    if (!directory || !key || !bytes || !persistent_accept(bytes, length, options) ||
        (options->cancelled && options->cancelled(options->user))) {
        return;
    }
    int lock = binary_cache_lock(directory);
    if (lock < 0) {
        return;
    }
    binary_cache_entry *entries = NULL;
    int count = 0;
    av_doc *args = NULL, *result = NULL;
    if (!binary_cache_remove(directory, key) ||
        !binary_cache_list(directory, &entries, &count)) {
        goto done;
    }
    args = av_doc_new();
    av_value *values = av_arr(args), *sizes = av_arr(args), *stamps = av_arr(args);
    av_doc_set_root(args, values);
    av_arr_append(values, sizes);
    av_arr_append(values, stamps);
    for (int i = 0; i < count; i++) {
        av_arr_append(sizes, av_uint(args, entries[i].bytes));
        av_arr_append(stamps, av_uint(args, entries[i].stamp));
    }
    av_arr_append(values, av_uint(args, length + BINARY_CACHE_HEADER_BYTES));
    asset_compiler_diagnostic ignored = { 0 };
    result = asset_source_invoke("project:asset_compiler:1:CacheEvict:callable", values, options, &ignored);
    const av_value *plan = result
                             ? av_obj_get(av_arr_get(av_doc_root(result), 0), "values")
                             : NULL;
    if (av_arr_size(plan) != 2 || !av_get_bool(av_arr_get(plan, 0))) {
        goto done;
    }
    const av_value *remove = av_arr_get(plan, 1);
    if (!av_is_arr(remove) || av_arr_size(remove) > (size_t)count) {
        goto done;
    }
    for (size_t i = 0; i < av_arr_size(remove); i++) {
        const av_value *index = av_arr_get(remove, i);
        if (!av_is_int(index) || av_get_sint(index) < 0 ||
            av_get_uint(index) >= (uint64_t)count ||
            !binary_cache_remove(directory, entries[av_get_int(index)].key)) {
            goto done;
        }
    }
    if (!options->cancelled || !options->cancelled(options->user)) {
        binary_cache_write(directory, key, bytes, length, COMPILER_HOST_MAX_OUTPUT_BYTES);
    }
done:
    av_doc_free(result);
    av_doc_free(args);
    free(entries);
    binary_cache_unlock(lock);
}

av_doc *asset_source_read(
    const char *source,
    const char *path,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
    if (!source || strnlen(source, COMPILER_HOST_MAX_OUTPUT_BYTES + 1) >
                       COMPILER_HOST_MAX_OUTPUT_BYTES) {
        failure(diagnostic, "Asset source must be non-null and at most 16 MiB");
        return NULL;
    }
    if (path && strnlen(path, 4097) > 4096) {
        failure(diagnostic, "Asset source path exceeds 4096 bytes");
        return NULL;
    }
    options = effective_options(options);
    asset_compiler_options invocation_options;
    if (options) {
        invocation_options = *options;
        options = &invocation_options;
    }
    if (!options || !options->compiler || !options->compiler_length) {
        failure(diagnostic, "The packaged Chibi asset compiler is unavailable");
        return NULL;
    }
    if (options->cancelled && options->cancelled(options->user)) {
        failure(diagnostic, "Asset compilation cancelled");
        return NULL;
    }
    path = path ? path : "/project/asset/Asset.chibi";
    size_t request_length;
    char *json = request(source, path, options, &request_length, diagnostic);
    if (!json) {
        return NULL;
    }
    host_lease *compiler = compiler_for(options, diagnostic);
    uint64_t generation = cache_generation;
    compiler_host_bytes compiled = { 0 };
    host_lease *asset = NULL;
    compiler_host_bytes result = { 0 };
    unsigned char *bytes = NULL;
    bool owned_bytes = true;
    av_doc *doc = NULL;
    char *directory = persistent_directory(options);
    char *key = NULL;
    unsigned char *publication = NULL;
    size_t publication_length = 0;
    uint64_t compile_budget = options->compile_budget ? options->compile_budget
                                                      : ASSET_COMPILE_BUDGET;
    uint64_t evaluation_budget = options->evaluation_budget
                                   ? options->evaluation_budget
                                   : ASSET_EVALUATION_BUDGET;
    compiled_asset *cached = find_asset(source, path);
    uint64_t entry = cached ? cached->entry : 0;
    size_t length = cached ? cached->length : 0;
    bool memory_hit = cached != NULL;
    bool from_cache = memory_hit;
    if (cached) {
        bytes = cached->bytes;
        owned_bytes = false;
        asset = host_retain(cached->decoded);
    } else if (compiler && directory) {
        key = persistent_key(json, request_length, options);
        compiler_host_bytes stored = { 0 };
        if (key) {
            stored.bytes = binary_cache_read(
                directory,
                key,
                COMPILER_HOST_MAX_OUTPUT_BYTES,
                &stored.length
            );
        }
        if (stored.bytes && persistent_accept(stored.bytes, stored.length, options)) {
            asset_compiler_diagnostic ignored = { 0 };
            bytes = compiled_bytes(&stored, &entry, &length, &ignored);
            from_cache = bytes != NULL;
        }
        free(stored.bytes);
        if (!bytes && key) {
            binary_cache_remove(directory, key);
        }
    }
prepare:
    if (!bytes) {
        if (!compiler) {
            goto done;
        }
        behavior_execution_scope compilation = { .remaining = compile_budget,
                                                 .prefer_compiled = true,
                                                 .cancelled = options->cancelled,
                                                 .user = options->user };
        behavior_execution_scope_begin(&compilation);
        cache_count(&cache.stats.compile_attempts);
        bool compiled_ok = compiler_host_invoke_bytes(
            compiler->host,
            compiler_host_entry("project:asset_compiler:1:CompileBytes:callable"),
            json,
            request_length,
            host_budget(compile_budget),
            COMPILER_HOST_MAX_OUTPUT_BYTES,
            &compiled
        );
        behavior_execution_scope_end(&compilation);
        if (!compiled_ok) {
            runtime_failure(diagnostic, compiled.diagnostic, &compiled.source);
            goto done;
        }
        if (key && persistent_accept(compiled.bytes, compiled.length, options)) {
            publication_length = compiled.length;
            publication = xmalloc(publication_length);
            memcpy(publication, compiled.bytes, publication_length);
        }
        bytes = compiled_bytes(&compiled, &entry, &length, diagnostic);
        if (!bytes) {
            goto done;
        }
    }
    if (options->cancelled && options->cancelled(options->user)) {
        failure(diagnostic, "Asset evaluation cancelled");
        goto done;
    }
    if (!asset) {
        cache_count(&cache.stats.preparation_attempts);
        asset = host_new(compiler_host_load_values(
            bytes,
            length,
            BEHAVIOR_BYTECODE,
            diagnostic->message,
            sizeof diagnostic->message
        ));
        if (asset && !compiler_host_accepts_byte_entry(
                         asset->host,
                         entry,
                         0,
                         diagnostic->message,
                         sizeof diagnostic->message
                     )) {
            host_release(asset);
            asset = NULL;
        }
    }
    if (!asset && from_cache) {
        if (owned_bytes) {
            free(bytes);
        }
        if (memory_hit && generation == cache_generation) {
            clear_asset(cached);
        }
        cached = NULL;
        memory_hit = false;
        if (!key && directory) {
            key = persistent_key(json, request_length, options);
        }
        if (key) {
            binary_cache_remove(directory, key);
        }
        bytes = NULL;
        owned_bytes = true;
        from_cache = false;
        *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
        goto prepare;
    }
    if (!asset) {
        goto done;
    }
    if (from_cache) {
        cache_count(memory_hit ? &cache.stats.memory_hits : &cache.stats.persistent_hits);
    }
    if (!memory_hit && generation == cache_generation &&
        keep_asset(source, path, bytes, length, entry, asset)) {
        owned_bytes = false;
    }
    if (generation == cache_generation) {
        persistent_publish(directory, key, publication, publication_length, options);
    }
    behavior_execution_scope execution = { .remaining = evaluation_budget,
                                           .cancelled = options->cancelled,
                                           .user = options->user,
                                           .pure = true };
    cache_count(&cache.stats.evaluations);
    behavior_execution_scope_begin(&execution);
    bool invoked = compiler_host_invoke_bytes(
        asset->host,
        entry,
        "[]",
        2,
        host_budget(evaluation_budget),
        COMPILER_HOST_MAX_OUTPUT_BYTES,
        &result
    );
    behavior_execution_scope_end(&execution);
    if (!invoked) {
        runtime_failure(diagnostic, result.diagnostic, &result.source);
        goto done;
    }
    if (memchr(result.bytes, 0, result.length)) {
        failure(diagnostic, "Asset Build returned a transport containing NUL bytes");
        goto done;
    }
    doc = read_asset_transport((const char *)result.bytes, result.length, diagnostic);
done:
    free(directory);
    free(key);
    free(publication);
    free(json);
    if (owned_bytes) {
        free(bytes);
    }
    free(compiled.bytes);
    free(result.bytes);
    host_release(asset);
    host_release(compiler);
    if (doc) {
        metadata_attach(source, av_doc_root(doc), options);
    }
    return doc;
}

av_doc *asset_source_invoke(
    const char *entry,
    const av_value *arguments,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
    options = effective_options(options);
    asset_compiler_options invocation_options;
    if (options) {
        invocation_options = *options;
        options = &invocation_options;
    }
    host_lease *host = compiler_for(options, diagnostic);
    if (!host) {
        return NULL;
    }
    char *json = av_json_write(arguments);
    if (!json) {
        failure(diagnostic, "Asset facade arguments cannot be encoded");
        host_release(host);
        return NULL;
    }
    uint64_t budget = options->compile_budget ? options->compile_budget
                                              : ASSET_COMPILE_BUDGET;
    behavior_execution_scope scope = { .remaining = budget,
                                       .prefer_compiled = true,
                                       .cancelled = options->cancelled,
                                       .user = options->user };
    compiler_host_result result = { 0 };
    behavior_execution_scope_begin(&scope);
    bool ok = compiler_host_invoke(
        host->host,
        compiler_host_entry(entry),
        json,
        strlen(json),
        host_budget(budget),
        &result
    );
    behavior_execution_scope_end(&scope);
    free(json);
    av_doc *doc = ok ? av_json_read(result.json) : NULL;
    if (!doc) {
        runtime_failure(
            diagnostic,
            ok ? "Asset facade returned invalid transport" : result.diagnostic,
            &result.source
        );
    }
    free(result.json);
    host_release(host);
    return doc;
}

typedef struct
{
    const av_value *value;
    const char *key;
    int parent, depth;
} document_item;
av_doc *asset_source_value_columns(const av_value *root, asset_compiler_diagnostic *diagnostic)
{
    av_doc *doc = av_doc_new();
    av_value *args = av_arr(doc);
    av_doc_set_root(doc, args);
    av_value *values[8];
    for (int i = 0; i < 8; i++) {
        values[i] = av_arr(doc);
        av_arr_append(args, values[i]);
    }
    document_item *pending = NULL;
    apush(pending, ((document_item){ .value = root, .key = "", .parent = -1 }));
    int count = 0;
    size_t visited = 0;
    while (acount(pending)) {
        document_item item = pending[acount(pending) - 1];
        asetlen(pending, acount(pending) - 1);
        if (item.depth > ASSET_DOCUMENT_MAX_DEPTH || visited == COMPILER_HOST_MAX_VALUES) {
            failure(diagnostic, "Asset document exceeds value/depth limits");
            afree(pending);
            av_doc_free(doc);
            return NULL;
        }
        visited++;
        int at = count++;
        av_type type = av_get_type(item.value);
        if (type == AV_STRING &&
            memchr(av_get_str(item.value), 0, av_get_len(item.value))) {
            failure(diagnostic, "Asset document text contains unsupported NUL bytes");
            afree(pending);
            av_doc_free(doc);
            return NULL;
        }
        av_arr_append(values[0], av_sint(doc, type));
        av_arr_append(values[1], av_sint(doc, item.parent));
        av_arr_append(values[2], av_str(doc, item.key));
        av_arr_append(values[3], av_str(doc, type == AV_STRING ? av_get_str(item.value) : ""));
        av_arr_append(values[4], av_sint(doc, type == AV_SINT ? av_get_sint(item.value) : 0));
        av_arr_append(values[5], av_uint(doc, type == AV_UINT ? av_get_uint(item.value) : 0));
        av_arr_append(values[6], av_real(doc, type == AV_REAL ? av_get_num(item.value) : 0));
        av_arr_append(values[7], av_bool(doc, type == AV_BOOL && av_get_bool(item.value)));
        size_t children = type == AV_OBJECT ? av_obj_size(item.value)
                        : type == AV_ARRAY  ? av_arr_size(item.value)
                                            : 0;
        size_t remaining = COMPILER_HOST_MAX_VALUES - visited - (size_t)acount(pending);
        if (children > remaining / (type == AV_OBJECT ? 2u : 1u) ||
            (children && item.depth == ASSET_DOCUMENT_MAX_DEPTH)) {
            failure(diagnostic, "Asset document exceeds value limits");
            afree(pending);
            av_doc_free(doc);
            return NULL;
        }
        for (size_t i = children; i > 0; i--) {
            const av_value *child = type == AV_OBJECT
                                      ? av_obj_value_at(item.value, i - 1)
                                      : av_arr_get(item.value, i - 1);
            const char *key = "";
            if (type == AV_OBJECT) {
                const av_value *name = av_obj_key_at(item.value, i - 1);
                key = av_get_str(name);
                if (memchr(key, 0, av_get_len(name))) {
                    failure(diagnostic, "Asset document key contains unsupported NUL bytes");
                    afree(pending);
                    av_doc_free(doc);
                    return NULL;
                }
                visited++;
            }
            apush(
                pending,
                ((
                    document_item
                ){ .value = child, .key = key, .parent = at, .depth = item.depth + 1 })
            );
        }
    }
    afree(pending);
    return doc;
}
char *asset_source_write(
    const av_value *root,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
    av_doc *args = asset_source_value_columns(root, diagnostic);
    if (!args) {
        return NULL;
    }
    char *text = asset_source_invoke_bytes(
        "project:asset_compiler:1:Write:callable",
        av_doc_root(args),
        options,
        diagnostic
    );
    av_doc_free(args);
    return text;
}

char *asset_source_invoke_bytes(
    const char *entry,
    const av_value *arguments,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
    options = effective_options(options);
    asset_compiler_options invocation_options;
    if (options) {
        invocation_options = *options;
        options = &invocation_options;
    }
    host_lease *host = compiler_for(options, diagnostic);
    if (!host) {
        return NULL;
    }
    char *json = av_json_write(arguments);
    if (!json) {
        failure(diagnostic, "Asset facade arguments cannot be encoded");
        host_release(host);
        return NULL;
    }
    uint64_t budget = options->compile_budget ? options->compile_budget
                                              : ASSET_COMPILE_BUDGET;
    behavior_execution_scope scope = { .remaining = budget,
                                       .prefer_compiled = true,
                                       .cancelled = options->cancelled,
                                       .user = options->user };
    compiler_host_bytes result = { 0 };
    behavior_execution_scope_begin(&scope);
    bool ok = compiler_host_invoke_bytes(
        host->host,
        compiler_host_entry(entry),
        json,
        strlen(json),
        host_budget(budget),
        COMPILER_HOST_MAX_OUTPUT_BYTES,
        &result
    );
    behavior_execution_scope_end(&scope);
    free(json);
    char *text = NULL;
    if (ok && !memchr(result.bytes, 0, result.length)) {
        text = xmalloc(result.length + 1);
        memcpy(text, result.bytes, result.length);
        text[result.length] = 0;
    } else {
        runtime_failure(
            diagnostic,
            ok ? "Asset facade returned a NUL byte" : result.diagnostic,
            &result.source
        );
    }
    free(result.bytes);
    host_release(host);
    return text;
}

/* A package closure is larger than an asset and compiles in more instructions:
 * as many as recovering the compiler from its sources, so a test closure may
 * hold the whole compiler. */
#define CLOSURE_COMPILE_BUDGET UINT64_C(4000000000)

/* The first diagnostic of a rejected package compile. */
static void closure_rejection(
    host_lease *host,
    const char *request,
    size_t request_length,
    uint64_t budget,
    asset_compiler_diagnostic *diagnostic
)
{
    compiler_host_result result = { 0 };
    bool called = compiler_host_invoke(
        host->host,
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
    host_lease *host = compiler_for(options, diagnostic);
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
        host->host,
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
    host_release(host);
    *length = result.length;
    return result.bytes;
}
