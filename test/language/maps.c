#include "authoring/source_literal.h"
#include "backend.h"
#include "capabilities/catalog_snapshot.h"
#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/inspection.h"
#include "execution/intrinsics.h"
#include "execution/map_intrinsics.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/map.h"
#include "types/text.h"
#include "types/value_wire.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const script_catalog_capability event = { .id = 901,
                                                 .name = "Run",
                                                 .kind = SCRIPT_CAPABILITY_EVENT };
static const script_catalog catalog = { .capabilities = &event, .capability_count = 1 };
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_program *program;
static execution_inspector *inspector;
static ecs_world *world;
static arena storage;

static void cleanup()
{
    execution_program_free(program);
    execution_inspector_free(inspector);
    execution_module_free(module);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    arena_free(&storage);
    program = NULL;
    inspector = NULL;
    module = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
}
static bool compile_in(const char *source, const script_catalog *types, behavior_backend backend)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, types);
    if (syntax->diagnostic_count || !analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(stderr, "map semantic: %s\n", analysis->diagnostics[i].message);
        }
        return false;
    }
    script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = script_lower(analysis, types, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(stderr, "map lower: %s\n", plan->diagnostics[i].message);
        }
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "map publication: %s\n", error);
        return false;
    }
    world = ecs_world_new();
    program = execution_program_new(module, world, NULL);
    return program != NULL;
}
static bool compile(const char *source, behavior_backend backend)
{
    return compile_in(source, &catalog, backend);
}
/* Compiles source against the records library declares, as a project's
 * catalog would hold them. */
static bool compile_with_records(const char *library, const char *source, behavior_backend backend)
{
    script_syntax *parsed = script_parse(library);
    script_analysis *checked = script_analyze(parsed, &catalog);
    script_catalog declared = { .types = checked->types,
                                .type_count = checked->type_count,
                                .capabilities = &event,
                                .capability_count = 1 },
                   owned;
    bool valid = !parsed->diagnostic_count && checked->valid &&
                 script_catalog_snapshot(&storage, &declared, &owned);
    script_analysis_free(checked);
    script_syntax_free(parsed);
    return valid && compile_in(source, &owned, backend);
}
static const pin_value *state(execution_instance *instance, const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (!strcmp(analysis->symbols[i].name, name) &&
            analysis->symbols[i].kind == SCRIPT_STATE) {
            return execution_instance_state(instance, analysis->symbols[i].id);
        }
    }
    return NULL;
}
static execution_instance *attach()
{
    return execution_program_attach(program, ecs_entity_to_u64(ecs_world_create_entity(world)));
}

TEST_CASE(maps_preserve_value_copies_and_snapshot_insertion_order)
{
    const char *source =
        "Maps :: behavior { result: int = 0; order: int = 0; "
        "scores: [string: int] = [\"Ada\": 1, \"Bea\": 2, \"Ada\": 3]; "
        "on Run :: () { copy := scores; scores[\"Ada\"] = 10; scores[\"Cid\"] "
        "= 4; "
        "for key, value in scores { order = order * 10 + value; result += "
        "value; "
        "scores[\"Later\"] = 100; } "
        "result += copy[\"Ada\"]; "
        "delete scores[\"Bea\"]; scores[\"Bea\"] = 5; "
        "if \"Ada\" in scores { result += 1; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = attach();
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, event.id, NULL, 0, 0, 0, 10000) == FLOW_OK);
        const pin_value *result = state(instance, "result"),
                        *order = state(instance, "order");
        REQUIRE(result && result->type == PIN_I32 && result->i == 20);
        REQUIRE(order && order->type == PIN_I32 && order->i == 1024);
        const pin_value *value = state(instance, "scores");
        REQUIRE(value && value->container == PIN_CONTAINER_MAP);
        const pin_map *map = value->ptr;
        REQUIRE(map && map->count == 4);
        REQUIRE(!strcmp(pin_text(map->entries[3].key).bytes, "Bea"));
        REQUIRE(map->entries[3].value.i == 5);
        cleanup();
    }
    return true;
}

/* Odin's Robin Hood index keeps lookups constant time: a few thousand keys,
 * strings and floats included, all found, in insertion order, after removals
 * that rebuild the index; 0.0 and -0.0 are one key. */
TEST_CASE(maps_index_finds_every_key_in_insertion_order)
{
    arena_init_collectable(&storage);
    pin_map *map = pin_map_create(
        &storage,
        (pin_value){ .type = PIN_I32 },
        (pin_value){ .type = PIN_I32 },
        NULL,
        0
    );
    REQUIRE(map);
    enum
    {
        KEYS = 3000
    };
    for (int i = 0; i < KEYS; i++) {
        pin_value key = { .type = PIN_I32, .i = i * 7919 },
                  value = { .type = PIN_I32, .i = i };
        REQUIRE(!pin_map_store_owned(&storage, &map, &key, &value));
    }
    REQUIRE(map->count == KEYS && map->slots);
    for (int i = 0; i < KEYS; i += 2) {
        pin_value key = { .type = PIN_I32, .i = i * 7919 };
        REQUIRE(!pin_map_delete_owned(&storage, map, &key));
    }
    REQUIRE(map->count == KEYS / 2);
    for (int i = 0; i < KEYS; i++) {
        pin_value key = { .type = PIN_I32, .i = i * 7919 };
        int found = pin_map_find(map, &key);
        REQUIRE(i % 2 ? found == i / 2 && map->entries[found].value.i == i : found < 0);
    }
    pin_value missing = { .type = PIN_I32, .i = -1 };
    REQUIRE(pin_map_find(map, &missing) < 0);
    REQUIRE(pin_map_equal(map, pin_map_snapshot_to_arena(&storage, map)));

    pin_map *names = pin_map_create(
        &storage,
        (pin_value){ .type = PIN_STR },
        (pin_value){ .type = PIN_I32 },
        NULL,
        0
    );
    char text[32];
    for (int i = 0; i < 200; i++) {
        snprintf(text, sizeof text, "key %d", i);
        pin_value key = { .type = PIN_STR,
                          .text = &(
                              const pin_string
                          ){ .bytes = text, .length = (uint32_t)strlen(text) } },
                  value = { .type = PIN_I32, .i = i };
        REQUIRE(!pin_map_store_owned(&storage, &names, &key, &value));
    }
    snprintf(text, sizeof text, "key %d", 137);
    pin_value named = {
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = text, .length = (uint32_t)strlen(text) }
    };
    REQUIRE(pin_map_find(names, &named) == 137);

    pin_map *floats = pin_map_create(
        &storage,
        (pin_value){ .type = PIN_F64 },
        (pin_value){ .type = PIN_I32 },
        NULL,
        0
    );
    for (int i = 0; i < 20; i++) {
        pin_value key = { .type = PIN_F64, .d = i ? i * 0.5 : -0.0 },
                  value = { .type = PIN_I32, .i = i };
        REQUIRE(!pin_map_store_owned(&storage, &floats, &key, &value));
    }
    pin_value zero = { .type = PIN_F64, .d = 0.0 };
    REQUIRE(floats->slots && pin_map_find(floats, &zero) == 0);
    arena_free(&storage);
    return true;
}
/* `m[k] = v` and `delete m[k]` on a local update it in place, so a
 * few thousand stores stay within a small budget; a copy keeps its values,
 * and the debugger still watches a removal's result. */
TEST_CASE(maps_local_updates_in_place)
{
    const char *simple = "Maps :: behavior { result: int = 0; on Run :: () { "
                         "squares: [int: int] = [:]; "
                         "for i in 0..<4000 { squares[i] = i * i; } "
                         "copy := squares; "
                         "for i in 0..<4000 { if i % 2 == 0 { "
                         "squares.delete(i); } } "
                         "squares[3] = 0; "
                         "total := 0; for key, value in squares { total += "
                         "value % 1000; } "
                         "if squares.has(4) { total += 1000000; } "
                         "result = total + copy[3] * 10000000; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    int expected = 9 * 10000000;
    for (int i = 1; i < 4000; i += 2) {
        expected += i == 3 ? 0 : (i * i) % 1000;
    }
    for (int b = 0; b < 2; b++) {
        REQUIRE(compile(simple, backends[b]));
        bool stores = false, deletes = false, watched = false;
        int removal = (int)(strstr(simple, "squares.delete(i)") - simple);
        for (int f = 0; f < execution_module_function_count(module); f++) {
            const behavior_function *body = execution_module_function_at(module, f)
                                                .body;
            for (int h = 0; body && h < acount(body->host_calls); h++) {
                uint64_t id = body->host_calls[h].capability_id;
                stores |= id == execution_intrinsic_id(&EXECUTION_reference_write_call);
                deletes |= id == execution_intrinsic_id(&EXECUTION_map_delete_place_call);
            }
            for (int w = 0; body && w < acount(body->source_watches); w++) {
                watched |= body->source_watches[w].origin.start == removal &&
                           body->source_watches[w].output;
            }
        }
        /* The debugger still watches the removal's result. */
        REQUIRE(stores && deletes && watched);
        execution_instance *instance = attach();
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, event.id, NULL, 0, 0, 0, 2000000) == FLOW_OK);
        const pin_value *result = state(instance, "result");
        REQUIRE(result && result->i == expected);
        cleanup();
    }
    return true;
}

TEST_CASE(maps_infer_literals_and_compose_with_nested_maps_and_arrays)
{
    const char *
        source = "Nested :: behavior { result: int = 0; "
                 "outer: [string: [string: int]] = [\"inside\": "
                 "[\"count\": 2]]; "
                 "rows: [string: int[]] = [\"values\": [3, 4]]; "
                 "empty: [int: string] = [:]; "
                 "on Run :: () { inferred := [\"one\": 1]; all := "
                 "[\"two\": 2]; "
                 "outer[\"inside\"][\"count\"] += 5; rows[\"values\"][1] = 9; "
                 "result = outer[\"inside\"][\"count\"] + rows[\"values\"][1]; "
                 "if \"one\" in inferred && all.has(\"two\") { "
                 "result += 1; } "
                 "a: [string: int] = [\"a\": 1, \"b\": 2]; "
                 "b: [string: int] = [\"b\": 2, \"a\": 1]; "
                 "if a == b { result += 1; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = attach();
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, event.id, NULL, 0, 0, 0, 10000) == FLOW_OK);
        const pin_value *value = state(instance, "result");
        REQUIRE(value && value->i == 18);
        value = state(instance, "empty");
        REQUIRE(value && value->container == PIN_CONTAINER_MAP && value->ptr);
        REQUIRE(((pin_map *)value->ptr)->count == 0);
        cleanup();
    }
    return true;
}

/* An absent key reads the value type's zero, as Odin's maps do: zero, an
 * empty string or container, or nil. The read adds no key. */
TEST_CASE(maps_absent_keys_read_zero_values)
{
    const char *source = "Zero :: behavior { result: int = 0; "
                         "counts: [string: int] = [\"Ada\": 2]; "
                         "on Run :: () { names: [int: string] = [:]; rows: "
                         "[string: int[]] = [:]; "
                         "nested: [string: [string: int]] = [:]; maybe: [int: "
                         "int?] = [:]; "
                         "counts[\"Bea\"] += 3; "
                         "result = counts[\"Ada\"] * 1000 + counts[\"Bea\"] * "
                         "100 + counts[\"Cid\"]; "
                         "if names[1] == \"\" { result += 10000; } "
                         "for row in rows[\"none\"] { result -= 1; } "
                         "if nested[\"a\"][\"b\"] == 0 && \"a\" not in nested "
                         "{ result += 100000; } "
                         "if maybe[3] == nil { result += 1000000; } "
                         "if \"Cid\" not in counts && !counts.has(\"Cid\") { "
                         "result += 10000000; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = attach();
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, event.id, NULL, 0, 0, 0, 10000) == FLOW_OK);
        const pin_value *result = state(instance, "result");
        REQUIRE(result && result->i == 11112300);
        const pin_value *counts = state(instance, "counts");
        REQUIRE(counts && ((pin_map *)counts->ptr)->count == 2);
        cleanup();
    }
    return true;
}

/* A record's zero is its field defaults. One with a required field has no
 * zero value, so reading an absent key of a map of them stays checked. */
TEST_CASE(maps_absent_record_keys_read_defaults_or_report_missing_keys)
{
    const char *library = "Pairs :: library { Pair :: struct { left: int = 4; "
                          "right: int = 5; } "
                          "Point :: struct { x: int; } }";
    const char *defaults = "Zero :: behavior { result: int = 0; on Run :: () { "
                           "pairs: [int: Pairs.Pair] = [:]; "
                           "result = pairs[7].left * 10 + pairs[7].right; } }";
    const char *required = "Missing :: behavior { result: int = 0; "
                           "points: [int: Pairs.Point] = [1: {x: 2}]; "
                           "on Run :: () { result = points[1].x; result += "
                           "points[99].x; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(compile_with_records(library, defaults, backends[b]));
        execution_instance *instance = attach();
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, event.id, NULL, 0, 0, 0, 10000) == FLOW_OK);
        REQUIRE(state(instance, "result") && state(instance, "result")->i == 45);
        cleanup();
        REQUIRE(compile_with_records(library, required, backends[b]));
        instance = attach();
        REQUIRE(instance);
        REQUIRE(
            execution_instance_dispatch(instance, event.id, NULL, 0, 0, 0, 10000) == FLOW_ERR_INVALID_GRAPH
        );
        const execution_program_failure *failure = execution_program_error(program);
        REQUIRE(failure && failure->status == FLOW_ERR_INVALID_GRAPH);
        REQUIRE(failure->kind == BEHAVIOR_FUNCTION_HOST_DATA);
        REQUIRE(strstr(failure->message, "Map key is absent"));
        REQUIRE(state(instance, "result") && state(instance, "result")->i == 2);
        cleanup();
    }
    return true;
}

/* `a ?? b` keeps a present value and evaluates b only for nil; `??` groups
 * to the right and binds below arithmetic, above comparison. */
TEST_CASE(maps_and_optionals_coalesce_nil_to_fallbacks)
{
    const char
        *source = "Coalesce :: behavior { result: int = 0; calls: int = 0; "
                  "flag: bool = false; "
                  "Fallback :: () -> int { calls += 1; return 7; } "
                  "on Run :: () { some: int? = 3; none: int? = nil; name: "
                  "string? = nil; "
                  "maybe: [string: int?] = [\"a\": 4, \"b\": nil]; "
                  "result = some ?? Fallback(); "
                  "result = result * 10 + (none ?? Fallback()); "
                  "result = result * 10 + (maybe[\"a\"] ?? 0); "
                  "result = result * 10 + (maybe[\"b\"] ?? maybe[\"c\"] ?? 5); "
                  "kept: int? = none ?? maybe[\"a\"]; result = result * 10 + "
                  "(kept ?? 0); "
                  "result = result * 10 + (none ?? 1 + 1); "
                  "flag = none ?? 1 < 2 && (name ?? \"x\") == \"x\"; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = attach();
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, event.id, NULL, 0, 0, 0, 10000) == FLOW_OK);
        REQUIRE(state(instance, "result") && state(instance, "result")->i == 374542);
        REQUIRE(state(instance, "calls") && state(instance, "calls")->i == 1);
        REQUIRE(state(instance, "flag") && state(instance, "flag")->b);
        cleanup();
    }
    return true;
}

TEST_CASE(maps_reject_missing_context_and_incompatible_key_value_types)
{
    const char *invalid[] = {
        "Bad :: behavior { m := [:]; }",
        "Bad :: behavior { m: [int: int] = [\"wrong\": 1]; }",
        "Bad :: behavior { m: [string: int] = [\"wrong\": true]; }",
        "Bad :: behavior { m: [int[]: int] = [:]; }",
        "Bad :: behavior { m: [int: int] = [:]; on Run :: () { key: number = "
        "1; "
        "m[key] = 2; } }",
        ("Bad :: behavior { m: [string: int] = [:]; n: [string: bool] = [:]; "
         "on Run :: () { m = n; } }"),
        "Bad :: behavior { m: [int: int] = [:]; on Run :: () { ok := "
        "m.delete(1); } }",
        "Bad :: behavior { m: [int: int] = [:]; on Run :: () { ok := \"a\" in "
        "m; } }",
        "Bad :: behavior { m: int = 0; on Run :: () { delete m[1]; } }",
        "Bad :: behavior { on Run :: () { [1: 2].delete(1); } }",
        "Bad :: behavior { m: [int: int] = [:]; on Run :: () { ok := m.has(1, "
        "2); } }",
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        syntax = script_parse(invalid[i]);
        REQUIRE(!syntax->diagnostic_count);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(!analysis->valid && analysis->diagnostic_count);
        cleanup();
    }
    return true;
}

TEST_CASE(maps_own_strings_compare_associations_and_reject_nonfinite_keys)
{
    char key[] = "first", text[] = "owned";
    pin_map_entry entries[] = {
        { { .type = PIN_STR,
            .text = &(const pin_string){ .bytes = key, .length = (uint32_t)strlen(key) } },
          { .type = PIN_STR,
            .text = &(const pin_string){ .bytes = text,
                                         .length = (uint32_t)strlen(text) } } },
        { { .type = PIN_STR,
            .text = &(const pin_string){ .bytes = "second",
                                         .length = sizeof("second") - 1 } },
          { .type = PIN_STR,
            .text = &(const pin_string){ .bytes = "two", .length = sizeof("two") - 1 } } },
    };
    pin_value strings = { .type = PIN_STR };
    pin_map *map = pin_map_create(&storage, strings, strings, entries, 2);
    REQUIRE(map && map->count == 2);
    key[0] = 'X';
    text[0] = 'X';
    REQUIRE(!strcmp(pin_text(map->entries[0].key).bytes, "first"));
    REQUIRE(!strcmp(pin_text(map->entries[0].value).bytes, "owned"));
    pin_map_entry reversed[] = { map->entries[1], map->entries[0] };
    pin_map *other = pin_map_create(&storage, strings, strings, reversed, 2);
    REQUIRE(other && pin_map_equal(map, other));
    pin_value numeric = { .type = PIN_F64 }, nan = { .type = PIN_F64, .d = NAN };
    pin_map_entry bad = { nan, strings };
    REQUIRE(!pin_map_create(&storage, numeric, strings, &bad, 1));
    REQUIRE(!pin_map_key_supported((pin_value){ .type = PIN_F64, .d = INFINITY }));
    return true;
}

TEST_CASE(maps_wire_preserves_nested_types_and_rejects_truncated_payloads)
{
    pin_value strings = { .type = PIN_STR }, ints = { .type = PIN_I32 };
    pin_map_entry inner_entry = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "count", .length = sizeof("count") - 1 } },
        { .type = PIN_I32, .i = 42 }
    };
    pin_map *inner = pin_map_create(&storage, strings, ints, &inner_entry, 1);
    REQUIRE(inner);
    pin_value inner_value = { .type = PIN_UNION,
                              .container = PIN_CONTAINER_MAP,
                              .type_name = "Map_string_int",
                              .ptr = inner };
    pin_map_entry entry = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "inside",
                                       .length = sizeof("inside") - 1 } },
        inner_value
    };
    pin_map *outer = pin_map_create(&storage, strings, inner_value, &entry, 1);
    REQUIRE(outer);
    pin_value source = { .type = PIN_UNION,
                         .container = PIN_CONTAINER_MAP,
                         .type_name = "Map_string_Map_string_int",
                         .ptr = outer };
    execution_wire writer = { 0 };
    execution_wire_value(&writer, &source);
    bool valid = !writer.error && writer.size > 0;
    arena decoded = { 0 };
    execution_wire reader = { .data = writer.data,
                              .size = writer.size,
                              .reading = true,
                              .storage = &decoded };
    pin_value output = { 0 };
    execution_wire_value(&reader, &output);
    valid = valid && !reader.error && reader.offset == reader.size &&
            output.container == PIN_CONTAINER_MAP &&
            pin_value_equal(&source, &output);
    const pin_map *map = output.ptr;
    valid = valid && map && map->count == 1 &&
            map->entries[0].value.container == PIN_CONTAINER_MAP;
    if (valid) {
        const pin_map *nested = map->entries[0].value.ptr;
        valid = nested && nested->count == 1 &&
                nested->entries[0].value.type == PIN_I32 &&
                nested->entries[0].value.i == 42;
    }
    execution_wire short_reader = { .data = writer.data,
                                    .size = writer.size - 1,
                                    .reading = true,
                                    .storage = &decoded };
    pin_value rejected = { 0 };
    execution_wire_value(&short_reader, &rejected);
    bool rejected_truncation = short_reader.error != NULL;
    arena_free(&decoded);
    afree(writer.data);
    REQUIRE(valid);
    REQUIRE(rejected_truncation);
    return true;
}

/* A decoded map's index counts against the reader's storage limit. */
TEST_CASE(maps_wire_charges_the_index_to_the_storage_limit)
{
    pin_map_entry entries[16];
    for (int i = 0; i < 16; i++) {
        entries[i] = (pin_map_entry){ { .type = PIN_I32, .i = i },
                                      { .type = PIN_I32, .i = i * i } };
    }
    pin_value ints = { .type = PIN_I32 };
    pin_value source = { .type = PIN_UNION,
                         .container = PIN_CONTAINER_MAP,
                         .type_name = "Map_int_int",
                         .ptr = pin_map_create(&storage, ints, ints, entries, 16) };
    execution_wire writer = { 0 };
    execution_wire_value(&writer, &source);
    arena decoded = { 0 };
    execution_wire reader = { .data = writer.data,
                              .size = writer.size,
                              .reading = true,
                              .storage = &decoded };
    pin_value output = { 0 };
    execution_wire_value(&reader, &output);
    size_t needed = sizeof("Map_int_int") + sizeof(pin_map) +
                    16 * sizeof(pin_map_entry) + pin_map_index_bytes(16);
    bool charged = !reader.error && ((const pin_map *)output.ptr)->slots &&
                   reader.owned_bytes == needed;
    execution_wire limited = { .data = writer.data,
                               .size = writer.size,
                               .reading = true,
                               .storage = &decoded,
                               .owned_limit = needed - 1 };
    execution_wire_value(&limited, &output);
    bool rejected = limited.error != NULL;
    arena_free(&decoded);
    afree(writer.data);
    REQUIRE(pin_map_index_bytes(16) > 0 && charged && rejected);
    return true;
}

TEST_CASE(maps_source_literal_round_trips_typed_keys_and_values)
{
    REQUIRE(
        compile("Literal :: behavior { values: [string: int] = [\"one\": 1, \"two\": 2]; }", BEHAVIOR_BYTECODE)
    );
    execution_instance *instance = attach();
    REQUIRE(instance);
    const pin_value *value = state(instance, "values");
    REQUIRE(value && value->container == PIN_CONTAINER_MAP);
    script_type type = { 0 };
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (!strcmp(analysis->symbols[i].name, "values")) {
            type = analysis->symbols[i].type;
        }
    }
    script_catalog owned = catalog;
    owned.types = analysis->types;
    owned.type_count = analysis->type_count;
    char error[192];
    char *literal = authoring_source_literal(&owned, type, value, error, sizeof error);
    REQUIRE(literal);
    char source[512];
    snprintf(source, sizeof source, "RoundTrip :: behavior { values: [string: int] = %s; }", literal);
    free(literal);
    script_syntax *parsed = script_parse(source);
    script_analysis *checked = script_analyze(parsed, &owned);
    bool valid = !parsed->diagnostic_count && checked->valid &&
                 !checked->diagnostic_count;
    script_analysis_free(checked);
    script_syntax_free(parsed);
    REQUIRE(valid);
    return true;
}

TEST_CASE(maps_retain_nested_task_results_until_the_last_handle_is_removed)
{
    const char
        *source = "Maps :: behavior { seen: f64 = 0; "
                  "jobs: [string: [string: task(f64)]] = [:]; "
                  "Work :: () -> f64 { yield; return 7; } "
                  "on Run :: () { jobs = [\"outer\": [\"inner\": Work()]]; "
                  "yield; yield; answer := await jobs[\"outer\"][\"inner\"]; "
                  "if answer is f64 value { seen = value; } jobs = [:]; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = attach();
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, event.id, NULL, 0, 0, 0, 10000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 10000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).task_result_bytes > 0);
        REQUIRE(execution_program_update(program, 2, 0, 10000) == FLOW_OK);
        const pin_value *seen = state(instance, "seen");
        REQUIRE(seen && seen->type == PIN_F64 && seen->d == 7);
        REQUIRE(!execution_instance_pending(instance));
        REQUIRE(!execution_program_memory_usage(program).task_result_bytes);
        cleanup();
    }
    return true;
}

static const pin_array *inspection_nested_array(const pin_value *value)
{
    if (!value || value->container != PIN_CONTAINER_MAP || !value->ptr) {
        return NULL;
    }
    const pin_map *outer = value->ptr;
    pin_value key = outer->key_type;
    key.text = &(const pin_string){ .bytes = "group",
                                    .length = (uint32_t)strlen("group") };
    int group = pin_map_find(outer, &key);
    if (group < 0 || outer->entries[group].value.container != PIN_CONTAINER_MAP) {
        return NULL;
    }
    const pin_map *inner = outer->entries[group].value.ptr;
    if (!inner) {
        return NULL;
    }
    key = inner->key_type;
    key.text = &(const pin_string){ .bytes = "values",
                                    .length = (uint32_t)strlen("values") };
    int values = pin_map_find(inner, &key);
    return values >= 0 && inner->entries[values].value.container == PIN_CONTAINER_ARRAY
             ? inner->entries[values].value.ptr
             : NULL;
}
TEST_CASE(maps_inspection_owns_nested_snapshots_after_mutation_and_program_release)
{
    const char *source = "Inspect :: behavior { seen:int=0; "
                         "values: [string: [string: int[]]] = [\"group\": "
                         "[\"values\": [1, 2]]]; "
                         "on Run :: () { saved:=values; "
                         "seen=saved[\"group\"][\"values\"][0]; "
                         "values[\"group\"][\"values\"][0]=9; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = attach();
        REQUIRE(instance);
        inspector = execution_program_inspector(program);
        REQUIRE(inspector);
        REQUIRE(execution_instance_dispatch(instance, event.id, NULL, 0, 0, 0, 10000) == FLOW_OK);
        const pin_array *live = inspection_nested_array(state(instance, "values"));
        REQUIRE(live && live->count == 2 && pin_array_get(live, 0).i == 9);
        int saved = -1;
        for (int i = 0; i < execution_inspector_watch_count(inspector); i++) {
            const execution_debug_watch *watch = execution_inspector_watch_at(inspector, i);
            const pin_array *array = watch->availability == EXECUTION_WATCH_AVAILABLE
                                       ? inspection_nested_array(&watch->value)
                                       : NULL;
            if (array && array->count == 2 && pin_array_get(array, 0).i == 1) {
                REQUIRE(array != live);
                saved = i;
                break;
            }
        }
        REQUIRE(saved >= 0);
        execution_program_free(program);
        program = NULL;
        const pin_array *snapshot = inspection_nested_array(
            &execution_inspector_watch_at(inspector, saved)->value
        );
        REQUIRE(snapshot && snapshot->count == 2);
        REQUIRE(pin_array_get(snapshot, 0).i == 1 && pin_array_get(snapshot, 1).i == 2);
        cleanup();
    }
    return true;
}
TEST_CASE(maps_intrinsics_reject_incompatible_result_banks)
{
    pin_map_entry entry = {
        .key = { .type = PIN_STR,
                 .text = &(const pin_string){ .bytes = "one",
                                              .length = sizeof("one") - 1 } },
        .value = { .type = PIN_I32, .i = 7 }
    };
    pin_map map = { .key_type = { .type = PIN_STR },
                    .value_type = { .type = PIN_I32 },
                    .count = 1,
                    .entries = &entry };
    pin_value args[] = { { .type = PIN_UNION, .container = PIN_CONTAINER_MAP, .ptr = &map },
                         entry.key,
                         { .type = PIN_BOOL } };
    pin_value out = { .type = PIN_STR };
    REQUIRE(EXECUTION_map_get_call.value(&storage, args, 2, &out));
    REQUIRE(out.type == PIN_STR && out.text == NULL);
    out = (pin_value){ .type = PIN_I32 };
    REQUIRE(!EXECUTION_map_get_call.value(&storage, args, 2, &out) && out.i == 7);
    args[1] = (pin_value){ .type = PIN_I32 };
    out = (pin_value){ .type = PIN_STR };
    REQUIRE(EXECUTION_map_item_call.value(&storage, args, 3, &out));
    args[2].b = true;
    out = (pin_value){ .type = PIN_I32 };
    REQUIRE(EXECUTION_map_item_call.value(&storage, args, 3, &out));
    out = (pin_value){ .type = PIN_STR };
    REQUIRE(!EXECUTION_map_item_call.value(&storage, args, 3, &out));
    REQUIRE(!strcmp(pin_text(out).bytes, "one"));
    behavior_function_context owner = { .value_storage = &storage };
    pin_value keyed[] = { execution_reference_value(&storage, &owner, &args[0], args[0]),
                          { .type = PIN_I64 },
                          entry.key };
    pin_value reference;
    REQUIRE(!EXECUTION_reference_key_call.value(&storage, keyed, 3, &reference));
    out = (pin_value){ .type = PIN_I64 };
    REQUIRE(EXECUTION_reference_read_call.value(&storage, &reference, 1, &out));
    return true;
}
void test_language_maps()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(maps_intrinsics_reject_incompatible_result_banks);
    RUN_TEST_CASE(maps_preserve_value_copies_and_snapshot_insertion_order);
    RUN_TEST_CASE(maps_index_finds_every_key_in_insertion_order);
    RUN_TEST_CASE(maps_local_updates_in_place);
    RUN_TEST_CASE(maps_infer_literals_and_compose_with_nested_maps_and_arrays);
    RUN_TEST_CASE(maps_absent_keys_read_zero_values);
    RUN_TEST_CASE(maps_absent_record_keys_read_defaults_or_report_missing_keys);
    RUN_TEST_CASE(maps_and_optionals_coalesce_nil_to_fallbacks);
    RUN_TEST_CASE(maps_reject_missing_context_and_incompatible_key_value_types);
    RUN_TEST_CASE(maps_own_strings_compare_associations_and_reject_nonfinite_keys);
    RUN_TEST_CASE(maps_wire_preserves_nested_types_and_rejects_truncated_payloads);
    RUN_TEST_CASE(maps_wire_charges_the_index_to_the_storage_limit);
    RUN_TEST_CASE(maps_source_literal_round_trips_typed_keys_and_values);
    RUN_TEST_CASE(maps_retain_nested_task_results_until_the_last_handle_is_removed);
    RUN_TEST_CASE(maps_inspection_owns_nested_snapshots_after_mutation_and_program_release);
    pu_clear_setup();
}
