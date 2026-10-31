#include "authoring/source_package_files.h"
#include "authoring/source_package_prepare.h"
#include "execution/status.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include "support/array.h"

#include <assert.h>
#include <stdlib.h>
#include <windows.h>
#include <winioctl.h>

static char temporary[1024], project[1024], core[1024];
static struct
{
    char path[1024];
    bool directory;
} paths[24];
static int path_count;
static source_package_snapshot *snapshot;
static source_package_input *files;
static source_package_load_error load_error;

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
    assert(path_count < 24 && strlen(name) < sizeof paths[0].path);
    strcpy(paths[path_count].path, name);
    paths[path_count++].directory = directory;
}
static void directory(const char *base, const char *name)
{
    char full[1024];
    path(full, base, name);
    assert(CreateDirectoryA(full, NULL));
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
static void setup()
{
    char base[1024];
    DWORD length = GetTempPathA(sizeof base, base);
    assert(length && length < sizeof base);
    assert(GetTempFileNameA(base, "cbp", 0, temporary));
    assert(DeleteFileA(temporary) && CreateDirectoryA(temporary, NULL));
    for (char *part = temporary; *part; part++) {
        if (*part == '\\') {
            *part = '/';
        }
    }
    remember(temporary, true);
    directory(temporary, "project");
    directory(temporary, "core");
    path(project, temporary, "project");
    path(core, temporary, "core");
    directory(core, "base");
    const char *main = "package app\nimport core \"core:base\"\nMain :: () -> "
                       "int { return Add() + core.Bias() }\n";
    const char *helper = "package app\nAdd :: () -> int { return 2 }\n";
    const char *library = "package base\npub Bias :: () -> int { return 3 }\n";
    write_bytes(project, "A.chibi", main, strlen(main));
    write_bytes(project, "Z.chibi", helper, strlen(helper));
    write_bytes(core, "base/Base.chibi", library, strlen(library));
}
static void cleanup()
{
    source_package_snapshot_free(snapshot);
    snapshot = NULL;
    source_package_files_free(files);
    files = NULL;
    for (int i = path_count - 1; i >= 0; i--) {
        bool removed = paths[i].directory ? RemoveDirectoryA(paths[i].path)
                                          : DeleteFileA(paths[i].path);
        assert(removed || GetLastError() == ERROR_FILE_NOT_FOUND);
    }
    path_count = 0;
}
static source_package_snapshot *load()
{
    const source_package_collection collection = { "core", core };
    const source_package_disk_options options = { .project_root = project,
                                                  .collections = &collection,
                                                  .collection_count = 1 };
    return source_package_snapshot_load(&options, &load_error);
}
TEST_CASE(package_files_windows_loads_canonical_sources_and_imports)
{
    directory(project, ".hidden");
    write_bytes(project, ".hidden/Broken.chibi", "invalid", 7);
    snapshot = load();
    REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
    int count = 0;
    const script_package_document *documents = source_package_snapshot_documents(snapshot, &count);
    REQUIRE(count == 3 && source_packages_count(source_package_snapshot_graph(snapshot)) == 2);
    for (int i = 0; i < count; i++) {
        REQUIRE(
            (documents[i].path[0] == '/' && documents[i].path[1] == '/') ||
            (documents[i].path[1] == ':' && documents[i].path[2] == '/')
        );
        REQUIRE(!strchr(documents[i].path, '\\'));
        REQUIRE(!i || strcmp(documents[i - 1].path, documents[i].path) < 0);
    }
    return true;
}
TEST_CASE(package_files_windows_reads_only_the_explicit_source)
{
    write_bytes(project, "Broken.chibi", "invalid", 7);
    char selected[1024];
    path(selected, project, "Z.chibi");
    REQUIRE(source_package_file_read(selected, &files, &load_error));
    REQUIRE(acount(files) == 1);
    REQUIRE(strstr(files[0].path, "/Z.chibi"));
    REQUIRE(strstr(files[0].text, "return 2"));
    REQUIRE(files[0].size == strlen(files[0].text));
    return true;
}
TEST_CASE(package_files_windows_rejects_nul_and_oversized_sources)
{
    char selected[1024];
    path(selected, project, "Bad.chibi");
    write_bytes(project, "Bad.chibi", "bad\0bytes", 9);
    REQUIRE(!source_package_file_read(selected, &files, &load_error));
    REQUIRE(!files && strstr(load_error.message, "NUL"));
    HANDLE file = CreateFileA(selected, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    LARGE_INTEGER size = { .QuadPart = 512 * 1024 + 1 };
    bool extended = SetFilePointerEx(file, size, NULL, FILE_BEGIN) &&
                    SetEndOfFile(file);
    bool closed = CloseHandle(file);
    REQUIRE(extended && closed);
    REQUIRE(!source_package_file_read(selected, &files, &load_error));
    REQUIRE(!files && strstr(load_error.message, "byte limit"));
    return true;
}
/* Directory junctions need no symbolic-link privilege. The only target is our
 * disposable fixture; cleanup removes the junction itself with RemoveDirectory. */
static bool junction(const char *name, const char *target)
{
    char destination[1024], absolute[1024];
    path(destination, temporary, name);
    DWORD size = GetFullPathNameA(target, sizeof absolute, absolute, NULL);
    if (!size || size >= sizeof absolute || !CreateDirectoryA(destination, NULL)) {
        return false;
    }
    remember(destination, true);
    struct
    {
        DWORD tag;
        WORD size, reserved;
        WORD substitute_offset, substitute_size, print_offset, print_size;
        WCHAR path[2056];
    } reparse = { .tag = IO_REPARSE_TAG_MOUNT_POINT };
    WCHAR wide[1024];
    int length = MultiByteToWideChar(CP_ACP, 0, absolute, -1, wide, 1024);
    if (!length) {
        return false;
    }
    memcpy(reparse.path, L"\\??\\", 4 * sizeof(WCHAR));
    memcpy(reparse.path + 4, wide, (size_t)length * sizeof(WCHAR));
    reparse.substitute_size = (WORD)((length + 3) * sizeof(WCHAR));
    reparse.print_offset = (WORD)(reparse.substitute_size + sizeof(WCHAR));
    reparse.print_size = (WORD)((length - 1) * sizeof(WCHAR));
    memcpy((char *)reparse.path + reparse.print_offset, wide, (size_t)length * sizeof(WCHAR));
    reparse.size = (WORD)(8 + reparse.print_offset + reparse.print_size + sizeof(WCHAR));
    HANDLE file = CreateFileA(
        destination,
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    bool ok = DeviceIoControl(
        file,
        FSCTL_SET_REPARSE_POINT,
        &reparse,
        (DWORD)reparse.size + 8,
        NULL,
        0,
        &written,
        NULL
    );
    return CloseHandle(file) && ok;
}
TEST_CASE(package_files_windows_canonicalizes_roots_and_rejects_descendant_junctions)
{
    REQUIRE(junction("root-link", project));
    char root_link[1024];
    path(root_link, temporary, "root-link");
    REQUIRE(source_package_files_read(root_link, &files, &load_error));
    REQUIRE(acount(files) == 2);
    REQUIRE(!strstr(files[0].path, "root-link"));
    source_package_files_free(files);
    files = NULL;
    REQUIRE(junction("project/escape", core));
    snapshot = load();
    REQUIRE(!snapshot && strstr(load_error.message, "reparse"));
    return true;
}
TEST_CASE(package_files_windows_normalizes_drive_and_unc_roots)
{
    static const struct
    {
        const char *root, *file, *normalized, *error;
    } cases[] = {
        { "C:/project/./app", "C:/project/./app/Main.chibi", "C:/project/app/Main.chibi", NULL },
        { "//server/share/project/../app", "Main.chibi", "//server/share/app/Main.chibi", NULL },
        { "C:/", "C:/Main.chibi", "C:/Main.chibi", NULL },
        { "//server/share", "//server/share/Main.chibi", "//server/share/Main.chibi", NULL },
        { "C:/project", "../project/Main.chibi", NULL, "Invalid package file path or root escape" },
        { "//server/share/../outside", "Main.chibi", NULL, "Invalid absolute collection root" },
        { "C:/", "C:/", NULL, "Invalid package file path" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const source_package_input file = { cases[i].file, "package app\n", 12 };
        const source_package_snapshot_options options = {
            .project_root = cases[i].root,
            .files = &file,
            .file_count = 1
        };
        snapshot = source_package_snapshot_new(&options);
        const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
        if (cases[i].error) {
            REQUIRE(error && strstr(error->diagnostic.message, cases[i].error));
        } else {
            REQUIRE(!error);
            int count = 0;
            const script_package_document
                *documents = source_package_snapshot_documents(snapshot, &count);
            REQUIRE(count == 1 && !strcmp(documents[0].path, cases[i].normalized));
            REQUIRE(!strcmp(documents[0].key, "project:Main.chibi"));
            REQUIRE(!strcmp(source_package_snapshot_identity(snapshot, 0), "project:"));
        }
        source_package_snapshot_free(snapshot);
        snapshot = NULL;
    }
    return true;
}
void test_language_package_files()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(package_files_windows_loads_canonical_sources_and_imports);
    RUN_TEST_CASE(package_files_windows_reads_only_the_explicit_source);
    RUN_TEST_CASE(package_files_windows_rejects_nul_and_oversized_sources);
    RUN_TEST_CASE(package_files_windows_canonicalizes_roots_and_rejects_descendant_junctions);
    RUN_TEST_CASE(package_files_windows_normalizes_drive_and_unc_roots);
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
    write_source(project_root, "math/a.chibi", "package numbers\npub Offset :: () -> int { return 2; }\n");
    write_source(core_root, "base/a.chibi", "package base\npub Bias :: () -> int { return 3; }\n");
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
        write_source(project, "math/a.chibi", "package numbers\npub Offset :: () -> int { return 2; }\n");
        REQUIRE(prepare(0, -1, backends[b], project, core));
        REQUIRE(prepare(1, 0, backends[b], project, core));
        REQUIRE(authoring_packages_reused(owners[1]) == 3);
        REQUIRE(app(0)->generation == app(1)->generation);
        write_source(project, "math/a.chibi", "package numbers\npub Offset :: () -> int { return 5; }\n");
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
    write_source(hidden_core, "base/a.chibi", "package base\npub Bias :: () -> int { return 3; }\n");
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
