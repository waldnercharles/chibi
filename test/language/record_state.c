#include "backend.h"
#include "capabilities/catalog_snapshot.h"
#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/inspection.h"
#include "execution/module.h"
#include "execution/status.h"
#include "execution/wasm_jit.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Three fixed cases: eight modules, sixteen analyses, eight dispatches, four
 * replacements, and one privately assembled frame-input reconstruction. */
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module, *next;
static execution_program *program;
static execution_inspector *inspector;
static ecs_world *world;
static arena catalog_storage, saved, producer;
static behavior_function frame;
static behavior_run_scope runs;
static uint64_t row_id, rows_id, nominal, layout;
static uint64_t expected_nominal[3], expected_layout[3];
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static const script_catalog_parameter text_parameter = { .name = "text",
                                                         .type = { .storage = PIN_STR } };
static const script_catalog_capability event = { .id = 71,
                                                 .name = "Set",
                                                 .kind = SCRIPT_CAPABILITY_EVENT,
                                                 .parameters = &text_parameter,
                                                 .parameter_count = 1 };
static const script_catalog empty_catalog;
static const script_lower_bindings bindings;
static const char
    behavior[] = "Store :: behavior { row: Rows.Row = {key: \"initial\", "
                 "label: \"initial\"}; rows: Rows.Row[] = []; on Set :: (text: "
                 "string) { "
                 "empty: Rows.Row[] = []; rows = empty; row = {key: text, "
                 "label: text}; rows = [row, row, row]; } }";
static void retire_source()
{
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
    arena_free(&catalog_storage);
}
static void retire_program()
{
    if (program) {
        execution_program_free(program);
        program = NULL;
    }
    execution_module_free(module);
    module = NULL;
    if (world) {
        ecs_world_free(world);
        world = NULL;
    }
}
static void cleanup()
{
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    retire_program();
    execution_module_free(next);
    next = NULL;
    if (inspector) {
        execution_inspector_free(inspector);
        inspector = NULL;
    }
    execution_module_plan_free(plan);
    plan = NULL;
    retire_source();
    afree(frame.parameters);
    afree(frame.locals);
    frame = (behavior_function){ 0 };
    arena_free(&saved);
    arena_free(&producer);
}
static bool analyze(const char *source, const script_catalog *catalog)
{
    syntax = script_parse(source);
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze(syntax, catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "record state semantic: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid);
    return true;
}
static bool compile(int generation, behavior_backend backend, execution_module **out)
{
    char source[768];
    int length = snprintf(
        source,
        sizeof source,
        "Rows :: library { Row :: struct { key: string; label: string; "
        "icon: string = \"%s\"; prototype: string = \"\"; drop: string = \"\"; "
        "%s: int = %d; behaviors: int = 0; active: bool = true; "
        "marked: bool = false; hidden: bool = false; locked: bool = false; "
        "renaming: bool = false; } }",
        generation ? "new-icon" : "node",
        generation == 2 ? "level" : "depth",
        generation + 1
    );
    REQUIRE(length > 0 && (size_t)length < sizeof source);
    REQUIRE(analyze(source, &empty_catalog));
    REQUIRE(analysis->type_count == 1 && analysis->types[0].authored_record);
    nominal = analysis->types[0].id;
    layout = analysis->types[0].layout;
    expected_nominal[generation] = nominal;
    expected_layout[generation] = layout;
    script_catalog source_catalog = { .types = analysis->types,
                                      .type_count = 1,
                                      .capabilities = &event,
                                      .capability_count = 1 },
                   catalog;
    REQUIRE(script_catalog_snapshot(&catalog_storage, &source_catalog, &catalog));
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
    REQUIRE(analyze(behavior, &catalog));
    row_id = rows_id = 0;
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *s = &analysis->symbols[i];
        if (s->kind == SCRIPT_STATE && !strcmp(s->name, "row")) {
            row_id = s->id;
        }
        if (s->kind == SCRIPT_STATE && !strcmp(s->name, "rows")) {
            rows_id = s->id;
        }
    }
    REQUIRE(row_id && rows_id && row_id != rows_id);
    plan = script_lower(analysis, &catalog, &bindings);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "record state lower: %s\n", plan->diagnostics[i].message);
    }
    REQUIRE(plan && plan->valid);
    char error[256];
    *out = execution_module_finish(&plan, backend, error, sizeof error);
    if (!*out) {
        fprintf(stderr, "record state module: %s\n", error);
    }
    REQUIRE(*out && execution_module_backend(*out) == backend);
    retire_source();
    return true;
}
static bool start(uint64_t *owner)
{
    world = ecs_world_new();
    REQUIRE(world);
    *owner = ecs_entity_to_u64(ecs_world_create_entity(world));
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    program = execution_program_new(module, world, NULL);
    REQUIRE(program && execution_program_attach(program, *owner));
    return true;
}
static bool row(const pin_value *v, const char *text, int generation)
{
    REQUIRE(v && v->type == PIN_STRUCT && v->container == PIN_CONTAINER_SINGLE && v->ptr);
    REQUIRE(pin_record_valid(v->record) && v->record->type.field_count == 12);
    REQUIRE(v->type_name == v->record->type.name && !strcmp(v->type_name, "Rows.Row"));
    REQUIRE(v->record->id == expected_nominal[generation]);
    REQUIRE(v->record->layout == expected_layout[generation]);
    REQUIRE(!strcmp(v->record->type.fields[5].name, generation == 2 ? "level" : "depth"));
    for (int i = 0; i < 12; i++) {
        pin_value field;
        REQUIRE(!pin_read_field(&producer, v->ptr, &v->record->type.fields[i], &field));
        if (i < 5) {
            const char *expected = i < 2  ? text
                                 : i == 2 ? (generation ? "new-icon" : "node")
                                          : "";
            REQUIRE(
                field.type == PIN_STR && pin_text(field).bytes &&
                !strcmp(pin_text(field).bytes, expected)
            );
        } else if (i < 7) {
            REQUIRE(field.type == PIN_I32 && field.i == (i == 5 ? generation + 1 : 0));
        } else {
            REQUIRE(field.type == PIN_BOOL && field.b == (i == 7));
        }
    }
    return true;
}
static bool rows(const pin_value *v, int count, const char *text, int generation)
{
    REQUIRE(v && v->type == PIN_STRUCT && v->container == PIN_CONTAINER_ARRAY);
    REQUIRE(pin_record_valid(v->record) && v->type_name == v->record->type.name);
    REQUIRE(!strcmp(v->type_name, "Rows.Row"));
    REQUIRE(v->record->id == expected_nominal[generation]);
    REQUIRE(v->record->layout == expected_layout[generation]);
    const pin_array *a = v->ptr;
    if (!a) {
        REQUIRE(count == 0);
        return true;
    }
    REQUIRE(a->element_type == PIN_STRUCT && a->record == v->record);
    REQUIRE(a->type_name == v->type_name && a->count == count);
    for (int i = 0; i < count; i++) {
        pin_value item = pin_array_get(a, i);
        REQUIRE(item.record == v->record && row(&item, text, generation));
    }
    return true;
}
static bool state(execution_instance *instance, const char *text, int generation, int count)
{
    REQUIRE(instance);
    REQUIRE(row(execution_instance_state(instance, row_id), text, generation));
    REQUIRE(rows(execution_instance_state(instance, rows_id), count, text, generation));
    return true;
}
static bool dispatch(execution_instance *instance, behavior_backend backend, char *text, bool inspected)
{
    behavior_run_scope_reset(&runs);
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
    pin_value argument = {
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = text, .length = (uint32_t)strlen(text) }
    };
    behavior_run_scope_begin(&runs);
    int status = execution_instance_dispatch(instance, event.id, &argument, 1, 0, 0, 10000);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK) {
        const execution_program_failure *failure = execution_program_error(program);
        fprintf(
            stderr,
            "record state dispatch: %d %s\n",
            status,
            failure ? failure->message : ""
        );
    }
    REQUIRE(status == FLOW_OK && !execution_instance_pending(instance));
    REQUIRE(!runs.counts.overflow);
    if (backend == BEHAVIOR_BYTECODE) {
        REQUIRE(
            runs.counts.bytecode_entries && !runs.counts.sljit_entries &&
            !runs.counts.wasm_entries
        );
    } else if (backend == BEHAVIOR_SLJIT) {
        REQUIRE(!runs.counts.wasm_entries);
        REQUIRE(
            runs.counts.sljit_entries ||
            (inspected && runs.counts.inspection_bytecode_fallbacks &&
             runs.counts.bytecode_entries)
        );
        REQUIRE(inspected || !runs.counts.bytecode_entries);
        REQUIRE(
            !inspected || runs.counts.inspected_sljit_entries || runs.counts.inspection_bytecode_fallbacks
        );
    } else {
        REQUIRE(
            runs.counts.wasm_entries && !runs.counts.sljit_entries &&
            !runs.counts.bytecode_entries
        );
        REQUIRE(!inspected || runs.counts.inspected_wasm_entries);
    }
    REQUIRE(inspected || !runs.counts.inspection_bytecode_fallbacks);
    execution_wasm_jit_get_stats(&after);
    REQUIRE(
        backend != BEHAVIOR_WASM || (after.executions > before.executions &&
                                     after.compile_failures == before.compile_failures)
    );
    fprintf(
        stderr,
        "record state entries: inspected=%d bytecode=%llu sljit=%llu wasm=%llu "
        "fallback=%llu\n",
        inspected,
        (unsigned long long)runs.counts.bytecode_entries,
        (unsigned long long)runs.counts.sljit_entries,
        (unsigned long long)runs.counts.wasm_entries,
        (unsigned long long)runs.counts.inspection_bytecode_fallbacks
    );
    memset(text, 'x', strlen(text));
    return true;
}
TEST_CASE(record_state_replacement)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(0, backends[b], &module));
        uint64_t owner, initial_row = row_id, initial_rows = rows_id;
        uint64_t first_nominal = nominal, first_layout = layout;
        REQUIRE(start(&owner));
        execution_instance *instance = execution_program_instance(program, owner);
        REQUIRE(state(instance, "initial", 0, 0));
        char first[] = "first";
        REQUIRE(dispatch(instance, backends[b], first, false));
        REQUIRE(state(instance, "first", 0, 3));
        pin_value detached_row, detached_rows;
        REQUIRE(
            !pin_value_snapshot_to_arena(&saved, &detached_row, execution_instance_state(instance, row_id))
        );
        REQUIRE(
            !pin_value_snapshot_to_arena(&saved, &detached_rows, execution_instance_state(instance, rows_id))
        );
        REQUIRE(compile(1, backends[b], &next));
        REQUIRE(row_id == initial_row && rows_id == initial_rows);
        REQUIRE(nominal == first_nominal && layout == first_layout);
        REQUIRE(execution_program_replace(program, next, false) == FLOW_OK);
        execution_module_free(module);
        module = next;
        next = NULL;
        instance = execution_program_instance(program, owner);
        REQUIRE(state(instance, "first", 0, 3));
        char second[] = "second";
        REQUIRE(dispatch(instance, backends[b], second, false));
        REQUIRE(state(instance, "second", 1, 3));
        REQUIRE(compile(2, backends[b], &next));
        REQUIRE(row_id == initial_row && rows_id == initial_rows);
        REQUIRE(nominal == first_nominal && layout != first_layout);
        REQUIRE(execution_program_replace(program, next, false) == FLOW_ERR_COMPILE);
        REQUIRE(state(execution_program_instance(program, owner), "second", 1, 3));
        REQUIRE(execution_program_replace(program, next, true) == FLOW_OK);
        execution_module_free(module);
        module = next;
        next = NULL;
        instance = execution_program_instance(program, owner);
        REQUIRE(state(instance, "initial", 2, 0));
        char third[] = "third";
        REQUIRE(dispatch(instance, backends[b], third, false));
        REQUIRE(state(instance, "third", 2, 3));
        retire_program();
        REQUIRE(row(&detached_row, "first", 0) && rows(&detached_rows, 3, "first", 0));
    }
    return true;
}
TEST_CASE(record_state_owned_watches)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(0, backends[b], &module));
        uint64_t owner;
        REQUIRE(start(&owner));
        execution_instance *instance = execution_program_instance(program, owner);
        inspector = execution_program_inspector(program);
        REQUIRE(inspector && execution_inspector_select_owner(inspector, owner));
        char text[] = "watched";
        REQUIRE(dispatch(instance, backends[b], text, true));
        REQUIRE(state(instance, "watched", 0, 3));
        int scalar = -1, three = -1, zero = -1;
        int count = execution_inspector_watch_count(inspector);
        REQUIRE(count > 0 && count <= 512);
        for (int i = 0; i < count; i++) {
            const execution_debug_watch *w = execution_inspector_watch_at(inspector, i);
            if (!w->value.record) {
                continue;
            }
            REQUIRE(pin_record_valid(w->value.record));
            if (w->source.reference) {
                REQUIRE(w->availability == EXECUTION_WATCH_UNCONSUMED_REFERENCE);
                continue;
            }
            REQUIRE(w->availability == EXECUTION_WATCH_AVAILABLE);
            if (w->value.container == PIN_CONTAINER_SINGLE && w->value.ptr) {
                pin_value field;
                {
                    const char *error = pin_read_field(
                        &producer,
                        w->value.ptr,
                        &w->value.record->type.fields[0],
                        &field
                    );
                    if (error) {
                        abort();
                    }
                };
                if (pin_text(field).bytes && !strcmp(pin_text(field).bytes, "watched")) {
                    REQUIRE(row(&w->value, "watched", 0));
                    scalar = i;
                }
            } else if (w->value.container == PIN_CONTAINER_ARRAY && w->value.ptr) {
                const pin_array *a = w->value.ptr;
                REQUIRE(a->count == 0 || a->count == 3);
                REQUIRE(rows(&w->value, a->count, "watched", 0));
                if (a->count) {
                    three = i;
                } else {
                    zero = i;
                }
            }
        }
        REQUIRE(scalar >= 0 && three >= 0 && zero >= 0);
        pin_value changed;
        REQUIRE(
            !pin_value_snapshot_to_arena(&producer, &changed, execution_instance_state(instance, row_id))
        );
        pin_value replacement = { .type = PIN_STR,
                                  .text = &(const pin_string){
                                      .bytes = "replaced",
                                      .length = sizeof("replaced") - 1 } };
        {
            const char *error = pin_write_field(
                &producer,
                changed.ptr,
                &changed.record->type.fields[0],
                replacement
            );
            if (error) {
                abort();
            }
        };
        {
            const char *error = pin_write_field(
                &producer,
                changed.ptr,
                &changed.record->type.fields[1],
                replacement
            );
            if (error) {
                abort();
            }
        };
        REQUIRE(execution_instance_set_state(instance, row_id, changed) == FLOW_OK);
        pin_value cleared = *execution_instance_state(instance, rows_id);
        pin_array array = { .element_type = PIN_STRUCT,
                            .type_name = cleared.type_name,
                            .record = cleared.record };
        cleared.ptr = &array;
        REQUIRE(execution_instance_set_state(instance, rows_id, cleared) == FLOW_OK);
        REQUIRE(state(instance, "replaced", 0, 0));
        arena_free(&producer);
        retire_program();
        REQUIRE(execution_inspector_cursor(inspector).retired);
        REQUIRE(row(&execution_inspector_watch_at(inspector, scalar)->value, "watched", 0));
        REQUIRE(rows(&execution_inspector_watch_at(inspector, three)->value, 3, "watched", 0));
        REQUIRE(rows(&execution_inspector_watch_at(inspector, zero)->value, 0, "watched", 0));
        REQUIRE(execution_inspector_watch_at(inspector, zero)->value.ptr);
    }
    return true;
}
static const type_field frame_fields[] = { { .name = "key", .type = PIN_STR },
                                           { .name = "label", .type = PIN_STR },
                                           { .name = "icon", .type = PIN_STR },
                                           { .name = "prototype", .type = PIN_STR },
                                           { .name = "drop", .type = PIN_STR },
                                           { .name = "depth", .type = PIN_I32 },
                                           { .name = "behaviors", .type = PIN_I32 },
                                           { .name = "active", .type = PIN_BOOL },
                                           { .name = "marked", .type = PIN_BOOL },
                                           { .name = "hidden", .type = PIN_BOOL },
                                           { .name = "locked", .type = PIN_BOOL },
                                           { .name = "renaming", .type = PIN_BOOL } };
static pin_value frame_row(const pin_record *record)
{
    pin_value v = { .type = PIN_STRUCT,
                    .record = record,
                    .type_name = record->type.name,
                    .ptr = arena_alloc(&producer, record->type.size) };
    memset(v.ptr, 0, record->type.size);
    for (int i = 0; i < 12; i++) {
        pin_value f = { .type = record->type.fields[i].type };
        if (i < 5) {
            const char *text = i < 2 ? "frame" : i == 2 ? "node" : "";
            char *copy = arena_alloc(&producer, strlen(text) + 1);
            strcpy(copy, text);
            const char *error = pin_value_text(&producer, &f, copy, strlen(copy));
            if (error) {
                abort();
            }
        } else if (i < 7) {
            f.i = i == 5;
        } else {
            f.b = i == 7;
        }
        {
            const char *error = pin_write_field(&producer, v.ptr, &record->type.fields[i], f);
            if (error) {
                abort();
            }
        };
    }
    return v;
}
static bool owned(const pin_value *v)
{
    REQUIRE(arena_contains(&saved, v->record));
    if (v->container == PIN_CONTAINER_ARRAY) {
        const pin_array *a = v->ptr;
        if (a) {
            REQUIRE(arena_contains(&saved, a));
            for (int i = 0; i < a->count; i++) {
                pin_value item = pin_array_get(a, i);
                REQUIRE(owned(&item));
            }
        }
        return true;
    }
    REQUIRE(arena_contains(&saved, v->ptr));
    for (int i = 0; i < 5; i++) {
        pin_value field;
        REQUIRE(!pin_read_field(&producer, v->ptr, &v->record->type.fields[i], &field));
        REQUIRE(arena_contains(&saved, pin_text(field).bytes));
    }
    return true;
}
TEST_CASE(record_state_frame_inputs)
{
    cleanup();
    const pin_record *record = pin_record_create(&producer, "Rows.Row", 91, frame_fields, 12);
    REQUIRE(record);
    expected_nominal[0] = record->id;
    expected_layout[0] = record->layout;
    pin_value scalar = frame_row(record), items[3];
    for (int i = 0; i < 3; i++) {
        items[i] = frame_row(record);
    }
    pin_array array = { .element_type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record,
                        .count = 3,
                        .capacity = 3,
                        .items = items };
    pin_array empty = { .element_type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record };
    pin_value input[4] = { scalar,
                           { .type = PIN_STRUCT,
                             .container = PIN_CONTAINER_ARRAY,
                             .record = record,
                             .type_name = record->type.name,
                             .ptr = &array },
                           { .type = PIN_STRUCT,
                             .container = PIN_CONTAINER_ARRAY,
                             .record = record,
                             .type_name = record->type.name },
                           { .type = PIN_STRUCT,
                             .container = PIN_CONTAINER_ARRAY,
                             .record = record,
                             .type_name = record->type.name,
                             .ptr = &empty } };
    uint64_t bits[15] = { 0 };
    for (int i = 0; i < 4; i++) {
        apush(
            frame.parameters,
            ((behavior_function_parameter){ .initial = input[i], .frame = 3, .pin = i })
        );
    }
    apush(
        frame.parameters,
        ((behavior_function_parameter){ .initial = { .type = PIN_I32 }, .frame = 4, .pin = 0 })
    );
    for (int i = 0; i < 12; i++) {
        pin_value field;
        {
            const char *
                error = pin_read_field(&producer, scalar.ptr, &record->type.fields[i], &field);
            if (error) {
                abort();
            }
        };
        memcpy(&bits[i], &field.d, behavior_scalar_size(field.type));
        apush(
            frame.locals,
            ((behavior_function_local){ .initial = { .type = field.type },
                                        .frame = 3,
                                        .pin = 0,
                                        .offset = record->type.fields[i].offset,
                                        .parameter = true })
        );
    }
    for (int i = 1; i < 4; i++) {
        memcpy(&bits[11 + i], &input[i].ptr, sizeof input[i].ptr);
        apush(
            frame.locals,
            ((behavior_function_local){ .initial = { .type = BF_POINTER_TYPE },
                                        .frame = 3,
                                        .pin = i,
                                        .parameter = true })
        );
    }
    behavior_function_activation activation = { .values = bits };
    int count = -1;
    const pin_value *values = behavior_function_frame_inputs(&frame, &activation, 3, &saved, &count);
    REQUIRE(values && count == 4);
    for (int i = 0; i < 4; i++) {
        REQUIRE(owned(&values[i]));
    }
    /* Private synthetic metadata is retired; this is not authored pause-frame coverage. */
    afree(frame.parameters);
    afree(frame.locals);
    arena_free(&producer);
    REQUIRE(row(&values[0], "frame", 0));
    REQUIRE(rows(&values[1], 3, "frame", 0));
    REQUIRE(rows(&values[2], 0, "frame", 0) && !values[2].ptr);
    REQUIRE(rows(&values[3], 0, "frame", 0) && values[3].ptr);
    return true;
}
void test_language_record_state()
{
    RUN_TEST_CASE(record_state_replacement);
    RUN_TEST_CASE(record_state_owned_watches);
    RUN_TEST_CASE(record_state_frame_inputs);
    cleanup();
}
