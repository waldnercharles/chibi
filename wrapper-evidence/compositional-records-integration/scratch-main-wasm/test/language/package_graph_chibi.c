#include "package_graph_chibi.h"

#include "authoring/native_bindings.h"
#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "compiler_fixture.h"
#include "execution/module.h"
#include "execution/status.h"
#include "package_core_source.h"
#include "package_graph_chibi_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/text_definition.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This adapter only packs values, invokes authored functions, and compares
 * observations. The unchanged 23-case descriptor corpus supplies every input.
 * The two authored generations remain prepared across all 28 constructions.
 * There are 56 Build and 348 query calls, 684 packed files and 590 packed
 * imports. Only the two 256-file Builds receive a 50-million instruction
 * ceiling; every other call receives 100000. No filesystem or processes are
 * used. */
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static native_capabilities *native;
static authoring_packages *owners[2];
static const authoring_package *programs[2];
static bool attempted;
static arena inputs, storage;
static behavior_function_context context;
static behavior_function_activation activation;
static pin_value output;

static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    output = (pin_value){ 0 };
}
void package_graph_chibi_finish()
{
    finish_call();
    arena_free(&inputs);
    for (int b = 0; b < 2; b++) {
        authoring_packages_free(owners[b]);
        owners[b] = NULL;
        programs[b] = NULL;
    }
    native_capabilities_free(native);
    native = NULL;
    attempted = false;
}
static bool prepare()
{
    if (attempted) {
        return programs[0] && programs[1];
    }
    attempted = true;
    native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                       NATIVE_ARRAY_DEFINITION_PACK };
    char error[192];
    native = native_capabilities_from_definition_packs(NULL, 0, packs, 2, error, sizeof error);
    if (!native) {
        fprintf(stderr, "authored graph native: %s\n", error);
    }
    REQUIRE(native);
    for (int b = 0; b < 2; b++) {
        source_package_input files[] = {
            { "package_graph/Graph.chibi",
              PACKAGE_GRAPH_CHIBI_SOURCE,
              sizeof PACKAGE_GRAPH_CHIBI_SOURCE - 1 },
            { "/sdk/core/strings/Strings.chibi",
              CORE_STRINGS_SOURCE,
              sizeof CORE_STRINGS_SOURCE - 1 },
        };
        source_package_collection collections[] = { { "core", "/sdk/core" } };
        source_package_snapshot_options options = {
            .project_root = "/compiler",
            .files = files,
            .file_count = 2,
            .collections = collections,
            .collection_count = 1,
        };
        source_package_snapshot *snapshot = source_package_snapshot_new(&options);
        REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
        script_lower_bindings bindings = authoring_native_bindings(native);
        owners[b] = authoring_packages_prepare(
            snapshot,
            native_capabilities_catalog(native),
            &bindings,
            backends[b],
            NULL
        );
        REQUIRE(owners[b]);
        if (!authoring_packages_valid(owners[b])) {
            const char *message = authoring_packages_error(owners[b]);
            if (message) {
                fprintf(stderr, "authored graph: %s\n", message);
            }
            for (int i = 0; i < authoring_packages_count(owners[b]); i++) {
                const authoring_package *package = authoring_packages_at(owners[b], i);
                for (int d = 0; d < package->diagnostic_count; d++) {
                    const script_diagnostic *diagnostic = &package->diagnostics[d];
                    fprintf(
                        stderr,
                        "%s:%d:%d: %s\n",
                        package->identity,
                        diagnostic->range.line,
                        diagnostic->range.column,
                        diagnostic->message
                    );
                }
            }
        }
        REQUIRE(authoring_packages_valid(owners[b]));
        REQUIRE(authoring_packages_count(owners[b]) == 2);
        for (int i = 0; i < authoring_packages_count(owners[b]); i++) {
            const authoring_package *package = authoring_packages_at(owners[b], i);
            if (!strcmp(package->identity, "project:package_graph")) {
                programs[b] = package;
            }
        }
        REQUIRE(programs[b] && programs[b]->module);
    }
    return true;
}
static uint64_t exported(int backend, const char *name)
{
    const script_package_exports *exports = programs[backend]->exports;
    for (int i = 0; i < exports->member_count; i++) {
        if (!strcmp(exports->members[i].name, name)) {
            return exports->members[i].id;
        }
    }
    return 0;
}
static bool invoke(int backend, const char *name, const pin_value *arguments, int count, pin_type type)
{
    finish_call();
    uint64_t id = exported(backend, name);
    REQUIRE(id);
    execution_module *module = programs[backend]->module;
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info info = execution_module_function_at(module, i);
        if (info.id == id) {
            function = info.body;
        }
    }
    REQUIRE(function);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    output = (pin_value){ .type = type };
    context = (behavior_function_context){
        .activation = behavior_function_resumable(function) ? &activation : NULL,
        .arguments = arguments,
        .argument_count = count,
        .results = &output,
        .budget = !strcmp(name, "Build") &&
                          ((const pin_array *)arguments[0].ptr)->count > 4
                    ? 50000000
                    : 100000,
    };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &context, backends[backend] == TEST_COMPILED_BACKEND);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK) {
        fprintf(stderr, "authored graph %s failed: status %d\n", name, status);
    }
    REQUIRE(status == FLOW_OK && context.returned && output.type == type);
    compiler_fixture_box(&storage, &output);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(backend == 0 ? runs.counts.bytecode_entries > 0 : !runs.counts.bytecode_entries);
    REQUIRE(backends[backend] != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
    REQUIRE(backends[backend] != BEHAVIOR_WASM || runs.counts.wasm_entries > 0);
    return true;
}
static const pin_record *layout(int backend, const char *name)
{
    const script_catalog *catalog = programs[backend]->exports->metadata;
    const script_catalog_type *type = script_catalog_type_by_id(catalog, exported(backend, name));
    if (!type || !type->authored_record || type->field_count > 8) {
        return NULL;
    }
    type_field fields[8] = { 0 };
    for (int i = 0; i < type->field_count; i++) {
        fields[i] = (type_field){ .name = type->fields[i].name,
                                  .type = type->fields[i].type.storage };
    }
    return pin_record_create(&inputs, type->name, type->id, fields, type->field_count);
}
static pin_value row(const pin_record *record)
{
    if (!record) {
        return (pin_value){ 0 };
    }
    void *data = arena_alloc(&inputs, record->type.size);
    memset(data, 0, record->type.size);
    return (pin_value){ .type = PIN_STRUCT,
                        .ptr = data,
                        .type_name = record->type.name,
                        .record = record };
}
static bool write(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&inputs, row->ptr, field, value));
    return true;
}
static pin_value text(const char *value)
{
    value = value ? value : "";
    pin_value result;
    const char *error = pin_value_text(&inputs, &result, value, strlen(value));
    if (error) {
        abort();
    }
    return result;
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static bool pack(
    int backend,
    const source_package_file *files,
    int file_count,
    const source_package_import *imports,
    int import_count,
    pin_value *arguments
)
{
    const pin_record *file = layout(backend, "File"),
                     *import = layout(backend, "Import");
    REQUIRE(file && import);
    pin_array *
        file_array = pin_array_snapshot_allocate(&inputs, PIN_STRUCT, file->type.name, file_count);
    pin_array *import_array = pin_array_snapshot_allocate(
        &inputs,
        PIN_STRUCT,
        import->type.name,
        import_count
    );
    REQUIRE(file_array && import_array);
    file_array->record = file;
    import_array->record = import;
    for (int i = 0; i < file_count; i++) {
        pin_value value = row(file);
        REQUIRE(
            write(&value, "path", text(files[i].path)) &&
            write(&value, "directory", text(files[i].directory)) &&
            write(&value, "name", text(files[i].name))
        );
        file_array->items[i] = value;
    }
    for (int i = 0; i < import_count; i++) {
        pin_value value = row(import);
        REQUIRE(
            write(&value, "file", integer(imports[i].file)) &&
            write(&value, "directory", text(imports[i].directory)) &&
            write(&value, "alias", text(imports[i].alias)) &&
            write(&value, "explicit_alias", boolean(imports[i].alias != NULL))
        );
        import_array->items[i] = value;
    }
    arguments[0] = (pin_value){ .type = PIN_STRUCT,
                                .container = PIN_CONTAINER_ARRAY,
                                .ptr = file_array,
                                .type_name = file->type.name,
                                .record = file };
    arguments[1] = (pin_value){ .type = PIN_STRUCT,
                                .container = PIN_CONTAINER_ARRAY,
                                .ptr = import_array,
                                .type_name = import->type.name,
                                .record = import };
    return true;
}
static bool field(const pin_value *record, const char *name, pin_type type, pin_value *value)
{
    const type_field *descriptor = pin_type_field_path(pin_value_type(record), name);
    REQUIRE(
        record->type == PIN_STRUCT && record->ptr && descriptor && descriptor->type == type
    );
    REQUIRE(!pin_read_field(&storage, record->ptr, descriptor, value));
    REQUIRE(value->type == type && value->container == PIN_CONTAINER_SINGLE);
    return true;
}
static bool number(const pin_value *record, const char *name, int expected)
{
    pin_value value;
    REQUIRE(field(record, name, PIN_I32, &value));
    REQUIRE(value.i == expected);
    return true;
}
static bool string(const pin_value *record, const char *name, const char *expected)
{
    pin_value value;
    REQUIRE(field(record, name, PIN_STR, &value));
    REQUIRE(pin_text(value).bytes && !strcmp(pin_text(value).bytes, expected));
    return true;
}
static const script_catalog_variant *variant(int backend, const pin_value *value, const char *name)
{
    const script_catalog_type *type = script_catalog_type_by_id(
        programs[backend]->exports->metadata,
        exported(backend, "Graph")
    );
    const pin_union *payload = value->ptr;
    for (int i = 0; type && payload && i < type->variant_count; i++) {
        if (payload->tag == type->variants[i].tag &&
            !strcmp(type->variants[i].name, name)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static const pin_value *payload(
    const pin_value *value,
    const script_catalog_variant *variant,
    const char *name
)
{
    const pin_union *data = value->ptr;
    if (!data || !variant || data->count != variant->field_count) {
        return NULL;
    }
    for (int i = 0; i < variant->field_count; i++) {
        if (!strcmp(variant->fields[i].name, name)) {
            return &data->values[i];
        }
    }
    return NULL;
}
static bool array(const pin_value *value, pin_type type, const pin_array **out)
{
    REQUIRE(value && value->type == type && value->container == PIN_CONTAINER_ARRAY);
    static const pin_array empty_int = { .element_type = PIN_I32 };
    static const pin_array empty_record = { .element_type = PIN_STRUCT };
    *out = value->ptr      ? value->ptr
         : type == PIN_I32 ? &empty_int
                           : &empty_record;
    REQUIRE((*out)->count >= 0 && (*out)->element_type == type);
    REQUIRE(!(*out)->count || (*out)->items);
    return true;
}
static bool integers(const pin_array *values, int index, int expected)
{
    REQUIRE(index >= 0 && index < values->count);
    REQUIRE(values->items[index].type == PIN_I32 && values->items[index].i == expected);
    return true;
}
static const char *alias(const source_package_import *import)
{
    if (import->alias) {
        return import->alias;
    }
    const char *slash = strrchr(import->directory, '/');
    return slash ? slash + 1 : import->directory;
}
static bool compare_error(int backend, const source_package_error *expected)
{
    const script_catalog_variant *rejected = variant(backend, &output, "Rejected");
    REQUIRE(rejected);
    const pin_value *error = payload(&output, rejected, "error");
    const pin_array *cycle;
    REQUIRE(error && array(payload(&output, rejected, "cycle"), PIN_I32, &cycle));
    REQUIRE(number(error, "code", expected->code));
    REQUIRE(number(error, "file", expected->file));
    REQUIRE(number(error, "import_index", expected->import));
    REQUIRE(number(error, "related_file", expected->related_file));
    REQUIRE(number(error, "related_import", expected->related_import));
    REQUIRE(number(error, "cycle_count", expected->cycle_count));
    REQUIRE(cycle->count == SOURCE_PACKAGE_MAX_FILES);
    for (int i = 0; i < expected->cycle_count; i++) {
        REQUIRE(integers(cycle, i, expected->cycle[i]));
    }
    return true;
}
static bool compare_graph(
    int backend,
    const source_package_file *files,
    int file_count,
    const source_package_import *imports,
    int import_count,
    const source_packages *expected
)
{
    const script_catalog_variant *accepted = variant(backend, &output, "Accepted");
    REQUIRE(accepted);
    const pin_array *packages, *members, *file_packages, *order, *edges, *bindings;
    REQUIRE(array(payload(&output, accepted, "packages"), PIN_STRUCT, &packages));
    REQUIRE(array(payload(&output, accepted, "members"), PIN_I32, &members));
    REQUIRE(array(payload(&output, accepted, "file_packages"), PIN_I32, &file_packages));
    REQUIRE(array(payload(&output, accepted, "order"), PIN_I32, &order));
    REQUIRE(array(payload(&output, accepted, "edges"), PIN_STRUCT, &edges));
    REQUIRE(array(payload(&output, accepted, "bindings"), PIN_STRUCT, &bindings));
    int count = source_packages_count(expected), member = 0;
    REQUIRE(packages->count == count && order->count == count);
    REQUIRE(members->count == file_count && file_packages->count == file_count);
    for (int p = 0; p < count; p++) {
        const source_package *package = source_packages_at(expected, p);
        const pin_value *actual = &packages->items[p];
        REQUIRE(
            string(actual, "directory", package->directory) &&
            string(actual, "name", package->name)
        );
        REQUIRE(
            number(actual, "member_start", member) &&
            number(actual, "member_count", package->file_count)
        );
        for (int i = 0; i < package->file_count; i++) {
            REQUIRE(integers(members, member++, package->files[i]));
        }
        REQUIRE(integers(order, p, source_packages_order(expected, p)));
    }
    REQUIRE(member == file_count);
    for (int i = 0; i < file_count; i++) {
        REQUIRE(integers(file_packages, i, source_packages_file(expected, i)));
    }
    /* Decode observations only: the C graph remains the sole oracle for every
     * matrix cell, including absent edges and the first import for duplicates. */
    int *matrix = arena_alloc(&inputs, (size_t)(count ? count * count : 1) * sizeof *matrix);
    for (int i = 0; i < count * count; i++) {
        matrix[i] = -1;
    }
    REQUIRE(edges->count <= import_count);
    for (int i = 0; i < edges->count; i++) {
        pin_value from, to, import;
        REQUIRE(
            field(&edges->items[i], "from", PIN_I32, &from) &&
            field(&edges->items[i], "to", PIN_I32, &to) &&
            field(&edges->items[i], "import_index", PIN_I32, &import)
        );
        REQUIRE(from.i >= 0 && from.i < count && to.i >= 0 && to.i < count);
        REQUIRE(
            import.i >= 0 && import.i < import_count &&
            matrix[from.i * count + to.i] == -1
        );
        matrix[from.i * count + to.i] = import.i;
    }
    for (int from = 0; from < count; from++) {
        for (int to = 0; to < count; to++) {
            REQUIRE(
                matrix[from * count + to] ==
                source_packages_dependency(expected, from, to)
            );
        }
    }
    int binding_count = 0;
    REQUIRE(bindings->count <= import_count);
    for (int i = 0; i < import_count; i++) {
        const char *name = alias(&imports[i]);
        if (!strcmp(name, "_")) {
            continue;
        }
        binding_count++;
        int matches = 0;
        for (int b = 0; b < bindings->count; b++) {
            pin_value index;
            REQUIRE(field(&bindings->items[b], "import_index", PIN_I32, &index));
            if (index.i == i) {
                matches++;
                REQUIRE(string(&bindings->items[b], "name", name));
                REQUIRE(number(&bindings->items[b], "file", imports[i].file));
                REQUIRE(number(
                    &bindings->items[b],
                    "target",
                    source_packages_lookup(expected, imports[i].file, name)
                ));
            }
        }
        REQUIRE(matches == 1);
    }
    REQUIRE(bindings->count == binding_count);
    (void)files;
    return true;
}
static bool query(int backend, const char *name, const pin_value *arguments, int count, int expected)
{
    REQUIRE(invoke(backend, name, arguments, count, PIN_I32));
    REQUIRE(output.i == expected);
    return true;
}
static bool queries(
    int backend,
    int file_count,
    const source_package_import *imports,
    int import_count,
    const source_packages *expected
)
{
    /* Detach Build's result before the next invocation releases its storage. */
    pin_value graph;
    REQUIRE(!compiler_fixture_snapshot(&inputs, &graph, &output));
    pin_value arguments[] = { graph, integer(0), integer(0) };
    int count = source_packages_count(expected);
    REQUIRE(query(backend, "Count", arguments, 1, count));
    if (!count) {
        return true;
    }
    const int file_indices[] = { 0, file_count - 1 };
    for (int i = 0; i < 2; i++) {
        arguments[1] = integer(file_indices[i]);
        REQUIRE(
            query(backend, "FilePackage", arguments, 2, source_packages_file(expected, file_indices[i]))
        );
    }
    const int order_indices[] = { 0, count - 1 };
    for (int i = 0; i < 2; i++) {
        arguments[1] = integer(order_indices[i]);
        REQUIRE(
            query(backend, "Order", arguments, 2, source_packages_order(expected, order_indices[i]))
        );
        REQUIRE(invoke(backend, "PackageAt", arguments, 2, PIN_STRUCT));
        const source_package *package = source_packages_at(expected, order_indices[i]);
        REQUIRE(
            string(&output, "directory", package->directory) &&
            string(&output, "name", package->name)
        );
        REQUIRE(number(&output, "member_count", package->file_count));
        int member_start = 0;
        for (int p = 0; p < order_indices[i]; p++) {
            member_start += source_packages_at(expected, p)->file_count;
        }
        REQUIRE(number(&output, "member_start", member_start));
    }
    arguments[1] = integer(0);
    arguments[2] = text("missing_alias");
    REQUIRE(query(backend, "Lookup", arguments, 3, -1));
    for (int i = 0; i < import_count && i < 3; i++) {
        arguments[1] = integer(imports[i].file);
        arguments[2] = text(alias(&imports[i]));
        REQUIRE(query(
            backend,
            "Lookup",
            arguments,
            3,
            source_packages_lookup(expected, imports[i].file, alias(&imports[i]))
        ));
    }
    arguments[1] = integer(0);
    arguments[2] = integer(0);
    REQUIRE(
        query(backend, "Dependency", arguments, 3, source_packages_dependency(expected, 0, 0))
    );
    arguments[1] = integer(count - 1);
    arguments[2] = integer(0);
    REQUIRE(
        query(backend, "Dependency", arguments, 3, source_packages_dependency(expected, count - 1, 0))
    );
    return true;
}
bool package_graph_chibi_compare(
    const source_package_file *files,
    int file_count,
    const source_package_import *imports,
    int import_count,
    const source_packages *expected,
    const source_package_error *error
)
{
    REQUIRE(prepare());
    REQUIRE(
        file_count >= 0 && file_count <= SOURCE_PACKAGE_MAX_FILES &&
        import_count >= 0 && import_count <= 255
    );
    for (int b = 0; b < 2; b++) {
        finish_call();
        arena_free(&inputs);
        pin_value arguments[2];
        REQUIRE(pack(b, files, file_count, imports, import_count, arguments));
        REQUIRE(invoke(b, "Build", arguments, 2, PIN_UNION));
        if (expected) {
            REQUIRE(compare_graph(b, files, file_count, imports, import_count, expected));
            REQUIRE(queries(b, file_count, imports, import_count, expected));
        } else {
            REQUIRE(compare_error(b, error));
        }
    }
    finish_call();
    arena_free(&inputs);
    return true;
}
