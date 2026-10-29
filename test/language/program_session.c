#include "authoring/native_bindings.h"
#include "authoring/program.h"
#include "authoring/program_service.h"
#include "backend.h"
#include "execution/status.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static native_capabilities *native;
static authoring_program *sessions[2];
static program_service *services[2];
static char temporary[1024], source[1200], core[1200], library[1300];
static char *previous_workers;
static bool workers_changed;
static task_system test_tasks;
static int submitted, finished;

typedef struct
{
    authoring_program *target;
    bool compile;
} session_bridge;
static source_package_snapshot *snapshot(const char *, const char *);

static const char *nested_call(
    const behavior_function_context *context,
    void *user,
    const pin_value *,
    pin_value *results
)
{
    session_bridge *bridge = user;
    if (bridge->compile) {
        bool accepted = authoring_program_compile(
            bridge->target,
            snapshot("package app\nMain :: () {}\n", NULL)
        );
        results[0].i = accepted ? FLOW_OK
                                : authoring_program_error(bridge->target)->status;
        return NULL;
    }
    int value = 0;
    const authoring_program_call_options options = { .caller = context->program };
    int status = authoring_program_call(bridge->target, &options, &value);
    results[0].i = status == FLOW_OK ? value : status;
    return NULL;
}

static const char *argument_count(
    const behavior_function_context *context,
    void *user,
    const pin_value *,
    pin_value *results
)
{
    const pin_array *arguments = program_service_arguments(user, context->program);
    results[0].i = arguments ? arguments->count : -1;
    return NULL;
}

static const native_capability_pack *bridge_pack()
{
    static const script_catalog_parameter result = { .name = "value",
                                                     .type = { .storage = PIN_I32 } };
    static const script_catalog_capability capabilities[] = {
        { .id = 0x78374512,
          .name = "test.nested",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = &result,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_HOST },
        { .id = 0x78374513,
          .name = "test.argument_count",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = &result,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_HOST }
    };
    static const script_catalog catalog = { .capabilities = capabilities,
                                            .capability_count = 2 };
    static const node_host_call call = { .service = "test.session",
                                         .context = nested_call };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    static const node_host_call arguments = { .service = PROGRAM_SERVICE,
                                              .context = argument_count };
    static const native_capability_binding bindings[] = {
        { .id = 0x78374512, .call = &call, .access = &access },
        { .id = 0x78374513, .call = &arguments, .access = &access }
    };
    static const native_capability_pack pack = { .catalog = &catalog,
                                                 .bindings = bindings,
                                                 .binding_count = 2 };
    return &pack;
}

static void cleanup()
{
    if (workers_changed) {
        if (previous_workers) {
            setenv("CHIBI_TEST_WORKERS", previous_workers, 1);
        } else {
#ifdef _WIN32
            setenv("CHIBI_TEST_WORKERS", "", 1);
#else
            unsetenv("CHIBI_TEST_WORKERS");
#endif
        }
        free(previous_workers);
        previous_workers = NULL;
        workers_changed = false;
    }
    test_tasks = (task_system){ 0 };
    submitted = finished = 0;
    for (int i = 0; i < 2; i++) {
        authoring_program_free(sessions[i]);
        sessions[i] = NULL;
        program_service_free(services[i]);
        services[i] = NULL;
    }
    native_capabilities_free(native);
    native = NULL;
    if (temporary[0]) {
        unlink(source);
        unlink(library);
        rmdir(core);
        rmdir(temporary);
        temporary[0] = source[0] = core[0] = library[0] = 0;
    }
}

static bool catalog()
{
    const native_definition_pack definitions[] = { CORE_DEFINITION_PACK,
                                                   NATIVE_TEXT_DEFINITION_PACK,
                                                   NATIVE_ARRAY_DEFINITION_PACK };
    const native_capability_pack packs[] = { *program_service_pack(), *bridge_pack() };
    char error[256];
    native = native_capabilities_from_definition_packs(packs, 2, definitions, 3, error, sizeof error);
    if (!native) {
        fprintf(stderr, "%s\n", error);
    }
    REQUIRE(native);
    return true;
}

static authoring_program_options options(behavior_backend backend)
{
    return (authoring_program_options){ .native = native,
                                        .bindings = authoring_native_bindings(native),
                                        .backend = backend };
}

static source_package_snapshot *snapshot(const char *main, const char *dependency)
{
    source_package_input files[] = {
        { "Main.chibi", main, strlen(main) },
        { "counter/Counter.chibi", dependency, dependency ? strlen(dependency) : 0 }
    };
    source_package_snapshot_options inputs = {
        .project_root = "/program-session",
        .files = files,
        .file_count = dependency ? 2 : 1
    };
    return source_package_snapshot_new(&inputs);
}

static bool accept(authoring_program *session, const char *main, const char *dependency, bool restart)
{
    REQUIRE(authoring_program_compile(session, snapshot(main, dependency)));
    REQUIRE(authoring_program_prepare(session, "Main", restart));
    REQUIRE(authoring_program_commit(session));
    return true;
}

static source_package_snapshot *collection_snapshot(bool invalid)
{
    const char *unused = "package unused\nUnused :: () -> int { return "
                         "unused_error }\n";
    const char *
        used = invalid ? "package used\npub Value :: () -> int { return "
                         "needed_error }\n"
                       : "package used\npub Value :: () -> int { return 37 }\n";
    const char *main = "package app\nimport used \"core:used\"\nMain :: () -> "
                       "int { return used.Value() }\n";
    const source_package_input files[] = {
        { "/aaa-core/unused/Unused.chibi", unused, strlen(unused) },
        { "/aaa-core/used/Value.chibi", used, strlen(used) },
        { "/program-session/Main.chibi", main, strlen(main) }
    };
    const source_package_collection collection = { "core", "/aaa-core" };
    const source_package_snapshot_options inputs = {
        .project_root = "/program-session",
        .files = files,
        .file_count = 3,
        .collections = &collection,
        .collection_count = 1
    };
    return source_package_snapshot_new(&inputs);
}

TEST_CASE(session_collection_reachability_preserves_entry_and_source_diagnostics)
{
    REQUIRE(catalog());
    authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
    sessions[0] = authoring_program_new(&configuration);
    REQUIRE(authoring_program_compile(sessions[0], collection_snapshot(false)));
    REQUIRE(authoring_program_prepare(sessions[0], "Main", false));
    REQUIRE(authoring_program_commit(sessions[0]));
    int result = 0;
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == 37);
    REQUIRE(!authoring_program_compile(sessions[0], collection_snapshot(true)));
    const authoring_program_diagnostic *error = authoring_program_error(sessions[0]);
    REQUIRE(error->path && !strcmp(error->path, "/aaa-core/used/Value.chibi"));
    REQUIRE(error->line == 2 && strstr(error->message, "needed_error"));
    authoring_program_discard(sessions[0]);
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == 37);
    return true;
}

TEST_CASE(session_candidates_preserve_package_and_import_state)
{
    REQUIRE(catalog());
    const char *main = "package app\nimport counter \"project:counter\"\nMain "
                       ":: () -> int { count: retain int = 0; count += 1; "
                       "return count * 100 + counter.Next() }\n";
    const char *before = "package counter\npub Next :: () -> int { count: "
                         "retain int = 0; count += 1; return count }\n";
    const char *after = "package counter\npub Next :: () -> int { count: "
                        "retain f64 = 50; count += 2; return int(count) }\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        authoring_program_options configuration = options(backends[i]);
        sessions[i] = authoring_program_new(&configuration);
        REQUIRE(accept(sessions[i], main, before, false));
        int result = 0;
        REQUIRE(authoring_program_call(sessions[i], NULL, &result) == FLOW_OK && result == 101);
        REQUIRE(authoring_program_compile(sessions[i], snapshot(main, after)));
        REQUIRE(authoring_program_prepare(sessions[i], "Main", false));
        REQUIRE(authoring_program_call(sessions[i], NULL, &result) == FLOW_ERR_STATE);
        authoring_program_discard(sessions[i]);
        REQUIRE(authoring_program_call(sessions[i], NULL, &result) == FLOW_OK && result == 202);
        REQUIRE(accept(sessions[i], main, after, false));
        REQUIRE(authoring_program_call(sessions[i], NULL, &result) == FLOW_OK && result == 304);
        REQUIRE(accept(sessions[i], main, after, true));
        REQUIRE(authoring_program_call(sessions[i], NULL, &result) == FLOW_OK && result == 152);
        const authoring_program_call_options same = {
            .caller = authoring_program_runtime(sessions[i])
        };
        REQUIRE(authoring_program_call(sessions[i], &same, &result) == FLOW_ERR_STATE);
    }
    return true;
}

TEST_CASE(session_rejected_source_and_entry_keep_the_accepted_runtime)
{
    REQUIRE(catalog());
    authoring_program_options configuration = options(BEHAVIOR_BYTECODE);
    sessions[0] = authoring_program_new(&configuration);
    REQUIRE(accept(sessions[0], "package app\nMain :: () -> int { return -37 }\n", NULL, false));
    REQUIRE(
        !authoring_program_compile(sessions[0], snapshot("package app\nMain :: () { missing() }\n", NULL))
    );
    const authoring_program_diagnostic *error = authoring_program_error(sessions[0]);
    REQUIRE(
        error->status == FLOW_ERR_COMPILE && error->line == 2 && error->path &&
        strstr(error->message, "missing")
    );
    authoring_program_discard(sessions[0]);
    REQUIRE(strstr(authoring_program_error(sessions[0])->message, "missing"));
    REQUIRE(authoring_program_compile(
        sessions[0],
        snapshot("package app\nMain :: (value: int) -> int { return value }\n", NULL)
    ));
    REQUIRE(!authoring_program_prepare(sessions[0], "Main", false));
    authoring_program_discard(sessions[0]);
    int result = 0;
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == -37);
    REQUIRE(!authoring_program_error(sessions[0])->status);
    return true;
}

static bool cancelled(void *user)
{
    return *(bool *)user;
}

TEST_CASE(session_calls_observe_host_cancellation_and_recover)
{
    REQUIRE(catalog());
    bool stop = true;
    authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
    configuration.cancelled = cancelled;
    configuration.cancel_user = &stop;
    sessions[0] = authoring_program_new(&configuration);
    REQUIRE(accept(sessions[0], "package app\nMain :: () -> int { return 7 }\n", NULL, false));
    int result = 0;
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) < 0);
    REQUIRE(strstr(authoring_program_error(sessions[0])->message, "cancel"));
    const authoring_program_call_options cooperative = { .cooperative = true };
    REQUIRE(authoring_program_call(sessions[0], &cooperative, &result) == FLOW_OK && result == 7);
    REQUIRE(!authoring_program_error(sessions[0])->status);
    stop = false;
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == 7);
    return true;
}

TEST_CASE(session_cooperative_host_preserves_nested_cancellation)
{
    REQUIRE(catalog());
    bool stop = true;
    session_bridge bridge = { 0 };
    service_binding *bindings = NULL;
    apush(bindings, ((service_binding){ "test.session", &bridge }));
    authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
    configuration.services = bindings;
    configuration.cancelled = cancelled;
    configuration.cancel_user = &stop;
    sessions[0] = authoring_program_new(&configuration);
    sessions[1] = authoring_program_new(&configuration);
    afree(bindings);
    REQUIRE(
        accept(sessions[0], "package app\nMain :: () -> int { return test.nested() }\n", NULL, false)
    );
    REQUIRE(accept(sessions[1], "package app\nMain :: () -> int { return 7 }\n", NULL, false));
    bridge.target = sessions[1];
    const authoring_program_call_options cooperative = { .cooperative = true };
    int result = 0;
    REQUIRE(authoring_program_call(sessions[0], &cooperative, &result) == FLOW_OK && result < 0);
    REQUIRE(strstr(authoring_program_error(sessions[1])->message, "cancel"));
    REQUIRE(!authoring_program_error(sessions[0])->status);
    stop = false;
    REQUIRE(authoring_program_call(sessions[0], &cooperative, &result) == FLOW_OK && result == 7);
    return true;
}

TEST_CASE(session_nested_calls_use_separate_state_and_reject_active_mutations)
{
    REQUIRE(catalog());
    session_bridge bridge = { 0 };
    service_binding *bindings = NULL;
    apush(bindings, ((service_binding){ "test.session", &bridge }));
    authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
    configuration.services = bindings;
    sessions[0] = authoring_program_new(&configuration);
    sessions[1] = authoring_program_new(&configuration);
    afree(bindings);
    REQUIRE(
        accept(sessions[0], "package app\nMain :: () -> int { return test.nested() }\n", NULL, false)
    );
    REQUIRE(
        accept(sessions[1], "package app\nMain :: () -> int { count: retain int = 5; count += 1; return count }\n", NULL, false)
    );
    bridge.target = sessions[1];
    int result = 0;
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == 6);
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == 7);
    bridge.target = sessions[0];
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == FLOW_ERR_STATE);
    bridge.compile = true;
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == FLOW_ERR_STATE);
    bridge.compile = false;
    bridge.target = sessions[1];
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == 8);
    return true;
}

static void write_source(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    size_t size = strlen(text);
    size_t written = fwrite(text, 1, size, file);
    int status = fclose(file);
    assert(written == size && !status);
    (void)written;
    (void)status;
}

static bool disk()
{
    const char *base = getenv("TMPDIR");
    if (!base) {
        base = getenv("TEMP");
    }
    snprintf(temporary, sizeof temporary, "%s/chibi-program-XXXXXX", base ? base : "/tmp");
    REQUIRE(mkdtemp(temporary));
    snprintf(source, sizeof source, "%s/Main.chibi", temporary);
    snprintf(core, sizeof core, "%s/core", temporary);
    snprintf(library, sizeof library, "%s/Base.chibi", core);
    REQUIRE(!mkdir(core, 0700));
    write_source(library, "package base\n");
    REQUIRE(catalog());
    for (int i = 0; i < 2; i++) {
        services[i] = program_service_new(core);
        service_binding *bindings = NULL;
        apush(bindings, ((service_binding){ PROGRAM_SERVICE, services[i] }));
        authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
        configuration.services = bindings;
        configuration.test_tasks = test_tasks.workers ? &test_tasks : NULL;
        program_service_configure(services[i], &configuration);
        afree(bindings);
    }
    return true;
}

TEST_CASE(program_handles_suppress_rejections_and_never_alias_other_sessions)
{
    REQUIRE(disk());
    write_source(source, "package app\nMain :: () -> int { count: retain int = 0; count += 1; return count }\n");
    uint64_t first = program_service_open(services[0], source, "Main", NULL, 0);
    uint64_t second = program_service_open(services[1], source, "Main", NULL, 0);
    REQUIRE(first && second && first != second);
    REQUIRE(program_service_update(services[0], first) == 1);
    REQUIRE(program_service_update(services[1], second) == 1);
    REQUIRE(program_service_update(services[0], first) == 0);
    int result = 0;
    REQUIRE(program_service_call(services[0], first, NULL, &result) == FLOW_OK && result == 1);
    write_source(source, "package app\nMain :: () { absent() }\n");
    REQUIRE(program_service_update(services[0], first) == -1);
    REQUIRE(strstr(program_service_diagnostic(services[0], first), "absent"));
    REQUIRE(program_service_update(services[0], first) == 0);
    REQUIRE(program_service_call(services[0], first, NULL, &result) == FLOW_OK && result == 2);
    REQUIRE(program_service_call(services[1], second, NULL, &result) == FLOW_OK && result == 1);
    REQUIRE(!program_service_close(services[0], second));
    REQUIRE(program_service_close(services[0], first));
    uint64_t replacement = program_service_open(services[0], source, "Main", NULL, 0);
    REQUIRE(replacement != first && replacement != second);
    REQUIRE(program_service_call(services[0], first, NULL, &result) == FLOW_ERR_ARGUMENT);
    REQUIRE(program_service_update(services[0], first) == -1);
    return true;
}

static void *enqueue_tests(task_range *run, int count, int, void *context, void *)
{
    submitted++;
    assert(count == 2);
    run(0, 1, 0, context);
    run(1, 2, 1, context);
    return NULL;
}

static void finish_tests(void *task, void *)
{
    assert(!task);
    finished++;
}

TEST_CASE(program_tests_bind_the_public_catalog_and_copy_transient_diagnostics)
{
    test_tasks = (task_system){ .workers = 2, .enqueue = enqueue_tests, .finish = finish_tests };
    const char *saved = getenv("CHIBI_TEST_WORKERS");
    previous_workers = saved ? strdup(saved) : NULL;
    workers_changed = true;
    REQUIRE(!setenv("CHIBI_TEST_WORKERS", "1", 1));
    REQUIRE(disk());
    write_source(library, "package base\nUnused :: () { unused_core_call() }\n");
    write_source(
        source,
        "package app\n"
        "@test\nFirst :: () { assert(1 + 2 == 3) }\n"
        "@test\nSecond :: () { assert(4 * 5 == 20) }\n"
        "@test\nPublic :: () { assert(test.argument_count() == 1); arguments: "
        "string[] = []; handle := "
        "program.open(\"chibi-program-absent-93fcde\", \"Main\", arguments); "
        "assert(program.update(handle) == -1); before := "
        "program.diagnostic(handle); assert(program.update(handle) == 0); "
        "other := program.open(\"chibi-program-absent-84cafe\", \"Main\", "
        "arguments); assert(program.diagnostic(other) == \"\"); assert(before "
        "!= \"\"); called := program.call(handle); assert(called.status != 0); "
        "program.close(other); program.close(handle) }\n"
    );
    REQUIRE(program_service_test(services[0], source) == 0);
    REQUIRE(!submitted && !finished);
    REQUIRE(!setenv("CHIBI_TEST_WORKERS", "2", 1));
    REQUIRE(program_service_test(services[0], source) == 0);
    REQUIRE(submitted == 1 && finished == 1);
    return true;
}

void test_language_program_session()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(session_collection_reachability_preserves_entry_and_source_diagnostics);
    RUN_TEST_CASE(session_candidates_preserve_package_and_import_state);
    RUN_TEST_CASE(session_rejected_source_and_entry_keep_the_accepted_runtime);
    RUN_TEST_CASE(session_calls_observe_host_cancellation_and_recover);
    RUN_TEST_CASE(session_cooperative_host_preserves_nested_cancellation);
    RUN_TEST_CASE(session_nested_calls_use_separate_state_and_reject_active_mutations);
    RUN_TEST_CASE(program_handles_suppress_rejections_and_never_alias_other_sessions);
    RUN_TEST_CASE(program_tests_bind_the_public_catalog_and_copy_transient_diagnostics);
    pu_clear_setup();
}
