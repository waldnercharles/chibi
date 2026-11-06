#include "compiler_artifact.h"

#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "host/process.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

/* Thirteen supervised children: the original four plus nine binary/usage
 * children. The source producer has three bounded cases; product hosts receive
 * only prepared artifacts and requests. See docs/testing.md for exact counts. */
enum
{
    ARTIFACT_CAP = 16 * 1024 * 1024,
    OUTPUT_CAP = 256 * 1024,
    ERROR_CAP = 4096,
    BINARY_CAP = 64 * 1024
};
/* Ordinary outputs keep the 16 MiB default; compiler bootstrap closures and
 * the CLI's file outputs pass the artifact bound. */
_Static_assert(COMPILER_HOST_MAX_OUTPUT_BYTES == 16u * 1024u * 1024u, "default byte result bound");
_Static_assert(COMPILER_HOST_MAX_ARTIFACT_BYTES == 32u * 1024u * 1024u, "compiled closure bound");
static const char *producer_path, *host_path;
static char directory[128], entry[32];
static bool directory_created;
static unsigned char *artifact, *changed, *golden;
static size_t artifact_size, golden_size;
static char output[OUTPUT_CAP + 2], errors[ERROR_CAP];
static chibi_process_output out, err;
static behavior_function_builder *builder;
static execution_module_plan *plan;
static execution_module *module;
static compiler_host *loaded;
static unsigned char *tiny_blob, *tiny_artifact;
static compiler_host_result invocation;
static compiler_host_bytes byte_result;
static unsigned char *byte_input, *binary_artifact;
static char binary_entries[4][32];
static uint64_t binary_ids[4];
extern char **environ;

static void cleanup_case()
{
    free(byte_result.bytes);
    byte_result = (compiler_host_bytes){ 0 };
    free(byte_input);
    byte_input = NULL;
    free(binary_artifact);
    binary_artifact = NULL;
    free(changed);
    changed = NULL;
    if (builder) {
        behavior_function_builder_free(builder);
    }
    builder = NULL;
    execution_module_plan_free(plan);
    plan = NULL;
    execution_module_free(module);
    module = NULL;
    compiler_host_free(loaded);
    loaded = NULL;
    free(tiny_blob);
    tiny_blob = NULL;
    free(tiny_artifact);
    tiny_artifact = NULL;
    free(invocation.json);
    invocation = (compiler_host_result){ 0 };
}
static bool path(char *result, size_t capacity, const char *name)
{
    int n = snprintf(result, capacity, "%s/%s", directory, name);
    REQUIRE(n > 0 && (size_t)n < capacity);
    return true;
}
static bool read_bytes(const char *name, size_t cap, unsigned char **result, size_t *size)
{
    char filename[4096];
    REQUIRE(path(filename, sizeof filename, name));
    FILE *stream = fopen(filename, "rb");
    REQUIRE(stream);
    bool valid = fseek(stream, 0, SEEK_END) == 0;
    long length = valid ? ftell(stream) : -1;
    valid = length > 0 && (unsigned long)length <= cap &&
            fseek(stream, 0, SEEK_SET) == 0;
    unsigned char *bytes = valid ? malloc((size_t)length + 1) : NULL;
    valid = bytes && fread(bytes, 1, (size_t)length, stream) == (size_t)length;
    valid = fclose(stream) == 0 && valid;
    if (!valid) {
        free(bytes);
    }
    REQUIRE(valid);
    bytes[length] = 0;
    *result = bytes;
    *size = (size_t)length;
    return true;
}
static bool capture(const char *const *argv, int expected_status)
{
    REQUIRE(argv[0] && argv[0][0] == '/');
    double deadline = chibi_process_now() + TOOL_RUN_SECONDS;
    out = (chibi_process_output){ .data = output, .capacity = sizeof output };
    err = (chibi_process_output){ .data = errors, .capacity = sizeof errors };
    chibi_process_capture_result result = chibi_process_capture(
        argv[0],
        argv,
        directory,
        (const char *const *)environ,
        deadline,
        NULL,
        &out,
        &err
    );
    if (result.error != CHIBI_PROCESS_OK || result.status != expected_status) {
        fprintf(
            stderr,
            "Compiler child error=%d status=%d: %s\n%s\n",
            result.error,
            result.status,
            errors,
            output
        );
    }
    REQUIRE(result.error == CHIBI_PROCESS_OK && result.status == expected_status);
    REQUIRE(!out.truncated && !err.truncated);
    return true;
}
static bool product(const char *filename, const char *backend, int status)
{
    const char *argv[] = { host_path,       "--artifact", filename,
                           "--entry",       entry,        "--input",
                           "requests.json", "--backend",  backend,
                           "--budget",      "3000000",    "--timeout-ms",
                           "10000",         "--batch",    NULL };
    REQUIRE(capture(argv, status));
    return true;
}
TEST_CASE(actual_compiler_artifact_runs_both_product_backends)
{
    strcpy(directory, "/tmp/chibi-compiler-artifact-XXXXXX");
    REQUIRE(mkdtemp(directory));
    directory_created = true;
    const char *argv[] = { producer_path, "--compiler-artifact-write", directory, NULL };
    REQUIRE(capture(argv, 0));
    REQUIRE(read_bytes("compiler.artifact", ARTIFACT_CAP, &artifact, &artifact_size));
    REQUIRE(read_bytes("expected.json", OUTPUT_CAP, &golden, &golden_size));
    unsigned char *id = NULL;
    size_t id_size;
    REQUIRE(read_bytes("entry.txt", sizeof entry - 1, &id, &id_size));
    memcpy(entry, id, id_size + 1);
    free(id);
    for (int b = 0; b < 2; b++) {
        REQUIRE(product("compiler.artifact", b ? "sljit" : "bytecode", 0));
        REQUIRE(
            out.size == golden_size + 1 && output[golden_size] == '\n' &&
            !memcmp(output, golden, golden_size)
        );
        uint64_t bytecode, sljit, wasm, fallback;
        unsigned overflow;
        int used = 0;
        REQUIRE(
            sscanf(
                errors,
                "compiler-host bytecode=%" SCNu64 " sljit=%" SCNu64
                " wasm=%" SCNu64 " fallback=%" SCNu64 " overflow=%u\n%n",
                &bytecode,
                &sljit,
                &wasm,
                &fallback,
                &overflow,
                &used
            ) == 5
        );
        REQUIRE((size_t)used == err.size && !wasm && !fallback && !overflow);
        REQUIRE(b ? sljit > 0 && bytecode == 0 : bytecode > 0 && sljit == 0);
    }
    return true;
}
static bool rejected(const unsigned char *bytes, size_t length)
{
    size_t remaining = ARTIFACT_CAP;
    uint64_t root = UINT64_MAX;
    char error[256] = { 0 };
    package_collection *collection = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    bool valid = collection == NULL;
    if (collection) {
        package_collection_free(collection);
    }
    REQUIRE(valid && remaining == ARTIFACT_CAP && root == 0 && error[0]);
    return true;
}
TEST_CASE(artifact_truncated_container_rejected_before_decode)
{
    REQUIRE(artifact && artifact_size > 32);
    REQUIRE(rejected(artifact, artifact_size - 1));
    return true;
}
static uint64_t little_endian(const unsigned char *bytes)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= (uint64_t)bytes[i] << (8 * i);
    }
    return value;
}
TEST_CASE(artifact_duplicate_module_id_rejected_before_decode)
{
    REQUIRE(artifact && artifact_size > 56 && little_endian(artifact + 24) == 8);
    uint64_t name = little_endian(artifact + 40), blob = little_endian(artifact + 48);
    REQUIRE(name <= artifact_size - 56 && blob <= artifact_size - 56 - name);
    size_t second = 56 + (size_t)name + (size_t)blob;
    REQUIRE(second <= artifact_size - 24);
    changed = malloc(artifact_size);
    REQUIRE(changed);
    memcpy(changed, artifact, artifact_size);
    memcpy(changed + second, artifact + 32, 8);
    REQUIRE(rejected(changed, artifact_size));
    return true;
}
TEST_CASE(artifact_trailing_data_rejected_before_decode)
{
    REQUIRE(artifact && artifact_size < ARTIFACT_CAP);
    changed = malloc(artifact_size + 1);
    REQUIRE(changed);
    memcpy(changed, artifact, artifact_size);
    changed[artifact_size] = 0;
    REQUIRE(rejected(changed, artifact_size + 1));
    return true;
}
TEST_CASE(product_cli_rejects_malformed_artifact_without_output)
{
    REQUIRE(artifact && artifact_size);
    char filename[4096];
    REQUIRE(path(filename, sizeof filename, "bad.artifact"));
    changed = malloc(artifact_size);
    REQUIRE(changed);
    memcpy(changed, artifact, artifact_size);
    changed[0] ^= 1;
    FILE *stream = fopen(filename, "wb");
    REQUIRE(stream);
    bool valid = fwrite(changed, 1, artifact_size, stream) == artifact_size;
    valid = fclose(stream) == 0 && valid;
    REQUIRE(valid);
    REQUIRE(product("bad.artifact", "bytecode", 1));
    REQUIRE(out.size == 0 && err.size > 0);
    return true;
}
TEST_CASE(product_rejects_reference_entry_before_decoding_pointer)
{
    /* A numeric-width reference is executable provenance, not a JSON integer.
     * Deliberately supply address 1: admission must reject before execution. */
    builder = behavior_function_builder_new();
    REQUIRE(builder);
    pin_value parameter = { .type = BF_POINTER_TYPE };
    behavior_function_builder_signature(builder, &parameter, 1, NULL, 0, NULL, 0);
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_ARGUMENT, 0, BEHAVIOR_REFERENCE));
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
    REQUIRE(body);
    plan = execution_module_plan_new();
    plan->id = 761001;
    plan->valid = true;
    apush(
        plan->functions,
        ((execution_module_function){ .id = 761002,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = body })
    );
    plan->function_count = 1;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    size_t blob_size, container_size;
    REQUIRE(execution_prepared_write(module, &tiny_blob, &blob_size, error, sizeof error));
    package_module row = { "reference", 761001, tiny_blob, blob_size };
    REQUIRE(
        compiler_artifact_write(&row, 1, 761001, &tiny_artifact, &container_size, error, sizeof error)
    );
    REQUIRE(container_size <= 65536);
    loaded = compiler_host_load(tiny_artifact, container_size, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(loaded);
    REQUIRE(!compiler_host_invoke(loaded, 761002, "[1]", 3, 100, &invocation));
    REQUIRE(!strcmp(invocation.diagnostic, "compiler entry requires an address or reference argument"));
    REQUIRE(!invocation.json && !invocation.length);
    REQUIRE(!compiler_host_invoke_bytes(loaded, 761002, "[1]", 3, 100, 256, &byte_result));
    REQUIRE(!strcmp(byte_result.diagnostic, "compiler entry requires an address or reference argument"));
    REQUIRE(!byte_result.bytes && !byte_result.length);
    REQUIRE(
        !byte_result.counts.bytecode_entries && !byte_result.counts.sljit_entries &&
        !byte_result.counts.wasm_entries && !byte_result.counts.inspected_sljit_entries &&
        !byte_result.counts.inspected_wasm_entries &&
        !byte_result.counts.inspection_bytecode_fallbacks && !byte_result.counts.overflow
    );
    REQUIRE(
        !invocation.counts.bytecode_entries &&
        !invocation.counts.sljit_entries && !invocation.counts.wasm_entries &&
        !invocation.counts.inspection_bytecode_fallbacks && !invocation.counts.overflow
    );
    return true;
}
static bool write_bytes(const char *name, const void *bytes, size_t length)
{
    char filename[4096];
    REQUIRE(path(filename, sizeof filename, name));
    FILE *stream = fopen(filename, "wb");
    REQUIRE(stream);
    bool ok = fwrite(bytes, 1, length, stream) == length;
    ok = fclose(stream) == 0 && ok;
    REQUIRE(ok);
    return true;
}
static bool read_binary_entries()
{
    const char *names[] = { "binary-echo.txt",
                            "binary-scalar.txt",
                            "binary-integers.txt",
                            "binary-loop.txt" };
    for (int i = 0; i < 4; i++) {
        size_t length;
        REQUIRE(read_bytes(names[i], sizeof binary_entries[i] - 1, &byte_input, &length));
        memcpy(binary_entries[i], byte_input, length + 1);
        free(byte_input);
        byte_input = NULL;
        char *end;
        binary_ids[i] = strtoull(binary_entries[i], &end, 16);
        REQUIRE(binary_ids[i] && !*end);
    }
    return true;
}
static bool load_binary(int backend)
{
    size_t length;
    REQUIRE(read_bytes("binary.artifact", BINARY_CAP, &binary_artifact, &length));
    char error[256];
    loaded = compiler_host_load(
        binary_artifact,
        length,
        backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    free(binary_artifact);
    binary_artifact = NULL;
    REQUIRE(loaded);
    return true;
}
static bool counts(const behavior_run_counts *value, int backend, bool executed)
{
    REQUIRE(
        !value->wasm_entries && !value->inspected_sljit_entries &&
        !value->inspected_wasm_entries &&
        !value->inspection_bytecode_fallbacks && !value->overflow
    );
    REQUIRE(value->bytecode_entries == (uint64_t)(executed && !backend));
    REQUIRE(value->sljit_entries == (uint64_t)(executed && backend));
    return true;
}
static bool byte_failure(int backend, bool executed)
{
    REQUIRE(!byte_result.bytes && !byte_result.length && byte_result.diagnostic[0]);
    REQUIRE(counts(&byte_result.counts, backend, executed));
    return true;
}
TEST_CASE(binary_bytes_survive_all_producer_owners)
{
    REQUIRE(read_binary_entries());
    for (int b = 0; b < 2; b++) {
        REQUIRE(load_binary(b));
        size_t input_size;
        REQUIRE(read_bytes("octets.json", OUTPUT_CAP, &byte_input, &input_size));
        REQUIRE(compiler_host_invoke_bytes(
            loaded,
            binary_ids[0],
            (const char *)byte_input,
            input_size,
            10000,
            256,
            &byte_result
        ));
        free(byte_input);
        byte_input = NULL;
        compiler_host_free(loaded);
        loaded = NULL;
        REQUIRE(
            byte_result.flow_status == FLOW_OK && byte_result.length == 256 &&
            byte_result.bytes
        );
        REQUIRE(counts(&byte_result.counts, b, true));
        for (int i = 0; i < 256; i++) {
            REQUIRE(byte_result.bytes[i] == (unsigned char)i);
        }
        free(byte_result.bytes);
        byte_result = (compiler_host_bytes){ 0 };
        REQUIRE(load_binary(b));
        REQUIRE(
            compiler_host_invoke_bytes(loaded, binary_ids[0], "[[]]", 4, 10000, 256, &byte_result)
        );
        compiler_host_free(loaded);
        loaded = NULL;
        REQUIRE(
            byte_result.flow_status == FLOW_OK && !byte_result.bytes &&
            !byte_result.length
        );
        REQUIRE(counts(&byte_result.counts, b, true));
    }
    return true;
}
TEST_CASE(binary_result_limit_is_atomic)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(load_binary(b));
        size_t input_size;
        REQUIRE(read_bytes("octets.json", OUTPUT_CAP, &byte_input, &input_size));
        REQUIRE(!compiler_host_invoke_bytes(
            loaded,
            binary_ids[0],
            (const char *)byte_input,
            input_size,
            10000,
            255,
            &byte_result
        ));
        REQUIRE(byte_result.flow_status == FLOW_OK && byte_failure(b, true));
        if (!b) {
            const size_t limits[] = { 0, COMPILER_HOST_MAX_ARTIFACT_BYTES + 1u };
            for (int i = 0; i < 2; i++) {
                REQUIRE(
                    !compiler_host_invoke_bytes(loaded, binary_ids[0], "[", 1, 10000, limits[i], &byte_result)
                );
                REQUIRE(byte_failure(0, false));
                REQUIRE(!strcmp(byte_result.diagnostic, "invalid compiler byte output limit"));
            }
        }
        free(byte_input);
        byte_input = NULL;
        compiler_host_free(loaded);
        loaded = NULL;
    }
    return true;
}
/* One native resize creates each byte array; no per-byte authored loop and
 * no large JSON input. Ordinary invocations retain their smaller bound. */
TEST_CASE(binary_artifact_bound_does_not_widen_ordinary_results)
{
    const size_t lengths[] = { COMPILER_HOST_MAX_OUTPUT_BYTES,
                               COMPILER_HOST_MAX_OUTPUT_BYTES + 1u,
                               COMPILER_HOST_MAX_ARTIFACT_BYTES,
                               COMPILER_HOST_MAX_ARTIFACT_BYTES + 1u };
    const size_t limits[] = { COMPILER_HOST_MAX_OUTPUT_BYTES,
                              COMPILER_HOST_MAX_ARTIFACT_BYTES };
    for (int b = 0; b < 2; b++) {
        size_t length;
        REQUIRE(read_bytes("sized.artifact", BINARY_CAP, &binary_artifact, &length));
        char error[256];
        loaded = compiler_host_load(
            binary_artifact,
            length,
            b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE,
            error,
            sizeof error
        );
        free(binary_artifact);
        binary_artifact = NULL;
        REQUIRE(loaded);
        for (int i = 0; i < 4; i++) {
            char input[32];
            int size = snprintf(input, sizeof input, "[%zu]", lengths[i]);
            for (int j = 0; j < 2; j++) {
                bool fits = lengths[i] <= limits[j];
                REQUIRE(
                    compiler_host_invoke_bytes(
                        loaded,
                        compiler_host_entry("project:binary_sized:1:Sized:callable"),
                        input,
                        (size_t)size,
                        10000,
                        limits[j],
                        &byte_result
                    ) == fits
                );
                REQUIRE(byte_result.flow_status == FLOW_OK);
                REQUIRE(counts(&byte_result.counts, b, true));
                if (fits) {
                    REQUIRE(byte_result.bytes && byte_result.length == lengths[i]);
                    REQUIRE(
                        byte_result.bytes[0] == 37 &&
                        byte_result.bytes[lengths[i] - 1] == 0
                    );
                } else {
                    REQUIRE(!byte_result.bytes && !byte_result.length);
                    REQUIRE(strstr(byte_result.diagnostic, "exceeds type, shape or storage limits"));
                }
                free(byte_result.bytes);
                byte_result = (compiler_host_bytes){ 0 };
            }
        }
        compiler_host_free(loaded);
        loaded = NULL;
    }
    return true;
}
TEST_CASE(binary_signature_and_input_reject_before_execution)
{
    REQUIRE(load_binary(0));
    for (int i = 1; i <= 2; i++) {
        REQUIRE(
            !compiler_host_invoke_bytes(loaded, binary_ids[i], "[", 1, 10000, 256, &byte_result)
        );
        REQUIRE(byte_failure(0, false));
        REQUIRE(
            !strcmp(byte_result.diagnostic, "compiler byte entry must return exactly one primitive u8[]")
        );
    }
    const char *inputs[] = { "[", "[]" };
    const char *messages[] = {
        "invalid or oversized compiler argument document",
        "compiler argument count mismatch"
    };
    for (int i = 0; i < 2; i++) {
        REQUIRE(
            !compiler_host_invoke_bytes(loaded, binary_ids[0], inputs[i], strlen(inputs[i]), 10000, 256, &byte_result)
        );
        REQUIRE(byte_failure(0, false));
        REQUIRE(!strcmp(byte_result.diagnostic, messages[i]));
    }
    return true;
}
TEST_CASE(binary_reference_result_rejects_before_input_decoding)
{
    /* Declaration only: zero pointer-width result, no operand slots, no pointer
     * execution. Prepared provenance must reject before malformed JSON parsing. */
    builder = behavior_function_builder_new();
    REQUIRE(builder);
    pin_value result = { .type = BF_POINTER_TYPE };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_RESULT, 0, BEHAVIOR_REFERENCE));
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
    REQUIRE(body);
    plan = execution_module_plan_new();
    plan->id = 761003;
    plan->valid = true;
    apush(
        plan->functions,
        ((execution_module_function){ .id = 761004,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = body })
    );
    plan->function_count = 1;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    size_t blob_size, container_size;
    REQUIRE(execution_prepared_write(module, &tiny_blob, &blob_size, error, sizeof error));
    package_module row = { "reference-result", 761003, tiny_blob, blob_size };
    REQUIRE(
        compiler_artifact_write(&row, 1, 761003, &tiny_artifact, &container_size, error, sizeof error)
    );
    REQUIRE(container_size && container_size <= 65536);
    loaded = compiler_host_load(tiny_artifact, container_size, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(loaded);
    REQUIRE(!compiler_host_invoke(loaded, 761004, "[", 1, 100, &invocation));
    REQUIRE(!invocation.json && !invocation.length);
    REQUIRE(!strcmp(invocation.diagnostic, "compiler entry returns an address or reference result"));
    REQUIRE(counts(&invocation.counts, 0, false));
    REQUIRE(!compiler_host_invoke_bytes(loaded, 761004, "[", 1, 100, 256, &byte_result));
    REQUIRE(byte_failure(0, false));
    REQUIRE(!strcmp(byte_result.diagnostic, "compiler entry returns an address or reference result"));
    return true;
}
TEST_CASE(binary_budget_failure_has_no_output)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(load_binary(b));
        REQUIRE(
            !compiler_host_invoke_bytes(loaded, binary_ids[3], "[true]", 6, 100, 256, &byte_result)
        );
        REQUIRE(byte_result.flow_status == FLOW_ERR_STEP_BUDGET);
        REQUIRE(byte_failure(b, true));
        compiler_host_free(loaded);
        loaded = NULL;
    }
    return true;
}
static bool binary_output(const char *option, const char *input, const char *destination, int backend, int status)
{
    const char *argv[] = { host_path,
                           "--artifact",
                           "binary.artifact",
                           "--entry",
                           binary_entries[0],
                           "--input",
                           input,
                           "--backend",
                           backend ? "sljit" : "bytecode",
                           "--budget",
                           "10000",
                           "--timeout-ms",
                           "10000",
                           option,
                           destination,
                           NULL };
    REQUIRE(capture(argv, status));
    REQUIRE(!out.size);
    return true;
}
static bool binary_product(const char *input, const char *destination, int backend, int status)
{
    return binary_output("--output-artifact", input, destination, backend, status);
}
static bool binary_counters(int backend)
{
    uint64_t bytecode, sljit, wasm, fallback;
    unsigned overflow;
    int used = 0;
    REQUIRE(
        sscanf(
            errors,
            "compiler-host bytecode=%" SCNu64 " sljit=%" SCNu64 " wasm=%" SCNu64
            " fallback=%" SCNu64 " overflow=%u\n%n",
            &bytecode,
            &sljit,
            &wasm,
            &fallback,
            &overflow,
            &used
        ) == 5
    );
    REQUIRE((size_t)used == err.size && !wasm && !fallback && !overflow);
    REQUIRE(bytecode == (uint64_t)!backend && sljit == (uint64_t)backend);
    return true;
}
static bool file_equals(const char *name, const unsigned char *bytes, size_t length)
{
    unsigned char *actual = NULL;
    size_t size;
    REQUIRE(read_bytes(name, BINARY_CAP, &actual, &size));
    bool same = size == length && !memcmp(actual, bytes, length);
    free(actual);
    REQUIRE(same);
    return true;
}
static bool no_staging(const char *name)
{
    DIR *dir = opendir(directory);
    REQUIRE(dir);
    bool clean = true;
    size_t prefix = strlen(name);
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(dir))) {
        if (!strncmp(entry->d_name, name, prefix) && entry->d_name[prefix] == '.') {
            clean = false;
        }
    }
    clean = !errno && clean;
    clean = closedir(dir) == 0 && clean;
    REQUIRE(clean);
    return true;
}
static const unsigned char sentinel[] = { 'o', 'l', 'd', 0, 255 };
TEST_CASE(binary_publication_reloads_both_backends)
{
    size_t length;
    REQUIRE(read_bytes("binary.artifact", BINARY_CAP, &binary_artifact, &length));
    const char *name = "published bytes.artifact";
    for (int b = 0; b < 2; b++) {
        REQUIRE(write_bytes(name, sentinel, sizeof sentinel));
        REQUIRE(binary_product("binary-input.json", name, b, 0));
        REQUIRE(
            binary_counters(b) && file_equals(name, binary_artifact, length) &&
            no_staging(name)
        );
        size_t published_size;
        REQUIRE(read_bytes(name, BINARY_CAP, &changed, &published_size));
        char error[256];
        loaded = compiler_host_load(
            changed,
            published_size,
            b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE,
            error,
            sizeof error
        );
        free(changed);
        changed = NULL;
        REQUIRE(loaded);
        REQUIRE(
            compiler_host_invoke_bytes(loaded, binary_ids[0], "[[0,255]]", 9, 10000, 2, &byte_result)
        );
        compiler_host_free(loaded);
        loaded = NULL;
        REQUIRE(
            byte_result.length == 2 && byte_result.bytes &&
            byte_result.bytes[0] == 0 && byte_result.bytes[1] == 255
        );
        REQUIRE(byte_result.flow_status == FLOW_OK && counts(&byte_result.counts, b, true));
        free(byte_result.bytes);
        byte_result = (compiler_host_bytes){ 0 };
    }
    return true;
}
/* Fixture-only input encoding; production output never projects through JSON. */
static bool write_byte_input(const char *name, const unsigned char *bytes, size_t length)
{
    REQUIRE(length <= BINARY_CAP);
    byte_input = malloc(4 * length + 5);
    REQUIRE(byte_input);
    size_t used = 0;
    byte_input[used++] = '[';
    byte_input[used++] = '[';
    for (size_t i = 0; i < length; i++) {
        int count = snprintf(
            (char *)byte_input + used,
            4 * length + 5 - used,
            "%s%u",
            i ? "," : "",
            bytes[i]
        );
        REQUIRE(count > 0 && (size_t)count < 4 * length + 5 - used);
        used += (size_t)count;
    }
    byte_input[used++] = ']';
    byte_input[used++] = ']';
    REQUIRE(used <= OUTPUT_CAP && write_bytes(name, byte_input, used));
    free(byte_input);
    byte_input = NULL;
    return true;
}
TEST_CASE(binary_invalid_artifact_preserves_destination)
{
    size_t length;
    REQUIRE(read_bytes("binary.artifact", BINARY_CAP, &binary_artifact, &length));
    /* Outer CHIBICLO row: 32-byte header, 24-byte row, then name and payload.
     * The prepared header is LEB128 magic, version and pointer width. Preserve
     * the container, magic and version, changing only that compatibility field. */
    REQUIRE(length > 80 && little_endian(binary_artifact + 24) == 1);
    uint64_t name_length = little_endian(binary_artifact + 40);
    uint64_t blob_length = little_endian(binary_artifact + 48);
    REQUIRE(
        name_length <= length - 56 && blob_length >= 32 &&
        blob_length == length - 56 - name_length
    );
    size_t width = 56 + (size_t)name_length;
    for (int field = 0; field < 2; field++) {
        while (width < length && binary_artifact[width] & 0x80) {
            width++;
        }
        width++;
    }
    REQUIRE(width < length && binary_artifact[width] == sizeof(void *));
    binary_artifact[width] ^= 1;
    REQUIRE(write_byte_input("wrong-target.json", binary_artifact, length));
    const char *inputs[] = { "octets.json", "empty.json", "wrong-target.json" };
    const char *name = "preserved bytes.artifact";
    for (int i = 0; i < 3; i++) {
        REQUIRE(write_bytes(name, sentinel, sizeof sentinel));
        REQUIRE(binary_product(inputs[i], name, 0, 1));
        REQUIRE(err.size && file_equals(name, sentinel, sizeof sentinel) && no_staging(name));
        if (i == 2) {
            REQUIRE(strstr(errors, "prepared target ABI differs"));
        }
    }
    return true;
}
/* --output-bytes writes whatever bytes the entry returns, without loading them
 * as a compiler, and refuses an empty result, keeping the destination. */
TEST_CASE(binary_raw_output_writes_bytes_that_are_no_compiler)
{
    const char *name = "raw bytes";
    REQUIRE(write_bytes(name, sentinel, sizeof sentinel));
    REQUIRE(binary_output("--output-bytes", "empty.json", name, 0, 1));
    REQUIRE(strstr(errors, "the compiler entry returned no bytes"));
    REQUIRE(file_equals(name, sentinel, sizeof sentinel) && no_staging(name));
    REQUIRE(binary_output("--output-bytes", "octets.json", name, 0, 0));
    REQUIRE(!file_equals(name, sentinel, sizeof sentinel) && no_staging(name));
    REQUIRE(binary_product("octets.json", name, 0, 1));
    return true;
}
TEST_CASE(binary_rename_failure_preserves_directory)
{
    const char *name = "output directory";
    char filename[4096];
    REQUIRE(path(filename, sizeof filename, name));
    REQUIRE(mkdir(filename, 0700) == 0);
    REQUIRE(write_bytes("output directory/sentinel", sentinel, sizeof sentinel));
    REQUIRE(binary_product("binary-input.json", name, 0, 1));
    struct stat info;
    REQUIRE(stat(filename, &info) == 0 && S_ISDIR(info.st_mode));
    REQUIRE(
        file_equals("output directory/sentinel", sentinel, sizeof sentinel) &&
        no_staging(name)
    );
    REQUIRE(strstr(errors, "compiler output could not be published"));
    return true;
}
TEST_CASE(binary_usage_preserves_destination)
{
    const char *name = "usage output.artifact";
    REQUIRE(write_bytes(name, sentinel, sizeof sentinel));
    const char *missing_timeout[] = { host_path,
                                      "--artifact",
                                      "binary.artifact",
                                      "--entry",
                                      binary_entries[0],
                                      "--input",
                                      "binary-input.json",
                                      "--backend",
                                      "bytecode",
                                      "--budget",
                                      "10000",
                                      "--output-artifact",
                                      name,
                                      NULL };
    REQUIRE(capture(missing_timeout, 1));
    REQUIRE(!out.size && strstr(errors, "usage:") && file_equals(name, sentinel, sizeof sentinel));
    REQUIRE(no_staging(name));
    const char *duplicate[] = { host_path,
                                "--artifact",
                                "binary.artifact",
                                "--entry",
                                binary_entries[0],
                                "--input",
                                "binary-input.json",
                                "--backend",
                                "bytecode",
                                "--budget",
                                "10000",
                                "--timeout-ms",
                                "10000",
                                "--output-artifact",
                                name,
                                "--output-artifact",
                                name,
                                NULL };
    const char *batch[] = { host_path,
                            "--artifact",
                            "binary.artifact",
                            "--entry",
                            binary_entries[0],
                            "--input",
                            "binary-input.json",
                            "--backend",
                            "bytecode",
                            "--budget",
                            "10000",
                            "--timeout-ms",
                            "10000",
                            "--batch",
                            "--output-artifact",
                            name,
                            NULL };
    REQUIRE(capture(duplicate, 1));
    REQUIRE(!out.size && strstr(errors, "usage:") && file_equals(name, sentinel, sizeof sentinel));
    REQUIRE(capture(batch, 1));
    REQUIRE(!out.size && strstr(errors, "usage:") && file_equals(name, sentinel, sizeof sentinel));
    REQUIRE(no_staging(name));
    return true;
}
void test_tool_compiler_artifact(const char *producer, const char *host)
{
    producer_path = producer;
    host_path = host;
    pu_setup(NULL, cleanup_case);
    RUN_TEST_CASE(actual_compiler_artifact_runs_both_product_backends);
    RUN_TEST_CASE(artifact_truncated_container_rejected_before_decode);
    RUN_TEST_CASE(artifact_duplicate_module_id_rejected_before_decode);
    RUN_TEST_CASE(artifact_trailing_data_rejected_before_decode);
    RUN_TEST_CASE(product_cli_rejects_malformed_artifact_without_output);
    RUN_TEST_CASE(product_rejects_reference_entry_before_decoding_pointer);
    RUN_TEST_CASE(binary_bytes_survive_all_producer_owners);
    RUN_TEST_CASE(binary_result_limit_is_atomic);
    RUN_TEST_CASE(binary_artifact_bound_does_not_widen_ordinary_results);
    RUN_TEST_CASE(binary_signature_and_input_reject_before_execution);
    RUN_TEST_CASE(binary_reference_result_rejects_before_input_decoding);
    RUN_TEST_CASE(binary_budget_failure_has_no_output);
    RUN_TEST_CASE(binary_publication_reloads_both_backends);
    RUN_TEST_CASE(binary_invalid_artifact_preserves_destination);
    RUN_TEST_CASE(binary_raw_output_writes_bytes_that_are_no_compiler);
    RUN_TEST_CASE(binary_rename_failure_preserves_directory);
    RUN_TEST_CASE(binary_usage_preserves_destination);
    pu_clear_setup();
    free(artifact);
    artifact = NULL;
    free(golden);
    golden = NULL;
    if (directory_created) {
        tool_remove_tree(directory);
        directory[0] = 0;
        directory_created = false;
    }
}
#else
void test_tool_compiler_artifact(const char *producer, const char *host)
{
    (void)producer;
    (void)host;
}
#endif
