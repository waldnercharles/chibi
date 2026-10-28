#include "package_snapshot_chibi.h"

#include "authoring/source_package_routes.h"
#include "authoring/source_package_syntax.h"
#include "compiler_fixture.h"
#include "package_core_source.h"
#include "package_graph_chibi_source.h"
#include "package_paths_chibi_source.h"
#include "package_routes_chibi_source.h"
#include "package_snapshot_chibi_source.h"
#include "package_syntax_chibi_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Twenty-four shared constructions, four precedence inputs and seventeen
 * validation requests and one missing-token input: 92 Build calls,
 * three million instructions each, eight actual source files per backend.
 * Inputs have at most five files below 1 KiB and four imports. The SIZE_MAX
 * precedence input is never read at its declared size. Native parsing receives
 * only raw source; all C trees below are comparison oracles, never inputs. */
enum
{
    MAX_FILES = 5,
    MAX_IMPORTS = 4,
    MAX_NODES = 1024,
    MAX_FOREST = 5 * MAX_NODES
};
static const char IDENTITY[] = "project:source_snapshot";
static compiler_fixture fixture;
static bool attempted;
static arena input, output, expected_storage, retained[2][2];
static pin_value saved[2][2];
static source_package_snapshot *precedence;
static bool saved_kind[2];
static pin_value saved_check[2][2];
static uint64_t saved_tag[2][2];
static char saved_expected[2][1024];
static source_package_syntax *extractions[MAX_FILES];
static compiler_fixture_node raw[MAX_NODES];
static source_package_collection expected_roots[3];
static int expected_root_count;
static const char *expected_root_error;
static source_package_routed_file expected_routes[MAX_FILES];
static int constructions, offered_files, parsed_documents, raw_nodes, missing_nodes;
static bool named_seen[2], extra_seen[2], error_seen;

static void finish_comparison()
{
    arena_free(&input);
    arena_free(&output);
    arena_free(&expected_storage);
    for (int i = 0; i < MAX_FILES; i++) {
        source_package_syntax_free(extractions[i]);
        extractions[i] = NULL;
    }
}
static void cleanup()
{
    finish_comparison();
    source_package_snapshot_free(precedence);
    precedence = NULL;
}
static const char *copy(arena *storage, const char *text, size_t size)
{
    char *owned = arena_alloc(storage, size + 1);
    if (size) {
        memcpy(owned, text, size);
    }
    owned[size] = 0;
    return owned;
}
static pin_value text(const char *value)
{
    value = value ? value : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, value, strlen(value));
    if (error) {
        abort();
    }
    return result;
}
static const char *prefix(const source_package_input *source, arena *storage)
{
    /* Oversize inputs fail before source retention. Do not infer backing size
     * from a declaration such as SIZE_MAX, and never strlen raw source bytes. */
    size_t size = source->text && source->size <= 512 * 1024 ? source->size : 0;
    const char *nul = size ? memchr(source->text, 0, size) : NULL;
    if (nul) {
        size = (size_t)(nul - source->text);
    }
    return copy(storage, source->text, size);
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static bool row(int backend, const char *identity, const char *name, pin_value *out)
{
    const pin_record *record = compiler_fixture_record(&fixture, backend, identity, name, &input);
    REQUIRE(record);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .record = record,
                        .type_name = record->type.name,
                        .ptr = arena_alloc(&input, record->type.size) };
    memset(out->ptr, 0, record->type.size);
    return true;
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool read(const pin_value *row, const char *name, pin_value *value)
{
    REQUIRE(row && row->type == PIN_STRUCT && row->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, value));
    return true;
}
static bool number(const pin_value *row, const char *name, int expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_I32 && value.i == expected);
    return true;
}
static bool flag(const pin_value *row, const char *name, bool expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_BOOL && value.b == expected);
    return true;
}
static bool string(const pin_value *row, const char *name, const char *expected)
{
    pin_value value;
    REQUIRE(
        read(row, name, &value) && value.type == PIN_STR && pin_text(value).bytes &&
        !strcmp(pin_text(value).bytes, expected ? expected : "")
    );
    return true;
}
static bool range(const pin_value *row, const char *prefix, script_range expected)
{
    const char *names[] = { "start", "end", "line", "column", "document" };
    const int values[] = { expected.start,
                           expected.end,
                           expected.line,
                           expected.column,
                           expected.document };
    for (int i = 0; i < 5; i++) {
        char name[40];
        snprintf(name, sizeof name, "%s%s", prefix, names[i]);
        REQUIRE(number(row, name, values[i]));
    }
    return true;
}
static const pin_value *payload(
    int backend,
    const char *identity,
    const char *name,
    const pin_value *result,
    const char *variant,
    const char *field
)
{
    if (!result || result->type != PIN_UNION || !result->ptr) {
        return NULL;
    }
    const authoring_package *package = compiler_fixture_package(&fixture, backend, identity);
    if (!package || !package->exports) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, backend, identity, name)
    );
    const pin_union *data = result->ptr;
    for (int v = 0; type && v < type->variant_count; v++) {
        const script_catalog_variant *kind = &type->variants[v];
        if (data->tag != kind->tag || strcmp(kind->name, variant) ||
            data->count != kind->field_count) {
            continue;
        }
        for (int i = 0; i < kind->field_count; i++) {
            if (!strcmp(kind->fields[i].name, field)) {
                return &data->values[i];
            }
        }
    }
    return NULL;
}
static bool array(const pin_value *value, pin_type type, int cap, const pin_array **out)
{
    REQUIRE(value && value->container == PIN_CONTAINER_ARRAY && value->type == type);
    static const pin_array empty_int = { .element_type = PIN_I32 },
                           empty_record = { .element_type = PIN_STRUCT };
    *out = value->ptr      ? value->ptr
         : type == PIN_I32 ? &empty_int
                           : &empty_record;
    REQUIRE(
        (*out)->element_type == type && (*out)->count >= 0 &&
        (*out)->count <= cap && (!(*out)->count || (*out)->items)
    );
    return true;
}
static bool own(arena *storage, const pin_value *value, int depth)
{
    REQUIRE(depth <= 6);
    if (value->container == PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return true;
        }
        const pin_array *items = value->ptr;
        REQUIRE(arena_contains(storage, items) && items->count >= 0 && items->count <= MAX_FOREST);
        REQUIRE(!items->count || arena_contains(storage, items->items));
        for (int i = 0; i < items->count; i++) {
            REQUIRE(own(storage, &items->items[i], depth + 1));
        }
        return true;
    }
    REQUIRE(value->container == PIN_CONTAINER_SINGLE);
    if (value->type == PIN_UNION) {
        const pin_union *data = value->ptr;
        REQUIRE(data && arena_contains(storage, data) && data->count >= 0 && data->count <= 7);
        for (int i = 0; i < data->count; i++) {
            REQUIRE(own(storage, &data->values[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STRUCT) {
        REQUIRE(
            value->ptr && value->record && arena_contains(storage, value->ptr) &&
            arena_contains(storage, value->record)
        );
        const type_desc *type = pin_value_type(value);
        REQUIRE(type && type->field_count <= 32);
        for (int i = 0; i < type->field_count; i++) {
            pin_value field;
            REQUIRE(!pin_read_field(storage, value->ptr, &type->fields[i], &field));
            REQUIRE(own(storage, &field, depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        /* Canonical empty text has no descriptor; present descriptors are owned here. */
        REQUIRE(
            pin_text(*value).bytes &&
            (!value->text || arena_contains(storage, value->text))
        );
        return true;
    }
    REQUIRE(value->type == PIN_I32 || value->type == PIN_BOOL || value->type == PIN_U64);
    return true;
}
static bool prepare()
{
    if (attempted) {
        return fixture.owners[0] && fixture.owners[1] &&
               authoring_packages_valid(fixture.owners[0]) &&
               authoring_packages_valid(fixture.owners[1]);
    }
    attempted = true;
    source_package_input files[] = {
        { "source_snapshot/Snapshot.chibi",
          PACKAGE_SNAPSHOT_CHIBI_SOURCE,
          sizeof PACKAGE_SNAPSHOT_CHIBI_SOURCE - 1 },
        { "compiler_syntax/Syntax.chibi",
          COMPILER_SYNTAX_CHIBI_SOURCE,
          sizeof COMPILER_SYNTAX_CHIBI_SOURCE - 1 },
        { "package_routes/Routes.chibi",
          PACKAGE_ROUTES_CHIBI_SOURCE,
          sizeof PACKAGE_ROUTES_CHIBI_SOURCE - 1 },
        { "package_syntax/Extract.chibi",
          PACKAGE_SYNTAX_CHIBI_SOURCE,
          sizeof PACKAGE_SYNTAX_CHIBI_SOURCE - 1 },
        { "package_graph/Graph.chibi",
          PACKAGE_GRAPH_CHIBI_SOURCE,
          sizeof PACKAGE_GRAPH_CHIBI_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "package_paths/Paths.chibi",
          PACKAGE_PATHS_CHIBI_SOURCE,
          sizeof PACKAGE_PATHS_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
    };
    const native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    /* Core strings uses the production snapshot limits and full retention checks. */
    for (int i = 0; i < 7; i++) {
        REQUIRE(files[i].size < 16384);
    }
    return compiler_fixture_prepare_packs(&fixture, files, 8, packs, 2);
}
static bool pack(int backend, const source_package_snapshot_options *options, pin_value *arguments)
{
    const bool present = options != NULL;
    static const source_package_snapshot_options absent = { 0 };
    if (!options) {
        options = &absent;
    }
    int actual_files = options->files && options->file_count >= 0 &&
                               options->file_count <= MAX_FILES
                         ? options->file_count
                         : 0;
    int actual_collections = options->collections && options->collection_count >= 0 &&
                                     options->collection_count <= 2
                               ? options->collection_count
                               : 0;
    REQUIRE(row(backend, IDENTITY, "Options", &arguments[0]));
    REQUIRE(
        put(&arguments[0], "project", text(options->project_root)) &&
        put(&arguments[0], "has_project", boolean(options->project_root != NULL))
    );
    REQUIRE(
        put(&arguments[0], "has_options", boolean(present)) &&
        put(&arguments[0], "has_files", boolean(options->files != NULL)) &&
        put(&arguments[0], "has_collections", boolean(options->collections != NULL))
    );
    REQUIRE(
        put(&arguments[0], "file_count", integer(options->file_count)) &&
        put(&arguments[0], "collection_count", integer(options->collection_count))
    );
    const pin_record *collection = compiler_fixture_record(
        &fixture,
        backend,
        "project:package_routes",
        "Collection",
        &input
    );
    const pin_record *record = compiler_fixture_record(&fixture, backend, IDENTITY, "Input", &input);
    REQUIRE(collection && record);
    pin_array *collections = pin_array_snapshot_allocate(
        &input,
        PIN_STRUCT,
        collection->type.name,
        actual_collections + 1
    );
    pin_array
        *files = pin_array_snapshot_allocate(&input, PIN_STRUCT, record->type.name, actual_files);
    pin_array *sizes = pin_array_snapshot_allocate(&input, PIN_U64, NULL, actual_files);
    REQUIRE(collections && files && sizes);
    collections->record = collection;
    files->record = record;
    for (int i = 0; i < actual_collections; i++) {
        const source_package_collection *source = &options->collections[i];
        pin_value value;
        REQUIRE(row(backend, "project:package_routes", "Collection", &value));
        REQUIRE(
            put(&value, "name", text(source->name)) &&
            put(&value, "path", text(source->root)) &&
            put(&value, "has_name", boolean(source->name != NULL)) &&
            put(&value, "has_path", boolean(source->root != NULL))
        );
        collections->items[i] = value;
    }
    /* The declared prefix is authoritative; this invalid offered tail must
     * never enter RootSet. It adds no policy request or parser invocation. */
    pin_value ignored;
    REQUIRE(row(backend, "project:package_routes", "Collection", &ignored));
    REQUIRE(
        put(&ignored, "name", text("")) && put(&ignored, "path", text("relative")) &&
        put(&ignored, "has_name", boolean(false)) &&
        put(&ignored, "has_path", boolean(true))
    );
    collections->items[actual_collections] = ignored;
    for (int i = 0; i < actual_files; i++) {
        const source_package_input *source = &options->files[i];
        pin_value value;
        REQUIRE(row(backend, IDENTITY, "Input", &value));
        const char *bytes = prefix(source, &input);
        REQUIRE(
            put(&value, "path", text(source->path)) &&
            put(&value,
                "text",
                (pin_value){ .type = PIN_STR,
                             .text = &(const pin_string){
                                 .bytes = bytes,
                                 .length = (uint32_t)strlen(bytes) } })
        );
        REQUIRE(
            put(&value, "has_path", boolean(source->path != NULL)) &&
            put(&value, "has_text", boolean(source->text != NULL))
        );
        files->items[i] = value;
        sizes->items[i] = (pin_value){ .type = PIN_U64, .u64 = (uint64_t)source->size };
    }
    arguments[1] = (pin_value){ .type = PIN_STRUCT,
                                .container = PIN_CONTAINER_ARRAY,
                                .ptr = collections,
                                .record = collection,
                                .type_name = collection->type.name };
    arguments[2] = (pin_value){ .type = PIN_STRUCT,
                                .container = PIN_CONTAINER_ARRAY,
                                .ptr = files,
                                .record = record,
                                .type_name = record->type.name };
    arguments[3] = (pin_value){ .type = PIN_U64,
                                .container = PIN_CONTAINER_ARRAY,
                                .ptr = sizes };
    return true;
}
static bool graph_error(const pin_value *value, const source_package_error *error)
{
    REQUIRE(
        value && number(value, "code", error->code) &&
        number(value, "file", error->file)
    );
    REQUIRE(
        number(value, "import_index", error->import) &&
        number(value, "related_file", error->related_file) &&
        number(value, "related_import", error->related_import) &&
        number(value, "cycle_count", error->cycle_count)
    );
    return true;
}
static bool compare_graph(
    int backend,
    const pin_value *value,
    const source_package_snapshot *snapshot,
    const pin_array *documents
)
{
    const source_packages *expected = source_package_snapshot_graph(snapshot);
    const pin_array *packages, *members, *files, *order, *edges, *bindings;
    REQUIRE(array(
        payload(backend, "project:package_graph", "Graph", value, "Accepted", "packages"),
        PIN_STRUCT,
        MAX_FILES,
        &packages
    ));
    REQUIRE(array(
        payload(backend, "project:package_graph", "Graph", value, "Accepted", "members"),
        PIN_I32,
        MAX_FILES,
        &members
    ));
    REQUIRE(array(
        payload(backend, "project:package_graph", "Graph", value, "Accepted", "file_packages"),
        PIN_I32,
        MAX_FILES,
        &files
    ));
    REQUIRE(array(
        payload(backend, "project:package_graph", "Graph", value, "Accepted", "order"),
        PIN_I32,
        MAX_FILES,
        &order
    ));
    REQUIRE(array(
        payload(backend, "project:package_graph", "Graph", value, "Accepted", "edges"),
        PIN_STRUCT,
        MAX_IMPORTS,
        &edges
    ));
    REQUIRE(array(
        payload(backend, "project:package_graph", "Graph", value, "Accepted", "bindings"),
        PIN_STRUCT,
        MAX_IMPORTS,
        &bindings
    ));
    int count = source_packages_count(expected), offset = 0;
    REQUIRE(
        packages->count == count && order->count == count &&
        files->count == documents->count && members->count == documents->count
    );
    for (int p = 0; p < count; p++) {
        const source_package *package = source_packages_at(expected, p);
        REQUIRE(
            string(&packages->items[p], "directory", package->directory) &&
            string(&packages->items[p], "name", package->name)
        );
        REQUIRE(
            number(&packages->items[p], "member_start", offset) &&
            number(&packages->items[p], "member_count", package->file_count)
        );
        for (int i = 0; i < package->file_count; i++) {
            REQUIRE(
                members->items[offset++].type == PIN_I32 &&
                members->items[offset - 1].i == package->files[i]
            );
            REQUIRE(string(
                &documents->items[package->files[i]],
                "identity",
                source_package_snapshot_identity(snapshot, p)
            ));
        }
        REQUIRE(
            order->items[p].type == PIN_I32 &&
            order->items[p].i == source_packages_order(expected, p)
        );
    }
    for (int f = 0; f < documents->count; f++) {
        REQUIRE(
            files->items[f].type == PIN_I32 &&
            files->items[f].i == source_packages_file(expected, f)
        );
    }
    int matrix[MAX_FILES * MAX_FILES];
    for (int i = 0; i < MAX_FILES * MAX_FILES; i++) {
        matrix[i] = -1;
    }
    for (int i = 0; i < edges->count; i++) {
        pin_value from, to, index;
        REQUIRE(
            read(&edges->items[i], "from", &from) &&
            read(&edges->items[i], "to", &to) &&
            read(&edges->items[i], "import_index", &index)
        );
        REQUIRE(
            from.type == PIN_I32 && to.type == PIN_I32 && index.type == PIN_I32 &&
            from.i >= 0 && from.i < count && to.i >= 0 && to.i < count
        );
        REQUIRE(matrix[from.i * count + to.i] == -1);
        matrix[from.i * count + to.i] = index.i;
    }
    for (int f = 0; f < count; f++) {
        for (int t = 0; t < count; t++) {
            REQUIRE(matrix[f * count + t] == source_packages_dependency(expected, f, t));
        }
    }
    int import_count;
    const source_package_import *imports = source_package_snapshot_imports(snapshot, &import_count);
    bool seen[MAX_IMPORTS] = { 0 };
    for (int i = 0; i < bindings->count; i++) {
        pin_value index;
        REQUIRE(
            read(&bindings->items[i], "import_index", &index) && index.type == PIN_I32 &&
            index.i >= 0 && index.i < import_count && !seen[index.i]
        );
        seen[index.i] = true;
        const source_package_import *imported = &imports[index.i];
        const char *slash = strrchr(imported->directory, '/');
        const char *name = imported->alias ? imported->alias
                         : slash           ? slash + 1
                                           : imported->directory;
        REQUIRE(strcmp(name, "_") && string(&bindings->items[i], "name", name));
        REQUIRE(
            number(&bindings->items[i], "file", imported->file) &&
            number(
                &bindings->items[i],
                "target",
                source_packages_lookup(expected, imported->file, name)
            )
        );
    }
    for (int i = 0; i < import_count; i++) {
        REQUIRE(seen[i] == (!imports[i].alias || strcmp(imports[i].alias, "_")));
    }
    return true;
}
static bool compare_result(
    int backend,
    const source_package_snapshot_options *options,
    const source_package_snapshot *snapshot,
    const pin_value *result
)
{
    const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
    const char *variant = error ? "Rejected" : "Accepted";
    const pin_array *documents, *forest, *imports;
    REQUIRE(
        array(payload(backend, IDENTITY, "Result", result, variant, "documents"), PIN_STRUCT, MAX_FILES, &documents)
    );
    REQUIRE(
        array(payload(backend, IDENTITY, "Result", result, variant, "nodes"), PIN_STRUCT, MAX_FOREST, &forest)
    );
    REQUIRE(
        array(payload(backend, IDENTITY, "Result", result, variant, "imports"), PIN_STRUCT, MAX_IMPORTS, &imports)
    );
    int count, import_count;
    const script_package_document *expected = source_package_snapshot_documents(snapshot, &count);
    const source_package_import *resolved = source_package_snapshot_imports(snapshot, &import_count);
    REQUIRE(documents->count == count && imports->count == import_count);
    if (error) {
        const pin_value *diagnostic = payload(backend, IDENTITY, "Result", result, variant, "diagnostic");
        REQUIRE(
            diagnostic && range(diagnostic, "", error->diagnostic.range) &&
            string(diagnostic, "message", error->diagnostic.message)
        );
        REQUIRE(range(
            payload(backend, IDENTITY, "Result", result, variant, "related"),
            "",
            error->related
        ));
        REQUIRE(graph_error(
            payload(backend, IDENTITY, "Result", result, variant, "graph_error"),
            &error->graph
        ));
        const pin_array *cycle;
        REQUIRE(
            array(payload(backend, IDENTITY, "Result", result, variant, "cycle"), PIN_I32, 256, &cycle) &&
            cycle->count == 256
        );
        for (int i = 0; i < 256; i++) {
            REQUIRE(
                cycle->items[i].type == PIN_I32 &&
                cycle->items[i].i == error->graph.cycle[i]
            );
        }
    }
    if (!error) {
        const pin_array *actual;
        REQUIRE(
            !expected_root_error &&
            array(payload(backend, IDENTITY, "Result", result, variant, "roots"), PIN_STRUCT, 3, &actual) &&
            actual->count == expected_root_count
        );
        for (int i = 0; i < expected_root_count; i++) {
            REQUIRE(
                string(&actual->items[i], "name", expected_roots[i].name) &&
                string(&actual->items[i], "path", expected_roots[i].root)
            );
        }
    }
    int node_offset = 0, import_offset = 0;
    bool file_phase_complete = options && count == options->file_count;
    for (int i = 0; i < count; i++) {
        file_phase_complete = file_phase_complete && extractions[i] != NULL;
    }
    for (int f = 0; f < count; f++) {
        const pin_value *document = &documents->items[f];
        REQUIRE(
            string(document, "original_path", options->files[f].path) &&
            string(document, "path", expected[f].path) &&
            string(document, "key", expected[f].key)
        );
        REQUIRE(
            string(document, "text", prefix(&options->files[f], &expected_storage)) &&
            flag(document, "has_key", expected[f].key != NULL) &&
            flag(document, "has_syntax", expected[f].syntax != NULL)
        );
        const source_package_routed_file *routed = &expected_routes[f];
        REQUIRE(
            string(document, "directory", routed->directory) &&
            string(document, "identity", routed->identity) &&
            number(document, "root", routed->root)
        );
        REQUIRE(flag(document, "has_header", extractions[f] != NULL));
        REQUIRE(range(
            document,
            "header_",
            extractions[f] ? (script_range){ extractions[f]->file.header.start,
                                             extractions[f]->file.header.end,
                                             extractions[f]->file.header.line,
                                             extractions[f]->file.header.column,
                                             f }
                           : (script_range){ 0 }
        ));
        int local_count = 0;
        if (expected[f].syntax) {
            REQUIRE(
                compiler_fixture_nodes(ts_tree_root_node(expected[f].syntax->tree), raw, MAX_NODES, 128, &local_count)
            );
            REQUIRE(
                number(document, "node_start", node_offset) &&
                number(document, "syntax_root", node_offset)
            );
            for (int n = 0; n < local_count; n++) {
                REQUIRE(node_offset + n < forest->count);
                const pin_value *node = &forest->items[node_offset + n];
                TSNode original = raw[n].node;
                TSPoint point = ts_node_start_point(original);
                REQUIRE(
                    string(node, "kind", ts_node_type(original)) &&
                    string(node, "field", raw[n].field)
                );
                REQUIRE(
                    number(node, "start", (int)ts_node_start_byte(original)) &&
                    number(node, "end", (int)ts_node_end_byte(original)) &&
                    number(node, "row", (int)point.row) &&
                    number(node, "column", (int)point.column)
                );
                REQUIRE(
                    number(
                        node,
                        "parent",
                        raw[n].parent < 0 ? -1 : node_offset + raw[n].parent
                    ) &&
                    number(
                        node,
                        "first_child",
                        raw[n].first_child < 0 ? -1 : node_offset + raw[n].first_child
                    ) &&
                    number(
                        node,
                        "next_sibling",
                        raw[n].next_sibling < 0 ? -1 : node_offset + raw[n].next_sibling
                    )
                );
                REQUIRE(
                    flag(node, "error", ts_node_is_error(original)) &&
                    flag(node, "missing", ts_node_is_missing(original)) &&
                    flag(node, "has_error", ts_node_has_error(original)) &&
                    flag(node, "is_named", ts_node_is_named(original)) &&
                    flag(node, "is_extra", ts_node_is_extra(original))
                );
                named_seen[ts_node_is_named(original)] = true;
                extra_seen[ts_node_is_extra(original)] = true;
                error_seen |= ts_node_is_error(original);
                missing_nodes += ts_node_is_missing(original);
            }
            node_offset += local_count;
        } else {
            REQUIRE(number(document, "node_start", 0) && number(document, "syntax_root", -1));
        }
        REQUIRE(number(document, "node_count", local_count));
        bool visited = file_phase_complete &&
                       (!error || error->graph.code != SOURCE_PACKAGE_OK ||
                        f <= error->diagnostic.range.document);
        int found = 0;
        for (int i = 0; i < import_count; i++) {
            found += resolved[i].file == f;
        }
        REQUIRE(
            number(document, "first_import", visited ? import_offset : 0) &&
            number(document, "import_count", found)
        );
        import_offset += found;
    }
    REQUIRE(node_offset == forest->count);
    int seen[MAX_FILES] = { 0 };
    for (int i = 0; i < import_count; i++) {
        const source_package_import *imported = &resolved[i];
        int f = imported->file, local = seen[f]++;
        REQUIRE(f >= 0 && f < count && extractions[f] && local < extractions[f]->import_count);
        const source_package_import_syntax *location = &extractions[f]->locations[local];
        const pin_value *value = &imports->items[i];
        REQUIRE(
            number(value, "file", f) && string(value, "alias", imported->alias) &&
            flag(value, "explicit_alias", imported->alias != NULL) &&
            string(value, "directory", imported->directory)
        );
        REQUIRE(
            range(value, "range_", imported->range) &&
            range(value, "path_", location->path) &&
            range(value, "alias_", location->alias)
        );
    }
    if (!error) {
        REQUIRE(compare_graph(
            backend,
            payload(backend, IDENTITY, "Result", result, variant, "graph"),
            snapshot,
            documents
        ));
    }
    return true;
}
bool package_snapshot_chibi_compare(
    const source_package_snapshot_options *options,
    const source_package_snapshot *snapshot
)
{
    REQUIRE(snapshot);
    REQUIRE(
        !options || ((options->file_count >= -1 && options->file_count <= MAX_FILES) ||
                     options->file_count == 257)
    );
    REQUIRE(
        !options ||
        ((options->collection_count >= -1 && options->collection_count <= 2) ||
         options->collection_count == 256)
    );
    REQUIRE(prepare());
    finish_comparison();
    int count;
    const script_package_document *documents = source_package_snapshot_documents(snapshot, &count);
    REQUIRE(count >= 0 && count <= MAX_FILES);
    constructions++;
    if (options && options->files && options->file_count >= 0 &&
        options->file_count <= MAX_FILES) {
        offered_files += options->file_count;
    }
    memset(expected_routes, 0, sizeof expected_routes);
    expected_root_count = 0;
    expected_root_error = "Invalid package snapshot options";
    bool valid_options = options && options->file_count >= 0 &&
                         options->file_count <= MAX_FILES &&
                         (!options->file_count || options->files) &&
                         options->collection_count >= 0 &&
                         options->collection_count <= 2 &&
                         (!options->collection_count || options->collections);
    if (valid_options) {
        expected_root_error = source_package_routes_roots(
            &expected_storage,
            options->project_root,
            options->collections,
            options->collection_count,
            expected_roots,
            &expected_root_count
        );
    }
    for (int f = 0; f < count; f++) {
        if (documents[f].key) {
            REQUIRE(
                !expected_root_error && !source_package_routes_file(
                                            &expected_storage,
                                            expected_roots,
                                            expected_root_count,
                                            options->files[f].path,
                                            &expected_routes[f]
                                        )
            );
        }
    }
    for (int f = 0; f < count; f++) {
        if (!documents[f].syntax) {
            continue;
        }
        parsed_documents++;
        int nodes;
        REQUIRE(
            compiler_fixture_nodes(ts_tree_root_node(documents[f].syntax->tree), raw, MAX_NODES, 128, &nodes)
        );
        raw_nodes += nodes;
        const char *slash = strrchr(documents[f].path, '/');
        REQUIRE(slash);
        const char *directory = slash == documents[f].path
                                  ? "/"
                                  : copy(
                                        &expected_storage,
                                        documents[f].path,
                                        (size_t)(slash - documents[f].path)
                                    );
        script_diagnostic diagnostic;
        extractions[f] = source_package_syntax_new(
            documents[f].syntax,
            documents[f].path,
            directory,
            f,
            &diagnostic
        );
    }
    int kind = source_package_snapshot_error_at(snapshot) ? 1 : 0;
    bool retain = !saved_kind[kind];
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&output);
        pin_value arguments[4], result;
        REQUIRE(pack(b, options, arguments));
        REQUIRE(
            compiler_fixture_call(&fixture, b, IDENTITY, "Build", arguments, 4, 3000000, &output, &result)
        );
        arena_free(&input);
        REQUIRE(own(&output, &result, 0));
        REQUIRE(compare_result(b, options, snapshot, &result));
        if (retain) {
            REQUIRE(!compiler_fixture_snapshot(&retained[b][kind], &saved[b][kind], &result));
            saved_tag[b][kind] = ((const pin_union *)result.ptr)->tag;
            const pin_value *value;
            pin_value check;
            if (kind) {
                value = payload(b, IDENTITY, "Result", &result, "Rejected", "diagnostic");
                REQUIRE(read(value, "message", &check));
                REQUIRE(
                    strlen(
                        source_package_snapshot_error_at(snapshot)->diagnostic.message
                    ) < sizeof saved_expected[kind]
                );
                strcpy(
                    saved_expected[kind],
                    source_package_snapshot_error_at(snapshot)->diagnostic.message
                );
            } else {
                const pin_array *docs;
                REQUIRE(
                    array(
                        payload(b, IDENTITY, "Result", &result, "Accepted", "documents"),
                        PIN_STRUCT,
                        MAX_FILES,
                        &docs
                    ) &&
                    docs->count > 0 && documents[0].syntax
                );
                REQUIRE(read(&docs->items[0], "text", &check));
                REQUIRE(strlen(documents[0].syntax->text) < sizeof saved_expected[kind]);
                strcpy(saved_expected[kind], documents[0].syntax->text);
            }
            REQUIRE(
                !compiler_fixture_snapshot(&retained[b][kind], &saved_check[b][kind], &check)
            );
        }
    }
    if (retain) {
        saved_kind[kind] = true;
    }
    finish_comparison();
    return true;
}
static bool precedence_case(const source_package_input *files, int count, const char *message, int document)
{
    source_package_snapshot_options options = {
        .project_root = "/workspace/project",
        .files = files,
        .file_count = count
    };
    precedence = source_package_snapshot_new(&options);
    REQUIRE(precedence);
    const source_package_snapshot_error *error = source_package_snapshot_error_at(precedence);
    REQUIRE(error && error->diagnostic.range.document == document);
    if (message) {
        REQUIRE(!strcmp(error->diagnostic.message, message));
    }
    return package_snapshot_chibi_compare(&options, precedence);
}
TEST_CASE(package_snapshot_path_before_size)
{
    const source_package_input file = { NULL, "x", SIZE_MAX };
    return precedence_case(&file, 1, "Invalid package file path", 0);
}
TEST_CASE(package_snapshot_early_parse_before_later_route)
{
    static const char bad[] = "package app\nbroken ::\n";
    const source_package_input files[] = {
        { "app/a.chibi", bad, sizeof bad - 1 },
        { "/outside/b.chibi", "package other\n", sizeof("package other\n") - 1 }
    };
    return precedence_case(files, 2, NULL, 0);
}
TEST_CASE(package_snapshot_all_parse_before_import)
{
    static const char
        importing[] = "package app\nimport lib \"unknown:math\"\n",
        bad[] = "package other\nbroken ::\n";
    const source_package_input files[] = {
        { "app/a.chibi", importing, sizeof importing - 1 },
        { "other/b.chibi", bad, sizeof bad - 1 }
    };
    return precedence_case(files, 2, NULL, 1);
}
TEST_CASE(package_snapshot_missing_syntax_token)
{
    /* Tree-sitter inserts MISSING ")" for the first body and recovers the
     * second declaration's stray brace as ERROR; both reach the snapshot. */
    static const char text[] = "package app\n"
                               "Value :: () -> int { return (1; }\n"
                               "Other :: () -> int { return 1; } }\n";
    const source_package_input file = { "app/a.chibi", text, sizeof text - 1 };
    int before = missing_nodes;
    error_seen = false;
    REQUIRE(precedence_case(&file, 1, NULL, 0));
    REQUIRE(missing_nodes > before && error_seen);
    return true;
}
static bool retained_results()
{
    compiler_fixture_free(&fixture);
    REQUIRE(constructions == 46 && offered_files == 72 && saved_kind[0] && saved_kind[1]);
    REQUIRE(named_seen[0] && named_seen[1] && extra_seen[0] && extra_seen[1]);
    REQUIRE(error_seen && missing_nodes > 0);
    for (int b = 0; b < 2; b++) {
        for (int kind = 0; kind < 2; kind++) {
            REQUIRE(saved[b][kind].type == PIN_UNION && saved[b][kind].ptr);
            REQUIRE(own(&retained[b][kind], &saved[b][kind], 0));
            const pin_union *data = saved[b][kind].ptr;
            REQUIRE(data->tag == saved_tag[b][kind] && data->count == (kind ? 7 : 5));
            const pin_value *check = &saved_check[b][kind];
            REQUIRE(
                check->type == PIN_STR && pin_text(*check).bytes &&
                arena_contains(&retained[b][kind], check->text) &&
                !strcmp(pin_text(*check).bytes, saved_expected[kind])
            );
        }
    }
    return true;
}
TEST_CASE(package_snapshot_route_before_byte_validation)
{
    const char bad[] = { 0, (char)0xff };
    const source_package_input file = { "/outside/a.chibi", bad, sizeof bad };
    return precedence_case(&file, 1, "Package file is outside the configured roots", 0);
}
TEST_CASE(package_snapshot_utf8_validation)
{
    static const char valid[] = "package app\n// é € 😀\n";
    static const char continuation[] = "package app\n// \x80";
    static const char overlong[] = "package app\n// \xc0\xaf";
    static const char surrogate[] = "package app\n// \xed\xa0\x80";
    static const char outside[] = "package app\n// \xf4\x90\x80\x80";
    static const char truncated[] = "package app\n// \xe2\x82";
    const source_package_input inputs[] = {
        { "app/a.chibi", valid, sizeof valid - 1 },
        { "app/a.chibi", continuation, sizeof continuation - 1 },
        { "app/a.chibi", overlong, sizeof overlong - 1 },
        { "app/a.chibi", surrogate, sizeof surrogate - 1 },
        { "app/a.chibi", outside, sizeof outside - 1 },
        { "app/a.chibi", truncated, sizeof truncated - 1 },
    };
    for (int i = 0; i < 6; i++) {
        cleanup();
        source_package_snapshot_options options = {
            .project_root = "/workspace/project",
            .files = &inputs[i],
            .file_count = 1
        };
        precedence = source_package_snapshot_new(&options);
        REQUIRE(precedence);
        const source_package_snapshot_error *error = source_package_snapshot_error_at(precedence);
        REQUIRE(
            i ? error && !strcmp(error->diagnostic.message, "Package source is not valid UTF-8")
              : !error
        );
        REQUIRE(package_snapshot_chibi_compare(&options, precedence));
    }
    return true;
}
TEST_CASE(package_snapshot_source_budget_validation)
{
    const source_package_input files[] = {
        { "app/a.chibi", NULL, 0 },
        { "app/a.chibi", "x", 524289 },
        { "app/a.chibi", "x", SIZE_MAX },
    };
    for (int i = 0; i < 3; i++) {
        cleanup();
        REQUIRE(precedence_case(&files[i], 1, "Package source byte limit exceeded", 0));
    }
    return true;
}
TEST_CASE(package_snapshot_options_validation)
{
    const source_package_input file = { "app/a.chibi",
                                        "package app\n",
                                        sizeof("package app\n") - 1 };
    const source_package_collection collection = { "core", "/sdk/core" };
    const source_package_snapshot_options cases[] = {
        { .project_root = "/workspace/project", .files = &file, .file_count = -1 },
        { .project_root = "/workspace/project", .files = &file, .file_count = 257 },
        { .project_root = "/workspace/project", .file_count = 1 },
        { .project_root = "/workspace/project", .collection_count = -1 },
        { .project_root = "/workspace/project",
          .collections = &collection,
          .collection_count = 256 },
        { .project_root = "/workspace/project", .collection_count = 1 },
        { .project_root = "/workspace/project" },
    };
    for (int i = 0; i < 8; i++) {
        cleanup();
        const source_package_snapshot_options *options = i ? &cases[i - 1] : NULL;
        precedence = source_package_snapshot_new(options);
        REQUIRE(precedence);
        const source_package_snapshot_error *error = source_package_snapshot_error_at(precedence);
        REQUIRE(
            i == 7 ? !error : error && !strcmp(error->diagnostic.message, "Invalid package snapshot options")
        );
        REQUIRE(package_snapshot_chibi_compare(options, precedence));
    }
    return retained_results();
}
void test_language_package_snapshot_chibi()
{
    test_language_package_resolution_observed();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_snapshot_path_before_size);
    RUN_TEST_CASE(package_snapshot_early_parse_before_later_route);
    RUN_TEST_CASE(package_snapshot_all_parse_before_import);
    RUN_TEST_CASE(package_snapshot_route_before_byte_validation);
    RUN_TEST_CASE(package_snapshot_missing_syntax_token);
    RUN_TEST_CASE(package_snapshot_utf8_validation);
    RUN_TEST_CASE(package_snapshot_source_budget_validation);
    RUN_TEST_CASE(package_snapshot_options_validation);
    pu_clear_setup();
    compiler_fixture_free(&fixture);
    printf(
        "Authored snapshots: %d builds/backend, %d offered files, %d parsed C "
        "documents, %d raw nodes\n",
        constructions,
        offered_files,
        parsed_documents,
        raw_nodes
    );
    cleanup();
    for (int b = 0; b < 2; b++) {
        for (int kind = 0; kind < 2; kind++) {
            arena_free(&retained[b][kind]);
            saved[b][kind] = (pin_value){ 0 };
            saved_check[b][kind] = (pin_value){ 0 };
        }
    }
    saved_kind[0] = saved_kind[1] = false;
    attempted = false;
    constructions = offered_files = parsed_documents = raw_nodes = missing_nodes = 0;
    named_seen[0] = named_seen[1] = extra_seen[0] = extra_seen[1] = error_seen = false;
}
