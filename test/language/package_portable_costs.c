#include "backend.h"
#include "compiler_fixture.h"
#include "package_core_source.h"
#include "package_portable_source.h"
#include "runtime/core_definition.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/copy.h"
#include "types/text.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Explicit opt-in measurements, never part of the correctness aggregate.
 * Each workload runs once per size/backend. Compilation and input construction
 * precede the measured call; the call includes detached result construction.
 * The copy scope counts selected same-thread managed-value C operations, not
 * allocations, every runtime copy or copies performed by worker threads. */
static const char CONSUMER[] =
    "package portable_costs\n"
    "import bytes \"core:bytes\"\n"
    "import utf8 \"core:utf8\"\n"
    "import base64 \"core:encoding/base64\"\n"
    "pub Validate :: (data: u8[], text: string) -> int {\n"
    "    byte_error := utf8.Validate(data);\n"
    "    if (byte_error) { assert(false, \"valid buffer rejected\"); }\n"
    "    text_error := utf8.ValidateString(text);\n"
    "    if (text_error) { assert(false, \"valid string rejected\"); }\n"
    "    counted := utf8.Count(text);\n"
    "    if (counted.error) { assert(false, \"valid count rejected\"); }\n"
    "    return counted.count;\n"
    "}\n"
    "pub Search :: (data: u8[], late: u8[], missing: u8[]) -> int {\n"
    "    found := bytes.Index(data, late);\n"
    "    assert(bytes.Index(data, missing) == -1, \"absent needle found\");\n"
    "    assert(bytes.LastIndex(data, late) == found, \"last match "
    "differs\");\n"
    "    return found;\n"
    "}\n"
    "pub Encode :: (data: u8[]) -> string {\n"
    "    encoded := base64.Encode(data);\n"
    "    if (encoded.error) { assert(false, \"small encoding rejected\"); }\n"
    "    return encoded.text;\n"
    "}\n";

static compiler_fixture fixture;
static arena input, detached;

static void cleanup()
{
    compiler_fixture_free(&fixture);
    arena_free(&input);
    arena_free(&detached);
}

static pin_value repeated_bytes(const unsigned char *pattern, int count, int size)
{
    pin_array *array = arena_alloc(&input, sizeof *array);
    *array = (pin_array){ .element_type = PIN_U8,
                          .type_name = "u8",
                          .count = size,
                          .capacity = size,
                          .stride = 1,
                          .data = arena_alloc(&input, (size_t)size) };
    unsigned char *data = array->data;
    for (int i = 0; i < size; i++) {
        data[i] = pattern[i % count];
    }
    return (pin_value){ .type = PIN_U8, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}

static bool retains_bytes(pin_value value, const unsigned char *pattern, int count)
{
    const pin_array *array = value.ptr;
    const unsigned char *data = array->data;
    for (int i = 0; i < array->count; i++) {
        REQUIRE(data[i] == pattern[i % count]);
    }
    return true;
}

static const char *backend_name(int backend)
{
    if (!backend) {
        return "bytecode";
    }
    return TEST_COMPILED_BACKEND == BEHAVIOR_SLJIT ? "sljit" : "wasm";
}

static bool measured_call(
    int backend,
    int size,
    const char *entry,
    const pin_value *arguments,
    int count,
    pin_value *out
)
{
    struct timespec start, finish;
    pin_copy_scope copies = { 0 };
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    pin_copy_scope_begin(&copies);
    bool called = compiler_fixture_call(
        &fixture,
        backend,
        "project:portable_costs",
        entry,
        arguments,
        count,
        100000000,
        &detached,
        out
    );
    pin_copy_scope_end(&copies);
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &finish) == 0);
    double elapsed = (double)(finish.tv_sec - start.tv_sec) +
                     (double)(finish.tv_nsec - start.tv_nsec) / 1000000000.0;
    printf(
        "portable-costs backend=%s workload=%s input_bytes=%d "
        "call_seconds=%.9f copy_text_bytes=%" PRIu64
        " copy_nominal_bytes=%" PRIu64 " copy_record_bytes=%" PRIu64
        " copy_value_buffer_bytes=%" PRIu64 " copy_value_header_moves=%" PRIu64
        " copy_overflow=%d\n",
        backend_name(backend),
        entry,
        size,
        elapsed,
        copies.counts.text_bytes,
        copies.counts.nominal_bytes,
        copies.counts.record_bytes,
        copies.counts.value_buffer_bytes,
        copies.counts.value_header_moves,
        copies.counts.overflow
    );
    REQUIRE(called);
    return true;
}

static bool validate_cost(int backend, int size)
{
    /* Each four-byte block contains ASCII, a two-byte scalar and U+0000. */
    const unsigned char pattern[] = { 65, 194, 162, 0 };
    pin_value arguments[2] = { repeated_bytes(pattern, sizeof pattern, size) };
    const pin_array *array = arguments[0].ptr;
    REQUIRE(!pin_value_text(&input, &arguments[1], array->data, (size_t)size));
    pin_value out = { 0 };
    REQUIRE(measured_call(backend, size, "Validate", arguments, 2, &out));
    REQUIRE(out.type == PIN_I32 && out.i == size / 4 * 3);
    REQUIRE(retains_bytes(arguments[0], pattern, sizeof pattern));
    pin_string text = pin_text(arguments[1]);
    REQUIRE(text.length == (uint32_t)size && !memcmp(text.bytes, array->data, (size_t)size));
    arena_free(&input);
    arena_free(&detached);
    return true;
}

static bool search_cost(int backend, int size)
{
    /* Repeated prefixes force KMP fallbacks until the one late match. */
    const unsigned char repeated[] = { 97 };
    pin_value arguments[] = { repeated_bytes(repeated, 1, size),
                              repeated_bytes(repeated, 1, 32),
                              repeated_bytes(repeated, 1, 32) };
    pin_array *data = arguments[0].ptr, *late = arguments[1].ptr,
              *missing = arguments[2].ptr;
    ((unsigned char *)data->data)[size - 1] = 98;
    ((unsigned char *)late->data)[31] = 98;
    ((unsigned char *)missing->data)[31] = 99;
    pin_value out = { 0 };
    REQUIRE(measured_call(backend, size, "Search", arguments, 3, &out));
    REQUIRE(out.type == PIN_I32 && out.i == size - 32);
    for (int i = 0; i < size; i++) {
        REQUIRE(((const unsigned char *)data->data)[i] == (i == size - 1 ? 98 : 97));
    }
    REQUIRE(((const unsigned char *)late->data)[31] == 98);
    REQUIRE(((const unsigned char *)missing->data)[31] == 99);
    arena_free(&input);
    arena_free(&detached);
    return true;
}

static bool encode_cost(int backend, int size)
{
    const unsigned char pattern[] = { 0, 1, 127, 128, 255, 65 };
    pin_value argument = repeated_bytes(pattern, sizeof pattern, size);
    pin_value out = { 0 };
    REQUIRE(measured_call(backend, size, "Encode", &argument, 1, &out));
    REQUIRE(out.type == PIN_STR);
    pin_string encoded = pin_text(out);
    /* Independent literal Base64 answers for the six-byte block and four-byte
     * tail. Both selected input sizes end with that tail. */
    REQUIRE(size % 6 == 4);
    int blocks = size / 6;
    REQUIRE(encoded.length == (uint32_t)(blocks * 8 + 8));
    for (int i = 0; i < blocks; i++) {
        REQUIRE(!memcmp(encoded.bytes + i * 8, "AAF/gP9B", 8));
    }
    REQUIRE(!memcmp(encoded.bytes + blocks * 8, "AAF/gA==", 8));
    REQUIRE(retains_bytes(argument, pattern, sizeof pattern));
    arena_free(&input);
    arena_free(&detached);
    return true;
}

TEST_CASE(portable_package_costs)
{
    static const source_package_input sources[] = { PORTABLE_SOURCE_INPUTS };
    enum
    {
        SOURCES = sizeof sources / sizeof *sources
    };
    source_package_input files[2 + SOURCES];
    char paths[SOURCES][256];
    files[0] = (source_package_input){ "/sdk/core/strings/Strings.chibi",
                                       CORE_STRINGS_SOURCE,
                                       sizeof CORE_STRINGS_SOURCE - 1 };
    files[1] = (source_package_input){ "/compiler/portable_costs/Costs.chibi",
                                       CONSUMER,
                                       sizeof CONSUMER - 1 };
    for (int i = 0; i < SOURCES; i++) {
        int count = snprintf(
            paths[i],
            sizeof paths[i],
            "/sdk/core/%s",
            sources[i].path
        );
        REQUIRE(count > 0 && (size_t)count < sizeof paths[i]);
        files[2 + i] = sources[i];
        files[2 + i].path = paths[i];
    }
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, files, 2 + SOURCES, core, 1));
    printf(
        "portable-costs uses TIME_UTC intervals and selected same-thread "
        "managed-value C copy counters. Includes fixture call/result "
        "detachment; "
        "excludes compilation, input setup, other memory traffic and worker "
        "threads. Allocation counts are unavailable.\n"
    );
    const int sizes[] = { 4096, 16384 };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(behavior_backend_available(backends[backend]));
        for (int i = 0; i < 2; i++) {
            REQUIRE(validate_cost(backend, sizes[i]));
            REQUIRE(search_cost(backend, sizes[i]));
            REQUIRE(encode_cost(backend, sizes[i]));
        }
    }
    return true;
}

void test_language_package_portable_costs()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(portable_package_costs);
    pu_clear_setup();
}
