#include "authoring/source_package_files.h"
#include "authoring/source_package_prepare.h"
#include "execution/status.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
TEST_CASE(package_files_windows_adapter_reports_unsupported)
{
    const source_package_disk_options options = { .project_root = "C:/unused" };
    source_package_load_error error;
    source_package_snapshot *snapshot = source_package_snapshot_load(&options, &error);
    bool unsupported = !snapshot && strstr(error.message, "unsupported on Windows");
    source_package_snapshot_free(snapshot);
    REQUIRE(unsupported);
    return true;
}
void test_language_package_files()
{
    RUN_TEST_CASE(package_files_windows_adapter_reports_unsupported);
}
#elif defined(__EMSCRIPTEN__)
TEST_CASE(package_files_route_requires_native_filesystem)
{
    fprintf(stderr, "The package-files route is admitted only on native Linux/macOS.\n");
    REQUIRE(false);
    return true;
}
void test_language_package_files()
{
    RUN_TEST_CASE(package_files_route_requires_native_filesystem);
}
#else
#include "backend.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* Disposable, fixed source trees. Cleanup visits only explicitly recorded paths,
 * in reverse creation order; it never follows links or traverses directories. */
enum
{
    MAX_PATHS = 64,
    MAX_OWNERS = 3
};
static char temporary[256], project[1024], core[1024];
static struct
{
    char path[1024];
    bool directory;
} paths[MAX_PATHS];
static int path_count;
static authoring_packages *owners[MAX_OWNERS];
static source_package_snapshot *snapshot;
static source_package_load_error load_error;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static pin_value argument, result;
static const script_catalog native;
static const script_lower_bindings bindings = { .generation = 1 };
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static const char
    *main_text = "package app\nimport lib \"../math\"\nimport core "
                 "\"core:base\"\nMain :: (value: int) -> int { return "
                 "Add(value) + lib.Offset() + core.Bias(); }\n";

static void path(char out[1024], const char *base, const char *name)
{
    int size = snprintf(out, 1024, "%s/%s", base, name);
    assert(size > 0 && size < 1024);
}
static void remember(const char *name, bool directory)
{
    for (int i = 0; i < path_count; i++) {
        if (!strcmp(paths[i].path, name)) {
            return;
        }
    }
    assert(path_count < MAX_PATHS && strlen(name) < sizeof paths[0].path);
    strcpy(paths[path_count].path, name);
    paths[path_count++].directory = directory;
}
static void directory(const char *base, const char *name)
{
    char full[1024];
    path(full, base, name);
    assert(!mkdir(full, 0700));
    remember(full, true);
}
static void write_bytes(const char *base, const char *name, const char *bytes, size_t size)
{
    char full[1024];
    path(full, base, name);
    FILE *file = fopen(full, "wb");
    assert(file);
    assert(fwrite(bytes, 1, size, file) == size && !fclose(file));
    remember(full, false);
}
static void write_source(const char *base, const char *name, const char *text)
{
    assert(strlen(text) < 1024);
    write_bytes(base, name, text, strlen(text));
}
static void tree(const char *project_root, const char *core_root)
{
    assert(!mkdir(project_root, 0700));
    remember(project_root, true);
    assert(!mkdir(core_root, 0700));
    remember(core_root, true);
    directory(project_root, "app");
    directory(project_root, "math");
    directory(core_root, "base");
    write_source(project_root, "app/a.chibi", main_text);
    write_source(project_root, "app/z.chibi", "package app\nAdd :: (value: int) -> int { return value + 1; }\n");
    write_source(project_root, "math/a.chibi", "package numbers\nOffset :: () -> int { return 2; }\n");
    write_source(core_root, "base/a.chibi", "package base\nBias :: () -> int { return 3; }\n");
}
static void setup()
{
    strcpy(temporary, "/tmp/chibi-package-files-XXXXXX");
    assert(mkdtemp(temporary));
    char *resolved = realpath(temporary, NULL);
    assert(resolved && strlen(resolved) < sizeof temporary);
    strcpy(temporary, resolved);
    free(resolved);
    remember(temporary, true);
    path(project, temporary, "project space");
    path(core, temporary, "core space");
    tree(project, core);
}
static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
}
static void cleanup()
{
    finish_call();
    source_package_snapshot_free(snapshot);
    snapshot = NULL;
    for (int i = 0; i < MAX_OWNERS; i++) {
        authoring_packages_free(owners[i]);
        owners[i] = NULL;
    }
    for (int i = path_count - 1; i >= 0; i--) {
        int status = paths[i].directory ? rmdir(paths[i].path) : unlink(paths[i].path);
        assert(!status || errno == ENOENT);
    }
    path_count = 0;
}
static source_package_snapshot *load(const char *project_root, const char *core_root)
{
    const source_package_collection collection = { "core", core_root };
    const source_package_disk_options options = { .project_root = project_root,
                                                  .collections = &collection,
                                                  .collection_count = 1 };
    return source_package_snapshot_load(&options, &load_error);
}
static bool prepare(int slot, int previous, behavior_backend backend, const char *project_root, const char *core_root)
{
    snapshot = load(project_root, core_root);
    if (!snapshot) {
        fprintf(stderr, "package load %s: %s\n", load_error.path, load_error.message);
    }
    REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
    authoring_packages_free(owners[slot]);
    owners[slot] = authoring_packages_prepare(
        snapshot,
        &native,
        &bindings,
        backend,
        previous >= 0 ? owners[previous] : NULL
    );
    snapshot = NULL;
    REQUIRE(owners[slot] && authoring_packages_valid(owners[slot]));
    return true;
}
static const authoring_package *app(int slot)
{
    for (int i = 0; i < authoring_packages_count(owners[slot]); i++) {
        const authoring_package *item = authoring_packages_at(owners[slot], i);
        if (!strcmp(item->identity, "project:app")) {
            return item;
        }
    }
    return NULL;
}
static uint64_t main_id(int slot)
{
    const authoring_package *item = app(slot);
    for (int i = 0; item && item->analysis && i < item->analysis->symbol_count; i++) {
        const script_symbol *symbol = &item->analysis->symbols[i];
        if (symbol->kind == SCRIPT_FUNCTION && !strcmp(symbol->name, "Main")) {
            return symbol->id;
        }
    }
    return 0;
}
static bool run(int slot, behavior_backend backend, int expected)
{
    finish_call();
    const authoring_package *item = app(slot);
    REQUIRE(item && item->module);
    uint64_t id = main_id(slot);
    const behavior_function *function = NULL;
    REQUIRE(execution_module_backend(item->module) == backend);
    for (int i = 0; i < execution_module_function_count(item->module); i++) {
        execution_module_function_info info = execution_module_function_at(item->module, i);
        if (info.id == id) {
            function = info.body;
        }
    }
    REQUIRE(function);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    argument = (pin_value){ .type = PIN_I32, .i = 7 };
    result = (pin_value){ .type = PIN_I32, .i = -1 };
    context = (behavior_function_context){
        .activation = behavior_function_resumable(function) ? &activation : NULL,
        .arguments = &argument,
        .argument_count = 1,
        .results = &result,
        .budget = 2000
    };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    int status = behavior_function_run(function, &context, backend == BEHAVIOR_SLJIT);
    behavior_run_scope_end(&scope);
    REQUIRE(
        status == FLOW_OK && context.returned && result.type == PIN_I32 && result.i == expected
    );
    REQUIRE(
        !scope.counts.overflow && !scope.counts.inspection_bytecode_fallbacks &&
        !scope.counts.inspected_sljit_entries &&
        !scope.counts.inspected_wasm_entries && !scope.counts.wasm_entries
    );
    REQUIRE(
        backend == BEHAVIOR_SLJIT
            ? scope.counts.sljit_entries > 0 && !scope.counts.bytecode_entries
            : scope.counts.bytecode_entries > 0 && !scope.counts.sljit_entries
    );
    finish_call();
    return true;
}
TEST_CASE(package_files_siblings_dependencies_and_explicit_core_execute)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, -1, backends[b], project, core));
        REQUIRE(authoring_packages_count(owners[0]) == 3 && run(0, backends[b], 13));
    }
    return true;
}
TEST_CASE(package_files_relocation_preserves_logical_identity)
{
    char moved_project[1024], moved_core[1024];
    path(moved_project, temporary, "moved project");
    path(moved_core, temporary, "moved core");
    tree(moved_project, moved_core);
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, -1, backends[b], project, core));
        REQUIRE(prepare(1, -1, backends[b], moved_project, moved_core));
        REQUIRE(main_id(0) && main_id(0) == main_id(1));
        REQUIRE(run(0, backends[b], 13) && run(1, backends[b], 13));
    }
    return true;
}
TEST_CASE(package_files_reload_reuses_and_invalidates_dependents)
{
    for (int b = 0; b < 2; b++) {
        write_source(project, "math/a.chibi", "package numbers\nOffset :: () -> int { return 2; }\n");
        REQUIRE(prepare(0, -1, backends[b], project, core));
        REQUIRE(prepare(1, 0, backends[b], project, core));
        REQUIRE(authoring_packages_reused(owners[1]) == 3);
        REQUIRE(app(0)->generation == app(1)->generation);
        write_source(project, "math/a.chibi", "package numbers\nOffset :: () -> int { return 5; }\n");
        REQUIRE(prepare(2, 1, backends[b], project, core));
        REQUIRE(
            authoring_packages_reused(owners[2]) == 1 &&
            app(1)->generation != app(2)->generation
        );
        REQUIRE(run(0, backends[b], 13));
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        authoring_packages_free(owners[1]);
        owners[1] = NULL;
        REQUIRE(run(2, backends[b], 16));
    }
    return true;
}
TEST_CASE(package_files_missing_import_retains_parsed_diagnostic)
{
    const char *text = "package app\nimport missing \"../absent\"\nMain :: "
                       "(value: int) -> int { return value; }\n";
    write_source(project, "app/a.chibi", text);
    snapshot = load(project, core);
    REQUIRE(snapshot);
    const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
    REQUIRE(error && !source_package_snapshot_graph(snapshot));
    int count;
    const script_package_document *documents = source_package_snapshot_documents(snapshot, &count);
    REQUIRE(error->diagnostic.range.document >= 0 && error->diagnostic.range.document < count);
    const script_package_document *origin = &documents[error->diagnostic.range.document];
    REQUIRE(!strcmp(origin->key, "project:app/a.chibi"));
    script_range range = error->diagnostic.range;
    const char *token = strstr(text, "\"../absent\"");
    REQUIRE(range.start == token - text && range.end == token - text + (int)strlen("\"../absent\""));
    return true;
}
TEST_CASE(package_files_descendant_symlink_file_and_directory_are_rejected)
{
    char outside[1024], linked[1024];
    path(outside, temporary, "outside.chibi");
    write_source(temporary, "outside.chibi", "package outside\n");
    path(linked, project, "app/link.chibi");
    assert(!symlink(outside, linked));
    remember(linked, false);
    snapshot = load(project, core);
    REQUIRE(!snapshot && load_error.message[0]);
    REQUIRE(!strcmp(load_error.path, linked));
    REQUIRE(!unlink(linked));
    directory(temporary, "outside directory");
    path(outside, temporary, "outside directory");
    write_source(outside, "a.chibi", "package outside\n");
    path(linked, project, "escape");
    assert(!symlink(outside, linked));
    remember(linked, false);
    snapshot = load(project, core);
    REQUIRE(!snapshot && load_error.message[0]);
    REQUIRE(!strcmp(load_error.path, linked));
    return true;
}
TEST_CASE(package_files_explicit_root_symlinks_are_canonicalized)
{
    char project_link[1024], core_link[1024];
    path(project_link, temporary, "project link");
    path(core_link, temporary, "core link");
    assert(!symlink(project, project_link) && !symlink(core, core_link));
    remember(project_link, false);
    remember(core_link, false);
    snapshot = load(project_link, core_link);
    REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
    int count;
    const script_package_document *documents = source_package_snapshot_documents(snapshot, &count);
    REQUIRE(count == 4);
    bool canonical = false;
    for (int i = 0; i < count; i++) {
        if (!strcmp(documents[i].key, "project:app/a.chibi")) {
            char expected[1024];
            path(expected, project, "app/a.chibi");
            canonical = !strcmp(documents[i].path, expected);
        }
    }
    REQUIRE(canonical);
    return true;
}
TEST_CASE(package_files_duplicate_physical_roots_reject_and_overlaps_scan_once)
{
    char alias[1024];
    path(alias, temporary, "core alias");
    assert(!symlink(core, alias));
    remember(alias, false);
    const source_package_collection duplicates[] = { { "core", core },
                                                     { "vendor", alias } };
    const source_package_disk_options duplicate = { .project_root = project,
                                                    .collections = duplicates,
                                                    .collection_count = 2 };
    snapshot = source_package_snapshot_load(&duplicate, &load_error);
    REQUIRE(!snapshot && load_error.message[0]);
    char nested[1024];
    path(nested, project, "math");
    write_source(project, "app/a.chibi", "package app\nimport lib \"core:\"\nMain :: (value: int) -> int { return Add(value) + lib.Offset(); }\n");
    snapshot = load(project, nested);
    REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
    int count;
    const script_package_document *documents = source_package_snapshot_documents(snapshot, &count);
    REQUIRE(count == 3);
    bool longest = false;
    for (int i = 0; i < count; i++) {
        longest |= !strcmp(documents[i].key, "core:a.chibi");
    }
    REQUIRE(longest && source_packages_count(source_package_snapshot_graph(snapshot)) == 2);
    source_package_snapshot_free(snapshot);
    snapshot = NULL;
    /* An explicit root remains visible when an ancestor's hidden-descendant
     * filter skips its location during the project forest scan. */
    directory(project, ".collections");
    char hidden_parent[1024], hidden_core[1024];
    path(hidden_parent, project, ".collections");
    directory(hidden_parent, "core");
    path(hidden_core, hidden_parent, "core");
    directory(hidden_core, "base");
    write_source(hidden_core, "base/a.chibi", "package base\nBias :: () -> int { return 3; }\n");
    write_source(project, "app/a.chibi", main_text);
    snapshot = load(project, hidden_core);
    REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
    source_package_snapshot_documents(snapshot, &count);
    REQUIRE(count == 4 && source_packages_count(source_package_snapshot_graph(snapshot)) == 3);
    return true;
}
TEST_CASE(package_files_nul_oversized_and_fifo_inputs_reject_without_reading_a_stream)
{
    char source[1024];
    path(source, project, "math/a.chibi");
    static const char nul[] = "package numbers\n\0hidden";
    write_bytes(project, "math/a.chibi", nul, sizeof nul - 1);
    snapshot = load(project, core);
    REQUIRE(!snapshot && load_error.message[0]);
    REQUIRE(!strcmp(load_error.path, source));
    int fd = open(source, O_WRONLY | O_TRUNC);
    REQUIRE(fd >= 0);
    assert(!ftruncate(fd, 512 * 1024 + 1) && !close(fd));
    snapshot = load(project, core);
    REQUIRE(!snapshot && load_error.message[0]);
    REQUIRE(!strcmp(load_error.path, source));
    REQUIRE(!unlink(source) && !mkfifo(source, 0600));
    snapshot = load(project, core);
    REQUIRE(!snapshot && load_error.message[0]);
    REQUIRE(!strcmp(load_error.path, source));
    return true;
}
void test_language_package_files()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(package_files_siblings_dependencies_and_explicit_core_execute);
    RUN_TEST_CASE(package_files_relocation_preserves_logical_identity);
    RUN_TEST_CASE(package_files_reload_reuses_and_invalidates_dependents);
    RUN_TEST_CASE(package_files_missing_import_retains_parsed_diagnostic);
    RUN_TEST_CASE(package_files_descendant_symlink_file_and_directory_are_rejected);
    RUN_TEST_CASE(package_files_explicit_root_symlinks_are_canonicalized);
    RUN_TEST_CASE(package_files_duplicate_physical_roots_reject_and_overlaps_scan_once);
    RUN_TEST_CASE(package_files_nul_oversized_and_fifo_inputs_reject_without_reading_a_stream);
    pu_clear_setup();
}

#endif
