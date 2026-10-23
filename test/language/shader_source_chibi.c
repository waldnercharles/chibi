#include "compiler_fixture.h"
#include "package_core_source.h"
#include "shader_source_chibi_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"
#include "types/union.h"

#include <stdint.h>
#include <string.h>

/* Seven cases, 84 explicit policy calls on bytecode and the configured compiled
 * backend; four source files are prepared once per backend.
 * Inputs are explicitly supplied provenance, not compiler-produced facts or
 * authored-material acceptance. Each call has a one-million instruction cap;
 * no rendering, subprocesses, source discovery or prepared service is used. */
static const char PACKAGE[] = "project:shader_source";
static const uint64_t CALLER = UINT64_C(0xfedcba9876543210);
static const uint64_t HELPER = UINT64_C(0xfedcba9876543211);
static const uint64_t ALIAS = UINT64_C(0xfedcba9876543212);
static compiler_fixture fixture;
static bool prepared;
static int calls;
static arena input, detached, retained[2];
static pin_value kept[2];

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value identity(uint64_t value)
{
    return (pin_value){ .type = PIN_U64, .u64 = value };
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool put_text(pin_value *row, const char *name, const char *value)
{
    pin_value text;
    REQUIRE(!pin_value_text(&input, &text, value, strlen(value)));
    REQUIRE(put(row, name, text));
    return true;
}
static bool read(const pin_value *row, const char *name, pin_value *out)
{
    REQUIRE(row->type == PIN_STRUCT && row->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&detached, row->ptr, field, out));
    return true;
}
static bool same_integer(const pin_value *row, const char *name, int expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_I32 && value.i == expected);
    return true;
}
static bool same_identity(const pin_value *row, const char *name, uint64_t expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_U64 && value.u64 == expected);
    return true;
}
static bool same_text(const pin_value *row, const char *name, const char *expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_STR);
    pin_string text = pin_text(value);
    REQUIRE(text.length == strlen(expected) && !memcmp(text.bytes, expected, text.length));
    return true;
}
static bool rows(int backend, const char *name, int count, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, backend, PACKAGE, name, &input);
    REQUIRE(shape);
    pin_array
        *array = pin_array_snapshot_allocate(&input, PIN_STRUCT, shape->type.name, count);
    REQUIRE(array);
    array->record = shape;
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = shape->type.name,
                        .record = shape,
                        .ptr = array };
    for (int i = 0; i < count; i++) {
        array->items[i] = (pin_value){ .type = PIN_STRUCT,
                                       .type_name = shape->type.name,
                                       .record = shape,
                                       .ptr = arena_alloc(&input, shape->type.size) };
        memset(array->items[i].ptr, 0, shape->type.size);
    }
    return true;
}
static pin_value *row(pin_value *array, int index)
{
    return &((pin_array *)array->ptr)->items[index];
}
static pin_value scalars(pin_type type, const pin_value *items, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, type, 0, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = items[i];
    }
    return (pin_value){ .type = type, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
static bool tables(int backend, pin_value args[4])
{
    REQUIRE(rows(backend, "Document", 2, &args[0]));
    REQUIRE(put_text(row(&args[0], 0), "identity", "project:material"));
    REQUIRE(put_text(row(&args[0], 0), "text", "call helper; alias"));
    REQUIRE(put_text(row(&args[0], 1), "identity", "project:helper"));
    REQUIRE(put_text(row(&args[0], 1), "text", "float(0.125)"));
    REQUIRE(rows(backend, "Origin", 3, &args[1]));
    const uint64_t ids[] = { CALLER, HELPER, ALIAS };
    for (int i = 0; i < 3; i++) {
        pin_value *origin = row(&args[1], i);
        REQUIRE(put(origin, "source_id", identity(ids[i])));
        REQUIRE(put(origin, "occurrence", identity(UINT64_C(0x8000000000000010) + i)));
        REQUIRE(put(origin, "call_site", identity(i == 1 ? CALLER : 0)));
        REQUIRE(put(origin, "document", integer(i == 1 ? 1 : 0)));
        REQUIRE(put(origin, "start", integer(i == 2 ? 13 : 0)));
        REQUIRE(put(origin, "end", integer(i == 0 ? 11 : i == 1 ? 12 : 18)));
    }
    REQUIRE(rows(backend, "SourceResult", 2, &args[2]));
    for (int i = 0; i < 2; i++) {
        REQUIRE(put(row(&args[2], i), "source_id", identity(i ? ALIAS : HELPER)));
        REQUIRE(put(row(&args[2], i), "node", integer(0)));
        REQUIRE(put(row(&args[2], i), "field", integer(-1)));
    }
    pin_value nodes[] = { identity(HELPER), identity(0) };
    args[3] = scalars(PIN_U64, nodes, 2);
    return true;
}
static bool call(int backend, const char *name, const pin_value *args, int count, pin_value *out)
{
    arena_free(&detached);
    calls++;
    REQUIRE(
        compiler_fixture_call(&fixture, backend, PACKAGE, name, args, count, 1000000, &detached, out)
    );
    return true;
}
static bool tagged(
    int backend,
    const pin_value *value,
    const char *type_name,
    const char *name,
    int count,
    const pin_value **out
)
{
    const authoring_package *package = compiler_fixture_package(&fixture, backend, PACKAGE);
    REQUIRE(package && package->exports && package->exports->metadata);
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, backend, PACKAGE, type_name)
    );
    REQUIRE(type && value->type == PIN_UNION && value->ptr);
    const pin_union *data = value->ptr;
    REQUIRE(data->count == count);
    for (int i = 0; i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, name)) {
            REQUIRE(data->tag == type->variants[i].tag);
            *out = data->values;
            return true;
        }
    }
    REQUIRE(false);
    return false;
}
TEST_CASE(shader_policy_prepares_preserved_sources)
{
    source_package_input sources[] = {
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "compiler_facts/Facts.chibi",
          COMPILER_FACTS_CHIBI_SOURCE,
          sizeof COMPILER_FACTS_CHIBI_SOURCE - 1 },
        { "shader_source/SourceMap.chibi",
          SHADER_SOURCE_CHIBI_SOURCE,
          sizeof SHADER_SOURCE_CHIBI_SOURCE - 1 },
    };
    REQUIRE(compiler_fixture_prepare(&fixture, sources, 4));
    prepared = true;
    for (int b = 0; b < 2; b++) {
        const pin_record *origin = compiler_fixture_record(
            &fixture,
            b,
            "project:compiler_facts",
            "Origin",
            &input
        );
        REQUIRE(origin);
        const type_field *occurrence = pin_type_field_path(&origin->type, "occurrence");
        REQUIRE(occurrence && occurrence->type == PIN_U64);
        pin_value args[4], value;
        const pin_value *payload;
        REQUIRE(tables(b, args) && call(b, "Validate", args, 4, &value));
        arena_free(&input);
        REQUIRE(tagged(b, &value, "Validation", "Checked", 0, &payload));
    }
    return true;
}
TEST_CASE(shader_policy_rejects_invalid_provenance)
{
    REQUIRE(prepared);
    static const struct
    {
        const char *message;
        int document, start, end, node;
    } cases[] = {
        { "Shader source needs a document identity", 0, 0, 0, -1 },
        { "Shader source document identities must be unique", 1, 0, 0, -1 },
        { "Shader source origin has an invalid identity or document", -1, 0, 0, -1 },
        { "Shader source origin has an invalid identity or document", -1, 0, 0, -1 },
        { "Shader source origin has an invalid byte range", -1, 0, 0, -1 },
        { "Shader source IDs must have unique origins", 1, 0, 12, -1 },
        { "Shader source call chain has a missing origin", 1, 0, 12, -1 },
        { "Shader source call chain contains a cycle", 0, 0, 11, -1 },
        { "Shader operation has no source origin", -1, 0, 0, 1 },
        { "Shader source result has no origin", -1, 0, 0, 0 },
        { "Shader source result has an invalid node or field", 1, 0, 12, 2 },
        { "Shader source result has an invalid node or field", 1, 0, 12, 0 },
        { "Shader source result bindings must be unique per field", 1, 0, 12, -1 },
    };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
            cleanup();
            pin_value args[4], value;
            const pin_value *payload;
            REQUIRE(tables(b, args));
            switch (i) {
                case 0:
                    REQUIRE(put_text(row(&args[0], 0), "identity", ""));
                    break;
                case 1:
                    REQUIRE(put_text(row(&args[0], 1), "identity", "project:material"));
                    break;
                case 2:
                    REQUIRE(put(row(&args[1], 1), "source_id", identity(0)));
                    break;
                case 3:
                    REQUIRE(put(row(&args[1], 1), "document", integer(2)));
                    break;
                case 4:
                    REQUIRE(put(row(&args[1], 1), "end", integer(13)));
                    break;
                case 5:
                    REQUIRE(put(row(&args[1], 1), "source_id", identity(CALLER)));
                    break;
                case 6:
                    REQUIRE(put(row(&args[1], 1), "call_site", identity(42)));
                    break;
                case 7:
                    REQUIRE(put(row(&args[1], 0), "call_site", identity(HELPER)));
                    break;
                case 8: *row(&args[3], 1) = identity(42); break;
                case 9:
                    REQUIRE(put(row(&args[2], 0), "source_id", identity(42)));
                    break;
                case 10:
                    REQUIRE(put(row(&args[2], 0), "node", integer(2)));
                    break;
                case 11:
                    REQUIRE(put(row(&args[2], 0), "field", integer(-2)));
                    break;
                case 12:
                    REQUIRE(put(row(&args[2], 1), "source_id", identity(HELPER)));
                    break;
            }
            REQUIRE(call(b, "Validate", args, 4, &value));
            arena_free(&input);
            REQUIRE(tagged(b, &value, "Validation", "Failed", 1, &payload));
            REQUIRE(same_text(payload, "message", cases[i].message));
            REQUIRE(same_integer(payload, "document", cases[i].document));
            REQUIRE(same_integer(payload, "start", cases[i].start));
            REQUIRE(same_integer(payload, "end", cases[i].end));
            REQUIRE(same_integer(payload, "node", cases[i].node));
        }
    }
    return true;
}
TEST_CASE(shader_policy_keeps_aliases_and_owned_call_chains)
{
    REQUIRE(prepared);
    for (int b = 0; b < 2; b++) {
        cleanup();
        pin_value args[4], value;
        const pin_value *payload;
        REQUIRE(tables(b, args));
        pin_value result_args[] = { args[2], identity(ALIAS), integer(-1) };
        REQUIRE(call(b, "ResultNode", result_args, 3, &value));
        REQUIRE(value.type == PIN_I32 && value.i == 0);
        pin_value find_args[] = { args[3], identity(HELPER), integer(-1) };
        REQUIRE(call(b, "FindNode", find_args, 3, &value));
        REQUIRE(value.type == PIN_I32 && value.i == 0);
        find_args[2] = integer(0);
        REQUIRE(call(b, "FindNode", find_args, 3, &value));
        REQUIRE(value.type == PIN_I32 && value.i == -1);
        pin_value chain_args[] = { args[1], identity(HELPER) };
        REQUIRE(call(b, "CallChain", chain_args, 2, &value));
        arena_free(&input);
        REQUIRE(tagged(b, &value, "ChainResult", "Found", 1, &payload));
        REQUIRE(payload->type == PIN_STRUCT && payload->container == PIN_CONTAINER_ARRAY);
        const pin_array *chain = payload->ptr;
        REQUIRE(chain && chain->count == 2);
        REQUIRE(same_identity(&chain->items[0], "source_id", HELPER));
        REQUIRE(same_identity(&chain->items[0], "occurrence", UINT64_C(0x8000000000000011)));
        REQUIRE(same_identity(&chain->items[1], "source_id", CALLER));
        REQUIRE(!compiler_fixture_snapshot(&retained[b], &kept[b], &value));
    }
    return true;
}
TEST_CASE(shader_policy_guards_ordered_closures_and_generations)
{
    REQUIRE(prepared);
    for (int b = 0; b < 2; b++) {
        cleanup();
        pin_value tables_args[4], value, current;
        REQUIRE(tables(b, tables_args));
        REQUIRE(rows(b, "Document", 2, &current));
        *row(&current, 0) = *row(&tables_args[0], 0);
        *row(&current, 1) = *row(&tables_args[0], 1);
        pin_value args[] = { tables_args[0], current };
        REQUIRE(call(b, "DocumentsMatch", args, 2, &value));
        REQUIRE(value.type == PIN_BOOL && value.b);
        *row(&current, 0) = *row(&tables_args[0], 1);
        *row(&current, 1) = *row(&tables_args[0], 0);
        REQUIRE(call(b, "DocumentsMatch", args, 2, &value));
        REQUIRE(value.type == PIN_BOOL && !value.b);
        REQUIRE(!compiler_fixture_snapshot(&input, &current, &tables_args[0]));
        args[1] = current;
        REQUIRE(put_text(row(&current, 1), "text", "float(0.25)"));
        REQUIRE(call(b, "DocumentsMatch", args, 2, &value));
        REQUIRE(value.type == PIN_BOOL && !value.b);
        REQUIRE(put_text(row(&current, 1), "text", "float(0.125)"));
        REQUIRE(put_text(row(&current, 1), "identity", "project:other"));
        REQUIRE(call(b, "DocumentsMatch", args, 2, &value));
        REQUIRE(value.type == PIN_BOOL && !value.b);
        REQUIRE(put_text(row(&current, 1), "identity", "project:helper"));
        pin_value embedded;
        REQUIRE(!pin_value_text(&input, &embedded, "x\0y", 3));
        REQUIRE(put(row(&current, 1), "text", embedded));
        REQUIRE(!pin_value_text(&input, &embedded, "x\0z", 3));
        REQUIRE(put(row(&tables_args[0], 1), "text", embedded));
        REQUIRE(call(b, "DocumentsMatch", args, 2, &value));
        REQUIRE(value.type == PIN_BOOL && !value.b);
        REQUIRE(put(row(&current, 1), "text", embedded));
        REQUIRE(call(b, "DocumentsMatch", args, 2, &value));
        REQUIRE(value.type == PIN_BOOL && value.b);
        pin_value matches[] = { boolean(true), boolean(true) };
        pin_value closure[] = { identity(HELPER),
                                identity(HELPER),
                                integer(2),
                                integer(2),
                                scalars(PIN_BOOL, matches, 2) };
        REQUIRE(call(b, "ClosureMatches", closure, 5, &value));
        REQUIRE(value.type == PIN_BOOL && value.b);
        closure[1] = identity(CALLER);
        REQUIRE(call(b, "ClosureMatches", closure, 5, &value));
        REQUIRE(value.type == PIN_BOOL && !value.b);
        closure[0] = identity(0);
        closure[1] = identity(0);
        REQUIRE(call(b, "ClosureMatches", closure, 5, &value));
        REQUIRE(value.type == PIN_BOOL && !value.b);
        closure[0] = closure[1] = identity(HELPER);
        closure[3] = integer(1);
        REQUIRE(call(b, "ClosureMatches", closure, 5, &value));
        REQUIRE(value.type == PIN_BOOL && !value.b);
        closure[3] = integer(2);
        *row(&closure[4], 1) = boolean(false);
        REQUIRE(call(b, "ClosureMatches", closure, 5, &value));
        REQUIRE(value.type == PIN_BOOL && !value.b);
    }
    return true;
}
TEST_CASE(shader_policy_bounds_tables_and_bytes)
{
    REQUIRE(prepared);
    static const char *const messages[] = {
        "Shader source document limit exceeded",
        "Shader source origin limit exceeded",
        "Shader source result limit exceeded",
        "Shader source node limit exceeded",
        "Shader source identity byte limit exceeded",
        "Shader source text byte limit exceeded",
    };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 6; i++) {
            cleanup();
            pin_value args[4], value;
            const pin_value *payload;
            REQUIRE(tables(b, args));
            switch (i) {
                case 0: REQUIRE(rows(b, "Document", 257, &args[0])); break;
                case 1: REQUIRE(rows(b, "Origin", 1025, &args[1])); break;
                case 2: REQUIRE(rows(b, "SourceResult", 1025, &args[2])); break;
                case 3: {
                    pin_value nodes[1025] = { 0 };
                    for (int n = 0; n < 1025; n++) {
                        nodes[n] = identity(0);
                    }
                    args[3] = scalars(PIN_U64, nodes, 1025);
                    break;
                }
                case 4:
                case 5: {
                    int count = i == 4 ? 4097 : 524289;
                    char *bytes = arena_alloc(&input, count);
                    memset(bytes, 'x', count);
                    pin_value text;
                    REQUIRE(!pin_value_text(&input, &text, bytes, count));
                    REQUIRE(put(row(&args[0], 0), i == 4 ? "identity" : "text", text));
                    break;
                }
            }
            REQUIRE(call(b, "Validate", args, 4, &value));
            arena_free(&input);
            REQUIRE(tagged(b, &value, "Validation", "Failed", 1, &payload));
            REQUIRE(same_text(payload, "message", messages[i]));
            REQUIRE(same_integer(payload, "document", i >= 4 ? 0 : -1));
        }
    }
    return true;
}
TEST_CASE(shader_policy_rejects_absent_queries_and_broken_chains)
{
    REQUIRE(prepared);
    for (int b = 0; b < 2; b++) {
        cleanup();
        pin_value args[4], value;
        const pin_value *payload;
        REQUIRE(tables(b, args));
        pin_value find[] = { args[3], identity(0), integer(-1) };
        REQUIRE(call(b, "FindNode", find, 3, &value));
        REQUIRE(value.type == PIN_I32 && value.i == -1);
        find[1] = identity(HELPER);
        find[2] = integer(-2);
        REQUIRE(call(b, "FindNode", find, 3, &value));
        REQUIRE(value.type == PIN_I32 && value.i == -1);
        find[2] = integer(2);
        REQUIRE(call(b, "FindNode", find, 3, &value));
        REQUIRE(value.type == PIN_I32 && value.i == -1);
        pin_value result[] = { args[2], identity(CALLER), integer(-1) };
        REQUIRE(call(b, "ResultNode", result, 3, &value));
        REQUIRE(value.type == PIN_I32 && value.i == -1);
        pin_value chain[] = { args[1], identity(42) };
        REQUIRE(call(b, "CallChain", chain, 2, &value));
        REQUIRE(tagged(b, &value, "ChainResult", "Failed", 1, &payload));
        REQUIRE(same_text(payload, "message", "Shader source origin was not found"));
        REQUIRE(put(row(&args[1], 1), "call_site", identity(42)));
        chain[1] = identity(HELPER);
        REQUIRE(call(b, "CallChain", chain, 2, &value));
        REQUIRE(tagged(b, &value, "ChainResult", "Failed", 1, &payload));
        REQUIRE(same_text(payload, "message", "Shader source call chain has a missing origin"));
        REQUIRE(same_identity(payload, "source_id", HELPER));
        REQUIRE(put(row(&args[1], 1), "call_site", identity(CALLER)));
        REQUIRE(put(row(&args[1], 0), "call_site", identity(HELPER)));
        REQUIRE(call(b, "CallChain", chain, 2, &value));
        arena_free(&input);
        REQUIRE(tagged(b, &value, "ChainResult", "Failed", 1, &payload));
        REQUIRE(same_text(payload, "message", "Shader source call chain contains a cycle"));
        REQUIRE(same_identity(payload, "source_id", HELPER));
    }
    return true;
}
TEST_CASE(shader_policy_results_outlive_preparation)
{
    REQUIRE(prepared);
    REQUIRE(calls == 84);
    compiler_fixture_free(&fixture);
    cleanup();
    for (int b = 0; b < 2; b++) {
        REQUIRE(kept[b].type == PIN_UNION && kept[b].ptr);
        const pin_union *value = kept[b].ptr;
        REQUIRE(value->count == 1 && value->values[0].container == PIN_CONTAINER_ARRAY);
        const pin_array *chain = value->values[0].ptr;
        REQUIRE(chain && chain->count == 2);
        REQUIRE(same_identity(&chain->items[0], "source_id", HELPER));
        REQUIRE(same_identity(&chain->items[1], "source_id", CALLER));
        REQUIRE(same_integer(&chain->items[0], "document", 1));
        REQUIRE(same_integer(&chain->items[0], "end", 12));
    }
    return true;
}
void test_language_shader_source_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(shader_policy_prepares_preserved_sources);
    RUN_TEST_CASE(shader_policy_rejects_invalid_provenance);
    RUN_TEST_CASE(shader_policy_keeps_aliases_and_owned_call_chains);
    RUN_TEST_CASE(shader_policy_guards_ordered_closures_and_generations);
    RUN_TEST_CASE(shader_policy_bounds_tables_and_bytes);
    RUN_TEST_CASE(shader_policy_rejects_absent_queries_and_broken_chains);
    RUN_TEST_CASE(shader_policy_results_outlive_preparation);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    prepared = false;
    calls = 0;
    for (int b = 0; b < 2; b++) {
        arena_free(&retained[b]);
        kept[b] = (pin_value){ 0 };
    }
}
