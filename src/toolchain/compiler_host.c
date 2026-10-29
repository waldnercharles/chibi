#include "toolchain/compiler_host.h"

#include "execution/function_builder.h"
#include "execution/instruction.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "runtime/arrays_definition.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
#include "support/asset_json.h"
#include "support/xalloc.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "toolchain/compiler_target.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"
#include "types/value_document.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

struct compiler_host
{
    native_capabilities *native;
    package_collection *packages;
    execution_module *root;
    size_t package_bytes;
    behavior_backend backend;
    bool values_only;
};
static void message(char *out, size_t size, const char *text)
{
    if (size) {
        snprintf(out, size, "%s", text);
    }
}
static compiler_host *load(
    const void *bytes,
    size_t length,
    behavior_backend backend,
    bool values_only,
    char *error,
    size_t error_size
)
{
    if (!behavior_backend_available(backend)) {
        message(error, error_size, "unsupported compiler host backend");
        return NULL;
    }
    const native_definition_pack packs[] = { values_only
                                                 ? NATIVE_TEXT_DEFINITION_PACK
                                                 : NATIVE_COMPILER_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    compiler_host *host = xcalloc(1, sizeof *host);
    host->backend = backend;
    host->values_only = values_only;
    host->native = native_capabilities_from_definition_packs(NULL, 0, packs, values_only ? 2 : 4, error, error_size);
    if (!host->native) {
        compiler_host_free(host);
        return NULL;
    }
    native_prepared_context context = { .native = host->native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    linker.decoded_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES;
    linker.decoded_records = COMPILER_HOST_MAX_CLOSURE_RECORDS;
    linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root;
    host->packages = compiler_artifact_read(bytes, length, &linker, backend, &remaining, &root, error, error_size);
    if (!host->packages) {
        compiler_host_free(host);
        return NULL;
    }
    host->package_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES - remaining;
    for (int i = 0; i < package_collection_count(host->packages); i++) {
        execution_module *module = package_collection_module(host->packages, i);
        if (execution_module_id(module) == root) {
            host->root = module;
            break;
        }
    }
    if (!host->root) {
        message(error, error_size, "compiler artifact root was not loaded");
        compiler_host_free(host);
        return NULL;
    }
    return host;
}
compiler_host *compiler_host_load(
    const void *bytes,
    size_t length,
    behavior_backend backend,
    char *error,
    size_t size
)
{
    return load(bytes, length, backend, false, error, size);
}
compiler_host *compiler_host_load_values(
    const void *bytes,
    size_t length,
    behavior_backend backend,
    char *error,
    size_t size
)
{
    return load(bytes, length, backend, true, error, size);
}
size_t compiler_host_storage_bytes(const compiler_host *host)
{
    assert(host);
    size_t bytes = native_capabilities_storage_bytes(host->native);
    if (bytes > SIZE_MAX - sizeof *host) {
        return SIZE_MAX;
    }
    bytes += sizeof *host;
    return host->package_bytes > SIZE_MAX - bytes ? SIZE_MAX
                                                  : bytes + host->package_bytes;
}
const execution_module *compiler_host_root(const compiler_host *host)
{
    return host->root;
}
uint64_t compiler_host_entry(const char *symbol)
{
    return script_catalog_id("package-symbol", symbol);
}
char *compiler_host_target_json(const compiler_host *host, size_t *length)
{
    return compiler_target_json(native_capabilities_catalog(host->native), length);
}
char *compiler_host_values_target_json(size_t *length, char *error, size_t size)
{
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK };
    native_capabilities
        *native = native_capabilities_from_definition_packs(NULL, 0, packs, 2, error, size);
    if (!native) {
        return NULL;
    }
    char *json = compiler_target_json(native_capabilities_catalog(native), length);
    native_capabilities_free(native);
    return json;
}
char *compiler_host_compiler_target_json(size_t *length, char *error, size_t size)
{
    const native_definition_pack packs[] = { NATIVE_COMPILER_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    native_capabilities
        *native = native_capabilities_from_definition_packs(NULL, 0, packs, 4, error, size);
    if (!native) {
        return NULL;
    }
    char *json = compiler_target_json(native_capabilities_catalog(native), length);
    native_capabilities_free(native);
    return json;
}
void compiler_host_free(compiler_host *host)
{
    if (!host) {
        return;
    }
    if (host->packages) {
        package_collection_free(host->packages);
    }
    native_capabilities_free(host->native);
    free(host);
}
typedef struct
{
    size_t values, bytes;
} quota;
static bool charge(quota *q, size_t bytes)
{
    if (++q->values > COMPILER_HOST_MAX_VALUES ||
        bytes > COMPILER_HOST_MAX_BYTES - q->bytes) {
        return false;
    }
    q->bytes += bytes;
    return true;
}
static bool string_size(quota *q, const char *text)
{
    size_t room = (COMPILER_HOST_MAX_BYTES - q->bytes) / 6;
    size_t length = text ? strnlen(text, room + 1) : 0;
    return length <= room && charge(q, length * 6);
}
static const yyjson_read_flag READ_FLAGS = COMPILER_HOST_JSON_READ_FLAGS;
static const yyjson_write_flag WRITE_FLAGS = YYJSON_WRITE_ALLOW_INVALID_UNICODE |
                                             YYJSON_WRITE_ALLOW_INF_AND_NAN;
/* Inspect the external JSON tree before recursive av_from_json allocation. */
static bool json_quota(const yyjson_val *value, int depth, quota *q)
{
    if (!value || depth >= COMPILER_HOST_MAX_DEPTH || !charge(q, 128)) {
        return false;
    }
    if (yyjson_is_str(value)) {
        size_t length = yyjson_get_len(value);
        return length <= (COMPILER_HOST_MAX_BYTES - q->bytes) / 6 &&
               charge(q, length * 6);
    }
    if (yyjson_is_arr(value)) {
        size_t i, count;
        yyjson_val *child;
        yyjson_arr_foreach((yyjson_val *)value, i, count, child)
        {
            if (!json_quota(child, depth + 1, q)) {
                return false;
            }
        }
    } else if (yyjson_is_obj(value)) {
        size_t i, count;
        yyjson_val *key, *child;
        yyjson_obj_foreach((yyjson_val *)value, i, count, key, child)
        {
            if (!json_quota(key, depth + 1, q) || !json_quota(child, depth + 1, q)) {
                return false;
            }
        }
    }
    return true;
}
/* The generic reader allocates a full declared row before checking its
 * fields. Count that storage for EVERY offered row, including malformed sparse
 * rows, before any conversion allocation. It applies no source defaults. */
static bool signature_quota(pin_value declaration, const yyjson_val *value, quota *q)
{
    size_t count = 1;
    bool array = declaration.container == PIN_CONTAINER_ARRAY ||
                 declaration.container == PIN_CONTAINER_SET;
    if (array) {
        if (!yyjson_is_arr(value)) {
            return false;
        }
        count = yyjson_arr_size(value);
        if (count > COMPILER_HOST_MAX_VALUES ||
            count > (COMPILER_HOST_MAX_BYTES - q->bytes) / sizeof(pin_value) ||
            !charge(q, count * sizeof(pin_value)) ||
            !charge(q, sizeof(pin_array) + 128)) {
            return false;
        }
    } else if (declaration.container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (declaration.record) {
        const pin_record *record = declaration.record;
        if (declaration.type != PIN_STRUCT ||
            record->bytes > COMPILER_HOST_MAX_BYTES - 128 ||
            !charge(q, record->bytes + 128) ||
            record->type.size > COMPILER_HOST_MAX_BYTES - 128) {
            return false;
        }
        size_t row = record->type.size + 128;
        if (count > (COMPILER_HOST_MAX_BYTES - q->bytes) / row ||
            !charge(q, count * row)) {
            return false;
        }
        for (size_t i = 0; i < count; i++) {
            const yyjson_val *object = array ? yyjson_arr_get(value, i) : value;
            if (yyjson_is_null(object)) {
                continue; /* Generic document reads permit a null struct. */
            }
            if (!yyjson_is_obj(object)) {
                return false;
            }
            for (int field = 0; field < record->type.field_count; field++) {
                /* Missing fields never acquire defaults in the document API. */
                if (!yyjson_obj_get(object, record->type.fields[field].name)) {
                    return false;
                }
            }
        }
    } else {
        if (declaration.type == PIN_STRUCT) {
            return false;
        }
        if (declaration.type_name) {
            size_t name = strnlen(declaration.type_name, COMPILER_HOST_MAX_BYTES);
            if (name >= COMPILER_HOST_MAX_BYTES - 129) {
                return false;
            }
            name += 129;
            size_t copies = count + (array ? 1 : 0);
            if (copies > (COMPILER_HOST_MAX_BYTES - q->bytes) / name ||
                !charge(q, copies * name)) {
                return false;
            }
        }
    }
    return true;
}
/* Validate a bounded projection before the recursive generic document writer.
 * Values are produced by the verified executable, not parsed native pointers. */
static bool value_quota(const pin_value *value, int depth, quota *q)
{
    if (!value || depth >= COMPILER_HOST_MAX_DEPTH || !charge(q, 128)) {
        return false;
    }
    if (value->container == PIN_CONTAINER_ARRAY || value->container == PIN_CONTAINER_SET) {
        const pin_array *array = value->ptr;
        if (!array) {
            return true;
        }
        if (!pin_array_valid(array) ||
            (size_t)array->count > COMPILER_HOST_MAX_VALUES - q->values) {
            return false;
        }
        for (int i = 0; i < array->count; i++) {
            pin_value item = pin_array_get(array, i);
            if (!value_quota(&item, depth + 1, q)) {
                return false;
            }
        }
        return true;
    }
    if (value->container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (value->type == PIN_STR) {
        size_t length = pin_text(*value).length;
        size_t overhead = sizeof(pin_string) + 1;
        size_t room = COMPILER_HOST_MAX_BYTES - q->bytes;
        return room >= overhead && length <= (room - overhead) / 6 &&
               charge(q, overhead + length * 6);
    }
    if (value->type == PIN_NAME) {
        return string_size(q, value->str);
    }
    if (value->type == PIN_UNION) {
        const pin_union *tagged = value->ptr;
        if (!tagged) {
            return true;
        }
        if (tagged->count < 0 ||
            (size_t)tagged->count > COMPILER_HOST_MAX_VALUES - q->values) {
            return false;
        }
        for (int i = 0; i < tagged->count; i++) {
            if (!value_quota(&tagged->values[i], depth + 1, q)) {
                return false;
            }
        }
    } else if (value->type == PIN_STRUCT && value->ptr) {
        /* This host admits no native struct packs. Authored layouts travel with
         * the prepared signature/constants and are independent of producer catalogs. */
        const type_desc *type = value->record ? pin_value_type(value) : NULL;
        if (!type || type->field_count < 0 ||
            (size_t)type->field_count > COMPILER_HOST_MAX_VALUES - q->values) {
            return false;
        }
        for (int i = 0; i < type->field_count; i++) {
            pin_value field;
            if (!string_size(q, type->fields[i].name)) {
                return false;
            }
            if (pin_read_field(NULL, value->ptr, &type->fields[i], &field)) {
                return false;
            }
            if (!value_quota(&field, depth + 1, q)) {
                return false;
            }
        }
    }
    return true;
}
/* Only value-bank callables can be represented by this host's owned transport. */
static const behavior_function *entry_function(compiler_host *host, uint64_t entry, char *error, size_t error_size)
{
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(host->root); i++) {
        execution_module_function_info info = execution_module_function_at(host->root, i);
        if (info.id == entry && info.kind == EXECUTION_FUNCTION_AUTHORED) {
            function = info.body;
            break;
        }
    }
    if (!function) {
        message(error, error_size, "compiler entry ID was not found");
        return NULL;
    }
    int argument_count, result_count;
    behavior_function_arguments(function, &argument_count);
    behavior_function_results(function, &result_count);
    if (argument_count < 0 || result_count < 0 ||
        argument_count > COMPILER_HOST_MAX_VALUES ||
        result_count > COMPILER_HOST_MAX_VALUES) {
        message(error, error_size, "invalid compiler entry signature");
        return NULL;
    }
    /* Physical U64 storage is also used for executable addresses. Admission is
     * based on prepared provenance, never scalar width: JSON numbers must not
     * become caller-supplied native addresses or language reference objects. */
    for (int i = 0; i < argument_count; i++) {
        if (behavior_function_kind(function, BEHAVIOR_BANK_ARGUMENT, i) != BEHAVIOR_VALUE) {
            message(error, error_size, "compiler entry requires an address or reference argument");
            return NULL;
        }
    }
    for (int i = 0; i < result_count; i++) {
        if (behavior_function_kind(function, BEHAVIOR_BANK_RESULT, i) != BEHAVIOR_VALUE) {
            message(error, error_size, "compiler entry returns an address or reference result");
            return NULL;
        }
    }
    return function;
}
typedef struct
{
    void *data;
    size_t length;
    int flow_status;
    behavior_run_counts counts;
    char diagnostic[256];
    compiler_host_source source;
} invocation_result;
static bool byte_type(const pin_value *value, pin_container container)
{
    return value->type == PIN_U8 && value->container == container &&
           !value->record && !value->type_name;
}
bool compiler_host_accepts_byte_entry(
    compiler_host *host,
    uint64_t entry,
    int arguments,
    char *error,
    size_t error_size
)
{
    if (!host || !entry || arguments < 0) {
        message(error, error_size, "invalid compiler byte entry query");
        return false;
    }
    const behavior_function *function = entry_function(host, entry, error, error_size);
    if (!function) {
        return false;
    }
    int argument_count, result_count;
    behavior_function_arguments(function, &argument_count);
    const pin_value *results = behavior_function_results(function, &result_count);
    if (argument_count != arguments || result_count != 1 ||
        !byte_type(&results[0], PIN_CONTAINER_ARRAY)) {
        message(error, error_size, "compiler byte entry has an incompatible signature");
        return false;
    }
    return true;
}
/* A byte result's storage for `limit` octets: its header, its items as values
 * with growth's spare capacity, and the copy of its octets. */
static size_t output_storage(size_t limit)
{
    return sizeof(pin_array) + limit * (2 * sizeof(pin_value) + 1);
}
/* Bytes may be packed octets or boxed values. Copy while the runtime arena is
 * alive, and expose nothing until every item has been checked. */
static bool extract_bytes(const pin_value *value, size_t limit, invocation_result *out)
{
    if (!byte_type(value, PIN_CONTAINER_ARRAY)) {
        return false;
    }
    const pin_array *array = value->ptr;
    if (!array) {
        return true; /* Existing typed empty-array representation. */
    }
    if (array->element_type != PIN_U8 || array->record || array->type_name ||
        !pin_array_valid(array) || (size_t)array->count > limit ||
        pin_array_storage_bytes(array) > output_storage(limit) - sizeof *array) {
        return false;
    }
    size_t length = (size_t)array->count;
    size_t represented = sizeof *array + pin_array_storage_bytes(array);
    if (length > output_storage(limit) - represented) {
        return false;
    }
    unsigned char *bytes = length ? xmalloc(length) : NULL;
    for (size_t i = 0; i < length; i++) {
        pin_value item = pin_array_get(array, (int)i);
        if (!byte_type(&item, PIN_CONTAINER_SINGLE)) {
            free(bytes);
            return false;
        }
        bytes[i] = item.u8;
    }
    out->data = bytes;
    out->length = length;
    return true;
}
static bool invoke(
    compiler_host *host,
    uint64_t entry,
    const char *json,
    size_t length,
    uintptr_t budget,
    size_t max_bytes,
    invocation_result *out
)
{
    if (!out) {
        return false;
    }
    *out = (invocation_result){ 0 };
    const char *failure = NULL;
    yyjson_doc *parsed = NULL;
    av_doc *input = NULL, *output = NULL;
    yyjson_mut_doc *encoded = NULL;
    arena storage = { 0 };
    behavior_managed_heap managed = { 0 };
    behavior_function_activation activation = { 0 };
    behavior_function_context context = { 0 };
    pin_value *arguments = NULL, *results = NULL;
    const behavior_function *function = NULL;
    if (!host || !entry || !json || !length ||
        length > COMPILER_HOST_MAX_BYTES || !budget) {
        failure = "invalid compiler host invocation";
        goto done;
    }
    if (host->values_only && behavior_execution_scope_active() &&
        !behavior_execution_scope_pure()) {
        failure = "values-only invocation requires a pure execution scope";
        goto done;
    }
    function = entry_function(host, entry, out->diagnostic, sizeof out->diagnostic);
    if (!function) {
        failure = "unsupported compiler entry signature";
        goto done;
    }
    int argument_count, result_count;
    const pin_value *declarations = behavior_function_arguments(function, &argument_count);
    const pin_value *returns = behavior_function_results(function, &result_count);
    if (max_bytes &&
        (result_count != 1 || !byte_type(&returns[0], PIN_CONTAINER_ARRAY))) {
        failure = "compiler byte entry must return exactly one primitive "
                  "u8[]";
        goto done;
    }
    parsed = yyjson_read(json, length, READ_FLAGS);
    quota q = { 0 };
    if (!parsed || !json_quota(yyjson_doc_get_root(parsed), 0, &q) ||
        !yyjson_is_arr(yyjson_doc_get_root(parsed))) {
        failure = "invalid or oversized compiler argument document";
        goto done;
    }
    if (yyjson_arr_size(yyjson_doc_get_root(parsed)) != (size_t)argument_count) {
        failure = "compiler argument count mismatch";
        goto done;
    }
    size_t bank_count = (size_t)argument_count + (size_t)result_count;
    if (bank_count > (COMPILER_HOST_MAX_BYTES - q.bytes) / sizeof(pin_value) ||
        !charge(&q, bank_count * sizeof(pin_value))) {
        failure = "compiler signature banks exceed shared storage limits";
        goto done;
    }
    /* Shared transport + signature allocation bound; no argument is decoded
     * until all row/layout amplification has been reserved. */
    for (int i = 0; i < argument_count; i++) {
        if (!signature_quota(
                declarations[i],
                yyjson_arr_get(yyjson_doc_get_root(parsed), (size_t)i),
                &q
            )) {
            failure = "compiler arguments exceed signature or shared storage "
                      "limits";
            goto done;
        }
    }
    for (int i = 0; i < result_count; i++) {
        if (returns[i].record) {
            const pin_record *record = returns[i].record;
            if (record->bytes > COMPILER_HOST_MAX_BYTES - 128 ||
                !charge(&q, record->bytes + 128) ||
                record->type.size > COMPILER_HOST_MAX_BYTES - 128 ||
                !charge(&q, record->type.size + 128)) {
                failure = "compiler result declarations exceed shared storage "
                          "limits";
                goto done;
            }
        }
    }
    /* Reserve the conservative decoded-storage upper bound once. Alignment,
     * layouts, every row and copied text were charged before this allocation;
     * argument conversion therefore does not rely on a post-growth limit. */
    if (q.bytes > COMPILER_HOST_MAX_BYTES - 128) {
        failure = "compiler storage reservation exceeds shared byte limit";
        goto done;
    }
    arena_init(&storage, q.bytes);
    input = av_doc_new();
    av_value *values = av_from_json(input, yyjson_doc_get_root(parsed));
    av_doc_set_root(input, values);
    arguments = xcalloc((size_t)argument_count, sizeof *arguments);
    results = xcalloc((size_t)result_count, sizeof *results);
    for (int i = 0; i < argument_count; i++) {
        if (pin_value_read_document(
                &storage,
                declarations[i],
                av_arr_get(values, (size_t)i),
                &arguments[i],
                out->diagnostic,
                sizeof out->diagnostic
            )) {
            failure = "compiler argument does not match the decoded signature";
            goto done;
        }
        if (arena_storage_bytes(&storage) > COMPILER_HOST_MAX_BYTES) {
            failure = "compiler arguments exceed shared owned-storage limit";
            goto done;
        }
    }
    if (result_count) {
        memcpy(results, returns, (size_t)result_count * sizeof *results);
    }
    behavior_function_prepare_results(function, results, &storage);
    bool resumable = behavior_function_resumable(function);
    if (resumable) {
        behavior_function_activation_init(function, &activation);
    }
    behavior_managed_heap_init(&managed);
    managed.call_collection_bytes = BEHAVIOR_CALL_COLLECTION_BYTES;
    context = (behavior_function_context){ .managed_heap = &managed,
                                           .activation = resumable ? &activation : NULL,
                                           .arguments = arguments,
                                           .argument_count = argument_count,
                                           .results = results,
                                           .budget = budget };
    /* Values live in the heap, so collections also reclaim what long-running
     * frames stop using. */
    behavior_function_bind_storage(&context, &managed.storage);
    if (!behavior_function_bind(function, &context, NULL, 0)) {
        failure = "compiler runtime binding failed";
        goto done;
    }
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    behavior_execution_scope execution = { .remaining = budget, .pure = true };
    bool owned_scope = host->values_only && !behavior_execution_scope_active();
    if (owned_scope) {
        behavior_execution_scope_begin(&execution);
    }
    out->flow_status = behavior_function_run(function, &context, host->backend != BEHAVIOR_BYTECODE);
    if (owned_scope) {
        behavior_execution_scope_end(&execution);
    }
    behavior_run_scope_end(&scope);
    out->counts = scope.counts;
    if (out->flow_status != FLOW_OK || !context.returned) {
        failure = context.error_message ? context.error_message
                                        : "compiler execution did not return";
        if (!context.error_message && out->flow_status != FLOW_OK) {
            snprintf(
                out->diagnostic,
                sizeof out->diagnostic,
                "compiler execution stopped: %s (status %d, %ju steps)",
                behavior_function_failure_text(context.failure),
                out->flow_status,
                (uintmax_t)context.steps
            );
        }
        goto done;
    }
    if (max_bytes) {
        if (!extract_bytes(&results[0], max_bytes, out)) {
            failure = "compiler byte result exceeds type, shape or storage "
                      "limits";
        }
        goto done;
    }
    q = (quota){ 0 };
    for (int i = 0; i < result_count; i++) {
        if (!value_quota(&results[i], 0, &q)) {
            failure = "compiler result exceeds projection limits";
            goto done;
        }
    }
    output = av_doc_new();
    av_value *projected = av_arr(output);
    av_doc_set_root(output, projected);
    for (int i = 0; i < result_count; i++) {
        av_value *value = pin_value_to_document(output, &results[i]);
        if (!value) {
            failure = "compiler result cannot be projected";
            goto done;
        }
        av_arr_append(projected, value);
    }
    encoded = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(encoded, av_to_json(encoded, projected));
    out->data = yyjson_mut_write(encoded, WRITE_FLAGS, &out->length);
    if (!out->data || out->length > COMPILER_HOST_MAX_BYTES) {
        free(out->data);
        out->data = NULL;
        out->length = 0;
        failure = "compiler result cannot be encoded within byte limit";
    }
done:
    if (failure && out->flow_status < 0) {
        const behavior_function_source *source = behavior_function_failure_source(&context);
        if (source) {
            out->source = (compiler_host_source){ .present = true,
                                                  .start = source->start,
                                                  .end = source->end,
                                                  .line = source->line,
                                                  .column = source->column };
            const char *asset = source->asset_name ? source->asset_name : "";
            int count = snprintf(out->source.asset, sizeof out->source.asset, "%s", asset);
            out->source.truncated = count < 0 ||
                                    (size_t)count >= sizeof out->source.asset;
        }
    }
    if (failure && !out->diagnostic[0]) {
        message(out->diagnostic, sizeof out->diagnostic, failure);
    }
    yyjson_mut_doc_free(encoded);
    yyjson_doc_free(parsed);
    av_doc_free(input);
    av_doc_free(output);
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&managed);
    arena_free(&storage);
    free(arguments);
    free(results);
    return failure == NULL;
}

bool compiler_host_invoke(
    compiler_host *host,
    uint64_t entry,
    const char *json,
    size_t length,
    uintptr_t budget,
    compiler_host_result *out
)
{
    if (!out) {
        return false;
    }
    *out = (compiler_host_result){ 0 };
    invocation_result result;
    bool ok = invoke(host, entry, json, length, budget, 0, &result);
    out->json = result.data;
    out->length = result.length;
    out->flow_status = result.flow_status;
    out->counts = result.counts;
    out->source = result.source;
    memcpy(out->diagnostic, result.diagnostic, sizeof out->diagnostic);
    return ok;
}
bool compiler_host_invoke_bytes(
    compiler_host *host,
    uint64_t entry,
    const char *json,
    size_t length,
    uintptr_t budget,
    size_t max_bytes,
    compiler_host_bytes *out
)
{
    if (!out) {
        return false;
    }
    *out = (compiler_host_bytes){ 0 };
    if (!max_bytes || max_bytes > COMPILER_HOST_MAX_ARTIFACT_BYTES) {
        message(out->diagnostic, sizeof out->diagnostic, "invalid compiler byte output limit");
        return false;
    }
    invocation_result result;
    bool ok = invoke(host, entry, json, length, budget, max_bytes, &result);
    out->bytes = result.data;
    out->length = result.length;
    out->flow_status = result.flow_status;
    out->counts = result.counts;
    out->source = result.source;
    memcpy(out->diagnostic, result.diagnostic, sizeof out->diagnostic);
    return ok;
}

static void sum_counter(uint64_t *total, uint64_t value, bool *overflow)
{
    if (value > UINT64_MAX - *total) {
        *total = UINT64_MAX;
        *overflow = true;
    } else {
        *total += value;
    }
}
static void sum_counts(behavior_run_counts *total, const behavior_run_counts *value)
{
    total->overflow |= value->overflow;
    sum_counter(&total->bytecode_entries, value->bytecode_entries, &total->overflow);
    sum_counter(&total->sljit_entries, value->sljit_entries, &total->overflow);
    sum_counter(&total->wasm_entries, value->wasm_entries, &total->overflow);
    sum_counter(
        &total->inspected_sljit_entries,
        value->inspected_sljit_entries,
        &total->overflow
    );
    sum_counter(
        &total->inspected_wasm_entries,
        value->inspected_wasm_entries,
        &total->overflow
    );
    sum_counter(
        &total->inspection_bytecode_fallbacks,
        value->inspection_bytecode_fallbacks,
        &total->overflow
    );
    sum_counter(
        &total->size_bytecode_fallbacks,
        value->size_bytecode_fallbacks,
        &total->overflow
    );
    sum_counter(&total->native_entries, value->native_entries, &total->overflow);
}
bool compiler_host_invoke_batch(
    compiler_host *host,
    uint64_t entry,
    const char *json,
    size_t length,
    uintptr_t budget,
    compiler_host_result *out
)
{
    if (!out) {
        return false;
    }
    *out = (compiler_host_result){ 0 };
    if (!host || !entry || !json || !length ||
        length > COMPILER_HOST_MAX_BYTES || !budget) {
        message(out->diagnostic, sizeof out->diagnostic, "invalid compiler batch invocation");
        return false;
    }
    if (!entry_function(host, entry, out->diagnostic, sizeof out->diagnostic)) {
        return false;
    }
    yyjson_doc *parsed = yyjson_read(json, length, READ_FLAGS);
    yyjson_val *requests = parsed ? yyjson_doc_get_root(parsed) : NULL;
    quota input_quota = { 0 }, output_quota = { 0 };
    if (!requests || !yyjson_is_arr(requests) || !yyjson_arr_size(requests) ||
        yyjson_arr_size(requests) > COMPILER_HOST_MAX_BATCH ||
        !json_quota(requests, 0, &input_quota)) {
        yyjson_doc_free(parsed);
        message(out->diagnostic, sizeof out->diagnostic, "invalid or oversized compiler batch");
        return false;
    }
    size_t used = 1;
    char *combined = xmalloc(2);
    combined[0] = '[';
    combined[1] = 0;
    size_t index, count;
    yyjson_val *request;
    bool ok = true;
    yyjson_arr_foreach(requests, index, count, request)
    {
        size_t request_length = 0;
        char *request_text = yyjson_val_write(request, WRITE_FLAGS, &request_length);
        compiler_host_result result = { 0 };
        bool invoked = request_text &&
                       compiler_host_invoke(host, entry, request_text, request_length, budget, &result);
        free(request_text);
        sum_counts(&out->counts, &result.counts);
        out->flow_status = result.flow_status;
        yyjson_doc *result_doc = invoked
                                   ? yyjson_read(result.json, result.length, READ_FLAGS)
                                   : NULL;
        bool fits = result_doc &&
                    json_quota(yyjson_doc_get_root(result_doc), 1, &output_quota) &&
                    used <= COMPILER_HOST_MAX_BYTES - 2 &&
                    result.length <= COMPILER_HOST_MAX_BYTES - used - 2;
        yyjson_doc_free(result_doc);
        if (!invoked || !fits) {
            out->source = result.source;
            snprintf(
                out->diagnostic,
                sizeof out->diagnostic,
                "compiler batch item %zu: %.190s",
                index,
                result.diagnostic[0] ? result.diagnostic : "result exceeds shared transport limits"
            );
            free(result.json);
            ok = false;
            break;
        }
        size_t separator = index ? 1 : 0;
        char *grown = xmalloc(used + separator + result.length + 2);
        memcpy(grown, combined, used);
        free(combined);
        combined = grown;
        if (separator) {
            combined[used++] = ',';
        }
        memcpy(combined + used, result.json, result.length);
        used += result.length;
        combined[used] = 0;
        free(result.json);
    }
    yyjson_doc_free(parsed);
    if (!ok) {
        free(combined);
        return false;
    }
    combined[used++] = ']';
    combined[used] = 0;
    out->json = combined;
    out->length = used;
    return true;
}
