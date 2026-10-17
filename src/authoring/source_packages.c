#include "authoring/source_packages.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *name;
    int file, package, import;
} import_binding;
struct source_packages
{
    source_package *packages;
    int *members, *file_packages, *order, *edges;
    import_binding *bindings;
    int count, file_count, binding_count;
};

void source_packages_free(source_packages *p)
{
    if (!p) {
        return;
    }
    free(p->packages);
    free(p->members);
    free(p->file_packages);
    free(p->order);
    free(p->edges);
    free(p->bindings);
    free(p);
}
static source_packages *reject(source_packages *p, source_package_error *error, source_package_code code)
{
    error->code = code;
    source_packages_free(p);
    return NULL;
}
static int find_directory(const source_packages *p, const char *directory)
{
    for (int i = 0; i < p->count; i++) {
        if (!strcmp(p->packages[i].directory, directory)) {
            return i;
        }
    }
    return -1;
}
static bool identifier(const char *name)
{
    unsigned char c = (unsigned char)*name++;
    if (c != '_' && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z')) {
        return false;
    }
    while ((c = (unsigned char)*name++)) {
        if (c != '_' && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9')) {
            return false;
        }
    }
    return true;
}
static const char *directory_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
static int binding_at(const source_packages *p, int file, const char *name)
{
    int lo = 0, hi = p->binding_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        const import_binding *binding = &p->bindings[mid];
        int cmp = file < binding->file ? -1
                : file > binding->file ? 1
                                       : strcmp(name, binding->name);
        if (!cmp) {
            return mid;
        }
        if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return -1;
}
static int compare_bindings(const void *left, const void *right)
{
    const import_binding *a = left, *b = right;
    if (a->file != b->file) {
        return a->file < b->file ? -1 : 1;
    }
    int names = strcmp(a->name, b->name);
    if (names) {
        return names;
    }
    return (a->import > b->import) - (a->import < b->import);
}
static bool unique_bindings(source_packages *p, source_package_error *error)
{
    qsort(p->bindings, (size_t)p->binding_count, sizeof *p->bindings, compare_bindings);
    int first = -1;
    for (int i = 1; i < p->binding_count; i++) {
        const import_binding *a = &p->bindings[i - 1], *b = &p->bindings[i];
        if (a->file == b->file && !strcmp(a->name, b->name) &&
            (first < 0 || b->import < p->bindings[first].import)) {
            first = i;
        }
    }
    if (first < 0) {
        return true;
    }
    error->file = error->related_file = p->bindings[first].file;
    error->import = p->bindings[first].import;
    error->related_import = p->bindings[first - 1].import;
    return false;
}
static void sort_members(source_packages *p, const source_package_file *files)
{
    int offset = 0;
    for (int i = 0; i < p->count; i++) {
        source_package *package = &p->packages[i];
        int *members = p->members + offset;
        int count = 0;
        for (int j = 0; j < p->file_count; j++) {
            if (p->file_packages[j] != i) {
                continue;
            }
            int position = count++;
            while (position > 0 &&
                   strcmp(files[members[position - 1]].path, files[j].path) > 0) {
                members[position] = members[position - 1];
                position--;
            }
            members[position] = j;
        }
        package->files = members;
        package->file_count = count;
        offset += count;
    }
    assert(offset == p->file_count);
}
static void find_cycle(
    const source_packages *p,
    const bool *emitted,
    const source_package_import *imports,
    source_package_error *error
)
{
    int seen[SOURCE_PACKAGE_MAX_FILES], path[SOURCE_PACKAGE_MAX_FILES];
    for (int i = 0; i < p->count; i++) {
        seen[i] = -1;
    }
    int current = 0, count = 0;
    while (emitted[current]) {
        current++;
    }
    while (seen[current] < 0) {
        seen[current] = count;
        int next = -1;
        for (int i = 0; i < p->count; i++) {
            if (i != current && !emitted[i] && p->edges[current * p->count + i] >= 0 &&
                (next < 0 ||
                 strcmp(p->packages[i].directory, p->packages[next].directory) < 0)) {
                next = i;
            }
        }
        /* Kahn's residual has an outgoing non-self edge at every node. */
        assert(next >= 0 && count < p->count);
        path[count++] = p->edges[current * p->count + next];
        current = next;
    }
    int start = seen[current];
    error->cycle_count = count - start;
    assert(error->cycle_count > 1);
    memcpy(error->cycle, path + start, (size_t)error->cycle_count * sizeof *path);
    error->import = error->cycle[0];
    error->file = imports[error->import].file;
    error->related_import = error->cycle[error->cycle_count - 1];
    error->related_file = imports[error->related_import].file;
}
static bool order_packages(source_packages *p, const source_package_import *imports, source_package_error *error)
{
    int dependencies[SOURCE_PACKAGE_MAX_FILES] = { 0 };
    bool emitted[SOURCE_PACKAGE_MAX_FILES] = { 0 };
    for (int i = 0; i < p->count; i++) {
        for (int j = 0; j < p->count; j++) {
            dependencies[i] += i != j && p->edges[i * p->count + j] >= 0;
        }
    }
    for (int ordinal = 0; ordinal < p->count; ordinal++) {
        int next = -1;
        for (int i = 0; i < p->count; i++) {
            if (!emitted[i] && !dependencies[i] &&
                (next < 0 ||
                 strcmp(p->packages[i].directory, p->packages[next].directory) < 0)) {
                next = i;
            }
        }
        if (next < 0) {
            find_cycle(p, emitted, imports, error);
            return false;
        }
        emitted[next] = true;
        p->order[ordinal] = next;
        for (int i = 0; i < p->count; i++) {
            if (!emitted[i] && p->edges[i * p->count + next] >= 0) {
                dependencies[i]--;
            }
        }
    }
    return true;
}

source_packages *source_packages_new(
    const source_package_file *files,
    int file_count,
    const source_package_import *imports,
    int import_count,
    source_package_error *error
)
{
    assert(error && file_count >= 0 && file_count <= SOURCE_PACKAGE_MAX_FILES);
    assert(import_count >= 0 && (files || !file_count) && (imports || !import_count));
    *error = (source_package_error){ .file = -1,
                                     .import = -1,
                                     .related_file = -1,
                                     .related_import = -1 };
    source_packages *p = xcalloc(1, sizeof *p);
    p->file_count = file_count;
    p->packages = xcalloc((size_t)file_count, sizeof *p->packages);
    p->members = xcalloc((size_t)file_count, sizeof *p->members);
    p->file_packages = xcalloc((size_t)file_count, sizeof *p->file_packages);
    p->order = xcalloc((size_t)file_count, sizeof *p->order);
    p->bindings = xcalloc((size_t)import_count, sizeof *p->bindings);
    int first_files[SOURCE_PACKAGE_MAX_FILES];
    for (int i = 0; i < file_count; i++) {
        const source_package_file *file = &files[i];
        assert(file->path && *file->path && file->directory && *file->directory);
        assert(file->name && identifier(file->name) && strcmp(file->name, "_"));
        error->file = i;
        for (int j = 0; j < i; j++) {
            if (!strcmp(file->path, files[j].path)) {
                error->related_file = j;
                return reject(p, error, SOURCE_PACKAGE_DUPLICATE_FILE);
            }
        }
        int package = find_directory(p, file->directory);
        if (package >= 0 && strcmp(p->packages[package].name, file->name)) {
            error->related_file = first_files[package];
            return reject(p, error, SOURCE_PACKAGE_HEADER_MISMATCH);
        }
        if (package < 0) {
            for (int j = 0; j < p->count; j++) {
                if (!strcmp(p->packages[j].name, file->name)) {
                    error->related_file = first_files[j];
                    return reject(p, error, SOURCE_PACKAGE_DUPLICATE_NAME);
                }
            }
            package = p->count++;
            first_files[package] = i;
            p->packages[package] = (source_package){ .directory = file->directory,
                                                     .name = file->name };
        }
        p->file_packages[i] = package;
    }
    sort_members(p, files);
    size_t edge_count = (size_t)p->count * (size_t)p->count;
    p->edges = xmalloc(edge_count * sizeof *p->edges);
    for (size_t i = 0; i < edge_count; i++) {
        p->edges[i] = -1;
    }
    for (int i = 0; i < import_count; i++) {
        const source_package_import *import = &imports[i];
        assert(import->file >= 0 && import->file < file_count);
        assert(import->directory && *import->directory);
        error->file = import->file;
        error->import = i;
        int package = find_directory(p, import->directory);
        if (package < 0) {
            return reject(p, error, SOURCE_PACKAGE_MISSING);
        }
        const char *alias = import->alias ? import->alias
                                          : directory_name(import->directory);
        bool blank = import->alias && !strcmp(alias, "_");
        if (!identifier(alias) || (!blank && !strcmp(alias, "_"))) {
            return reject(p, error, SOURCE_PACKAGE_IMPORT_NAME);
        }
        if (!blank) {
            p->bindings[p->binding_count++] = (import_binding){ alias,
                                                                import->file,
                                                                package,
                                                                i };
        }
        int from = p->file_packages[import->file];
        int *edge = &p->edges[from * p->count + package];
        if (*edge < 0) {
            *edge = i;
        }
    }
    if (!unique_bindings(p, error)) {
        return reject(p, error, SOURCE_PACKAGE_DUPLICATE_IMPORT);
    }
    if (!order_packages(p, imports, error)) {
        return reject(p, error, SOURCE_PACKAGE_CYCLE);
    }
    *error = (source_package_error){ .file = -1,
                                     .import = -1,
                                     .related_file = -1,
                                     .related_import = -1 };
    return p;
}
int source_packages_count(const source_packages *p)
{
    return p->count;
}
const source_package *source_packages_at(const source_packages *p, int package)
{
    assert(package >= 0 && package < p->count);
    return &p->packages[package];
}
int source_packages_file(const source_packages *p, int file)
{
    assert(file >= 0 && file < p->file_count);
    return p->file_packages[file];
}
int source_packages_order(const source_packages *p, int ordinal)
{
    assert(ordinal >= 0 && ordinal < p->count);
    return p->order[ordinal];
}
int source_packages_lookup(const source_packages *p, int file, const char *alias)
{
    assert(file >= 0 && file < p->file_count && alias);
    int binding = binding_at(p, file, alias);
    return binding < 0 ? -1 : p->bindings[binding].package;
}
int source_packages_dependency(const source_packages *p, int from, int to)
{
    assert(from >= 0 && from < p->count && to >= 0 && to < p->count);
    return p->edges[from * p->count + to];
}
