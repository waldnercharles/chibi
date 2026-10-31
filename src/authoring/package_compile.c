#include "authoring/package_compile.h"

#include "authoring/asset_compiler_host.h"
#include "runtime/native_prepared.h"
#include "support/array.h"
#include "support/sha256.h"
#include "support/xalloc.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_target.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(package_compile_error *error, const char *path, int line, int column, const char *message)
{
    snprintf(error->path, sizeof error->path, "%s", path ? path : "");
    error->line = line;
    error->column = column;
    snprintf(error->message, sizeof error->message, "%s", message);
}

/* The index of the first input file directly in the project root, or -1. */
static int top_level_file(const source_package_snapshot_options *inputs)
{
    size_t root = strlen(inputs->project_root);
    for (int i = 0; i < inputs->file_count; i++) {
        const char *path = inputs->files[i].path;
        if (!strncmp(path, inputs->project_root, root) && path[root] == '/' &&
            !strchr(path + root + 1, '/')) {
            return i;
        }
    }
    return -1;
}

package_collection *package_compile_read(
    const unsigned char *closure,
    size_t length,
    const native_capabilities *native,
    behavior_backend backend,
    package_compile_error *error
)
{
    *error = (package_compile_error){ 0 };
    native_prepared_context context = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    linker.decoded_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES;
    linker.decoded_records = COMPILER_HOST_MAX_CLOSURE_RECORDS;
    linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char reason[256] = { 0 };
    package_collection *packages = compiler_artifact_read(
        closure,
        length,
        &linker,
        backend,
        &remaining,
        &root,
        reason,
        sizeof reason
    );
    if (!packages) {
        fail(error, NULL, 0, 0, reason);
    }
    return packages;
}

/* A package is the files of one directory. A compile reads only the packages
 * the root package reaches through `import NAME "COLLECTION:PATH"` lines and
 * relative `import NAME "PATH"` lines, as every file of a snapshot costs the
 * compiler parsing and graph work. */
static char *directory_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    size_t length = slash ? (size_t)(slash - path) : 0;
    char *copy = xmalloc(length + 1);
    memcpy(copy, path, length);
    copy[length] = 0;
    return copy;
}

static bool contains(char *const *directories, const char *directory)
{
    for (int i = 0; i < acount(directories); i++) {
        if (!strcmp(directories[i], directory)) {
            return true;
        }
    }
    return false;
}

/* `path` (`length` bytes) below the directory `base`, its `.` and `..`
 * segments resolved lexically, as the compiler resolves an import; a
 * directory reads as directory_of writes it. NULL when `..` climbs above the
 * first segment, which the compiler rejects too. Malloc-owned. */
static char *resolve_directory(const char *base, const char *path, size_t length)
{
    size_t used = strlen(base);
    /* Each segment appends at most its bytes and one slash. */
    char *directory = xmalloc(used + length + 2);
    memcpy(directory, base, used);
    for (const char *segment = path, *end = path + length; segment < end;) {
        const char *slash = memchr(segment, '/', (size_t)(end - segment));
        size_t size = (size_t)((slash ? slash : end) - segment);
        if (size == 2 && segment[0] == '.' && segment[1] == '.') {
            while (used && directory[used - 1] != '/') {
                used--;
            }
            if (!used) {
                free(directory);
                return NULL;
            }
            used--;
        } else if (size && !(size == 1 && segment[0] == '.')) {
            directory[used++] = '/';
            memcpy(directory + used, segment, size);
            used += size;
        }
        segment = slash ? slash + 1 : end;
    }
    directory[used] = 0;
    return directory;
}

/* The directory an import names: `COLLECTION:PATH` below that collection's
 * root, or a relative PATH below the importing package's directory. NULL for
 * an unknown collection, which the compiler then rejects. Malloc-owned. */
static char *imported_directory(
    const source_package_snapshot_options *inputs,
    const char *importer,
    const char *target,
    size_t length
)
{
    const char *colon = memchr(target, ':', length);
    if (!colon) {
        return resolve_directory(importer, target, length);
    }
    size_t name_length = (size_t)(colon - target);
    const char *root = NULL;
    if (name_length == strlen("project") && !memcmp(target, "project", name_length)) {
        root = inputs->project_root;
    }
    for (int i = 0; i < inputs->collection_count; i++) {
        if (strlen(inputs->collections[i].name) == name_length &&
            !memcmp(inputs->collections[i].name, target, name_length)) {
            root = inputs->collections[i].root;
        }
    }
    return root ? resolve_directory(root, colon + 1, length - name_length - 1) : NULL;
}

/* Adds the directories the imports of `text`, a file of the package in
 * `importer`, name. */
static void reach_imports(
    const source_package_snapshot_options *inputs,
    const char *importer,
    const char *text,
    size_t size,
    char ***reached
)
{
    const char *end = text + size;
    for (const char *line = text; line < end;) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        const char *stop = next ? next : end;
        const char *at = line;
        while (at < stop && (*at == ' ' || *at == '\t')) {
            at++;
        }
        if (stop - at > 7 && !memcmp(at, "import", 6) &&
            (at[6] == ' ' || at[6] == '\t')) {
            const char *open = memchr(at, '"', (size_t)(stop - at));
            const char *close = open
                                  ? memchr(open + 1, '"', (size_t)(stop - open - 1))
                                  : NULL;
            char *directory = close ? imported_directory(
                                          inputs,
                                          importer,
                                          open + 1,
                                          (size_t)(close - open - 1)
                                      )
                                    : NULL;
            if (directory && !contains(*reached, directory)) {
                apush(*reached, directory);
            } else {
                free(directory);
            }
        }
        line = next ? next + 1 : end;
    }
}

/* Marks the input files of the packages reachable from `root_directory`,
 * whose own text is `first_text` when the root file is virtual. */
static bool *reachable_files(
    const source_package_snapshot_options *inputs,
    const char *root_directory,
    const char *first_text
)
{
    char **directories = xmalloc((size_t)(inputs->file_count + 1) * sizeof *directories);
    for (int i = 0; i < inputs->file_count; i++) {
        directories[i] = directory_of(inputs->files[i].path);
    }
    char **reached = NULL;
    apush(reached, xstrdup(root_directory));
    if (first_text) {
        reach_imports(inputs, root_directory, first_text, strlen(first_text), &reached);
    }
    for (int at = 0; at < acount(reached); at++) {
        /* Appending may move the array, never the directory strings. */
        const char *importer = reached[at];
        for (int i = 0; i < inputs->file_count; i++) {
            if (!strcmp(directories[i], importer)) {
                reach_imports(
                    inputs,
                    importer,
                    inputs->files[i].text,
                    inputs->files[i].size,
                    &reached
                );
            }
        }
    }
    bool *keep = xcalloc((size_t)(inputs->file_count + 1), sizeof *keep);
    for (int i = 0; i < inputs->file_count; i++) {
        keep[i] = contains(reached, directories[i]);
        free(directories[i]);
    }
    for (int i = 0; i < acount(reached); i++) {
        free(reached[i]);
    }
    afree(reached);
    free(directories);
    return keep;
}

#ifdef _WIN32
static char *collections_salt(const source_package_snapshot_options *)
{
    return NULL;
}
#else
#include <sys/stat.h>
/* Adds the path, size and modification time of every file below `directory`
 * with an inode number, in sorted order: the compile reads collection files the
 * request only names by root, so the key must change when one of them does. */
static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static void add_tree(sha256 *state, const char *directory)
{
    DIR *open = opendir(directory);
    if (!open) {
        return;
    }
    char **names = NULL;
    for (struct dirent *entry; (entry = readdir(open));) {
        if (entry->d_name[0] != '.') {
            apush(names, xstrdup(entry->d_name));
        }
    }
    closedir(open);
    qsort(names, (size_t)acount(names), sizeof *names, by_name);
    for (int i = 0; i < acount(names); i++) {
        size_t size = strlen(directory) + strlen(names[i]) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s/%s", directory, names[i]);
        struct stat info;
        if (!stat(path, &info)) {
            if (S_ISDIR(info.st_mode)) {
                sha256_add(state, path, strlen(path) + 1);
                add_tree(state, path);
            } else {
                char line[96];
                int length = snprintf(
                    line,
                    sizeof line,
                    " %lld %lld %llu\n",
                    (long long)info.st_size,
                    (long long)info.st_mtime,
                    (unsigned long long)info.st_ino
                );
                sha256_add(state, path, strlen(path) + 1);
                sha256_add(state, line, (size_t)length);
            }
        }
        free(path);
        free(names[i]);
    }
    afree(names);
}
static char *collections_salt(const source_package_snapshot_options *inputs)
{
    sha256 state;
    sha256_begin(&state);
    for (int i = 0; i < inputs->collection_count; i++) {
        sha256_add(
            &state,
            inputs->collections[i].name,
            strlen(inputs->collections[i].name) + 1
        );
        add_tree(&state, inputs->collections[i].root);
    }
    char *digest = xmalloc(65);
    sha256_end(&state, digest);
    return digest;
}
#endif

void package_compile_source_path(
    const char *project_root,
    const source_package_collection *collections,
    int collection_count,
    const char *label,
    char *out,
    size_t size
)
{
    const char *colon = strchr(label, ':');
    size_t name = colon ? (size_t)(colon - label) : 0;
    if (colon && name == strlen("project") && !memcmp(label, "project", name)) {
        snprintf(out, size, "%s/%s", project_root, colon + 1);
        return;
    }
    for (int i = 0; colon && i < collection_count; i++) {
        if (name == strlen(collections[i].name) &&
            !memcmp(label, collections[i].name, name)) {
            snprintf(out, size, "%s/%s", collections[i].root, colon + 1);
            return;
        }
    }
    snprintf(out, size, "%s", label);
}

/* The files a compile sends: the root package's and those of the packages it
 * reaches, borrowed from the inputs and the virtual first file. */
typedef struct
{
    const char **paths, **texts;
    size_t *sizes;
    int count;
} compile_files;

static void compile_files_free(compile_files *files)
{
    free(files->sizes);
    free(files->texts);
    free(files->paths);
}

static bool select_files(
    const source_package_snapshot_options *inputs,
    const char *first_path,
    const char *first_text,
    compile_files *files,
    package_compile_error *error
)
{
    int first = first_path ? -1 : top_level_file(inputs);
    if (!first_path && first < 0) {
        fail(error, NULL, 0, 0, "The project folder holds no source file");
        return false;
    }
    bool virtual_file = first_path != NULL;
    char *root_directory = directory_of(
        virtual_file ? first_path : inputs->files[first].path
    );
    bool *keep = reachable_files(inputs, root_directory, virtual_file ? first_text : NULL);
    free(root_directory);
    int count = virtual_file ? 1 : 0;
    for (int i = 0; i < inputs->file_count; i++) {
        count += keep[i];
    }
    files->paths = xmalloc((size_t)count * sizeof *files->paths);
    files->texts = xmalloc((size_t)count * sizeof *files->texts);
    files->sizes = xmalloc((size_t)count * sizeof *files->sizes);
    int at = 0;
    if (virtual_file) {
        files->paths[at] = first_path;
        files->texts[at] = first_text;
        files->sizes[at++] = strlen(first_text);
    }
    for (int i = -1; i < inputs->file_count; i++) {
        int index = i < 0 ? first : i;
        if (index < 0 || (i >= 0 && (i == first || !keep[i]))) {
            continue;
        }
        files->paths[at] = inputs->files[index].path;
        files->texts[at] = inputs->files[index].text;
        files->sizes[at++] = inputs->files[index].size;
    }
    files->count = count;
    free(keep);
    return true;
}

static char *request_json(
    const source_package_snapshot_options *inputs,
    const compile_files *files,
    const native_capabilities *native,
    size_t *length,
    package_compile_error *error
)
{
    const char **names = xmalloc((size_t)(inputs->collection_count + 1) * sizeof *names);
    const char **roots = xmalloc((size_t)(inputs->collection_count + 1) * sizeof *roots);
    for (int i = 0; i < inputs->collection_count; i++) {
        names[i] = inputs->collections[i].name;
        roots[i] = inputs->collections[i].root;
    }
    char *request = compiler_package_catalog_request_json(
        inputs->project_root,
        names,
        roots,
        inputs->collection_count,
        files->paths,
        files->texts,
        files->sizes,
        files->count,
        native_capabilities_catalog(native),
        native,
        length
    );
    if (!request) {
        fail(error, NULL, 0, 0, "The native catalog cannot be described to the compiler");
    }
    free(roots);
    free(names);
    return request;
}

char *package_compile_request(
    const source_package_snapshot_options *inputs,
    const native_capabilities *native,
    size_t *length,
    package_compile_error *error
)
{
    *error = (package_compile_error){ 0 };
    compile_files files;
    if (!select_files(inputs, NULL, NULL, &files, error)) {
        return NULL;
    }
    char *request = request_json(inputs, &files, native, length, error);
    compile_files_free(&files);
    return request;
}

package_collection *package_compile(
    const source_package_snapshot_options *inputs,
    const char *first_path,
    const char *first_text,
    const native_capabilities *native,
    behavior_backend backend,
    package_compile_error *error
)
{
    *error = (package_compile_error){ 0 };
    compile_files files;
    if (!select_files(inputs, first_path, first_text, &files, error)) {
        return NULL;
    }
    size_t request_length = 0;
    char *request = request_json(inputs, &files, native, &request_length, error);
    package_collection *packages = NULL;
    if (request) {
        asset_compiler_diagnostic diagnostic;
        size_t length = 0;
        char *salt = collections_salt(inputs);
        unsigned char *closure = asset_compiler_compile_closure_cached(
            request,
            request_length,
            salt,
            &length,
            NULL,
            &diagnostic
        );
        free(salt);
        if (closure) {
            packages = package_compile_read(closure, length, native, backend, error);
        } else {
            bool known = diagnostic.document >= 0 && diagnostic.document < files.count;
            fail(
                error,
                known ? files.paths[diagnostic.document] : NULL,
                diagnostic.line,
                diagnostic.column,
                diagnostic.message
            );
        }
        free(closure);
    }
    free(request);
    compile_files_free(&files);
    return packages;
}
