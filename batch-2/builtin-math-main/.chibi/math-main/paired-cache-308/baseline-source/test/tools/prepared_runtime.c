#include "execution/function.h"
#include "execution/inspection.h"
#include "execution/log.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "runtime/behaviors/package.h"
#include "support/array.h"
#include "test/prepared_fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/text.h"
#include "types/value_wire.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MISSING_HOST = 1,
    HOST_ABI,
    MISSING_TYPE,
    TYPE_LAYOUT,
    TYPE_STORAGE,
    MISSING_IMPORT,
    WRONG_IMPORT,
    EMPTY_SERVICE
};
static const char *directory;
static unsigned char *bytes, *library_bytes, *wrong_bytes, *mutation;
static size_t length, library_length, wrong_length;
static uint64_t host_abi, state_id;
static int mismatch, log_count;
static bool log_text;
static bool native_header_types;
static execution_log_service logger;
static execution_module *module, *library, *wrong_library;
static execution_program *program;
static execution_inspector *debugger;
static source_registry *registry;
static ecs_world *world;
static struct
{
    unsigned char *data;
    size_t length;
} closure_bytes[11];
static execution_module *closure_leaves[4], *closure_middles[4];
static bool closure_mode;
static int selected_leaf, event_checks;
static uint64_t declared_event_ids[3], declared_event_abis[3];

static void cleanup()
{
    if (registry) {
        source_registry_free(registry);
    }
    registry = NULL;
    execution_program_free(program);
    execution_inspector_free(debugger);
    debugger = NULL;
    execution_module_free(module);
    execution_module_free(library);
    execution_module_free(wrong_library);
    ecs_world_free(world);
    free(bytes);
    free(library_bytes);
    free(wrong_bytes);
    free(mutation);
    module = library = wrong_library = NULL;
    program = NULL;
    world = NULL;
    bytes = library_bytes = wrong_bytes = mutation = NULL;
    for (int i = 0; i < 4; i++) {
        execution_module_free(closure_middles[i]);
        execution_module_free(closure_leaves[i]);
        closure_middles[i] = closure_leaves[i] = NULL;
    }
    for (int i = 0; i < 11; i++) {
        free(closure_bytes[i].data);
        closure_bytes[i].data = NULL;
        closure_bytes[i].length = 0;
    }
    closure_mode = false;
    selected_leaf = event_checks = 0;
    mismatch = log_count = 0;
    log_text = true;
    if (native_header_types) {
        type_catalog_set_project(NULL);
        native_header_types = false;
    }
}
static bool read_file(const char *name, unsigned char **out, size_t *size)
{
    char path[PREPARED_TEST_MAX_PATH];
    int n = snprintf(path, sizeof path, "%s/%s", directory, name);
    REQUIRE(n > 0 && n < (int)sizeof path);
    FILE *file = fopen(path, "rb");
    REQUIRE(file);
    bool valid = !fseek(file, 0, SEEK_END);
    long end = valid ? ftell(file) : -1;
    valid = end > 0 && end <= PREPARED_TEST_MAX_BYTES && !fseek(file, 0, SEEK_SET);
    if (!valid) {
        fclose(file);
        return false;
    }
    *size = (size_t)end;
    *out = malloc(*size + 1);
    if (!*out) {
        fclose(file);
        return false;
    }
    valid = fread(*out, 1, *size, file) == *size && fgetc(file) == EOF &&
            !ferror(file);
    return fclose(file) == 0 && valid;
}
static bool inputs()
{
    REQUIRE(read_file("main.prepared", &bytes, &length));
    REQUIRE(read_file("library.prepared", &library_bytes, &library_length));
    REQUIRE(read_file("wrong-library.prepared", &wrong_bytes, &wrong_length));
    unsigned char *metadata = NULL;
    size_t size = 0;
    bool read = read_file("host-abi.txt", &metadata, &size);
    if (!read || size > 96) {
        free(metadata);
        return false;
    }
    metadata[size] = 0;
    unsigned long long abi = 0, identity = 0;
    char tail;
    int count = sscanf((char *)metadata, "%llu %llu %c", &abi, &identity, &tail);
    free(metadata);
    REQUIRE(count == 2 && abi && identity);
    host_abi = abi;
    state_id = identity;
    REQUIRE(length > 32);
    return true;
}
static bool resolve_type(void *, uint64_t id, execution_prepared_type *out)
{
    if (mismatch == MISSING_TYPE) {
        return false;
    }
    if (id == 700) {
        *out = (execution_prepared_type){ .name = "ecs.entity",
                                          .storage = PIN_HANDLE,
                                          .size = sizeof(uint64_t) };
        return true;
    }
    if (id != 101) {
        return false;
    }
    *out = (execution_prepared_type){ .name = prepared_counter_type.name,
                                      .type = &prepared_counter_type,
                                      .storage = PIN_STRUCT,
                                      .size = prepared_counter_type.size,
                                      .layout = type_layout_hash(&prepared_counter_type) };
    if (mismatch == TYPE_LAYOUT) {
        out->layout ^= 1;
    }
    if (mismatch == TYPE_STORAGE) {
        out->storage = PIN_F64;
    }
    return true;
}
static bool resolve_host(void *, uint64_t id, execution_prepared_host *out)
{
    if (id != 13 || mismatch == MISSING_HOST) {
        return false;
    }
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (execution_prepared_host){ .call = &EXECUTION_PrintString,
                                      .access = &access,
                                      .abi = mismatch == HOST_ABI ? host_abi ^ 1
                                                                  : host_abi };
    if (mismatch == EMPTY_SERVICE) {
        static node_host_call invalid;
        invalid = EXECUTION_PrintString;
        invalid.service = "";
        out->call = &invalid;
    }
    return true;
}
static execution_module *resolve_import(void *, uint64_t id, uint64_t)
{
    if (closure_mode) {
        for (int i = 0; i < 4; i++) {
            if (closure_middles[i] && execution_module_id(closure_middles[i]) == id) {
                execution_module_retain(closure_middles[i]);
                return closure_middles[i];
            }
        }
        execution_module *leaf = closure_leaves[selected_leaf];
        if (leaf && execution_module_id(leaf) == id) {
            execution_module_retain(leaf);
            return leaf;
        }
        return NULL;
    }
    execution_module *selected = mismatch == WRONG_IMPORT ? wrong_library : library;
    if (mismatch == MISSING_IMPORT || !selected || execution_module_id(selected) != id) {
        return NULL;
    }
    execution_module_retain(selected);
    return selected;
}
static execution_prepared_linker linker()
{
    return (execution_prepared_linker){ .type = resolve_type,
                                        .host = resolve_host,
                                        .import = resolve_import };
}
static bool dependencies(behavior_backend backend)
{
    char error[192];
    execution_prepared_linker link = linker();
    library = execution_prepared_read(library_bytes, library_length, &link, backend, error, sizeof error);
    if (!library) {
        fprintf(stderr, "prepared library: %s\n", error);
    }
    REQUIRE(library);
    wrong_library = execution_prepared_read(wrong_bytes, wrong_length, &link, backend, error, sizeof error);
    REQUIRE(wrong_library);
    return true;
}
static void log_message(void *, const execution_log_message *message)
{
    log_count++;
    log_text &= !strcmp(message->text, "persistent");
}
static bool count_is(execution_instance *instance, ecs_entity entity, int expected)
{
    const pin_value *value = execution_instance_state(instance, state_id);
    const prepared_counter *
        component = ecs_component_data(world, entity, prepared_counter_component.id);
    return value && value->type == PIN_I32 && value->i == expected &&
           component && component->value == expected;
}
typedef struct
{
    uint64_t entity, before, resumed, same, text, count;
} operation_fixture;
static bool operation_type(void *user, uint64_t id, execution_prepared_type *out)
{
    const operation_fixture *fixture = user;
    if (id != fixture->entity) {
        return false;
    }
    *out = (execution_prepared_type){ .name = "ecs.entity",
                                      .storage = PIN_HANDLE,
                                      .size = sizeof(uint64_t) };
    return true;
}
static bool operation_states(execution_instance *instance, const operation_fixture *fixture, int phase)
{
    const pin_value *before = execution_instance_state(instance, fixture->before);
    const pin_value *resumed = execution_instance_state(instance, fixture->resumed);
    const pin_value *same = execution_instance_state(instance, fixture->same);
    const pin_value *text = execution_instance_state(instance, fixture->text);
    const pin_value *count = execution_instance_state(instance, fixture->count);
    REQUIRE(before && before->type == PIN_HANDLE && before->u64 == 7);
    REQUIRE(resumed && resumed->type == PIN_HANDLE && resumed->u64 == (phase ? 7 : 0));
    REQUIRE(same && same->type == PIN_BOOL && same->b == (phase != 0));
    REQUIRE(text && text->type == PIN_STR);
    pin_string value = pin_text(*text);
    REQUIRE(value.length == 3 && !memcmp(value.bytes, phase ? "old" : "new", 3));
    REQUIRE(count && count->type == PIN_I32 && count->i == (phase == 2 ? 1 : 0));
    return true;
}
static bool operation_backend_entries(behavior_run_counts counts, behavior_backend backend, const char *phase)
{
    printf(
        "prepared-operations phase=%s backend=%s bytecode=%" PRIu64
        " sljit=%" PRIu64 " wasm=%" PRIu64 " inspected_sljit=%" PRIu64
        " inspected_wasm=%" PRIu64 " inspection_fallback=%" PRIu64
        " size_fallback=%" PRIu64 " overflow=%d\n",
        phase,
        backend == BEHAVIOR_BYTECODE ? "bytecode"
        : backend == BEHAVIOR_SLJIT  ? "sljit"
                                     : "wasm",
        counts.bytecode_entries,
        counts.sljit_entries,
        counts.wasm_entries,
        counts.inspected_sljit_entries,
        counts.inspected_wasm_entries,
        counts.inspection_bytecode_fallbacks,
        counts.size_bytecode_fallbacks,
        counts.overflow
    );
    REQUIRE(!counts.overflow);
    REQUIRE(!counts.inspection_bytecode_fallbacks && !counts.size_bytecode_fallbacks);
    REQUIRE(!counts.inspected_sljit_entries && !counts.inspected_wasm_entries);
    REQUIRE(
        backend == BEHAVIOR_BYTECODE ? counts.bytecode_entries > 0
                                     : counts.bytecode_entries == 0
    );
    REQUIRE(backend == BEHAVIOR_SLJIT ? counts.sljit_entries > 0 : counts.sljit_entries == 0);
    REQUIRE(counts.wasm_entries == 0);
    return true;
}
TEST_CASE(prepared_runtime_operations_keep_owner_and_live_values_without_native_callbacks)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(read_file("operations.prepared", &bytes, &length));
        unsigned char *metadata = NULL;
        size_t size = 0;
        REQUIRE(read_file("operations-state.txt", &metadata, &size));
        if (size > 160) {
            free(metadata);
            REQUIRE(false);
        }
        metadata[size] = 0;
        unsigned long long ids[6] = { 0 };
        char tail;
        int fields = sscanf(
            (char *)metadata,
            "%llu %llu %llu %llu %llu %llu %c",
            &ids[0],
            &ids[1],
            &ids[2],
            &ids[3],
            &ids[4],
            &ids[5],
            &tail
        );
        free(metadata);
        REQUIRE(fields == 6);
        for (int i = 0; i < 6; i++) {
            REQUIRE(ids[i]);
        }
        const operation_fixture fixture = { .entity = ids[0],
                                            .before = ids[1],
                                            .resumed = ids[2],
                                            .same = ids[3],
                                            .text = ids[4],
                                            .count = ids[5] };
        /* Owner and inline Delay are instructions; the stored timer resolves
         * through its built-in runtime identity, without a native catalog. */
        const execution_prepared_linker link = { .user = (void *)&fixture,
                                                 .type = operation_type };
        char error[256];
        module = execution_prepared_read(bytes, length, &link, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "prepared operations: %s\n", error);
        }
        REQUIRE(module && !execution_module_import_count(module));
        free(bytes);
        bytes = NULL;
        program = execution_program_new(module, NULL, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, 7);
        REQUIRE(instance);
        /* Give the saved string independent runtime ownership before the
         * authored state overwrite and suspension. */
        char initial[] = "old";
        const pin_string initial_text = { .bytes = initial, .length = 3 };
        REQUIRE(
            execution_instance_set_state(
                instance,
                fixture.text,
                (pin_value){ .type = PIN_STR, .text = &initial_text }
            ) == FLOW_OK
        );
        memcpy(initial, "bad", 3);
        uint64_t begin = execution_module_event(module, "Begin");
        REQUIRE(begin);
        behavior_run_scope run = { 0 };
        behavior_run_scope_begin(&run);
        int status = execution_instance_dispatch(instance, begin, NULL, 0, 0, 0, 1000);
        behavior_run_scope_end(&run);
        REQUIRE(status == FLOW_OK);
        REQUIRE(operation_backend_entries(run.counts, backends[b], "dispatch"));
        REQUIRE(execution_instance_pending(instance) == 1);
        REQUIRE(operation_states(instance, &fixture, 0));
        behavior_run_scope_reset(&run);
        behavior_run_scope_begin(&run);
        status = execution_program_update(program, 0.25, 0.25, 1000);
        behavior_run_scope_end(&run);
        REQUIRE(status == FLOW_OK);
        REQUIRE(operation_backend_entries(run.counts, backends[b], "resume"));
        REQUIRE(operation_states(instance, &fixture, 1));
        behavior_run_scope_reset(&run);
        behavior_run_scope_begin(&run);
        status = execution_program_update(program, 0.5, 0.25, 1000);
        behavior_run_scope_end(&run);
        REQUIRE(status == FLOW_OK);
        REQUIRE(operation_backend_entries(run.counts, backends[b], "stored-completion"));
        REQUIRE(operation_states(instance, &fixture, 2));
        REQUIRE(!execution_instance_pending(instance));
    }
    return true;
}

TEST_CASE(prepared_runtime_state_events_bindings_and_imports)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(inputs());
        REQUIRE(dependencies(backends[b]));
        execution_prepared_linker link = linker();
        char error[192];
        module = execution_prepared_read(bytes, length, &link, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "prepared main: %s\n", error);
        }
        REQUIRE(module && execution_module_import_count(module) == 1);
        free(bytes);
        free(library_bytes);
        free(wrong_bytes);
        bytes = library_bytes = wrong_bytes = NULL;
        world = ecs_world_new();
        REQUIRE(ecs_world_register_component(world, &prepared_counter_component, NULL) == ECS_OK);
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(
            ecs_world_record_add(world, entity, prepared_counter_component.id, NULL, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        logger = (execution_log_service){ .emit = log_message };
        service_binding *services = NULL;
        apush(services, ((service_binding){ EXECUTION_LOG_SERVICE, &logger }));
        program = execution_program_new(module, world, services);
        afree(services);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        REQUIRE(
            execution_instance_dispatch(instance, execution_module_event(module, "Begin"), NULL, 0, 0, 0, 1000) ==
            FLOW_OK
        );
        REQUIRE(execution_program_bindings(program, 0, 0, 1000) == FLOW_OK);
        REQUIRE(count_is(instance, entity, 1));
        REQUIRE(
            execution_instance_dispatch(instance, execution_module_event(module, "Tick"), NULL, 0, 1, .25, 1000) ==
            FLOW_OK
        );
        REQUIRE(execution_program_bindings(program, 1, .25, 1000) == FLOW_OK);
        REQUIRE(count_is(instance, entity, 3) && log_count == 1 && log_text);
        pin_value amount = { .type = PIN_I32, .i = 5 };
        const int expected[] = { 19, 36 };
        for (int call = 0; call < 2; call++) {
            REQUIRE(
                execution_instance_dispatch(
                    instance,
                    execution_module_event(module, "Add"),
                    &amount,
                    1,
                    call + 2,
                    0,
                    1000
                ) == FLOW_OK
            );
            REQUIRE(execution_program_pump(program, call + 2, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_bindings(program, call + 2, 0, 1000) == FLOW_OK);
            REQUIRE(count_is(instance, entity, expected[call]));
        }
    }
    return true;
}
TEST_CASE(prepared_runtime_rejects_missing_or_incompatible_linkage)
{
    REQUIRE(inputs());
    REQUIRE(dependencies(BEHAVIOR_BYTECODE));
    execution_prepared_linker link = linker();
    for (int failure = MISSING_HOST; failure <= EMPTY_SERVICE; failure++) {
        mismatch = failure;
        char error[192] = { 0 };
        module = execution_prepared_read(bytes, length, &link, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(!module && error[0]);
    }
    return true;
}
TEST_CASE(prepared_runtime_rejects_malformed_wire)
{
    REQUIRE(inputs());
    REQUIRE(dependencies(BEHAVIOR_BYTECODE));
    execution_prepared_linker link = linker();
    const size_t sizes[] = { 0, 1, 8, 16, 31, length / 2, length - 1 };
    for (int i = 0; i < 7; i++) {
        char error[192] = { 0 };
        module = execution_prepared_read(bytes, sizes[i], &link, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(!module && error[0]);
    }
    mutation = malloc(length + 1);
    REQUIRE(mutation);
    for (int i = 0; i < 4; i++) {
        memcpy(mutation, bytes, length);
        size_t size = length;
        if (i == 2) {
            mutation[length] = 0;
            size++;
        } else if (i == 3) {
            memset(mutation + 8, 0, 8);
            mutation[8] = 1; /* Version 1 cannot represent event declarations. */
        } else {
            mutation[i == 0 ? 0 : 8] ^= 0xff; /* Canonical magic and version u64 fields. */
        }
        char error[192] = { 0 };
        module = execution_prepared_read(mutation, size, &link, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(!module && error[0]);
    }
    return true;
}
TEST_CASE(prepared_runtime_shared_storage_budget_charges_only_success)
{
    REQUIRE(inputs());
    REQUIRE(dependencies(BEHAVIOR_BYTECODE));
    execution_prepared_linker link = linker();
    char error[192] = { 0 };
    size_t remaining = 0;
    module = execution_prepared_read_budget(bytes, length, &link, BEHAVIOR_BYTECODE, &remaining, error, sizeof error);
    REQUIRE(!module && error[0] && remaining == 0);
    const size_t budget = 64 * 1024 * 1024;
    remaining = budget;
    module = execution_prepared_read_budget(bytes, length, &link, BEHAVIOR_BYTECODE, &remaining, error, sizeof error);
    REQUIRE(module);
    size_t retained = execution_module_storage_bytes(module);
    REQUIRE(retained > 0 && retained < budget);
    REQUIRE(remaining == budget - retained);
    execution_module_free(module);
    module = NULL;
    remaining = retained - 1;
    size_t before = remaining;
    module = execution_prepared_read_budget(bytes, length, &link, BEHAVIOR_BYTECODE, &remaining, error, sizeof error);
    REQUIRE(!module && error[0] && remaining == before);
    remaining = budget;
    module = execution_prepared_read_budget(
        bytes,
        length - 1,
        &link,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    REQUIRE(!module && error[0] && remaining == budget);
    return true;
}
static bool reads_within(execution_prepared_linker *link, size_t budget)
{
    char error[192] = { 0 };
    size_t remaining = budget;
    execution_module *m = execution_prepared_read_budget(
        bytes,
        length,
        link,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    if (!m) {
        REQUIRE(error[0] && remaining == budget);
        return false;
    }
    REQUIRE(remaining == budget - execution_module_storage_bytes(m));
    execution_module_free(m);
    return true;
}
TEST_CASE(prepared_runtime_import_fingerprint_returns_its_scratch)
{
    REQUIRE(inputs());
    REQUIRE(dependencies(BEHAVIOR_BYTECODE));
    execution_prepared_linker link = linker();
    char error[192] = { 0 };
    size_t remaining = 64 * 1024 * 1024;
    module = execution_prepared_read_budget(bytes, length, &link, BEHAVIOR_BYTECODE, &remaining, error, sizeof error);
    REQUIRE(module);
    const size_t retained = execution_module_storage_bytes(module);
    unsigned char *written = NULL;
    size_t written_length = 0, measured = 0;
    uint64_t fingerprint = 0, hash = UINT64_C(14695981039346656037);
    REQUIRE(execution_prepared_write(library, &written, &written_length, error, sizeof error));
    REQUIRE(execution_prepared_measure(library, &measured, &fingerprint, error, sizeof error));
    for (size_t i = 0; i < written_length; i++) {
        hash = (hash ^ written[i]) * UINT64_C(1099511628211);
    }
    free(written);
    REQUIRE(measured == written_length && fingerprint == hash);
    /* Reading imports re-encodes the library to fingerprint it. That
     * encoding's scratch, its string table included, comes out of the
     * importer's budget while it runs and goes back afterwards, so every
     * budget either fails untouched or keeps exactly the retained bytes. */
    size_t low = retained - 1, high = 64 * 1024 * 1024;
    while (high - low > 1) {
        size_t middle = low + (high - low) / 2;
        *(reads_within(&link, middle) ? &high : &low) = middle;
    }
    REQUIRE(!reads_within(&link, high - 1) && reads_within(&link, high));
    return true;
}
TEST_CASE(prepared_runtime_package_resolves_reversed_records_atomically)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(inputs());
        REQUIRE(dependencies(backends[b]));
        execution_prepared_linker link = linker();
        char error[192];
        module = execution_prepared_read(bytes, length, &link, backends[b], error, sizeof error);
        REQUIRE(module);
        package_module records[] = {
            { .name = "Persist", .id = execution_module_id(module), .data = bytes, .length = length },
            { .name = "Depot",
              .id = execution_module_id(library),
              .data = library_bytes,
              .length = library_length },
        };
        const size_t collection_budget = 64 * 1024 * 1024;
        size_t collection_remaining = collection_budget;
        package_collection *collection = package_read_budget(
            records,
            2,
            &link,
            NULL,
            backends[b],
            &collection_remaining,
            error,
            sizeof error
        );
        REQUIRE(collection);
        bool named = package_collection_count(collection) == 2 &&
                     !strcmp(package_collection_name(collection, 0), "Persist") &&
                     !strcmp(package_collection_name(collection, 1), "Depot");
        size_t modules_bytes = named
                                 ? execution_module_storage_bytes(
                                       package_collection_module(collection, 0)
                                   ) +
                                       execution_module_storage_bytes(
                                           package_collection_module(collection, 1)
                                       )
                                 : 0;
        size_t collection_bytes = collection_budget - collection_remaining;
        if (collection) {
            package_collection_free(collection);
        }
        REQUIRE(named);
        REQUIRE(collection_bytes > modules_bytes && collection_bytes - modules_bytes < 4096);
        collection_remaining = collection_budget;
        collection = package_read_budget(records, 2, &link, NULL, backends[b], &collection_remaining, error, sizeof error);
        bool repeated = collection && collection_remaining ==
                                          collection_budget - collection_bytes;
        if (collection) {
            package_collection_free(collection);
        }
        REQUIRE(repeated);
        collection_remaining = collection_bytes - 1;
        collection = package_read_budget(records, 2, &link, NULL, backends[b], &collection_remaining, error, sizeof error);
        bool rejected = !collection && error[0] &&
                        collection_remaining == collection_bytes - 1;
        if (collection) {
            package_collection_free(collection);
        }
        REQUIRE(rejected);
        collection_remaining = 0;
        collection = package_read_budget(records, 2, &link, NULL, backends[b], &collection_remaining, error, sizeof error);
        rejected = !collection && error[0] && collection_remaining == 0;
        if (collection) {
            package_collection_free(collection);
        }
        REQUIRE(rejected);
        world = ecs_world_new();
        REQUIRE(ecs_world_register_component(world, &prepared_counter_component, NULL) == ECS_OK);
        logger = (execution_log_service){ .emit = log_message };
        service_binding *services = NULL;
        apush(services, ((service_binding){ EXECUTION_LOG_SERVICE, &logger }));
        registry = source_registry_new(world);
        size_t budget = 64 * 1024 * 1024, remaining = budget;
        source_staging *stage = package_stage_budget(
            registry,
            records,
            2,
            &link,
            services,
            backends[b],
            &remaining,
            error,
            sizeof error
        );
        REQUIRE(stage);
        source_commit(stage);
        REQUIRE(
            remaining == budget - execution_module_storage_bytes(module) -
                             execution_module_storage_bytes(library)
        );
        afree(services);
        REQUIRE(registry);
        REQUIRE(source_registered(registry, "Persist") && source_registered(registry, "Depot"));
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(
            ecs_world_record_add(world, entity, prepared_counter_component.id, NULL, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(source_attach(registry, entity, "Persist", false, 0, 0) == FLOW_OK);
        REQUIRE(source_step(registry, 0, 0) == FLOW_OK);
        const prepared_counter *counter = ecs_component_data(
            world,
            entity,
            prepared_counter_component.id
        );
        REQUIRE(counter && counter->value == 1);
        source_registry_free(registry);
        registry = NULL;
        for (int failure = 0; failure < 3; failure++) {
            package_module invalid[] = { records[0], records[1] };
            if (failure == 0) {
                invalid[1].id = invalid[0].id;
            } else if (failure == 1) {
                invalid[1].name = invalid[0].name;
            } else {
                invalid[1].length--;
            }
            error[0] = 0;
            apush(services, ((service_binding){ EXECUTION_LOG_SERVICE, &logger }));
            registry = package_load(world, invalid, 2, &link, services, backends[b], error, sizeof error);
            afree(services);
            REQUIRE(!registry && error[0]);
        }
    }
    return true;
}
static bool resolve_event(void *, uint64_t id, uint64_t abi)
{
    event_checks++;
    for (int i = 0; i < 3; i++) {
        if (declared_event_ids[i] == id) {
            return declared_event_abis[i] == abi;
        }
    }
    return false;
}
TEST_CASE(prepared_declared_event_linker_policy)
{
    REQUIRE(inputs());
    REQUIRE(dependencies(BEHAVIOR_BYTECODE));
    execution_prepared_linker link = linker();
    char error[192];
    module = execution_prepared_read(bytes, length, &link, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module && event_checks == 0); /* Owned declarations need no callback. */
    int declarations = execution_module_event_count(module);
    REQUIRE(declarations == 3);
    bool found = false;
    for (int i = 0; i < declarations; i++) {
        execution_module_event_info e = execution_module_event_at(module, i);
        declared_event_ids[i] = e.id;
        declared_event_abis[i] = e.abi;
        if (!strcmp(e.name, "Add")) {
            REQUIRE(e.id == 12 && e.abi && e.field_count == 1);
            execution_module_event_field_info f = execution_module_event_field_at(module, i, 0);
            REQUIRE(!strcmp(f.name, "amount") && f.initial.type == PIN_I32 && !f.nominal);
            REQUIRE(!f.has_default && !f.default_owner);
            found = true;
        }
    }
    REQUIRE(found);
    execution_module_free(module);
    module = NULL;
    link.event = resolve_event;
    module = execution_prepared_read(bytes, length, &link, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module && event_checks == declarations);
    execution_module_free(module);
    module = NULL;
    declared_event_abis[0] ^= 1;
    event_checks = 0;
    error[0] = 0;
    module = execution_prepared_read(bytes, length, &link, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(!module && event_checks > 0 && strstr(error, "event"));
    return true;
}
TEST_CASE(prepared_package_validates_complete_external_closure)
{
    const char *files[] = {
        "leaf.prepared",          "changed-leaf.prepared",
        "event-leaf.prepared",    "middle.prepared",
        "good-middle.prepared",   "bad-middle.prepared",
        "event-middle.prepared",  "closure.prepared",
        "good-closure.prepared",  "bad-closure.prepared",
        "event-closure.prepared",
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        closure_mode = true;
        for (int i = 0; i < 11; i++) {
            REQUIRE(
                read_file(files[i], &closure_bytes[i].data, &closure_bytes[i].length)
            );
        }
        execution_prepared_linker link = linker();
        char error[192];
        for (int i = 0; i < 4; i++) {
            int file = i < 2 ? 0 : i - 1;
            closure_leaves[i] = execution_prepared_read(
                closure_bytes[file].data,
                closure_bytes[file].length,
                &link,
                backends[b],
                error,
                sizeof error
            );
            REQUIRE(closure_leaves[i]);
        }
        REQUIRE(closure_leaves[0] != closure_leaves[1]);
        REQUIRE(
            execution_module_id(closure_leaves[0]) ==
            execution_module_id(closure_leaves[1])
        );
        REQUIRE(
            execution_module_id(closure_leaves[0]) ==
            execution_module_id(closure_leaves[2])
        );
        REQUIRE(execution_module_event_count(closure_leaves[0]) == 1);
        REQUIRE(execution_module_event_count(closure_leaves[3]) == 1);
        execution_module_event_info normal = execution_module_event_at(closure_leaves[0], 0);
        execution_module_event_info conflict = execution_module_event_at(closure_leaves[3], 0);
        REQUIRE(normal.id == 12 && conflict.id == 12 && normal.abi != conflict.abi);
        REQUIRE(execution_module_event_field_at(closure_leaves[0], 0, 0).initial.type == PIN_I32);
        REQUIRE(execution_module_event_field_at(closure_leaves[3], 0, 0).initial.type == PIN_BOOL);
        for (int i = 0; i < 4; i++) {
            selected_leaf = i;
            closure_middles[i] = execution_prepared_read(
                closure_bytes[3 + i].data,
                closure_bytes[3 + i].length,
                &link,
                backends[b],
                error,
                sizeof error
            );
            REQUIRE(
                closure_middles[i] &&
                execution_module_import_count(closure_middles[i]) == 1
            );
            REQUIRE(execution_module_import_at(closure_middles[i], 0) == closure_leaves[i]);
        }
        package_module records[4];
        const char *names[] = { "Closure", "GoodClosure", "BadClosure", "EventClosure" };
        for (int i = 0; i < 4; i++) {
            module = execution_prepared_read(
                closure_bytes[7 + i].data,
                closure_bytes[7 + i].length,
                &link,
                backends[b],
                error,
                sizeof error
            );
            REQUIRE(module && execution_module_import_count(module) == 1);
            records[i] = (package_module){
                .name = names[i],
                .id = execution_module_id(module),
                .data = closure_bytes[7 + i].data,
                .length = closure_bytes[7 + i].length,
            };
            execution_module_free(module);
            module = NULL;
        }
        world = ecs_world_new();
        registry = package_load(world, records, 2, &link, NULL, backends[b], error, sizeof error);
        REQUIRE(
            registry && source_registered(registry, "Closure") &&
            source_registered(registry, "GoodClosure")
        );
        source_registry_free(registry);
        registry = NULL;
        for (int i = 2; i < 4; i++) {
            package_module conflicting[] = { records[0], records[i] };
            error[0] = 0;
            registry = package_load(world, conflicting, 2, &link, NULL, backends[b], error, sizeof error);
            REQUIRE(!registry);
            REQUIRE(strstr(error, i == 2 ? "conflicting generations" : "conflicting declarations"));
        }
    }
    return true;
}

TEST_CASE(prepared_runtime_state_action_banks_survive_wire_and_replacement)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(read_file("state-actions.prepared", &bytes, &length));
        unsigned char *metadata = NULL;
        size_t size = 0;
        REQUIRE(read_file("state-action-id.txt", &metadata, &size));
        if (size > 32) {
            free(metadata);
            REQUIRE(false);
        }
        metadata[size] = 0;
        unsigned long long identity = 0;
        char tail;
        int fields = sscanf((char *)metadata, "%llu %c", &identity, &tail);
        free(metadata);
        REQUIRE(fields == 1 && identity);
        execution_prepared_linker link = linker();
        char error[256];
        module = execution_prepared_read(bytes, length, &link, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "prepared state actions: %s\n", error);
        }
        REQUIRE(module);
        free(bytes);
        bytes = NULL;
        world = ecs_world_new();
        ecs_entity owner = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        const int expected[] = { 1110, 11111, 11121 };
        for (int i = 0; i < 3; i++) {
            REQUIRE(execution_instance_dispatch(instance, 11, NULL, 0, i, 0, 1000) == FLOW_OK);
            const pin_value *value = execution_instance_state(instance, identity);
            REQUIRE(value && value->type == PIN_I32 && value->i == expected[i]);
        }
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        instance = execution_program_instance(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 11, NULL, 0, 3, 0, 1000) == FLOW_OK);
        const pin_value *value = execution_instance_state(instance, identity);
        REQUIRE(value && value->type == PIN_I32 && value->i == 11122);
        REQUIRE(execution_program_replace(program, module, true) == FLOW_OK);
        instance = execution_program_instance(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 11, NULL, 0, 4, 0, 1000) == FLOW_OK);
        value = execution_instance_state(instance, identity);
        REQUIRE(value && value->type == PIN_I32 && value->i == 1110);
        const pin_value amount = { .type = PIN_I32, .i = 1 };
        REQUIRE(execution_instance_dispatch(instance, 12, &amount, 1, 5, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_dispatch(instance, 12, &amount, 1, 6, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_pump(program, 8, 0, 1000) == FLOW_OK);
        value = execution_instance_state(instance, identity);
        REQUIRE(value && value->type == PIN_I32 && value->i == 1110);
        REQUIRE(execution_program_pump(program, 9, 0, 1000) == FLOW_OK);
        value = execution_instance_state(instance, identity);
        REQUIRE(value && value->type == PIN_I32 && value->i == 101110);
    }
    return true;
}

TEST_CASE(prepared_runtime_field_observer_baseline_edges_and_retarget)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(read_file("field-observer.prepared", &bytes, &length));
        unsigned char *metadata = NULL;
        size_t size = 0;
        REQUIRE(read_file("field-observer-ids.txt", &metadata, &size));
        if (size > 64) {
            free(metadata);
            REQUIRE(false);
        }
        metadata[size] = 0;
        unsigned long long count_id = 0, target_id = 0;
        char tail;
        int fields = sscanf((char *)metadata, "%llu %llu %c", &count_id, &target_id, &tail);
        free(metadata);
        REQUIRE(fields == 2 && count_id && target_id);
        execution_prepared_linker link = linker();
        char error[256];
        module = execution_prepared_read(bytes, length, &link, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "prepared field observer: %s\n", error);
        }
        REQUIRE(module);
        free(bytes);
        bytes = NULL;
        world = ecs_world_new();
        REQUIRE(!ecs_world_register_component(world, &prepared_counter_component, NULL));
        ecs_entity entities[] = { ecs_world_create_entity(world),
                                  ecs_world_create_entity(world) };
        const prepared_counter initial[] = { { .value = 1 }, { .value = 20 } };
        for (int i = 0; i < 2; i++) {
            REQUIRE(!ecs_world_record_add(
                world,
                entities[i],
                prepared_counter_component.id,
                &initial[i],
                0
            ));
        }
        REQUIRE(!ecs_world_apply_commands(world));
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(entities[0])
        );
        REQUIRE(instance);
        pin_value destination = { .type = PIN_HANDLE,
                                  .type_name = "ecs.entity",
                                  .u64 = ecs_entity_to_u64(entities[0]) };
        REQUIRE(execution_instance_dispatch(instance, 81, &destination, 1, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        const pin_value *count = execution_instance_state(instance, count_id);
        REQUIRE(count && count->type == PIN_I32 && count->i == 0);
        prepared_counter *first = ecs_component_data(
            world,
            entities[0],
            prepared_counter_component.id
        );
        first->value = 5;
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        count = execution_instance_state(instance, count_id);
        REQUIRE(count && count->type == PIN_I32 && count->i == 5);
        destination.u64 = ecs_entity_to_u64(entities[1]);
        REQUIRE(execution_instance_dispatch(instance, 81, &destination, 1, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_step(program, 2, 0, 1000) == FLOW_OK);
        count = execution_instance_state(instance, count_id);
        REQUIRE(count && count->type == PIN_I32 && count->i == 5);
        const pin_value *target = execution_instance_state(instance, target_id);
        REQUIRE(
            target && target->type == PIN_HANDLE && target->u64 == destination.u64
        );
        first = ecs_component_data(
            world,
            entities[0],
            prepared_counter_component.id
        );
        first->value = 100;
        REQUIRE(execution_program_step(program, 3, 0, 1000) == FLOW_OK);
        count = execution_instance_state(instance, count_id);
        REQUIRE(count && count->type == PIN_I32 && count->i == 5);
        prepared_counter *second = ecs_component_data(
            world,
            entities[1],
            prepared_counter_component.id
        );
        second->value = 9;
        REQUIRE(execution_program_step(program, 4, 0, 1000) == FLOW_OK);
        count = execution_instance_state(instance, count_id);
        REQUIRE(count && count->type == PIN_I32 && count->i == 14);
        REQUIRE(execution_program_step(program, 5, 0, 1000) == FLOW_OK);
        count = execution_instance_state(instance, count_id);
        REQUIRE(count && count->type == PIN_I32 && count->i == 14);
    }
    return true;
}

TEST_CASE(prepared_runtime_debugger_uses_decoded_occurrences_without_compiler)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(read_file("source-debug.prepared", &bytes, &length));
        unsigned char *metadata;
        size_t size;
        REQUIRE(read_file("source-debug-state.txt", &metadata, &size));
        metadata[size] = 0;
        unsigned long long identity = 0;
        char tail;
        int fields = sscanf((char *)metadata, "%llu %c", &identity, &tail);
        free(metadata);
        REQUIRE(fields == 1 && identity);
        execution_prepared_linker link = linker();
        char error[256];
        module = execution_prepared_read(bytes, length, &link, backends[b], error, sizeof error);
        REQUIRE(module);
        free(bytes);
        bytes = NULL;
        world = ecs_world_new();
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        uint64_t owner = ecs_entity_to_u64(entity);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, owner);
        REQUIRE(instance);
        uint64_t work = 0, handler = 0, occurrence = 0;
        const behavior_function *body = NULL;
        for (int n = 0; n < execution_module_function_count(module); n++) {
            execution_module_function_info f = execution_module_function_at(module, n);
            if (f.kind == EXECUTION_FUNCTION_AUTHORED) {
                work = f.id;
            }
            if (f.event == 11) {
                handler = f.id;
                body = f.body;
            }
        }
        REQUIRE(work && handler && body);
        for (int n = 0; n < behavior_function_source_point_count(body); n++) {
            behavior_source_point p = behavior_function_source_point_at(body, n);
            if (p.meaning == work && !p.completion) {
                occurrence = p.occurrence;
            }
        }
        REQUIRE(occurrence);
        debugger = execution_program_inspector(program);
        REQUIRE(
            execution_inspector_breakpoint(debugger, execution_module_id(module), handler, occurrence, true)
        );
        REQUIRE(execution_instance_dispatch(instance, 11, NULL, 0, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_paused(program));
        REQUIRE(execution_inspector_cursor(debugger).status == FLOW_PAUSED_RESULT);
        const pin_value *count = execution_instance_state(instance, identity);
        REQUIRE(count && count->type == PIN_I32 && count->i == 0);
        REQUIRE(
            execution_inspector_breakpoint(debugger, execution_module_id(module), handler, occurrence, false)
        );
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_INTO) == FLOW_PAUSED_RESULT);
        REQUIRE(execution_inspector_cursor(debugger).depth == 1);
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_OUT) == FLOW_PAUSED_RESULT);
        count = execution_instance_state(instance, identity);
        REQUIRE(count && count->i == 1);
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_OK);
        REQUIRE(!execution_program_paused(program));
        count = execution_instance_state(instance, identity);
        REQUIRE(count && count->i == 2);
        REQUIRE(execution_inspector_watch_count(debugger) > 0);
        execution_program_free(program);
        program = NULL;
        execution_module_free(module);
        module = NULL;
        REQUIRE(execution_inspector_cursor(debugger).retired);
        REQUIRE(execution_inspector_watch_count(debugger) > 0);
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_ERR_STATE);
    }
    return true;
}

static const behavior_function *scalar_getter()
{
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info function = execution_module_function_at(module, i);
        if (function.kind == EXECUTION_FUNCTION_AUTHORED) {
            return function.body;
        }
    }
    return NULL;
}

static bool scalar_getter_run(
    const behavior_function *function,
    ecs_entity entity,
    bool compiled,
    bool present,
    bool expected
)
{
    arena storage = { 0 };
    behavior_function_activation activation = { 0 };
    behavior_function_activation_init(function, &activation);
    pin_value result = { .type = PIN_BOOL };
    behavior_function_context context = {
        .results = &result,
        .activation = &activation,
        .budget = 1000,
        .world = ecs_world_execution(world),
        .failure = BEHAVIOR_FUNCTION_HOST_FLOW,
        .error_message = "stale error",
    };
    behavior_function_bind_storage(&context, &storage);
    bool bound = behavior_function_bind(function, &context, NULL, ecs_entity_to_u64(entity));
    int status = bound ? behavior_function_run(function, &context, compiled)
                       : FLOW_ERR_STATE;
    bool valid = present
                   ? status == FLOW_OK && result.type == PIN_BOOL &&
                         result.b == expected && !context.error_message &&
                         context.failure == BEHAVIOR_FUNCTION_HOST_FLOW
                   : status == FLOW_ERR_INVALID_GRAPH &&
                         context.failure == BEHAVIOR_FUNCTION_HOST_DATA &&
                         context.error_message &&
                         !strcmp(context.error_message, "Referenced storage is unavailable");
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    REQUIRE(bound && valid);
    return true;
}

TEST_CASE(prepared_runtime_scalar_getter_executes_after_wire_retirement)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(read_file("scalar-read.prepared", &bytes, &length));
        REQUIRE(length <= 16 * 1024);
        execution_prepared_linker link = linker();
        char error[192];
        module = execution_prepared_read(bytes, length, &link, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "prepared scalar getter: %s\n", error);
        }
        REQUIRE(module && !execution_module_import_count(module));
        free(bytes);
        bytes = NULL;
        const behavior_function *function = scalar_getter();
        REQUIRE(function && execution_module_function_count(module) == 1);
        REQUIRE(b == 0 || behavior_function_entry(function));

        /* A bool value read must validate layout and stride just like numeric
         * reads; it is not the older bool component-presence operation. */
        for (int schema = 0; schema < 3; schema++) {
            world = ecs_world_new();
            type_desc wrong_type = prepared_counter_type;
            ecs_component_desc wrong = prepared_counter_component;
            if (schema) {
                wrong.type = &wrong_type;
                wrong.layout_hash = type_layout_hash(&prepared_counter_type);
                if (schema == 1) {
                    wrong.layout_hash ^= 1;
                } else {
                    wrong_type.size++;
                    wrong.element_size++;
                }
                REQUIRE(ecs_world_register_component(world, &wrong, NULL) == ECS_OK);
            }
            REQUIRE(!execution_module_components_valid(module, world));
            program = execution_program_new(module, world, NULL);
            REQUIRE(!program);
            ecs_world_free(world);
            world = NULL;
        }
        world = ecs_world_new();
        REQUIRE(ecs_world_register_component(world, &prepared_counter_component, NULL) == ECS_OK);
        REQUIRE(execution_module_components_valid(module, world));
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        ecs_entity entity = ecs_world_create_entity(world);
        prepared_counter initial = { .value = 17, .enabled = true };
        REQUIRE(
            ecs_world_record_add(world, entity, prepared_counter_component.id, &initial, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(scalar_getter_run(function, entity, b == 1, true, true));
        prepared_counter *counter = ecs_component_data(
            world,
            entity,
            prepared_counter_component.id
        );
        REQUIRE(counter);
        counter->enabled = false;
        REQUIRE(scalar_getter_run(function, entity, b == 1, true, false));
        REQUIRE(
            ecs_world_record_remove(world, entity, prepared_counter_component.id, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(scalar_getter_run(function, entity, b == 1, false, false));
    }
    return true;
}

TEST_CASE(prepared_runtime_scalar_getter_rejects_old_version_and_forged_range)
{
    REQUIRE(read_file("scalar-read.prepared", &bytes, &length));
    REQUIRE(length <= 16 * 1024);
    execution_prepared_linker link = linker();
    char error[192];
    size_t target = 0;
    REQUIRE(prepared_scalar_getter_target(bytes, length, &target));
    unsigned char crossing = (unsigned char)(sizeof(prepared_counter) * 2);
    execution_wire header = { .data = bytes, .size = length, .reading = true };
    uint64_t magic = 0, version = 0;
    execution_wire_u64(&header, &magic);
    size_t version_offset = header.offset;
    execution_wire_u64(&header, &version);
    REQUIRE(!header.error && version == EXECUTION_PREPARED_VERSION);
    REQUIRE(header.offset == version_offset + 1);
    mutation = malloc(length);
    REQUIRE(mutation);
    for (int invalid = 0; invalid < 3; invalid++) {
        memcpy(mutation, bytes, length);
        if (!invalid) {
            mutation[version_offset] = EXECUTION_PREPARED_VERSION - 1;
        } else {
            mutation[target] = invalid == 1 ? 1 : crossing; /* Zigzag -1 or stride. */
        }
        module = execution_prepared_read(mutation, length, &link, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(!module);
        REQUIRE(
            strstr(error, invalid ? "prepared definition verification failed" : "unknown prepared format version")
        );
    }
    const int incompatible[] = { MISSING_TYPE, TYPE_LAYOUT, TYPE_STORAGE };
    for (int i = 0; i < 3; i++) {
        mismatch = incompatible[i];
        module = execution_prepared_read(bytes, length, &link, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(!module && strstr(error, "type requirement"));
    }
    return true;
}

TEST_CASE(prepared_runtime_owned_native_header_checks_linked_layout)
{
    REQUIRE(read_file("native-header.prepared", &bytes, &length));
    REQUIRE(length <= 16 * 1024);
    static const type_desc *const reflected[] = { &prepared_counter_type };
    static const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    type_catalog_set_project(&pack);
    native_header_types = true;
    execution_prepared_linker link = linker();
    char error[192];
    module = execution_prepared_read(bytes, length, &link, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module && execution_module_function_count(module) == 1);
    execution_module_free(module);
    module = NULL;
    size_t native_layout = 0;
    REQUIRE(prepared_native_header_layout(bytes, length, &native_layout));
    mutation = malloc(length);
    REQUIRE(mutation);
    memcpy(mutation, bytes, length);
    mutation[native_layout] ^= 1; /* Keep varint framing; change only layout bits. */
    module = execution_prepared_read(mutation, length, &link, BEHAVIOR_BYTECODE, error, sizeof error);
    type_catalog_set_project(NULL);
    native_header_types = false;
    REQUIRE(!module && strstr(error, "Prepared struct layout differs from linked type"));
    return true;
}

void test_tool_prepared_runtime(const char *path)
{
    directory = path;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_runtime_operations_keep_owner_and_live_values_without_native_callbacks);
    RUN_TEST_CASE(prepared_runtime_state_events_bindings_and_imports);
    RUN_TEST_CASE(prepared_runtime_rejects_missing_or_incompatible_linkage);
    RUN_TEST_CASE(prepared_runtime_rejects_malformed_wire);
    RUN_TEST_CASE(prepared_runtime_shared_storage_budget_charges_only_success);
    RUN_TEST_CASE(prepared_runtime_import_fingerprint_returns_its_scratch);
    RUN_TEST_CASE(prepared_runtime_package_resolves_reversed_records_atomically);
    RUN_TEST_CASE(prepared_declared_event_linker_policy);
    RUN_TEST_CASE(prepared_package_validates_complete_external_closure);
    RUN_TEST_CASE(prepared_runtime_state_action_banks_survive_wire_and_replacement);
    RUN_TEST_CASE(prepared_runtime_field_observer_baseline_edges_and_retarget);
    RUN_TEST_CASE(prepared_runtime_debugger_uses_decoded_occurrences_without_compiler);
    RUN_TEST_CASE(prepared_runtime_scalar_getter_executes_after_wire_retirement);
    RUN_TEST_CASE(prepared_runtime_scalar_getter_rejects_old_version_and_forged_range);
    RUN_TEST_CASE(prepared_runtime_owned_native_header_checks_linked_layout);
    pu_clear_setup();
}
