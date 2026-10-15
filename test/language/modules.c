#include "authoring/source_collection.h"
#include "backend.h"
#include "execution/status.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
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
                                                .type = { .storage = PIN_F64 } };
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
static void prepare(int collection, const input *inputs, int count, bool reverse, behavior_backend backend)
{
    assert(count > 0 && count <= MAX_SOURCES);
    assert(collection >= 0 && collection < MAX_COLLECTIONS && !collections[collection]);
    char paths[MAX_SOURCES][256];
    source_catalog_edit edits[MAX_SOURCES];
    for (int i = 0; i < count; i++) {
        int at = reverse ? count - 1 - i : i;
        snprintf(paths[i], sizeof paths[i], "%s/%s", root, inputs[at].path);
        edits[i] = (source_catalog_edit){ .path = paths[i], .text = inputs[at].text };
    }
    source_catalog *snapshot = source_catalog_replaced(root, &catalog, edits, count);
    collections[collection] = authoring_sources_prepare_catalog(snapshot, &bindings, backend);
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
        const pin_value arg = { .type = PIN_F64, .d = amounts[i] };
        REQUIRE(execution_instance_dispatch(instance, tick.id, &arg, 1, i, 0, 1000) == FLOW_OK);
        const pin_value *value = execution_instance_state(instance, offset);
        REQUIRE(value && value->type == PIN_F64 && value->d == expected[i]);
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
        { "libraries/z/Scroll.chibi", "library Scroll { " STEP " }" },
        { "libraries/a/Other.chibi",
          "library Other { "
          "Step :: (offset: number, cells: number, advance: number, most: "
          "number) "
          "-> number { return -100; } }" },
        { "behaviors/Inline.chibi",
          "behavior Inline { offset: number = 0; "
          "on Tick(cells) { offset += cells * 2; if offset < 0 { offset = 0; } "
          "if offset > 6 { offset = 6; } } }" },
        { "behaviors/Local.chibi",
          "behavior Local { offset: number = 0; " STEP
          "on Tick(cells) { offset = Step(offset, cells, 2, 6); } }" },
        { "behaviors/Imported.chibi",
          "behavior Imported { offset: number = 0; "
          "on Tick(cells) { offset = Scroll.Step(offset, cells, 2, 6); } }" }
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
        { "libraries/Left.chibi", "library Left { Step :: () -> number { return 1; } }" },
        { "libraries/Right.chibi", "library Right { Step :: () -> number { return 2; } }" },
        { "behaviors/Caller.chibi",
          "behavior Caller { offset: number = 0; "
          "on Tick(cells) { offset = Step(); } }" }
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
        { "libraries/First.chibi", "library Same { Step :: () -> number { return 1; } }" },
        { "libraries/Second.chibi", "library Same { Step :: () -> number { return 2; } }" },
        { "behaviors/Caller.chibi",
          "behavior Caller { offset: number = 0; "
          "on Tick(cells) { offset = Same.Step(); } }" }
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
          "library Left { Step :: () -> number { return Right.Step(); } }" },
        { "libraries/Right.chibi",
          "library Right { Step :: () -> number { return Left.Step(); } }" },
        { "behaviors/Caller.chibi",
          "behavior Caller { offset: number = 0; "
          "on Tick(cells) { offset = Left.Step(); } }" }
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
        { "libraries/Store.chibi",
          "library Store { secret: number = 11; "
          "Read :: () -> number { return secret; } }" },
        { "behaviors/Caller.chibi",
          "behavior Caller { offset: number = 0; "
          "on Tick(cells) { offset = Store.secret; } }" }
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

TEST_CASE(modules_independent_catalogs_do_not_supply_missing_or_other_bodies)
{
    const input inputs[] = {
        { "libraries/Store.chibi", "library Store { Read :: () -> number { return 3; } }" },
        { "behaviors/Caller.chibi",
          "behavior Caller { offset: number = 0; "
          "on Tick(cells) { offset = Store.Read(); } }" }
    };
    input other[2] = { inputs[0], inputs[1] };
    other[0].text = "library Store { Read :: () -> number { return 7; } }";
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
        { "libraries/Scoped.chibi",
          "library Scoped { "
          "First :: () { Helper :: () -> number { return 2; } return Helper(); "
          "} "
          "Second :: () { Helper :: () -> number { return 3; } return "
          "Helper(); } }" },
        { "behaviors/Caller.chibi",
          "behavior Caller { offset: number = 0; "
          "on Tick(cells) { offset = Scoped.First() * 10 + Scoped.Second(); } "
          "}" }
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

void test_language_modules()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(modules_scroll_update_matches_inline_local_and_qualified_helpers);
    RUN_TEST_CASE(modules_unqualified_same_spelled_helpers_are_ambiguous);
    RUN_TEST_CASE(modules_duplicate_qualified_helpers_cannot_publish_by_path_order);
    RUN_TEST_CASE(modules_cross_library_cycles_reject_callers_without_generations);
    RUN_TEST_CASE(modules_library_state_is_not_an_exported_value);
    RUN_TEST_CASE(modules_independent_catalogs_do_not_supply_missing_or_other_bodies);
    RUN_TEST_CASE(modules_nested_same_spelled_helpers_stay_in_lexical_scope);
}
