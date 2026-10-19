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
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "types/array.h"

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
int authoring_project_compile(const char *project, const char *scene, const char *output)
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
                                          .read_only = !output };
    bool valid = authoring_project_prepare(&options, &artifact, error, sizeof error);
    authoring_application_end(&application);
    if (valid && output) {
        valid = write_artifact(output, &artifact, error, sizeof error);
    }
    if (valid && output) {
        printf("Prepared project: %s (%zu bytes)\n", output, artifact.size);
    } else if (valid) {
        printf("Checked project: %s\n", project);
    } else {
        fprintf(stderr, "Project preparation failed: %s\n", error);
    }
    authoring_project_artifact_free(&artifact);
    return valid ? 0 : 1;
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
static int run_tests(const authoring_packages *packages, behavior_backend backend)
{
    package_test *tests = package_tests_find(packages);
    int failed = 0;
    for (int i = 0; i < acount(tests); i++) {
        package_test_failure failure;
        bool passed = package_test_run(&tests[i], backend, PACKAGE_TEST_BUDGET, &failure);
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
    printf("%d tests, %d failed\n", acount(tests), failed);
    afree(tests);
    return failed;
}
/* Tests run without a game, so they get the natives that need none of it. */
int authoring_project_test(const char *root, const char *core)
{
    char error[512] = { 0 };
    native_capabilities *native = host_native_standalone_catalog_new(error, sizeof error);
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
        status = run_tests(packages, backend) ? 1 : 0;
    } else {
        print_invalid(packages);
    }
    authoring_packages_free(packages);
    native_capabilities_free(native);
    return status;
}

/* Instructions one call of the site generator may run. */
#define DOCS_BUDGET 1000000000u

typedef struct
{
    const char *section, *title, *source, *output;
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
            docs_page page = { name,
                               av_get_str(av_obj_get(entry, "title")),
                               av_get_str(av_obj_get(entry, "source")),
                               av_get_str(av_obj_get(entry, "output")) };
            if (!page.title || !page.source || !page.output || !page_name(page.output)) {
                snprintf(error, size, "page %zu of %s needs a title, a source and an .html output", j + 1, name);
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
    behavior_function_context context = { .activation = resumable ? &activation : NULL,
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
        const char *reason = context.error_message ? context.error_message
                           : context.failure == BEHAVIOR_FUNCTION_BUDGET
                               ? "instruction budget exhausted"
                               : "execution failed";
        snprintf(error, size, "the site generator failed on %s: %s", name, reason);
    }
    const pin_array *pieces = result.ptr;
    bool written = generated;
    for (int i = 0; written && i < pieces->count; i++) {
        written = fputs(pieces->items[i].str, file) >= 0;
    }
    if (generated && !written) {
        snprintf(error, size, "cannot write %s: %s", name, strerror(errno));
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    return written;
}

static pin_value text_value(const char *text)
{
    return (pin_value){ .type = PIN_STR, .str = text };
}

/* An empty string[] argument of count items in storage. */
static pin_value strings_value(arena *storage, int count)
{
    pin_array *array = pin_array_snapshot_allocate(storage, PIN_STR, NULL, count);
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}

/* text's lines, without their line breaks, as a string[] argument whose
 * strings point into text, which this splits in place. */
static pin_value lines_value(arena *storage, char *text)
{
    int count = *text ? 1 : 0;
    for (const char *c = text; *c; c++) {
        count += *c == '\n' && c[1];
    }
    pin_value lines = strings_value(storage, count);
    pin_array *array = lines.ptr;
    for (int i = 0; i < count; i++) {
        char *end = strchr(text, '\n');
        if (end) {
            *end = '\0';
        }
        array->items[i] = text_value(text);
        text = end ? end + 1 : text + strlen(text);
    }
    return lines;
}

typedef struct
{
    const behavior_function *render, *head, *foot, *style;
    behavior_backend backend;
    const script_catalog *catalog; /* Names a published example may use. */
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

/* One example: it parses, and a package also analyzes. */
static bool check_example(
    const char *source,
    int fence,
    const char *code,
    const script_catalog *catalog,
    char *error,
    size_t size
)
{
    script_syntax *syntax = script_parse(code);
    bool valid = !syntax->diagnostic_count;
    if (!valid) {
        example_error(source, fence, &syntax->diagnostics[0], error, size);
    }
    if (valid && !strncmp(code, "package ", strlen("package "))) {
        script_package_document document = { source, source, syntax };
        const script_package_options options = { .documents = &document,
                                                 .document_count = 1,
                                                 .identity = "project:example",
                                                 .native = catalog };
        script_analysis *analysis = script_analyze_package(&options);
        valid = !analysis->diagnostic_count;
        if (!valid) {
            example_error(source, fence, &analysis->diagnostics[0], error, size);
        }
        script_analysis_free(analysis);
    }
    script_syntax_free(syntax);
    return valid;
}

/* Every example fenced as chibi on a page parses, and one that is a whole
 * package also analyzes against the natives a test may call, so published
 * examples stay true to the language. */
static bool check_examples(
    const char *source,
    const char *text,
    const script_catalog *catalog,
    char *error,
    size_t size
)
{
    static const char FENCE[] = "```chibi";
    int line = 1, fence = 0;
    bool inside = false, valid = true;
    char *code = NULL;
    for (const char *at = text; valid && *at; line++) {
        const char *end = strchr(at, '\n');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        if (inside && !strncmp(at, "```", 3)) {
            apush(code, '\0');
            valid = check_example(source, fence, code, catalog, error, size);
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

/* Writes one page into folder: the layout around its rendered source. */
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
    char *source = source_file(root, page->source);
    char *text = source ? file_read_text(source) : NULL;
    if (!text) {
        snprintf(error, size, "%s is not a readable file inside %s", page->source, root);
        free(source);
        return false;
    }
    if (!check_examples(page->source, text, generator->catalog, error, size)) {
        free(text);
        free(source);
        return false;
    }
    char *target = join(folder, page->output);
    FILE *file = fopen(target, "wb");
    if (!file) {
        snprintf(error, size, "cannot write %s: %s", target, strerror(errno));
    }
    arena storage = { 0 };
    pin_value head[] = { map[0], map[1], map[2], map[3], { .type = PIN_I32, .i = index } };
    pin_value render[] = { lines_value(&storage, text),
                           text_value(page->source),
                           map[4],
                           map[3] };
    bool written = file &&
                   generate(generator->head, head, 5, generator->backend, file, page->output, error, size) &&
                   generate(generator->render, render, 4, generator->backend, file, page->output, error, size) &&
                   generate(generator->foot, NULL, 0, generator->backend, file, page->output, error, size);
    if (file) {
        written = finish(file, written, page->output, error, size);
    }
    arena_free(&storage);
    free(target);
    free(text);
    free(source);
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
    pin_value map[] = { text_value(site),
                        strings_value(&storage, count),
                        strings_value(&storage, count),
                        strings_value(&storage, count),
                        strings_value(&storage, count) };
    for (int i = 0; i < count; i++) {
        ((pin_array *)map[1].ptr)->items[i] = text_value(pages[i].section);
        ((pin_array *)map[2].ptr)->items[i] = text_value(pages[i].title);
        ((pin_array *)map[3].ptr)->items[i] = text_value(pages[i].output);
        ((pin_array *)map[4].ptr)->items[i] = text_value(pages[i].source);
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
    bool valid = (!mkdir(build, 0777) || errno == EEXIST) && mkdtemp(partial);
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
                                    ? host_native_standalone_catalog_new(error, sizeof error)
                                    : NULL;
    authoring_packages *packages = NULL;
    if (native) {
        source_package_collection collections[] = { { "core", core } };
        source_package_disk_options options = { generator_root, collections, 1 };
        source_package_load_error load = { 0 };
        source_package_snapshot *snapshot = source_package_snapshot_load(&options, &load);
        if (!snapshot) {
            fprintf(stderr, "%s: %s\n", load.path, load.message);
        }
        script_lower_bindings bindings = authoring_native_bindings(native);
        packages = snapshot ? authoring_packages_prepare(
                                  snapshot,
                                  native_capabilities_catalog(native),
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
        docs_generator generator = {
            generator_function(packages, "project:markdown", "Render", 4),
            generator_function(packages, "project:site", "Head", 5),
            generator_function(packages, "project:site", "Foot", 0),
            generator_function(packages, "project:site", "Style", 0),
            behavior_default_backend(),
            native_capabilities_catalog(native)
        };
        assert(generator.render && generator.head && generator.foot && generator.style);
        valid = write_site(&generator, root, site, pages, error, sizeof error);
        if (!valid) {
            fprintf(stderr, "%s\n", error);
        }
    }
    authoring_packages_free(packages);
    native_capabilities_free(native);
    afree(pages);
    av_doc_free(doc);
    free(text);
    free(manifest);
    return valid ? 0 : 1;
}
