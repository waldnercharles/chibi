#include "authoring/source_package_snapshot.h"

#include "authoring/source_package_paths.h"
#include "authoring/source_package_routes.h"
#include "authoring/source_package_syntax.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_FILE_BYTES = 512 * 1024,
    MAX_TOTAL_BYTES = 8 * 1024 * 1024,
    MAX_IMPORTS = 4096,
};
struct source_package_snapshot
{
    arena storage;
    source_package_collection *roots;
    source_package_input *inputs;
    script_package_document *documents;
    source_package_file *files;
    source_package_syntax **extracted;
    const char **identities;
    int *file_roots;
    source_package_import *imports;
    source_packages *graph;
    int root_count, file_count;
    bool failed;
    source_package_snapshot_error error;
};

static char *copy(source_package_snapshot *s, const char *text, size_t size)
{
    char *owned = arena_alloc(&s->storage, size + 1);
    memcpy(owned, text, size);
    owned[size] = 0;
    return owned;
}
static bool reject(source_package_snapshot *s, script_range range, const char *message)
{
    s->failed = true;
    s->error.diagnostic.range = range;
    snprintf(s->error.diagnostic.message, sizeof s->error.diagnostic.message, "%s", message);
    return false;
}
static script_range file_range(int file)
{
    return (script_range){ .line = 1, .column = 1, .document = file };
}
static bool valid_utf8(const char *text, size_t size)
{
    for (size_t i = 0; i < size;) {
        unsigned char c = (unsigned char)text[i++];
        if (c < 0x80) {
            continue;
        }
        int count = c >= 0xc2 && c <= 0xdf ? 1
                  : c >= 0xe0 && c <= 0xef ? 2
                  : c >= 0xf0 && c <= 0xf4 ? 3
                                           : -1;
        if (count < 0 || (size_t)count > size - i) {
            return false;
        }
        unsigned value = c & (0x7f >> (count + 1));
        unsigned minimum = count == 1 ? 0x80 : count == 2 ? 0x800 : 0x10000;
        for (int j = 0; j < count; j++) {
            unsigned char next = (unsigned char)text[i++];
            if ((next & 0xc0) != 0x80) {
                return false;
            }
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff)) {
            return false;
        }
    }
    return true;
}
static bool retain_file(source_package_snapshot *s, const source_package_input *input, int file, size_t *bytes)
{
    script_range range = file_range(file);
    if (!source_package_path_bounded(input->path) || !*input->path) {
        return reject(s, range, "Invalid package file path");
    }
    if (!input->text || input->size > MAX_FILE_BYTES ||
        *bytes > MAX_TOTAL_BYTES - input->size) {
        return reject(s, range, "Package source byte limit exceeded");
    }
    *bytes += input->size;
    s->file_count = file + 1;
    s->inputs[file] = (source_package_input){ copy(s, input->path, strlen(input->path)),
                                              copy(s, input->text, input->size),
                                              input->size };
    s->documents[file].path = s->inputs[file].path;
    source_package_routed_file routed;
    const char *route_error = source_package_routes_file(
        &s->storage,
        s->roots,
        s->root_count,
        input->path,
        &routed
    );
    if (route_error) {
        return reject(s, range, route_error);
    }
    const char *path = routed.path, *directory = routed.directory;
    s->documents[file].path = path;
    s->documents[file].key = routed.key;
    s->identities[file] = routed.identity;
    s->file_roots[file] = routed.root;
    if (memchr(input->text, 0, input->size)) {
        return reject(s, range, "Package source contains NUL bytes");
    }
    if (!valid_utf8(input->text, input->size)) {
        return reject(s, range, "Package source is not valid UTF-8");
    }
    script_syntax *syntax = script_parse(s->inputs[file].text);
    s->documents[file].syntax = syntax;
    script_diagnostic error;
    s->extracted[file] = source_package_syntax_new(syntax, path, directory, file, &error);
    if (!s->extracted[file]) {
        s->error.diagnostic = error;
        s->error.diagnostic.range.document = file;
        s->failed = true;
        return false;
    }
    s->files[file] = s->extracted[file]->file;
    s->files[file].header.document = file;
    return true;
}
static bool resolve_import(source_package_snapshot *s, int file, int index)
{
    const source_package_syntax *source = s->extracted[file];
    script_range range = source->locations[index].path;
    range.document = file;
    const char *text = s->documents[file].syntax->text + range.start;
    char *directory;
    const char *route_error = source_package_routes_import(
        &s->storage,
        s->roots,
        s->root_count,
        s->files[file].directory,
        s->file_roots[file],
        text,
        (size_t)(range.end - range.start),
        &directory
    );
    if (route_error) {
        return reject(s, range, route_error);
    }
    source_package_import import = source->imports[index];
    import.directory = directory;
    import.range.document = file;
    apush(s->imports, import);
    return true;
}
static bool graph_error(source_package_snapshot *s)
{
    const source_package_error *error = &s->error.graph;
    script_range range = error->import >= 0 ? s->imports[error->import].range
                       : error->file >= 0   ? s->files[error->file].header
                                            : file_range(-1);
    if (error->code == SOURCE_PACKAGE_MISSING ||
        error->code == SOURCE_PACKAGE_IMPORT_NAME) {
        int index = error->import;
        for (int i = 0; i < s->file_count; i++) {
            if (index < s->extracted[i]->import_count) {
                range = s->extracted[i]->locations[index].path;
                range.document = i;
                break;
            }
            index -= s->extracted[i]->import_count;
        }
    }
    s->error.related = error->related_import >= 0
                         ? s->imports[error->related_import].range
                     : error->related_file >= 0 ? s->files[error->related_file].header
                                                : file_range(-1);
    const char *message = "Invalid package graph";
    switch (error->code) {
        case SOURCE_PACKAGE_DUPLICATE_FILE:
            message = "Duplicate package file";
            break;
        case SOURCE_PACKAGE_HEADER_MISMATCH:
            message = "Package sibling headers differ";
            break;
        case SOURCE_PACKAGE_DUPLICATE_NAME:
            message = "Duplicate package name";
            break;
        case SOURCE_PACKAGE_MISSING:
            message = "Imported package is missing from the snapshot";
            break;
        case SOURCE_PACKAGE_IMPORT_NAME:
            message = "Invalid import binding name";
            break;
        case SOURCE_PACKAGE_DUPLICATE_IMPORT:
            message = "Duplicate file-local import binding";
            break;
        case SOURCE_PACKAGE_CYCLE: message = "Package import cycle"; break;
        case SOURCE_PACKAGE_OK: break;
    }
    return reject(s, range, message);
}
source_package_snapshot *source_package_snapshot_new(const source_package_snapshot_options *options)
{
    source_package_snapshot *s = xcalloc(1, sizeof *s);
    s->error.related = file_range(-1);
    s->error.graph = (source_package_error){ .file = -1,
                                             .import = -1,
                                             .related_file = -1,
                                             .related_import = -1 };
    if (!options || options->file_count < 0 ||
        options->file_count > SOURCE_PACKAGE_MAX_FILES ||
        (options->file_count && !options->files) || options->collection_count < 0 ||
        options->collection_count >= SOURCE_PACKAGE_MAX_FILES ||
        (options->collection_count && !options->collections)) {
        reject(s, file_range(-1), "Invalid package snapshot options");
        return s;
    }
    s->roots = xcalloc((size_t)options->collection_count + 1, sizeof *s->roots);
    const char *route_error = source_package_routes_roots(
        &s->storage,
        options->project_root,
        options->collections,
        options->collection_count,
        s->roots,
        &s->root_count
    );
    if (route_error) {
        reject(s, file_range(-1), route_error);
        return s;
    }
    size_t count = (size_t)options->file_count;
    s->inputs = xcalloc(count + 1, sizeof *s->inputs);
    s->documents = xcalloc(count + 1, sizeof *s->documents);
    s->files = xcalloc(count + 1, sizeof *s->files);
    s->extracted = xcalloc(count + 1, sizeof *s->extracted);
    s->identities = xcalloc(count + 1, sizeof *s->identities);
    s->file_roots = xcalloc(count + 1, sizeof *s->file_roots);
    size_t bytes = 0;
    for (int i = 0; i < options->file_count; i++) {
        if (!retain_file(s, &options->files[i], i, &bytes)) {
            return s;
        }
    }
    for (int i = 0; i < s->file_count; i++) {
        for (int j = 0; j < s->extracted[i]->import_count; j++) {
            if (acount(s->imports) >= MAX_IMPORTS) {
                reject(s, file_range(i), "Package import limit exceeded");
                return s;
            }
            if (!resolve_import(s, i, j)) {
                return s;
            }
        }
    }
    s->graph = source_packages_new(
        s->files,
        s->file_count,
        s->imports,
        acount(s->imports),
        &s->error.graph
    );
    if (!s->graph) {
        graph_error(s);
    }
    return s;
}
void source_package_snapshot_free(source_package_snapshot *s)
{
    if (!s) {
        return;
    }
    source_packages_free(s->graph);
    for (int i = 0; i < s->file_count; i++) {
        source_package_syntax_free(s->extracted[i]);
        script_syntax_free((script_syntax *)s->documents[i].syntax);
    }
    free(s->roots);
    free(s->inputs);
    free(s->documents);
    free(s->files);
    free(s->extracted);
    free(s->identities);
    free(s->file_roots);
    afree(s->imports);
    arena_free(&s->storage);
    free(s);
}
const source_package_snapshot_error *source_package_snapshot_error_at(const source_package_snapshot *s)
{
    return s->failed ? &s->error : NULL;
}
const source_packages *source_package_snapshot_graph(const source_package_snapshot *s)
{
    return s->graph;
}
const script_package_document *source_package_snapshot_documents(
    const source_package_snapshot *s,
    int *count
)
{
    *count = s->file_count;
    return s->documents;
}
const source_package_import *source_package_snapshot_imports(
    const source_package_snapshot *s,
    int *count
)
{
    *count = acount(s->imports);
    return s->imports;
}
const char *source_package_snapshot_identity(const source_package_snapshot *s, int package)
{
    if (!s->graph || package < 0 || package >= source_packages_count(s->graph)) {
        return NULL;
    }
    const source_package *record = source_packages_at(s->graph, package);
    return s->identities[record->files[0]];
}
