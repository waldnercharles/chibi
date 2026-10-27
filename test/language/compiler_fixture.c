#include "compiler_fixture.h"

#include "authoring/package_tests.h"
#include "backend.h"
#include "execution/module.h"
#include "execution/status.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/tasks.h"
#ifdef CHIBI_PACKAGE_TEST_POOL
#include "runtime/task_pool.h"
#endif
#include "runtime/text_definition.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/map.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
/* Differentials compare values, not packing: results box every packed array
 * in place, in the result's own storage, through unions and record fields. */
static void box(arena *storage, const pin_value *value, int depth);
static void box_record(arena *storage, const pin_value *value, int depth)
{
    const type_desc *type = pin_value_type(value);
    for (int i = 0; type && i < type->field_count; i++) {
        pin_value field;
        {
            const char *error = pin_read_field(storage, value->ptr, &type->fields[i], &field);
            if (error) {
                abort();
            }
        };
        if (field.type == PIN_UNION) {
            box(storage, &field, depth + 1);
        }
    }
}
static void box(arena *storage, const pin_value *value, int depth)
{
    if (!value->ptr || depth > 64) {
        return;
    }
    if (value->container == PIN_CONTAINER_ARRAY || value->container == PIN_CONTAINER_SET) {
        pin_array *array = value->ptr;
        if (array->stride) {
            pin_value *items = array->count
                                 ? arena_alloc(storage, (size_t)array->count * sizeof *items)
                                 : NULL;
            for (int i = 0; i < array->count; i++) {
                items[i] = pin_array_get(array, i);
            }
            *array = (pin_array){ .element_type = array->element_type,
                                  .type_name = array->type_name,
                                  .record = array->record,
                                  .count = array->count,
                                  .capacity = array->count,
                                  .items = items };
        }
        for (int i = 0; i < array->count; i++) {
            box(storage, &array->items[i], depth + 1);
        }
        return;
    }
    if (value->container == PIN_CONTAINER_MAP) {
        pin_map *map = value->ptr;
        for (int i = 0; i < map->count; i++) {
            box(storage, &map->entries[i].key, depth + 1);
            box(storage, &map->entries[i].value, depth + 1);
        }
        return;
    }
    if (value->type == PIN_UNION) {
        pin_union *tagged = value->ptr;
        for (int i = 0; i < tagged->count; i++) {
            box(storage, &tagged->values[i], depth + 1);
        }
        return;
    }
    if (value->type == PIN_STRUCT && value->record) {
        box_record(storage, value, depth);
    }
}
const authoring_package *compiler_fixture_package(
    const compiler_fixture *fixture,
    int backend,
    const char *identity
)
{
    assert(backend >= 0 && backend < 2 && fixture->owners[backend]);
    for (int i = 0; i < authoring_packages_count(fixture->owners[backend]); i++) {
        const authoring_package *package = authoring_packages_at(fixture->owners[backend], i);
        if (!strcmp(package->identity, identity)) {
            return package;
        }
    }
    return NULL;
}
uint64_t compiler_fixture_export(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const char *name
)
{
    const authoring_package *package = compiler_fixture_package(fixture, backend, identity);
    for (int i = 0; package && i < package->exports->member_count; i++) {
        if (!strcmp(package->exports->members[i].name, name)) {
            return package->exports->members[i].id;
        }
    }
    return 0;
}
void compiler_fixture_free(compiler_fixture *fixture)
{
    if (fixture->managed) {
        behavior_managed_heap_free(fixture->managed);
        free(fixture->managed);
    }
    for (int b = 0; b < 2; b++) {
        authoring_packages_free(fixture->owners[b]);
    }
    native_capabilities_free(fixture->native);
    *fixture = (compiler_fixture){ 0 };
}
bool compiler_fixture_prepare(compiler_fixture *fixture, const source_package_input *files, int count)
{
    return compiler_fixture_prepare_packs(fixture, files, count, NULL, 0);
}
bool compiler_fixture_prepare_packs(
    compiler_fixture *fixture,
    const source_package_input *files,
    int count,
    const native_definition_pack *additional,
    int additional_count
)
{
    return compiler_fixture_prepare_backends(fixture, COMPILER_FIXTURE_BOTH, files, count, additional, additional_count);
}
bool compiler_fixture_prepare_backends(
    compiler_fixture *fixture,
    unsigned selected,
    const source_package_input *files,
    int count,
    const native_definition_pack *additional,
    int additional_count
)
{
    assert(selected && !(selected & ~COMPILER_FIXTURE_BOTH));
    assert(!fixture->native && !fixture->owners[0] && !fixture->owners[1]);
    REQUIRE(additional_count >= 0 && (!additional_count || additional));
    fixture->managed = xmalloc(sizeof *fixture->managed);
    behavior_managed_heap_init(fixture->managed);
    native_definition_pack *packs = xmalloc(((size_t)additional_count + 2) * sizeof *packs);
    packs[0] = NATIVE_TEXT_DEFINITION_PACK;
    packs[1] = NATIVE_ARRAY_DEFINITION_PACK;
    for (int i = 0; i < additional_count; i++) {
        packs[i + 2] = additional[i];
    }
    char message[192];
    fixture->native = native_capabilities_from_definition_packs(
        NULL,
        0,
        packs,
        additional_count + 2,
        message,
        sizeof message
    );
    free(packs);
    REQUIRE(fixture->native);
    source_package_collection collections[] = { { "core", "/sdk/core" } };
    source_package_snapshot_options options = { .project_root = "/compiler",
                                                .files = files,
                                                .file_count = count,
                                                .collections = collections,
                                                .collection_count = 1 };
    for (int b = 0; b < 2; b++) {
        if (!(selected & (1u << b))) {
            continue;
        }
        source_package_snapshot *snapshot = source_package_snapshot_new(&options);
        REQUIRE(snapshot);
        const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
        if (error) {
            int document = error->diagnostic.range.document;
            const char *path = document >= 0 && document < count
                                 ? files[document].path
                                 : "compiler fixture";
            fprintf(
                stderr,
                "%s[%d]:%d:%d: %s\n",
                path,
                document,
                error->diagnostic.range.line,
                error->diagnostic.range.column,
                error->diagnostic.message
            );
            source_package_snapshot_free(snapshot);
            REQUIRE(false);
        }
        script_lower_bindings bindings = authoring_native_bindings(fixture->native);
        fixture->owners[b] = authoring_packages_prepare(
            snapshot,
            native_capabilities_catalog(fixture->native),
            &bindings,
            backends[b],
            NULL
        );
        REQUIRE(fixture->owners[b]);
        for (int p = 0; p < authoring_packages_count(fixture->owners[b]); p++) {
            const authoring_package *package = authoring_packages_at(fixture->owners[b], p);
            for (int d = 0; d < package->diagnostic_count; d++) {
                const script_diagnostic *diagnostic = &package->diagnostics[d];
                const script_source_document
                    *document = package->analysis ? script_analysis_document(
                                                        package->analysis,
                                                        diagnostic->range.document
                                                    )
                                                  : NULL;
                fprintf(
                    stderr,
                    "%s:%d:%d: %s\n",
                    document ? document->path : package->identity,
                    diagnostic->range.line,
                    diagnostic->range.column,
                    diagnostic->message
                );
            }
        }
        REQUIRE(authoring_packages_valid(fixture->owners[b]));
        int document_count = 0;
        const script_package_document *documents = source_package_snapshot_documents(
            authoring_packages_snapshot(fixture->owners[b]),
            &document_count
        );
        REQUIRE(document_count == count);
        for (int i = 0; i < count; i++) {
            REQUIRE(documents[i].syntax && documents[i].syntax->text != files[i].text);
            REQUIRE(strlen(documents[i].syntax->text) == files[i].size);
            REQUIRE(!memcmp(documents[i].syntax->text, files[i].text, files[i].size));
        }
    }
    return true;
}
/* Shares the authored runner's worker override, not the outer C case pool's.
 * Keep invalid settings fatal even on platforms without a native pool. */
static bool test_workers(int *workers)
{
    *workers = 0;
    const char *setting = getenv("CHIBI_TEST_WORKERS");
    if (!setting) {
        return true;
    }
    char *end;
    errno = 0;
    long value = strtol(setting, &end, 10);
    if (!*setting || strspn(setting, "0123456789") != strlen(setting) || *end ||
        errno || value < 1 || value > TASK_WORKER_MAX) {
        fprintf(pu_errors(), "CHIBI_TEST_WORKERS must be an integer from 1 to %d\n", TASK_WORKER_MAX);
        return false;
    }
    *workers = (int)value;
    return true;
}
bool compiler_fixture_tests(const compiler_fixture *fixture)
{
    int workers;
    if (!test_workers(&workers)) {
        return false;
    }
    const task_system *tasks = NULL;
#ifdef CHIBI_PACKAGE_TEST_POOL
    task_pool *pool = task_pool_acquire_workers(workers);
    tasks = task_pool_tasks(pool);
#else
    (void)workers;
#endif
    bool found = false, passed = true;
    for (int b = 0; b < 2; b++) {
        if (!fixture->owners[b]) {
            continue;
        }
        const char *backend = behavior_backend_name(backends[b]);
        if (!behavior_backend_available(backends[b])) {
            fprintf(pu_errors(), "%s: authored test backend is unavailable\n", backend);
            passed = false;
            continue;
        }
        package_test *tests = package_tests_find(fixture->owners[b]);
        int count = acount(tests);
        if (!count) {
            fprintf(pu_errors(), "%s: no authored tests found\n", backend);
            passed = false;
            afree(tests);
            continue;
        }
        found = true;
        package_test_result *results = xcalloc((size_t)count, sizeof *results);
#ifdef CHIBI_PACKAGE_TEST_POOL
        task_pool_begin(pool, count > 1);
#endif
        package_tests_run(tests, count, backends[b], PACKAGE_TEST_BUDGET, tasks, results);
#ifdef CHIBI_PACKAGE_TEST_POOL
        task_pool_end(pool);
#endif
        int failed = 0;
        for (int i = 0; i < count; i++) {
            fprintf(
                pu_output(),
                "%s %s.%s (%s)\n",
                results[i].passed ? "PASS" : "FAIL",
                tests[i].package,
                tests[i].name,
                backend
            );
            if (results[i].passed) {
                continue;
            }
            failed++;
            const package_test_failure *failure = &results[i].failure;
            if (failure->path) {
                fprintf(
                    pu_output(),
                    "  %s:%d:%d: %s\n",
                    failure->path,
                    failure->line,
                    failure->column,
                    failure->message
                );
            } else {
                fprintf(pu_output(), "  %s\n", failure->message);
            }
        }
        fprintf(pu_output(), "%s: %d tests, %d failed\n", backend, count, failed);
        passed &= !failed;
        free(results);
        afree(tests);
    }
#ifdef CHIBI_PACKAGE_TEST_POOL
    task_pool_release(pool);
#endif
    if (!found) {
        fprintf(pu_errors(), "Compiler fixture contains no authored tests\n");
    }
    return found && passed;
}
const pin_record *compiler_fixture_record(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const char *name,
    arena *storage
)
{
    const authoring_package *package = compiler_fixture_package(fixture, backend, identity);
    if (!package) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(fixture, backend, identity, name)
    );
    if (!type || !type->authored_record || type->field_count > 32) {
        return NULL;
    }
    type_field fields[32] = { 0 };
    for (int i = 0; i < type->field_count; i++) {
        fields[i] = (type_field){ .name = type->fields[i].name,
                                  .type = type->fields[i].type.storage };
    }
    return pin_record_create(storage, type->name, type->id, fields, type->field_count);
}
typedef struct
{
    bool observed;
    behavior_function_failure category;
    uintptr_t steps;
    char message[192];
    char source[192];
    int line;
    int column;
} failure_state;
typedef struct
{
    behavior_function_context context;
    behavior_function_activation activation;
    arena producer;
    pin_value result;
    failure_state failure;
} call_state;
static void observe_failure(const behavior_function *function, const behavior_function_context *context, int)
{
    failure_state *failure = context->inspection_user;
    if (failure->observed) {
        return;
    }
    /* Child contexts retire during unwind; retain only this first observation. */
    failure->observed = true;
    failure->category = context->failure;
    failure->steps = context->error_steps;
    const behavior_function_source
        *source = behavior_function_source_at(function, context->error_source);
    if (source) {
        snprintf(
            failure->source,
            sizeof failure->source,
            "%s",
            source->asset_name ? source->asset_name : ""
        );
        failure->line = source->line;
        failure->column = source->column;
    }
    snprintf(
        failure->message,
        sizeof failure->message,
        "%s",
        context->error_message ? context->error_message : ""
    );
}
static bool run(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const char *name,
    const pin_value *arguments,
    int count,
    int budget,
    arena *detached,
    pin_value *out,
    call_state *state
)
{
    REQUIRE(backend >= 0 && backend < 2 && budget > 0);
    const authoring_package *package = compiler_fixture_package(fixture, backend, identity);
    REQUIRE(package && package->valid && package->module);
    uint64_t id = compiler_fixture_export(fixture, backend, identity, name);
    const script_catalog_capability
        *signature = script_catalog_capability_by_id(package->exports->metadata, id);
    REQUIRE(signature && signature->result_count == 1);
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(package->module); i++) {
        execution_module_function_info
            info = execution_module_function_at(package->module, i);
        if (info.id == id) {
            function = info.body;
        }
    }
    REQUIRE(function);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &state->activation);
    }
    state->result = (pin_value){ .type = signature->results[0].type.storage,
                                 .container = signature->results[0].type.container };
    state->context = (behavior_function_context){
        .activation = behavior_function_resumable(function) ? &state->activation : NULL,
        .arguments = arguments,
        .argument_count = count,
        .results = &state->result,
        .managed_heap = fixture->managed,
        .budget = (uintptr_t)budget,
        .inspection_user = fixture->observe_failures ? &state->failure : NULL,
        .failure_observer = fixture->observe_failures ? observe_failure : NULL
    };
    if (fixture->collectable) {
        arena_init_collectable(&state->producer);
    }
    behavior_function_bind_storage(&state->context, &state->producer);
    REQUIRE(behavior_function_bind(function, &state->context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &state->context, backend != 0);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK || !state->context.returned) {
        const behavior_function_context *context = &state->context;
        const failure_state *observed = &state->failure;
        behavior_function_failure category = observed->observed ? observed->category
                                                                : context->failure;
        uintptr_t steps = observed->observed ? observed->steps : context->error_steps;
        const char *message = observed->observed ? observed->message
                                                 : context->error_message;
        bool budget_failure = status == FLOW_ERR_STEP_BUDGET ||
                              (observed->observed && category == BEHAVIOR_FUNCTION_BUDGET);
        const char *failure = budget_failure                      ? "budget"
                            : category == BEHAVIOR_FUNCTION_DEPTH ? "depth"
                            : !observed->observed && !steps && !message
                                ? "unreported"
                                : "other";
        fprintf(
            stderr,
            "compiler fixture failure: backend=%s package=%s entry=%s "
            "status=%d "
            "returned=%d failure=%s category=%d error_steps=%zu "
            "context_steps=%zu context_budget=%zu call_budget=%d message=%s "
            "source=%s:%d:%d\n",
            backend == 0 ? "bytecode" : "compiled",
            identity,
            name,
            status,
            context->returned,
            failure,
            category,
            (size_t)steps,
            (size_t)context->steps,
            (size_t)context->budget,
            budget,
            message ? message : "",
            observed->source,
            observed->line,
            observed->column
        );
    }
    REQUIRE(status == FLOW_OK && state->context.returned);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    /* A compiled run enters bytecode only for a definition past the Wasm
     * JIT's size budget. */
    REQUIRE(
        backend == 0 ? runs.counts.bytecode_entries > 0
                     : runs.counts.bytecode_entries == runs.counts.size_bytecode_fallbacks
    );
    REQUIRE(backends[backend] != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
    REQUIRE(backends[backend] != BEHAVIOR_WASM || runs.counts.wasm_entries > 0);
    REQUIRE(!compiler_fixture_snapshot(detached, out, &state->result));
    return true;
}
int compiler_fixture_snapshot(arena *storage, pin_value *out, const pin_value *value)
{
    if (pin_value_snapshot_to_arena(storage, out, value)) {
        return -1;
    }
    box(storage, out, 0);
    return 0;
}
void compiler_fixture_box(arena *storage, const pin_value *value)
{
    box(storage, value, 0);
}
int compiler_fixture_union(arena *storage, uint64_t tag, const pin_value *values, int count, pin_value *out)
{
    if (pin_union_value(storage, tag, values, count, out)) {
        return -1;
    }
    box(storage, out, 0);
    return 0;
}
bool compiler_fixture_call(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const char *name,
    const pin_value *arguments,
    int count,
    int budget,
    arena *detached,
    pin_value *out
)
{
    call_state state = { 0 };
    bool valid = run(fixture, backend, identity, name, arguments, count, budget, detached, out, &state);
    behavior_function_release_host(&state.context);
    behavior_function_activation_free(&state.activation);
    arena_free(&state.producer);
    return valid;
}
static compiler_fixture prepared_fixtures[2];
static arena prepared_results;
static void preparation_cleanup()
{
    for (int i = 0; i < 2; i++) {
        compiler_fixture_free(&prepared_fixtures[i]);
    }
    arena_free(&prepared_results);
}
static bool preparation_value(const compiler_fixture *fixture, int b, pin_value *out)
{
    return compiler_fixture_call(fixture, b, "project:fixture", "Value", NULL, 0, 100, &prepared_results, out);
}
TEST_CASE(compiler_fixture_prepares_only_selected_backend)
{
    for (int b = 0; b < 2; b++) {
        char source[] = "package fixture\npub Value :: () -> string { return "
                        "\"first\"; }";
        source_package_input file = { "fixture/Fixture.chibi", source, sizeof source - 1 };
        compiler_fixture *first = &prepared_fixtures[0],
                         *second = &prepared_fixtures[1];
        first->observe_failures = true;
        first->collectable = true;
        REQUIRE(compiler_fixture_prepare_backends(first, 1u << b, &file, 1, NULL, 0));
        REQUIRE(first->owners[b] && !first->owners[1 - b]);
        REQUIRE(first->observe_failures && first->collectable);
        memcpy(strstr(source, "first"), "later", 5);
        REQUIRE(compiler_fixture_prepare_backends(second, 1u << b, &file, 1, NULL, 0));
        REQUIRE(second->owners[b] && !second->owners[1 - b]);
        REQUIRE(first->native != second->native && first->owners[b] != second->owners[b]);
        REQUIRE(
            authoring_packages_snapshot(first->owners[b]) !=
            authoring_packages_snapshot(second->owners[b])
        );
        pin_value old_value, new_value;
        REQUIRE(preparation_value(first, b, &old_value));
        REQUIRE(preparation_value(second, b, &new_value));
        compiler_fixture_free(first);
        compiler_fixture_free(second);
        REQUIRE(!first->native && !first->owners[0] && !first->owners[1]);
        REQUIRE(!first->observe_failures && !first->collectable);
        REQUIRE(pin_text_equal(pin_text(old_value), (pin_string){ "first", 5 }));
        REQUIRE(pin_text_equal(pin_text(new_value), (pin_string){ "later", 5 }));
        /* Reuse only the empty fixture shell, with changed source. */
        REQUIRE(compiler_fixture_prepare_backends(first, 1u << b, &file, 1, NULL, 0));
        REQUIRE(first->owners[b] && !first->owners[1 - b]);
        REQUIRE(preparation_value(first, b, &new_value));
        compiler_fixture_free(first);
        compiler_fixture_free(first);
        REQUIRE(pin_text_equal(pin_text(new_value), (pin_string){ "later", 5 }));
        REQUIRE(pin_text_equal(pin_text(old_value), (pin_string){ "first", 5 }));
        arena_free(&prepared_results);
    }
    return true;
}
TEST_CASE(compiler_fixture_default_keeps_both_backends)
{
    const char source[] = "package fixture\npub Value :: () -> string { return "
                          "\"both\"; }";
    source_package_input file = { "fixture/Fixture.chibi", source, sizeof source - 1 };
    compiler_fixture *fixture = &prepared_fixtures[0];
    REQUIRE(compiler_fixture_prepare(fixture, &file, 1));
    REQUIRE(
        fixture->owners[0] && fixture->owners[1] &&
        fixture->owners[0] != fixture->owners[1]
    );
    REQUIRE(
        authoring_packages_snapshot(fixture->owners[0]) !=
        authoring_packages_snapshot(fixture->owners[1])
    );
    pin_value values[2];
    for (int b = 0; b < 2; b++) {
        REQUIRE(preparation_value(fixture, b, &values[b]));
    }
    compiler_fixture_free(fixture);
    for (int b = 0; b < 2; b++) {
        REQUIRE(pin_text_equal(pin_text(values[b]), (pin_string){ "both", 4 }));
    }
    return true;
}
/* Capture deliberate authored failures without changing the enclosing case's
 * pico result or leaking expected FAIL reports into its normal output. */
static const compiler_fixture *captured_fixture;
static bool run_captured_tests()
{
    return compiler_fixture_tests(captured_fixture);
}
static bool fixture_test_report(const compiler_fixture *fixture, bool passed, const char *expected)
{
    FILE *output = tmpfile();
    REQUIRE(output);
    pu_test_context context = { .quiet = true, .output = output, .errors = output };
    captured_fixture = fixture;
    pu_run_test_context(&context, "authored adapter", run_captured_tests);
    captured_fixture = NULL;
    rewind(output);
    char text[2048];
    size_t count = fread(text, 1, sizeof text - 1, output);
    text[count] = 0;
    bool valid = !ferror(output) && feof(output) && !strcmp(text, expected) &&
                 context.passed == (unsigned)passed &&
                 context.failed == (unsigned)!passed;
    if (!valid) {
        fprintf(pu_errors(), "authored adapter report: %s", text);
    }
    fclose(output);
    return valid;
}
TEST_CASE(compiler_fixture_runs_authored_tests_on_selected_backends)
{
    const char source[] = "package fixture\n"
                          "@test First :: () { assert(true, \"first\"); }\n"
                          "@test Second :: () { assert(true, \"second\"); }\n";
    source_package_input file = { "fixture/Tests.chibi", source, sizeof source - 1 };
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    compiler_fixture *fixture = &prepared_fixtures[0];
    char expected[512];
    for (unsigned selected = 1; selected <= COMPILER_FIXTURE_BOTH; selected++) {
        REQUIRE(compiler_fixture_prepare_backends(fixture, selected, &file, 1, core, 1));
        expected[0] = 0;
        for (int b = 0; b < 2; b++) {
            if (!(selected & (1u << b))) {
                continue;
            }
            const char *backend = behavior_backend_name(backends[b]);
            size_t used = strlen(expected);
            snprintf(
                expected + used,
                sizeof expected - used,
                "PASS fixture.First (%s)\nPASS fixture.Second (%s)\n%s: 2 "
                "tests, 0 failed\n",
                backend,
                backend,
                backend
            );
        }
        REQUIRE(fixture_test_report(fixture, true, expected));
        compiler_fixture_free(fixture);
    }
    return true;
}
TEST_CASE(compiler_fixture_rejects_missing_and_failed_authored_tests)
{
    compiler_fixture *fixture = &prepared_fixtures[0];
    REQUIRE(fixture_test_report(fixture, false, "Compiler fixture contains no authored tests\n"));
    const char
        empty[] = "package fixture\npub Value :: () -> int { return 1; }\n";
    source_package_input file = { "fixture/Tests.chibi", empty, sizeof empty - 1 };
    REQUIRE(
        compiler_fixture_prepare_backends(fixture, COMPILER_FIXTURE_BYTECODE, &file, 1, NULL, 0)
    );
    REQUIRE(
        fixture_test_report(fixture, false, "BYTECODE: no authored tests found\nCompiler fixture contains no authored tests\n")
    );
    compiler_fixture_free(fixture);
    const char source[] = "package fixture\n"
                          "@test First :: () { assert(false, \"deliberate "
                          "failure\"); }\n"
                          "@test Second :: () { assert(true); }\n";
    file = (source_package_input){ "fixture/Tests.chibi", source, sizeof source - 1 };
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(fixture, &file, 1, core, 1));
    char expected[1024];
    snprintf(
        expected,
        sizeof expected,
        "FAIL fixture.First (BYTECODE)\n  /compiler/fixture/Tests.chibi:2:21: "
        "Assertion "
        "failed: deliberate failure\n"
        "PASS fixture.Second (BYTECODE)\nBYTECODE: 2 tests, 1 failed\n"
        "FAIL fixture.First (%s)\n  /compiler/fixture/Tests.chibi:2:21: "
        "Assertion "
        "failed: deliberate failure\n"
        "PASS fixture.Second (%s)\n%s: 2 tests, 1 failed\n",
        behavior_backend_name(backends[1]),
        behavior_backend_name(backends[1]),
        behavior_backend_name(backends[1])
    );
    REQUIRE(fixture_test_report(fixture, false, expected));
    return true;
}
TEST_CASE(compiler_fixture_rejects_invalid_authored_worker_settings)
{
    const char *setting = getenv("CHIBI_TEST_WORKERS");
    char *saved = setting ? xstrdup(setting) : NULL;
    const char *invalid[] = { "",   "0",  "33", "-1",
                              "+2", " 2", "2x", "9999999999999999999999999999" };
    bool valid = true;
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        valid &= !setenv("CHIBI_TEST_WORKERS", invalid[i], 1);
        valid &= fixture_test_report(&prepared_fixtures[0], false, "CHIBI_TEST_WORKERS must be an integer from 1 to 32\n");
    }
    valid &= saved ? !setenv("CHIBI_TEST_WORKERS", saved, 1)
                   : !unsetenv("CHIBI_TEST_WORKERS");
    free(saved);
    REQUIRE(valid);
    return true;
}
void test_language_compiler_fixture()
{
    pu_setup(NULL, preparation_cleanup);
    RUN_TEST_CASE(compiler_fixture_prepares_only_selected_backend);
    RUN_TEST_CASE(compiler_fixture_default_keeps_both_backends);
    RUN_TEST_CASE(compiler_fixture_runs_authored_tests_on_selected_backends);
    RUN_TEST_CASE(compiler_fixture_rejects_missing_and_failed_authored_tests);
    RUN_TEST_CASE(compiler_fixture_rejects_invalid_authored_worker_settings);
    pu_clear_setup();
}
bool compiler_fixture_nodes(TSNode root, compiler_fixture_node *nodes, int capacity, int depth_cap, int *count)
{
    REQUIRE(capacity > 0 && depth_cap >= 0 && nodes && count && !ts_node_is_null(root));
    REQUIRE(ts_node_descendant_count(root) <= (uint32_t)capacity);
    *count = 0;
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    int parent = -1, previous = -1, depth = 0;
    for (;;) {
        if (depth > depth_cap || *count >= capacity) {
            ts_tree_cursor_delete(&cursor);
            REQUIRE(false);
        }
        int current = (*count)++;
        nodes[current] = (compiler_fixture_node){
            ts_tree_cursor_current_node(&cursor),
            ts_tree_cursor_current_field_name(&cursor),
            parent,
            -1,
            -1
        };
        if (parent >= 0) {
            if (previous < 0) {
                nodes[parent].first_child = current;
            } else {
                nodes[previous].next_sibling = current;
            }
        }
        if (ts_tree_cursor_goto_first_child(&cursor)) {
            parent = current;
            previous = -1;
            depth++;
            continue;
        }
        for (;;) {
            if (ts_tree_cursor_goto_next_sibling(&cursor)) {
                previous = current;
                break;
            }
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                ts_tree_cursor_delete(&cursor);
                return true;
            }
            current = parent;
            parent = nodes[current].parent;
            depth--;
        }
    }
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static pin_value text(arena *storage, const char *value)
{
    pin_value result;
    const char *error = pin_value_text(storage, &result, value, strlen(value));
    if (error) {
        abort();
    }
    return result;
}
static bool put(arena *storage, pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(storage, row->ptr, field, value));
    return true;
}
bool compiler_fixture_pack_nodes(
    const compiler_fixture *fixture,
    int backend,
    const compiler_fixture_node *nodes,
    int count,
    arena *input,
    pin_value *out,
    pin_value *fields
)
{
    const pin_record *record = compiler_fixture_record(fixture, backend, "project:syntax_diagnostics", "Node", input);
    REQUIRE(record && record->type.field_count == 11);
    pin_array
        *array = pin_array_snapshot_allocate(input, PIN_STRUCT, record->type.name, count);
    pin_array *names = fields ? pin_array_snapshot_allocate(input, PIN_STR, NULL, count)
                              : NULL;
    REQUIRE(array && (!fields || names));
    array->record = record;
    for (int i = 0; i < count; i++) {
        TSNode node = nodes[i].node;
        TSPoint point = ts_node_start_point(node);
        pin_value row = { .type = PIN_STRUCT,
                          .ptr = arena_alloc(input, record->type.size),
                          .record = record,
                          .type_name = record->type.name };
        memset(row.ptr, 0, record->type.size);
        REQUIRE(put(input, &row, "kind", text(input, ts_node_type(node))));
        REQUIRE(
            put(input, &row, "start", integer((int)ts_node_start_byte(node))) &&
            put(input, &row, "end", integer((int)ts_node_end_byte(node)))
        );
        REQUIRE(
            put(input, &row, "row", integer((int)point.row)) &&
            put(input, &row, "column", integer((int)point.column))
        );
        REQUIRE(
            put(input, &row, "parent", integer(nodes[i].parent)) &&
            put(input, &row, "first_child", integer(nodes[i].first_child)) &&
            put(input, &row, "next_sibling", integer(nodes[i].next_sibling))
        );
        REQUIRE(
            put(input, &row, "error", boolean(ts_node_is_error(node))) &&
            put(input, &row, "missing", boolean(ts_node_is_missing(node))) &&
            put(input, &row, "has_error", boolean(ts_node_has_error(node)))
        );
        array->items[i] = row;
        if (names) {
            names->items[i] = text(input, nodes[i].field ? nodes[i].field : "");
        }
    }
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array,
                        .record = record,
                        .type_name = record->type.name };
    if (fields) {
        *fields = (pin_value){ .type = PIN_STR,
                               .container = PIN_CONTAINER_ARRAY,
                               .ptr = names };
    }
    return true;
}
/* A zeroed row of `identity`'s record `name`. */
static pin_value record_row(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const char *name,
    arena *input
)
{
    const pin_record *shape = compiler_fixture_record(fixture, backend, identity, name, input);
    assert(shape);
    pin_value row = { .type = PIN_STRUCT,
                      .ptr = arena_alloc(input, shape->type.size),
                      .record = shape,
                      .type_name = shape->type.name };
    memset(row.ptr, 0, shape->type.size);
    return row;
}
static pin_value rows(arena *input, const pin_value *items, int count, pin_type type)
{
    const pin_record *shape = count && type == PIN_STRUCT ? items[0].record : NULL;
    pin_array *
        array = pin_array_snapshot_allocate(input, type, shape ? shape->type.name : NULL, count);
    array->record = shape;
    for (int i = 0; i < count; i++) {
        array->items[i] = items[i];
    }
    return (pin_value){ .type = type,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array,
                        .record = shape,
                        .type_name = shape ? shape->type.name : NULL };
}
/* A port's storage as Compile takes it: a generic port's type is the one its
 * call infers (0), a wildcard takes each argument's own type (-2), and -1 is
 * a type the compiler cannot pass. */
static int port_storage(script_type type)
{
    unsigned inferred = SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_GENERIC;
    if (type.flags & SCRIPT_TYPE_UNTYPED) {
        if (type.flags & ~inferred) {
            return -1;
        }
        return type.flags & SCRIPT_TYPE_GENERIC ? 0 : -2;
    }
    return type.flags || type.nominal || type.storage == PIN_FLOW ? -1
                                                                  : (int)type.storage;
}
bool compiler_fixture_natives(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const script_catalog *catalog,
    arena *input,
    pin_value out[3],
    pin_value *effects
)
{
    pin_value *functions = NULL, *ports = NULL, *identities = NULL, *masks = NULL;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_FUNCTION ||
            c->flow.kind != SCRIPT_FLOW_ACCESS_NONE || c->outcome_count ||
            (c->effects & SCRIPT_EFFECT_SUSPEND)) {
            continue;
        }
        pin_value function = record_row(fixture, backend, identity, "Native", input);
        REQUIRE(put(input, &function, "name", text(input, c->name)));
        REQUIRE(put(input, &function, "first", integer(acount(ports))));
        REQUIRE(put(input, &function, "parameters", integer(c->parameter_count)));
        REQUIRE(put(input, &function, "results", integer(c->result_count)));
        REQUIRE(put(input, &function, "variadic", boolean(c->variadic || c->variadic_optional)));
        REQUIRE(put(input, &function, "effects", integer((int)c->effects)));
        apush(functions, function);
        apush(masks, integer((int)c->effects));
        for (int j = 0; j < c->parameter_count + c->result_count; j++) {
            const script_catalog_parameter *declared = j < c->parameter_count
                                                         ? &c->parameters[j]
                                                         : &c->results[j - c->parameter_count];
            pin_value port = record_row(fixture, backend, identity, "Port", input);
            REQUIRE(
                put(input, &port, "name", text(input, declared->name ? declared->name : ""))
            );
            REQUIRE(put(input, &port, "storage", integer(port_storage(declared->type))));
            REQUIRE(put(input, &port, "container", integer((int)declared->type.container)));
            REQUIRE(put(input, &port, "c_string", boolean(declared->c_string)));
            apush(ports, port);
        }
        apush(identities, ((pin_value){ .type = PIN_U64, .u64 = c->id }));
        apush(
            identities,
            ((pin_value){ .type = PIN_U64,
                          .u64 = script_catalog_capability_abi(catalog, c) })
        );
    }
    out[0] = rows(input, functions, acount(functions), PIN_STRUCT);
    out[1] = rows(input, ports, acount(ports), PIN_STRUCT);
    out[2] = rows(input, identities, acount(identities), PIN_U64);
    *effects = rows(input, masks, acount(masks), PIN_I32);
    afree(functions);
    afree(ports);
    afree(identities);
    afree(masks);
    return true;
}
