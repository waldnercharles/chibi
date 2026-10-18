#include "compiler_artifact.h"

#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "execution/prepared.h"
#include "host/process.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>

/* Four supervised children: one source producer, two five-request product
 * batches, one malformed-container CLI rejection. Three malformed reads reject
 * before prepared-module decoding. No child receives implementation source. */
enum
{
    ARTIFACT_CAP = 16 * 1024 * 1024,
    OUTPUT_CAP = 256 * 1024,
    ERROR_CAP = 4096
};
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
extern char **environ;

static void cleanup_case()
{
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
    const char *inherited = getenv("CHIBI_TEST_DEADLINE");
    if (inherited) {
        char *end;
        double shared = strtod(inherited, &end);
        REQUIRE(end != inherited && !*end && isfinite(shared) && shared > 0);
        if (shared < deadline) {
            deadline = shared;
        }
    }
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
    pin_value parameter = { .type = PIN_U64 };
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
    REQUIRE(
        !invocation.counts.bytecode_entries &&
        !invocation.counts.sljit_entries && !invocation.counts.wasm_entries &&
        !invocation.counts.inspection_bytecode_fallbacks && !invocation.counts.overflow
    );
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
