#include "asset_packaging_chibi_source.h"
#include "backend.h"
#include "compiler_fixture.h"
#include "package_core_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/text_format.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test-owned text table delegates the unchanged formatter and counts emitted
 * text bytes, not allocation/RSS. At most 1024 source bytes and 16 calls per
 * backend; no resources, subprocesses or filesystem discovery. */
static compiler_fixture fixture;
static native_definition_binding *text_bindings;
static arena input, detached;
static uint64_t formatted_bytes;
static char expected[8192];
static int expected_length;
static const char PACKAGE[] = "project:asset_packaging";
static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
    compiler_fixture_free(&fixture);
    free(text_bindings);
    text_bindings = NULL;
}
static const char *count_format(arena *storage, const pin_value *args, int count, pin_value *out)
{
    const char *error = NATIVE_FORMAT_TEXT_CALL.value(storage, args, count, out);
    if (!error) {
        formatted_bytes += pin_text(*out).length;
    }
    return error;
}
static node_host_call count_format_call;
static bool prepare(bool boundaries)
{
    cleanup();
    native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                       NATIVE_ARRAY_DEFINITION_PACK,
                                       CORE_DEFINITION_PACK };
    size_t size = (size_t)packs[0].binding_count * sizeof *text_bindings;
    text_bindings = malloc(size);
    REQUIRE(text_bindings);
    memcpy(text_bindings, packs[0].bindings, size);
    bool replaced = false;
    for (int i = 0; i < packs[0].binding_count; i++) {
        if (!strcmp(text_bindings[i].canonical_name, NATIVE_FORMAT_TEXT_NAME)) {
            text_bindings[i].implementation.call = &count_format_call;
            replaced = true;
        }
    }
    REQUIRE(replaced);
    count_format_call = NATIVE_FORMAT_TEXT_CALL;
    count_format_call.value = count_format;
    packs[0].bindings = text_bindings;
    char error[256];
    fixture.native = native_capabilities_from_definition_packs(NULL, 0, packs, 3, error, sizeof error);
    REQUIRE(fixture.native);
    static const char BOUNDARY[] = "package asset_packaging\n"
                                   "pub JoinProbe :: (parts: string[]) -> "
                                   "byte[] { return Bytes(Join(parts)); }\n";
    source_package_input files[] = {
        { "asset_packaging/Packaging.chibi",
          ASSET_PACKAGING_SOURCE,
          sizeof ASSET_PACKAGING_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
        { "asset_packaging/Boundary.chibi", BOUNDARY, sizeof BOUNDARY - 1 }
    };
    source_package_collection collection = { "core", "/sdk/core" };
    source_package_snapshot_options options = { .project_root = "/compiler",
                                                .files = files,
                                                .file_count = boundaries ? 3 : 2,
                                                .collections = &collection,
                                                .collection_count = 1 };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        source_package_snapshot *snapshot = source_package_snapshot_new(&options);
        REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
        script_lower_bindings bindings = authoring_native_bindings(fixture.native);
        fixture.owners[b] = authoring_packages_prepare(
            snapshot,
            native_capabilities_catalog(fixture.native),
            &bindings,
            backends[b],
            NULL
        );
        REQUIRE(fixture.owners[b] && authoring_packages_valid(fixture.owners[b]));
    }
    return true;
}
static pin_value texts(const char *const *values, const size_t *lengths, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        const char *error = pin_value_text(&input, &array->items[i], values[i], lengths[i]);
        if (error) {
            fprintf(stderr, "%s\n", error);
            abort();
        }
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
static bool append_expected(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int size = vsnprintf(expected + expected_length, sizeof expected - (size_t)expected_length, format, args);
    va_end(args);
    REQUIRE(size >= 0 && (size_t)size < sizeof expected - (size_t)expected_length);
    expected_length += size;
    return true;
}
static bool expected_resource(const unsigned char *source, int length)
{
    expected_length = 0;
    REQUIRE(append_expected(
        "#include \"authoring/asset_compiler_host.h\"\n#include "
        "\"asset_compiler_blob.inc\"\n"
        "static const unsigned char asset_library_0[] = {"
    ));
    for (int i = 0; i < length; i++) {
        REQUIRE(append_expected("%u,", source[i]));
    }
    REQUIRE(append_expected(
        "0};\nstatic const asset_source_library asset_libraries[] = {\n"
        "{\"/sdk/game/Test.chibi\", (const char *)asset_library_0},\n"
        "};\nstatic const asset_source_collection asset_collections[] = {\n"
        "{\"core\", \"/sdk/core\"}, {\"game\", \"/sdk/game\"}};\n"
        "const asset_compiler_options *asset_compiler_packaged_options() {\n"
        "static const asset_compiler_options options = {\n"
        ".compiler = asset_compiler_blob, .compiler_length = sizeof "
        "asset_compiler_blob,\n"
        ".libraries = asset_libraries, .library_count = sizeof asset_libraries "
        "/ sizeof asset_libraries[0],\n"
        ".collections = asset_collections, .collection_count = 2\n};\nreturn "
        "&options;\n}\n"
    ));
    return true;
}
static bool same_bytes(pin_value value, const char *bytes, int length)
{
    REQUIRE(value.type == PIN_U8 && value.container == PIN_CONTAINER_ARRAY);
    const pin_array *array = value.ptr;
    REQUIRE(array && array->count == length);
    char actual[8192];
    REQUIRE(length <= (int)sizeof actual);
    for (int i = 0; i < length; i++) {
        pin_value item = pin_array_get(array, i);
        REQUIRE(item.type == PIN_U8);
        actual[i] = (char)item.u8;
    }
    REQUIRE(!memcmp(actual, bytes, (size_t)length));
    return true;
}
static bool check(int backend)
{
    REQUIRE(prepare(false));
    unsigned char source[1024];
    const unsigned char pattern[] = { 0, 34, 92, 0xc3, 0xa9, 10, 'x' };
    for (int i = 0; i < (int)sizeof source; i++) {
        source[i] = pattern[i % (int)sizeof pattern];
    }
    const int sizes[] = { 0, 1, 3, 512, 1024 };
    for (int n = 0; n < 5; n++) {
        int length = sizes[n];
        const char *path = "/sdk/game/Test.chibi", *text = (const char *)source;
        size_t path_length = strlen(path), text_length = (size_t)length;
        pin_value arguments[] = { texts(&path, &path_length, 1),
                                  texts(&text, &text_length, 1) },
                  out;
        REQUIRE(expected_resource(source, length));
        formatted_bytes = 0;
        REQUIRE(
            compiler_fixture_call(&fixture, backend, PACKAGE, "Packaged", arguments, 2, 1000000, &detached, &out)
        );
        REQUIRE(same_bytes(out, expected, expected_length));
        int levels = 0;
        for (int fragments = length + 7; fragments > 1;
             fragments = (fragments + 1) / 2) {
            levels++;
        }
        uint64_t bound = (uint64_t)expected_length * (uint64_t)(levels + 1);
        printf(
            "packaging backend=%d bytes=%d output=%d formatted=%" PRIu64
            " bound=%" PRIu64 "\n",
            backend,
            length,
            expected_length,
            formatted_bytes,
            bound
        );
        REQUIRE(formatted_bytes <= bound);
        arena_free(&input);
        arena_free(&detached);
    }
    /* Old production fails the measured bound before preparing this wrapper
     * for the new private helper; it never reports a missing-helper red. */
    REQUIRE(prepare(true));
    const char *parts[] = { "A\0B", "", "\xc3\xa9" };
    const size_t lengths[] = { 3, 0, 2 };
    const int counts[] = { 0, 1, 3 }, joined[] = { 0, 3, 5 };
    for (int i = 0; i < 3; i++) {
        pin_value argument = texts(parts, lengths, counts[i]), out;
        formatted_bytes = 0;
        REQUIRE(
            compiler_fixture_call(&fixture, backend, PACKAGE, "JoinProbe", &argument, 1, 1000000, &detached, &out)
        );
        REQUIRE(same_bytes(out, "A\0B\xc3\xa9", joined[i]));
        arena_free(&input);
        arena_free(&detached);
    }
    return true;
}
TEST_CASE(asset_packaging_vm_preserves_bytes_with_bounded_work)
{
    return check(0);
}
TEST_CASE(asset_packaging_compiled_preserves_bytes_with_bounded_work)
{
    return check(1);
}
void test_language_asset_packaging_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(asset_packaging_vm_preserves_bytes_with_bounded_work);
    RUN_TEST_CASE(asset_packaging_compiled_preserves_bytes_with_bounded_work);
    pu_clear_setup();
}
