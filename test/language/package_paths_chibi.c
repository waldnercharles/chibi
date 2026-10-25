#include "package_paths_chibi.h"

#include "authoring/source_package_paths.h"
#include "compiler_fixture.h"
#include "package_core_source.h"
#include "package_paths_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Three cases, one 26-request corpus, 26 C oracle calls and 52 authored calls.
 * Each authored call has a one-million-instruction ceiling. Two retained owners
 * prepare Paths plus core strings once per backend (four source parses total).
 * The four long inputs are built once with fixed lengths; no runtime IO,
 * services, generated combinations or timing loops occur. */
typedef struct
{
    const char *id, *base, *path, *floor;
    bool valid;
} path_case;
static char segment[301], longest[4096], oversized[4097], collapsing[4095];
static const path_case cases[] = {
    { "project-root", NULL, "/workspace/./project", NULL, true },
    { "collection-root", NULL, "/sdk/./core", NULL, true },
    { "project-relative-file", "/workspace/project", "app/a.chibi", "/workspace/project", true },
    { "project-absolute-file", "/workspace/project", "/workspace/project/app/./a.chibi", NULL, true },
    { "collection-absolute-file", "/workspace/project", "/sdk/core/scalar/a.chibi", NULL, true },
    { "relative-import", "/workspace/project/app", "../math", "/workspace/project", true },
    { "project-import", "/workspace/project", "math/.", "/workspace/project", true },
    { "collection-import", "/sdk/core", "scalar", "/sdk/core", true },
    { "project-root-escape", "/workspace/project/app", "../../outside", "/workspace/project", false },
    { "nested-root-escape", "/workspace/project/app", "../math", "/workspace/project/app", false },
    { "missing-import-path", "/workspace/project/app", "../missing", "/workspace/project", true },
    { "relocated-root", NULL, "/moved/project", NULL, true },
    { "root-floor-dotdot", "/", "x/../y", "/", true },
    { "final-prefix-boundary", NULL, "/workspace/project2/a", "/workspace/project", false },
    { "intermediate-escape-reentry", "/workspace/project", "../project/app", "/workspace/project", false },
    { "slashes-and-dots", "/workspace/project", "./a//b/../c", "/workspace/project", true },
    { "backslash", "/workspace/project", "a\\b", NULL, false },
    { "null-path", "/workspace/project", NULL, NULL, false },
    { "relative-without-base", NULL, "app", NULL, false },
    { "empty-relative", "/workspace/project", "", NULL, true },
    { "absolute-root", NULL, "/", NULL, true },
    { "segment-over-255", "/workspace/project", segment, "/workspace/project", true },
    { "absolute-4095", NULL, longest, NULL, true },
    { "absolute-4096", NULL, oversized, NULL, false },
    { "joined-limit-before-collapse", "/", collapsing, NULL, false },
    { "absolute-ignores-unused-base", "bad\\base", "/ok", NULL, true },
};
static const char IDENTITY[] = "project:package_paths";
static compiler_fixture fixture;
static arena input, oracle, detached, retained[2];
static pin_value previous[2];
static uint64_t retained_tag[2];
static char retained_expected[2][SOURCE_PACKAGE_PATH_MAX];

static void cleanup()
{
    arena_free(&input);
    arena_free(&oracle);
    arena_free(&detached);
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static pin_value string(const char *text)
{
    text = text ? text : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, text, strlen(text));
    if (error) {
        abort();
    }
    return result;
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static bool request(int backend, const path_case *item, pin_value *out)
{
    const pin_record *record = compiler_fixture_record(&fixture, backend, IDENTITY, "Request", &input);
    REQUIRE(record && record->type.field_count == 6);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .ptr = arena_alloc(&input, record->type.size),
                        .type_name = record->type.name,
                        .record = record };
    memset(out->ptr, 0, record->type.size);
    REQUIRE(
        put(out, "base", string(item->base)) &&
        put(out, "path", string(item->path)) && put(out, "floor", string(item->floor))
    );
    REQUIRE(
        put(out, "has_base", boolean(item->base != NULL)) &&
        put(out, "has_path", boolean(item->path != NULL)) &&
        put(out, "has_floor", boolean(item->floor != NULL))
    );
    return true;
}
static const script_catalog_variant *variant(int backend, const pin_value *value, const char *name)
{
    const authoring_package *package = compiler_fixture_package(&fixture, backend, IDENTITY);
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, backend, IDENTITY, "Path")
    );
    const pin_union *data = value->ptr;
    for (int i = 0; type && data && i < type->variant_count; i++) {
        if (data->tag == type->variants[i].tag &&
            !strcmp(type->variants[i].name, name)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool compare(int backend, const path_case *item, const char *expected, bool retain)
{
    arena_free(&input);
    arena_free(&detached);
    pin_value argument, result;
    REQUIRE(request(backend, item, &argument));
    REQUIRE(
        compiler_fixture_call(&fixture, backend, IDENTITY, "Normalize", &argument, 1, 1000000, &detached, &result)
    );
    arena_free(&input);
    REQUIRE(
        result.type == PIN_UNION && result.container == PIN_CONTAINER_SINGLE &&
        result.ptr
    );
    const script_catalog_variant *kind = variant(backend, &result, expected ? "Normalized" : "Invalid");
    const pin_union *data = result.ptr;
    REQUIRE(kind && data->count == kind->field_count);
    if (!expected) {
        REQUIRE(data->count == 0);
        return true;
    }
    REQUIRE(data->count == 1 && !strcmp(kind->fields[0].name, "path"));
    const pin_value *path = &data->values[0];
    if (path->type != PIN_STR || !pin_text(*path).bytes ||
        strcmp(pin_text(*path).bytes, expected)) {
        fprintf(stderr, "path mismatch: %s backend %d\n", item->id, backend);
    }
    REQUIRE(
        path->type == PIN_STR && path->container == PIN_CONTAINER_SINGLE &&
        pin_text(*path).bytes && !strcmp(pin_text(*path).bytes, expected)
    );
    if (retain) {
        REQUIRE(!compiler_fixture_snapshot(&retained[backend], &previous[backend], &result));
        retained_tag[backend] = kind->tag;
        REQUIRE(strlen(expected) < sizeof retained_expected[backend]);
        strcpy(retained_expected[backend], expected);
    }
    return true;
}
static bool corpus(int first, int end)
{
    REQUIRE(
        fixture.owners[0] && fixture.owners[1] &&
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1])
    );
    for (int i = first; i < end; i++) {
        const path_case *item = &cases[i];
        arena_free(&oracle);
        const char *expected = source_package_path_normalize(&oracle, item->base, item->path, item->floor);
        if ((expected != NULL) != item->valid) {
            fprintf(stderr, "C path control mismatch: %s\n", item->id);
        }
        REQUIRE((expected != NULL) == item->valid);
        /* compare retires only call inputs/results, keeping this oracle alive. */
        for (int b = 0; b < 2; b++) {
            REQUIRE(compare(b, item, expected, i == 22));
        }
    }
    return true;
}
TEST_CASE(package_paths_existing_resolver_inputs)
{
    memset(segment, 'a', sizeof segment - 1);
    segment[sizeof segment - 1] = 0;
    memset(longest, 'a', sizeof longest - 1);
    longest[0] = '/';
    longest[sizeof longest - 1] = 0;
    memset(oversized, 'a', sizeof oversized - 1);
    oversized[0] = '/';
    oversized[sizeof oversized - 1] = 0;
    memset(collapsing, '/', sizeof collapsing - 1);
    collapsing[0] = '.';
    collapsing[sizeof collapsing - 1] = 0;
    REQUIRE(strlen(segment) == 300 && strlen(longest) == 4095 && strlen(oversized) == 4096);
    REQUIRE(strlen(collapsing) == 4094);
    REQUIRE(sizeof cases / sizeof cases[0] == 26);
    source_package_input files[] = {
        { "package_paths/Paths.chibi",
          PACKAGE_PATHS_CHIBI_SOURCE,
          sizeof PACKAGE_PATHS_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
    };
    REQUIRE(files[0].size < 16384);
    REQUIRE(compiler_fixture_prepare(&fixture, files, 2));
    return corpus(0, 12);
}
TEST_CASE(package_paths_corner_inputs)
{
    return corpus(12, 26);
}
TEST_CASE(package_paths_detached_results)
{
    compiler_fixture_free(&fixture);
    cleanup();
    for (int b = 0; b < 2; b++) {
        REQUIRE(previous[b].type == PIN_UNION && previous[b].ptr);
        const pin_union *data = previous[b].ptr;
        REQUIRE(data->tag == retained_tag[b] && data->count == 1);
        REQUIRE(
            data->values[0].type == PIN_STR && pin_text(data->values[0]).bytes &&
            !strcmp(pin_text(data->values[0]).bytes, retained_expected[b])
        );
    }
    return true;
}
void test_language_package_paths_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_paths_existing_resolver_inputs);
    RUN_TEST_CASE(package_paths_corner_inputs);
    RUN_TEST_CASE(package_paths_detached_results);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        arena_free(&retained[b]);
        previous[b] = (pin_value){ 0 };
    }
}
