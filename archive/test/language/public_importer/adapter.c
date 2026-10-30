#include "model.inc"
#define IMPORTER_ID UINT64_C(3)
#define DEPENDENCY_ID UINT64_C(0x8fb3be3da78425e0)
#define WRITER_ID UINT64_C(0x61058c5dc9ed077d)
static const char *IMPORTER_NAME = "project:capacity_importer";
static uint64_t fingerprint(const unsigned char *data, size_t length)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ data[i]) * UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}
static unsigned char *read_file(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END)) {
        return NULL;
    }
    long size = ftell(f);
    if (size <= 0 || size > COMPILER_HOST_MAX_BYTES || fseek(f, 0, SEEK_SET)) {
        return NULL;
    }
    unsigned char *bytes = malloc((size_t)size);
    if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) {
        return NULL;
    }
    fclose(f);
    *length = (size_t)size;
    return bytes;
}
static bool call(
    const behavior_function *f,
    const pin_value *argument,
    pin_value *result,
    arena *detached,
    const char *label
)
{
    REQUIRE(f && f->argument_count == 1 && f->result_count == 1);
    behavior_function_activation activation = { 0 };
    arena producer = { 0 };
    if (behavior_function_resumable(f)) {
        behavior_function_activation_init(f, &activation);
    }
    behavior_function_context ctx = { .activation = behavior_function_resumable(f)
                                                      ? &activation
                                                      : NULL,
                                      .arguments = argument,
                                      .argument_count = 1,
                                      .results = result,
                                      .budget = BUDGET };
    behavior_function_bind_storage(&ctx, &producer);
    REQUIRE(behavior_function_bind(f, &ctx, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(f, &ctx, false);
    behavior_run_scope_end(&runs);
    printf(
        "call=%s status=%d returned=%d bytecode_entries=%" PRIu64
        " sljit_entries=%" PRIu64 " wasm_entries=%" PRIu64
        " failure=%d message=%s\n",
        label,
        status,
        ctx.returned,
        runs.counts.bytecode_entries,
        runs.counts.sljit_entries,
        runs.counts.wasm_entries,
        ctx.failure,
        ctx.error_message ? ctx.error_message : ""
    );
    bool ok = status == FLOW_OK && ctx.returned &&
              runs.counts.bytecode_entries > 0 && !runs.counts.sljit_entries &&
              !runs.counts.wasm_entries && !runs.counts.overflow &&
              !runs.counts.inspection_bytecode_fallbacks;
    pin_value copy = { 0 };
    if (ok) {
        ok = !pin_value_snapshot_to_arena(detached, &copy, result);
    }
    behavior_function_release_host(&ctx);
    behavior_function_activation_free(&activation);
    arena_free(&producer);
    REQUIRE(ok);
    *result = copy;
    return true;
}
static behavior_function_builder *builder;
static int slot(pin_type type)
{
    return behavior_function_builder_slot(builder, type);
}
static int emit(behavior_function_instruction op)
{
    return behavior_function_builder_emit(builder, op);
}
static behavior_function *sparse_definition(int slots, int additions, char *error, size_t size)
{
    pin_value number = { .type = PIN_I32 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &number, 1, &number, 1, NULL, 0);
    int input = slot(PIN_I32), sum = slot(PIN_I32);
    for (int i = 2; i < slots; i++) {
        slot(PIN_I32);
    }
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_I32, .output = input, .value = number });
    for (int i = 0; i < additions; i++) {
        emit((
            behavior_function_instruction
        ){ .op = BF_ADD, .type = PIN_I32, .output = sum, .left = input, .right = input });
    }
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = sum });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    behavior_function *sparse = behavior_function_builder_finish(builder, error, size);
    behavior_function_builder_free(builder);
    builder = NULL;
    return sparse;
}
static execution_module *tiny_module()
{
    char error[256] = "";
    behavior_function *f = sparse_definition(2, 1, error, sizeof error);
    if (!f) {
        return NULL;
    }
    execution_module_plan *p = execution_module_plan_new();
    p->id = IMPORTER_ID;
    p->valid = true;
    apush(
        p->functions,
        ((execution_module_function){ .id = 1, .kind = EXECUTION_FUNCTION_AUTHORED, .body = f })
    );
    p->function_count = 1;
    return execution_module_finish(&p, BEHAVIOR_BYTECODE, error, sizeof error);
}
static bool write_tiny(
    const behavior_function *write,
    const pin_value *value,
    unsigned char **bytes,
    size_t *length,
    const char *label
)
{
    arena detached = { 0 };
    pin_value result = { .type = PIN_U8, .container = PIN_CONTAINER_ARRAY };
    REQUIRE(call(write, value, &result, &detached, label));
    const pin_array *array = result.ptr;
    REQUIRE(
        result.type == PIN_U8 && result.container == PIN_CONTAINER_ARRAY &&
        array && array->count > 0 && array->count < 4096
    );
    *length = (size_t)array->count;
    *bytes = malloc(*length);
    REQUIRE(*bytes);
    for (int i = 0; i < array->count; i++) {
        (*bytes)[i] = pin_array_get(array, i).u8;
    }
    arena_free(&detached);
    return true;
}
static bool generate(
    package_collection *collection,
    const package_module *records,
    int count,
    const char *output
)
{
    const package_module *dependency = NULL;
    for (int i = 0; i < count; i++) {
        if (records[i].id == WRITER_ID) {
            writer_module = package_collection_module(collection, i);
        }
        if (records[i].id == DEPENDENCY_ID) {
            dependency = &records[i];
        }
    }
    REQUIRE(writer_module && dependency && execution_module_id(writer_module) == WRITER_ID);
    uint64_t write_id = script_catalog_id("package-symbol", "project:prepared_writer:1:Write:callable");
    REQUIRE(write_id == UINT64_C(0xa6ae63194ee648f4));
    const behavior_function *write = NULL;
    for (int i = 0; i < execution_module_function_count(writer_module); i++) {
        execution_module_function_info f = execution_module_function_at(writer_module, i);
        if (f.id == write_id) {
            write = f.body;
        }
    }
    REQUIRE(
        write && loaded_record("Import") &&
        loaded_record("Import")->id == UINT64_C(0x244761419acaabc3)
    );
    REQUIRE(tag(0, "Module", "Tables") == UINT64_C(0x90d2aa5548c71174));
    char error[256] = "", digest[65];
    sha256_hex(dependency->data, dependency->length, digest);
    REQUIRE(!strcmp(digest, "c1dd7ff228cc63904523962fefadce4e0deaab8d2e3801eb8231c0bca48723ce"));
    uint64_t raw_hash = fingerprint(dependency->data, dependency->length);
    printf(
        "dependency id=0x%016" PRIx64
        " bytes=%zu sha256=%s raw_module_fnv=0x%016" PRIx64 "\n",
        dependency->id,
        dependency->length,
        digest,
        raw_hash
    );
    execution_module *tiny = tiny_module();
    REQUIRE(tiny);
    unsigned char *native = NULL, *authored = NULL;
    size_t n = 0, a = 0;
    REQUIRE(execution_prepared_write(tiny, &native, &n, error, sizeof error));
    REQUIRE(header_abi(native) == execution_prepared_abi());
    model m = { 0 };
    pin_value dto = { 0 };
    REQUIRE(marshal(&m, tiny, header_abi(native), &dto));
    REQUIRE(write_tiny(write, &dto, &authored, &a, "Writer.Write-no-import"));
    REQUIRE(a == n && !memcmp(authored, native, n));
    printf("tiny_no_import_native_equivalence=1 bytes=%zu native_target_abi=0x%016" PRIx64 "\n", n, header_abi(native));
    free(authored);
    free(native);
    execution_module_free(tiny);
    pin_value imported = record(&m, "Import");
    put_int(&imported, "id", wide(&m, dependency->id));
    put_int(&imported, "fingerprint", wide(&m, raw_hash));
    apush(m.imports, imported);
    const pin_union *tables = dto.ptr;
    REQUIRE(tables && tables->count == 30);
    pin_value payload[30];
    memcpy(payload, tables->values, sizeof payload);
    payload[4] = rows(&m, NULL, PIN_U64, m.wide);
    payload[11] = rows(&m, "Import", PIN_STRUCT, m.imports);
    pin_value with_import = { 0 };
    REQUIRE(!adapter_union(&input, tables->tag, payload, 30, &with_import));
    with_import.type_name = "Module";
    REQUIRE(write_tiny(write, &with_import, &authored, &a, "Writer.Write-importer"));
    sha256_hex(authored, a, digest);
    FILE *file = fopen(output, "wb");
    REQUIRE(file);
    REQUIRE(fwrite(authored, 1, a, file) == a && !fclose(file));
    printf("importer_emitted bytes=%zu sha256=%s writer_entry=0x%016" PRIx64 "\n", a, digest, write_id);
    free(authored);
    model_free(&m);
    arena_free(&input);
    writer_module = NULL;
    return true;
}
static bool check(
    package_module *original,
    int count,
    const execution_prepared_linker *linker,
    const char *path,
    bool mutant
)
{
    size_t length = 0;
    unsigned char *bytes = read_file(path, &length);
    REQUIRE(bytes);
    package_module *all = malloc((size_t)(count + 1) * sizeof *all);
    REQUIRE(all);
    memcpy(all, original, (size_t)count * sizeof *all);
    all[count] = (package_module){ .name = IMPORTER_NAME,
                                   .id = IMPORTER_ID,
                                   .data = bytes,
                                   .length = length };
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    char error[512] = "", digest[65];
    sha256_hex(bytes, length, digest);
    printf("importer_input bytes=%zu sha256=%s\n", length, digest);
    printf(
        "combined_begin records=4000000 decoded_bytes=%zu aggregate_bytes=%zu "
        "layout_work=%zu\n",
        linker->decoded_bytes,
        remaining,
        linker->layout_work
    );
    package_collection *combined = package_read_budget(
        all,
        count + 1,
        linker,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    printf(
        "combined_end success=%d remaining=%zu charged=%zu error=%s\n",
        combined != NULL,
        remaining,
        (size_t)COMPILER_HOST_MAX_CLOSURE_BYTES - remaining,
        error
    );
    bool ok;
    if (mutant) {
        ok = !combined && remaining == COMPILER_HOST_MAX_CLOSURE_BYTES &&
             strstr(error, "Prepared module project:capacity_importer:") &&
             strstr(error, "record count limit");
    } else {
        REQUIRE(combined && package_collection_count(combined) == count + 1);
        const execution_module *importer = package_collection_module(combined, count);
        REQUIRE(
            execution_module_id(importer) == IMPORTER_ID &&
            execution_module_import_count(importer) == 1
        );
        REQUIRE(execution_module_id(execution_module_import_at(importer, 0)) == DEPENDENCY_ID);
        REQUIRE(execution_module_function_count(importer) == 1);
        execution_module_function_info f = execution_module_function_at(importer, 0);
        REQUIRE(f.id == 1 && f.body && !acount(f.body->calls) && !acount(f.body->host_calls));
        arena detached = { 0 };
        pin_value argument = { .type = PIN_I32, .i = 7 }, result = { .type = PIN_I32 };
        REQUIRE(call(f.body, &argument, &result, &detached, "importer-owned-tiny-entry"));
        ok = result.type == PIN_I32 && result.i == 14;
        printf(
            "canonical_dependency_fingerprint_verified=1 tiny_input=7 "
            "tiny_output=%d oracle_pass=%d\n",
            result.i,
            ok
        );
        arena_free(&detached);
    }
    if (combined) {
        package_collection_free(combined);
    }
    free(all);
    free(bytes);
    return ok;
}
static bool run(int argc, char **argv)
{
    REQUIRE(argc == 4);
    bool emitting = !strcmp(argv[1], "--emit"),
         mutant = !strcmp(argv[1], "--check-mutant");
    REQUIRE(emitting || mutant || !strcmp(argv[1], "--check"));
    size_t length = 0;
    unsigned char *bytes = read_file(argv[2], &length);
    REQUIRE(bytes);
    char digest[65], error[512] = "";
    sha256_hex(bytes, length, digest);
    REQUIRE(
        length == 15499688 &&
        !strcmp(digest, "3c20a8b4271f60b6b54f9f9dcad8d39ee39510e5f631d84f6daeb7469123b329")
    );
    int count = 0;
    uint64_t root = 0;
    package_module *records = compiler_artifact_rows(bytes, length, &count, &root, error, sizeof error);
    REQUIRE(records && count == 12 && root == DEPENDENCY_ID);
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    native_capabilities *
        native = native_capabilities_from_definition_packs(NULL, 0, packs, 4, error, sizeof error);
    REQUIRE(native);
    native_prepared_context context = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    linker.decoded_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES;
    linker.decoded_records = 4000000;
    linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    printf("original_closure_begin sha256=%s records=4000000\n", digest);
    package_collection *collection = package_read_budget(
        records,
        count,
        &linker,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    printf("original_closure_end success=%d remaining=%zu error=%s\n", collection != NULL, remaining, error);
    REQUIRE(collection); // Failure here is explicitly not importer-path evidence.
    bool ok = true;
    if (emitting) {
        ok = generate(collection, records, count, argv[3]);
    }
    package_collection_free(collection);
    if (!emitting) {
        ok = check(records, count, &linker, argv[3], mutant);
    }
    compiler_artifact_rows_free(records, count);
    native_capabilities_free(native);
    free(bytes);
    return ok;
}
static int adapter_argc;
static char **adapter_argv;
TEST_CASE(public_large_dependency_importer)
{
    return run(adapter_argc, adapter_argv);
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    adapter_argc = argc;
    adapter_argv = argv;
    RUN_TEST_CASE(public_large_dependency_importer);
    pu_print_stats();
    bool ok = !pu_test_failed();
    printf("case_result=%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
