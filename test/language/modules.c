#include "authoring/source_collection.h"
#include "backend.h"
#include "ecs/ecs.h"
#include "execution/status.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Fixed module-boundary specimen, not an app-scale or scheduling benchmark.
 * Each preparation has at most five tiny overlays and two libraries. */
enum
{
    MAX_SOURCES = 5,
    MAX_COLLECTIONS = 3
};
static char root[128];
static authoring_sources *collections[MAX_COLLECTIONS];
static execution_program *program;
static const script_catalog_parameter cells = { .name = "cells",
                                                .type = { .storage = PIN_F32 } };
static const script_catalog_capability tick = { .id = 10,
                                                .name = "Tick",
                                                .kind = SCRIPT_CAPABILITY_EVENT,
                                                .parameters = &cells,
                                                .parameter_count = 1,
                                                .completion_outcome = -1 };
static const script_catalog catalog = { .capabilities = &tick, .capability_count = 1 };
static const script_lower_bindings bindings = { .generation = 1 };
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };

typedef struct
{
    const char *path, *text;
} input;

static void setup()
{
    strcpy(root, "/tmp/chibi-modules-XXXXXX");
    assert(mkdtemp(root));
}
static void clear()
{
    execution_program_free(program);
    program = NULL;
    for (int i = 0; i < MAX_COLLECTIONS; i++) {
        authoring_sources_free(collections[i]);
        collections[i] = NULL;
    }
}
static void cleanup()
{
    clear();
    rmdir(root);
}
static source_catalog *snapshot(const input *inputs, int count, bool reverse)
{
    assert(count > 0 && count <= MAX_SOURCES);
    char paths[MAX_SOURCES][256];
    source_catalog_edit edits[MAX_SOURCES];
    for (int i = 0; i < count; i++) {
        int at = reverse ? count - 1 - i : i;
        snprintf(paths[i], sizeof paths[i], "%s/%s", root, inputs[at].path);
        edits[i] = (source_catalog_edit){ .path = paths[i], .text = inputs[at].text };
    }
    return source_catalog_replaced(root, &catalog, edits, count);
}
static void prepare(int collection, const input *inputs, int count, bool reverse, behavior_backend backend)
{
    assert(collection >= 0 && collection < MAX_COLLECTIONS && !collections[collection]);
    collections[collection] = authoring_sources_prepare_catalog(
        snapshot(inputs, count, reverse),
        &bindings,
        backend
    );
}
static const authoring_source *source(int collection, const char *name)
{
    for (int i = 0; i < authoring_sources_count(collections[collection]); i++) {
        const authoring_source *s = authoring_sources_at(collections[collection], i);
        if (s->analysis->name && !strcmp(s->analysis->name, name)) {
            return s;
        }
    }
    return NULL;
}
static bool valid(int collection)
{
    if (authoring_sources_valid(collections[collection])) {
        return true;
    }
    for (int i = 0;
         i < authoring_sources_catalog_diagnostic_count(collections[collection]);
         i++) {
        fprintf(
            stderr,
            "modules: %s\n",
            authoring_sources_catalog_diagnostic_at(collections[collection], i)->message
        );
    }
    for (int i = 0; i < authoring_sources_count(collections[collection]); i++) {
        const authoring_source *s = authoring_sources_at(collections[collection], i);
        for (int j = 0; j < s->diagnostic_count; j++) {
            fprintf(stderr, "%s: %s\n", s->path, s->diagnostics[j].message);
        }
    }
    return false;
}
static bool diagnostic(const authoring_source *s, const char *message)
{
    for (int i = 0; s && i < s->diagnostic_count; i++) {
        if (strstr(s->diagnostics[i].message, message)) {
            return true;
        }
    }
    return false;
}
static bool run(
    int collection,
    const char *name,
    behavior_backend backend,
    const double *amounts,
    const double *expected,
    int count
)
{
    const authoring_source *s = source(collection, name);
    REQUIRE(s && s->valid);
    REQUIRE(execution_module_backend(s->module) == backend);
    uint64_t offset = 0;
    for (int i = 0; i < s->analysis->symbol_count; i++) {
        const script_symbol *symbol = &s->analysis->symbols[i];
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, "offset")) {
            offset = symbol->id;
        }
    }
    REQUIRE(offset);
    program = execution_program_new(s->module, NULL, NULL);
    REQUIRE(program);
    execution_instance *instance = execution_program_attach(program, 1);
    REQUIRE(instance);
    for (int i = 0; i < count; i++) {
        const pin_value arg = { .type = PIN_F32, .f = (float)amounts[i] };
        REQUIRE(execution_instance_dispatch(instance, tick.id, &arg, 1, i, 0, 1000) == FLOW_OK);
        const pin_value *value = execution_instance_state(instance, offset);
        REQUIRE(value && value->type == PIN_F32 && value->f == (float)expected[i]);
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    execution_program_free(program);
    program = NULL;
    return true;
}

/* Studio kit_code.c scroll_sideways: clamp(offset + cells * advance, 0,
 * max(most, 0)). The fixed inputs hit the upper/lower bounds and an interior
 * value. No RML, native functions, service registry or source flattening. */
#define STEP_BODY                                                              \
    "next := offset + cells * advance; if most < 0 { most = 0; } "             \
    "if next < 0 { return 0; } if next > most { return most; } return next;"
#define STEP                                                                   \
    "Step :: (offset: number, cells: number, advance: number, most: number) "  \
    "-> number { " STEP_BODY " } "

TEST_CASE(modules_scroll_update_matches_inline_local_and_qualified_helpers)
{
    static const input inputs[] = {
        { "libraries/z/Scroll.chibi", "Scroll :: library { " STEP " }" },
        { "libraries/a/Other.chibi", "Other :: library { Step :: (offset: number, cells: number, advance: number, most: number) -> number { return -100; } }" },
        { "behaviors/Inline.chibi", "Inline :: behavior { offset: number = 0; on Tick :: (cells: number) { offset += cells * 2; if offset < 0 { offset = 0; } if offset > 6 { offset = 6; } } }" },
        { "behaviors/Local.chibi", "Local :: behavior { offset: number = 0; " STEP "on Tick :: (cells: number) { offset = Step(offset, cells, 2, 6); } }" },
        { "behaviors/Imported.chibi", "Imported :: behavior { offset: number = 0; on Tick :: (cells: number) { offset = Scroll.Step(offset, cells, 2, 6); } }" }
    };
    static const double amounts[] = { 1, 4, -1, -20 };
    static const double expected[] = { 2, 6, 4, 0 };
    static const char *const names[] = { "Inline", "Local", "Imported" };
    for (int b = 0; b < 2; b++) {
        REQUIRE(behavior_backend_available(backends[b]));
        for (int order = 0; order < 2; order++) {
            prepare(0, inputs, 5, order, backends[b]);
            REQUIRE(valid(0));
            const script_catalog *view = authoring_sources_catalog(collections[0]);
            REQUIRE(
                script_catalog_find_capability(view, "Step", SCRIPT_CAPABILITY_FUNCTION) == -2
            );
            int scroll = script_catalog_find_capability(view, "Scroll.Step", SCRIPT_CAPABILITY_FUNCTION);
            int other = script_catalog_find_capability(view, "Other.Step", SCRIPT_CAPABILITY_FUNCTION);
            REQUIRE(scroll >= 0 && other >= 0 && scroll != other);
            REQUIRE(view->capabilities[scroll].id != view->capabilities[other].id);
            for (int i = 0; i < 3; i++) {
                REQUIRE(run(0, names[i], backends[b], amounts, expected, 4));
                const execution_module *module = source(0, names[i])->module;
                REQUIRE(execution_module_import_count(module) == (i == 2 ? 1 : 0));
            }
            const execution_module
                *imported = execution_module_import_at(source(0, "Imported")->module, 0);
            REQUIRE(imported == source(0, "Scroll")->module);
            REQUIRE(execution_module_backend(imported) == backends[b]);
            clear();
        }
    }
    return true;
}

TEST_CASE(modules_unqualified_same_spelled_helpers_are_ambiguous)
{
    static const input inputs[] = {
        { "libraries/Left.chibi", "Left :: library { Step :: () -> number { return 1; } }" },
        { "libraries/Right.chibi",
          "Right :: library { Step :: () -> number { return 2; } }" },
        { "behaviors/Caller.chibi", "Caller :: behavior { offset: number = 0; on Tick :: () { offset = Step(); } }" }
    };
    for (int order = 0; order < 2; order++) {
        prepare(0, inputs, 3, order, BEHAVIOR_BYTECODE);
        REQUIRE(!authoring_sources_valid(collections[0]));
        REQUIRE(source(0, "Left")->valid && source(0, "Right")->valid);
        REQUIRE(diagnostic(source(0, "Caller"), "Ambiguous callable 'Step'"));
        clear();
    }
    return true;
}

TEST_CASE(modules_duplicate_qualified_helpers_cannot_publish_by_path_order)
{
    static const input inputs[] = {
        { "libraries/First.chibi", "Same :: library { Step :: () -> number { return 1; } }" },
        { "libraries/Second.chibi",
          "Same :: library { Step :: () -> number { return 2; } }" },
        { "behaviors/Caller.chibi", "Caller :: behavior { offset: number = 0; on Tick :: () { offset = Same.Step(); } }" }
    };
    for (int order = 0; order < 2; order++) {
        prepare(0, inputs, 3, order, BEHAVIOR_BYTECODE);
        REQUIRE(!authoring_sources_valid(collections[0]));
        REQUIRE(authoring_sources_catalog_diagnostic_count(collections[0]) == 1);
        const source_catalog_diagnostic
            *d = authoring_sources_catalog_diagnostic_at(collections[0], 0);
        REQUIRE(!strcmp(d->message, "Duplicate exported callable name 'Same.Step'"));
        char path[256];
        snprintf(path, sizeof path, "%s/%s", root, inputs[1].path);
        REQUIRE(!strcmp(d->path, path));
        REQUIRE(
            d->range.start == (int)(strstr(inputs[1].text, "Step") - inputs[1].text)
        );
        REQUIRE(!source(0, "Caller")->module);
        clear();
    }
    return true;
}

TEST_CASE(modules_cross_library_cycles_reject_callers_without_generations)
{
    static const input inputs[] = {
        { "libraries/Left.chibi",
          "Left :: library { Step :: () -> number { return Right.Step(); } }" },
        { "libraries/Right.chibi",
          "Right :: library { Step :: () -> number { return Left.Step(); } }" },
        { "behaviors/Caller.chibi", "Caller :: behavior { offset: number = 0; on Tick :: () { offset = Left.Step(); } }" }
    };
    for (int order = 0; order < 2; order++) {
        prepare(0, inputs, 3, order, BEHAVIOR_BYTECODE);
        REQUIRE(!authoring_sources_valid(collections[0]));
        REQUIRE(!authoring_sources_catalog_diagnostic_count(collections[0]));
        for (int i = 0; i < 3; i++) {
            const authoring_source *s = authoring_sources_at(collections[0], i);
            REQUIRE(!s->module && !s->valid);
            REQUIRE(diagnostic(s, "Cross-library import cycle has no publishable generation"));
        }
        clear();
    }
    return true;
}

TEST_CASE(modules_library_state_is_not_an_exported_value)
{
    static const input inputs[] = {
        { "libraries/Store.chibi", "Store :: library { secret: number = 11; Read :: () -> number { return secret; } }" },
        { "behaviors/Caller.chibi", "Caller :: behavior { offset: number = 0; on Tick :: () { offset = Store.secret; } }" }
    };
    for (int order = 0; order < 2; order++) {
        prepare(0, inputs, 2, order, BEHAVIOR_BYTECODE);
        REQUIRE(!authoring_sources_valid(collections[0]));
        REQUIRE(source(0, "Store")->valid);
        REQUIRE(!source(0, "Caller")->valid && source(0, "Caller")->diagnostic_count > 0);
        const script_catalog *view = authoring_sources_catalog(collections[0]);
        REQUIRE(script_catalog_find_capability(view, "Store.Read", SCRIPT_CAPABILITY_FUNCTION) >= 0);
        REQUIRE(
            script_catalog_find_capability(view, "Store.secret", SCRIPT_CAPABILITY_FUNCTION) < 0
        );
        REQUIRE(view->global_count == 0);
        clear();
    }
    return true;
}

static uint64_t caller_state(const char *name)
{
    const authoring_source *s = source(0, "Caller");
    for (int i = 0; i < s->analysis->symbol_count; i++) {
        const script_symbol *symbol = &s->analysis->symbols[i];
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, name)) {
            return symbol->id;
        }
    }
    return 0;
}
/* Reads a record field of value as f64, or NAN when it has none. */
static double record_number(pin_value value, const char *name)
{
    for (int i = 0; value.record && i < value.record->type.field_count; i++) {
        const type_field *field = &value.record->type.fields[i];
        pin_value read;
        if (!strcmp(field->name, name) &&
            !pin_read_field(NULL, value.ptr, field, &read)) {
            return read.type == PIN_F64 ? read.d
                 : read.type == PIN_F32 ? read.f
                                        : NAN;
        }
    }
    return NAN;
}
static execution_instance *run_caller(int collection)
{
    program = execution_program_new(source(collection, "Caller")->module, NULL, NULL);
    execution_instance *instance = program ? execution_program_attach(program, 1)
                                           : NULL;
    const pin_value amount = { .type = PIN_F32, .f = 5 };
    return instance &&
                   execution_instance_dispatch(instance, tick.id, &amount, 1, 0, 0, 1000) == FLOW_OK
             ? instance
             : NULL;
}
/* One owner on bytecode. A behavior keeps a pointer to a record its library
 * declares: an identical reload keeps the pointer, and one that adds a field
 * to the record resets the pointer that reaches an old one. Other state keeps
 * its value, and the next tick makes an Actor of the new layout. */
TEST_CASE(modules_replacement_resets_imported_pointee_layouts)
{
#define CALLER(fields)                                                         \
    "Caller :: behavior { offset: number = 0; saved: ^Store.Actor?; "          \
    "on Tick :: (cells: number) { if saved == nil { "                          \
    "actor: Store.Actor = {" fields "}; saved = &actor; } "                    \
    "if saved != nil { offset = saved.hp; } } }"
    static const input
        inputs[] = { { "libraries/Store.chibi",
                       "Store :: library { Actor :: struct { hp: number; } }" },
                     { "behaviors/Caller.chibi", CALLER("hp: cells") } },
        added[] = { { "libraries/Store.chibi", "Store :: library { Actor :: struct { hp: number; armor: number; } }" },
                    { "behaviors/Caller.chibi", CALLER("hp: cells, armor: 0") } };
#undef CALLER
    prepare(0, inputs, 2, false, BEHAVIOR_BYTECODE);
    prepare(1, inputs, 2, false, BEHAVIOR_BYTECODE);
    prepare(2, added, 2, false, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0) && valid(1) && valid(2));
    uint64_t saved = caller_state("saved"), offset = caller_state("offset");
    execution_instance *instance = run_caller(0);
    REQUIRE(saved && offset && instance);
    const void *cell = execution_instance_state(instance, saved)->ptr;
    REQUIRE(cell);
    REQUIRE(execution_program_replace(program, source(1, "Caller")->module, false) == FLOW_OK);
    instance = execution_program_instance(program, 1);
    REQUIRE(execution_instance_state(instance, saved)->ptr == cell);
    REQUIRE(execution_program_replace(program, source(2, "Caller")->module, false) == FLOW_OK);
    REQUIRE(execution_program_module(program) == source(2, "Caller")->module);
    instance = execution_program_instance(program, 1);
    REQUIRE(!execution_instance_state(instance, saved)->ptr);
    const pin_value *kept = execution_instance_state(instance, offset);
    REQUIRE(kept->type == PIN_F32 && kept->f == 5);
    const pin_value amount = { .type = PIN_F32, .f = 6 };
    REQUIRE(execution_instance_dispatch(instance, tick.id, &amount, 1, 0, 0, 1000) == FLOW_OK);
    REQUIRE(execution_instance_state(instance, saved)->ptr);
    REQUIRE(execution_instance_state(instance, offset)->f == 6);
    clear();
    return true;
}
/* One owner on bytecode. A reload keeps record and number state whose types
 * changed: fields match by name, a new field and one that cannot convert take
 * their defaults, a removed one is dropped, and numbers convert as T(value)
 * does, narrowing included. */
TEST_CASE(modules_replacement_migrates_kept_records_and_numbers)
{
    static const input inputs
        [] = { { "libraries/Store.chibi", "Store :: library { Actor :: struct { hp: number; name: string; old: number; } }" },
               { "behaviors/Caller.chibi",
                 "Caller :: behavior { a: Store.Actor = {hp: 1, name: \"a\", "
                 "old: 2}; n: number = 0; "
                 "on Tick :: (cells: number) { a.hp = cells; a.name = \"set\"; "
                 "n = cells; } }" } },
  changed[] = { { "libraries/Store.chibi",
                  "Store :: library { Actor :: struct { hp: f64; name: number "
                  "= 3; "
                  "armor: number = 7; } }" },
                { "behaviors/Caller.chibi",
                  "Caller :: behavior { a: Store.Actor = {hp: 1}; "
                  "n: f64 = 0; "
                  "on Tick :: (cells: number) { a.hp = cells; n = cells; } "
                  "}" } },
  narrowed[] = { inputs[0],
                 { "behaviors/Caller.chibi",
                   "Caller :: behavior { a: Store.Actor = {hp: 1, name: \"a\", "
                   "old: 2}; n: int = 0; "
                   "on Tick :: (cells: number) { n = int(cells); } }" } };
    prepare(0, inputs, 2, false, BEHAVIOR_BYTECODE);
    prepare(1, changed, 2, false, BEHAVIOR_BYTECODE);
    prepare(2, narrowed, 2, false, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0) && valid(1) && valid(2));
    uint64_t a = caller_state("a"), n = caller_state("n");
    execution_instance *instance = run_caller(0);
    REQUIRE(a && n && instance);
    REQUIRE(execution_program_replace(program, source(2, "Caller")->module, false) == FLOW_OK);
    instance = execution_program_instance(program, 1);
    const pin_value *number = execution_instance_state(instance, n);
    REQUIRE(number && number->type == PIN_I32 && number->i == 5);
    REQUIRE(execution_program_replace(program, source(1, "Caller")->module, false) == FLOW_OK);
    instance = execution_program_instance(program, 1);
    const pin_value *actor = execution_instance_state(instance, a);
    number = execution_instance_state(instance, n);
    REQUIRE(actor && record_number(*actor, "hp") == 5 && record_number(*actor, "armor") == 7);
    REQUIRE(record_number(*actor, "name") == 3 && isnan(record_number(*actor, "old")));
    REQUIRE(number && number->type == PIN_F64 && number->d == 5);
    clear();
    return true;
}
/* One owner on bytecode. Items of a kept array migrate when every field they
 * keep converts. An item cannot take a new field's default, so adding one to
 * the record resets the array to its initializer. */
TEST_CASE(modules_replacement_migrates_kept_array_items)
{
#define STORE(fields) "Store :: library { Actor :: struct { " fields " } }"
#define CALLER(items)                                                          \
    "Caller :: behavior { xs: Store.Actor[] = []; "                            \
    "on Tick :: (cells: number) { xs = [" items "]; } }"
    static const input
        inputs[] = { { "libraries/Store.chibi", STORE("hp: number; old: number;") },
                     { "behaviors/Caller.chibi",
                       CALLER("{hp: cells, old: 1}, {hp: 2, old: 1}") } },
        removed[] = { { "libraries/Store.chibi", STORE("hp: f64;") },
                      { "behaviors/Caller.chibi", CALLER("{hp: cells}") } },
        added[] = { { "libraries/Store.chibi",
                      STORE("hp: number; old: number; armor: number = 7;") },
                    { "behaviors/Caller.chibi", CALLER("{hp: cells, old: 1}") } };
#undef STORE
#undef CALLER
    prepare(0, inputs, 2, false, BEHAVIOR_BYTECODE);
    prepare(1, removed, 2, false, BEHAVIOR_BYTECODE);
    prepare(2, added, 2, false, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0) && valid(1) && valid(2));
    uint64_t xs = caller_state("xs");
    execution_instance *instance = run_caller(0);
    REQUIRE(xs && instance);
    REQUIRE(execution_program_replace(program, source(1, "Caller")->module, false) == FLOW_OK);
    instance = execution_program_instance(program, 1);
    const pin_value *items = execution_instance_state(instance, xs);
    REQUIRE(items && items->container == PIN_CONTAINER_ARRAY);
    const pin_array *array = items->ptr;
    REQUIRE(array && array->count == 2);
    REQUIRE(
        record_number(pin_array_get(array, 0), "hp") == 5 &&
        record_number(pin_array_get(array, 1), "hp") == 2
    );
    REQUIRE(isnan(record_number(pin_array_get(array, 0), "old")));
    REQUIRE(execution_program_replace(program, source(2, "Caller")->module, false) == FLOW_OK);
    instance = execution_program_instance(program, 1);
    items = execution_instance_state(instance, xs);
    REQUIRE(items && items->container == PIN_CONTAINER_ARRAY);
    array = items->ptr;
    REQUIRE(!array || array->count == 0);
    clear();
    return true;
}

TEST_CASE(modules_independent_catalogs_do_not_supply_missing_or_other_bodies)
{
    const input inputs[] = {
        { "libraries/Store.chibi",
          "Store :: library { Read :: () -> number { return 3; } }" },
        { "behaviors/Caller.chibi", "Caller :: behavior { offset: number = 0; on Tick :: () { offset = Store.Read(); } }" }
    };
    input other[2] = { inputs[0], inputs[1] };
    other[0].text = "Store :: library { Read :: () -> number { return 7; } }";
    static const double amounts[] = { 0 }, first[] = { 3 }, second[] = { 7 };
    for (int b = 0; b < 2; b++) {
        REQUIRE(behavior_backend_available(backends[b]));
        prepare(0, inputs, 2, false, backends[b]);
        prepare(1, other, 2, false, backends[b]);
        prepare(2, &inputs[1], 1, false, backends[b]);
        REQUIRE(valid(0) && valid(1));
        REQUIRE(
            execution_module_id(source(0, "Store")->module) ==
            execution_module_id(source(1, "Store")->module)
        );
        REQUIRE(source(0, "Store")->module != source(1, "Store")->module);
        REQUIRE(!authoring_sources_valid(collections[2]));
        REQUIRE(diagnostic(source(2, "Caller"), "Unknown callable 'Store.Read'"));
        REQUIRE(run(0, "Caller", backends[b], amounts, first, 1));
        REQUIRE(run(1, "Caller", backends[b], amounts, second, 1));
        clear();
    }
    return true;
}

TEST_CASE(modules_nested_same_spelled_helpers_stay_in_lexical_scope)
{
    static const input inputs[] = {
        { "libraries/Scoped.chibi", "Scoped :: library { First :: () { Helper :: () -> number { return 2; } return Helper(); } Second :: () { Helper :: () -> number { return 3; } return Helper(); } }" },
        { "behaviors/Caller.chibi", "Caller :: behavior { offset: number = 0; on Tick :: () { offset = Scoped.First() * 10 + Scoped.Second(); } }" }
    };
    static const double amounts[] = { 0 }, expected[] = { 23 };
    for (int b = 0; b < 2; b++) {
        REQUIRE(behavior_backend_available(backends[b]));
        for (int order = 0; order < 2; order++) {
            prepare(0, inputs, 2, order, backends[b]);
            REQUIRE(valid(0));
            const script_catalog *view = authoring_sources_catalog(collections[0]);
            REQUIRE(
                script_catalog_find_capability(view, "Scoped.First", SCRIPT_CAPABILITY_FUNCTION) >= 0
            );
            REQUIRE(
                script_catalog_find_capability(view, "Scoped.Second", SCRIPT_CAPABILITY_FUNCTION) >= 0
            );
            REQUIRE(
                script_catalog_find_capability(view, "Scoped.Helper", SCRIPT_CAPABILITY_FUNCTION) < 0
            );
            const script_analysis *a = source(0, "Scoped")->analysis;
            uint64_t helpers[2] = { 0 };
            int helper_count = 0;
            for (int i = 0; i < a->function_count; i++) {
                const script_function *f = &a->functions[i];
                if (f->symbol < 0 || strcmp(a->symbols[f->symbol].name, "Helper")) {
                    continue;
                }
                REQUIRE(helper_count < 2);
                REQUIRE(f->parent >= 0 && f->parent < a->function_count);
                const script_function *parent = &a->functions[f->parent];
                REQUIRE(parent->parent < 0 && parent->symbol >= 0);
                REQUIRE(a->symbols[f->symbol].scope == parent->scope);
                helpers[helper_count++] = a->symbols[f->symbol].id;
            }
            REQUIRE(helper_count == 2 && helpers[0] != helpers[1]);
            REQUIRE(run(0, "Caller", backends[b], amounts, expected, 1));
            clear();
        }
    }
    return true;
}

static const input generic_inputs[] = {
    { "libraries/Units.chibi",
      "Units :: library { "
      "Quantity :: struct($DataType: Type, $Length: int, $Time: int) { amount: "
      "DataType } "
      "ProductType :: (A: Type, B: Type) -> Type { return Quantity(A.DataType, "
      "A.Length + B.Length, A.Time + B.Time) } "
      "Identity :: (value: $T) -> T { return value } "
      "Factorial :: (n: int) -> int { if n <= 1 { return 1 } return n * "
      "Factorial(n - 1) } }" },
    { "libraries/Operations.chibi",
      "Operations :: library { "
      "Multiply :: (a: $A, b: $B) -> Units.ProductType(A, B) { "
      "result: Units.ProductType(A, B) = {amount: a.amount * b.amount}; return "
      "result } "
      "Scale :: (value: $T, $N: int) -> T { Factor :: Units.Factorial(4); "
      "return Units.Identity(value) * N + Factor } }" },
    { "behaviors/Caller.chibi",
      "Caller :: behavior { offset: number = 0; on Tick :: (cells: number) { "
      "speed: Units.Quantity(number, 1, -1) = {amount: 6}; "
      "time: Units.Quantity(number, 0, 1) = {amount: 2}; "
      "distance := Operations.Multiply(speed, time); "
      "offset = distance.amount + Operations.Scale(cells, 2) } }" },
};

TEST_CASE(modules_imported_scalar_templates_resolve_nested_calls_without_record_types)
{
    static const input inputs[] = {
        { "libraries/Numbers.chibi",
          "Numbers :: library { Identity :: (value: $T) -> T { return value } "
          "Factorial :: (n: int) -> int { if n <= 1 { return 1 } "
          "return n * Factorial(n - 1) } }" },
        { "libraries/Operations.chibi",
          "Operations :: library { Scale :: (value: $T, $N: int) -> T { "
          "Factor :: Numbers.Factorial(4); "
          "return Numbers.Identity(value) * N + Factor } }" },
        { "behaviors/Caller.chibi",
          "Caller :: behavior { offset: number = 0; on Tick :: (cells: number) "
          "{ "
          "offset = Operations.Scale(cells, 2) } }" },
    };
    static const double amounts[] = { 2 }, expected[] = { 28 };
    for (int backend = 0; backend < 2; backend++) {
        prepare(0, inputs, 3, false, backends[backend]);
        REQUIRE(valid(0));
        REQUIRE(run(0, "Caller", backends[backend], amounts, expected, 1));
        clear();
    }
    return true;
}

TEST_CASE(modules_empty_compile_bindings_materialize)
{
    static const input inputs[] = {
        { "libraries/Flags.chibi",
          "Flags :: library { Enabled :: () -> bool { return true } }" },
        { "behaviors/Caller.chibi",
          "Caller :: behavior { Value :: Flags.Enabled(); offset:number=0; "
          "on Tick :: (cells: number) { if Value { offset=cells } } }" },
    };
    static const double amounts[] = { 2 }, expected[] = { 2 };
    for (int backend = 0; backend < 2; backend++) {
        prepare(0, inputs, 2, false, backends[backend]);
        REQUIRE(valid(0));
        const authoring_source *caller = source(0, "Caller");
        REQUIRE(caller);
        const script_analysis *a = caller->analysis;
        REQUIRE(a->request_count == 1);
        const script_specialization_request *request = &a->requests[0];
        REQUIRE(request->evaluate);
        REQUIRE(!request->binding_count && !request->bindings);
        const script_catalog *view = authoring_sources_catalog(collections[0]);
        bool found = false;
        for (int i = 0; i < view->specialization_count; i++) {
            const script_catalog_specialization *s = &view->specializations[i];
            if (s->id != request->id) {
                continue;
            }
            REQUIRE(s->template_id == request->template_id);
            REQUIRE(!s->binding_count);
            REQUIRE(!s->executable && !s->result.is_type);
            REQUIRE(s->result.value.type == PIN_BOOL && s->result.value.b);
            found = true;
        }
        REQUIRE(found);
        REQUIRE(run(0, "Caller", backends[backend], amounts, expected, 1));
        clear();
    }
    return true;
}

TEST_CASE(modules_imported_float_compile_values_survive_dedupe_and_cache_reuse)
{
    static const input inputs[] = {
        { "libraries/Numbers.chibi",
          "Numbers :: library { Double :: (value: number) -> number { "
          "return value * 2 } }" },
        { "libraries/Operations.chibi",
          "Operations :: library { Scale :: (value: $T, $Gain: number) -> T { "
          "Offset :: Numbers.Double(Gain); return value * Gain + Offset } }" },
        { "behaviors/Caller.chibi",
          "Caller :: behavior { offset: number = 0; on Tick :: (cells: number) "
          "{ "
          "offset = Operations.Scale(cells, 1.5) + "
          "Operations.Scale(cells, 1.5) } }" },
    };
    static const double amounts[] = { 2 }, expected[] = { 12 };
    for (int backend = 0; backend < 2; backend++) {
        prepare(0, inputs, 3, false, backends[backend]);
        REQUIRE(valid(0));
        collections[1] = authoring_sources_prepare_incremental(
            snapshot(inputs, 3, true),
            &bindings,
            backends[backend],
            collections[0]
        );
        REQUIRE(valid(1));
        REQUIRE(authoring_sources_reused(collections[1]) == 3);
        REQUIRE(run(1, "Caller", backends[backend], amounts, expected, 1));
        clear();
    }
    return true;
}

TEST_CASE(modules_float_specialization_metadata_compares_exact_ieee_bits)
{
    char path[256];
    snprintf(path, sizeof path, "%s/behaviors/Caller.chibi", root);
    const source_catalog_edit edit = { .path = path, .text = "Caller :: behavior { offset: number = 0; on Tick :: (cells: number) { offset = cells } }" };
    const uint64_t doubles[] = { UINT64_C(0),
                                 UINT64_C(0x8000000000000000),
                                 UINT64_C(0x7ff8000000000001),
                                 UINT64_C(0x7ff8000000000002) };
    const uint32_t floats[] = { UINT32_C(0),
                                UINT32_C(0x80000000),
                                UINT32_C(0x7fc00001),
                                UINT32_C(0x7fc00002) };
    for (int width = 0; width < 2; width++) {
        for (int left = 0; left < 4; left++) {
            for (int right = 0; right < 4; right++) {
                script_catalog_specialization a = { .id = 1, .template_id = 2 },
                                              b = a;
                a.result.value.type = b.result.value.type = width ? PIN_F64 : PIN_F32;
                if (width) {
                    memcpy(
                        &a.result.value.d,
                        &doubles[left],
                        sizeof a.result.value.d
                    );
                    memcpy(
                        &b.result.value.d,
                        &doubles[right],
                        sizeof b.result.value.d
                    );
                } else {
                    memcpy(
                        &a.result.value.f,
                        &floats[left],
                        sizeof a.result.value.f
                    );
                    memcpy(
                        &b.result.value.f,
                        &floats[right],
                        sizeof b.result.value.f
                    );
                }
                script_catalog x = catalog, y = catalog;
                x.specializations = &a;
                y.specializations = &b;
                x.specialization_count = y.specialization_count = 1;
                collections[0] = authoring_sources_prepare_catalog(
                    source_catalog_replaced(root, &x, &edit, 1),
                    &bindings,
                    BEHAVIOR_BYTECODE
                );
                REQUIRE(valid(0));
                collections[1] = authoring_sources_prepare_incremental(
                    source_catalog_replaced(root, &y, &edit, 1),
                    &bindings,
                    BEHAVIOR_BYTECODE,
                    collections[0]
                );
                REQUIRE(valid(1));
                REQUIRE(authoring_sources_reused(collections[1]) == (left == right ? 1 : 0));
                clear();
            }
        }
    }
    return true;
}

TEST_CASE(modules_imported_templates_keep_owner_types_bodies_and_nested_requests)
{
    static const double amounts[] = { 2 }, expected[] = { 40 };
    for (int backend = 0; backend < 2; backend++) {
        for (int order = 0; order < 2; order++) {
            prepare(0, generic_inputs, 3, order, backends[backend]);
            REQUIRE(valid(0));
            REQUIRE(run(0, "Caller", backends[backend], amounts, expected, 1));
            const script_catalog *view = authoring_sources_catalog(collections[0]);
            REQUIRE(view->template_count >= 6 && view->specialization_count >= 6);
            const authoring_source *units = source(0, "Units");
            REQUIRE(units && strcmp(units->analysis->identity, units->analysis->name));
            bool record = false, factorial = false;
            for (int i = 0; i < view->type_count; i++) {
                const script_catalog_type *type = &view->types[i];
                if (type->authored_record) {
                    REQUIRE(type->field_count == 1 && type->size == sizeof(float));
                    REQUIRE(!strcmp(type->fields[0].name, "amount"));
                    const script_catalog_type *declared = NULL;
                    for (int j = 0; j < units->analysis->type_count; j++) {
                        if (units->analysis->types[j].id == type->id) {
                            declared = &units->analysis->types[j];
                            break;
                        }
                    }
                    REQUIRE(declared);
                    REQUIRE(!strcmp(declared->name, type->name));
                    REQUIRE(!strcmp(declared->source_name, type->source_name));
                    record = true;
                }
            }
            for (int i = 0; i < view->specialization_count; i++) {
                const script_catalog_specialization *instance = &view->specializations[i];
                if (!instance->executable && !instance->result.is_type &&
                    instance->result.value.type == PIN_I32 &&
                    instance->result.value.i == 24) {
                    factorial = true;
                }
                if (!instance->executable) {
                    continue;
                }
                const authoring_source *owner = NULL;
                for (int t = 0; t < view->template_count; t++) {
                    if (view->templates[t].id == instance->template_id) {
                        const char *identity = view->templates[t].owner;
                        for (int d = 0; d < authoring_sources_count(collections[0]);
                             d++) {
                            const authoring_source
                                *candidate = authoring_sources_at(collections[0], d);
                            if (!strcmp(candidate->analysis->identity, identity)) {
                                owner = candidate;
                            }
                        }
                    }
                }
                REQUIRE(owner && owner->analysis->kind == SCRIPT_PROGRAM_LIBRARY);
                bool published = false;
                for (int f = 0; f < execution_module_function_count(owner->module);
                     f++) {
                    execution_module_function_info
                        function = execution_module_function_at(owner->module, f);
                    published |= function.id == instance->id && function.body;
                }
                REQUIRE(published);
            }
            REQUIRE(record && factorial);
            const authoring_source *caller = source(0, "Caller");
            for (int i = 0; i < caller->analysis->function_count; i++) {
                int symbol = caller->analysis->functions[i].symbol;
                REQUIRE(
                    symbol < 0 ||
                    (strcmp(caller->analysis->symbols[symbol].name, "Multiply") &&
                     strcmp(caller->analysis->symbols[symbol].name, "Scale") &&
                     strcmp(caller->analysis->symbols[symbol].name, "Identity"))
                );
            }
            clear();
        }
    }
    return true;
}

TEST_CASE(modules_imported_template_body_edits_invalidate_linked_generations)
{
    static const double amounts[] = { 2 }, before[] = { 40 }, after[] = { 136 };
    input edited[3] = { generic_inputs[0], generic_inputs[1], generic_inputs[2] };
    edited[0]
        .text = "Units :: library { "
                "Quantity :: struct($DataType: Type, $Length: int, $Time: int) "
                "{ amount: DataType } "
                "ProductType :: (A: Type, B: Type) -> Type { return "
                "Quantity(A.DataType, A.Length + B.Length, A.Time + B.Time) } "
                "Identity :: (value: $T) -> T { return value } "
                "Factorial :: (n: int) -> int { if n <= 1 { return 5 } return "
                "n * Factorial(n - 1) } }";
    for (int backend = 0; backend < 2; backend++) {
        prepare(0, generic_inputs, 3, false, backends[backend]);
        REQUIRE(valid(0));
        collections[1] = authoring_sources_prepare_incremental(
            snapshot(edited, 3, true),
            &bindings,
            backends[backend],
            collections[0]
        );
        REQUIRE(valid(1));
        REQUIRE(authoring_sources_reused(collections[1]) == 0);
        REQUIRE(run(0, "Caller", backends[backend], amounts, before, 1));
        REQUIRE(run(1, "Caller", backends[backend], amounts, after, 1));
        clear();
    }
    return true;
}

TEST_CASE(modules_failed_imported_requests_publish_no_concrete_generation)
{
    static const input inputs[] = {
        { "libraries/Bad.chibi",
          "Bad :: library { Broken :: (value: $T) -> int { return true } }" },
        { "behaviors/Caller.chibi", "Caller :: behavior { offset: number = 0; on Tick :: () { offset = Bad.Broken(int(1)) } }" },
    };
    for (int backend = 0; backend < 2; backend++) {
        prepare(0, inputs, 2, false, backends[backend]);
        REQUIRE(!authoring_sources_valid(collections[0]));
        const script_catalog *view = authoring_sources_catalog(collections[0]);
        REQUIRE(view->specialization_count == 0);
        REQUIRE(!source(0, "Caller")->module);
        bool owner_diagnostic = false;
        for (int i = 0;
             i < authoring_sources_catalog_diagnostic_count(collections[0]);
             i++) {
            const source_catalog_diagnostic *
                diagnostic = authoring_sources_catalog_diagnostic_at(collections[0], i);
            owner_diagnostic |= strstr(diagnostic->path, "Bad.chibi") &&
                                diagnostic->range.start > 0;
        }
        REQUIRE(owner_diagnostic);
        clear();
    }
    return true;
}

TEST_CASE(modules_retained_library_state_inherits_consuming_owner)
{
    const input inputs[] = {
        { "libraries/Store.chibi", "Store :: library { value:int=0; Next::()->int { calls:retain int=0; value+=1; calls+=1; return value+calls; } }" },
        { "systems/Counter.chibi", "Counter :: system() { observed:retain int=0; observed=Store.Next(); }" },
        { "behaviors/Reader.chibi", "Reader :: behavior { observed:int=0; on Tick :: () { observed=Store.Next(); } }" },
    };
    for (int backend = 0; backend < 2; backend++) {
        prepare(0, inputs, 3, false, backends[backend]);
        REQUIRE(valid(0));
        const authoring_source *counter = source(0, "Counter"),
                               *reader = source(0, "Reader");
        uint64_t counter_value = 0, reader_value = 0;
        for (int i = 0; i < counter->analysis->symbol_count; i++) {
            const script_symbol *s = &counter->analysis->symbols[i];
            if (s->kind == SCRIPT_SHARED_STATE && !strcmp(s->name, "observed")) {
                counter_value = s->id;
            }
        }
        for (int i = 0; i < reader->analysis->symbol_count; i++) {
            const script_symbol *s = &reader->analysis->symbols[i];
            if (s->kind == SCRIPT_STATE && !strcmp(s->name, "observed")) {
                reader_value = s->id;
            }
        }
        REQUIRE(counter_value && reader_value);
        ecs_world *world = ecs_world_new();
        ecs_entity a = ecs_world_create_entity(world),
                   b = ecs_world_create_entity(world);
        program = execution_program_new(counter->module, world, NULL);
        REQUIRE(program && execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        execution_instance *row = execution_program_instance(program, ecs_entity_to_u64(a));
        REQUIRE(row && execution_instance_state(row, counter_value)->i == 4);
        execution_program *other = execution_program_new(counter->module, world, NULL);
        REQUIRE(other && execution_program_step(other, 0, 0, 1000) == FLOW_OK);
        execution_instance *isolated = execution_program_instance(other, ecs_entity_to_u64(a));
        REQUIRE(isolated && execution_instance_state(isolated, counter_value)->i == 4);
        execution_program_free(other);
        REQUIRE(!ecs_world_destroy_entity(world, a) && !ecs_world_destroy_entity(world, b));
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_replace(program, counter->module, false) == FLOW_OK);
        a = ecs_world_create_entity(world);
        REQUIRE(execution_program_step(program, 2, 0, 1000) == FLOW_OK);
        row = execution_program_instance(program, ecs_entity_to_u64(a));
        REQUIRE(row && execution_instance_state(row, counter_value)->i == 6);
        REQUIRE(execution_program_replace(program, counter->module, true) == FLOW_OK);
        REQUIRE(execution_program_step(program, 3, 0, 1000) == FLOW_OK);
        row = execution_program_instance(program, ecs_entity_to_u64(a));
        REQUIRE(row && execution_instance_state(row, counter_value)->i == 2);
        execution_program_free(program);
        program = execution_program_new(reader->module, world, NULL);
        REQUIRE(program);
        execution_instance *first = execution_program_attach(program, ecs_entity_to_u64(a));
        b = ecs_world_create_entity(world);
        execution_instance *second = execution_program_attach(program, ecs_entity_to_u64(b));
        const pin_value amount = { .type = PIN_F32 };
        REQUIRE(first && second);
        REQUIRE(execution_instance_dispatch(first, tick.id, &amount, 1, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_dispatch(second, tick.id, &amount, 1, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_state(first, reader_value)->i == 2);
        REQUIRE(execution_instance_state(second, reader_value)->i == 2);
        execution_program_free(program);
        program = NULL;
        ecs_world_free(world);
        clear();
    }
    return true;
}
TEST_CASE(modules_systems_reject_task_state_in_imported_libraries)
{
    const char *types[] = { "task", "task[]", "[string: task]", "[string: task[]][]" };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        char library[256];
        snprintf(library, sizeof library, "Store :: library { pending:retain %s; Read::()->int { return 1; } }", types[i]);
        const input inputs[] = {
            { "libraries/Store.chibi", library },
            { "systems/Counter.chibi", "Counter :: system() { observed:retain int=0; observed=Store.Read(); }" },
            { "behaviors/Reader.chibi", "Reader :: behavior { observed:int=0; on Tick :: () { observed=Store.Read(); } }" },
        };
        prepare(0, inputs, 3, false, BEHAVIOR_BYTECODE);
        if (!source(0, "Store")->valid || !source(0, "Reader")->valid) {
            fprintf(stderr, "retained task library type: %s\n", types[i]);
            valid(0);
        }
        REQUIRE(source(0, "Store")->valid && source(0, "Reader")->valid);
        REQUIRE(!source(0, "Counter")->valid);
        REQUIRE(diagnostic(source(0, "Counter"), "cannot contain tasks"));
        clear();
    }
    return true;
}
/* `has` names a map method only on a value, so a library's own `has` stays
 * callable through the library's name. */
TEST_CASE(modules_library_has_stays_callable_beside_map_methods)
{
    const input inputs[] = {
        { "libraries/Store.chibi", "Store :: library { has :: (value: bool) -> bool { return value; } }" },
        { "behaviors/Reader.chibi",
          "Reader :: behavior { observed: int = 0; seen: [int: bool] = [:]; on "
          "Tick :: () { "
          "if Store.has(value: true) && !seen.has(1) { observed = 1; } } }" },
    };
    prepare(0, inputs, 2, false, BEHAVIOR_BYTECODE);
    REQUIRE(source(0, "Store")->valid && source(0, "Reader")->valid);
    clear();
    return true;
}
TEST_CASE(modules_compile_time_parameters_preserve_constant_mutability)
{
    const input inputs[] = {
        { "libraries/Math.chibi", "Math :: library { Advance :: (/* value */ N:int)->int { N+=1; return N; } }" },
        { "behaviors/Caller.chibi", "Caller :: behavior { Value :: Math.Advance(1); observed:int=Value; }" },
    };
    prepare(0, inputs, 2, false, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0));
    bool result = false;
    const script_analysis *analysis = source(0, "Caller")->analysis;
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->kind == SCRIPT_CONSTANT && !strcmp(symbol->name, "Value")) {
            const pin_value value = analysis->expressions[symbol->initializer].constant;
            result = value.type == PIN_I32 && value.i == 2;
        }
    }
    REQUIRE(result);
    clear();
    input invalid[] = { inputs[0], inputs[1] };
    invalid[0].text = "Math :: library { Advance :: ($N:int)->int { N+=1; "
                      "return N; } }";
    prepare(0, invalid, 2, false, BEHAVIOR_BYTECODE);
    REQUIRE(!authoring_sources_valid(collections[0]));
    bool rejected = false;
    for (int i = 0; i < authoring_sources_catalog_diagnostic_count(collections[0]);
         i++) {
        const source_catalog_diagnostic
            *d = authoring_sources_catalog_diagnostic_at(collections[0], i);
        rejected |= strstr(d->message, "assignment cannot change a constant binding") !=
                    NULL;
    }
    REQUIRE(rejected);
    REQUIRE(!source(0, "Caller")->module);
    return true;
}
void test_language_modules()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(modules_scroll_update_matches_inline_local_and_qualified_helpers);
    RUN_TEST_CASE(modules_unqualified_same_spelled_helpers_are_ambiguous);
    RUN_TEST_CASE(modules_duplicate_qualified_helpers_cannot_publish_by_path_order);
    RUN_TEST_CASE(modules_cross_library_cycles_reject_callers_without_generations);
    RUN_TEST_CASE(modules_library_state_is_not_an_exported_value);
    RUN_TEST_CASE(modules_replacement_resets_imported_pointee_layouts);
    RUN_TEST_CASE(modules_replacement_migrates_kept_records_and_numbers);
    RUN_TEST_CASE(modules_replacement_migrates_kept_array_items);
    RUN_TEST_CASE(modules_independent_catalogs_do_not_supply_missing_or_other_bodies);
    RUN_TEST_CASE(modules_nested_same_spelled_helpers_stay_in_lexical_scope);
    RUN_TEST_CASE(modules_imported_scalar_templates_resolve_nested_calls_without_record_types);
    RUN_TEST_CASE(modules_empty_compile_bindings_materialize);
    RUN_TEST_CASE(modules_imported_float_compile_values_survive_dedupe_and_cache_reuse);
    RUN_TEST_CASE(modules_float_specialization_metadata_compares_exact_ieee_bits);
    RUN_TEST_CASE(modules_imported_templates_keep_owner_types_bodies_and_nested_requests);
    RUN_TEST_CASE(modules_imported_template_body_edits_invalidate_linked_generations);
    RUN_TEST_CASE(modules_failed_imported_requests_publish_no_concrete_generation);
    RUN_TEST_CASE(modules_retained_library_state_inherits_consuming_owner);
    RUN_TEST_CASE(modules_systems_reject_task_state_in_imported_libraries);
    RUN_TEST_CASE(modules_library_has_stays_callable_beside_map_methods);
    RUN_TEST_CASE(modules_compile_time_parameters_preserve_constant_mutability);
}
