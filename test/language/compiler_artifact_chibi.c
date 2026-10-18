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
#include "support/asset_json.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/union.h"
#include "types/value_document.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One bounded source producer: eight packages, sixteen preparation parses,
 * five C snapshots and five authored calls at three million instructions each.
 * The ordinary product host receives only serialized executables and requests. */
enum
{
    ARTIFACT_CAP = 16 * 1024 * 1024,
    INPUT_CAP = 8 * 1024,
    OUTPUT_CAP = 256 * 1024
};
static compiler_fixture fixture;
static const char *directory;
static arena storage, result_storage;
static av_doc *requests, *expected;
static yyjson_mut_doc *encoded;
static source_package_snapshot *oracle;
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
static bool field_string(const pin_value *value, const char *name, const char *text)
{
    REQUIRE(value->type == PIN_STRUCT && value->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(value), name);
    pin_value result;
    REQUIRE(field);
    pin_read_field(value->ptr, field, &result);
    REQUIRE(result.type == PIN_STR && result.str && !strcmp(result.str, text));
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
    for (int i = 0; i < 8; i++) {
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
    static const char app[] = "package app\nimport lib \"../math\"\nimport _ "
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
    REQUIRE(
        write_document("requests.json", batch, INPUT_CAP) &&
        write_document("expected.json", golden, OUTPUT_CAP)
    );
    printf("Compiler artifact producer: 8 modules, %zu bytes, 5 source calls\n", length);
    return true;
}
void test_language_compiler_artifact_write(const char *path)
{
    directory = path;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(compiler_actual_closure_produces_deterministic_artifact);
    pu_clear_setup();
}
