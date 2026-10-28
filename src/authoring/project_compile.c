#include "authoring/project_compile.h"

#include "authoring/graph_catalog.h"
#include "authoring/native_bindings.h"
#include "authoring/package_tests.h"
#include "authoring/project_prepare.h"
#include "authoring/renderer_catalog.h"
#include "authoring/source_package_files.h"
#include "execution/backend.h"
#include "execution/function.h"
#include "execution/module.h"
#include "execution/status.h"
#include "host/native_default.h"
#include "runtime/arrays_definition.h"
#include "runtime/component_catalog.h"
#include "runtime/task_pool.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "support/asset_format.h"
#include "support/asset_text.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char *temporary_path(const char *path)
{
    size_t length = strlen(path) + sizeof ".tmp.XXXXXX";
    char *temporary = xmalloc(length);
    snprintf(temporary, length, "%s.tmp.XXXXXX", path);
    return temporary;
}
static char *write_temporary(const char *path, const void *data, size_t bytes)
{
    char *temporary = temporary_path(path);
    int fd = mkstemp(temporary);
    if (fd < 0) {
        free(temporary);
        return NULL;
    }
    FILE *file = fdopen(fd, "wb");
    bool valid = file && fwrite(data, 1, bytes, file) == bytes;
    if (file) {
        valid = fclose(file) == 0 && valid;
    } else {
        close(fd);
    }
    if (valid) {
        return temporary;
    }
    unlink(temporary);
    free(temporary);
    return NULL;
}
static bool backup_file(const char *path, char **backup)
{
    struct stat info;
    if (lstat(path, &info)) {
        return errno == ENOENT;
    }
    if (!S_ISREG(info.st_mode)) {
        errno = EINVAL;
        return false;
    }
    char *temporary = temporary_path(path);
    int fd = mkstemp(temporary);
    if (fd < 0) {
        free(temporary);
        return false;
    }
    close(fd);
    if (rename(path, temporary)) {
        unlink(temporary);
        free(temporary);
        return false;
    }
    *backup = temporary;
    return true;
}
/* Creates the directories above `path` that do not exist yet. */
static bool make_parents(const char *path)
{
    assert(path[0]);
    char *directory = xstrdup(path);
    bool valid = true;
    for (char *slash = strchr(directory + 1, '/'); valid && slash;
         slash = strchr(slash + 1, '/')) {
        *slash = 0;
        valid = !mkdir(directory, 0777) || errno == EEXIST;
        *slash = '/';
    }
    free(directory);
    return valid;
}
static bool write_artifact(
    const char *output,
    const authoring_project_artifact *artifact,
    char *error,
    size_t size
)
{
    size_t length = strlen(output) + sizeof ".resources.json";
    char *manifest = xmalloc(length);
    snprintf(manifest, length, "%s.resources.json", output);
    const char *paths[] = { output, manifest };
    char *temporary[] = { NULL, NULL };
    if (make_parents(output)) {
        temporary[0] = write_temporary(output, artifact->data, artifact->size);
    }
    char *backups[2] = { 0 };
    bool published[2] = { 0 };
    bool valid = temporary[0] != NULL;
    if (valid) {
        temporary[1] = write_temporary(
            manifest,
            artifact->manifest,
            strlen(artifact->manifest)
        );
        valid = temporary[1] != NULL;
    }
    for (int i = 0; valid && i < 2; i++) {
        valid = backup_file(paths[i], &backups[i]);
    }
    for (int i = 0; valid && i < 2; i++) {
        valid = rename(temporary[i], paths[i]) == 0;
        published[i] = valid;
    }
    if (!valid) {
        snprintf(error, size, "Cannot publish prepared artifact pair: %s", strerror(errno));
        for (int i = 0; i < 2; i++) {
            if (published[i]) {
                unlink(paths[i]);
            }
            if (backups[i] && rename(backups[i], paths[i])) {
                snprintf(error, size, "Cannot restore prior artifact; retained backup: %s", backups[i]);
                /* Preserve the old bytes when external filesystem mutation
                 * prevents restoration; report their exact recovery path. */
                free(backups[i]);
                backups[i] = NULL;
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        if (temporary[i]) {
            unlink(temporary[i]);
            free(temporary[i]);
        }
        if (backups[i]) {
            unlink(backups[i]);
            free(backups[i]);
        }
    }
    free(manifest);
    return valid;
}
bool authoring_application_begin(authoring_application *application, char *error, size_t size)
{
    *application = (authoring_application){ authoring_renderer_catalog_new(error, size),
                                            NULL };
    if (!application->native) {
        return false;
    }
    authoring_graph_catalog_config config = {
        .native = application->native,
        .types = TYPE_PACK,
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT }
    };
    application->graphs = authoring_graph_catalog_new(config, error, size);
    if (!application->graphs) {
        native_capabilities_free(application->native);
        return false;
    }
    int count = 0;
    const registry_pack *const
        *packs = authoring_graph_catalog_packs(application->graphs, &count);
    registry_set_packs(packs, count);
    return true;
}
void authoring_application_end(authoring_application *application)
{
    registry_set_packs(NULL, 0);
    authoring_graph_catalog_free(application->graphs);
    native_capabilities_free(application->native);
}
/* Prepares the project, or assembles it when `modules` is nonnull,
 * and writes the artifact to `output` when there is one. */
static int compile(
    const char *project,
    const char *scene,
    const package_module *modules,
    int count,
    const char *output,
    authoring_cost_report_mode cost_report
)
{
    char error[512] = { 0 };
    authoring_application application;
    if (!authoring_application_begin(&application, error, sizeof error)) {
        fprintf(stderr, "Project preparation failed: %s\n", error);
        return 1;
    }
    authoring_project_artifact artifact = { 0 };
    authoring_project_options options = { .project = project,
                                          .scene = scene,
                                          .native = application.native,
                                          .components = { REGISTRY_COMPONENTS,
                                                          REGISTRY_COMPONENT_COUNT },
                                          .modules = WORLD_MODULES_ALL,
                                          .read_only = !output,
                                          .cost_report = cost_report };
    bool valid = modules
                   ? authoring_project_assemble(&options, modules, count, &artifact, error, sizeof error)
                   : authoring_project_prepare(&options, &artifact, error, sizeof error);
    authoring_application_end(&application);
    if (valid && output) {
        valid = write_artifact(output, &artifact, error, sizeof error);
    }
    if (valid && output) {
        printf("Prepared project: %s (%zu bytes)\n", output, artifact.size);
    } else if (valid && cost_report) {
        fputs(artifact.cost_report, stdout);
    } else if (valid) {
        printf("Checked project: %s\n", project);
    } else {
        fprintf(stderr, "Project preparation failed: %s\n", error);
    }
    authoring_project_artifact_free(&artifact);
    return valid ? 0 : 1;
}
int authoring_project_compile(const char *project, const char *scene, const char *output)
{
    return compile(project, scene, NULL, 0, output, AUTHORING_COST_REPORT_NONE);
}
int authoring_project_explain_costs(const char *project, const char *scene, bool json)
{
    return compile(project, scene, NULL, 0, NULL, json ? AUTHORING_COST_REPORT_JSON : AUTHORING_COST_REPORT_TEXT);
}
int authoring_project_compile_modules(
    const char *project,
    const package_module *modules,
    int count,
    const char *output
)
{
    assert(count >= 0 && (!count || modules) && output);
    static const package_module none;
    return compile(project, NULL, count ? modules : &none, count, output, AUTHORING_COST_REPORT_NONE);
}

static void print_diagnostic(const char *path, const script_diagnostic *diagnostic)
{
    if (!path) {
        fprintf(stderr, "%s\n", diagnostic->message);
        return;
    }
    const script_range *at = &diagnostic->range;
    fprintf(stderr, "%s:%d:%d: %s\n", path, at->line, at->column, diagnostic->message);
}
static void print_invalid(const authoring_packages *packages)
{
    const source_package_snapshot *snapshot = authoring_packages_snapshot(packages);
    const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
    if (error) {
        int count = 0;
        const script_package_document
            *documents = source_package_snapshot_documents(snapshot, &count);
        int document = error->diagnostic.range.document;
        print_diagnostic(
            document >= 0 && document < count ? documents[document].path : NULL,
            &error->diagnostic
        );
        return;
    }
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        for (int j = 0; j < package->diagnostic_count; j++) {
            const script_diagnostic *diagnostic = &package->diagnostics[j];
            int document = diagnostic->range.document;
            const script_analysis *analysis = package->analysis;
            bool known = analysis && document >= 0 && document < analysis->document_count;
            print_diagnostic(known ? analysis->documents[document].path : NULL, diagnostic);
        }
    }
}
/* Runs each test and prints its outcome; the number that failed. */
static int run_tests(const authoring_packages *packages, behavior_backend backend, int workers)
{
    package_test *tests = package_tests_find(packages);
    int count = acount(tests);
    package_test_result *results = xcalloc((size_t)count, sizeof *results);
    task_pool *pool = task_pool_acquire_workers(workers);
    task_pool_begin(pool, count > 1);
    package_tests_run(tests, count, backend, PACKAGE_TEST_BUDGET, task_pool_tasks(pool), results);
    task_pool_end(pool);
    task_pool_release(pool);
    int failed = 0;
    for (int i = 0; i < count; i++) {
        package_test_failure failure = results[i].failure;
        bool passed = results[i].passed;
        printf(
            "%s %s.%s\n",
            passed ? "PASS" : "FAIL",
            tests[i].package,
            tests[i].name
        );
        if (passed) {
            continue;
        }
        failed++;
        if (failure.path) {
            printf(
                "  %s:%d:%d: %s\n",
                failure.path,
                failure.line,
                failure.column,
                failure.message
            );
        } else {
            printf("  %s\n", failure.message);
        }
    }
    printf("%d tests, %d failed\n", count, failed);
    free(results);
    afree(tests);
    return failed;
}
/* Tests run without a game, so they get the natives that need none of it, and
 * the compiler's raw-syntax and JSON bridges, so the compiler's own packages
 * can hold tests. */
int authoring_project_test(const char *root, const char *core)
{
    /* Independent of the repository selector runner's process-level job limit. */
    const char *setting = getenv("CHIBI_TEST_WORKERS");
    int workers = 0;
    if (setting) {
        char *end;
        errno = 0;
        long value = strtol(setting, &end, 10);
        if (!*setting || strspn(setting, "0123456789") != strlen(setting) ||
            *end || errno || value < 1 || value > TASK_WORKER_MAX) {
            fprintf(stderr, "CHIBI_TEST_WORKERS must be an integer from 1 to %d\n", TASK_WORKER_MAX);
            return 1;
        }
        workers = (int)value;
    }
    char error[512] = { 0 };
    const native_definition_pack compiler[] = { COMPILER_SYNTAX_DEFINITION_PACK,
                                                COMPILER_JSON_DEFINITION_PACK };
    native_capabilities *native = host_native_standalone_catalog_new(compiler, 2, error, sizeof error);
    if (!native) {
        fprintf(stderr, "Test preparation failed: %s\n", error);
        return 1;
    }
    source_package_collection collections[] = { { "core", core } };
    source_package_disk_options options = { root, collections, 1 };
    source_package_load_error load = { 0 };
    source_package_snapshot *snapshot = source_package_snapshot_load(&options, &load);
    if (!snapshot) {
        fprintf(stderr, "%s: %s\n", load.path, load.message);
        native_capabilities_free(native);
        return 1;
    }
    script_lower_bindings bindings = authoring_native_bindings(native);
    behavior_backend backend = behavior_default_backend();
    authoring_packages *packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(native),
        &bindings,
        backend,
        NULL
    );
    int status = 1;
    if (authoring_packages_valid(packages)) {
        status = run_tests(packages, backend, workers) ? 1 : 0;
    } else {
        print_invalid(packages);
    }
    authoring_packages_free(packages);
    native_capabilities_free(native);
    return status;
}

/* Instructions one call of the site generator may run. */
#define DOCS_BUDGET 1000000000u

typedef enum
{
    DOCS_MARKDOWN, /* A Markdown page. */
    DOCS_PACKAGES, /* The reference of the packages in a folder. */
    DOCS_GRAMMAR,  /* The rules of a Tree-sitter grammar. */
    DOCS_KINDS
} docs_kind;
typedef struct
{
    const char *section, *title, *source, *output;
    docs_kind kind;
    const char *collection; /* The packages' import collection. */
} docs_page;

static char *join(const char *base, const char *name)
{
    size_t size = strlen(base) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", base, name);
    return path;
}

/* A page file name: letters, digits, hyphens, underscores and dots, ending in
 * .html, so every page lands in the site's own folder. */
static bool page_name(const char *name)
{
    size_t length = strlen(name);
    if (length <= strlen(".html") || strcmp(name + length - strlen(".html"), ".html")) {
        return false;
    }
    return strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") ==
           length;
}

/* The source file a page names, resolved: a regular file inside root, which
 * is itself resolved, so no link leads outside it. NULL when it is not. */
static char *source_file(const char *root, const char *source)
{
    char *joined = join(root, source);
    char *resolved = realpath(joined, NULL);
    free(joined);
    size_t length = strlen(root);
    struct stat info;
    bool inside = resolved && !strncmp(resolved, root, length) &&
                  resolved[length] == '/' && !stat(resolved, &info) &&
                  S_ISREG(info.st_mode);
    if (!inside) {
        free(resolved);
        return NULL;
    }
    return resolved;
}

/* The pages docs/site.kdl lists, in navigation order, borrowed from doc. */
static bool read_site(const av_doc *doc, const char **site, docs_page **pages, char *error, size_t size)
{
    const av_value *root = av_doc_root(doc);
    *site = av_get_str(av_obj_get(root, "title"));
    const av_value *sections = av_obj_get(root, "sections");
    if (!*site || !sections || !av_is_arr(sections) || !av_arr_size(sections)) {
        snprintf(error, size, "the site needs a title and sections");
        return false;
    }
    for (size_t i = 0; i < av_arr_size(sections); i++) {
        const av_value *section = av_arr_get(sections, i);
        const char *name = av_get_str(av_obj_get(section, "name"));
        const av_value *list = av_obj_get(section, "pages");
        if (!name || !list || !av_is_arr(list) || !av_arr_size(list)) {
            snprintf(error, size, "section %zu needs a name and pages", i + 1);
            return false;
        }
        for (size_t j = 0; j < av_arr_size(list); j++) {
            const av_value *entry = av_arr_get(list, j);
            const char *markdown = av_get_str(av_obj_get(entry, "source"));
            const char *packages = av_get_str(av_obj_get(entry, "packages"));
            const char *grammar = av_get_str(av_obj_get(entry, "grammar"));
            docs_page page = { name,
                               av_get_str(av_obj_get(entry, "title")),
                               markdown   ? markdown
                               : packages ? packages
                                          : grammar,
                               av_get_str(av_obj_get(entry, "output")),
                               markdown   ? DOCS_MARKDOWN
                               : packages ? DOCS_PACKAGES
                                          : DOCS_GRAMMAR,
                               av_get_str(av_obj_get(entry, "collection")) };
            if (!page.title || !page.output || !page_name(page.output) ||
                !!markdown + !!packages + !!grammar != 1 ||
                !packages != !page.collection) {
                snprintf(
                    error,
                    size,
                    "page %zu of %s needs a title, an .html output and one of "
                    "source, grammar or packages with its collection",
                    j + 1,
                    name
                );
                return false;
            }
            for (int k = 0; k < acount(*pages); k++) {
                if (!strcmp((*pages)[k].output, page.output)) {
                    snprintf(error, size, "two pages write %s", page.output);
                    return false;
                }
            }
            apush(*pages, page);
        }
    }
    return true;
}

/* An exported generator function, whose signature the C side relies on. */
static const behavior_function *generator_function(
    const authoring_packages *packages,
    const char *identity,
    const char *name,
    int parameters
)
{
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        for (int j = 0; strcmp(package->identity, identity) == 0 &&
                        j < package->exports->member_count;
             j++) {
            if (strcmp(package->exports->members[j].name, name)) {
                continue;
            }
            uint64_t id = package->exports->members[j].id;
            const script_catalog_capability
                *signature = script_catalog_capability_by_id(package->exports->metadata, id);
            assert(
                signature && signature->parameter_count == parameters &&
                signature->result_count == 1
            );
            (void)signature;
            (void)parameters;
            for (int k = 0; k < execution_module_function_count(package->module); k++) {
                execution_module_function_info
                    info = execution_module_function_at(package->module, k);
                if (info.id == id) {
                    return info.body;
                }
            }
        }
    }
    return NULL;
}

/* Runs a generator function and writes the pieces of its string[] result to
 * file, which is called name in errors. */
static bool generate(
    const behavior_function *function,
    const pin_value *arguments,
    int count,
    behavior_backend backend,
    FILE *file,
    const char *name,
    char *error,
    size_t size
)
{
    behavior_function_activation activation = { 0 };
    bool resumable = behavior_function_resumable(function);
    if (resumable) {
        behavior_function_activation_init(function, &activation);
    }
    pin_value result = { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY };
    behavior_managed_heap managed;
    behavior_managed_heap_init(&managed);
    behavior_function_context context = { .managed_heap = &managed,
                                          .activation = resumable ? &activation : NULL,
                                          .arguments = arguments,
                                          .argument_count = count,
                                          .results = &result,
                                          .budget = DOCS_BUDGET };
    arena storage = { 0 };
    behavior_function_bind_storage(&context, &storage);
    bool bound = behavior_function_bind(function, &context, NULL, 0);
    assert(bound);
    (void)bound;
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    bool generated = status == FLOW_OK && context.returned;
    if (!generated) {
        const char *reason = context.error_message
                               ? context.error_message
                               : behavior_function_failure_text(context.failure);
        snprintf(error, size, "the site generator failed on %s: %s", name, reason);
    }
    const pin_array *pieces = result.ptr;
    bool written = generated;
    for (int i = 0; written && i < pieces->count; i++) {
        pin_string piece = pin_text(pieces->items[i]);
        written = fwrite(piece.bytes, 1, piece.length, file) == piece.length;
    }
    if (generated && !written) {
        snprintf(error, size, "cannot write %s: %s", name, strerror(errno));
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&managed);
    arena_free(&storage);
    return written;
}

static pin_value text_value(arena *storage, const char *text)
{
    pin_value value;
    const char *error = pin_value_text(storage, &value, text, strlen(text));
    assert(!error);
    (void)error;
    return value;
}

/* An empty string[] argument of count items in storage. */
static pin_value strings_value(arena *storage, int count)
{
    pin_array *array = pin_array_snapshot_allocate(storage, PIN_STR, NULL, count);
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}

/* Appends text's lines, without their line breaks, to *lines; they point into
 * text, which this splits in place. */
static void split_lines(char *text, const char ***lines)
{
    while (*text) {
        char *end = strchr(text, '\n');
        if (end) {
            *end = '\0';
        }
        apush(*lines, text);
        text = end ? end + 1 : text + strlen(text);
    }
}

/* lines as a string[] argument in storage. */
static pin_value lines_value(arena *storage, const char **lines)
{
    pin_value value = strings_value(storage, acount(lines));
    for (int i = 0; i < acount(lines); i++) {
        ((pin_array *)value.ptr)->items[i] = text_value(storage, lines[i]);
    }
    return value;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The sorted names in folder of its subfolders, or of its regular files
 * ending in extension; false when it cannot be read. */
static bool folder_names(const char *folder, const char *extension, char ***names)
{
    DIR *dir = opendir(folder);
    for (struct dirent *entry; dir && (entry = readdir(dir));) {
        char *path = join(folder, entry->d_name);
        struct stat info;
        size_t length = strlen(entry->d_name),
               suffix = extension ? strlen(extension) : 0;
        bool wanted = entry->d_name[0] != '.' && !lstat(path, &info) &&
                      (extension ? S_ISREG(info.st_mode) && length > suffix &&
                                       !strcmp(entry->d_name + length - suffix, extension)
                                 : S_ISDIR(info.st_mode));
        if (wanted) {
            apush(*names, xstrdup(entry->d_name));
        }
        free(path);
    }
    if (dir) {
        closedir(dir);
    }
    if (*names) {
        qsort(*names, (size_t)acount(*names), sizeof **names, compare_names);
    }
    return dir != NULL;
}

static void free_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

/* What renders a page's body: the files it reads, kept until the call that
 * renders them runs, and that call's arguments in storage. */
typedef struct
{
    arena storage;
    char **texts;       /* Owned file texts, split into lines. */
    char **paths;       /* Owned package file paths. */
    const char **lines; /* Every line, pointing into texts. */
    int *starts;        /* Where each package file's lines begin. */
    pin_value arguments[7];
    int count;
} docs_body;

static void free_body(docs_body *body)
{
    free_names(body->texts);
    free_names(body->paths);
    afree(body->lines);
    afree(body->starts);
    arena_free(&body->storage);
}

/* Reads a file inside root into body and splits it into lines; false with
 * why. */
static bool read_input(const char *root, const char *source, docs_body *body, char *error, size_t size)
{
    char *path = source_file(root, source);
    char *text = path ? file_read_text(path) : NULL;
    free(path);
    if (!text) {
        snprintf(error, size, "%s is not a readable file inside %s", source, root);
        return false;
    }
    apush(body->texts, text);
    return true;
}

/* Reads the .chibi files of each package folder in a folder inside root,
 * sorted, into body; false with why, also for a folder with no package
 * files. */
static bool read_packages(const char *root, const char *source, docs_body *body, char *error, size_t size)
{
    char *joined = join(root, source);
    char *folder = realpath(joined, NULL);
    free(joined);
    size_t length = strlen(root);
    char **packages = NULL;
    bool valid = folder && !strncmp(folder, root, length) &&
                 folder[length] == '/' && folder_names(folder, NULL, &packages);
    if (!valid) {
        snprintf(error, size, "%s is not a readable folder inside %s", source, root);
    }
    for (int i = 0; valid && i < acount(packages); i++) {
        char *package = join(folder, packages[i]);
        char **files = NULL;
        valid = folder_names(package, CODE_ASSET_EXTENSION, &files);
        if (!valid) {
            snprintf(error, size, "%s/%s is not a readable folder", source, packages[i]);
        }
        for (int j = 0; valid && j < acount(files); j++) {
            char *relative = join(packages[i], files[j]);
            char *within = join(source, relative);
            apush(body->starts, acount(body->lines));
            apush(body->paths, relative);
            valid = read_input(root, within, body, error, size);
            if (valid) {
                split_lines(body->texts[acount(body->texts) - 1], &body->lines);
            }
            free(within);
        }
        free_names(files);
        free(package);
    }
    if (valid && !acount(body->paths)) {
        snprintf(error, size, "%s holds no package files", source);
        valid = false;
    }
    free_names(packages);
    free(folder);
    return valid;
}

typedef struct
{
    /* Each kind's body and report of the links it writes as text. */
    const behavior_function *render[DOCS_KINDS], *unpublished[DOCS_KINDS];
    const behavior_function *head, *foot, *style;
    behavior_backend backend;
    const native_capabilities *native;
    const authoring_packages *packages;
    const char *core;
} docs_generator;

/* Reports a diagnostic of an example that starts after line `fence` of the
 * page `source`. */
static void example_error(
    const char *source,
    int fence,
    const script_diagnostic *diagnostic,
    char *error,
    size_t size
)
{
    const script_range *at = &diagnostic->range;
    snprintf(
        error,
        size,
        "%s:%d:%d: example: %s",
        source,
        fence + at->line,
        at->column,
        diagnostic->message
    );
}

typedef struct
{
    source_package_input file;
    int fence;
} docs_example;

/* Map package-local diagnostics back to their original fenced example. */
static void package_example_error(
    const char *source,
    const docs_example *examples,
    const char *path,
    const script_diagnostic *diagnostic,
    char *error,
    size_t size
)
{
    for (int i = 0; i < acount(examples); i++) {
        if (path && !strcmp(path, examples[i].file.path)) {
            example_error(source, examples[i].fence, diagnostic, error, size);
            return;
        }
    }
    snprintf(
        error,
        size,
        "%s:%d:%d: example dependency: %s",
        path ? path : source,
        diagnostic->range.line,
        diagnostic->range.column,
        diagnostic->message
    );
}

/* Each page is a package workspace. A package header selects its folder, so
 * sibling fences share declarations and relative imports use normal rules.
 * The shipped core sources remain in their real collection. */
static bool check_package_examples(
    const docs_generator *generator,
    const char *source,
    const docs_example *examples,
    char *error,
    size_t size
)
{
    if (!acount(examples)) {
        return true;
    }
    source_package_input *files = NULL;
    const source_package_snapshot *original = authoring_packages_snapshot(
        generator->packages
    );
    const source_packages *graph = source_package_snapshot_graph(original);
    int count;
    const script_package_document *documents = source_package_snapshot_documents(original, &count);
    for (int i = 0; i < source_packages_count(graph); i++) {
        if (strncmp(source_package_snapshot_identity(original, i), "core:", 5)) {
            continue;
        }
        const source_package *package = source_packages_at(graph, i);
        for (int j = 0; j < package->file_count; j++) {
            const script_package_document *document = &documents[package->files[j]];
            apush(
                files,
                ((source_package_input){ document->path,
                                         document->syntax->text,
                                         strlen(document->syntax->text) })
            );
        }
    }
    for (int i = 0; i < acount(examples); i++) {
        apush(files, examples[i].file);
    }
    char *core = realpath(generator->core, NULL);
    source_package_collection collections[] = { { "core", core } };
    source_package_snapshot_options options = { "/docs-examples",
                                                files,
                                                acount(files),
                                                collections,
                                                1 };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    free(core);
    const source_package_snapshot_error *failure = source_package_snapshot_error_at(snapshot);
    bool valid = !failure;
    if (failure) {
        documents = source_package_snapshot_documents(snapshot, &count);
        int at = failure->diagnostic.range.document;
        package_example_error(
            source,
            examples,
            at >= 0 && at < count ? documents[at].path : NULL,
            &failure->diagnostic,
            error,
            size
        );
    }
    authoring_packages *packages = NULL;
    if (valid) {
        script_lower_bindings bindings = authoring_native_bindings(generator->native);
        packages = authoring_packages_prepare(
            snapshot,
            native_capabilities_catalog(generator->native),
            &bindings,
            generator->backend,
            generator->packages
        );
        valid = authoring_packages_valid(packages);
        if (!valid) {
            snprintf(
                error,
                size,
                "%s: example: %s",
                source,
                authoring_packages_error(packages) ? authoring_packages_error(packages)
                                                   : "Invalid package example"
            );
            const source_packages *resolved = source_package_snapshot_graph(snapshot);
            documents = source_package_snapshot_documents(snapshot, &count);
            for (int i = 0; i < authoring_packages_count(packages); i++) {
                int index = source_packages_order(resolved, i);
                const authoring_package *package = authoring_packages_at(packages, index);
                if (!package->diagnostic_count) {
                    continue;
                }
                const script_diagnostic *diagnostic = &package->diagnostics[0];
                int at = diagnostic->range.document;
                const source_package *membership = source_packages_at(resolved, index);
                const char *path = at >= 0 && at < membership->file_count
                                     ? documents[membership->files[at]].path
                                     : NULL;
                package_example_error(source, examples, path, diagnostic, error, size);
                break;
            }
        }
    }
    if (packages) {
        authoring_packages_free(packages);
    } else {
        source_package_snapshot_free(snapshot);
    }
    afree(files);
    return valid;
}

/* Every chibi fence parses. Complete packages also resolve imports and compile
 * together, using the same package APIs and natives as standalone tests. */
static bool check_examples(
    const docs_generator *generator,
    const char *source,
    const char *text,
    char *error,
    size_t size
)
{
    static const char FENCE[] = "```chibi";
    int line = 1, fence = 0;
    bool inside = false, valid = true;
    char *code = NULL;
    docs_example *examples = NULL;
    for (const char *at = text; valid && *at; line++) {
        const char *end = strchr(at, '\n');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        if (inside && !strncmp(at, "```", 3)) {
            apush(code, '\0');
            script_syntax *syntax = script_parse(code);
            valid = !syntax->diagnostic_count;
            if (!valid) {
                example_error(source, fence, &syntax->diagnostics[0], error, size);
            } else {
                TSNode header = ts_node_child_by_field_name(
                    ts_tree_root_node(syntax->tree),
                    "package",
                    7
                );
                if (!ts_node_is_null(header)) {
                    TSNode name = ts_node_child_by_field_name(header, "name", 4);
                    script_range range = script_node_range(name);
                    char path[4096];
                    snprintf(
                        path,
                        sizeof path,
                        "/docs-examples/%.*s/Example%d.chibi",
                        range.end - range.start,
                        code + range.start,
                        fence
                    );
                    apush(
                        examples,
                        ((docs_example){ { xstrdup(path), xstrdup(code), strlen(code) }, fence })
                    );
                }
            }
            script_syntax_free(syntax);
            asetlen(code, 0);
            inside = false;
        } else if (inside) {
            for (size_t i = 0; i < length; i++) {
                apush(code, at[i]);
            }
            apush(code, '\n');
        } else if (length == strlen(FENCE) && !strncmp(at, FENCE, length)) {
            fence = line;
            inside = true;
        }
        at = end ? end + 1 : at + length;
    }
    valid = valid && check_package_examples(generator, source, examples, error, size);
    for (int i = 0; i < acount(examples); i++) {
        free((char *)examples[i].file.path);
        free((char *)examples[i].file.text);
    }
    afree(examples);
    afree(code);
    return valid;
}

/* Closes a written file, saying why it could not be finished. */
static bool finish(FILE *file, bool written, const char *name, char *error, size_t size)
{
    if (fclose(file) && written) {
        snprintf(error, size, "cannot write %s: %s", name, strerror(errno));
        return false;
    }
    return written;
}

/* Reads what a page's body needs and sets up the call that renders it;
 * false with why. */
static bool page_body(
    const docs_generator *generator,
    const pin_value *map,
    const char *root,
    const docs_page *page,
    docs_body *body,
    char *error,
    size_t size
)
{
    if (page->kind == DOCS_PACKAGES) {
        if (!read_packages(root, page->source, body, error, size)) {
            return false;
        }
        int count = acount(body->paths);
        pin_value paths = strings_value(&body->storage, count);
        pin_array *starts = pin_array_snapshot_allocate(&body->storage, PIN_I32, NULL, count);
        for (int i = 0; i < count; i++) {
            ((pin_array *)paths.ptr)->items[i] = text_value(&body->storage, body->paths[i]);
            starts->items[i] = (pin_value){ .type = PIN_I32, .i = body->starts[i] };
        }
        body->arguments[0] = text_value(&body->storage, page->collection);
        body->arguments[1] = paths;
        body->arguments[2] = (pin_value){ .type = PIN_I32,
                                          .container = PIN_CONTAINER_ARRAY,
                                          .ptr = starts };
        body->arguments[3] = lines_value(&body->storage, body->lines);
        body->arguments[4] = text_value(&body->storage, page->source);
        body->arguments[5] = map[4];
        body->arguments[6] = map[3];
        body->count = 7;
        return true;
    }
    if (!read_input(root, page->source, body, error, size)) {
        return false;
    }
    char *text = body->texts[0];
    if (page->kind == DOCS_MARKDOWN &&
        !check_examples(generator, page->source, text, error, size)) {
        return false;
    }
    split_lines(text, &body->lines);
    bool rules = false;
    for (int i = 0; i < acount(body->lines); i++) {
        rules = rules || !strcmp(body->lines[i], "  rules: {");
    }
    if (page->kind == DOCS_GRAMMAR && !rules) {
        snprintf(error, size, "%s has no `  rules: {` block", page->source);
        return false;
    }
    body->arguments[0] = lines_value(&body->storage, body->lines);
    body->arguments[1] = text_value(&body->storage, page->source);
    body->arguments[2] = map[4];
    body->arguments[3] = map[3];
    body->count = 4;
    return true;
}

/* Writes one page into folder: the layout around its rendered body. Then
 * reports on stderr each link the body writes as text, which renders the body
 * a second time, as each generator function returns a single string[]. */
static bool write_page(
    const docs_generator *generator,
    const pin_value *map,
    const char *root,
    const char *folder,
    const docs_page *page,
    int index,
    char *error,
    size_t size
)
{
    docs_body body = { 0 };
    bool written = page_body(generator, map, root, page, &body, error, size);
    char *target = join(folder, page->output);
    FILE *file = written ? fopen(target, "wb") : NULL;
    if (written && !file) {
        snprintf(error, size, "cannot write %s: %s", target, strerror(errno));
    }
    pin_value head[] = { map[0], map[1], map[2], map[3], { .type = PIN_I32, .i = index } };
    written = file &&
              generate(generator->head, head, 5, generator->backend, file, page->output, error, size) &&
              generate(
                  generator->render[page->kind],
                  body.arguments,
                  body.count,
                  generator->backend,
                  file,
                  page->output,
                  error,
                  size
              ) &&
              generate(generator->foot, NULL, 0, generator->backend, file, page->output, error, size) &&
              generate(
                  generator->unpublished[page->kind],
                  body.arguments,
                  body.count,
                  generator->backend,
                  stderr,
                  page->output,
                  error,
                  size
              );
    if (file) {
        written = finish(file, written, page->output, error, size);
    }
    free(target);
    free_body(&body);
    return written;
}

/* Deletes a folder this command wrote, without following links. */
static void remove_folder(const char *path)
{
    DIR *dir = opendir(path);
    for (struct dirent *entry; dir && (entry = readdir(dir));) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            char *child = join(path, entry->d_name);
            struct stat info;
            if (!lstat(child, &info) && S_ISDIR(info.st_mode)) {
                remove_folder(child);
            } else {
                unlink(child);
            }
            free(child);
        }
    }
    if (dir) {
        closedir(dir);
    }
    rmdir(path);
}

/* Every page and the stylesheet, written into folder. */
static bool write_pages(
    const docs_generator *generator,
    const char *root,
    const char *folder,
    const char *site,
    const docs_page *pages,
    char *error,
    size_t size
)
{
    /* The site, then the pages' sections, titles, outputs and sources. */
    arena storage = { 0 };
    int count = acount(pages);
    pin_value map[] = { text_value(&storage, site),
                        strings_value(&storage, count),
                        strings_value(&storage, count),
                        strings_value(&storage, count),
                        strings_value(&storage, count) };
    for (int i = 0; i < count; i++) {
        ((pin_array *)map[1].ptr)->items[i] = text_value(&storage, pages[i].section);
        ((pin_array *)map[2].ptr)->items[i] = text_value(&storage, pages[i].title);
        ((pin_array *)map[3].ptr)->items[i] = text_value(&storage, pages[i].output);
        ((pin_array *)map[4].ptr)->items[i] = text_value(&storage, pages[i].source);
    }
    bool valid = true;
    for (int i = 0; valid && i < count; i++) {
        valid = write_page(generator, map, root, folder, &pages[i], i, error, size);
    }
    char *style = join(folder, "style.css");
    FILE *file = valid ? fopen(style, "wb") : NULL;
    if (valid && !file) {
        snprintf(error, size, "cannot write %s: %s", style, strerror(errno));
    }
    valid = file &&
            generate(generator->style, NULL, 0, generator->backend, file, "style.css", error, size);
    if (file) {
        valid = finish(file, valid, "style.css", error, size);
    }
    arena_free(&storage);
    free(style);
    return valid;
}

/* Writes the site into a fresh folder beside root/build/docs and only then
 * replaces it, so the published folder never holds a partial site or pages
 * the manifest no longer lists. */
/* mkdtemp makes a private folder; give it the permissions mkdir would, so the
 * published site is as readable as the pages written into it. */
static bool publishable(const char *folder)
{
    mode_t mask = umask(0);
    umask(mask);
    return !chmod(folder, 0777 & ~mask);
}

static bool write_site(
    const docs_generator *generator,
    const char *root,
    const char *site,
    const docs_page *pages,
    char *error,
    size_t size
)
{
    char *build = join(root, "build");
    char *folder = join(build, "docs");
    char *partial = join(build, "docs.partial-XXXXXX");
    bool valid = (!mkdir(build, 0777) || errno == EEXIST) && mkdtemp(partial) &&
                 publishable(partial);
    if (!valid) {
        snprintf(error, size, "cannot create a folder in %s: %s", build, strerror(errno));
    }
    valid = valid && write_pages(generator, root, partial, site, pages, error, size);
    if (valid) {
        remove_folder(folder);
        valid = !rename(partial, folder);
        if (!valid) {
            snprintf(error, size, "cannot replace %s: %s", folder, strerror(errno));
        }
    }
    if (!valid) {
        remove_folder(partial);
    }
    if (valid) {
        for (int i = 0; i < acount(pages); i++) {
            printf("Wrote %s\n", pages[i].output);
        }
    }
    free(partial);
    free(folder);
    free(build);
    return valid;
}

int authoring_project_docs(const char *root, const char *generator_root, const char *core)
{
    char error[512] = { 0 };
    char *manifest = join(root, "docs/site.kdl");
    char *text = file_read_text(manifest);
    av_doc *doc = text ? av_doc_read(text, error, sizeof error) : NULL;
    const char *site = NULL;
    docs_page *pages = NULL;
    if (!text) {
        snprintf(error, sizeof error, "cannot read the site manifest");
    }
    bool valid = doc && read_site(doc, &site, &pages, error, sizeof error);
    if (!valid) {
        fprintf(stderr, "%s: %s\n", manifest, error);
    }
    native_capabilities *native = valid
                                    ? host_native_standalone_catalog_new(NULL, 0, error, sizeof error)
                                    : NULL;
    /* The private parser belongs to the generator, never to validated examples. */
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    native_capabilities *render_native = native ? native_capabilities_from_definition_packs(
                                                      NULL,
                                                      0,
                                                      packs,
                                                      3,
                                                      error,
                                                      sizeof error
                                                  )
                                                : NULL;
    authoring_packages *packages = NULL;
    if (render_native) {
        source_package_collection collections[] = { { "core", core } };
        source_package_disk_options options = { generator_root, collections, 1 };
        source_package_load_error load = { 0 };
        source_package_snapshot *snapshot = source_package_snapshot_load(&options, &load);
        if (!snapshot) {
            fprintf(stderr, "%s: %s\n", load.path, load.message);
        }
        script_lower_bindings bindings = authoring_native_bindings(render_native);
        packages = snapshot ? authoring_packages_prepare(
                                  snapshot,
                                  native_capabilities_catalog(render_native),
                                  &bindings,
                                  behavior_default_backend(),
                                  NULL
                              )
                            : NULL;
    } else if (valid) {
        fprintf(stderr, "Site generator natives failed: %s\n", error);
    }
    valid = packages && authoring_packages_valid(packages);
    if (packages && !valid) {
        print_invalid(packages);
    }
    if (valid) {
        static const char *const identities[] = {
            [DOCS_MARKDOWN] = "project:markdown",
            [DOCS_PACKAGES] = "project:reference",
            [DOCS_GRAMMAR] = "project:grammar"
        };
        static const int parameters[] = { [DOCS_MARKDOWN] = 4,
                                          [DOCS_PACKAGES] = 7,
                                          [DOCS_GRAMMAR] = 4 };
        docs_generator generator = {
            .head = generator_function(packages, "project:site", "Head", 5),
            .foot = generator_function(packages, "project:site", "Foot", 0),
            .style = generator_function(packages, "project:site", "Style", 0),
            .backend = behavior_default_backend(),
            .native = native,
            .packages = packages,
            .core = core
        };
        for (int kind = 0; kind < DOCS_KINDS; kind++) {
            generator.render[kind] = generator_function(
                packages,
                identities[kind],
                "Render",
                parameters[kind]
            );
            generator.unpublished[kind] = generator_function(
                packages,
                identities[kind],
                "Unpublished",
                parameters[kind]
            );
            assert(generator.render[kind] && generator.unpublished[kind]);
        }
        assert(generator.head && generator.foot && generator.style);
        valid = write_site(&generator, root, site, pages, error, sizeof error);
        if (!valid) {
            fprintf(stderr, "%s\n", error);
        }
    }
    authoring_packages_free(packages);
    native_capabilities_free(render_native);
    native_capabilities_free(native);
    afree(pages);
    av_doc_free(doc);
    free(text);
    free(manifest);
    return valid ? 0 : 1;
}
