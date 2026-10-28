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
 * text bytes, not allocation/RSS. Packaging uses at most 1024 source bytes;
 * generator JSON additionally checks the bounded explicit 20/32 source lists.
 * No subprocesses, resource generation or filesystem discovery. */
static compiler_fixture fixture;
static native_definition_binding *text_bindings;
static arena input, detached;
static uint64_t formatted_bytes, formatted_calls;
static char expected[8192];
static int expected_length;
static const char PACKAGE[] = "project:asset_packaging";
static char *generator_prefix;
/* CompilerSources.txt lists the bootstrap compiler; the facade adds its own. */
enum
{
    COMPILER_SOURCES = 25,
    SOURCES = COMPILER_SOURCES + 12
};
static char *source_texts[SOURCES];
static size_t source_lengths[SOURCES];
static int source_count;
static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
    compiler_fixture_free(&fixture);
    free(generator_prefix);
    generator_prefix = NULL;
    for (int i = 0; i < source_count; i++) {
        free(source_texts[i]);
    }
    source_count = 0;
    free(text_bindings);
    text_bindings = NULL;
}
static const char *count_format(arena *storage, const pin_value *args, int count, pin_value *out)
{
    const char *error = NATIVE_FORMAT_TEXT_CALL.value(storage, args, count, out);
    if (!error) {
        formatted_bytes += pin_text(*out).length;
        formatted_calls++;
    }
    return error;
}
static node_host_call count_format_call;
static bool prepare_sources(const source_package_input *files, int count)
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
    compiler_fixture_init_heap(&fixture);
    source_package_collection collection = { "core", "/sdk/core" };
    source_package_snapshot_options options = { .project_root = "/compiler",
                                                .files = files,
                                                .file_count = count,
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
static bool prepare(bool boundaries)
{
    static const char BOUNDARY[] = "package asset_packaging\n"
                                   "pub JoinProbe :: (parts: string[]) -> "
                                   "u8[] { return Bytes(Join(parts)); }\n";
    source_package_input files[] = {
        { "asset_packaging/Packaging.chibi",
          ASSET_PACKAGING_SOURCE,
          sizeof ASSET_PACKAGING_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
        { "asset_packaging/Boundary.chibi", BOUNDARY, sizeof BOUNDARY - 1 }
    };
    return prepare_sources(files, boundaries ? 3 : 2);
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
        formatted_calls = 0;
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
        formatted_calls = 0;
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
/* Only the exact pure prefix is compiled here: later generator declarations
 * require the private build host. The embedded complete source and unique
 * boundary keep this test on production Join/Quote/Array, not a translation. */
static bool prepare_generator()
{
    const char *boundary = strstr(ASSET_GENERATOR_SOURCE, "\nSources :: (");
    REQUIRE(boundary && !strstr(boundary + 1, "\nSources :: ("));
    size_t length = (size_t)(boundary - ASSET_GENERATOR_SOURCE);
    char *prefix = malloc(length + 1);
    REQUIRE(prefix);
    memcpy(prefix, ASSET_GENERATOR_SOURCE, length);
    prefix[length] = 0;
    static const char PROBES[] = "package asset_compiler_stage\n"
                                 "pub JoinProbe :: (parts: string[]) -> string "
                                 "{ return Join(parts); }\n"
                                 "pub QuoteProbe :: (text: string) -> string { "
                                 "return Quote(text); }\n"
                                 "pub ArrayProbe :: (values: string[]) -> "
                                 "string { return Array(values); }\n";
    source_package_input files[] = {
        { "asset_generator/Generate.chibi", prefix, length },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
        { "asset_generator/Probe.chibi", PROBES, sizeof PROBES - 1 }
    };
    bool ready = prepare_sources(files, 3);
    generator_prefix = prefix;
    return ready;
}
static bool same_text(pin_value value, const char *bytes, size_t length)
{
    REQUIRE(value.type == PIN_STR && value.container == PIN_CONTAINER_SINGLE);
    pin_string text = pin_text(value);
    REQUIRE(text.length == length);
    REQUIRE(!length || !memcmp(text.bytes, bytes, length));
    return true;
}
static pin_value text(const char *bytes, size_t length)
{
    pin_value value;
    const char *error = pin_value_text(&input, &value, bytes, length);
    if (error) {
        fprintf(stderr, "%s\n", error);
        abort();
    }
    return value;
}
/* Independent byte-wise JSON reference. In particular the generator uses
 * lowercase \u00xx rather than JSON writers' optional \b and \f spellings. */
static char *quoted(char *out, const char *bytes, size_t length)
{
    static const char hex[] = "0123456789abcdef";
    *out++ = '"';
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == '"' || c == '\\') {
            *out++ = '\\';
            *out++ = (char)c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            *out++ = '\\';
            *out++ = c == '\n' ? 'n' : c == '\r' ? 'r' : 't';
        } else if (c < 32) {
            memcpy(out, "\\u00", 4);
            out += 4;
            *out++ = hex[c >> 4];
            *out++ = hex[c & 15];
        } else {
            *out++ = (char)c;
        }
    }
    *out++ = '"';
    return out;
}
static int levels(int fragments)
{
    int count = 0;
    for (; fragments > 1; fragments = (fragments + 1) / 2) {
        count++;
    }
    return count;
}
static bool generator_quote(int backend, const char *bytes, size_t length, size_t *written)
{
    REQUIRE(length <= 1024);
    char expected_quote[6 * 1024 + 2];
    *written = (size_t)(quoted(expected_quote, bytes, length) - expected_quote);
    pin_value argument = text(bytes, length), out;
    formatted_bytes = 0;
    formatted_calls = 0;
    bool called = compiler_fixture_call(
        &fixture,
        backend,
        "project:asset_generator",
        "QuoteProbe",
        &argument,
        1,
        1000000,
        &detached,
        &out
    );
    bool unchanged = same_text(argument, bytes, length);
    arena_free(&input);
    bool exact = called && same_text(out, expected_quote, *written);
    arena_free(&detached);
    REQUIRE(unchanged && exact);
    return true;
}
static char *read_source(const char *relative, size_t *length)
{
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/%s", CHIBI_SOURCE_ROOT, relative);
    if (n < 0 || (size_t)n >= sizeof path) {
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    bool measured = !fseek(file, 0, SEEK_END);
    long size = measured ? ftell(file) : -1;
    bool bounded = size >= 0 && size <= 1048576 && !fseek(file, 0, SEEK_SET);
    char *bytes = bounded ? malloc((size_t)size + 1) : NULL;
    bool read = bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size;
    bool closed = !fclose(file);
    if (!read || !closed) {
        free(bytes);
        return NULL;
    }
    bytes[size] = 0;
    *length = (size_t)size;
    return bytes;
}
static bool add_source(const char *relative)
{
    REQUIRE(source_count < SOURCES);
    source_texts[source_count] = read_source(relative, &source_lengths[source_count]);
    REQUIRE(source_texts[source_count]);
    source_count++;
    return true;
}
static bool compiler_sources()
{
    size_t length = 0;
    char *manifest = read_source("tools/compiler/asset_compiler/CompilerSources.txt", &length);
    REQUIRE(manifest);
    bool ok = true;
    for (char *line = manifest; ok && *line;) {
        char *end = strchr(line, '\n');
        if (end) {
            *end = 0;
        }
        char relative[512];
        int n = !strncmp(line, "/sdk/", 5)
                  ? snprintf(relative, sizeof relative, "packages/%s", line + 5)
                  : snprintf(relative, sizeof relative, "tools/compiler/%s", line);
        ok = n > 0 && (size_t)n < sizeof relative && *line && add_source(relative);
        line = end ? end + 1 : line + strlen(line);
    }
    free(manifest);
    REQUIRE(ok && source_count == COMPILER_SOURCES);
    return true;
}
static bool generator_array(int backend)
{
    size_t length = 0;
    int quote_levels = 0;
    for (int i = 0; i < source_count; i++) {
        length += source_lengths[i];
        int escapes = 0;
        for (size_t b = 0; b < source_lengths[i]; b++) {
            unsigned char c = (unsigned char)source_texts[i][b];
            escapes += c < 32 || c == '"' || c == '\\';
        }
        int depth = levels(2 * escapes + 3);
        if (depth > quote_levels) {
            quote_levels = depth;
        }
    }
    REQUIRE(length <= 2097152);
    char *expected_array = malloc(6 * length + 3 * (size_t)source_count + 2);
    REQUIRE(expected_array);
    char *at = expected_array;
    *at++ = '[';
    for (int i = 0; i < source_count; i++) {
        if (i) {
            *at++ = ',';
        }
        at = quoted(at, source_texts[i], source_lengths[i]);
    }
    *at++ = ']';
    size_t output_length = (size_t)(at - expected_array);
    pin_value argument = texts((const char *const *)source_texts, source_lengths, source_count),
              out;
    formatted_bytes = 0;
    formatted_calls = 0;
    /* New bounded real-source fixture; existing small packaging allowances are
     * unchanged. This call covers at most SOURCES explicitly named files. */
    bool called = compiler_fixture_call(
        &fixture,
        backend,
        "project:asset_generator",
        "ArrayProbe",
        &argument,
        1,
        200000000,
        &detached,
        &out
    );
    const pin_array *values = argument.ptr;
    bool unchanged = values && values->count == source_count;
    for (int i = 0; unchanged && i < source_count; i++) {
        unchanged = same_text(pin_array_get(values, i), source_texts[i], source_lengths[i]);
    }
    arena_free(&input);
    bool exact = called && same_text(out, expected_array, output_length);
    free(expected_array);
    arena_free(&detached);
    /* Quote costs at most depth*B plus 2*B for constructing \u00xx escapes.
     * Array's unchanged comma-Join costs at most (count-1)*B and its framing
     * another 2*B: total <= (depth+count+3)*B for this nonempty array. */
    uint64_t bound = (uint64_t)output_length *
                     (uint64_t)(quote_levels + source_count + 3);
    printf(
        "generator backend=%d sources=%d bytes=%zu output=%zu levels=%d "
        "calls=%" PRIu64 " formatted=%" PRIu64 " bound=%" PRIu64 "\n",
        backend,
        source_count,
        length,
        output_length,
        quote_levels,
        formatted_calls,
        formatted_bytes,
        bound
    );
    REQUIRE(unchanged && exact);
    REQUIRE(formatted_bytes <= bound);
    return true;
}
static bool generator(int backend)
{
    REQUIRE(prepare_generator());
    char small[1024];
    memset(small, '\n', sizeof small);
    size_t output_length = 0;
    REQUIRE(generator_quote(backend, small, sizeof small, &output_length));
    uint64_t bound = (uint64_t)output_length *
                     (uint64_t)levels(2 * (int)sizeof small + 3);
    printf(
        "generator bound backend=%d output=%zu formatted=%" PRIu64
        " bound=%" PRIu64 "\n",
        backend,
        output_length,
        formatted_bytes,
        bound
    );
    /* Safe old-production red: stop here, before any real-source array call. */
    REQUIRE(formatted_bytes <= bound);
    unsigned char controls[40];
    for (int i = 0; i < 32; i++) {
        controls[i] = (unsigned char)i;
    }
    const unsigned char tail[] = { '"', '\\', '{', '}', 0xc3, 0xa9 };
    memcpy(controls + 32, tail, sizeof tail);
    REQUIRE(generator_quote(backend, (const char *)controls, 32 + sizeof tail, &output_length));
    bound = (uint64_t)output_length * (uint64_t)(levels(2 * 34 + 3) + 2);
    REQUIRE(formatted_bytes <= bound);
    const char golden[] = { 0,   8,    12,         31,         '\t', '\n', '\r',
                            '"', '\\', (char)0xc3, (char)0xa9, '{',  '}' };
    static const char escaped[] = "\"\\u0000\\u0008\\u000c\\u001f\\t\\n\\r\\\""
                                  "\\\\\xc3\xa9{}\"";
    char checked[6 * sizeof golden + 2];
    char *end = quoted(checked, golden, sizeof golden);
    REQUIRE((size_t)(end - checked) == sizeof escaped - 1);
    REQUIRE(!memcmp(checked, escaped, sizeof escaped - 1));
    REQUIRE(generator_quote(backend, golden, sizeof golden, &output_length));
    REQUIRE(generator_quote(backend, "", 0, &output_length));
    REQUIRE(compiler_sources());
    REQUIRE(generator_array(backend));
    static const char *const additions[] = {
        "tools/compiler/asset_compiler/Cache.chibi",
        "tools/compiler/asset_compiler/Compile.chibi",
        "tools/compiler/asset_source/Computed.chibi",
        "tools/compiler/asset_source/Metadata.chibi",
        "tools/compiler/asset_source/Parameters.chibi",
        "tools/compiler/asset_source/Reconcile.chibi",
        "tools/compiler/asset_source/Validate.chibi",
        "tools/compiler/asset_source/Write.chibi",
        "tools/compiler/asset_packaging/Packaging.chibi",
        "packages/game/assets/Assets.chibi",
        "packages/game/assets/Schemas.chibi",
        "packages/game/documentation/Documentation.chibi"
    };
    for (int i = 0; i < (int)(sizeof additions / sizeof *additions); i++) {
        REQUIRE(add_source(additions[i]));
    }
    REQUIRE(source_count == SOURCES);
    /* Request moves its entry to index zero, preserving all other sources.
     * The facade entry follows the bootstrap files and Cache.chibi. */
    const int at = COMPILER_SOURCES + 1;
    char *entry = source_texts[at];
    size_t entry_length = source_lengths[at];
    memmove(source_texts + 1, source_texts, at * sizeof *source_texts);
    memmove(source_lengths + 1, source_lengths, at * sizeof *source_lengths);
    source_texts[0] = entry;
    source_lengths[0] = entry_length;
    REQUIRE(generator_array(backend));
    const char *parts[] = { "A\0B", "", "\xc3\xa9" };
    const size_t lengths[] = { 3, 0, 2 };
    const int counts[] = { 0, 1, 3 };
    const size_t joined[] = { 0, 3, 5 };
    for (int i = 0; i < 3; i++) {
        pin_value argument = texts(parts, lengths, counts[i]), out;
        formatted_bytes = 0;
        formatted_calls = 0;
        REQUIRE(compiler_fixture_call(
            &fixture,
            backend,
            "project:asset_generator",
            "JoinProbe",
            &argument,
            1,
            1000000,
            &detached,
            &out
        ));
        const pin_array *values = argument.ptr;
        for (int j = 0; j < counts[i]; j++) {
            REQUIRE(same_text(pin_array_get(values, j), parts[j], lengths[j]));
        }
        arena_free(&input);
        if (i == 2) {
            compiler_fixture_free(&fixture);
        }
        REQUIRE(same_text(out, "A\0B\xc3\xa9", joined[i]));
        arena_free(&detached);
    }
    return true;
}
TEST_CASE(asset_generator_vm_preserves_json_with_bounded_work)
{
    return generator(0);
}
TEST_CASE(asset_generator_compiled_preserves_json_with_bounded_work)
{
    return generator(1);
}
void test_language_asset_packaging_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(asset_packaging_vm_preserves_bytes_with_bounded_work);
    RUN_TEST_CASE(asset_packaging_compiled_preserves_bytes_with_bounded_work);
    RUN_TEST_CASE(asset_generator_vm_preserves_json_with_bounded_work);
    RUN_TEST_CASE(asset_generator_compiled_preserves_json_with_bounded_work);
    pu_clear_setup();
}
