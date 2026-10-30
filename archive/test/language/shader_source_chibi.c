#include "compiler_fixture.h"
#include "package_core_source.h"
#include "runtime/shaders/program.h"
#include "shader_source_chibi_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"
#include "types/union.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Twelve cases, 84 policy and 66 lowering calls on bytecode and the configured
 * compiled backend; five source files are prepared once per backend.
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
static int lower_calls;
static arena input, detached, retained[2];
static pin_value kept[2];
static arena lower_retained[2];
static pin_value lower_kept[2];

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
    if (text.length != strlen(expected) || memcmp(text.bytes, expected, text.length)) {
        fprintf(
            stderr,
            "shader text: expected %s, received %.*s\n",
            expected,
            (int)text.length,
            text.bytes
        );
    }
    REQUIRE(text.length == strlen(expected) && !memcmp(text.bytes, expected, text.length));
    return true;
}
static bool package_rows(int backend, const char *package, const char *name, int count, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, backend, package, name, &input);
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
static bool rows(int backend, const char *name, int count, pin_value *out)
{
    return package_rows(backend, PACKAGE, name, count, out);
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
    if (data->count != count && data->count == 1 && data->values[0].type == PIN_STRUCT) {
        pin_value message;
        REQUIRE(read(data->values, "message", &message));
        pin_string text = pin_text(message);
        fprintf(stderr, "shader rejected: %.*s\n", (int)text.length, text.bytes);
    }
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
        { "shader_source/Lower.chibi",
          SHADER_LOWER_CHIBI_SOURCE,
          sizeof SHADER_LOWER_CHIBI_SOURCE - 1 },
    };
    REQUIRE(compiler_fixture_prepare(&fixture, sources, 5));
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
/* Supplied semantic rows only. The labels below are provenance test bytes,
 * deliberately not presented as output from an ordinary Analyze producer. */
static bool facts_rows(int b, const char *name, int count, pin_value *out)
{
    return package_rows(b, "project:compiler_facts", name, count, out);
}
static bool facts_result(int b, const char *name, const pin_value *payload, int count, pin_value *out)
{
    const char *package = "project:compiler_facts";
    const authoring_package *owner = compiler_fixture_package(&fixture, b, package);
    REQUIRE(owner && owner->exports && owner->exports->metadata);
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(&fixture, b, package, "Result")
    );
    REQUIRE(type);
    for (int i = 0; i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, name)) {
            REQUIRE(!compiler_fixture_union(&input, type->variants[i].tag, payload, count, out));
            return true;
        }
    }
    REQUIRE(false);
    return false;
}
static bool scalar_facts(int b, pin_value p[10], pin_value *target)
{
    p[0] = integer(0);
    REQUIRE(facts_rows(b, "Document", 1, &p[1]));
    REQUIRE(put_text(row(&p[1], 0), "identity", "supplied-facts:scalar"));
    REQUIRE(put_text(row(&p[1], 0), "key", "scalar"));
    REQUIRE(put_text(row(&p[1], 0), "path", "scalar.chibi"));
    REQUIRE(put_text(row(&p[1], 0), "text", "scalar fixture provenance"));
    REQUIRE(facts_rows(b, "Origin", 13, &p[2]));
    for (int i = 0; i < 13; i++) {
        pin_value *origin = row(&p[2], i);
        REQUIRE(put(origin, "document", integer(0)));
        REQUIRE(put(origin, "node", integer(10000 + i)));
        REQUIRE(put(origin, "start", integer(i)));
        REQUIRE(put(origin, "end", integer(i + 1)));
        REQUIRE(put(origin, "line", integer(1)));
        REQUIRE(put(origin, "column", integer(i + 1)));
        REQUIRE(put(origin, "occurrence", identity(UINT64_C(0xfedc000000000000) + i)));
    }
    REQUIRE(facts_rows(b, "Type", 2, &p[3]));
    for (int i = 0; i < 2; i++) {
        REQUIRE(put(row(&p[3], i), "storage", integer(PIN_F32)));
    }
    REQUIRE(facts_rows(b, "Field", 0, &p[4]));
    REQUIRE(facts_rows(b, "Function", 1, &p[5]));
    pin_value *function = row(&p[5], 0);
    REQUIRE(put(function, "semantic", identity(UINT64_C(0x8000000000000001))));
    REQUIRE(put_text(function, "name", "Shade"));
    REQUIRE(put(function, "statement_count", integer(3)));
    REQUIRE(put(function, "native_kind", integer(-1)));
    REQUIRE(put(function, "intrinsic", integer(-1)));
    REQUIRE(facts_rows(b, "Binding", 2, &p[6]));
    for (int i = 0; i < 2; i++) {
        pin_value *binding = row(&p[6], i);
        REQUIRE(put(binding, "origin", integer(i ? 5 : 2)));
        REQUIRE(put_text(binding, "name", i ? "alias" : "value"));
        REQUIRE(put(binding, "role", integer(1)));
        REQUIRE(put(binding, "ordinal", integer(i)));
        REQUIRE(put(binding, "initializer", integer(i)));
        REQUIRE(put(binding, "write_origin", integer(-1)));
        REQUIRE(put(binding, "ref_origin", integer(-1)));
    }
    REQUIRE(facts_rows(b, "Statement", 3, &p[7]));
    for (int i = 0; i < 3; i++) {
        pin_value *statement = row(&p[7], i);
        REQUIRE(put(statement, "kind", integer(i < 2 ? 1 : 2)));
        REQUIRE(put(statement, "origin", integer(i * 3 + 1)));
        REQUIRE(put(statement, "binding", integer(i < 2 ? i : -1)));
        REQUIRE(put(statement, "expression", integer(i < 2 ? i : 6)));
    }
    REQUIRE(facts_rows(b, "Expression", 7, &p[8]));
    const int kinds[] = { 1, 2, 2, 1, 3, 1, 3 };
    const int origins[] = { 3, 6, 8, 9, 10, 11, 12 };
    for (int i = 0; i < 7; i++) {
        pin_value *expression = row(&p[8], i);
        REQUIRE(put(expression, "kind", integer(kinds[i])));
        REQUIRE(put(expression, "origin", integer(origins[i])));
        REQUIRE(
            put(expression,
                "binding",
                integer(
                    i == 1   ? 0
                    : i == 2 ? 1
                             : -1
                ))
        );
        REQUIRE(put(expression, "callee", integer(-1)));
        REQUIRE(put(expression, "field", integer(-1)));
        REQUIRE(put(expression, "operand_type", integer(kinds[i] == 3 ? 0 : -1)));
        REQUIRE(put(expression, "first_argument", integer(i == 6 ? 2 : 0)));
        REQUIRE(put(expression, "argument_count", integer(kinds[i] == 3 ? 2 : 0)));
        REQUIRE(
            put(expression,
                "operation",
                integer(
                    i == 4   ? 1
                    : i == 6 ? 2
                             : 0
                ))
        );
    }
    REQUIRE(put(row(&p[8], 0), "bits", identity(UINT32_C(0x3f000000))));
    REQUIRE(put(row(&p[8], 3), "bits", identity(UINT32_C(0x3e000000))));
    REQUIRE(put(row(&p[8], 5), "bits", identity(UINT32_C(0x40000000))));
    REQUIRE(facts_rows(b, "Argument", 4, &p[9]));
    const int operands[] = { 2, 3, 4, 5 };
    for (int i = 0; i < 4; i++) {
        pin_value *argument = row(&p[9], i);
        REQUIRE(put(argument, "expression", integer(operands[i])));
        REQUIRE(put(argument, "parameter", integer(-1)));
        REQUIRE(put(argument, "field", integer(-1)));
        REQUIRE(put(argument, "origin", integer(origins[operands[i]])));
    }
    pin_value targets;
    REQUIRE(rows(b, "ScalarTarget", 1, &targets));
    *target = *row(&targets, 0);
    REQUIRE(put(target, "float_storage", integer(PIN_F32)));
    REQUIRE(put(target, "constant_op", integer(SHADER_CONSTANT)));
    REQUIRE(put(target, "add_op", integer(SHADER_ADD)));
    REQUIRE(put(target, "multiply_op", integer(SHADER_MULTIPLY)));
    return true;
}
static bool lower(int b, pin_value p[10], pin_value target, pin_value *out)
{
    pin_value args[2];
    REQUIRE(facts_result(b, "Accepted", p, 10, &args[0]));
    args[1] = target;
    lower_calls++;
    return call(b, "LowerScalar", args, 2, out);
}
static bool scalar_graph(const pin_value *payload, shader_graph *graph)
{
    REQUIRE(payload[0].type == PIN_STRUCT && payload[0].container == PIN_CONTAINER_ARRAY);
    const pin_array *nodes = payload[0].ptr;
    REQUIRE(nodes && nodes->count <= SHADER_NODE_MAX);
    shader_graph_init(graph);
    for (int i = 0; i < nodes->count; i++) {
        pin_value op, left, right, bits, source;
        REQUIRE(read(&nodes->items[i], "op", &op) && op.type == PIN_I32);
        REQUIRE(read(&nodes->items[i], "left", &left) && left.type == PIN_I32);
        REQUIRE(read(&nodes->items[i], "right", &right) && right.type == PIN_I32);
        REQUIRE(
            read(&nodes->items[i], "bits", &bits) && bits.type == PIN_U64 &&
            bits.u64 <= UINT32_MAX
        );
        REQUIRE(read(&nodes->items[i], "source_id", &source) && source.type == PIN_U64);
        REQUIRE(op.i == SHADER_CONSTANT || op.i == SHADER_ADD || op.i == SHADER_MULTIPLY);
        REQUIRE(left.i >= -1 && left.i < i && right.i >= -1 && right.i < i);
        int index = shader_graph_add(graph, op.i, 1);
        REQUIRE(index == i);
        shader_node *node = &graph->nodes[index];
        node->input[0] = left.i;
        node->input[1] = right.i;
        uint32_t raw = (uint32_t)bits.u64;
        memcpy(&node->value[0], &raw, sizeof raw);
        node->source_id = source.u64;
    }
    REQUIRE(
        payload[1].type == PIN_I32 && payload[1].i >= 0 && payload[1].i < nodes->count
    );
    graph->output_count = 1;
    graph->output[0] = payload[1].i;
    shader_error error = { 0 };
    REQUIRE(shader_graph_validate(graph, &error));
    return true;
}
TEST_CASE(shader_scalar_facts_lower_to_existing_ir)
{
    REQUIRE(prepared);
    for (int b = 0; b < 2; b++) {
        cleanup();
        pin_value p[10], target, value;
        const pin_value *payload;
        REQUIRE(scalar_facts(b, p, &target));
        REQUIRE(lower(b, p, target, &value));
        REQUIRE(tagged(b, &value, "ScalarLowering", "Ready", 5, &payload));
        const pin_array *nodes = payload[0].ptr;
        REQUIRE(nodes && nodes->count == 5);
        const uint64_t ids[] = { 4, 10, 11, 12, 13 };
        for (int i = 0; i < 5; i++) {
            REQUIRE(same_identity(&nodes->items[i], "source_id", ids[i]));
        }
        REQUIRE(!compiler_fixture_snapshot(&lower_retained[b], &lower_kept[b], &value));
        REQUIRE(lower(b, p, target, &value));
        REQUIRE(pin_value_equal(&lower_kept[b], &value));
        arena_free(&input);
        REQUIRE(tagged(b, &value, "ScalarLowering", "Ready", 5, &payload));
        shader_graph graph = { 0 };
        REQUIRE(scalar_graph(payload, &graph));
        float values[5][4] = { 0 };
        shader_error error = { 0 };
        shader_sample_context sample = { 0 };
        REQUIRE(shader_graph_eval_values(&graph, &sample, NULL, values, &error));
        REQUIRE(values[graph.output[0]][0] == 1.25f);
        const pin_array *results = payload[4].ptr;
        REQUIRE(results && results->count == 13);
        int aliases = 0;
        for (int i = 0; i < results->count; i++) {
            pin_value source;
            REQUIRE(read(&results->items[i], "source_id", &source) && source.type == PIN_U64);
            REQUIRE(same_integer(&results->items[i], "field", -1));
            if (source.u64 == 3 || source.u64 == 6 || source.u64 == 7 ||
                source.u64 == 9) {
                REQUIRE(same_integer(&results->items[i], "node", 0));
                aliases++;
            }
        }
        REQUIRE(aliases == 4);
        const pin_array *origins = payload[3].ptr;
        REQUIRE(origins && origins->count == 13);
        REQUIRE(same_identity(&origins->items[3], "occurrence", UINT64_C(0xfedc000000000003)));
        shader_graph_free(&graph);
    }
    return true;
}
TEST_CASE(shader_scalar_reuses_shared_expression_nodes)
{
    REQUIRE(prepared);
    for (int b = 0; b < 2; b++) {
        cleanup();
        pin_value p[10], target, value;
        const pin_value *payload;
        REQUIRE(scalar_facts(b, p, &target));
        /* The return multiplies the same unevaluated add expression by itself. */
        REQUIRE(put(row(&p[9], 3), "expression", integer(4)));
        REQUIRE(lower(b, p, target, &value));
        arena_free(&input);
        REQUIRE(tagged(b, &value, "ScalarLowering", "Ready", 5, &payload));
        const pin_array *nodes = payload[0].ptr;
        REQUIRE(nodes && nodes->count == 4);
        REQUIRE(same_integer(&nodes->items[3], "left", 2));
        REQUIRE(same_integer(&nodes->items[3], "right", 2));
        shader_graph graph = { 0 };
        REQUIRE(scalar_graph(payload, &graph));
        float values[4][4] = { 0 };
        shader_sample_context sample = { 0 };
        shader_error error = { 0 };
        REQUIRE(shader_graph_eval_values(&graph, &sample, NULL, values, &error));
        REQUIRE(values[graph.output[0]][0] == 0.390625f);
        shader_graph_free(&graph);
    }
    return true;
}
TEST_CASE(shader_scalar_preserves_ordinary_rejection)
{
    REQUIRE(prepared);
    for (int b = 0; b < 2; b++) {
        cleanup();
        pin_value p[10], target, diagnostics, rejected[2], args[2], value;
        const pin_value *payload;
        REQUIRE(scalar_facts(b, p, &target));
        REQUIRE(package_rows(b, "project:syntax_diagnostics", "Diagnostic", 1, &diagnostics));
        pin_value diagnostic = *row(&diagnostics, 0);
        REQUIRE(put(&diagnostic, "document", integer(0)));
        REQUIRE(put(&diagnostic, "start", integer(2)));
        REQUIRE(put(&diagnostic, "end", integer(8)));
        REQUIRE(put_text(&diagnostic, "message", "Ordinary source diagnostic"));
        rejected[0] = p[1];
        rejected[1] = diagnostic;
        REQUIRE(facts_result(b, "Rejected", rejected, 2, &args[0]));
        args[1] = target;
        lower_calls++;
        REQUIRE(call(b, "LowerScalar", args, 2, &value));
        arena_free(&input);
        REQUIRE(tagged(b, &value, "ScalarLowering", "Rejected", 1, &payload));
        REQUIRE(same_text(payload, "message", "Ordinary source diagnostic"));
        REQUIRE(same_integer(payload, "document", 0));
        REQUIRE(same_integer(payload, "start", 2));
        REQUIRE(same_integer(payload, "end", 8));
        REQUIRE(same_integer(payload, "node", -1));
        REQUIRE(same_identity(payload, "source_id", 0));
    }
    return true;
}
TEST_CASE(shader_scalar_constants_preserve_exact_bits)
{
    REQUIRE(prepared);
    const uint32_t bits[] = { UINT32_C(0x80000000), UINT32_C(1), UINT32_C(0x7f7fffff), 0 };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 4; i++) {
            cleanup();
            pin_value p[10], target, value;
            const pin_value *payload;
            REQUIRE(scalar_facts(b, p, &target));
            REQUIRE(put(row(&p[5], 0), "first_statement", integer(2)));
            REQUIRE(put(row(&p[5], 0), "statement_count", integer(1)));
            REQUIRE(put(row(&p[7], 2), "expression", integer(0)));
            REQUIRE(put(row(&p[8], 0), "bits", identity(bits[i])));
            REQUIRE(lower(b, p, target, &value));
            arena_free(&input);
            REQUIRE(tagged(b, &value, "ScalarLowering", "Ready", 5, &payload));
            const pin_array *nodes = payload[0].ptr;
            REQUIRE(nodes && nodes->count == 1);
            REQUIRE(same_identity(&nodes->items[0], "bits", bits[i]));
            shader_graph graph = { 0 };
            REQUIRE(scalar_graph(payload, &graph));
            uint32_t actual;
            memcpy(&actual, &graph.nodes[0].value[0], sizeof actual);
            REQUIRE(actual == bits[i]);
            float values[1][4] = { 0 };
            shader_sample_context sample = { 0 };
            shader_error error = { 0 };
            REQUIRE(shader_graph_eval_values(&graph, &sample, NULL, values, &error));
            memcpy(&actual, &values[0][0], sizeof actual);
            REQUIRE(actual == bits[i]);
            shader_graph_free(&graph);
        }
    }
    return true;
}
TEST_CASE(shader_scalar_rejects_unsupported_and_invalid_facts)
{
    REQUIRE(prepared);
    static const struct
    {
        const char *message;
        int origin;
    } cases[] = {
        { "Shader scalar entry must be pure", 0 },
        { "Shader scalar entry must be a nullary source function returning "
          "number",
          0 },
        { "Unsupported shader scalar statement", 1 },
        { "Shader scalar bindings must be immutable", 2 },
        { "Shader scalar bindings cannot escape by reference", 2 },
        { "Unsupported shader scalar expression", 3 },
        { "Unsupported shader scalar binary operation", 10 },
        { "Unsupported shader scalar binary operation", 10 },
        { "Unsupported shader scalar expression or type", 3 },
        { "Shader scalar read requires an initialized immutable local", 6 },
        { "Shader scalar constants must be finite f32 bits", 3 },
        { "Shader scalar constants must be finite f32 bits", 3 },
        { "Shader scalar return must be the final statement", 1 },
        { "Shader scalar entry needs a final return", 0 },
        { "Unsupported shader scalar binary operation", 10 },
        { "Invalid shader scalar operands", 10 },
        { "Shader scalar expressions contain a cycle", 10 },
        { "Invalid shader scalar initialization", 1 },
        { "Shader scalar entry must be a nullary source function returning "
          "number",
          0 },
        { "Unsupported shader scalar expression", 3 },
        { "Unsupported shader scalar expression", 9 },
        { "Unsupported shader scalar expression", 3 },
        { "Unsupported shader scalar expression or type", 3 },
        { "Unsupported shader scalar expression or type", 3 },
        { "Shader scalar origin has conflicting results", 3 },
    };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
            cleanup();
            pin_value p[10], target, value;
            const pin_value *payload;
            REQUIRE(scalar_facts(b, p, &target));
            switch (i) {
                case 0:
                    REQUIRE(put(row(&p[5], 0), "effects", integer(1)));
                    break;
                case 1:
                    REQUIRE(put(row(&p[5], 0), "parameter_count", integer(1)));
                    break;
                case 2: REQUIRE(put(row(&p[7], 0), "kind", integer(0))); break;
                case 3:
                    REQUIRE(put(row(&p[6], 0), "write_origin", integer(2)));
                    break;
                case 4:
                    REQUIRE(put(row(&p[6], 0), "ref_origin", integer(2)));
                    break;
                case 5: REQUIRE(put(row(&p[8], 0), "kind", integer(7))); break;
                case 6:
                    REQUIRE(put(row(&p[8], 4), "operation", integer(999)));
                    break;
                case 7:
                    REQUIRE(put(row(&p[8], 4), "operand_type", integer(-1)));
                    break;
                case 8:
                    REQUIRE(put(row(&p[3], 1), "qualifiers", integer(2)));
                    REQUIRE(put(row(&p[8], 0), "type", integer(1)));
                    break;
                case 9:
                    REQUIRE(put(row(&p[8], 1), "binding", integer(1)));
                    break;
                case 10:
                    REQUIRE(put(row(&p[8], 0), "bits", identity(UINT32_C(0x7fc00001))));
                    break;
                case 11:
                    REQUIRE(put(row(&p[8], 0), "bits", identity(UINT64_C(0x13f000000))));
                    break;
                case 12: REQUIRE(put(row(&p[7], 0), "kind", integer(2))); break;
                case 13:
                    REQUIRE(put(row(&p[5], 0), "statement_count", integer(2)));
                    break;
                case 14:
                    REQUIRE(put(row(&p[8], 4), "first_argument", integer(4)));
                    break;
                case 15:
                    REQUIRE(put(row(&p[9], 0), "expression", integer(7)));
                    break;
                case 16:
                    REQUIRE(put(row(&p[9], 0), "expression", integer(4)));
                    break;
                case 17:
                    REQUIRE(put(row(&p[6], 0), "initializer", integer(1)));
                    break;
                case 18: REQUIRE(put(row(&p[5], 0), "kind", integer(1))); break;
                case 19: REQUIRE(put(row(&p[8], 0), "kind", integer(5))); break;
                case 20:
                    REQUIRE(put(row(&p[8], 2), "kind", integer(7)));
                    REQUIRE(put(row(&p[8], 3), "kind", integer(7)));
                    REQUIRE(put(row(&p[9], 0), "expression", integer(3)));
                    REQUIRE(put(row(&p[9], 1), "expression", integer(2)));
                    break;
                case 21:
                    /* The unsupported initializer is never read by the return. */
                    REQUIRE(put(row(&p[5], 0), "statement_count", integer(2)));
                    REQUIRE(put(row(&p[7], 1), "kind", integer(2)));
                    REQUIRE(put(row(&p[7], 1), "expression", integer(5)));
                    REQUIRE(put(row(&p[8], 0), "kind", integer(7)));
                    break;
                case 22:
                    REQUIRE(put(row(&p[3], 1), "storage", integer(PIN_F64)));
                    REQUIRE(put(row(&p[8], 0), "type", integer(1)));
                    break;
                case 23:
                    REQUIRE(put(row(&p[3], 1), "nominal", identity(UINT64_C(0x8000000000000001))));
                    REQUIRE(put(row(&p[8], 0), "type", integer(1)));
                    break;
                case 24:
                    REQUIRE(put(row(&p[8], 3), "origin", integer(3)));
                    break;
            }
            REQUIRE(lower(b, p, target, &value));
            arena_free(&input);
            REQUIRE(tagged(b, &value, "ScalarLowering", "Rejected", 1, &payload));
            REQUIRE(same_text(payload, "message", cases[i].message));
            REQUIRE(same_integer(payload, "document", 0));
            REQUIRE(same_integer(payload, "start", cases[i].origin));
            REQUIRE(same_integer(payload, "end", cases[i].origin + 1));
            REQUIRE(same_identity(payload, "source_id", cases[i].origin + 1));
        }
    }
    return true;
}

TEST_CASE(shader_policy_results_outlive_preparation)
{
    REQUIRE(prepared);
    REQUIRE(lower_calls == 66);
    REQUIRE(calls == 84 + lower_calls);
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
        REQUIRE(lower_kept[b].type == PIN_UNION && lower_kept[b].ptr);
        const pin_union *lowered = lower_kept[b].ptr;
        REQUIRE(lowered->count == 5);
        const pin_array *documents = lowered->values[2].ptr;
        REQUIRE(documents && documents->count == 1);
        REQUIRE(same_text(&documents->items[0], "identity", "supplied-facts:scalar"));
        REQUIRE(same_text(&documents->items[0], "text", "scalar fixture provenance"));
        shader_graph graph = { 0 };
        REQUIRE(scalar_graph(lowered->values, &graph));
        float values[5][4] = { 0 };
        shader_sample_context sample = { 0 };
        shader_error error = { 0 };
        REQUIRE(shader_graph_eval_values(&graph, &sample, NULL, values, &error));
        REQUIRE(values[graph.output[0]][0] == 1.25f);
        shader_graph_free(&graph);
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
    RUN_TEST_CASE(shader_scalar_facts_lower_to_existing_ir);
    RUN_TEST_CASE(shader_scalar_reuses_shared_expression_nodes);
    RUN_TEST_CASE(shader_scalar_preserves_ordinary_rejection);
    RUN_TEST_CASE(shader_scalar_constants_preserve_exact_bits);
    RUN_TEST_CASE(shader_scalar_rejects_unsupported_and_invalid_facts);
    RUN_TEST_CASE(shader_policy_results_outlive_preparation);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    prepared = false;
    calls = 0;
    lower_calls = 0;
    for (int b = 0; b < 2; b++) {
        arena_free(&retained[b]);
        kept[b] = (pin_value){ 0 };
        arena_free(&lower_retained[b]);
        lower_kept[b] = (pin_value){ 0 };
    }
}
