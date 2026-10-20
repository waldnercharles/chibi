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
    { .id = 33, .name = "opaque_host", .effects = SCRIPT_EFFECT_HOST },
};
static const script_catalog catalog = {
    .types = types,
    .type_count = 2,
    .capabilities = capabilities,
    .capability_count = (int)(sizeof capabilities / sizeof *capabilities),
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
    const char *source = "Scope :: behavior {\nsample := 1;\non Tick(sample) { "
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
TEST_CASE(language_authored_map_intrinsic_names_preserve_callable_identity)
{
    parse(
        "Names :: library { map_has :: (value: bool) -> bool { return value; } "
        "map_remove :: (value: int) -> int { return value + 1; } "
        "Use :: () -> int { if map_has(value: true) { return map_remove(value: "
        "2); } return 0; } }"
    );
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    int calls = 0;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        REQUIRE(expression->kind != SCRIPT_EXPR_MAP_HAS && expression->kind != SCRIPT_EXPR_MAP_REMOVE);
        if (expression->kind == SCRIPT_EXPR_CALL) {
            REQUIRE(expression->right >= 0);
            calls++;
        }
    }
    REQUIRE(calls == 2);
    return true;
}

TEST_CASE(language_inferred_map_requires_an_initializer)
{
    parse("Names :: behavior { scores: retain Map; }");
    REQUIRE(!syntax->diagnostic_count && !analysis->valid);
    REQUIRE(diagnostic("'scores' requires a default value"));
    const char *invalid[] = {
        "Names :: behavior { scores: retain Map = 1; }",
        "Names :: library { Run :: () { scores: Map = [1, 2]; } }",
        "Names :: library { Run :: () { scores: Map = null; } }",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        parse(invalid[i]);
        REQUIRE(!syntax->diagnostic_count && !analysis->valid);
        REQUIRE(diagnostic("Map inference requires an associative map value"));
    }
    return true;
}

TEST_CASE(language_retained_bindings_preserve_owner_and_value_type)
{
    parse(
        "Counters :: system() { implicit := 0; visits: retain int = 0; "
        "inferred: retain = true; total: retain int = 0; "
        "Run :: () { calls: retain int = 0; local := 0; "
        "shared_calls: retain = true; calls += 1; "
        "visits += calls; } Run(); }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    int checked = 0;
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (!strcmp(symbol->name, "visits") || !strcmp(symbol->name, "calls")) {
            REQUIRE(symbol->kind == SCRIPT_SHARED_STATE);
            REQUIRE(symbol->type.storage == PIN_I32);
            REQUIRE(symbol->type.flags == 0);
            checked++;
        } else if (!strcmp(symbol->name, "implicit")) {
            REQUIRE(symbol->kind == SCRIPT_LOCAL);
            checked++;
        } else if (!strcmp(symbol->name, "inferred")) {
            REQUIRE(symbol->kind == SCRIPT_SHARED_STATE);
            REQUIRE(symbol->type.storage == PIN_BOOL);
            checked++;
        } else if (!strcmp(symbol->name, "total")) {
            REQUIRE(symbol->kind == SCRIPT_SHARED_STATE);
            REQUIRE(symbol->type.storage == PIN_I32);
            checked++;
        } else if (!strcmp(symbol->name, "shared_calls")) {
            REQUIRE(symbol->kind == SCRIPT_SHARED_STATE);
            REQUIRE(symbol->type.storage == PIN_BOOL);
            checked++;
        } else if (!strcmp(symbol->name, "local")) {
            REQUIRE(symbol->kind == SCRIPT_LOCAL);
            checked++;
        }
    }
    REQUIRE(checked == 7);
    return true;
}

TEST_CASE(language_system_retention_rejects_non_system_owners_and_tasks)
{
    const char *invalid[] = {
        "Bad :: behavior { total: retain(system) int = 0; }",
        "Bad :: library { Run :: () { total: retain(system) = 0; } }",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        parse(invalid[i]);
        REQUIRE(syntax->diagnostic_count > 0);
        REQUIRE(!analysis->valid);
    }
    const char *tasks[] = {
        "Bad :: system() { pending: retain task; }",
        "Bad :: system() { pending: retain task[]; }",
        "Bad :: system() { pending: retain Map<string, task>; }",
        "Bad :: system() { Work :: () { pending: retain task; } Work(); }",
    };
    for (size_t i = 0; i < sizeof tasks / sizeof *tasks; i++) {
        parse(tasks[i]);
        REQUIRE(syntax->diagnostic_count == 0);
        REQUIRE(!analysis->valid);
        REQUIRE(
            diagnostic("System-owned retained bindings cannot contain tasks; keep task handles in coroutine locals")
        );
    }
    parse("Defaults :: behavior { pending: retain task; target: retain entity; items: retain int[]; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    return true;
}

TEST_CASE(language_system_invocation_locals_cross_calls_only_as_arguments)
{
    parse(
        "Motion :: system(position: Position) { step := position.x; "
        "Apply :: (value: float) { position.y = value; } Apply(step); }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse(
        "Motion :: system(position: Position) { step := position.x; "
        "Apply :: () { position.y = step; } Apply(); }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Pass system invocation locals as parameters across callable boundaries"));
    parse("Motion :: system() { step := 1; on Tick { step += 1; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Pass system invocation locals as parameters across callable boundaries"));
    return true;
}

TEST_CASE(language_optional_component_requires_presence_guard)
{
    parse("Motion :: system(position: Position?) { position.x = 1; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    const script_diagnostic *error = diagnostic(
        "Optional component requires a presence guard before member access"
    );
    int member = (int)(strstr(analysis->text, "position.x") - analysis->text) +
                 (int)strlen("position.");
    REQUIRE(error && error->range.start == member && error->range.end == member + 1);
    parse("Motion :: system(position: Position?) { if (position) { position.x = 1; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse(
        "Motion :: system(position: Position?) { Change :: () { mutate(); } "
        "if position { Change(); position.x = 1; } }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Optional component requires a presence guard before member access"));
    return true;
}
TEST_CASE(language_reductions_resolve_stored_fields_and_membership)
{
    parse(
        "Motion :: system(position: Position?) where has(Position) { "
        "Amount :: () -> float { return 2; } "
        "if position { reduce position.x += Amount(); "
        "reduce min(position.x, 3); reduce max(position.y, 4); } }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    int reductions = 0;
    for (int i = 0; i < analysis->statement_count; i++) {
        const script_statement *s = &analysis->statements[i];
        if (s->kind == SCRIPT_STMT_REDUCE) {
            REQUIRE(analysis->expressions[s->expression].type.storage == PIN_F32);
            REQUIRE(s->reduction == (script_reduction_op)reductions);
            reductions++;
        }
    }
    REQUIRE(reductions == 3);
    parse("Motion :: system(position: Position) { reduce self.Position.x += 1; }");
    REQUIRE(analysis->valid);
    parse("Motion :: system(position: Position?) { if position != nil { reduce position.x += 1; } }");
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_reductions_require_system_component_fields)
{
    const char *invalid_targets[] = {
        "Motion :: system() { total := 0; reduce total += 1; }",
        "Motion :: system() { if true { total := 0; reduce total += 1; } }",
        "Motion :: system(position: Position) { reduce position += position; }",
        "Motion :: system(position: Placement) { reduce position.x += 1; }",
        "Motion :: system(position: Position?) { reduce position.x += 1; }",
    };
    for (int i = 0; i < (int)(sizeof invalid_targets / sizeof *invalid_targets); i++) {
        parse(invalid_targets[i]);
        REQUIRE(syntax->diagnostic_count == 0);
        REQUIRE(!analysis->valid);
    }
    parse("Motion :: behavior { on Tick { reduce self.Position.x += 1; } }");
    REQUIRE(diagnostic("Reduction requires a synchronous system body"));
    parse("Motion :: system(position: Position) { Update :: () { reduce position.x += 1; } Update(); }");
    REQUIRE(diagnostic("Reduction requires a synchronous system body"));
    return true;
}
TEST_CASE(language_reduction_producers_reject_conflicting_component_values)
{
    const char *sources[] = {
        "Motion :: system(position: Position) { reduce position.x += "
        "position.y; "
        "}",
        "Motion :: system(position: Position) { position.y = 0; reduce "
        "position.x "
        "+= 1; }",
        "Motion :: system(position: Position) { if position.y > 0 { reduce "
        "position.x += 1; } }",
        "Motion :: system(position: Position) where position.y > 0 { reduce "
        "position.x += 1; }",
        "Motion :: system(position: Position) where changed(position) { reduce "
        "position.x += 1; }",
        "Motion :: system(position: Position) { if true { snapshot := "
        "position; "
        "reduce position.x += 1; } }",
        "Motion :: system(position: Position) { Read :: () -> float { return "
        "position.y; } reduce position.x += Read(); }",
        "Motion :: system(position: Position) { reduce position.x += 1; "
        "self.Position.y = 2; }",
        "Motion :: system(position: Position) { bind position.y = 2; reduce "
        "position.x += 1; }",
        "Motion :: system(position: Position) { Read :: (n: int) -> float { if "
        "n "
        "> 0 { return Read(n - 1); } return position.y; } reduce position.x += "
        "Read(1); }",
        "Motion :: system(position: Position) { Owner :: () -> entity { "
        "position.y = 1; return self; } reduce Owner().Position.x += 1; }",
    };
    for (int i = 0; i < (int)(sizeof sources / sizeof *sources); i++) {
        parse(sources[i]);
        REQUIRE(syntax->diagnostic_count == 0);
        REQUIRE(
            diagnostic("Reduction producer cannot read or write component 'Position' outside reductions")
        );
    }
    parse("Motion :: system(position: Position) { mutate(); reduce position.x += 1; }");
    REQUIRE(diagnostic("Reduction producer cannot call an opaque component accessor"));
    parse("Motion :: system(position: Position) { opaque_host(); reduce position.x += 1; }");
    REQUIRE(diagnostic("Reduction producer cannot call an opaque component accessor"));
    parse("Motion :: system(position: Position, other: Placement) { reduce position.x += other.y; }");
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_reduction_producers_cannot_suspend_or_start_tasks)
{
    parse("Motion :: system(position: Position) { reduce position.x += 1; yield; }");
    REQUIRE(diagnostic("Reduction requires a synchronous system body without tasks"));
    parse("Motion :: system(position: Position) { Work :: () { yield; } Work(); reduce position.x += 1; }");
    REQUIRE(diagnostic("Reduction requires a synchronous system body without tasks"));
    return true;
}
TEST_CASE(language_unordered_reductions_require_unbounded_isolated_components)
{
    parse("Motion :: system(position: Position) { reduce unordered position.x += 1; reduce unordered position.y += 2; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    int count = 0;
    for (int i = 0; i < analysis->statement_count; i++) {
        if (analysis->statements[i].kind == SCRIPT_STMT_REDUCE) {
            REQUIRE(analysis->statements[i].reduction == SCRIPT_REDUCTION_ADD_UNORDERED);
            count++;
        }
    }
    REQUIRE(count == 2);
    parse("Motion :: system(unordered: Position) { reduce unordered.x += 1; }");
    REQUIRE(analysis->valid);
    parse("Motion :: system(position: Position) { reduce unordered position.x += 1; reduce position.y += 2; }");
    REQUIRE(diagnostic("A component cannot mix ordered and unordered reductions in one system"));
    parse("Motion :: system(position: Position) { reduce min(position.y, 2); reduce unordered position.x += 1; }");
    REQUIRE(diagnostic("A component cannot mix ordered and unordered reductions in one system"));

    script_catalog_field bounded_fields[2];
    memcpy(bounded_fields, fields, sizeof bounded_fields);
    bounded_fields[0].range_contract = true;
    bounded_fields[0].min = 0;
    bounded_fields[0].max = 10;
    script_catalog_type bounded_type = types[0];
    bounded_type.fields = bounded_fields;
    script_catalog bounded = catalog;
    bounded.types = &bounded_type;
    bounded.type_count = 1;
    cleanup();
    syntax = script_parse("Motion :: system(position: Position) { reduce unordered position.x += 1; }");
    analysis = script_analyze(syntax, &bounded);
    REQUIRE(diagnostic("Unordered reduction requires an unbounded component field"));
    cleanup();
    syntax = script_parse("Motion :: system(position: Position) { reduce position.x += 1; }");
    analysis = script_analyze(syntax, &bounded);
    REQUIRE(analysis->valid);
    cleanup();
    bounded_fields[0].range_contract = false;
    syntax = script_parse("Motion :: system(position: Position) { reduce unordered position.x += 1; }");
    analysis = script_analyze(syntax, &bounded);
    REQUIRE(analysis->valid); /* A slider hint is not a stored range contract. */
    return true;
}
TEST_CASE(language_query_header_resolves_boolean_filters_and_sources)
{
    const script_catalog_relationship relationships[] = {
        { .id = 601, .name = "Targets", .exclusive = true },
        { .id = 602, .name = "Likes" },
    };
    script_catalog queries = catalog;
    queries.relationships = relationships;
    queries.relationship_count = 2;
    cleanup();
    syntax = script_parse(
        "Follow :: system(p: Position, aim: Targets.Position?, "
        "up: Placement from ancestor(ChildOf) "
        ") where (Position or Placement) and not self.Placement "
        "{ if aim { p.x = aim.x + up.x; } }"
    );
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && analysis->valid);
    bool found_path = false, found_ancestor = false;
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        found_path |= !strcmp(symbol->name, "aim") &&
                      symbol->source_relationship == 601 &&
                      (symbol->type.flags & SCRIPT_TYPE_OPTIONAL);
        found_ancestor |= !strcmp(symbol->name, "up") && symbol->source_traversal == 1;
    }
    REQUIRE(found_path && found_ancestor && analysis->query >= 0);
    cleanup();
    syntax = script_parse(
        "Follow :: system(target: Targets.Position ) without Position { "
        "target.x += 1; }"
    );
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && analysis->valid);
    parse("Bad :: system(p: Position ) without Position { p.x = 1; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("A component cannot be both a parameter and excluded"));
    parse(
        "Inherit :: system(p: Position from ancestor(ChildOf) ) without "
        "Position { p.x += 1; }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Inherit :: system(p: Position from self_or_ancestor(ChildOf) "
        ") without Position { p.x += 1; }"
    );
    REQUIRE(analysis->valid);
    cleanup();
    syntax = script_parse("Bad :: system(p: Likes.Position ) { p.x = 1; }");
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && !analysis->valid);
    REQUIRE(
        diagnostic("Scalar query path requires an exclusive relationship; iterate multiple targets explicitly")
    );
    parse("Bad :: system(p: Position ) where target.Position { p.x = 1; }");
    REQUIRE(!analysis->valid && diagnostic("Unknown query relationship 'target'"));
    parse("Safe :: system(p: Position? ) { if p { p.x = 1; p.y = 2; } }");
    REQUIRE(analysis->valid);
    parse("Waiting :: system(p: Position? ) { if p { yield; p.x = 1; } }");
    REQUIRE(
        !analysis->valid &&
        diagnostic("Optional component requires a presence guard before member access")
    );
    script_catalog_capability structural_capabilities[sizeof capabilities / sizeof *capabilities];
    memcpy(structural_capabilities, capabilities, sizeof structural_capabilities);
    structural_capabilities[2].effects = SCRIPT_EFFECT_STRUCTURAL;
    queries.capabilities = structural_capabilities;
    queries.capability_count = (int)(sizeof structural_capabilities /
                                     sizeof *structural_capabilities);
    cleanup();
    syntax = script_parse("Changed :: system(p: Position? ) { if p { mutate(); p.x = 1; } }");
    analysis = script_analyze(syntax, &queries);
    REQUIRE(
        !analysis->valid &&
        diagnostic("Optional component requires a presence guard before member access")
    );
    const script_catalog_relationship shadow = { .id = 601,
                                                 .name = "ChildOf",
                                                 .exclusive = true };
    queries.relationships = &shadow;
    queries.relationship_count = 1;
    cleanup();
    syntax = script_parse("Builtin :: system(p: ChildOf.Position ) { for target in self.ChildOf {} }");
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && analysis->valid);
    bool builtin_path = false, builtin_stream = false;
    for (int i = 0; i < analysis->symbol_count; i++) {
        builtin_path |= analysis->symbols[i].source_relationship == SCRIPT_CHILD_OF;
    }
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        builtin_stream |= expression->kind == SCRIPT_EXPR_RELATION &&
                          expression->capability == SCRIPT_CHILD_OF;
    }
    REQUIRE(builtin_path && builtin_stream);
    return true;
}
TEST_CASE(language_query_joins_and_filters_share_source_rules)
{
    const script_catalog_relationship relationships[] = {
        { .id = 601, .name = "Targets", .exclusive = true },
        { .id = 602, .name = "Likes" },
    };
    script_catalog queries = catalog;
    queries.relationships = relationships;
    queries.relationship_count = 2;
    cleanup();
    syntax = script_parse(
        "Follow :: system(p: Position from self "
        ") join aim: Position? from Targets "
        "join up: Placement from ancestor(ChildOf) "
        "where self.Position and not Targets.Placement and "
        "ancestor(ChildOf).Position and has(Likes) "
        "{ if aim { p.x = aim.x + up.x; } }"
    );
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && analysis->valid);
    bool target_filter = false, ancestor_filter = false, existential = false;
    for (int i = 0; i < analysis->query_count; i++) {
        const script_query *term = &analysis->queries[i];
        target_filter |= term->kind == SCRIPT_QUERY_HAS && term->component == 502 &&
                         term->source_relationship == 601 &&
                         term->source_traversal == SCRIPT_SOURCE_SELF;
        ancestor_filter |= term->kind == SCRIPT_QUERY_HAS && term->component == 501 &&
                           term->source_relationship == SCRIPT_CHILD_OF &&
                           term->source_traversal == SCRIPT_SOURCE_ANCESTOR;
        existential |= term->kind == SCRIPT_QUERY_RELATION && term->component == 602;
    }
    REQUIRE(target_filter && ancestor_filter && existential);
    const char *valid[] = {
        "Follow :: system(p: Position ) join aim: Targets.Position { p.x = "
        "aim.x; }",
        "Follow :: system() join aim: Targets.Position? where not "
        "Targets.Placement "
        "{ if aim { aim.x = 1; } }",
        "Follow :: system(p: Position ) where has(Targets.Position) or not "
        "has(Likes) { p.x = 1; }",
        "Follow :: system(p: Position ) where "
        "self_or_ancestor(ChildOf).Placement { p.x = 1; }",
        "Follow :: system(p: Position ) join up: Position from "
        "ancestor(/* parent */ ChildOf /* edge */) where "
        "has(/* any target */ Likes) and "
        "self_or_ancestor(/* provider */ ChildOf).Position { p.x = up.x; }",
    };
    for (size_t i = 0; i < sizeof valid / sizeof *valid; i++) {
        cleanup();
        syntax = script_parse(valid[i]);
        analysis = script_analyze(syntax, &queries);
        REQUIRE(syntax->diagnostic_count == 0 && analysis->valid);
    }
    const char *invalid[] = {
        "Bad :: system() join p: Position {}",
        "Bad :: system() join p: Targets.Position from self {}",
        "Bad :: system() join p: Likes.Position {}",
        "Bad :: system(p: Position ) where Likes.Placement {}",
        "Bad :: system(p: Position ) join aim: Targets.Position? { if aim { "
        "yield; p.x = aim.x; } }",
        "Bad :: system(p: Position ) join aim: Targets.Position? where "
        "Targets.Position { p.x = aim.x; }",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        cleanup();
        syntax = script_parse(invalid[i]);
        analysis = script_analyze(syntax, &queries);
        REQUIRE(syntax->diagnostic_count == 0 && !analysis->valid);
    }
    return true;
}
TEST_CASE(language_query_generators_bind_rows_and_pure_value_filters)
{
    const script_catalog_relationship relationships[] = {
        { .id = 601, .name = "Targets" },
        { .id = 602, .name = "Likes" },
    };
    script_catalog queries = catalog;
    queries.relationships = relationships;
    queries.relationship_count = 2;
    cleanup();
    syntax = script_parse(
        "Follow :: system(p: Position ) join target in self.Targets "
        "join next in target.Likes join aim: Position? from next "
        "where Positive(p.x) and (aim and aim.x > p.x) and changed(aim) "
        "and has(target.Likes) and not has(next.Placement) "
        "{ Positive :: (x: float) -> bool { positive := x > 0; return "
        "positive; } "
        "if aim { p.x = aim.x; } }"
    );
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && analysis->valid);
    REQUIRE(analysis->query_variable_count == 2);
    int generators = 0, predicates = 0;
    bool changed = false, membership = false, optional = false;
    for (int i = 0; i < analysis->query_count; i++) {
        const script_query *query = &analysis->queries[i];
        if (query->kind == SCRIPT_QUERY_GENERATOR) {
            generators++;
            REQUIRE(query->source_variable == generators - 1);
            REQUIRE(query->target_variable == generators);
            REQUIRE(query->component == (uint64_t)(600 + generators));
        }
        predicates += query->kind == SCRIPT_QUERY_VALUE;
        changed |= query->kind == SCRIPT_QUERY_CHANGED &&
                   query->component == 501 && query->source_variable == 2;
        membership |= query->kind == SCRIPT_QUERY_RELATION &&
                      query->component == 602 && query->source_variable == 1;
        REQUIRE(
            !(query->kind == SCRIPT_QUERY_HAS && query->component == 501 &&
              query->source_variable == 2)
        );
    }
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        optional |= !strcmp(symbol->name, "aim") && symbol->source_variable == 2 &&
                    (symbol->type.flags & SCRIPT_TYPE_OPTIONAL);
    }
    REQUIRE(generators == 2 && predicates == 3 && changed && membership && optional);
    return true;
}
TEST_CASE(language_query_filters_reject_effects_and_unbound_variables)
{
    const script_catalog_relationship targets = { .id = 601, .name = "Targets" };
    script_catalog queries = catalog;
    queries.relationships = &targets;
    queries.relationship_count = 1;
    const char *invalid[] = {
        "Bad :: system() join target in later.Targets join later in "
        "self.Targets {}",
        "Bad :: system() join target in target.Targets {}",
        "Bad :: system() join target in self.Unknown {}",
        "Bad :: system() join target in self.Targets join target in "
        "self.Targets {}",
        "Bad :: system() join target in self.Targets join p: Position? from "
        "target "
        "where p.x > 0 {}",
        "Bad :: system() join target in self.Targets join p: Position? from "
        "target "
        "where p or p.x > 0 {}",
        "Bad :: system() join target in self.Targets join p: Position from "
        "target { "
        "bind p.x = 1; }",
        "Bad :: system(p: Position ) where changed(p.x) {}",
        "Bad :: system(p: Position ) where p.x > 0 { query changed(p); }",
        "Bad :: system(p: Position ) { query p.x > 0; }",
        "Bad :: system(p: Position ) where changed(p, p) {}",
        "Bad :: system(p: Position ) where p.x {}",
        "Bad :: system(p: Position ) where Mutating() { Mutating :: () -> "
        "bool { mutate(); return true; } }",
        "Bad :: system(p: Position ) where Deferred() { Deferred :: () -> "
        "bool { yield; return true; } }",
        "Bad :: system(p: Position ) where await Deferred() { "
        "A :: () {} B :: () {} C :: () {} D :: () {} E :: () {} F :: () {} "
        "Deferred :: () -> bool { yield; return true; } }",
        "Bad :: system(p: Position ) where enabled { enabled: retain = true; "
        "}",
        "Bad :: system(p: Position ) where enabled { enabled := true; }",
        "Bad :: system(p: Position ) where ReadLocal() { enabled := true; "
        "ReadLocal :: () -> bool { return enabled; } }",
        "Bad :: system(p: Position ) where ReadState() { enabled: "
        "retain = "
        "true; ReadState :: () -> bool { return enabled; } }",
        "Bad :: system(p: Position ) where ReadState() { enabled: retain = "
        "true; ReadState :: () -> bool { return Inner(); } Inner :: () -> bool "
        "{ return enabled; } }",
        "Bad :: system(p: Position ) where event.dt > 0 {}",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        cleanup();
        syntax = script_parse(invalid[i]);
        analysis = script_analyze(syntax, &queries);
        REQUIRE(syntax->diagnostic_count == 0);
        REQUIRE(!analysis->valid);
    }
    REQUIRE(
        diagnostic("Query predicate cannot read system state, body locals or event payload; test invocation values in the system body")
    );
    cleanup();
    syntax = script_parse(
        "Many :: system() join a in self.Targets join b in a.Targets "
        "join c in b.Targets join d in c.Targets join e in d.Targets "
        "join f in e.Targets join g in f.Targets join h in g.Targets "
        "join i in h.Targets {}"
    );
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && !analysis->valid);
    REQUIRE(diagnostic("Query supports at most eight target variables"));
    return true;
}
TEST_CASE(language_derived_component_is_read_only)
{
    parse("Follow :: system(placement: Placement, position: Position) { position.x = placement.x; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse("Follow :: system(placement: Placement) { placement.x = 1; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && diagnostic("Assignment requires a writable value"));
    parse("Follow :: behavior { on Tick(dt) { self.Placement.y = dt; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && diagnostic("Assignment requires a writable value"));
    return true;
}
TEST_CASE(language_numeric_literal_storage_limits)
{
    parse("Limits :: behavior { sample: int8 = 128; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    const script_diagnostic *error = diagnostic("Numeric literal is outside the range of int8");
    int literal = (int)(strstr(analysis->text, "128") - analysis->text);
    REQUIRE(error && error->range.start == literal && error->range.end == literal + 3);
    parse("Limits :: behavior { sample: int64 = 9223372036854775807; }");
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
static const script_expression *constant_of(pin_type storage)
{
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        if (expression->kind == SCRIPT_EXPR_CONSTANT &&
            expression->type.storage == storage) {
            return expression;
        }
    }
    return NULL;
}
static const script_expression *expression_of(script_expression_kind kind, pin_type storage)
{
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        if (expression->kind == kind && expression->type.storage == storage) {
            return expression;
        }
    }
    return NULL;
}
static bool converts(const char *body, behavior_cast_mode mode)
{
    char source[256];
    snprintf(source, sizeof source, "L :: library { F :: (x: int) -> byte { return %s; } }", body);
    parse(source);
    const script_expression *conversion = expression_of(SCRIPT_EXPR_CAST, PIN_U8);
    return analysis->valid && conversion && conversion->conversion == mode;
}
TEST_CASE(language_numeric_conversions_check_modes_and_constants)
{
    REQUIRE(converts("byte(x)", BEHAVIOR_CAST_EXACT));
    REQUIRE(converts("byte(wrap: x)", BEHAVIOR_CAST_WRAP));
    REQUIRE(converts("byte(clamp: x)", BEHAVIOR_CAST_CLAMP));
    REQUIRE(converts("byte(wrap: 300)", BEHAVIOR_CAST_WRAP));
    parse("L :: library { X: int : 300; F :: () -> byte { return byte(X + 1); } }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Constant does not fit in byte; use byte(wrap: value) or byte(clamp: value)"));
    parse("L :: library { X: int : 200; F :: () -> byte { return byte(X); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (x: int) -> byte { return byte(round: x); } }");
    REQUIRE(diagnostic("A conversion argument may only be named wrap or clamp"));
    parse("L :: library { F :: (x: float) -> int { return int(wrap: x); } }");
    REQUIRE(diagnostic("wrap and clamp convert between integer types"));
    parse("L :: library { F :: () -> int { return int(wrap: 3.5); } }");
    REQUIRE(
        analysis->diagnostic_count == 1 &&
        diagnostic("wrap and clamp convert between integer types")
    );
    parse("L :: library { F :: () -> uint64 { return uint64(clamp: 18446744073709551615); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (x: int) -> byte { return byte(x, x); } }");
    REQUIRE(diagnostic("Conversion takes one value"));
    parse("L :: library { F :: (x: int) -> int { return cast<int>(x); } }");
    REQUIRE(diagnostic("cast<T>(value) was removed; write T(value)"));
    return true;
}
TEST_CASE(language_integers_widen_implicitly_and_narrow_explicitly)
{
    parse("L :: library { F :: (x: uint16) -> uint { return x; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (x: byte) -> float { return x; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (x: int) -> byte { return x; } }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Incompatible types: expected byte, got int; convert with byte(value)"));
    parse("L :: library { F :: (x: int, y: uint) -> int64 { return x + y; } }");
    REQUIRE(analysis->valid && expression_of(SCRIPT_EXPR_BINARY, PIN_I64));
    parse("L :: library { F :: (x: byte, y: int8) -> int { return x + y; } }");
    REQUIRE(analysis->valid && expression_of(SCRIPT_EXPR_BINARY, PIN_I32));
    parse("L :: library { F :: (x: int64, y: uint64) -> int64 { return x + y; } }");
    REQUIRE(diagnostic("int64 and uint64 operands need an explicit conversion, such as int64(value)"));
    parse("L :: library { Mask: uint : 0xFF; F :: () -> uint { return Mask; } }");
    REQUIRE(analysis->valid && constant_of(PIN_U32));
    parse("L :: library { Small: byte : 300; }");
    REQUIRE(diagnostic("Numeric literal is outside the range of byte"));
    return true;
}
/* The value of the one character constant the last parse produced. */
static bool character(pin_type storage, int value, bool is_char)
{
    const script_expression *constant = constant_of(storage);
    return analysis->valid && constant &&
           script_type_char(constant->type) == is_char &&
           (storage == PIN_U8 ? constant->constant.u8 : constant->constant.i) == value;
}
TEST_CASE(language_character_literals_take_their_type_from_context)
{
    parse("L :: library { F :: (c: byte) -> bool { return c == '/'; } }");
    REQUIRE(character(PIN_U8, '/', false));
    parse("L :: library { F :: (c: int) -> bool { return c >= 'A' && c <= 'Z'; } }");
    REQUIRE(analysis->valid && !constant_of(PIN_F64));
    parse("L :: library { Slash: byte : 'é'; }");
    REQUIRE(character(PIN_U8, 233, false));
    parse("L :: library { Euro: byte : '€'; }");
    REQUIRE(diagnostic("Character literal is outside the range of byte"));
    parse("L :: library { F :: () -> char { c := 'a'; return c; } }");
    REQUIRE(character(PIN_I32, 'a', true));
    parse("L :: library { F :: () -> char { return '🐈'; } }");
    REQUIRE(character(PIN_I32, 0x1F408, true));
    parse("L :: library { F :: () -> char { return '\\uD83D\\uDC08'; } }");
    REQUIRE(character(PIN_I32, 0x1F408, true));
    parse("L :: library { F :: () -> char { return '\\''; } }");
    REQUIRE(character(PIN_I32, '\'', true));
    parse("L :: library { F :: () -> char { return '\"'; } }");
    REQUIRE(character(PIN_I32, '"', true));
    parse("L :: library { F :: () -> char { return '\\n'; } }");
    REQUIRE(character(PIN_I32, '\n', true));
    parse("L :: library { F :: () -> char { return '\\u0000'; } }");
    REQUIRE(character(PIN_I32, 0, true));
    parse("L :: library { F :: () -> char { return ''; } }");
    REQUIRE(diagnostic("A character literal holds exactly one character"));
    parse("L :: library { F :: () -> char { return 'ab'; } }");
    REQUIRE(diagnostic("A character literal holds exactly one character"));
    parse("L :: library { F :: () -> char { return '\\q'; } }");
    REQUIRE(diagnostic("Invalid character literal"));
    parse("L :: library { F :: () -> char { return '\\uD800'; } }");
    REQUIRE(diagnostic("Invalid character literal"));
    parse("L :: library { F :: () -> string { return 'a'; } }");
    REQUIRE(!analysis->valid);
    parse("L :: library { F :: () -> double { return 'a'; } }");
    REQUIRE(diagnostic("A character literal needs an integer or char type"));
    return true;
}
TEST_CASE(language_char_widens_to_int_and_narrows_explicitly)
{
    parse("L :: library { F :: (c: char) -> int { return c; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char) -> int64 { return c; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char) -> byte { return c; } }");
    REQUIRE(diagnostic("Incompatible types: expected byte, got char; convert with byte(value)"));
    parse("L :: library { F :: (n: int) -> char { return n; } }");
    REQUIRE(diagnostic("Incompatible types: expected char, got int; convert with char(value)"));
    parse("L :: library { F :: (n: int) -> char { return char(n); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char) -> byte { return byte(c); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (n: int) -> char { return char(wrap: n); } }");
    REQUIRE(diagnostic("char converts only with char(value)"));
    parse("L :: library { F :: () -> char { return char(0xD800); } }");
    REQUIRE(diagnostic("Constant does not fit in char"));
    parse("L :: library { F :: () -> char { return char(0x10FFFF); } }");
    REQUIRE(analysis->valid);
    /* char arithmetic stays char; with an int it widens to int. */
    parse("L :: library { F :: (c: char) -> char { return c + 1; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char, n: int) -> int { return c + n; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char, n: int) -> char { return c + n; } }");
    REQUIRE(diagnostic("Incompatible types: expected char, got int; convert with char(value)"));
    parse("L :: library { F :: () -> char { return 'a' + 1; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char) -> int { return c - '0'; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (d: double) -> char { return char(d); } }");
    REQUIRE(diagnostic("char converts from an integer"));
    /* A compile-time call evaluates its character literals. */
    parse(
        "L :: library { Code :: () -> int { return 'a'; } Folded :: Code(); "
        "F :: () -> bool { return Folded == 97; } }"
    );
    REQUIRE(analysis->valid);
    /* A char type argument specializes apart from int. */
    parse(
        "L :: library { Box :: struct($T: Type) { value: T } "
        "A :: () -> Box(char) { return {value: 'a'}; } "
        "B :: () -> Box(int) { return A(); } }"
    );
    REQUIRE(!analysis->valid);
    return true;
}
/* Ranges never count down: `for <` reverses a range or an array instead. */
TEST_CASE(language_loops_reverse_explicitly)
{
    parse("L :: library { F :: (n: int) { for < i in 0..<n {} for i in 0..<n {} for < x, i in [1, 2] {} } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: () { for i in 10..0 {} } }");
    REQUIRE(diagnostic("Range bounds descend; count down with for <"));
    parse("L :: library { Top :: 10; F :: () { for < i in Top..<1 {} } }");
    REQUIRE(diagnostic("Range bounds descend; for < takes the low bound first, as in for < i in 0..10"));
    parse("L :: library { F :: () { for < c in \"ab\" {} } }");
    REQUIRE(diagnostic("for < reverses only ranges and arrays"));
    parse("L :: library { F :: (m: Map<string, int>) { for < k, v in m {} } }");
    REQUIRE(diagnostic("for < reverses only ranges and arrays"));
    return true;
}
TEST_CASE(language_hex_literal_fills_integer_storage)
{
    parse("Colors :: behavior { color: uint = 0xf7d308ff; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_U32) && constant_of(PIN_U32)->constant.u == 0xf7d308ffu);
    parse("Colors :: behavior { color: uint = 0xef; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_U32) && constant_of(PIN_U32)->constant.u == 0xef);
    parse("Colors :: behavior { mask: uint64 = 0xffffffffffffffff; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_U64) && constant_of(PIN_U64)->constant.u64 == UINT64_MAX);
    parse("Colors :: behavior { mask: int = -0x10; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_I32) && constant_of(PIN_I32)->constant.i == -16);
    parse("Colors :: behavior { mask: int8 = 0xff; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of int8"));
    parse("Limits :: behavior { mask: int64 = 0x7fffffffffffffff; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_I64) && constant_of(PIN_I64)->constant.i64 == INT64_MAX);
    parse("Limits :: behavior { mask: int64 = -0x8000000000000000; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_I64) && constant_of(PIN_I64)->constant.i64 == INT64_MIN);
    parse("Limits :: behavior { mask: int64 = 0x8000000000000000; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of int64"));
    parse("Limits :: behavior { mask: int64 = -0x8000000000000001; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of int64"));
    parse("Limits :: behavior { mask: uint64 = 0x10000000000000000; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of uint64"));
    parse("Limits :: behavior { mask: uint64 = -0x1; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of uint64"));
    parse("Limits :: behavior { count: int = 1e2; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_I32) && constant_of(PIN_I32)->constant.i == 100);
    return true;
}
TEST_CASE(language_vector_operator_resolves_catalog_identity)
{
    parse("Motion :: system(position: Position) { position = position + position; }");
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
TEST_CASE(language_call_chains_infer_effects_and_purity)
{
    parse("Effects :: library { Write :: () -> () { mutate(); } Read :: () -> () { Write(); } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    REQUIRE(analysis->function_count == 2);
    for (int i = 0; i < analysis->function_count; i++) {
        REQUIRE(!analysis->functions[i].pure);
        REQUIRE(analysis->functions[i].effects & SCRIPT_EFFECT_WRITE);
    }
    parse("Effects :: library { Read :: () -> (output: int) { return 1; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    REQUIRE(analysis->functions[0].pure);
    return true;
}
TEST_CASE(language_owned_record_and_index_writes_stay_pure)
{
    parse(
        "Effects :: library { Row :: struct { value: int; }; "
        "Run :: () -> int { row: Row = {value: 1}; "
        "row.value = 2; row.value += 3; rows: Row[] = [row]; "
        "rows[0].value += 4; values: int[] = [1]; values[0] = 2; "
        "return rows[0].value + values[0]; } }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    REQUIRE(analysis->function_count == 1);
    REQUIRE(!(analysis->functions[0].effects & SCRIPT_EFFECT_WRITE));
    REQUIRE(analysis->functions[0].pure);
    return true;
}
TEST_CASE(language_external_record_and_index_writes_are_effects)
{
    const char *sources[] = {
        "Effects :: library { Run :: (value: ref int) { value = 2; } }",
        "Effects :: library { Row :: struct { value: int; }; "
        "Run :: (row: ref Row) { row.value = 2; } }",
        "Effects :: library { Row :: struct { value: int; }; "
        "Run :: (rows: ref Row[]) { rows[0].value += 2; } }",
        "Effects :: library { Run :: (values: ref int[]) { values[0] = 2; } }",
        "Effects :: behavior { value: int = 1; Run :: () { value = 2; } }",
        "Effects :: behavior { values: int[] = [1]; Run :: () { values[0] = 2; "
        "} }",
        "Effects :: behavior { Run :: () { self.Position.x = 2; } }",
    };
    for (size_t i = 0; i < sizeof sources / sizeof *sources; i++) {
        parse(sources[i]);
        REQUIRE(syntax->diagnostic_count == 0);
        REQUIRE(analysis->valid);
        bool found = false;
        for (int j = 0; j < analysis->function_count; j++) {
            const script_function *f = &analysis->functions[j];
            if (f->symbol >= 0 && !strcmp(analysis->symbols[f->symbol].name, "Run")) {
                REQUIRE(f->effects & SCRIPT_EFFECT_WRITE);
                REQUIRE(!f->pure);
                found = true;
            }
        }
        REQUIRE(found);
    }
    return true;
}
TEST_CASE(language_discarded_pure_result_rejected)
{
    parse("Motion :: system(position: Position) { VectorAdd(position, position); mutate(); }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("VectorAdd returns a new value and changes nothing; assign its result"));
    parse("Motion :: system(position: Position) { action sum = VectorAdd(position, position); sum(); mutate(); }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("sum returns a new value and changes nothing; assign its result"));
    parse("Values :: library { One :: () -> (count: int) { return 1; } Use :: () -> () { One(); } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse("Motion :: system(position: Position) { position = VectorAdd(position, position); mutate(); }");
    REQUIRE(analysis->valid);
    return true;
}
/* Analyzes `source` as the one file of package project:facts. */
static void parse_package(const char *source)
{
    cleanup();
    syntax = script_parse(source);
    script_package_document document = { "/facts/main.chibi", "facts/main.chibi", syntax };
    const script_package_options options = { .documents = &document,
                                             .document_count = 1,
                                             .identity = "project:facts",
                                             .native = &catalog };
    analysis = script_analyze_package(&options);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "package %d:%d: %s\n",
            analysis->diagnostics[i].range.line,
            analysis->diagnostics[i].range.column,
            analysis->diagnostics[i].message
        );
    }
}
/* The names of the functions `@test` marks, joined by spaces. */
static void tests_of(char *out, size_t size)
{
    out[0] = 0;
    for (int i = 0; i < analysis->function_count; i++) {
        if (analysis->functions[i].test) {
            const char *name = analysis->symbols[analysis->functions[i].symbol].name;
            snprintf(out + strlen(out), size - strlen(out), "%s%s", out[0] ? " " : "", name);
        }
    }
}
/* `@test` marks a package function that takes and returns nothing; another
 * attribute, a repeated one, a misplaced test, one with a signature and one
 * whose name the package already declares are rejected. */
TEST_CASE(language_test_attribute_marks_package_functions)
{
    char tests[128];
    parse_package(
        "package facts\n@test\nAdds :: () { x := 1 + 1; }\nHelper :: () -> "
        "int { return 1; }\n"
        "@test Second :: (/* none */) { }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    tests_of(tests, sizeof tests);
    REQUIRE(!strcmp(tests, "Adds Second"));
    const struct
    {
        const char *source, *message;
    } rejected[] = {
        { "package facts\n@bench\nRuns :: () { }", "Unknown attribute '@bench'" },
        { "package facts\n@test @test\nTwice :: () { }", "Duplicate attribute '@test'" },
        { "package facts\n@test\nTakes :: (n: int) { }",
          "A @test function takes no parameters and returns nothing" },
        { "package facts\n@test\nGives :: () -> int { return 1; }",
          "A @test function takes no parameters and returns nothing" },
        { "package facts\n@test\nInfers :: () { return 1; }",
          "A @test function takes no parameters and returns nothing" },
        { "package facts\n@test\nGeneric :: ($T: Type) { }",
          "A @test function takes no parameters and returns nothing" },
        { "package facts\nOuter :: () { @test Inner :: () { } Inner(); }",
          "@test needs a package-level function" },
        { "Values :: library { @test Inside :: () { } }",
          "@test needs a package-level function" },
        { "package facts\nSame :: () { }\n@test\nSame :: () { }",
          "Duplicate declaration 'Same'" },
        { "package facts\nSame :: 1;\n@test\nSame :: () { }",
          "Duplicate declaration 'Same'" },
    };
    for (size_t i = 0; i < sizeof rejected / sizeof *rejected; i++) {
        if (!strncmp(rejected[i].source, "package", 7)) {
            parse_package(rejected[i].source);
        } else {
            parse(rejected[i].source);
        }
        REQUIRE(syntax->diagnostic_count == 0);
        REQUIRE(!analysis->valid && diagnostic(rejected[i].message));
        /* A misplaced or misshapen test is not marked; the analysis is
         * invalid either way. */
        tests_of(tests, sizeof tests);
        REQUIRE(i < 2 || !tests[0]);
    }
    return true;
}
TEST_CASE(language_binding_cycle_rejected)
{
    parse("Bindings :: behavior { bind self.Position.x = self.Position.y; bind self.Position.y = self.Position.x; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Binding dependency cycle"));
    parse("Bindings :: behavior { bind self.Position.y = self.Position.x; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    REQUIRE(analysis->binding_count == 1);
    return true;
}
TEST_CASE(language_inferred_return_of_several_results_is_rejected)
{
    parse(
        "Tuples :: library { Pair :: () -> (a: double, b: double) { "
        "return(1, 2) } "
        "Both :: () { return Pair() } }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic(
        "This call has several results; return the ones you need by name, as "
        "in "
        "return(Hit: r.Hit, Distance: r.Distance)"
    ));
    parse(
        "Tuples :: library { Pair :: () -> (a: double, b: double) { "
        "return(1, 2) } "
        "First :: () { return Pair().a } }"
    );
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_invalid_source_retains_bounded_diagnostics)
{
    const char *sources[] = {
        "Broken :: behavior { on Tick { sample := ; }",
        "Broken :: behavior { value: struct<\"Position\"> = null; }",
        "Broken :: behavior { value: struct; }",
    };
    for (size_t s = 0; s < sizeof sources / sizeof *sources; s++) {
        const char *source = sources[s];
        parse(source);
        REQUIRE(!analysis->valid);
        REQUIRE(analysis->diagnostic_count > 0);
        REQUIRE(analysis->diagnostic_count >= syntax->diagnostic_count);
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            script_range range = analysis->diagnostics[i].range;
            REQUIRE(
                range.start >= 0 && range.start <= range.end &&
                range.end <= (int)strlen(source)
            );
        }
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
        "Projectile :: component {\n"
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
        "    stored: float charge\n"
        "    measured: int u/s\n"
        "    at: Position\n"
        "}\n"
    );
    REQUIRE(c && !component_diagnostics && acount(c->fields) == 11);
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
    REQUIRE(field_is(component_field(c, "stored"), "float", "charge", 0, 0));
    REQUIRE(field_is(component_field(c, "measured"), "int", "u/s", 0, 0));
    REQUIRE(field_is(component_field(c, "at"), "Position", NULL, 0, 0));
    script_component_free(c);
    /* Unit names stay free names outside a field's type, and executable
     * source cannot declare a component. */
    parse("S :: system() {\n    if true {\n        s := 2\n        u := s * 3\n        component := u\n    }\n}\n");
    REQUIRE(analysis->valid);
    parse("C :: component {\n    a: float\n}\n");
    REQUIRE(diagnostic("Expected behavior, library or system"));
    return true;
}
TEST_CASE(language_component_rejects_invalid_fields)
{
    REQUIRE(!read_component(
        "Bad :: component {\n"
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
    REQUIRE(!read_component("Empty :: component {\n}\n"));
    REQUIRE(component_diagnostic("A component declares at least one field"));
    REQUIRE(!read_component("\"Quoted\" :: component {\n    a: float\n}\n"));
    REQUIRE(syntax->diagnostic_count > 0);
    REQUIRE(acount(component_diagnostics) == syntax->diagnostic_count);
    REQUIRE(!read_component("S :: system() {\n}\n"));
    REQUIRE(acount(component_diagnostics) == 1);
    REQUIRE(component_diagnostic("A component source declares one component"));
    /* Keywords are contextual data-member names, not storage declarations. */
    script_component *contextual = read_component("C :: component {\n    state: float\n}\n");
    REQUIRE(contextual && !syntax->diagnostic_count && !acount(component_diagnostics));
    REQUIRE(acount(contextual->fields) == 1 && !strcmp(contextual->fields[0].name, "state"));
    REQUIRE(field_is(&contextual->fields[0], "float", NULL, 0, 0));
    script_component_free(contextual);
    afree(component_diagnostics);
    return true;
}

/* A component measured the way the engine's reflected fields are. */
static const script_catalog_field body_fields[] = {
    { .id = 60, .name = "x", .type = { .storage = PIN_F32 }, .writable = true, .unit = "u" },
    { .id = 61,
      .name = "speed",
      .type = { .storage = PIN_F32 },
      .offset = 4,
      .writable = true,
      .unit = "u/s" },
    { .id = 62,
      .name = "accel",
      .type = { .storage = PIN_F32 },
      .offset = 8,
      .writable = true,
      .unit = "u/s²" },
    { .id = 63,
      .name = "angle",
      .type = { .storage = PIN_F32 },
      .offset = 12,
      .writable = true,
      .unit = "rad" },
    { .id = 64, .name = "count", .type = { .storage = PIN_F32 }, .offset = 16, .writable = true },
    { .id = 65,
      .name = "mass",
      .type = { .storage = PIN_F32 },
      .offset = 20,
      .writable = true,
      .unit = "kg" },
    { .id = 66,
      .name = "push",
      .type = { .storage = PIN_F32 },
      .offset = 24,
      .writable = true,
      .unit = "m/s^2" },
    { .id = 67,
      .name = "force",
      .type = { .storage = PIN_F32 },
      .offset = 28,
      .writable = true,
      .unit = "kg·m/s²" },
    { .id = 68,
      .name = "rate",
      .type = { .storage = PIN_F32 },
      .offset = 32,
      .writable = true,
      .unit = "1/s" },
    { .id = 69,
      .name = "volume",
      .type = { .storage = PIN_F32 },
      .offset = 36,
      .writable = true,
      .unit = "m³" },
    { .id = 70,
      .name = "snap",
      .type = { .storage = PIN_F32 },
      .offset = 40,
      .writable = true,
      .unit = "u/s^4" },
};
static const script_catalog_type body_type = { .id = 50,
                                               .name = "Body",
                                               .type = { .storage = PIN_STRUCT,
                                                         .nominal = 50 },
                                               .fields = body_fields,
                                               .field_count = 11,
                                               .size = 11 * sizeof(float),
                                               .component = true,
                                               .component_id = 502 };
static const script_catalog_global measured_globals[] = {
    { .id = 70, .name = "dt", .type = { .storage = PIN_F32 }, .unit = "s" },
};
static const script_catalog measured = {
    .types = &body_type,
    .type_count = 1,
    .globals = measured_globals,
    .global_count = 1,
    .entity_type = 40,
};
static void measure(const char *body)
{
    char source[1024];
    /* Inside a block, `:=` declares a local rather than system state. */
    snprintf(source, sizeof source, "Move :: system(body: Body) {\nif true {\n%s}\n}\n", body);
    cleanup();
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &measured);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "semantic: %s\n", analysis->diagnostics[i].message);
    }
}
TEST_CASE(language_units_combine_through_arithmetic)
{
    measure(
        "body.x += body.speed * dt\n"
        "body.speed += body.accel * dt\n"
        "body.speed = (body.x - 3) / dt\n"
        "if body.x < 10 { body.x = 0 }\n"
        "step := body.speed * dt\n"
        "body.x = body.x + step * 2\n"
        "body.x *= body.x / body.x\n"
        "body.x /= 2\n"
        "body.angle = body.angle % 6.28\n"
        "body.x %= body.x\n"
        "body.angle = -body.angle\n"
        "body.count = body.count + body.x\n"
        "n := 0\n"
        "n = body.angle\n"
        /* A literal factor may carry any unit. */
        "body.angle += 3 * dt\n"
        "body.accel = body.speed / 0.25\n"
        "body.x = +body.x\n"
        /* Units written with ^, · and superscripts, and 1/s. */
        "body.force = body.mass * body.push\n"
        "body.force = body.push * body.mass\n"
        "body.x *= body.rate * dt\n"
    );
    REQUIRE(analysis->valid);
    for (int i = 0; i < analysis->warning_count; i++) {
        fprintf(stderr, "unexpected: %s\n", analysis->warnings[i].message);
    }
    REQUIRE(analysis->warning_count == 0);
    return true;
}
TEST_CASE(language_reductions_check_contribution_units)
{
    measure("reduce body.x += dt\n");
    REQUIRE(diagnostic("Units differ: u = s"));
    measure("reduce min(body.x, dt)\n");
    REQUIRE(diagnostic("Units differ: u = s"));
    measure("reduce max(body.x, 2)\n");
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_units_reject_mismatch)
{
    measure(
        "body.x += body.speed\n"
        "body.x = body.angle\n"
        "if body.speed < body.x {}\n"
        "late := body.x + dt\n"
        "body.x *= dt\n"
        "body.x = body.speed / dt\n"
        "body.x = body.x / body.x\n"
        "body.x = +body.angle\n"
        "body.x = body.speed + body.x\n"
        "body.volume = body.mass\n"
        "body.x = body.snap\n"
        "body.speed = body.rate\n"
        "body.x = body.x % body.speed\n"
        "body.x %= body.speed\n"
        "body.x = fallback(body.speed, default: 0)\n"
    );
    /* Proven dimensional mistakes prevent execution. */
    REQUIRE(!analysis->valid && analysis->warning_count == 0);
    REQUIRE(diagnostic("Units differ: u += u/s")->range.line == 3);
    REQUIRE(diagnostic("Units differ: u = rad"));
    REQUIRE(diagnostic("Units differ: u/s < u"));
    REQUIRE(diagnostic("Units differ: u + s"));
    REQUIRE(diagnostic("Units differ: *= would turn u into u·s"));
    REQUIRE(diagnostic("Units differ: u = u/s²"));
    REQUIRE(diagnostic("Units differ: u = 1"));
    REQUIRE(diagnostic("Units differ: u = rad"));
    /* One mistake, one diagnostic: the sum is wrong, not its assignment. */
    REQUIRE(diagnostic("Units differ: u/s + u"));
    REQUIRE(diagnostic("Units differ: m³ = kg"));
    REQUIRE(diagnostic("Units differ: u = u/s^4"));
    REQUIRE(diagnostic("Units differ: u/s = 1/s"));
    REQUIRE(diagnostic("Units differ: u % u/s"));
    REQUIRE(diagnostic("Units differ: u %= u/s"));
    /* A fallback measures what it reads first. */
    REQUIRE(diagnostic("Units differ: u = u/s"));
    REQUIRE(analysis->diagnostic_count == 15);
    /* Listed in source order. */
    for (int i = 1; i < analysis->diagnostic_count; i++) {
        REQUIRE(
            analysis->diagnostics[i - 1].range.start <=
            analysis->diagnostics[i].range.start
        );
    }
    return true;
}
TEST_CASE(language_units_check_struct_field_initializers)
{
    measure("copy: Body = {x: body.speed}; body = copy;\n");
    REQUIRE(!analysis->valid && diagnostic("Units differ: u = u/s"));
    measure("copy: Body = {x: dt}; body.x = copy.x;\n");
    REQUIRE(!analysis->valid && diagnostic("Units differ: u = s"));
    measure("copy: Body = {x: body.speed * dt}; body.x = copy.x;\n");
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_units_check_empty_struct_defaults)
{
    measure("copy: Body = null; body = copy;\n");
    REQUIRE(analysis->valid);
    parse("Reset :: system(p: Position ) { if true { copy := event; copy = null; } }");
    REQUIRE(!analysis->valid && diagnostic("Incompatible result tuple shape"));
    return true;
}
TEST_CASE(language_units_check_bindings)
{
    cleanup();
    syntax = script_parse(
        "Bound :: behavior {\n"
        "    bind self.Body.x = self.Body.speed\n"
        "    bind self.Body.push = self.Body.force / self.Body.mass\n"
        "}\n"
    );
    analysis = script_analyze(syntax, &measured);
    REQUIRE(!analysis->valid && analysis->binding_count == 2);
    REQUIRE(analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("Units differ: u = u/s")->range.line == 2);
    return true;
}
TEST_CASE(language_units_skip_invalid_sources)
{
    /* Recovery closes this source to analyze it; it stays unchecked. */
    cleanup();
    syntax = script_parse("Move :: system(body: Body) {\n    body.x = (body.speed + 1");
    analysis = script_analyze(syntax, &measured);
    REQUIRE(!analysis->valid && analysis->warning_count == 0);
    REQUIRE(!diagnostic("Units differ: u = u/s"));
    return true;
}

TEST_CASE(language_units_check_all_fallback_values)
{
    measure("body.x = fallback(body.x, binding: body.speed, default: 0)\n");
    REQUIRE(!analysis->valid);
    REQUIRE(analysis->diagnostic_count == 1 && analysis->warning_count == 0);
    REQUIRE(diagnostic("Units differ: u fallback u/s"));
    measure("body.x = fallback(body.x, binding: body.x, default: 0)\n");
    REQUIRE(analysis->valid && analysis->diagnostic_count == 0);
    return true;
}

TEST_CASE(language_units_follow_constant_and_state_initializers)
{
    const char *sources[] = {
        "Move :: system(body: Body) { elapsed := dt; if true { body.x = "
        "elapsed; "
        "} }",
        "Move :: system(body: Body) { elapsed: retain = dt; if true { "
        "body.x = "
        "elapsed; } }",
    };
    for (size_t i = 0; i < sizeof sources / sizeof *sources; i++) {
        cleanup();
        syntax = script_parse(sources[i]);
        analysis = script_analyze(syntax, &measured);
        REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
        REQUIRE(diagnostic("Units differ: u = s"));
    }
    measure("Step :: 2; body.x = body.speed * dt * Step;\n");
    REQUIRE(analysis->valid && analysis->diagnostic_count == 0);
    /* Component reads are effects, even from a constant copy. A plain reflected
     * value supports constant field access and carries the same unit metadata. */
    script_catalog_type measured_value = body_type;
    measured_value.component = false;
    measured_value.component_id = 0;
    script_catalog values = measured;
    values.types = &measured_value;
    cleanup();
    syntax = script_parse(
        "Measured :: library { Run :: () { Origin: Body : {x: 1}; "
        "Step :: Origin.x; body: Body = {}; body.speed = Step; } }"
    );
    analysis = script_analyze(syntax, &values);
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("Units differ: u/s = u"));

    const script_catalog_capability setter = { .id = 71,
                                               .name = "set_measure",
                                               .state_access = SCRIPT_STATE_ACCESS_WRITE };
    script_catalog with_setter = measured;
    with_setter.capabilities = &setter;
    with_setter.capability_count = 1;
    cleanup();
    syntax = script_parse("Move :: system(body: Body) { elapsed: retain = dt; if true { set_measure<elapsed>(body.x); } }");
    analysis = script_analyze(syntax, &with_setter);
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("Units differ: s = u"));
    return true;
}

static void annotated_global(const char *unit, const char *body)
{
    char source[1024];
    snprintf(source, sizeof source, "Measured :: library { Run :: () { %s } }", body);
    const script_catalog_global global = { .id = 70,
                                           .name = "measured",
                                           .type = { .storage = PIN_F64 },
                                           .unit = unit };
    script_catalog with_unit = measured;
    with_unit.globals = &global;
    with_unit.global_count = 1;
    cleanup();
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &with_unit);
}

static void measured_source(const char *source)
{
    cleanup();
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &measured);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "signature units: %s\n", analysis->diagnostics[i].message);
    }
}
TEST_CASE(language_units_check_callable_contracts)
{
    measured_source(
        "Measures :: library { "
        "Travel :: (speed: u/s, elapsed: s) -> u { return speed * elapsed; } "
        "Charge :: (value: float charge) -> number charge { return value; } "
        "First :: (value: u) -> u { return Later(value); } "
        "Later :: (value: u) -> u { if value > 0 { return First(value - 1); } "
        "return value; } "
        "Borrow :: (value: ref number u) -> ref number u { return value; } "
        "Pair :: (distance: u, elapsed: s) -> (Distance: u, Elapsed: s) { "
        "return(distance, elapsed); } "
        "Use :: (distance: u, elapsed: s) -> u { pair := Pair(distance, "
        "elapsed); pair = Pair(distance, elapsed); pair.Distance = distance; "
        "return Travel(pair.Distance / pair.Elapsed, elapsed); } "
        "Plain :: () { count := 0; } }"
    );
    REQUIRE(analysis->valid);
    bool plain = false, measured_parameter = false;
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *s = &analysis->symbols[i];
        if (!strcmp(s->name, "count")) {
            plain = s->type.storage == PIN_F64 && !s->unit;
        }
        if (!strcmp(s->name, "speed")) {
            measured_parameter = s->type.storage == PIN_F64 && s->unit &&
                                 !strcmp(s->unit, "u/s");
        }
    }
    REQUIRE(plain && measured_parameter);
    return true;
}
TEST_CASE(language_units_reject_callable_mismatches)
{
    const struct
    {
        const char *source, *message;
    } cases[] = {
        { "M :: library { Bad :: (elapsed: s) -> u { return elapsed; } }",
          "Units differ: u = s" },
        { "M :: library { Bad :: (elapsed: s) { Later(elapsed); } Later :: "
          "(distance: u) {} }",
          "Units differ: u argument s" },
        { "M :: library { Bad :: (distance: ref number u, elapsed: s) { "
          "distance "
          "= elapsed; } }",
          "Units differ: u = s" },
        { "M :: library { Bad :: (body: Body) { Later(body.speed); } Later :: "
          "(distance: ref float u) {} }",
          "Units differ: u argument u/s" },
        { "M :: library { Bad :: (elapsed: ref number s) -> ref number u { "
          "return "
          "elapsed; } }",
          "Units differ: u = s" },
        { "M :: library { Bad :: (distance: u = dt) {} }", "Units differ: u default s" },
        { "M :: library { Bad :: () -> (Distance: u = dt) {} }",
          "Units differ: u default s" },
        { "M :: library { Bad :: () -> (Distance: u, Elapsed: s) { "
          "return(Distance: dt, Elapsed: 1); } }",
          "Units differ: u = s" },
        { "M :: library { Bad :: (unused: number u^0) {} }", "Invalid unit 'u^0'" },
        { "M :: library { Bad :: (unused: string s) {} }",
          "A measured signature requires a numeric storage type" },
        { "M :: library { Bad :: (unused: flaot) {} }", "Unknown type 'flaot'" },
        { "M :: library { First :: () -> (Value: u, Other: s) { return(1, 1); "
          "} "
          "Second :: () -> (Value: s, Other: s) { return(1, 1); } "
          "Bad :: () { tuple := First(); tuple = Second(); } }",
          "Incompatible types: expected struct, got struct" },
        { "M :: library { Pair :: () -> (Value: u, Other: s) { return(1, 1); } "
          "Bad :: () { tuple := Pair(); tuple.Value = dt; } }",
          "Units differ: u = s" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        measured_source(cases[i].source);
        REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
        REQUIRE(diagnostic(cases[i].message));
    }
    return true;
}
TEST_CASE(language_units_follow_native_signatures_and_task_results)
{
    const script_catalog_parameter parameter = { .name = "distance",
                                                 .unit = "u",
                                                 .type = { .storage = PIN_F64 } };
    const script_catalog_parameter result = { .name = "Elapsed",
                                              .unit = "s",
                                              .type = { .storage = PIN_F64 } };
    const script_catalog_capability capability = { .id = 91,
                                                   .name = "Measured",
                                                   .parameters = &parameter,
                                                   .parameter_count = 1,
                                                   .results = &result,
                                                   .result_count = 1 };
    script_catalog native = measured;
    native.capabilities = &capability;
    native.capability_count = 1;
    const char *sources[] = {
        "M :: library { Bad :: (elapsed: s) -> s { return Measured(elapsed); } "
        "}",
        "M :: library { Bad :: () -> u { return Measured(1); } }",
        ("M :: library { Bad :: () -> u { value := Measured(1); return "
         "value.Elapsed; } }"),
    };
    const char *messages[] = { "Units differ: u argument s",
                               "Units differ: u = s",
                               "Units differ: u = s" };
    for (int i = 0; i < 3; i++) {
        cleanup();
        syntax = script_parse(sources[i]);
        analysis = script_analyze(syntax, &native);
        REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
        REQUIRE(diagnostic(messages[i]));
    }
    script_catalog_capability variadic = capability;
    variadic.variadic = true;
    native.capabilities = &variadic;
    cleanup();
    syntax = script_parse("M :: library { Bad :: (elapsed: s) -> s { return Measured(1, elapsed); } }");
    analysis = script_analyze(syntax, &native);
    REQUIRE(!analysis->valid && diagnostic("Units differ: u argument s"));
    const script_catalog_capability event = { .id = 92,
                                              .name = "Tick",
                                              .kind = SCRIPT_CAPABILITY_EVENT,
                                              .parameters = &result,
                                              .parameter_count = 1 };
    native.capabilities = &event;
    native.tick_event = event.id;
    cleanup();
    syntax = script_parse("M :: behavior { on Tick(elapsed) { self.Body.x = elapsed; } }");
    analysis = script_analyze(syntax, &native);
    REQUIRE(!analysis->valid && diagnostic("Units differ: u = s"));
    measured_source(
        "M :: library { Work :: (distance: u) -> u { yield; return distance; } "
        "Bad :: () -> s { pending := Work(1); outcome := await pending; "
        "if outcome is number value { return value; } return 0; } }"
    );
    REQUIRE(!analysis->valid && diagnostic("Units differ: s = u"));
    return true;
}

TEST_CASE(language_units_reject_malformed_metadata_and_overflow)
{
    const char *invalid[] = { "",
                              "u/",
                              "u//s",
                              "u^",
                              "u^0",
                              "u^2.5",
                              "u^2147483648",
                              "u^-2147483648",
                              "u^99999999999999999999999999999999",
                              "u·",
                              "1u",
                              "not a unit",
                              "a/b/c/d/e/f/g/h/i" };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        annotated_global(invalid[i], "value := measured;");
        REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
        REQUIRE(
            strstr(analysis->diagnostics[0].message, "Invalid unit '") ==
            analysis->diagnostics[0].message
        );
        REQUIRE(analysis->warning_count == 0);
    }
    annotated_global("u^2147483647", "value := measured * measured;");
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("Unit expression exceeds supported dimensional complexity"));
    annotated_global("u^-2147483647", "value := measured * measured;");
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("Unit expression exceeds supported dimensional complexity"));
    /* Custom names are nominal dimensions, not misspellings of an SI registry. */
    annotated_global("charge/basket", "value := measured / measured;");
    REQUIRE(analysis->valid && analysis->diagnostic_count == 0);
    annotated_global(NULL, "value := measured;");
    REQUIRE(analysis->valid && analysis->diagnostic_count == 0);
    return true;
}

TEST_CASE(language_constants_reject_runtime_dependencies_and_writes)
{
    parse(
        "Constants :: library { Base :: 2; Total :: Base * 3; "
        "Compute :: (x: int) -> int { Twice :: 2; return x * Twice } }"
    );
    REQUIRE(analysis->valid);
    int count = 0;
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_CONSTANT) {
            REQUIRE(!analysis->symbols[i].writable);
            count++;
        }
    }
    REQUIRE(count == 3);
    parse("Constants :: behavior { value := 2; Frozen :: value; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Constant initializer requires compile-time values and operators"));
    parse("Constants :: library { Run :: (x: int) { Frozen :: x; } }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Constant initializer requires compile-time values and operators"));
    parse("Constants :: library { Frozen :: 2; Run :: () { Frozen = 3; } }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Assignment requires a writable value"));
    parse("Constants :: library { A :: B; B :: A; }");
    REQUIRE(!analysis->valid);
    return true;
}

TEST_CASE(language_unions_share_patterns_and_require_exhaustive_switches)
{
    parse(
        "Unions :: library { AttackState :: union { Idle | Windup(remaining: "
        "number) | Active(damage: number) }; "
        "Run :: (value: AttackState) -> number { switch value { case .Idle: "
        "return 0; "
        "case .Windup(remaining): return remaining; case .Active(damage): "
        "return damage; } } }"
    );
    REQUIRE(analysis->valid);
    REQUIRE(analysis->type_count == 1);
    REQUIRE(analysis->types[0].variant_count == 3);
    parse(
        "Unions :: library { Status :: union { \"Yes\" | \"No\" }; "
        "Run :: (value: Status) -> number { switch value { case \"Yes\": "
        "return 1; } } }"
    );
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Non-exhaustive switch: a union alternative is missing"));
    parse(
        "Unions :: library { Status :: union { \"Yes\" | \"No\" }; "
        "Run :: (value: Status) -> number { switch value { case \"Yes\": "
        "return 1; "
        "case \"Yes\": return 2; case \"No\": return 0; } } }"
    );
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Unreachable duplicate union pattern"));
    parse("Unions :: behavior { Choice :: union { A | B }; value: retain Choice; }");
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Choice :: union { A | B }; "
        "Run :: () -> Choice { } }"
    );
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Union result 'Result' requires a value on every return path"));
    parse("Unions :: library { Choice :: union { A | B }; Run :: () -> Choice { return Result; } }");
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Choice :: union { A | B }; "
        "Run :: () -> (value: Choice = Choice.A) { return; } }"
    );
    REQUIRE(analysis->valid);
    parse("Unions :: behavior { Choice :: union { A | B }; value: retain Choice = null; }");
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Choice :: union { A | B }; "
        "Run :: (value: Choice?) { switch value { case .A: return; case .B: "
        "return; } } }"
    );
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Optional union requires a presence guard before switching"));
    parse(
        "Unions :: library { Choice :: union { A | B }; "
        "Run :: (value: Choice?) { if value { switch value { case .A: "
        "return; case .B: return; } } } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Choice :: union { A | B }; "
        "Run :: (value: Choice?) { if value is Choice.A { return; } "
        "switch value { case .A: return; case .B: return; } } }"
    );
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Optional union requires a presence guard before switching"));
    parse(
        "Unions :: library { Choice :: union { A | B }; "
        "Run :: (flag: bool) -> Choice { if flag { return Choice.A; } } }"
    );
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Choice :: union { A | B }; "
        "Run :: (flag: bool) -> Choice { if flag { return Choice.A; } else { "
        "return Choice.B; } } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Choice :: union { A | B }; "
        "Run :: (value: Choice) -> Choice { switch value { case .A: return "
        "Choice.B; case .B: return Choice.A; } } }"
    );
    REQUIRE(analysis->valid);
    parse("Unions :: library { Choice :: union { A | B }; Default :: Choice.A; }");
    REQUIRE(analysis->valid);
    parse("Unions :: library { List :: union { Nil | Cons(tail: List) }; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Recursive owned union payloads are not supported"));
    parse(
        "Unions :: library { First :: union { One(next: Second[]) }; "
        "Second :: union { Two(next: First?) }; }"
    );
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Recursive owned union payloads are not supported"));
    parse(
        "Unions :: library { First :: union { One(next: Second) }; "
        "Second :: union { Two(value: number) }; }"
    );
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_cancelled_builtin_cannot_be_shadowed)
{
    const char *sources[] = {
        "Reserved :: library { Run :: () { Cancelled := 1; copy := Cancelled; "
        "} }",
        "Reserved :: behavior { Cancelled: number = 1; }",
        "Reserved :: library { Cancelled :: 1; }",
        "Reserved :: library { Run :: (Cancelled: number) {} }",
        "Reserved :: library { Run :: () -> (Cancelled: number) {} }",
        "Reserved :: library { Cancelled :: () {} }",
        "Reserved :: library { Cancelled :: union { A | B }; }",
        ("Reserved :: library { Run :: (value: number | Cancelled) { "
         "if value is number Cancelled {} } }"),
    };
    for (int i = 0; i < (int)(sizeof sources / sizeof *sources); i++) {
        parse(sources[i]);
        REQUIRE(!analysis->valid);
        REQUIRE(diagnostic("'Cancelled' is reserved for the cancellation singleton"));
    }
    parse(
        "Reserved :: library { Choice :: union { Cancelled(Cancelled: number) "
        "| "
        "Value }; "
        "Run :: () -> number { value := Choice.Cancelled(Cancelled: 3); "
        "if value is .Cancelled(payload) { return payload; } return 0; } }"
    );
    REQUIRE(analysis->valid);
    script_component *component = read_component("Status :: component { Cancelled: float }");
    REQUIRE(component && !component_diagnostics);
    REQUIRE(!strcmp(component->fields[0].name, "Cancelled"));
    script_component_free(component);
    return true;
}

TEST_CASE(language_switch_exhaustiveness_respects_narrowed_tags)
{
    parse(
        "Unions :: library { Choice :: union { A | B(value: number) }; "
        "Run :: (value: Choice) -> number { if value is Choice.B { "
        "switch value { case .B(payload): return payload; } } return 0; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Choice :: union { A | B(value: number) }; "
        "Run :: (value: Choice) -> number { if value is Choice.A { return 0; } "
        "switch value { case .B(payload): return payload; } } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Choice :: union { A | B(value: number) }; "
        "Run :: (value: Choice) -> number { "
        "switch value { case .B(payload): return payload; } } }"
    );
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Non-exhaustive switch: a union alternative is missing"));
    parse(
        "Unions :: library { Choice :: union { A | B | C }; "
        "Run :: (value: Choice) -> number { if value is Choice.A { return 0; } "
        "switch value { case .B: return 1; case .C: return 2; } } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Choice :: union { A | B | C }; "
        "Run :: (value: Choice) -> number { if value is Choice.A { return 0; } "
        "switch value { case .B: return 1; } } }"
    );
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Non-exhaustive switch: a union alternative is missing"));
    return true;
}

TEST_CASE(language_type_patterns_narrow_and_invalidate)
{
    parse(
        "Unions :: library { Run :: (result: number | Cancelled) -> number { "
        "if result is Cancelled { return 0; } return result + 1; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Run :: (result: number | Cancelled) -> number { "
        "if result is number num_result { return num_result; } return 0; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Run :: (result: number | Cancelled) -> number { "
        "if result is number { result = Cancelled; return result + 1; } return "
        "0; } }"
    );
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Run :: (result: number | Cancelled) -> number { "
        "if result is number num_result { return num_result; } return "
        "num_result; } }"
    );
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Run :: (result: number | Cancelled) -> number { "
        "if result is number { mutate(); return result + 1; } return 0; } }"
    );
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Run :: (result: number | Cancelled) -> number { "
        "if result is number num_result || true { return num_result; } return "
        "0; } }"
    );
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Run :: (result: number | Cancelled) -> number { "
        "if result is not Cancelled { return result; } return 0; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Run :: (result: number | Cancelled) -> number { "
        "switch result { case Cancelled: return 0; case: return result + 1; "
        "} } }"
    );
    REQUIRE(analysis->valid);
    parse("Unions :: library { Outer :: (local: number) { Inner :: () -> number { return local; } } }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Nested functions require enclosing locals to be passed as parameters"));
    parse("Unions :: library { Run :: (result: number | Cancelled) -> number? { return result as number; } }");
    REQUIRE(analysis->valid);
    parse("Unions :: library { Run :: (value: int?) -> number? { return value; } }");
    REQUIRE(!analysis->valid);
    parse("Unions :: library { Run :: (value: int) -> number? { return value; } }");
    REQUIRE(!analysis->valid);
    parse("Unions :: library { Run :: () -> int? { return 0; } }");
    REQUIRE(analysis->valid);
    parse("Unions :: library { Run :: (value: int?) -> number? { if value { return number(value); } return nil; } }");
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Run :: (pending: task<number>) -> number { "
        "result := await pending; if result is not Cancelled { return result; "
        "} return 0; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Work :: () -> number { yield; return 0; } "
        "Run :: () -> number { pending: task<number> = Work(); result := await "
        "pending; "
        "if result is number value { return value; } return 1; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Run :: (pending: task<number>[]) -> number { "
        "result := await pending[0]; if result is not Cancelled { return "
        "result; } return 0; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Run :: (pending: task<number>[]) -> number { "
        "result := await pending; return 0; } }"
    );
    REQUIRE(!analysis->valid);
    parse("Unions :: library { Run :: (pending: task<number>[]) -> bool { return !pending; } }");
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Run :: (pending: task<number?>) -> number { "
        "result := await pending; if result is not Cancelled { value := "
        "result; if value { return value; } } return 0; } }"
    );
    REQUIRE(analysis->valid);
    return true;
}

void test_language_semantic()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_unions_share_patterns_and_require_exhaustive_switches);
    RUN_TEST_CASE(language_cancelled_builtin_cannot_be_shadowed);
    RUN_TEST_CASE(language_switch_exhaustiveness_respects_narrowed_tags);
    RUN_TEST_CASE(language_type_patterns_narrow_and_invalidate);
    RUN_TEST_CASE(language_constants_reject_runtime_dependencies_and_writes);
    RUN_TEST_CASE(language_shadowing_resolves_nearest_declaration);
    RUN_TEST_CASE(language_authored_map_intrinsic_names_preserve_callable_identity);
    RUN_TEST_CASE(language_inferred_map_requires_an_initializer);
    RUN_TEST_CASE(language_retained_bindings_preserve_owner_and_value_type);
    RUN_TEST_CASE(language_system_retention_rejects_non_system_owners_and_tasks);
    RUN_TEST_CASE(language_system_invocation_locals_cross_calls_only_as_arguments);
    RUN_TEST_CASE(language_optional_component_requires_presence_guard);
    RUN_TEST_CASE(language_reductions_resolve_stored_fields_and_membership);
    RUN_TEST_CASE(language_reductions_require_system_component_fields);
    RUN_TEST_CASE(language_reduction_producers_reject_conflicting_component_values);
    RUN_TEST_CASE(language_reduction_producers_cannot_suspend_or_start_tasks);
    RUN_TEST_CASE(language_unordered_reductions_require_unbounded_isolated_components);
    RUN_TEST_CASE(language_reductions_check_contribution_units);
    RUN_TEST_CASE(language_query_header_resolves_boolean_filters_and_sources);
    RUN_TEST_CASE(language_query_joins_and_filters_share_source_rules);
    RUN_TEST_CASE(language_query_generators_bind_rows_and_pure_value_filters);
    RUN_TEST_CASE(language_query_filters_reject_effects_and_unbound_variables);
    RUN_TEST_CASE(language_derived_component_is_read_only);
    RUN_TEST_CASE(language_numeric_literal_storage_limits);
    RUN_TEST_CASE(language_loops_reverse_explicitly);
    RUN_TEST_CASE(language_hex_literal_fills_integer_storage);
    RUN_TEST_CASE(language_character_literals_take_their_type_from_context);
    RUN_TEST_CASE(language_char_widens_to_int_and_narrows_explicitly);
    RUN_TEST_CASE(language_numeric_conversions_check_modes_and_constants);
    RUN_TEST_CASE(language_integers_widen_implicitly_and_narrow_explicitly);
    RUN_TEST_CASE(language_vector_operator_resolves_catalog_identity);
    RUN_TEST_CASE(language_call_chains_infer_effects_and_purity);
    RUN_TEST_CASE(language_owned_record_and_index_writes_stay_pure);
    RUN_TEST_CASE(language_external_record_and_index_writes_are_effects);
    RUN_TEST_CASE(language_discarded_pure_result_rejected);
    RUN_TEST_CASE(language_binding_cycle_rejected);
    RUN_TEST_CASE(language_inferred_return_of_several_results_is_rejected);
    RUN_TEST_CASE(language_invalid_source_retains_bounded_diagnostics);
    RUN_TEST_CASE(language_component_declares_measured_fields);
    RUN_TEST_CASE(language_component_rejects_invalid_fields);
    RUN_TEST_CASE(language_units_combine_through_arithmetic);
    RUN_TEST_CASE(language_units_reject_mismatch);
    RUN_TEST_CASE(language_units_check_struct_field_initializers);
    RUN_TEST_CASE(language_units_check_empty_struct_defaults);
    RUN_TEST_CASE(language_units_check_bindings);
    RUN_TEST_CASE(language_units_skip_invalid_sources);
    RUN_TEST_CASE(language_units_check_all_fallback_values);
    RUN_TEST_CASE(language_units_follow_constant_and_state_initializers);
    RUN_TEST_CASE(language_units_check_callable_contracts);
    RUN_TEST_CASE(language_units_reject_callable_mismatches);
    RUN_TEST_CASE(language_units_follow_native_signatures_and_task_results);
    RUN_TEST_CASE(language_units_reject_malformed_metadata_and_overflow);
    RUN_TEST_CASE(language_test_attribute_marks_package_functions);
    pu_clear_setup();
}
