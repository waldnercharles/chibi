#include "language/semantic.h"

#include "language/component.h"
#include "support/array.h"
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
    { .id = 11,
      .name = "Placement",
      .type = { .storage = PIN_STRUCT, .nominal = 11 },
      .fields = fields,
      .field_count = 2,
      .size = 2 * sizeof(float),
      .component = true,
      .component_id = 502,
      .derived = true },
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
    .type_count = 2,
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
TEST_CASE(language_derived_component_is_read_only)
{
    parse("system Follow(placement: Placement, position: Position) { position.x = placement.x; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse("system Follow(placement: Placement) { placement.x = 1; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && diagnostic("Assignment requires a writable value"));
    parse("behavior Follow { on Tick(dt) { self.Placement.y = dt; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && diagnostic("Assignment requires a writable value"));
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
    parse("system Motion(position: Position) { action sum = VectorAdd(position, position); sum(); mutate(); }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("sum returns a new value and changes nothing; assign its result"));
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
TEST_CASE(language_inferred_return_of_several_results_is_rejected)
{
    parse(
        "library Tuples { function Pair() -> (a: double, b: double) { "
        "return(1, 2) } "
        "function Both() { return Pair() } }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic(
        "This call has several results; return the ones you need by name, as "
        "in "
        "return(Hit: r.Hit, Distance: r.Distance)"
    ));
    parse(
        "library Tuples { function Pair() -> (a: double, b: double) { "
        "return(1, 2) } "
        "function First() { return Pair().a } }"
    );
    REQUIRE(analysis->valid);
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

static script_diagnostic *component_diagnostics;
/* Reads the component `source` declares; diagnostics stay until the next read. */
static script_component *read_component(const char *source)
{
    script_syntax_free(syntax);
    afree(component_diagnostics);
    syntax = script_parse(source);
    return script_component_read(syntax, &catalog, &component_diagnostics);
}
static const script_diagnostic *component_diagnostic(const char *message)
{
    for (int i = 0; i < acount(component_diagnostics); i++) {
        if (!strcmp(component_diagnostics[i].message, message)) {
            return &component_diagnostics[i];
        }
    }
    return NULL;
}
static const script_component_field *component_field(const script_component *c, const char *name)
{
    for (int i = 0; i < acount(c->fields); i++) {
        if (!strcmp(c->fields[i].name, name)) {
            return &c->fields[i];
        }
    }
    return NULL;
}
static bool field_is(const script_component_field *f, const char *type, const char *unit, double min, double max)
{
    return f && !strcmp(f->type, type) &&
           (unit ? f->unit && !strcmp(f->unit, unit) : !f->unit) &&
           f->min == min && f->max == max;
}
TEST_CASE(language_component_declares_measured_fields)
{
    script_component *c = read_component(
        "// Something fired.\n"
        "// Twice described.\n"
        "component Projectile {\n"
        "    // How fast.\n"
        "    speed: 0..600 u/s\n"
        "    lifetime: s // not a description\n"
        "    accel: u/s^2\n"
        "\n"
        "    // detached\n"
        "\n"
        "    friction: 0..1\n"
        "    //\r\n"
        "    // Turns.\r\n"
        "    drift: -1.5..2 rad /* per */ / s\n"
        "    rate: banana/basket; count: 0..12 banana\n"
        "    level: float\n"
        "    at: Position\n"
        "}\n"
    );
    REQUIRE(c && !component_diagnostics && acount(c->fields) == 9);
    REQUIRE(!strcmp(c->name, "Projectile"));
    REQUIRE(!strcmp(c->description, "Something fired.\nTwice described."));
    REQUIRE(field_is(component_field(c, "speed"), "number", "u/s", 0, 600));
    REQUIRE(!strcmp(component_field(c, "speed")->description, "How fast."));
    REQUIRE(field_is(component_field(c, "lifetime"), "number", "s", 0, 0));
    REQUIRE(!component_field(c, "accel")->description);
    REQUIRE(field_is(component_field(c, "accel"), "number", "u/s²", 0, 0));
    REQUIRE(!component_field(c, "friction")->description);
    REQUIRE(field_is(component_field(c, "friction"), "number", NULL, 0, 1));
    REQUIRE(field_is(component_field(c, "drift"), "number", "rad/s", -1.5, 2));
    REQUIRE(!strcmp(component_field(c, "drift")->description, "\nTurns."));
    REQUIRE(field_is(component_field(c, "rate"), "number", "banana/basket", 0, 0));
    REQUIRE(field_is(component_field(c, "count"), "number", "banana", 0, 12));
    REQUIRE(field_is(component_field(c, "level"), "float", NULL, 0, 0));
    REQUIRE(field_is(component_field(c, "at"), "Position", NULL, 0, 0));
    script_component_free(c);
    /* Unit names stay free names outside a field's type, and executable
     * source cannot declare a component. */
    parse("system S {\n    if true {\n        s := 2\n        u := s * 3\n        component := u\n    }\n}\n");
    REQUIRE(analysis->valid);
    parse("component C {\n    a: float\n}\n");
    REQUIRE(diagnostic("Expected behavior, library or system"));
    return true;
}
TEST_CASE(language_component_rejects_invalid_fields)
{
    REQUIRE(!read_component(
        "component Bad {\n"
        "    a: 1..0\n"
        "    b: flaot\n"
        "    c: u/s^4\n"
        "    a: float\n"
        "    d: float[]\n"
        "    e: 0..1 float\n"
        "}\n"
    ));
    REQUIRE(component_diagnostic("A range goes from a smaller finite number to a larger one"));
    REQUIRE(component_diagnostic("Unknown type or unit 'flaot'")->range.line == 3);
    REQUIRE(component_diagnostic("A unit's power is ^2 or ^3"));
    REQUIRE(component_diagnostic("Field 'a' is declared twice"));
    REQUIRE(component_diagnostic("A component field holds a number, a scalar or a struct"));
    REQUIRE(component_diagnostic("'float' is a type, not a unit")->range.line == 7);
    REQUIRE(acount(component_diagnostics) == 6);
    REQUIRE(!read_component("component Empty {\n}\n"));
    REQUIRE(component_diagnostic("A component declares at least one field"));
    REQUIRE(!read_component("component \"Quoted\" {\n    a: float\n}\n"));
    REQUIRE(acount(component_diagnostics) == 1);
    REQUIRE(component_diagnostic("A component and its fields are named by identifiers"));
    REQUIRE(!read_component("system S {\n}\n"));
    REQUIRE(acount(component_diagnostics) == 1);
    REQUIRE(component_diagnostic("A component source declares one component"));
    /* A reserved word is not a field name; the syntax reports it once. */
    REQUIRE(!read_component("component C {\n    state: float\n}\n"));
    REQUIRE(acount(component_diagnostics) == syntax->diagnostic_count);
    afree(component_diagnostics);
    return true;
}

void test_language_semantic()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_shadowing_resolves_nearest_declaration);
    RUN_TEST_CASE(language_optional_component_requires_presence_guard);
    RUN_TEST_CASE(language_derived_component_is_read_only);
    RUN_TEST_CASE(language_numeric_literal_storage_limits);
    RUN_TEST_CASE(language_vector_operator_resolves_catalog_identity);
    RUN_TEST_CASE(language_pure_call_chain_rejects_effects);
    RUN_TEST_CASE(language_discarded_pure_result_rejected);
    RUN_TEST_CASE(language_binding_cycle_rejected);
    RUN_TEST_CASE(language_inferred_return_of_several_results_is_rejected);
    RUN_TEST_CASE(language_invalid_source_retains_bounded_diagnostics);
    RUN_TEST_CASE(language_component_declares_measured_fields);
    RUN_TEST_CASE(language_component_rejects_invalid_fields);
    pu_clear_setup();
}
