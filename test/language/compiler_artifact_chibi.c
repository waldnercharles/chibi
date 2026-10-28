#include "compiler_artifact_chibi.h"

#include "compiler_fixture.h"
#include "execution/function_builder.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "package_core_source.h"
#include "package_graph_chibi_source.h"
#include "package_paths_chibi_source.h"
#include "package_routes_chibi_source.h"
#include "package_snapshot_chibi_source.h"
#include "package_syntax_chibi_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
#include "support/asset_json.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"
#include "types/value_document.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One bounded producer process, three cases: the original eight-package closure
 * and two tiny binary fixtures, twenty preparation parses total. The original
 * five C snapshots and five authored calls retain three million instructions;
 * the binary fixtures have no source calls. Four retained trees add raw flag/root
 * comparisons. One bounded decode rejects the previous syntax ABI without an
 * invocation, preparation or process. Product hosts receive only prepared
 * compiler artifacts and requests. */
enum
{
    ARTIFACT_CAP = 16 * 1024 * 1024,
    INPUT_CAP = 8 * 1024,
    OUTPUT_CAP = 256 * 1024,
    BINARY_CAP = 64 * 1024
};
static compiler_fixture fixture;
static const char *directory;
static arena storage, result_storage;
static av_doc *requests, *expected;
static yyjson_mut_doc *encoded;
static source_package_snapshot *oracle;
static native_capabilities *previous_syntax;
static package_collection *incompatible;
static compiler_fixture_node raw[1024];
static bool named_seen[2], extra_seen[2];
static unsigned char *blobs[8], *artifact, *reordered;
static size_t blob_sizes[8];
static FILE *file;
static char *json;
static void cleanup()
{
    if (file) {
        fclose(file);
        file = NULL;
    }
    free(json);
    json = NULL;
    yyjson_mut_doc_free(encoded);
    encoded = NULL;
    av_doc_free(requests);
    requests = NULL;
    av_doc_free(expected);
    expected = NULL;
    source_package_snapshot_free(oracle);
    oracle = NULL;
    if (incompatible) {
        package_collection_free(incompatible);
        incompatible = NULL;
    }
    native_capabilities_free(previous_syntax);
    previous_syntax = NULL;
    named_seen[0] = named_seen[1] = extra_seen[0] = extra_seen[1] = false;
    for (int i = 0; i < 8; i++) {
        free(blobs[i]);
        blobs[i] = NULL;
    }
    free(artifact);
    artifact = NULL;
    free(reordered);
    reordered = NULL;
    arena_free(&storage);
    arena_free(&result_storage);
    compiler_fixture_free(&fixture);
}
static bool write_bytes(const char *name, const void *bytes, size_t size)
{
    char path[4096];
    int count = snprintf(path, sizeof path, "%s/%s", directory, name);
    REQUIRE(count > 0 && count < (int)sizeof path);
    file = fopen(path, "wb");
    REQUIRE(file);
    bool valid = fwrite(bytes, 1, size, file) == size;
    valid = fclose(file) == 0 && valid;
    file = NULL;
    REQUIRE(valid);
    return true;
}
static bool write_document(const char *name, const av_value *root, size_t cap)
{
    encoded = yyjson_mut_doc_new(NULL);
    REQUIRE(encoded);
    yyjson_mut_doc_set_root(encoded, av_to_json(encoded, root));
    size_t length;
    json = yyjson_mut_write(encoded, YYJSON_WRITE_ALLOW_INVALID_UNICODE, &length);
    REQUIRE(json && length && length <= cap);
    REQUIRE(write_bytes(name, json, length));
    free(json);
    json = NULL;
    yyjson_mut_doc_free(encoded);
    encoded = NULL;
    return true;
}
static av_value *record(av_doc *doc, const char *path, const char *text)
{
    av_value *value = av_obj(doc);
    av_obj_add_str(doc, value, "path", path);
    av_obj_add_str(doc, value, "text", text);
    av_obj_add_bool(doc, value, "has_path", true);
    av_obj_add_bool(doc, value, "has_text", true);
    return value;
}
static av_value *request(av_doc *doc, const source_package_input *files, int count, bool core)
{
    av_value *arguments = av_arr(doc), *options = av_obj(doc),
             *collections = av_arr(doc), *inputs = av_arr(doc),
             *sizes = av_arr(doc);
    av_obj_add_str(doc, options, "project", "/workspace/project");
    av_obj_add_bool(doc, options, "has_project", true);
    av_obj_add_bool(doc, options, "has_options", true);
    av_obj_add_bool(doc, options, "has_files", true);
    av_obj_add_bool(doc, options, "has_collections", core);
    av_obj_add_int(doc, options, "file_count", count);
    av_obj_add_int(doc, options, "collection_count", core ? 1 : 0);
    if (core) {
        av_value *value = av_obj(doc);
        av_obj_add_str(doc, value, "name", "core");
        av_obj_add_str(doc, value, "path", "/sdk/core");
        av_obj_add_bool(doc, value, "has_name", true);
        av_obj_add_bool(doc, value, "has_path", true);
        av_arr_append(collections, value);
    }
    for (int i = 0; i < count; i++) {
        /* Fixed inputs are terminated; NUL's prefix and the independent exact
         * size preserve the engine string ABI and authored NUL rejection. */
        av_arr_append(inputs, record(doc, files[i].path, files[i].text));
        av_arr_append(sizes, av_uint(doc, (uint64_t)files[i].size));
    }
    av_arr_append(arguments, options);
    av_arr_append(arguments, collections);
    av_arr_append(arguments, inputs);
    av_arr_append(arguments, sizes);
    return arguments;
}
static bool field_value(const pin_value *value, const char *name, pin_value *out)
{
    REQUIRE(value->type == PIN_STRUCT && value->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(value), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&result_storage, value->ptr, field, out));
    return true;
}
static bool field_string(const pin_value *value, const char *name, const char *text)
{
    pin_value result;
    REQUIRE(field_value(value, name, &result));
    REQUIRE(
        result.type == PIN_STR && pin_text(result).bytes &&
        !strcmp(pin_text(result).bytes, text)
    );
    return true;
}
static bool field_integer(const pin_value *value, const char *name, int expected)
{
    pin_value result;
    REQUIRE(field_value(value, name, &result));
    REQUIRE(result.type == PIN_I32 && result.i == expected);
    return true;
}
static bool field_flag(const pin_value *value, const char *name, bool expected)
{
    pin_value result;
    REQUIRE(field_value(value, name, &result));
    REQUIRE(result.type == PIN_BOOL && result.b == expected);
    return true;
}
static bool payload_array(
    const pin_union *data,
    const script_catalog_variant *variant,
    const char *name,
    const pin_array **out
)
{
    REQUIRE(data->count == variant->field_count);
    for (int i = 0; i < data->count; i++) {
        if (strcmp(variant->fields[i].name, name)) {
            continue;
        }
        const pin_value *value = &data->values[i];
        REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
        static const pin_array empty = { .element_type = PIN_STRUCT };
        *out = value->ptr ? value->ptr : &empty;
        REQUIRE((*out)->element_type == PIN_STRUCT && (*out)->count >= 0);
        REQUIRE(!(*out)->count || (*out)->items);
        return true;
    }
    REQUIRE(false);
    return false;
}
static bool source_facts(const pin_union *data, const script_catalog_variant *variant)
{
    const pin_array *documents, *forest;
    REQUIRE(payload_array(data, variant, "documents", &documents));
    REQUIRE(payload_array(data, variant, "nodes", &forest));
    int count, offset = 0;
    const script_package_document *expected = source_package_snapshot_documents(oracle, &count);
    REQUIRE(documents->count == count && count <= 3 && forest->count <= 3 * 1024);
    for (int i = 0; i < count; i++) {
        const pin_value *document = &documents->items[i];
        int nodes = 0;
        if (expected[i].syntax) {
            REQUIRE(
                compiler_fixture_nodes(ts_tree_root_node(expected[i].syntax->tree), raw, 1024, 128, &nodes)
            );
        }
        REQUIRE(field_flag(document, "has_syntax", expected[i].syntax != NULL));
        REQUIRE(field_integer(document, "syntax_root", expected[i].syntax ? offset : -1));
        REQUIRE(field_integer(document, "node_start", expected[i].syntax ? offset : 0));
        REQUIRE(field_integer(document, "node_count", nodes));
        for (int n = 0; n < nodes; n++) {
            REQUIRE(offset + n < forest->count);
            const pin_value *node = &forest->items[offset + n];
            bool named = ts_node_is_named(raw[n].node),
                 extra = ts_node_is_extra(raw[n].node);
            REQUIRE(field_flag(node, "is_named", named) && field_flag(node, "is_extra", extra));
            named_seen[named] = true;
            extra_seen[extra] = true;
        }
        offset += nodes;
    }
    REQUIRE(offset == forest->count);
    return true;
}
static bool reject_previous_syntax_abi(size_t length)
{
    /* Decode the real closure against the exact previous thirteen-result
     * descriptor. The callback is never invoked with this incompatible ABI. */
    native_definition_pack syntax = COMPILER_SYNTAX_DEFINITION_PACK;
    REQUIRE(syntax.definition_count == 1 && syntax.definitions[0].output_count == 15);
    native_capability_definition definition = syntax.definitions[0];
    definition.output_count = 13;
    syntax.definitions = &definition;
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_JSON_DEFINITION_PACK,
                                             syntax };
    char error[256] = { 0 };
    previous_syntax = native_capabilities_from_definition_packs(NULL, 0, packs, 4, error, sizeof error);
    REQUIRE(previous_syntax);
    const script_catalog *current = native_capabilities_catalog(fixture.native),
                         *previous = native_capabilities_catalog(previous_syntax);
    int index = script_catalog_find_capability(current, "compiler_syntax_parse", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(index >= 0);
    const script_catalog_capability
        *capability = &current->capabilities[index],
        *old = script_catalog_capability_by_id(previous, capability->id);
    REQUIRE(old && old->result_count == 13 && capability->result_count == 15);
    REQUIRE(
        script_catalog_capability_abi(current, capability) !=
        script_catalog_capability_abi(previous, old)
    );
    for (int i = 13; i < 15; i++) {
        REQUIRE(capability->results[i].type.storage == PIN_BOOL);
        REQUIRE(capability->results[i].type.container == PIN_CONTAINER_ARRAY);
        REQUIRE(!strcmp(capability->results[i].name, i == 13 ? "is_named" : "is_extra"));
    }
    native_prepared_context context = { .native = previous_syntax };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    size_t remaining = COMPILER_HOST_MAX_BYTES;
    uint64_t root = UINT64_MAX;
    incompatible = compiler_artifact_read(
        artifact,
        length,
        &linker,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    REQUIRE(!incompatible && remaining == COMPILER_HOST_MAX_BYTES && root == 0);
    REQUIRE(
        !strcmp(error, "Prepared module project:compiler_syntax: missing or incompatible host capability")
    );
    native_capabilities_free(previous_syntax);
    previous_syntax = NULL;
    return true;
}
static bool source_call(const behavior_function *function, const av_value *arguments, av_value *batch)
{
    arena_free(&storage);
    arena_free(&result_storage);
    int count;
    const pin_value *declarations = behavior_function_arguments(function, &count);
    REQUIRE(count == 4 && av_arr_size(arguments) == 4);
    pin_value packed[4], result;
    char error[256];
    for (int i = 0; i < 4; i++) {
        REQUIRE(!pin_value_read_document(
            &storage,
            declarations[i],
            av_arr_get(arguments, (size_t)i),
            &packed[i],
            error,
            sizeof error
        ));
    }
    REQUIRE(
        compiler_fixture_call(&fixture, 0, "project:source_snapshot", "Build", packed, 4, 3000000, &result_storage, &result)
    );
    arena_free(&storage);
    REQUIRE(result.type == PIN_UNION && result.ptr);
    const pin_union *data = result.ptr;
    const source_package_snapshot_error *failure = source_package_snapshot_error_at(oracle);
    const authoring_package *package = compiler_fixture_package(&fixture, 0, "project:source_snapshot");
    REQUIRE(package && package->exports);
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, 0, "project:source_snapshot", "Result")
    );
    const script_catalog_variant *variant = NULL;
    for (int i = 0; type && i < type->variant_count; i++) {
        if (type->variants[i].tag == data->tag) {
            variant = &type->variants[i];
        }
    }
    REQUIRE(variant && !strcmp(variant->name, failure ? "Rejected" : "Accepted"));
    if (failure) {
        REQUIRE(
            data->count == 7 &&
            field_string(&data->values[0], "message", failure->diagnostic.message)
        );
    } else {
        REQUIRE(
            source_packages_count(source_package_snapshot_graph(oracle)) == 3 &&
            data->count == 5
        );
    }
    REQUIRE(source_facts(data, variant));
    av_value *values = av_arr(expected),
             *projected = pin_value_to_document(expected, &result);
    REQUIRE(projected);
    av_arr_append(values, projected);
    av_arr_append(batch, values);
    return true;
}
TEST_CASE(compiler_actual_closure_produces_deterministic_artifact)
{
    source_package_input sources[] = {
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
        REQUIRE(sources[i].size < 16384);
    }
    REQUIRE(compiler_fixture_prepare_packs(&fixture, sources, 8, packs, 2));
    REQUIRE(authoring_packages_count(fixture.owners[0]) == 8);
    package_module rows[8], reverse[8];
    char error[256];
    for (int i = 0; i < 8; i++) {
        const authoring_package *package = authoring_packages_at(fixture.owners[0], i);
        REQUIRE(
            execution_prepared_write(package->module, &blobs[i], &blob_sizes[i], error, sizeof error)
        );
        REQUIRE(blob_sizes[i] && blob_sizes[i] <= ARTIFACT_CAP);
        rows[i] = (package_module){ package->identity,
                                    execution_module_id(package->module),
                                    blobs[i],
                                    blob_sizes[i] };
        reverse[7 - i] = rows[i];
    }
    const authoring_package *root = compiler_fixture_package(&fixture, 0, "project:source_snapshot");
    REQUIRE(root && root->module);
    size_t length, second;
    REQUIRE(
        compiler_artifact_write(rows, 8, execution_module_id(root->module), &artifact, &length, error, sizeof error)
    );
    REQUIRE(
        compiler_artifact_write(reverse, 8, execution_module_id(root->module), &reordered, &second, error, sizeof error)
    );
    REQUIRE(
        length && length <= ARTIFACT_CAP && second == length &&
        !memcmp(artifact, reordered, length)
    );
    REQUIRE(reject_previous_syntax_abi(length));
    REQUIRE(write_bytes("compiler.artifact", artifact, length));
    uint64_t entry = compiler_fixture_export(&fixture, 0, "project:source_snapshot", "Build");
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(root->module); i++) {
        execution_module_function_info info = execution_module_function_at(root->module, i);
        if (info.id == entry) {
            function = info.body;
        }
    }
    REQUIRE(function && entry);
    char id[32];
    int id_length = snprintf(id, sizeof id, "%" PRIx64, entry);
    REQUIRE(
        id_length > 0 && id_length < (int)sizeof id &&
        write_bytes("entry.txt", id, (size_t)id_length)
    );
    static const char
        app[] = "package app\n// raw extra\nimport lib \"../math\"\nimport _ "
                "\"core:scalar\"\n",
        math[] = "package numbers\n", scalar[] = "package scalar\n";
    static const char syntax[] = "package app\nbroken ::\n",
                      invalid[] = "package app\n// \x80",
                      nul[] = "package app\n\0tail";
    const source_package_input valid[] = {
        { "app/a.chibi", app, sizeof app - 1 },
        { "math/a.chibi", math, sizeof math - 1 },
        { "/sdk/core/scalar/a.chibi", scalar, sizeof scalar - 1 }
    };
    const source_package_input bad[] = { { "app/a.chibi", syntax, sizeof syntax - 1 },
                                         { "app/a.chibi", invalid, sizeof invalid - 1 },
                                         { "app/a.chibi", nul, sizeof nul - 1 },
                                         { "app/a.chibi", "tiny", SIZE_MAX } };
    const source_package_collection collection = { "core", "/sdk/core" };
    requests = av_doc_new();
    expected = av_doc_new();
    REQUIRE(requests && expected);
    av_value *batch = av_arr(requests), *golden = av_arr(expected);
    av_doc_set_root(requests, batch);
    av_doc_set_root(expected, golden);
    for (int i = 0; i < 5; i++) {
        source_package_snapshot_free(oracle);
        oracle = NULL;
        source_package_snapshot_options options = {
            .project_root = "/workspace/project",
            .files = i ? &bad[i - 1] : valid,
            .file_count = i ? 1 : 3,
            .collections = i ? NULL : &collection,
            .collection_count = i ? 0 : 1
        };
        oracle = source_package_snapshot_new(&options);
        REQUIRE(oracle);
        av_value *arguments = request(requests, options.files, options.file_count, i == 0);
        av_arr_append(batch, arguments);
        REQUIRE(source_call(function, arguments, golden));
    }
    REQUIRE(av_arr_size(batch) == 5 && av_arr_size(golden) == 5);
    REQUIRE(named_seen[0] && named_seen[1] && extra_seen[0] && extra_seen[1]);
    REQUIRE(
        write_document("requests.json", batch, INPUT_CAP) &&
        write_document("expected.json", golden, OUTPUT_CAP)
    );
    printf("Compiler artifact producer: 8 modules, %zu bytes, 5 source calls\n", length);
    return true;
}
/* Tiny transport fixture only. Stage zero still authors/serializes it; echoing
 * these bytes is not authored artifact encoding or compiler self-compilation. */
static bool write_byte_input(const char *name, const unsigned char *bytes, size_t length)
{
    requests = av_doc_new();
    REQUIRE(requests);
    av_value *arguments = av_arr(requests), *values = av_arr(requests);
    av_doc_set_root(requests, arguments);
    av_arr_append(arguments, values);
    for (size_t i = 0; i < length; i++) {
        av_arr_append(values, av_uint(requests, bytes[i]));
    }
    REQUIRE(write_document(name, arguments, OUTPUT_CAP));
    av_doc_free(requests);
    requests = NULL;
    return true;
}
TEST_CASE(binary_transport_fixture_produces_bounded_artifact)
{
    static const char
        source[] = "package binary_fixture\n"
                   "pub Echo :: (values: u8[]) -> u8[] { return values; }\n"
                   "pub Scalar :: () -> u8 { return u8(7); }\n"
                   "pub Integers :: () -> int[] { values: int[] = []; return "
                   "values; }\n"
                   "pub Loop :: (spin: bool) -> u8[] {\n"
                   "  while (spin) {}\n"
                   "  values: u8[] = []; return values;\n"
                   "}\n";
    REQUIRE(sizeof source - 1 <= 4096);
    source_package_input input = { "binary_fixture/Fixture.chibi",
                                   source,
                                   sizeof source - 1 };
    REQUIRE(compiler_fixture_prepare(&fixture, &input, 1));
    for (int b = 0; b < 2; b++) {
        REQUIRE(authoring_packages_count(fixture.owners[b]) == 1);
    }
    const authoring_package *root = compiler_fixture_package(&fixture, 0, "project:binary_fixture");
    REQUIRE(root && root->module && execution_module_function_count(root->module) == 4);
    char error[256];
    REQUIRE(execution_prepared_write(root->module, &blobs[0], &blob_sizes[0], error, sizeof error));
    package_module row = { root->identity,
                           execution_module_id(root->module),
                           blobs[0],
                           blob_sizes[0] };
    size_t length;
    REQUIRE(compiler_artifact_write(&row, 1, row.id, &artifact, &length, error, sizeof error));
    REQUIRE(length && length <= BINARY_CAP);
    REQUIRE(write_bytes("binary.artifact", artifact, length));
    const char *names[] = { "Echo", "Scalar", "Integers", "Loop" };
    const char *files[] = { "binary-echo.txt",
                            "binary-scalar.txt",
                            "binary-integers.txt",
                            "binary-loop.txt" };
    for (int i = 0; i < 4; i++) {
        uint64_t entry = compiler_fixture_export(&fixture, 0, "project:binary_fixture", names[i]);
        REQUIRE(
            entry &&
            entry == compiler_fixture_export(&fixture, 1, "project:binary_fixture", names[i])
        );
        char id[32];
        int count = snprintf(id, sizeof id, "%" PRIx64, entry);
        REQUIRE(count > 0 && (size_t)count < sizeof id);
        REQUIRE(write_bytes(files[i], id, (size_t)count));
    }
    REQUIRE(write_byte_input("binary-input.json", artifact, length));
    unsigned char octets[256];
    for (int i = 0; i < 256; i++) {
        octets[i] = (unsigned char)i;
    }
    REQUIRE(write_byte_input("octets.json", octets, sizeof octets));
    REQUIRE(write_bytes("empty.json", "[[]]", 4));
    printf("Binary transport producer: 1 module, %zu bytes, 0 source calls\n", length);
    return true;
}
/* Keep the existing four-export transport fixture under its 32 KiB cap. */
TEST_CASE(binary_sized_fixture_produces_bounded_artifact)
{
    static const char source[] = "package binary_sized\n"
                                 "pub Sized :: (count: int) -> u8[] {\n"
                                 "  return resize([u8(37)], count);\n"
                                 "}\n";
    const source_package_input input = { "binary_sized/Fixture.chibi",
                                         source,
                                         sizeof source - 1 };
    REQUIRE(compiler_fixture_prepare(&fixture, &input, 1));
    const authoring_package *root = compiler_fixture_package(&fixture, 0, "project:binary_sized");
    REQUIRE(root && root->module && execution_module_function_count(root->module) == 1);
    char error[256];
    REQUIRE(execution_prepared_write(root->module, &blobs[0], &blob_sizes[0], error, sizeof error));
    package_module row = { root->identity,
                           execution_module_id(root->module),
                           blobs[0],
                           blob_sizes[0] };
    size_t length;
    REQUIRE(compiler_artifact_write(&row, 1, row.id, &artifact, &length, error, sizeof error));
    REQUIRE(length && length <= BINARY_CAP);
    REQUIRE(write_bytes("sized.artifact", artifact, length));
    return true;
}
void test_language_compiler_artifact_write(const char *path)
{
    directory = path;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(compiler_actual_closure_produces_deterministic_artifact);
    RUN_TEST_CASE(binary_transport_fixture_produces_bounded_artifact);
    RUN_TEST_CASE(binary_sized_fixture_produces_bounded_artifact);
    pu_clear_setup();
}
