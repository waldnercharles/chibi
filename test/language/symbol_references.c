#include "language/semantic.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

static const script_catalog_parameter prototype_input = {
    .name = "prototype",
    .type = { .storage = PIN_NAME },
    .reference_kind = SCRIPT_REFERENCE_PROTOTYPE
};
static const script_catalog_parameter component_input = {
    .name = "component",
    .type = { .storage = PIN_NAME },
    .reference_kind = SCRIPT_REFERENCE_COMPONENT
};
static const script_catalog_parameter relationship_input = {
    .name = "relationship",
    .type = { .storage = PIN_U64 },
    .reference_kind = SCRIPT_REFERENCE_RELATIONSHIP
};
static const script_catalog_capability capabilities[] = {
    { .id = 1, .name = "Enemy", .kind = SCRIPT_CAPABILITY_PROTOTYPE },
    { .id = 2, .name = "spawn_prototype", .parameters = &prototype_input, .parameter_count = 1 },
    { .id = 3, .name = "component_use", .parameters = &component_input, .parameter_count = 1 },
    { .id = 4, .name = "relationship_use", .parameters = &relationship_input, .parameter_count = 1 },
};
static const script_catalog_type health = {
    .id = 11,
    .name = "Health",
    .component = true,
    .type = { .storage = PIN_I32, .nominal = 11 },
};
static const script_catalog_relationship targets = { .id = 12, .name = "Targets" };
static const script_catalog catalog = {
    .types = &health,
    .type_count = 1,
    .capabilities = capabilities,
    .capability_count = sizeof capabilities / sizeof *capabilities,
    .relationships = &targets,
    .relationship_count = 1,
};
static script_syntax *syntax;
static script_analysis *analysis;

static void cleanup()
{
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
}
static bool analyze(const char *text)
{
    cleanup();
    syntax = script_parse(text);
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "symbol reference: %s\n", analysis->diagnostics[i].message);
    }
    return !syntax->diagnostic_count && analysis->valid;
}
TEST_CASE(symbol_references_infer_and_pass_through_typed_parameters)
{
    REQUIRE(analyze(
        "References :: library { "
        "Forward :: (asset: Prototype) -> Prototype { return asset; } "
        "Run :: () { asset := Enemy; component := Health; relation := Targets; "
        "spawn_prototype(Forward(asset)); component_use(component); "
        "relationship_use(relation); } }"
    ));
    const char *names[] = { "asset", "component", "relation" };
    script_reference_kind kinds[] = { SCRIPT_REFERENCE_PROTOTYPE,
                                      SCRIPT_REFERENCE_COMPONENT,
                                      SCRIPT_REFERENCE_RELATIONSHIP };
    for (int n = 0; n < 3; n++) {
        bool found = false;
        for (int i = 0; i < analysis->symbol_count; i++) {
            const script_symbol *symbol = &analysis->symbols[i];
            if (symbol->kind == SCRIPT_LOCAL && !strcmp(symbol->name, names[n])) {
                REQUIRE(script_reference_kind_for_type(symbol->type) == kinds[n]);
                found = true;
            }
        }
        REQUIRE(found);
    }
    return true;
}
TEST_CASE(symbol_references_reject_strings_numbers_and_wrong_categories)
{
    const char *invalid[] = {
        "References :: library { Run :: () { spawn_prototype(\"Enemy\"); } }",
        "References :: library { Run :: () { name := \"Enemy\"; "
        "spawn_prototype(name); } }",
        "References :: library { Run :: () { component_use(Enemy); } }",
        "References :: library { Run :: () { spawn_prototype(Health); } }",
        "References :: library { Run :: () { relationship_use(12); } }",
        "References :: library { Run :: () { relationship_use(\"Targets\"); } "
        "}",
        "References :: library { Run :: () { spawn_prototype(null); } }",
        "References :: library { Run :: () { asset: Prototype = null; } }",
        "References :: library { Run :: () { relation: Relationship = null; } "
        "}",
        "References :: library { Run :: () { selected: Component = nil; } }",
        "References :: library { Run :: (asset: string) { "
        "spawn_prototype(asset); } }",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        REQUIRE(!analyze(invalid[i]));
        REQUIRE(!syntax->diagnostic_count);
    }
    return true;
}
void test_language_symbol_references()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(symbol_references_infer_and_pass_through_typed_parameters);
    RUN_TEST_CASE(symbol_references_reject_strings_numbers_and_wrong_categories);
    pu_clear_setup();
}
