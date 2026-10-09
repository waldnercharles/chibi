#include "language/semantic.h"

#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

static script_syntax *syntax;
static script_analysis *analysis;
static const script_catalog_parameter tick[] = { { .name = "dt",
                                                   .type = { .storage = PIN_F32 } } };
static const script_catalog_field fields[] = {
    { .id = 20, .name = "x", .type = { .storage = PIN_F32 }, .writable = true },
    { .id = 21, .name = "y", .type = { .storage = PIN_F32 }, .offset = sizeof(float), .writable = true },
};
static const script_catalog_type types[] = {
    { .id = 10,
      .name = "Position",
      .type = { .storage = PIN_STRUCT, .nominal = 10 },
      .fields = fields,
      .field_count = 2,
      .size = 2 * sizeof(float),
      .component = true,
      .component_id = 501 },
};
static const script_catalog_parameter operands[] = {
    { .name = "a", .type = { .storage = PIN_STRUCT, .nominal = 10 } },
    { .name = "b", .type = { .storage = PIN_STRUCT, .nominal = 10 } },
};
static const script_catalog_parameter vector_result[] = {
    { .name = "value", .type = { .storage = PIN_STRUCT, .nominal = 10 } }
};
static const script_catalog_capability capabilities[] = {
    { .id = 30,
      .name = "Tick",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = tick,
      .parameter_count = 1 },
    { .id = 31,
      .name = "VectorAdd",
      .operator_name = "+",
      .parameters = operands,
      .parameter_count = 2,
      .results = vector_result,
      .result_count = 1,
      .pure = true },
    { .id = 32, .name = "mutate", .effects = SCRIPT_EFFECT_WRITE },
};
static const script_catalog catalog = {
    .types = types,
    .type_count = 1,
    .capabilities = capabilities,
    .capability_count = 3,
    .entity_type = 40,
    .tick_event = 30,
};
static void cleanup()
{
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
}
static void parse(const char *source)
{
    cleanup();
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "semantic %d:%d: %s\n",
            analysis->diagnostics[i].range.line,
            analysis->diagnostics[i].range.column,
            analysis->diagnostics[i].message
        );
    }
}
static const script_diagnostic *diagnostic(const char *message)
{
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        if (!strcmp(analysis->diagnostics[i].message, message)) {
            return &analysis->diagnostics[i];
        }
    }
    return NULL;
}
TEST_CASE(language_shadowing_resolves_nearest_declaration)
{
    const char *source = "behavior Scope {\nsample := 1;\non Tick(sample) { "
                         "copy: float = sample; }\n}";
    parse(source);
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    int parameter = -1, state = -1;
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (!strcmp(analysis->symbols[i].name, "sample")) {
            if (analysis->symbols[i].kind == SCRIPT_PARAMETER) {
                parameter = i;
            }
            if (analysis->symbols[i].kind == SCRIPT_STATE) {
                state = i;
            }
        }
    }
    REQUIRE(parameter >= 0 && state >= 0 && parameter != state);
    const script_use *use = script_use_at(analysis, (int)(strstr(source, "= sample") - source) + 2);
    REQUIRE(use && use->symbol == parameter);
    REQUIRE(analysis->symbols[state].type.storage == PIN_F64);
    return true;
}
TEST_CASE(language_optional_component_requires_presence_guard)
{
    parse("system Motion(position: Position?) { position.x = 1; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    const script_diagnostic *error = diagnostic(
        "Optional component requires a presence guard before member access"
    );
    REQUIRE(error && error->range.start == 46 && error->range.end == 47);
    parse("system Motion(position: Position?) { if (position) { position.x = 1; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_numeric_literal_storage_limits)
{
    parse("behavior Limits { sample: int8 = 128; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    const script_diagnostic *error = diagnostic("Numeric literal is outside the range of int8");
    REQUIRE(error && error->range.start == 33 && error->range.end == 36);
    parse("behavior Limits { sample: int64 = 9223372036854775807; }");
    REQUIRE(analysis->valid);
    bool found = false;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        if (expression->kind == SCRIPT_EXPR_CONSTANT &&
            expression->type.storage == PIN_I64) {
            REQUIRE(expression->constant.i64 == INT64_MAX);
            found = true;
        }
    }
    REQUIRE(found);
    return true;
}
TEST_CASE(language_vector_operator_resolves_catalog_identity)
{
    parse("system Motion(position: Position) { position = position + position; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    bool found = false;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        if (expression->kind == SCRIPT_EXPR_CALL && expression->capability == 31) {
            REQUIRE(expression->type.nominal == 10 && expression->argument_count == 2);
            found = true;
        }
    }
    REQUIRE(found);
    return true;
}
TEST_CASE(language_pure_call_chain_rejects_effects)
{
    parse("library Effects { function Write() -> () { mutate(); } pure function Read() -> () { Write(); } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    bool found = false;
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        found |= strstr(analysis->diagnostics[i].message, "Pure function") != NULL;
    }
    REQUIRE(found);
    parse("library Effects { pure function Read() -> (output: int) { return 1; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_discarded_pure_result_rejected)
{
    parse("system Motion(position: Position) { VectorAdd(position, position); mutate(); }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("VectorAdd returns a new value and changes nothing; assign its result"));
    parse("library Values { function One() -> (count: int) { return 1; } function Use() -> () { One(); } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse("system Motion(position: Position) { position = VectorAdd(position, position); mutate(); }");
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_binding_cycle_rejected)
{
    parse("behavior Bindings { bind self.Position.x = self.Position.y; bind self.Position.y = self.Position.x; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Binding dependency cycle"));
    parse("behavior Bindings { bind self.Position.y = self.Position.x; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    REQUIRE(analysis->binding_count == 1);
    return true;
}
TEST_CASE(language_invalid_source_retains_bounded_diagnostics)
{
    const char *source = "behavior Broken { on Tick { sample := ; }";
    parse(source);
    REQUIRE(syntax->diagnostic_count > 0);
    REQUIRE(!analysis->valid);
    REQUIRE(analysis->diagnostic_count >= syntax->diagnostic_count);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        script_range range = analysis->diagnostics[i].range;
        REQUIRE(range.start >= 0 && range.start <= range.end && range.end <= (int)strlen(source));
    }
    return true;
}

void test_language_semantic()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_shadowing_resolves_nearest_declaration);
    RUN_TEST_CASE(language_optional_component_requires_presence_guard);
    RUN_TEST_CASE(language_numeric_literal_storage_limits);
    RUN_TEST_CASE(language_vector_operator_resolves_catalog_identity);
    RUN_TEST_CASE(language_pure_call_chain_rejects_effects);
    RUN_TEST_CASE(language_discarded_pure_result_rejected);
    RUN_TEST_CASE(language_binding_cycle_rejected);
    RUN_TEST_CASE(language_invalid_source_retains_bounded_diagnostics);
    pu_clear_setup();
}
