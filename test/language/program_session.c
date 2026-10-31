#include "authoring/asset_compiler_host.h"
#include "authoring/package_compile.h"
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
static char temporary[1024], source[1200], sibling[1200], core[1200], library[1300];
static char *previous_workers;
static bool workers_changed;
static task_system test_tasks;

typedef struct
{
    authoring_program *target;
    bool compile;
} session_bridge;
static const source_package_snapshot_options *snapshot(const char *, const char *);

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
        unlink(sibling);
        unlink(library);
        rmdir(core);
        rmdir(temporary);
        temporary[0] = source[0] = sibling[0] = core[0] = library[0] = 0;
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
    return (authoring_program_options){ .native = native, .backend = backend };
}

/* The inputs of one compile, valid until the next call. */
static const source_package_snapshot_options *snapshot(const char *main, const char *dependency)
{
    static source_package_input files[2];
    static source_package_snapshot_options inputs;
    files[0] = (source_package_input){ "/program-session/Main.chibi", main, strlen(main) };
    files[1] = (source_package_input){ "/program-session/counter/Counter.chibi",
                                       dependency,
                                       dependency ? strlen(dependency) : 0 };
    inputs = (source_package_snapshot_options){
        .project_root = "/program-session",
        .files = files,
        .file_count = dependency ? 2 : 1
    };
    return &inputs;
}

static bool accept(authoring_program *session, const char *main, const char *dependency, bool restart)
{
    REQUIRE(authoring_program_compile(session, snapshot(main, dependency)));
    REQUIRE(authoring_program_prepare(session, "Main", restart));
    REQUIRE(authoring_program_commit(session));
    return true;
}

static const source_package_snapshot_options *collection_snapshot(bool invalid)
{
    const char *unused = "package unused\nUnused :: () -> int { return "
                         "unused_error }\n";
    const char *
        used = invalid ? "package used\npub Value :: () -> int { return "
                         "needed_error }\n"
                       : "package used\npub Value :: () -> int { return 37 }\n";
    const char *main = "package app\nimport used \"core:used\"\nMain :: () -> "
                       "int { return used.Value() }\n";
    static source_package_input files[3];
    const source_package_input listed[] = {
        { "/aaa-core/unused/Unused.chibi", unused, strlen(unused) },
        { "/aaa-core/used/Value.chibi", used, strlen(used) },
        { "/program-session/Main.chibi", main, strlen(main) }
    };
    static const source_package_collection collection = { "core", "/aaa-core" };
    static source_package_snapshot_options inputs;
    memcpy(files, listed, sizeof files);
    inputs = (source_package_snapshot_options){
        .project_root = "/program-session",
        .files = files,
        .file_count = 3,
        .collections = &collection,
        .collection_count = 1
    };
    return &inputs;
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

/* A relative import names a package below the importing package's directory,
 * in the project or in a collection, and the compile reaches it as it reaches
 * a `COLLECTION:PATH` import. */
TEST_CASE(session_reaches_relative_imports_in_the_project_and_collections)
{
    REQUIRE(catalog());
    const char *outer = "package outer\nimport inner \"../inner\"\npub Value "
                        ":: () -> int { return inner.Value() + 1 "
                        "}\n";
    const char *inner = "package inner\npub Value :: () -> int { return 40 }\n";
    const char *main = "package app\nimport outer \"core:outer\"\nimport near "
                       "\"near\"\nMain :: () -> int { "
                       "return outer.Value() + near.Value() }\n";
    const char *near = "package near\npub Value :: () -> int { return 1 }\n";
    const source_package_input files[] = {
        { "/aaa-core/inner/Inner.chibi", inner, strlen(inner) },
        { "/aaa-core/outer/Outer.chibi", outer, strlen(outer) },
        { "/program-session/Main.chibi", main, strlen(main) },
        { "/program-session/near/Near.chibi", near, strlen(near) }
    };
    const source_package_collection collection = { "core", "/aaa-core" };
    const source_package_snapshot_options inputs = {
        .project_root = "/program-session",
        .files = files,
        .file_count = 4,
        .collections = &collection,
        .collection_count = 1
    };
    authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
    sessions[0] = authoring_program_new(&configuration);
    REQUIRE(authoring_program_compile(sessions[0], &inputs));
    REQUIRE(authoring_program_prepare(sessions[0], "Main", false));
    REQUIRE(authoring_program_commit(sessions[0]));
    int result = 0;
    REQUIRE(authoring_program_call(sessions[0], NULL, &result) == FLOW_OK && result == 42);
    return true;
}

TEST_CASE(session_patches_frame_calls_by_name_without_restarting_state)
{
    REQUIRE(catalog());
    const char
        *before = "package app\nFrame :: () -> int { count: retain int = 0; "
                  "count += 1; return count }\n";
    const char
        *after = "package app\nFrame :: () -> int { count: retain f64 = 0; "
                 "count += 10; return int(count) }\nMain :: () -> int { return "
                 "7 }\n";
    const char *broken = "package app\nFrame :: () -> int { return missing }\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        authoring_program_options configuration = options(backends[i]);
        sessions[i] = authoring_program_new(&configuration);
        /* An empty entry requires no runnable function: Frame-only programs load. */
        REQUIRE(authoring_program_compile(sessions[i], snapshot(before, NULL)));
        REQUIRE(authoring_program_prepare(sessions[i], "", false));
        REQUIRE(authoring_program_commit(sessions[i]));
        int result = 0;
        REQUIRE(
            authoring_program_has(sessions[i], "Frame") &&
            !authoring_program_has(sessions[i], "Main")
        );
        REQUIRE(authoring_program_call_entry(sessions[i], "Main", NULL, &result) == FLOW_ERR_ARGUMENT);
        REQUIRE(strstr(authoring_program_error(sessions[i])->message, "No runnable Main"));
        REQUIRE(
            authoring_program_call_entry(sessions[i], "Frame", NULL, &result) == FLOW_OK &&
            result == 1
        );
        REQUIRE(
            authoring_program_call_entry(sessions[i], "Frame", NULL, &result) == FLOW_OK &&
            result == 2
        );
        /* A broken edit keeps the running code and its state. */
        REQUIRE(!authoring_program_compile(sessions[i], snapshot(broken, NULL)));
        REQUIRE(
            authoring_program_call_entry(sessions[i], "Frame", NULL, &result) == FLOW_OK &&
            result == 3
        );
        /* A good edit patches in place: the retained count converts and continues,
         * and the new function is callable without calling Main again. */
        REQUIRE(authoring_program_compile(sessions[i], snapshot(after, NULL)));
        REQUIRE(authoring_program_prepare(sessions[i], "", false));
        REQUIRE(authoring_program_commit(sessions[i]));
        REQUIRE(authoring_program_has(sessions[i], "Main"));
        REQUIRE(
            authoring_program_call_entry(sessions[i], "Frame", NULL, &result) == FLOW_OK &&
            result == 13
        );
        REQUIRE(
            authoring_program_call_entry(sessions[i], "Main", NULL, &result) == FLOW_OK &&
            result == 7
        );
    }
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

/* Finite bodies make both the broken and corrected paths finish; instruction
 * counts, rather than elapsed time, are the oracle. */
static const char
    *budgeted_loop = "package app\nMain :: () -> int {\n count := 0\n while "
                     "count < 1000 {\n count += 1\n }\n return count\n}\n";

TEST_CASE(session_calls_debit_the_caller_budget_and_keep_its_vm_policy)
{
    REQUIRE(catalog());
    for (int compiled = 0; compiled < 2; compiled++) {
        authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
        sessions[compiled] = authoring_program_new(&configuration);
        REQUIRE(accept(sessions[compiled], budgeted_loop, NULL, false));
        behavior_execution_scope caller = { .remaining = 32,
                                            .prefer_compiled = compiled != 0 };
        behavior_run_scope runs = { 0 };
        behavior_run_scope_begin(&runs);
        behavior_execution_scope_begin(&caller);
        int result = 0;
        int status = authoring_program_call(sessions[compiled], NULL, &result);
        behavior_execution_scope_end(&caller);
        behavior_run_scope_end(&runs);
        REQUIRE(status == FLOW_ERR_STEP_BUDGET && !caller.remaining);
        REQUIRE(
            strstr(authoring_program_error(sessions[compiled])->message, "shared instruction budget exhausted")
        );
        REQUIRE(!behavior_execution_scope_active());
        if (!compiled || !behavior_backend_available(BEHAVIOR_SLJIT)) {
            REQUIRE(runs.counts.bytecode_entries && !runs.counts.sljit_entries);
        } else {
            REQUIRE(runs.counts.sljit_entries);
        }
        REQUIRE(
            authoring_program_call(sessions[compiled], NULL, &result) == FLOW_OK &&
            result == 1000
        );
    }
    return true;
}

TEST_CASE(session_repeated_calls_cannot_reset_the_enclosing_budget)
{
    REQUIRE(catalog());
    authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
    sessions[0] = authoring_program_new(&configuration);
    REQUIRE(
        accept(sessions[0], "package app\nMain :: () -> int { count: retain int = 0; count += 1; return count }\n", NULL, false)
    );
    behavior_execution_scope caller = { .remaining = 256, .prefer_compiled = true };
    behavior_execution_scope_begin(&caller);
    int result = 0, status = FLOW_OK, completed = 0;
    bool charged = true;
    while (completed < 64) {
        uint64_t before = caller.remaining;
        status = authoring_program_call(sessions[0], NULL, &result);
        if (status != FLOW_OK) {
            break;
        }
        charged &= result == completed + 1 && caller.remaining < before;
        completed++;
    }
    behavior_execution_scope_end(&caller);
    if (!(completed > 0 && completed < 64 && charged)) {
        fprintf(
            stderr,
            "repeated calls completed=%d status=%d result=%d remaining=%llu "
            "charged=%d\n",
            completed,
            status,
            result,
            (unsigned long long)caller.remaining,
            charged
        );
    }
    REQUIRE(completed > 0 && completed < 64 && charged);
    REQUIRE(status == FLOW_ERR_STEP_BUDGET && !caller.remaining);
    REQUIRE(
        strstr(authoring_program_error(sessions[0])->message, "shared instruction budget exhausted")
    );
    return true;
}

static bool caller_cancelled(void *user)
{
    const behavior_execution_scope *caller = user;
    return caller->remaining <= 16;
}

TEST_CASE(session_calls_honor_the_callers_live_cancellation_budget)
{
    REQUIRE(catalog());
    authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
    sessions[0] = authoring_program_new(&configuration);
    REQUIRE(accept(sessions[0], budgeted_loop, NULL, false));
    behavior_execution_scope caller = { .remaining = 32,
                                        .prefer_compiled = true,
                                        .cancelled = caller_cancelled };
    caller.user = &caller;
    const authoring_program_call_options cooperative = { .cooperative = true };
    behavior_execution_scope_begin(&caller);
    int result = 0;
    int status = authoring_program_call(sessions[0], &cooperative, &result);
    behavior_execution_scope_end(&caller);
    REQUIRE(status == FLOW_ERR_STEP_BUDGET && caller.remaining == 16);
    REQUIRE(strstr(authoring_program_error(sessions[0])->message, "execution cancelled"));
    REQUIRE(!behavior_execution_scope_active());
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
    snprintf(sibling, sizeof sibling, "%s/Sibling.chibi", temporary);
    snprintf(core, sizeof core, "%s/core", temporary);
    snprintf(library, sizeof library, "%s/Base.chibi", core);
    REQUIRE(!mkdir(core, 0700));
    write_source(library, "package base\n");
    REQUIRE(catalog());
    for (int i = 0; i < 2; i++) {
        services[i] = program_service_new(core, NULL, 0);
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

/* A program compiled ahead of time opens by name: the closure a compile of
 * the same request returns runs, an update after acceptance reports it
 * unchanged, a name the service does not embed opens nothing, and bytes that
 * are no closure fail verification with a diagnostic. */
TEST_CASE(embedded_programs_open_by_name_and_verify_their_bytes)
{
    REQUIRE(catalog());
    package_compile_error error;
    size_t request_length = 0;
    char *request = package_compile_request(
        snapshot("package app\nMain :: () -> int { return 7 }\n", NULL),
        native,
        &request_length,
        &error
    );
    REQUIRE(request);
    asset_compiler_diagnostic diagnostic;
    size_t length = 0;
    unsigned char *closure = asset_compiler_compile_closure(request, request_length, &length, NULL, &diagnostic);
    free(request);
    REQUIRE(closure);
    static const unsigned char garbage[] = "not a closure";
    const embedded_program programs[] = {
        { "seven", "programs/seven", closure, length },
        { "broken", "programs/broken", garbage, sizeof garbage }
    };
    services[0] = program_service_new("/", programs, 2);
    service_binding *bindings = NULL;
    apush(bindings, ((service_binding){ PROGRAM_SERVICE, services[0] }));
    authoring_program_options configuration = options(TEST_COMPILED_BACKEND);
    configuration.services = bindings;
    program_service_configure(services[0], &configuration);
    afree(bindings);
    REQUIRE(!program_service_open_embedded(services[0], "absent", "Main", NULL, 0));
    uint64_t seven = program_service_open_embedded(services[0], "seven", "Main", NULL, 0);
    REQUIRE(seven && program_service_update(services[0], seven) == 1);
    REQUIRE(program_service_update(services[0], seven) == 0);
    int result = 0;
    REQUIRE(program_service_call(services[0], seven, NULL, &result) == FLOW_OK && result == 7);
    uint64_t broken = program_service_open_embedded(services[0], "broken", "Main", NULL, 0);
    REQUIRE(broken && program_service_update(services[0], broken) == -1);
    REQUIRE(*program_service_diagnostic(services[0], broken));
    program_service_free(services[0]);
    services[0] = NULL;
    free(closure);
    return true;
}

void test_language_program_session()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(session_collection_reachability_preserves_entry_and_source_diagnostics);
    RUN_TEST_CASE(session_reaches_relative_imports_in_the_project_and_collections);
    RUN_TEST_CASE(session_candidates_preserve_package_and_import_state);
    RUN_TEST_CASE(session_patches_frame_calls_by_name_without_restarting_state);
    RUN_TEST_CASE(session_rejected_source_and_entry_keep_the_accepted_runtime);
    RUN_TEST_CASE(session_calls_observe_host_cancellation_and_recover);
    RUN_TEST_CASE(session_cooperative_host_preserves_nested_cancellation);
    RUN_TEST_CASE(session_nested_calls_use_separate_state_and_reject_active_mutations);
    RUN_TEST_CASE(session_calls_debit_the_caller_budget_and_keep_its_vm_policy);
    RUN_TEST_CASE(session_repeated_calls_cannot_reset_the_enclosing_budget);
    RUN_TEST_CASE(session_calls_honor_the_callers_live_cancellation_budget);
    RUN_TEST_CASE(program_handles_suppress_rejections_and_never_alias_other_sessions);
    RUN_TEST_CASE(embedded_programs_open_by_name_and_verify_their_bytes);
    pu_clear_setup();
}
