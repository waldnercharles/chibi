#include "authoring/package_compile.h"

#include "authoring/asset_compiler_host.h"
#include "runtime/native_prepared.h"
#include "support/xalloc.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_target.h"

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

static package_collection *read_closure(
    const unsigned char *closure,
    size_t length,
    const native_capabilities *native,
    behavior_backend backend,
    package_compile_error *error
)
{
    native_prepared_context context = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    linker.decoded_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES;
    linker.decoded_records = COMPILER_HOST_MAX_CLOSURE_RECORDS;
    linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char reason[256] = { 0 };
    package_collection *packages = compiler_artifact_read(closure, length, &linker, backend, &remaining, &root, reason, sizeof reason);
    if (!packages) {
        fail(error, NULL, 0, 0, reason);
    }
    return packages;
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
    int first = first_path ? -1 : top_level_file(inputs);
    if (!first_path && first < 0) {
        fail(error, NULL, 0, 0, "The project folder holds no source file");
        return NULL;
    }
    bool virtual_file = first_path != NULL;
    int count = inputs->file_count + (virtual_file ? 1 : 0);
    const char **paths = xmalloc((size_t)count * sizeof *paths);
    const char **texts = xmalloc((size_t)count * sizeof *texts);
    size_t *sizes = xmalloc((size_t)count * sizeof *sizes);
    int at = 0;
    if (virtual_file) {
        paths[at] = first_path;
        texts[at] = first_text;
        sizes[at++] = strlen(first_text);
    }
    for (int i = -1; i < inputs->file_count; i++) {
        int index = i < 0 ? first : i;
        if (index < 0 || (i >= 0 && i == first)) {
            continue;
        }
        paths[at] = inputs->files[index].path;
        texts[at] = inputs->files[index].text;
        sizes[at++] = inputs->files[index].size;
    }
    const char **names = xmalloc((size_t)(inputs->collection_count + 1) * sizeof *names);
    const char **roots = xmalloc((size_t)(inputs->collection_count + 1) * sizeof *roots);
    for (int i = 0; i < inputs->collection_count; i++) {
        names[i] = inputs->collections[i].name;
        roots[i] = inputs->collections[i].root;
    }
    size_t request_length = 0;
    char *request = compiler_package_request_json(
        inputs->project_root,
        names,
        roots,
        inputs->collection_count,
        paths,
        texts,
        sizes,
        count,
        native_capabilities_catalog(native),
        &request_length
    );
    package_collection *packages = NULL;
    if (!request) {
        fail(error, NULL, 0, 0, "The native catalog cannot be described to the compiler");
    } else {
        asset_compiler_diagnostic diagnostic;
        size_t length = 0;
        unsigned char *closure = asset_compiler_compile_closure(request, request_length, &length, NULL, &diagnostic);
        if (closure) {
            packages = read_closure(closure, length, native, backend, error);
        } else {
            bool known = diagnostic.document >= 0 && diagnostic.document < count;
            fail(error, known ? paths[diagnostic.document] : NULL, diagnostic.line, diagnostic.column, diagnostic.message);
        }
        free(closure);
    }
    free(request);
    free(roots);
    free(names);
    free(sizes);
    free(texts);
    free(paths);
    return packages;
}
