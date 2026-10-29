#include "package_routes_chibi.h"

#include "authoring/source_package_routes.h"
#include "compiler_fixture.h"
#include "package_core_source.h"
#include "package_paths_chibi_source.h"
#include "package_routes_chibi_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_json.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forty fixed requests: 13 roots, 11 files, 16 imports. Each file/import gets
 * its own authored RootSet, giving 134 calls across two reused preparations.
 * Each call is capped at one million instructions. Four actual source files
 * per backend give eight preparation parses. Roots never come from C outputs.
 * At most three roots, 128 input bytes and 17 directory segments per request;
 * no filesystem, parser corpus, services, processes or generated combinations. */
typedef struct
{
    const char *id, *project;
    source_package_collection collections[2];
    int count;
} root_case;
static const root_case roots_cases[] = {
    { "baseline",
      "/workspace/./project",
      { { "core", "/sdk/./core" }, { "vendor", "/workspace/project/vendor" } },
      2 },
    { "relocated", "/moved/project", { { "core", "/sdk/core" } }, 1 },
    { "null-project", NULL, { { 0 } }, 0 },
    { "relative-project", "project", { { 0 } }, 0 },
    { "backslash-project", "/workspace\\project", { { 0 } }, 0 },
    { "duplicate-name",
      "/workspace/project",
      { { "core", "/sdk/core" }, { "core", "/other/core" } },
      2 },
    { "reserved-project", "/workspace/project", { { "project", "/other/project" } }, 1 },
    { "invalid-name", "/workspace/project", { { "bad-name", "/sdk/core" } }, 1 },
    { "null-name", "/workspace/project", { { NULL, "/sdk/core" } }, 1 },
    { "null-path", "/workspace/project", { { "core", NULL } }, 1 },
    { "relative-collection", "/workspace/project", { { "core", "core" } }, 1 },
    { "empty-name", "/workspace/project", { { "", "/sdk/core" } }, 1 },
    { "duplicate-canonical-root",
      "/workspace/project",
      { { "core", "/sdk/core" }, { "other", "/sdk/./core" } },
      2 },
};
typedef struct
{
    const char *id, *path;
    int roots;
} file_case;
static char depth16[128], depth17[128];
static const file_case file_cases[] = {
    { "project-file", "app/a.chibi", 0 },
    { "core-file", "/sdk/core/scalar/a.chibi", 0 },
    { "longest-root-file", "/workspace/project/vendor/math/a.chibi", 0 },
    { "normalized-file", "app/../math/./a.chibi", 0 },
    { "relocated-file", "/moved/project/app/a.chibi", 1 },
    { "null-file", NULL, 0 },
    { "empty-file", "", 0 },
    { "depth-16", depth16, 0 },
    { "outside-roots", "/outside/a.chibi", 0 },
    { "root-is-not-file", "/", 0 },
    { "depth-17", depth17, 0 },
};
typedef struct
{
    const char *id, *literal, *directory;
    int root;
} import_case;
static const import_case import_cases[] = {
    { "relative", "\"../math\"", "/workspace/project/app", 0 },
    { "core", "\"core:scalar\"", "/workspace/project/app", 0 },
    { "project", "\"project:math/.\"", "/workspace/project/app", 0 },
    { "empty-relative", "\"\"", "/workspace/project/app", 0 },
    { "longest-root-escape", "\"../../outside\"", "/workspace/project/vendor/math", 2 },
    { "unknown-collection", "\"missing:math\"", "/workspace/project/app", 0 },
    { "embedded-nul", "\"../ma\\u0000th\"", "/workspace/project/app", 0 },
    { "escaped-unicode", "\"../m\\u0061th\"", "/workspace/project/app", 0 },
    { "backslash", "\"..\\\\math\"", "/workspace/project/app", 0 },
    { "surrogate-pair", "\"core:\\ud83d\\ude00\"", "/workspace/project/app", 0 },
    { "invalid-surrogate", "\"core:\\ud800\"", "/workspace/project/app", 0 },
    { "malformed-json", "\"unterminated", "/workspace/project/app", 0 },
    { "json-nonstring", "[]", "/workspace/project/app", 0 },
    { "leading-nul", "\"\\u0000../math\"", "/workspace/project/app", 0 },
    { "empty-collection-tail", "\"core:\"", "/workspace/project/app", 0 },
    { "raw-utf8", "\"core:😀\"", "/workspace/project/app", 0 },
};
static const char IDENTITY[] = "project:package_routes";
static compiler_fixture fixture;
static arena input, oracle, output, root_output, retained[2][3], retained_oracle;
static pin_value saved[2][3];
static uint64_t saved_tag[2][3];
static source_package_collection saved_roots[3];
static int saved_root_count;
static source_package_routed_file saved_file;
static const char *saved_import;

static void cleanup()
{
    arena_free(&input);
    arena_free(&oracle);
    arena_free(&output);
    arena_free(&root_output);
}
static const char *copy(arena *storage, const char *value)
{
    if (!value) {
        value = "";
    }
    size_t size = strlen(value) + 1;
    char *owned = arena_alloc(storage, size);
    memcpy(owned, value, size);
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
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static bool row(int backend, const char *name, pin_value *out)
{
    const pin_record *record = compiler_fixture_record(&fixture, backend, IDENTITY, name, &input);
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
static bool same_string(const pin_value *row, const char *name, const char *expected)
{
    pin_value value;
    REQUIRE(
        read(row, name, &value) && value.type == PIN_STR &&
        pin_text(value).bytes && !strcmp(pin_text(value).bytes, expected)
    );
    return true;
}
static bool same_int(const pin_value *row, const char *name, int expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_I32 && value.i == expected);
    return true;
}
static bool call(
    int backend,
    const char *name,
    const pin_value *arguments,
    int count,
    arena *storage,
    pin_value *out
)
{
    bool valid = compiler_fixture_call(&fixture, backend, IDENTITY, name, arguments, count, 1000000, storage, out);
    arena_free(&input);
    REQUIRE(valid);
    return true;
}
static bool unpack(
    int backend,
    const char *type_name,
    const pin_value *result,
    const char *error,
    script_range range,
    const pin_value **value
)
{
    REQUIRE(result->type == PIN_UNION && result->ptr);
    const authoring_package *package = compiler_fixture_package(&fixture, backend, IDENTITY);
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, backend, IDENTITY, type_name)
    );
    const pin_union *data = result->ptr;
    const script_catalog_variant *kind = NULL;
    for (int i = 0; type && i < type->variant_count; i++) {
        if (type->variants[i].tag == data->tag) {
            kind = &type->variants[i];
        }
    }
    REQUIRE(kind && !strcmp(kind->name, error ? "Rejected" : "Accepted") && data->count == 1);
    *value = &data->values[0];
    if (error) {
        REQUIRE(same_string(*value, "message", error));
        REQUIRE(
            same_int(*value, "start", range.start) &&
            same_int(*value, "end", range.end)
        );
        REQUIRE(
            same_int(*value, "line", range.line) &&
            same_int(*value, "column", range.column) &&
            same_int(*value, "document", range.document)
        );
    }
    return true;
}
static bool save(int backend, int kind, const pin_value *result)
{
    REQUIRE(!compiler_fixture_snapshot(&retained[backend][kind], &saved[backend][kind], result));
    saved_tag[backend][kind] = ((const pin_union *)result->ptr)->tag;
    return true;
}
static bool owned(arena *storage, const pin_value *value, int depth)
{
    REQUIRE(depth <= 4);
    if (value->container == PIN_CONTAINER_ARRAY) {
        const pin_array *array = value->ptr;
        if (!array) {
            return true;
        }
        REQUIRE(arena_contains(storage, array) && array->count >= 0 && array->count <= 3);
        REQUIRE(!array->count || arena_contains(storage, array->items));
        for (int i = 0; i < array->count; i++) {
            REQUIRE(owned(storage, &array->items[i], depth + 1));
        }
        return true;
    }
    REQUIRE(value->container == PIN_CONTAINER_SINGLE);
    if (value->type == PIN_UNION) {
        const pin_union *data = value->ptr;
        REQUIRE(data && arena_contains(storage, data) && data->count == 1);
        return owned(storage, &data->values[0], depth + 1);
    }
    if (value->type == PIN_STRUCT) {
        REQUIRE(
            value->ptr && value->record && arena_contains(storage, value->ptr) &&
            arena_contains(storage, value->record)
        );
        const type_desc *type = pin_value_type(value);
        REQUIRE(type && type->field_count <= 8);
        for (int i = 0; i < type->field_count; i++) {
            pin_value field;
            REQUIRE(!pin_read_field(storage, value->ptr, &type->fields[i], &field));
            REQUIRE(owned(storage, &field, depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        REQUIRE(pin_text(*value).bytes && arena_contains(storage, value->text));
        return true;
    }
    REQUIRE(value->type == PIN_I32 || value->type == PIN_BOOL);
    return true;
}
static bool root_set(int backend, const root_case *item, arena *storage, pin_value *result)
{
    arena_free(&input);
    const pin_record *record = compiler_fixture_record(&fixture, backend, IDENTITY, "Collection", &input);
    REQUIRE(record);
    pin_array *collections = pin_array_snapshot_allocate(
        &input,
        PIN_STRUCT,
        record->type.name,
        item->count
    );
    REQUIRE(collections);
    collections->record = record;
    for (int i = 0; i < item->count; i++) {
        const source_package_collection *collection = &item->collections[i];
        pin_value value;
        REQUIRE(row(backend, "Collection", &value));
        REQUIRE(
            put(&value, "name", text(collection->name)) &&
            put(&value, "path", text(collection->root))
        );
        REQUIRE(
            put(&value, "has_name", boolean(collection->name != NULL)) &&
            put(&value, "has_path", boolean(collection->root != NULL))
        );
        collections->items[i] = value;
    }
    pin_value arguments[] = {
        text(item->project),
        boolean(item->project != NULL),
        { .type = PIN_STRUCT,
          .container = PIN_CONTAINER_ARRAY,
          .ptr = collections,
          .record = record,
          .type_name = record->type.name },
    };
    REQUIRE(call(backend, "RootSet", arguments, 3, storage, result));
    REQUIRE(owned(storage, result, 0));
    return true;
}
static bool compare_roots(const pin_value *value, const source_package_collection *roots, int count)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    const pin_array *array = value->ptr;
    REQUIRE(array && array->count == count);
    for (int i = 0; i < count; i++) {
        REQUIRE(
            same_string(&array->items[i], "name", roots[i].name) &&
            same_string(&array->items[i], "path", roots[i].root)
        );
    }
    return true;
}
static bool compare_file(const pin_value *value, const source_package_routed_file *file)
{
    REQUIRE(
        same_string(value, "path", file->path) &&
        same_string(value, "directory", file->directory)
    );
    REQUIRE(
        same_string(value, "key", file->key) &&
        same_string(value, "identity", file->identity)
    );
    REQUIRE(same_int(value, "root", file->root));
    return true;
}
static bool prepare()
{
    source_package_input files[] = {
        { "package_routes/Routes.chibi",
          PACKAGE_ROUTES_CHIBI_SOURCE,
          sizeof PACKAGE_ROUTES_CHIBI_SOURCE - 1 },
        { "package_paths/Paths.chibi",
          PACKAGE_PATHS_CHIBI_SOURCE,
          sizeof PACKAGE_PATHS_CHIBI_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
    };
    /* Core strings uses the production snapshot limits and full retention checks. */
    for (int i = 0; i < 3; i++) {
        REQUIRE(files[i].size < 16384);
    }
    return compiler_fixture_prepare_packs(&fixture, files, 4, &COMPILER_JSON_DEFINITION_PACK, 1);
}
TEST_CASE(package_routes_roots)
{
    REQUIRE(prepare());
    REQUIRE(sizeof roots_cases / sizeof roots_cases[0] == 13);
    for (int i = 0; i < 13; i++) {
        cleanup();
        source_package_collection roots[3];
        int count;
        const root_case *item = &roots_cases[i];
        const char *error = source_package_routes_roots(
            &oracle,
            item->project,
            item->collections,
            item->count,
            roots,
            &count
        );
        for (int b = 0; b < 2; b++) {
            arena_free(&output);
            pin_value result;
            const pin_value *value;
            REQUIRE(root_set(b, item, &output, &result));
            REQUIRE(
                unpack(b, "RootResult", &result, error, (script_range){ 0, 0, 1, 1, -1 }, &value)
            );
            if (!error) {
                REQUIRE(compare_roots(value, roots, count));
            }
            if (i == 0) {
                REQUIRE(save(b, 0, &result));
            }
        }
        if (i == 0) {
            saved_root_count = count;
            for (int r = 0; r < count; r++) {
                saved_roots[r] = (source_package_collection){
                    copy(&retained_oracle, roots[r].name),
                    copy(&retained_oracle, roots[r].root)
                };
            }
        }
    }
    return true;
}
static bool ready()
{
    REQUIRE(
        fixture.owners[0] && fixture.owners[1] &&
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1])
    );
    return true;
}
static void depth_path(char *out, int count)
{
    for (int i = 0; i < count; i++) {
        out[2 * i] = 'n';
        out[2 * i + 1] = '/';
    }
    strcpy(out + 2 * count, "a.chibi");
}
static bool authored_roots(
    int backend,
    const root_case *item,
    const source_package_collection *expected,
    int count,
    pin_value *result,
    const pin_value **roots
)
{
    arena_free(&root_output);
    REQUIRE(root_set(backend, item, &root_output, result));
    REQUIRE(unpack(backend, "RootResult", result, NULL, (script_range){ 0 }, roots));
    REQUIRE(compare_roots(*roots, expected, count));
    return true;
}
TEST_CASE(package_routes_files)
{
    REQUIRE(ready());
    depth_path(depth16, 16);
    depth_path(depth17, 17);
    REQUIRE(sizeof file_cases / sizeof file_cases[0] == 11);
    for (int i = 0; i < 11; i++) {
        cleanup();
        const file_case *item = &file_cases[i];
        const root_case *config = &roots_cases[item->roots];
        source_package_collection roots[3];
        int count;
        REQUIRE(!source_package_routes_roots(
            &oracle,
            config->project,
            config->collections,
            config->count,
            roots,
            &count
        ));
        source_package_routed_file file;
        const char *error = source_package_routes_file(&oracle, roots, count, item->path, &file);
        for (int b = 0; b < 2; b++) {
            pin_value root_result, result;
            const pin_value *root_values, *value;
            REQUIRE(authored_roots(b, config, roots, count, &root_result, &root_values));
            arena_free(&output);
            pin_value arguments[] = { *root_values,
                                      text(item->path),
                                      boolean(item->path != NULL),
                                      integer(i + 4) };
            REQUIRE(call(b, "FileRoute", arguments, 4, &output, &result));
            arena_free(&root_output);
            REQUIRE(owned(&output, &result, 0));
            REQUIRE(
                unpack(b, "FileResult", &result, error, (script_range){ 0, 0, 1, 1, i + 4 }, &value)
            );
            if (!error) {
                REQUIRE(compare_file(value, &file));
            }
            if (i == 0) {
                REQUIRE(save(b, 1, &result));
            }
        }
        if (i == 0) {
            saved_file = (source_package_routed_file){
                copy(&retained_oracle, file.path),
                copy(&retained_oracle, file.directory),
                copy(&retained_oracle, file.key),
                copy(&retained_oracle, file.identity),
                file.root
            };
        }
    }
    return true;
}
TEST_CASE(package_routes_imports)
{
    REQUIRE(ready());
    REQUIRE(sizeof import_cases / sizeof import_cases[0] == 16);
    for (int i = 0; i < 16; i++) {
        cleanup();
        const import_case *item = &import_cases[i];
        const root_case *config = &roots_cases[0];
        source_package_collection roots[3];
        int count;
        REQUIRE(strlen(item->literal) < 128 && strlen(item->directory) < 128);
        REQUIRE(!source_package_routes_roots(
            &oracle,
            config->project,
            config->collections,
            config->count,
            roots,
            &count
        ));
        char *directory;
        const char *error = source_package_routes_import(
            &oracle,
            roots,
            count,
            item->directory,
            item->root,
            item->literal,
            strlen(item->literal),
            &directory
        );
        script_range range = { 7, 7 + (int)strlen(item->literal), 3, 5, 2 };
        for (int b = 0; b < 2; b++) {
            pin_value root_result, result, request;
            const pin_value *root_values, *value;
            REQUIRE(authored_roots(b, config, roots, count, &root_result, &root_values));
            REQUIRE(row(b, "ImportInput", &request));
            REQUIRE(
                put(&request, "directory", text(item->directory)) &&
                put(&request, "literal", text(item->literal)) &&
                put(&request, "root", integer(item->root))
            );
            REQUIRE(
                put(&request, "start", integer(range.start)) &&
                put(&request, "end", integer(range.end)) &&
                put(&request, "line", integer(range.line)) &&
                put(&request, "column", integer(range.column)) &&
                put(&request, "document", integer(range.document))
            );
            pin_value arguments[] = { *root_values, request };
            arena_free(&output);
            REQUIRE(call(b, "ImportRoute", arguments, 2, &output, &result));
            arena_free(&root_output);
            REQUIRE(owned(&output, &result, 0));
            REQUIRE(unpack(b, "ImportResult", &result, error, range, &value));
            if (!error) {
                REQUIRE(
                    value->type == PIN_STR && pin_text(*value).bytes &&
                    !strcmp(pin_text(*value).bytes, directory)
                );
            }
            if (i == 7) {
                REQUIRE(save(b, 2, &result));
            }
        }
        if (i == 7) {
            REQUIRE(!error);
            saved_import = copy(&retained_oracle, directory);
        }
    }
    return true;
}
TEST_CASE(package_routes_detached_results)
{
    compiler_fixture_free(&fixture);
    cleanup();
    for (int b = 0; b < 2; b++) {
        for (int kind = 0; kind < 3; kind++) {
            REQUIRE(saved[b][kind].type == PIN_UNION && saved[b][kind].ptr);
            REQUIRE(owned(&retained[b][kind], &saved[b][kind], 0));
            const pin_union *data = saved[b][kind].ptr;
            REQUIRE(data->tag == saved_tag[b][kind] && data->count == 1);
            const pin_value *value = &data->values[0];
            if (kind == 0) {
                REQUIRE(compare_roots(value, saved_roots, saved_root_count));
            }
            if (kind == 1) {
                REQUIRE(compare_file(value, &saved_file));
            }
            if (kind == 2) {
                REQUIRE(
                    value->type == PIN_STR && pin_text(*value).bytes &&
                    !strcmp(pin_text(*value).bytes, saved_import)
                );
            }
        }
    }
    return true;
}
void test_language_package_routes_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_routes_roots);
    RUN_TEST_CASE(package_routes_files);
    RUN_TEST_CASE(package_routes_imports);
    RUN_TEST_CASE(package_routes_detached_results);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        for (int kind = 0; kind < 3; kind++) {
            arena_free(&retained[b][kind]);
            saved[b][kind] = (pin_value){ 0 };
        }
    }
    arena_free(&retained_oracle);
}
