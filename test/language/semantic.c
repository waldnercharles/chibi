#include "language/semantic.h"

#include "float_conversion_cases.h"
#include "language/component.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/scalar.h"

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
/* Sixteen fixed sources, each parsed and analyzed once, without execution. */
TEST_CASE(language_managed_pointers_reject_invalid_storage_and_types)
{
    const struct
    {
        const char *source, *message;
    } cases[] = {
        { "L :: library { F :: () { p: ^int = nil; } }", "A non-null pointer cannot be nil" },
        { "B :: behavior { p: ^int; }", "A non-null pointer requires an initializer" },
        { "L :: library { R :: struct { p: ^int; } F :: () { value: R = {}; } "
          "}",
          "Missing required record field 'p'" },
        { "L :: library { F :: () { n: int = 1; p: ^int = &n; text: string = "
          "\"x\"; p = &text; } }",
          NULL },
        { "L :: library { F :: () { xs: int[] = [1]; p := &xs[0]; } }",
          "Managed pointer requires a writable local or record field" },
        { "L :: library { F :: () { xs: [int: int] = [1: 2]; p := &xs[1]; } }",
          "Managed pointer requires a writable local or record field" },
        { "L :: library { F :: () -> int { n: int = 1; p := &n; return int(p); "
          "} }",
          "Conversion requires numeric values or the value's own type" },
        { "L :: library { F :: () { p: ^int = 1; } }", NULL },
        { "L :: library { F :: () { p := &(1 + 2); } }",
          "Managed pointer requires a writable local or record field" },
        { "L :: library { F :: () { n: int : 1; p := &n; } }",
          "Managed pointer requires a writable local or record field" },
        { "L :: library { F :: (p: ^int?) -> int { return p^; } }",
          "Nullable pointer requires a presence guard before dereference" },
        { "L :: library { F :: (n: int) -> int { return n^; } }",
          "Dereference requires a managed pointer" },
        { "L :: library { F :: () -> ^int { x := 1; p: ^int? = &x; "
          "if p != nil { q := &p; q^ = nil; return p; } return &x; } }",
          NULL },
        { "L :: library { Take :: (p: ^(^int)) {} F :: () { x := 1; "
          "p: ^int? = &x; if p != nil { Take(&p); } } }",
          NULL },
        { "L :: library { I :: struct { x: int; } O :: struct { inner: I; y: "
          "int; } "
          "F :: () { o: O = {inner: {x: 1}, y: 2}; p := &o; } }",
          "Managed pointers require records without inline record fields" },
        { "L :: library { I :: struct { x: int; } O :: struct { inner: I; y: "
          "int; } "
          "F :: () { o: O = {inner: {x: 1}, y: 2}; p := &o.y; } }",
          "Managed pointers require records without inline record fields" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        parse(cases[i].source);
        REQUIRE(!syntax->diagnostic_count);
        REQUIRE(!analysis->valid && analysis->diagnostic_count > 0);
        if (cases[i].message) {
            REQUIRE(diagnostic(cases[i].message));
        }
    }
    return true;
}
TEST_CASE(language_shadowing_resolves_nearest_declaration)
{
    const char *source = "Scope :: behavior {\ndt := 1;\non Tick :: (dt: "
                         "number s) { copy: number = dt; }\n}";
    parse(source);
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    int parameter = -1, state = -1;
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (!strcmp(analysis->symbols[i].name, "dt")) {
            if (analysis->symbols[i].kind == SCRIPT_PARAMETER) {
                parameter = i;
            }
            if (analysis->symbols[i].kind == SCRIPT_STATE) {
                state = i;
            }
        }
    }
    REQUIRE(parameter >= 0 && state >= 0 && parameter != state);
    const script_use *use = script_use_at(analysis, (int)(strstr(source, "= dt") - source) + 2);
    REQUIRE(use && use->symbol == parameter);
    /* An integer literal is an int unless its context says otherwise. */
    REQUIRE(analysis->symbols[state].type.storage == PIN_I32);
    return true;
}
static pin_type local_storage(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (!strcmp(analysis->symbols[i].name, name)) {
            return analysis->symbols[i].type.storage;
        }
    }
    return PIN_FLOW;
}
/* Without context a literal is typed by its spelling: an integer is int, or
 * the first of i64 and u64 that holds it, and a decimal point or an
 * exponent makes a number, a 32-bit float. A floating value becomes an integer
 * only through int(x). */
TEST_CASE(language_literals_are_typed_by_spelling)
{
    parse(
        "L :: library { F :: () { count := 0; ratio := 0.5; power := 1e3; "
        "mask := 0xFF; big := 3000000000; huge := 0xFFFFFFFFFFFFFFFF; "
        "half := 1 / 2; mixed := 1 / 2.0; scaled: number = 1; } }"
    );
    REQUIRE(analysis->valid);
    REQUIRE(local_storage("count") == PIN_I32 && local_storage("mask") == PIN_I32);
    REQUIRE(local_storage("ratio") == PIN_F32 && local_storage("power") == PIN_F32);
    REQUIRE(local_storage("big") == PIN_I64 && local_storage("huge") == PIN_U64);
    REQUIRE(local_storage("half") == PIN_I32 && local_storage("mixed") == PIN_F32);
    REQUIRE(local_storage("scaled") == PIN_F32);
    parse("L :: library { F :: (v: number) { count := 0; count = v; } }");
    REQUIRE(diagnostic("Incompatible types: expected int, got number; convert with int(value)"));
    parse("L :: library { F :: (v: number) { count := 0; count += 1.5; } }");
    REQUIRE(diagnostic("Numeric literal has a fraction, which int cannot hold"));
    parse("L :: library { F :: (v: number) -> int { return int(v) + 1; } }");
    REQUIRE(analysis->valid);
    return true;
}
/* pub exports a package's top-level declarations; nothing else takes it. */
TEST_CASE(language_pub_marks_only_package_declarations)
{
    cleanup();
    syntax = script_parse(
        "package facts\n@test pub F :: () {}\npub L: int : 3;\npub C :: 4;\n"
        "pub S :: struct { x: int; }\npub U :: union { A | B }\n"
    );
    REQUIRE(!syntax->diagnostic_count);
    const char *invalid[] = {
        "L :: library { pub F :: () {} }",
        "B :: behavior { pub Limit :: 3; }",
        "package facts\nF :: () { pub Limit :: 3; }",
        "package facts\npub x := 1;",
        "package facts\npub import strings \"core:strings\"\n",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        cleanup();
        syntax = script_parse(invalid[i]);
        REQUIRE(syntax->diagnostic_count);
    }
    return true;
}
TEST_CASE(language_scoped_pointer_calls_require_addresses)
{
    parse(
        "Refs :: library { Bump :: (value: ^int) { value^ += 1; } "
        "Relay :: (value: ^int) { Bump(value: value); } "
        "Run :: () -> int { value := 4; Relay(&value); return value; } }"
    );
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    static const struct
    {
        const char *source, *message, *argument;
        int offset;
    } cases[] = {
        { "R :: library { F :: (n: ^int) {} Run :: () { n := 1; F(n); } }",
          "Pointer parameter requires &value or a pointer",
          "F(n)",
          2 },
        { "R :: library { Value :: (n: int) {} Run :: () { n := 1; Value(&n); "
          "} }",
          "Incompatible types: expected int, got pointer",
          "Value(&n)",
          6 },
        { "R :: library { F :: (n: ^int) {} Run :: () { F(&1); } }",
          "Pointer argument requires a writable location",
          "F(&1)",
          2 },
        { "R :: library { F :: (n: ^int) {} Run :: () { Frozen :: 1; "
          "F(&Frozen); } }",
          "Pointer argument requires a writable location",
          "F(&Frozen)",
          2 },
        { "R :: library { F :: (n: ^int) {} Run :: () { flag := true; "
          "F(&flag); } }",
          "Incompatible types: expected int, got bool",
          "F(&flag)",
          2 },
        { "R :: library { F :: (n: ^i64) {} Run :: () { n: int = 1; F(&n); "
          "} }",
          "Pointer argument requires the exact parameter type",
          "F(&n)",
          2 },
        { "R :: library { F :: (n: ^i64) {} Run :: () { n: int = 1; F(n: "
          "^int = &n); } }",
          "Pointer argument requires the exact parameter type",
          "F(n: ^int = &n)",
          5 },
        { "R :: library { F :: (row: ^Position) {} Run "
          ":: () { F(row.x: 1.0); } }",
          "Pointer parameter members cannot be named",
          "F(row.x: 1.0)",
          2 },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        parse(cases[i].source);
        REQUIRE(!syntax->diagnostic_count && !analysis->valid);
        const script_diagnostic *error = diagnostic(cases[i].message);
        REQUIRE(error);
        const char *argument = strstr(cases[i].source, cases[i].argument);
        REQUIRE(
            argument && error->range.start ==
                            (int)(argument - cases[i].source) + cases[i].offset
        );
    }
    return true;
}

static const script_symbol *parameter_named(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_PARAMETER &&
            !strcmp(analysis->symbols[i].name, name)) {
            return &analysis->symbols[i];
        }
    }
    return NULL;
}
/* A `^T` parameter whose address stays inside the call aliases its argument;
 * any escaping use makes it a managed pointer. */
TEST_CASE(language_pointer_parameters_infer_scope)
{
    parse(
        "Scope :: library { Row :: struct { x: int; } "
        "Write :: (written: ^int) { written^ = 1; } "
        "Member :: (row: ^Row) { row.x += 1; } "
        "Forward :: (forwarded: ^int) { Write(forwarded); } "
        "Field :: (fielded: ^Row) { Write(&fielded.x); } "
        "Loop :: (looped: ^int, n: int) { if n > 0 { Loop(looped, n - 1); } } "
        "Keep :: (kept: ^int) -> ^int { return kept; } "
        "Pass :: (passed: ^int) -> ^int { return Keep(passed); } "
        "Optional :: (maybe: ^int?) {} "
        "Shadow :: (shadowed: ^int) { if true { shadowed := 1; } } }"
    );
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    static const struct
    {
        const char *name;
        bool scoped;
    } cases[] = { { "written", true },   { "row", true },
                  { "forwarded", true }, { "fielded", true },
                  { "looped", true },    { "kept", false },
                  { "passed", false },   { "maybe", false },
                  { "shadowed", false } };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        const script_symbol *symbol = parameter_named(cases[i].name);
        REQUIRE(symbol);
        bool scoped = (symbol->type.flags & SCRIPT_TYPE_REFERENCE) != 0;
        REQUIRE(scoped == cases[i].scoped);
        REQUIRE(scoped || symbol->type.storage == PIN_POINTER);
    }
    parse(
        "Labels :: library { Store :: (into: ^int) { into^ = 2; } "
        "Relay :: (into: ^int) { Store(into: into); } }"
    );
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        REQUIRE(symbol->kind != SCRIPT_PARAMETER || (symbol->type.flags & SCRIPT_TYPE_REFERENCE));
    }
    return true;
}
/* Only a scoped parameter accepts an engine-owned location. */
TEST_CASE(language_scoped_pointers_accept_engine_locations)
{
    parse(
        "Weapon :: behavior { elapsed: number = 0; "
        "Advance :: (value: ^number, dt: number) { value^ += dt; } "
        "Run :: () { Advance(&elapsed, 1.0); Advance(&self.Position.x, 1.0); } "
        "}"
    );
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    parse(
        "Weapon :: behavior { elapsed: number = 0; "
        "Keep :: (value: ^number) -> ^number { return value; } "
        "Run :: () { Keep(&elapsed); } }"
    );
    REQUIRE(!syntax->diagnostic_count && !analysis->valid);
    REQUIRE(diagnostic("Managed pointer requires a writable local or record field"));
    return true;
}

/* `has` names a map method only on a value; an authored `has` stays a call. */
TEST_CASE(language_authored_map_method_names_preserve_callable_identity)
{
    parse(
        "Names :: library { has :: (value: bool) -> bool { return value; } "
        "Use :: () -> int { if has(value: true) { return 1; } return 0; } }"
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
    REQUIRE(calls == 1);
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
        "Bad :: system() { pending: retain [string: task]; }",
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
        "Motion :: system(position: ^Position) { step := position.x; Apply :: "
        "(value: number) { "
        "position.y = value; } Apply(step); }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse(
        "Motion :: system(position: ^Position) { step := position.x; Apply :: "
        "() { position.y = "
        "step; } Apply(); }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Pass system invocation locals as parameters across callable boundaries"));
    parse("Motion :: system() { step := 1; on Tick :: () { step += 1; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Pass system invocation locals as parameters across callable boundaries"));
    return true;
}

TEST_CASE(language_optional_component_requires_presence_guard)
{
    parse("Motion :: system(position: ^Position?) { position.x = 1; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    const script_diagnostic *error = diagnostic(
        "Nullable pointer requires a presence guard before dereference"
    );
    int receiver = (int)(strstr(analysis->text, "position.x") - analysis->text);
    REQUIRE(
        error && error->range.start == receiver &&
        error->range.end == receiver + (int)strlen("position")
    );
    parse("Motion :: system(position: ^Position?) { if (position) { position.x = 1; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse(
        "Motion :: system(position: ^Position?) { Change :: () { mutate(); } "
        "if position { "
        "Change(); position.x = 1; } }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Nullable pointer requires a presence guard before dereference"));
    return true;
}
/* `??` needs an optional on its left and a fallback of its value type; an
 * optional fallback keeps the result optional. */
TEST_CASE(language_coalesce_types_its_operands)
{
    parse("L :: library { F :: (a: int?, b: int?) -> int { c: int? = a ?? b; return a ?? b ?? 0; } }");
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    parse("L :: library { F :: (a: int?) -> bool { return a ?? 1 < 2; } }");
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    const char *invalid[] = {
        "L :: library { F :: (a: int) -> int { return a ?? 1; } }",
        "L :: library { F :: (a: int?) -> int { return a ?? \"x\"; } }",
        "L :: library { F :: (a: int?, b: int?) -> int { return a ?? b; } }",
        "Motion :: system(position: ^Position?) { x := position ?? 1; }",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        parse(invalid[i]);
        REQUIRE(!syntax->diagnostic_count && !analysis->valid);
    }
    parse(invalid[0]);
    REQUIRE(diagnostic("?? requires an optional value on its left"));
    return true;
}
TEST_CASE(language_reductions_resolve_stored_fields_and_membership)
{
    parse(
        "Motion :: system(position: ^Position? , if has(Position)) { Amount :: "
        "() -> number { "
        "return 2; } if position { reduce position.x += Amount(); reduce "
        "min(position.x, 3); "
        "reduce max(position.y, 4); } }"
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
    parse("Motion :: system(position: ^Position) { reduce self.Position.x += 1; }");
    REQUIRE(analysis->valid);
    parse("Motion :: system(position: ^Position?) { if position != nil { reduce position.x += 1; } }");
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_reductions_require_system_component_fields)
{
    const char *invalid_targets[] = {
        "Motion :: system() { total := 0; reduce total += 1; }",
        "Motion :: system() { if true { total := 0; reduce total += 1; } }",
        "Motion :: system(position: ^Position) { reduce position^ += "
        "position^; }",
        "Motion :: system(position: ^Placement) { reduce position.x += 1; }",
        "Motion :: system(position: ^Position?) { reduce position.x += 1; }",
    };
    for (int i = 0; i < (int)(sizeof invalid_targets / sizeof *invalid_targets); i++) {
        parse(invalid_targets[i]);
        REQUIRE(syntax->diagnostic_count == 0);
        REQUIRE(!analysis->valid);
    }
    parse("Motion :: behavior { on Tick :: () { reduce self.Position.x += 1; } }");
    REQUIRE(diagnostic("Reduction requires a synchronous system body"));
    parse(
        "Motion :: system(position: ^Position) { Update :: () { reduce "
        "position.x += 1; } "
        "Update(); }"
    );
    REQUIRE(diagnostic("Reduction requires a synchronous system body"));
    return true;
}
TEST_CASE(language_reduction_producers_reject_conflicting_component_values)
{
    const char *sources[] = {
        "Motion :: system(position: ^Position) { reduce position.x += "
        "position.y; }",
        "Motion :: system(position: ^Position) { position.y = 0; reduce "
        "position.x += 1; }",
        "Motion :: system(position: ^Position) { if position.y > 0 { reduce "
        "position.x += 1; } }",
        "Motion :: system(position: ^Position , if position.y > 0) { reduce "
        "position.x += 1; }",
        "Motion :: system(position: ^Position , if changed(position)) { reduce "
        "position.x += 1; }",
        "Motion :: system(position: ^Position) { if true { snapshot := "
        "position^; reduce "
        "position.x += 1; } }",
        "Motion :: system(position: ^Position) { Read :: () -> number { return "
        "position.y; } reduce "
        "position.x += Read(); }",
        "Motion :: system(position: ^Position) { reduce position.x += 1; "
        "self.Position.y = 2; }",
        "Motion :: system(position: ^Position) { bind position.y = 2; reduce "
        "position.x += 1; }",
        "Motion :: system(position: ^Position) { Read :: (n: int) -> number { "
        "if n > 0 { return "
        "Read(n - 1); } return position.y; } reduce position.x += Read(1); }",
        "Motion :: system(position: ^Position) { Owner :: () -> entity { "
        "position.y = 1; return "
        "self; } reduce Owner().Position.x += 1; }",
    };
    for (int i = 0; i < (int)(sizeof sources / sizeof *sources); i++) {
        parse(sources[i]);
        REQUIRE(syntax->diagnostic_count == 0);
        REQUIRE(
            diagnostic("Reduction producer cannot read or write component 'Position' outside reductions")
        );
    }
    parse("Motion :: system(position: ^Position) { mutate(); reduce position.x += 1; }");
    REQUIRE(diagnostic("Reduction producer cannot call an opaque component accessor"));
    parse("Motion :: system(position: ^Position) { opaque_host(); reduce position.x += 1; }");
    REQUIRE(diagnostic("Reduction producer cannot call an opaque component accessor"));
    parse("Motion :: system(position: ^Position, other: ^Placement) { reduce position.x += other.y; }");
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_reduction_producers_cannot_suspend_or_start_tasks)
{
    parse("Motion :: system(position: ^Position) { reduce position.x += 1; yield; }");
    REQUIRE(diagnostic("Reduction requires a synchronous system body without tasks"));
    parse(
        "Motion :: system(position: ^Position) { Work :: () { yield; } Work(); "
        "reduce position.x "
        "+= 1; }"
    );
    REQUIRE(diagnostic("Reduction requires a synchronous system body without tasks"));
    return true;
}
TEST_CASE(language_unordered_reductions_require_unbounded_isolated_components)
{
    parse(
        "Motion :: system(position: ^Position) { reduce unordered position.x "
        "+= 1; reduce "
        "unordered position.y += 2; }"
    );
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
    parse("Motion :: system(unordered: ^Position) { reduce unordered.x += 1; }");
    REQUIRE(analysis->valid);
    parse(
        "Motion :: system(position: ^Position) { reduce unordered position.x "
        "+= 1; reduce "
        "position.y += 2; }"
    );
    REQUIRE(diagnostic("A component cannot mix ordered and unordered reductions in one system"));
    parse(
        "Motion :: system(position: ^Position) { reduce min(position.y, 2); "
        "reduce unordered "
        "position.x += 1; }"
    );
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
    syntax = script_parse("Motion :: system(position: ^Position) { reduce unordered position.x += 1; }");
    analysis = script_analyze(syntax, &bounded);
    REQUIRE(diagnostic("Unordered reduction requires an unbounded component field"));
    cleanup();
    syntax = script_parse("Motion :: system(position: ^Position) { reduce position.x += 1; }");
    analysis = script_analyze(syntax, &bounded);
    REQUIRE(analysis->valid);
    cleanup();
    bounded_fields[0].range_contract = false;
    syntax = script_parse("Motion :: system(position: ^Position) { reduce unordered position.x += 1; }");
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
        "Follow :: system(p: ^Position, aim: ^Position? from Targets, up: "
        "^Placement from "
        "ancestor(ChildOf)  , if (Position or Placement) and not "
        "self.Placement) { if aim { p.x = "
        "aim.x + up.x; } }"
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
    syntax = script_parse("Follow :: system(target: ^Position from Targets  , if ! Position) { target.x += 1; }");
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && analysis->valid);
    parse("Bad :: system(p: ^Position  , if ! Position) { p.x = 1; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("A component cannot be both a parameter and excluded"));
    parse("Inherit :: system(p: ^Position from ancestor(ChildOf)  , if ! Position) { p.x += 1; }");
    REQUIRE(analysis->valid);
    parse(
        "Inherit :: system(p: ^Position from self_or_ancestor(ChildOf)  , if ! "
        "Position) { p.x += "
        "1; }"
    );
    REQUIRE(analysis->valid);
    cleanup();
    syntax = script_parse("Bad :: system(p: ^Position from Likes ) { p.x = 1; }");
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && !analysis->valid);
    REQUIRE(
        diagnostic("Scalar query path requires an exclusive relationship; iterate multiple targets explicitly")
    );
    parse("Bad :: system(p: ^Position  , if target.Position) { p.x = 1; }");
    REQUIRE(!analysis->valid && diagnostic("Unknown query relationship 'target'"));
    parse("Safe :: system(p: ^Position? ) { if p { p.x = 1; p.y = 2; } }");
    REQUIRE(analysis->valid);
    parse("Waiting :: system(p: ^Position? ) { if p { yield; p.x = 1; } }");
    REQUIRE(
        !analysis->valid &&
        diagnostic("Nullable pointer requires a presence guard before dereference")
    );
    script_catalog_capability structural_capabilities[sizeof capabilities / sizeof *capabilities];
    memcpy(structural_capabilities, capabilities, sizeof structural_capabilities);
    structural_capabilities[2].effects = SCRIPT_EFFECT_STRUCTURAL;
    queries.capabilities = structural_capabilities;
    queries.capability_count = (int)(sizeof structural_capabilities /
                                     sizeof *structural_capabilities);
    cleanup();
    syntax = script_parse("Changed :: system(p: ^Position? ) { if p { mutate(); p.x = 1; } }");
    analysis = script_analyze(syntax, &queries);
    REQUIRE(
        !analysis->valid &&
        diagnostic("Nullable pointer requires a presence guard before dereference")
    );
    const script_catalog_relationship shadow = { .id = 601,
                                                 .name = "ChildOf",
                                                 .exclusive = true };
    queries.relationships = &shadow;
    queries.relationship_count = 1;
    cleanup();
    syntax = script_parse("Builtin :: system(p: ^Position from ChildOf ) { for target in self.ChildOf {} }");
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
        "Follow :: system(p: ^Position from self  , aim: ^Position? from "
        "Targets , up: ^Placement "
        "from ancestor(ChildOf) , if self.Position and not Targets.Placement "
        "and "
        "ancestor(ChildOf).Position and has(Likes)) { if aim { p.x = aim.x + "
        "up.x; } }"
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
        "Follow :: system(p: ^Position  , aim: ^Position from Targets) { p.x = "
        "aim.x; }",
        "Follow :: system(  aim: ^Position? from Targets , if not "
        "Targets.Placement) { if aim { "
        "aim.x = 1; } }",
        "Follow :: system(p: ^Position  , if has(Targets.Position) or not "
        "has(Likes)) { p.x = 1; }",
        "Follow :: system(p: ^Position  , if "
        "self_or_ancestor(ChildOf).Placement) { p.x = 1; }",
        "Follow :: system(p: ^Position  , up: ^Position from ancestor(/* "
        "parent */ ChildOf /* edge "
        "*/) , if has(/* any target */ Likes) and self_or_ancestor(/* provider "
        "*/ "
        "ChildOf).Position) { p.x = up.x; }",
    };
    for (size_t i = 0; i < sizeof valid / sizeof *valid; i++) {
        cleanup();
        syntax = script_parse(valid[i]);
        analysis = script_analyze(syntax, &queries);
        REQUIRE(syntax->diagnostic_count == 0 && analysis->valid);
    }
    const char *invalid[] = {
        "Bad :: system(  p: ^Targets.Position from self) {}",
        "Bad :: system(  p: ^Position from Likes) {}",
        "Bad :: system(p: ^Position  , if Likes.Placement) {}",
        "Bad :: system(p: ^Position  , aim: ^Position? from Targets) { if aim "
        "{ yield; p.x = "
        "aim.x; } }",
        "Bad :: system(p: ^Position  , aim: ^Position? from Targets , if "
        "Targets.Position) { p.x = "
        "aim.x; }",
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
        "Follow :: system(p: ^Position  , target in self.Targets , next in "
        "target.Likes , aim: "
        "^Position? from next , if Positive(p.x) and (aim and aim.x > p.x) and "
        "changed(aim) and "
        "has(target.Likes) and not has(next.Placement)) { Positive :: (x: "
        "number) -> bool { "
        "positive := x > 0; return positive; } if aim { p.x = aim.x; } }"
    );
    analysis = script_analyze(syntax, &queries);
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(stderr, "query generator: %s\n", analysis->diagnostics[i].message);
        }
    }
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
        "Bad :: system(  target in later.Targets , later in self.Targets) {}",
        "Bad :: system(  target in target.Targets) {}",
        "Bad :: system(  target in self.Unknown) {}",
        "Bad :: system(  target in self.Targets , target in self.Targets) {}",
        "Bad :: system(  target in self.Targets , p: ^Position? from target , "
        "if p.x > 0) {}",
        "Bad :: system(  target in self.Targets , p: ^Position? from target , "
        "if p or p.x > 0) {}",
        "Bad :: system(  target in self.Targets , p: ^Position from target) { "
        "bind p.x = 1; }",
        "Bad :: system(p: ^Position  , if changed(p.x)) {}",
        "Bad :: system(p: ^Position  , if changed(p, p)) {}",
        "Bad :: system(p: ^Position  , if p.x) {}",
        "Bad :: system(p: ^Position  , if Mutating()) { Mutating :: () -> bool "
        "{ mutate(); return "
        "true; } }",
        "Bad :: system(p: ^Position  , if Deferred()) { Deferred :: () -> bool "
        "{ yield; return "
        "true; } }",
        "Bad :: system(p: ^Position  , if await Deferred()) { A :: () {} B :: "
        "() {} C :: () {} D "
        ":: () {} E :: () {} F :: () {} Deferred :: () -> bool { yield; return "
        "true; } }",
        "Bad :: system(p: ^Position  , if enabled) { enabled: retain = true; }",
        "Bad :: system(p: ^Position  , if enabled) { enabled := true; }",
        "Bad :: system(p: ^Position  , if ReadLocal()) { enabled := true; "
        "ReadLocal :: () -> bool "
        "{ return enabled; } }",
        "Bad :: system(p: ^Position  , if ReadState()) { enabled: retain = "
        "true; ReadState :: () "
        "-> bool { return enabled; } }",
        "Bad :: system(p: ^Position  , if ReadState()) { enabled: retain = "
        "true; ReadState :: () "
        "-> bool { return Inner(); } Inner :: () -> bool { return enabled; } }",
        "Bad :: system(p: ^Position, if dt > 0) {}",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        cleanup();
        syntax = script_parse(invalid[i]);
        analysis = script_analyze(syntax, &queries);
        REQUIRE(syntax->diagnostic_count == 0);
        if (analysis->valid) {
            fprintf(stderr, "query filter unexpectedly accepted case %zu: %s\n", i, invalid[i]);
        }
        REQUIRE(!analysis->valid);
    }
    REQUIRE(
        diagnostic("Query predicate cannot read system state, body locals or event payload; test invocation values in the system body")
    );
    cleanup();
    syntax = script_parse(
        "Many :: system(  a in self.Targets , b in a.Targets , c in b.Targets "
        ", d in c.Targets , e "
        "in d.Targets , f in e.Targets , g in f.Targets , h in g.Targets , i "
        "in h.Targets) {}"
    );
    analysis = script_analyze(syntax, &queries);
    REQUIRE(syntax->diagnostic_count == 0 && !analysis->valid);
    REQUIRE(diagnostic("Query supports at most eight target variables"));
    return true;
}
TEST_CASE(language_derived_component_is_read_only)
{
    parse("Follow :: system(placement: ^Placement, position: ^Position) { position.x = placement.x; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse("Follow :: system(placement: ^Placement) { placement.x = 1; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && diagnostic("Assignment requires a writable value"));
    parse("Follow :: behavior { on Tick :: (dt: number s) { self.Placement.y = dt; } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && diagnostic("Assignment requires a writable value"));
    return true;
}
TEST_CASE(language_numeric_literal_storage_limits)
{
    parse("Limits :: behavior { sample: i8 = 128; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid);
    const script_diagnostic *error = diagnostic("Numeric literal is outside the range of i8");
    int literal = (int)(strstr(analysis->text, "128") - analysis->text);
    REQUIRE(error && error->range.start == literal && error->range.end == literal + 3);
    parse("Limits :: behavior { sample: i64 = 9223372036854775807; }");
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
    snprintf(source, sizeof source, "L :: library { F :: (x: int) -> u8 { return %s; } }", body);
    parse(source);
    const script_expression *conversion = expression_of(SCRIPT_EXPR_CAST, PIN_U8);
    return analysis->valid && conversion && conversion->conversion == mode;
}
TEST_CASE(language_numeric_conversions_check_modes_and_constants)
{
    REQUIRE(converts("u8(x)", BEHAVIOR_CAST_EXACT));
    REQUIRE(converts("u8(wrap: x)", BEHAVIOR_CAST_WRAP));
    parse("L :: library { F :: (x: int) -> u8 { return u8(clamp: x); } }");
    REQUIRE(diagnostic("A conversion argument may only be named wrap"));
    parse("L :: library { F :: () -> u8 { return u8(wrap: 300); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { X: int : 300; F :: () -> u8 { return u8(X + 1); } }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Constant does not fit in u8; use u8(wrap: value)"));
    parse("L :: library { X: int : 200; F :: () -> u8 { return u8(X); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (x: int) -> u8 { return u8(round: x); } }");
    REQUIRE(diagnostic("A conversion argument may only be named wrap"));
    parse("L :: library { F :: (x: number) -> int { return int(wrap: x); } }");
    REQUIRE(diagnostic("wrap converts between integer types"));
    parse("L :: library { F :: () -> int { return int(wrap: 3.5); } }");
    REQUIRE(analysis->diagnostic_count == 1 && diagnostic("wrap converts between integer types"));
    parse("L :: library { F :: () -> u64 { return u64(18446744073709551615); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: () -> int { return int(wrap: (0xFFFFFFFFFFFFFFFF)); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (x: int) -> u8 { return u8(x, x); } }");
    REQUIRE(diagnostic("Conversion takes one value"));
    return true;
}
/* `emit target.Event(payload)` and `emit Event(target, payload)` send the
 * event through the catalog's sender; a plain sender call is refused. */
TEST_CASE(language_emit_sends_events_to_their_target)
{
    static const script_catalog_parameter target[] = {
        { .name = "Entity", .type = { .storage = PIN_HANDLE, .nominal = 40 } }
    };
    static const script_catalog_parameter payload[] = {
        { .name = "Amount", .type = { .storage = PIN_I32 } }
    };
    static const script_catalog_capability events[] = {
        { .id = 60,
          .name = "Send Event",
          .kind = SCRIPT_CAPABILITY_ACTION,
          .parameters = target,
          .parameter_count = 1,
          .event_sender = true },
        { .id = 61,
          .name = "Damage",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = payload,
          .parameter_count = 1 },
    };
    const script_catalog sender = { .capabilities = events,
                                    .capability_count = 2,
                                    .entity_type = 40 };
    const char *valid[] = {
        "L :: library { F :: (other: entity) { emit other.Damage(3) } }",
        "L :: library { F :: (other: entity) { emit Damage(other, Amount: 3) } "
        "}",
        /* An event send never calls the function its event shares a name with. */
        "L :: library { Damage :: (other: entity) { emit Damage(other, 3) } }",
    };
    for (size_t i = 0; i < sizeof valid / sizeof *valid; i++) {
        cleanup();
        syntax = script_parse(valid[i]);
        analysis = script_analyze(syntax, &sender);
        REQUIRE(analysis->valid);
        const script_expression *send = NULL;
        for (int e = 0; e < analysis->expression_count; e++) {
            if (analysis->expressions[e].kind == SCRIPT_EXPR_CALL) {
                send = &analysis->expressions[e];
            }
        }
        REQUIRE(send && send->capability == 60 && send->member == 61 && send->argument_count == 2);
        REQUIRE(send->arguments[0].parameter == 0 && send->arguments[1].parameter == 1);
    }
    const struct
    {
        const char *source, *message;
    } invalid[] = {
        { "L :: library { F :: (other: entity) { send_event(other) } }",
          "Send events with emit target.Event(...)" },
        { "L :: library { F :: (other: entity) { emit other.Missing(3) } }",
          "Unknown event declaration" },
        { "L :: library { F :: (other: entity) { emit other.Damage(\"x\") } }", NULL },
        { "L :: library { F :: () { emit \"x\".Damage(1) } }", NULL },
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        cleanup();
        syntax = script_parse(invalid[i].source);
        analysis = script_analyze(syntax, &sender);
        REQUIRE(
            !analysis->valid && (!invalid[i].message || diagnostic(invalid[i].message))
        );
    }
    return true;
}
/* number is a 32-bit float that f32 also names; f32 widens to f64, and f64
 * becomes number only through number(x) or f32(x). */
TEST_CASE(language_number_is_f32_and_f64_narrows_explicitly)
{
    parse("L :: library { F :: (x: number, y: f32, z: i32) { a := x; b := y; c := z; } }");
    REQUIRE(analysis->valid && local_storage("a") == PIN_F32);
    REQUIRE(local_storage("b") == PIN_F32 && local_storage("c") == PIN_I32);
    parse("L :: library { F :: (x: number) -> f64 { return x; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (x: f64) -> number { return x; } }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Incompatible types: expected number, got f64; convert with number(value)"));
    parse("L :: library { F :: (x: f64) { y: number = 0; y = x; } }");
    REQUIRE(!analysis->valid);
    parse("L :: library { G :: (x: number) {} F :: (x: f64) { G(x); } }");
    REQUIRE(!analysis->valid);
    parse("L :: library { F :: (x: f64) -> number { return number(x) + f32(x); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: () -> f64 { return 0.1; } }");
    const script_expression *wide = constant_of(PIN_F64);
    REQUIRE(analysis->valid && wide && wide->constant.d == 0.1);
    return true;
}
TEST_CASE(language_integers_widen_implicitly_and_narrow_explicitly)
{
    parse("L :: library { F :: (x: u16) -> u32 { return x; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (x: u8) -> number { return x; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (x: int) -> u8 { return x; } }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Incompatible types: expected u8, got int; convert with u8(value)"));
    parse("L :: library { F :: (x: int, y: u32) -> i64 { return i64(x) + i64(y); } }");
    REQUIRE(analysis->valid && expression_of(SCRIPT_EXPR_BINARY, PIN_I64));
    parse("L :: library { F :: (x: u8, y: i8) -> int { return x + y; } }");
    REQUIRE(diagnostic("Mismatched integer types u8 and i8; convert one operand"));
    parse("L :: library { F :: (x: int, y: u32) -> bool { return x < y; } }");
    REQUIRE(diagnostic("Mismatched integer types int and u32; convert one operand"));
    parse("L :: library { F :: (x: i64, y: u64) -> i64 { return x + y; } }");
    REQUIRE(diagnostic("Mismatched integer types i64 and u64; convert one operand"));
    parse("L :: library { F :: (x: int, y: u8) -> int { n := x; n += y; return n; } }");
    REQUIRE(diagnostic("Mismatched integer types int and u8; convert one operand"));
    parse("L :: library { F :: (x: int, y: u8) -> int { n := x; n += int(y) + 1; return n; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { Mask: u32 : 0xFF; F :: () -> u32 { return Mask; } }");
    REQUIRE(analysis->valid && constant_of(PIN_U32));
    parse("L :: library { Small: u8 : 300; }");
    REQUIRE(diagnostic("Numeric literal is outside the range of u8"));
    return true;
}
/* A shift has its left operand's type and an unsigned count of any width. */
TEST_CASE(language_shift_counts_are_unsigned_and_keep_their_type)
{
    parse("L :: library { F :: (x: int, n: int) -> int { return x << n; } }");
    REQUIRE(diagnostic("Shift count must be unsigned; convert it with u32(value)"));
    parse("L :: library { F :: (x: int) -> int { return x << -1; } }");
    REQUIRE(diagnostic("Shift count must not be negative"));
    parse("L :: library { F :: (x: i64, n: u8) -> i64 { return x << n; } }");
    REQUIRE(analysis->valid && expression_of(SCRIPT_EXPR_BINARY, PIN_I64));
    parse("L :: library { F :: (a: i8) -> i8 { return a << 200; } }");
    const script_expression *count = expression_of(SCRIPT_EXPR_CONSTANT, PIN_I64);
    REQUIRE(analysis->valid && count && count->constant.i64 == 200);
    parse("L :: library { F :: (x: i64, n: u32) -> bool { return x == (1 << n); } }");
    REQUIRE(analysis->valid && expression_of(SCRIPT_EXPR_BINARY, PIN_I64));
    parse("L :: library { F :: (n: u32) -> int { y := 1 << n; return y; } }");
    REQUIRE(analysis->valid && expression_of(SCRIPT_EXPR_BINARY, PIN_I32));
    parse("L :: library { F :: (n: u32) -> bool { return 1.5 << n == 0; } }");
    REQUIRE(diagnostic("Numeric literal has a fraction, which int cannot hold"));
    parse("L :: library { F :: (x: u8, n: u64) -> u8 { return x >> n; } }");
    REQUIRE(analysis->valid && expression_of(SCRIPT_EXPR_BINARY, PIN_U8));
    parse("L :: library { F :: (n: u8) -> u64 { return 1 << n; } }");
    REQUIRE(analysis->valid && expression_of(SCRIPT_EXPR_BINARY, PIN_U64));
    /* The count does not type an untyped value: the mask is int, not byte. */
    parse("L :: library { F :: (n: u8) -> int { mask := (1 << n) - 1; return mask; } }");
    REQUIRE(
        analysis->valid && expression_of(SCRIPT_EXPR_BINARY, PIN_I32) &&
        !expression_of(SCRIPT_EXPR_BINARY, PIN_U8)
    );
    parse("L :: library { F :: (x: int) -> int { return x << 3; } }");
    REQUIRE(analysis->valid);
    return true;
}
/* Whether the last parse holds a constant of storage with these bits. */
static bool folded(pin_type storage, uint64_t bits)
{
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *e = &analysis->expressions[i];
        if (e->kind == SCRIPT_EXPR_CONSTANT && e->constant.type == storage &&
            behavior_scalar_bits(e->constant) == bits) {
            return true;
        }
    }
    return false;
}
static bool rejects(const char *source, const char *message, int column)
{
    parse(source);
    const script_diagnostic *found = diagnostic(message);
    return !analysis->valid && found && found->range.column == column;
}
TEST_CASE(language_integer_constants_are_exact_and_must_fit)
{
    parse("L :: library { C: i8 : 127 + 1 - 1; }");
    REQUIRE(analysis->valid && folded(PIN_I8, 127));
    parse("L :: library { F :: () -> i8 { return (127 + 1) / 2; } }");
    REQUIRE(analysis->valid && folded(PIN_I8, 64));
    parse("L :: library { C: int : (1 << 4000) >> 3990; }");
    REQUIRE(analysis->valid && folded(PIN_I32, 1024));
    parse("L :: library { F :: () -> i8 { return (i8(1) << 200) >> 199; } }");
    REQUIRE(analysis->valid && folded(PIN_I8, 2));
    parse("L :: library { F :: () -> u64 { return ~0; } }");
    REQUIRE(analysis->valid && folded(PIN_U64, UINT64_MAX));
    parse("L :: library { F :: () -> u8 { return ~u8(5); } }");
    REQUIRE(analysis->valid && folded(PIN_U8, 250));
    parse("L :: library { C: u8 : u8(wrap: 300); }");
    REQUIRE(analysis->valid && folded(PIN_U8, 44));
    parse("L :: library { F :: (x: int) -> int { return x << 40; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (a: u8) -> u8 { return -a; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (f: number) -> number { return f / 0; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (a: u8) -> u8 { return u8(a + 1); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (f: number) -> int { return int(f + 1); } }");
    REQUIRE(analysis->valid);
    REQUIRE(
        rejects("L :: library { C: int : 1 << 32; }", "Constant value is outside the range of int", 25)
    );
    REQUIRE(
        rejects("L :: library { M: int : -2147483648 / -1; }", "Constant value is outside the range of int", 25)
    );
    REQUIRE(
        rejects("L :: library { F :: () -> int { return 1 << 32; } }", "Constant value is outside the range of int", 40)
    );
    REQUIRE(rejects(
        "L :: library { F :: () -> u8 { return u8(i8(100) + i8(100)); } }",
        "Constant value is outside the range of i8",
        42
    ));
    REQUIRE(
        rejects("L :: library { F :: () -> int { return 1 << 5000; } }", "Constant shift count is too large", 45)
    );
    REQUIRE(
        rejects("L :: library { F :: (a: int) -> int { return a / 0; } }", "Constant division by zero", 46)
    );
    REQUIRE(
        rejects("L :: library { F :: (a: int) -> int { return a % (2 - 2); } }", "Constant division by zero", 46)
    );
    REQUIRE(rejects("L :: library { C :: 1 / 0; }", "Constant division by zero", 21));
    REQUIRE(
        rejects("L :: library { F :: (b: int) -> int { b /= 0; return b; } }", "Constant division by zero", 39)
    );
    REQUIRE(
        rejects("L :: library { F :: () -> u8 { return -u8(1); } }", "Cannot negate an unsigned constant", 39)
    );
    REQUIRE(
        rejects("L :: library { U: u32 : 3; F :: () -> u32 { return -U; } }", "Cannot negate an unsigned constant", 52)
    );
    REQUIRE(rejects(
        "L :: library { F :: () -> u8 { return u8(300); } }",
        "Constant does not fit in u8; use u8(wrap: value)",
        39
    ));
    REQUIRE(rejects(
        "L :: library { F :: () -> u8 { return u8(200 + 100); } }",
        "Constant does not fit in u8; use u8(wrap: value)",
        39
    ));
    REQUIRE(rejects(
        "L :: library { F :: (a: u8) -> bool { return a < -1; } }",
        "Numeric literal is outside the range of u8",
        50
    ));
    REQUIRE(
        rejects("L :: library { F :: (x: int) -> int { return x << (2 - 3); } }", "Shift count must not be negative", 52)
    );
    REQUIRE(
        rejects("L :: library { F :: () -> i8 { return i8(1) << 5000; } }", "Constant shift count is too large", 48)
    );
    REQUIRE(
        rejects("L :: library { Grow :: (b: u8) -> int { a: int = 1; a += b; return a; } }", "Mismatched integer types int and u8; convert one operand", 53)
    );
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
    parse("L :: library { F :: (c: u8) -> bool { return c == '/'; } }");
    REQUIRE(character(PIN_U8, '/', false));
    parse("L :: library { F :: (c: int) -> bool { return c >= 'A' && c <= 'Z'; } }");
    REQUIRE(analysis->valid && !constant_of(PIN_F64));
    parse("L :: library { Slash: u8 : 'é'; }");
    REQUIRE(character(PIN_U8, 233, false));
    parse("L :: library { Euro: u8 : '€'; }");
    REQUIRE(diagnostic("Character literal is outside the range of u8"));
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
    parse("L :: library { F :: () -> f64 { return 'a'; } }");
    REQUIRE(diagnostic("A character literal needs an integer or char type"));
    return true;
}
TEST_CASE(language_char_widens_to_int_and_narrows_explicitly)
{
    parse("L :: library { F :: (c: char) -> int { return c; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char) -> i64 { return c; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char) -> u8 { return c; } }");
    REQUIRE(diagnostic("Incompatible types: expected u8, got char; convert with u8(value)"));
    parse("L :: library { F :: (n: int) -> char { return n; } }");
    REQUIRE(diagnostic("Incompatible types: expected char, got int; convert with char(value)"));
    parse("L :: library { F :: (n: int) -> char { return char(n); } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char) -> u8 { return u8(c); } }");
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
    parse("L :: library { F :: (d: f64) -> char { return char(d); } }");
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
    /* Beside a wide unsigned operand a char takes part as uint, not int64. */
    parse("L :: library { F :: (c: char, u: u32) -> u32 { return c + u; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char, u: u64) -> u64 { return c + u; } }");
    REQUIRE(analysis->valid);
    parse("L :: library { F :: (c: char, b: u8) -> int { return c + b; } }");
    REQUIRE(diagnostic("Mismatched integer types char and u8; convert one operand"));
    parse("L :: library { F :: (c: char, b: u8) -> int { return c + int(b); } }");
    REQUIRE(analysis->valid);
    /* A compile-time call keeps its declared char result. */
    parse(
        "L :: library { Code :: () -> char { return 'a'; } Folded :: Code(); "
        "F :: () -> char { return Folded; } }"
    );
    REQUIRE(analysis->valid);
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
    parse("L :: library { F :: (m: [string: int]) { for < k, v in m {} } }");
    REQUIRE(diagnostic("for < reverses only ranges and arrays"));
    return true;
}
/* A function with results ends in a terminating statement, as Go and Odin
 * require: a return, an if/else or exhaustive switch whose every branch
 * terminates, or a while (true) that nothing breaks out of. */
TEST_CASE(language_functions_with_results_must_return)
{
    const char *missing[] = {
        "L :: library { F :: (n: int) -> int { if n > 0 { return 1; } } }",
        "L :: library { F :: (a: number) -> (r: number) { r = a; } }",
        "L :: library { F :: (c: bool) -> int { while (true) { if c { break; } "
        "return 1; } } }",
        "L :: library { F :: (n: int) -> int { switch n { case 1: return 1; "
        "case 2: return 2; } } }",
        "L :: library { F :: (n: int) -> int { while (n > 0) { return 1; } } }",
        "L :: library { F :: (n: int) -> int { for i in 0..n { return i; } } }",
        "L :: library { F :: (n: int) -> int { while (true) { switch n { case "
        "1: break; case: return 1; } } } }",
        "L :: library { F :: (n: int) { if n > 0 { return 1; } } }",
        "Run :: behavior { F :: () -> int { on Begin :: () { } } }",
    };
    for (size_t i = 0; i < sizeof missing / sizeof *missing; i++) {
        parse(missing[i]);
        REQUIRE(!analysis->valid && diagnostic("Missing return"));
    }
    const char *ending[] = {
        "L :: library { F :: (n: int) -> int { if n > 0 { return 1; } else { "
        "return 2; } } }",
        "L :: library { F :: (a: number) -> (r: number) { r = a; return; } }",
        "L :: library { F :: () -> int { while (true) { return 1; } } }",
        "L :: library { F :: (c: bool) -> int { while (true) { while (c) { "
        "break; } return 1; } } }",
        "L :: library { F :: (n: int) -> int { switch n { case 1: return 1; "
        "case: return 2; } } }",
        "L :: library { Choice :: union { A | B }; "
        "F :: (n: int) -> Choice { while (true) { if n > 0 { return Choice.A; "
        "} return Choice.B; } } }",
        "L :: library { F :: (n: int) { if n > 0 { return; } } }",
        "L :: library { F :: (n: int) -> int { if n > 0 { return 1; } else if "
        "n < 0 { return -1; } else { return 0; } } }",

    };
    for (size_t i = 0; i < sizeof ending / sizeof *ending; i++) {
        parse(ending[i]);
        REQUIRE(analysis->valid);
    }
    /* A union result still needs a value on every return path. */
    parse("L :: library { Choice :: union { A | B }; F :: () -> (value: Choice) { return; } }");
    REQUIRE(diagnostic("Union result 'value' requires a value on every return path"));
    /* The diagnostic sits on the closing brace that control would fall past. */
    parse("L :: library { F :: (n: int) -> int { if n > 0 { return 1; } } }");
    const script_diagnostic *error = diagnostic("Missing return");
    int brace = (int)(strstr(analysis->text, "} } }") - analysis->text) + 2;
    REQUIRE(error && error->range.start == brace && error->range.end == brace + 1);
    return true;
}
TEST_CASE(language_hex_literal_fills_integer_storage)
{
    parse("Colors :: behavior { color: u32 = 0xf7d308ff; }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_U32) && constant_of(PIN_U32)->constant.u == 0xf7d308ffu);
    parse("Colors :: behavior { color: u32 = 0xef; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_U32) && constant_of(PIN_U32)->constant.u == 0xef);
    parse("Colors :: behavior { mask: u64 = 0xffffffffffffffff; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_U64) && constant_of(PIN_U64)->constant.u64 == UINT64_MAX);
    parse("Colors :: behavior { mask: int = -0x10; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_I32) && constant_of(PIN_I32)->constant.i == -16);
    parse("Colors :: behavior { mask: i8 = 0xff; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of i8"));
    parse("Limits :: behavior { mask: i64 = 0x7fffffffffffffff; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_I64) && constant_of(PIN_I64)->constant.i64 == INT64_MAX);
    parse("Limits :: behavior { mask: i64 = -0x8000000000000000; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_I64) && constant_of(PIN_I64)->constant.i64 == INT64_MIN);
    parse("Limits :: behavior { mask: i64 = 0x8000000000000000; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of i64"));
    parse("Limits :: behavior { mask: i64 = -0x8000000000000001; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of i64"));
    parse("Limits :: behavior { mask: u64 = 0x10000000000000000; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of u64"));
    parse("Limits :: behavior { mask: u64 = -0x1; }");
    REQUIRE(!analysis->valid);
    REQUIRE(diagnostic("Numeric literal is outside the range of u64"));
    parse("Limits :: behavior { count: int = 1e2; }");
    REQUIRE(analysis->valid);
    REQUIRE(constant_of(PIN_I32) && constant_of(PIN_I32)->constant.i == 100);
    return true;
}
TEST_CASE(language_vector_operator_resolves_catalog_identity)
{
    parse("Motion :: system(position: ^Position) { position^ = position^ + position^; }");
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
        "Effects :: library { Run :: (value: ^int) { value^ = 2; } }",
        "Effects :: library { Row :: struct { value: int; }; "
        "Run :: (row: ^Row) { row.value = 2; } }",
        "Effects :: library { Row :: struct { value: int; }; "
        "Run :: (rows: ^(Row[])) { rows^[0].value += 2; } }",
        "Effects :: library { Run :: (values: ^(int[])) { values^[0] = 2; } }",
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
    parse("Motion :: system(position: ^Position) { VectorAdd(position^, position^); mutate(); }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("VectorAdd returns a new value and changes nothing; assign its result"));
    parse(
        "Motion :: system(position: ^Position) { action sum = "
        "VectorAdd(position^, position^); "
        "sum(); mutate(); }"
    );
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(!analysis->valid && analysis->diagnostic_count == 1);
    REQUIRE(diagnostic("sum returns a new value and changes nothing; assign its result"));
    parse("Values :: library { One :: () -> (count: int) { return 1; } Use :: () -> () { One(); } }");
    REQUIRE(syntax->diagnostic_count == 0);
    REQUIRE(analysis->valid);
    parse(
        "Motion :: system(position: ^Position) { position^ = "
        "VectorAdd(position^, position^); "
        "mutate(); }"
    );
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
        "Tuples :: library { Pair :: () -> (a: f64, b: f64) { "
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
        "Tuples :: library { Pair :: () -> (a: f64, b: f64) { "
        "return(1, 2) } "
        "First :: () { return Pair().a } }"
    );
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_invalid_source_retains_bounded_diagnostics)
{
    const char *sources[] = {
        "Broken :: behavior { on Tick :: () { sample := ; }",
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
        "    level: number\n"
        "    stored: number charge\n"
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
    REQUIRE(field_is(component_field(c, "level"), "number", NULL, 0, 0));
    REQUIRE(field_is(component_field(c, "stored"), "number", "charge", 0, 0));
    REQUIRE(field_is(component_field(c, "measured"), "int", "u/s", 0, 0));
    REQUIRE(field_is(component_field(c, "at"), "Position", NULL, 0, 0));
    script_component_free(c);
    /* Unit names stay free names outside a field's type, and executable
     * source cannot declare a component. */
    parse("S :: system() {\n    if true {\n        s := 2\n        u := s * 3\n        component := u\n    }\n}\n");
    REQUIRE(analysis->valid);
    parse("C :: component {\n    a: number\n}\n");
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
        "    a: number\n"
        "    d: number[]\n"
        "    e: 0..1 number\n"
        "}\n"
    ));
    REQUIRE(component_diagnostic("A range goes from a smaller finite number to a larger one"));
    REQUIRE(component_diagnostic("Unknown type or unit 'flaot'")->range.line == 3);
    REQUIRE(component_diagnostic("A unit's power is ^2 or ^3"));
    REQUIRE(component_diagnostic("Field 'a' is declared twice"));
    REQUIRE(component_diagnostic("A component field holds a number, a scalar or a struct"));
    REQUIRE(component_diagnostic("'number' is a type, not a unit")->range.line == 7);
    REQUIRE(acount(component_diagnostics) == 6);
    REQUIRE(!read_component("Empty :: component {\n}\n"));
    REQUIRE(component_diagnostic("A component declares at least one field"));
    REQUIRE(!read_component("\"Quoted\" :: component {\n    a: number\n}\n"));
    REQUIRE(syntax->diagnostic_count > 0);
    REQUIRE(acount(component_diagnostics) == syntax->diagnostic_count);
    REQUIRE(!read_component("S :: system() {\n}\n"));
    REQUIRE(acount(component_diagnostics) == 1);
    REQUIRE(component_diagnostic("A component source declares one component"));
    /* Keywords are contextual data-member names, not storage declarations. */
    script_component *contextual = read_component("C :: component {\n    state: number\n}\n");
    REQUIRE(contextual && !syntax->diagnostic_count && !acount(component_diagnostics));
    REQUIRE(acount(contextual->fields) == 1 && !strcmp(contextual->fields[0].name, "state"));
    REQUIRE(field_is(&contextual->fields[0], "number", NULL, 0, 0));
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
    snprintf(source, sizeof source, "Move :: system(body: ^Body) {\nif true {\n%s}\n}\n", body);
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
        "n := 0.0\n"
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
    measure("copy: Body = {x: body.speed}; body^ = copy;\n");
    REQUIRE(!analysis->valid && diagnostic("Units differ: u = u/s"));
    measure("copy: Body = {x: dt}; body.x = copy.x;\n");
    REQUIRE(!analysis->valid && diagnostic("Units differ: u = s"));
    measure("copy: Body = {x: body.speed * dt}; body.x = copy.x;\n");
    REQUIRE(analysis->valid);
    return true;
}
TEST_CASE(language_units_check_empty_struct_defaults)
{
    measure("copy: Body = null; body^ = copy;\n");
    REQUIRE(analysis->valid);
    parse("Reset :: behavior { Pair :: () -> (a: int, b: int) { return (1, 2) } on Tick :: () { if true { copy := Pair(); copy = null; } } }");
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
    syntax = script_parse("Move :: system(body: ^Body) {\n    body.x = (body.speed + 1");
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
        "Move :: system(body: ^Body) { elapsed := dt; if true { body.x = "
        "elapsed; } }",
        "Move :: system(body: ^Body) { elapsed: retain = dt; if true { body.x "
        "= elapsed; } }",
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

    cleanup();
    syntax = script_parse("Move :: system(body: ^Body) { elapsed: retain = dt; if true { elapsed = body.x; } }");
    analysis = script_analyze(syntax, &measured);
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
        "Charge :: (value: number charge) -> number charge { return value; } "
        "First :: (value: u) -> u { return Later(value); } "
        "Later :: (value: u) -> u { if value > 0 { return First(value - 1); } "
        "return value; } "
        "Borrow :: (value: ^number u, other: number u) { value^ = other; } "
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
            plain = s->type.storage == PIN_I32 && !s->unit;
        }
        if (!strcmp(s->name, "speed")) {
            measured_parameter = s->type.storage == PIN_F32 && s->unit &&
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
        { "M :: library { Bad :: (distance: ^number u, elapsed: s) { "
          "distance^ "
          "= elapsed; } }",
          "Units differ: u = s" },
        { "M :: library { Bad :: (body: Body) { Later(&body.speed); } Later "
          ":: "
          "(distance: ^number u) {} }",
          "Units differ: u argument u/s" },
        { "M :: library { Bad :: (distance: u = dt) {} }", "Units differ: u default s" },
        { "M :: library { Bad :: () -> (Distance: u = dt) { return } }",
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
                                                 .type = { .storage = PIN_F32 } };
    const script_catalog_parameter result = { .name = "Elapsed",
                                              .unit = "s",
                                              .type = { .storage = PIN_F32 } };
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
    syntax = script_parse("M :: behavior { on Tick :: (Elapsed: number) { self.Body.x = Elapsed; } }");
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
    REQUIRE(diagnostic("Missing return"));
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
    script_component *component = read_component("Status :: component { Cancelled: number }");
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
        "Unions :: library { Run :: (pending: task(number)) -> number { "
        "result := await pending; if result is not Cancelled { return result; "
        "} return 0; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Work :: () -> number { yield; return 0; } "
        "Run :: () -> number { pending: task(number) = Work(); result := await "
        "pending; "
        "if result is number value { return value; } return 1; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Run :: (pending: task(number)[]) -> number { "
        "result := await pending[0]; if result is not Cancelled { return "
        "result; } return 0; } }"
    );
    REQUIRE(analysis->valid);
    parse(
        "Unions :: library { Run :: (pending: task(number)[]) -> number { "
        "result := await pending; return 0; } }"
    );
    REQUIRE(!analysis->valid);
    parse("Unions :: library { Run :: (pending: task(number)[]) -> bool { return !pending; } }");
    REQUIRE(!analysis->valid);
    parse(
        "Unions :: library { Work :: () -> number? { yield; return nil; } "
        "Run :: () -> number { pending := Work(); result := await pending; if "
        "result is not Cancelled { value := result; if value { return value; } "
        "} return 0; } }"
    );
    REQUIRE(analysis->valid);
    return true;
}

TEST_CASE(language_approved_float_conversion_admission)
{
    bool agreed = true;
    for (size_t i = 0; i < sizeof FLOAT_CONVERSION_CASES / sizeof *FLOAT_CONVERSION_CASES;
         i++) {
        const float_conversion_case *item = &FLOAT_CONVERSION_CASES[i];
        cleanup();
        syntax = script_parse(item->source);
        REQUIRE(syntax->diagnostic_count == 0);
        script_package_document document = { "facts/Main.chibi", "facts/Main.chibi", syntax };
        script_package_options options = { .documents = &document,
                                           .document_count = 1,
                                           .identity = "project:facts",
                                           .native = &catalog };
        analysis = script_analyze_package(&options);
        if (!analysis->valid) {
            for (int d = 0; d < analysis->diagnostic_count; d++) {
                const script_diagnostic *diagnostic = &analysis->diagnostics[d];
                fprintf(
                    stderr,
                    "FLOAT_REJECTION stage0 %s %d:%d %s\n",
                    item->name,
                    diagnostic->range.line,
                    diagnostic->range.column,
                    diagnostic->message
                );
            }
        }
        if (!item->accepted) {
            REQUIRE(item->stage_zero.message);
            const script_diagnostic *narrowing = diagnostic(item->stage_zero.message);
            if (!narrowing || analysis->diagnostic_count != 1 ||
                narrowing->range.line != item->stage_zero.line ||
                narrowing->range.column != item->stage_zero.column) {
                fprintf(
                    stderr,
                    "FLOAT_REJECTION_MISMATCH stage0 %s expected %d:%d %s\n",
                    item->name,
                    item->stage_zero.line,
                    item->stage_zero.column,
                    item->stage_zero.message
                );
                agreed = false;
            }
        }
        if (analysis->valid != item->accepted) {
            fprintf(
                stderr,
                "floating admission %s: expected %s, got %s\n",
                item->name,
                item->accepted ? "accepted" : "rejected",
                analysis->valid ? "accepted" : "rejected"
            );
            agreed = false;
        }
    }
    REQUIRE(agreed);
    return true;
}

void test_language_semantic()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_approved_float_conversion_admission);
    RUN_TEST_CASE(language_unions_share_patterns_and_require_exhaustive_switches);
    RUN_TEST_CASE(language_cancelled_builtin_cannot_be_shadowed);
    RUN_TEST_CASE(language_switch_exhaustiveness_respects_narrowed_tags);
    RUN_TEST_CASE(language_type_patterns_narrow_and_invalidate);
    RUN_TEST_CASE(language_constants_reject_runtime_dependencies_and_writes);
    RUN_TEST_CASE(language_managed_pointers_reject_invalid_storage_and_types);
    RUN_TEST_CASE(language_shadowing_resolves_nearest_declaration);
    RUN_TEST_CASE(language_pub_marks_only_package_declarations);
    RUN_TEST_CASE(language_literals_are_typed_by_spelling);
    RUN_TEST_CASE(language_authored_map_method_names_preserve_callable_identity);
    RUN_TEST_CASE(language_scoped_pointer_calls_require_addresses);
    RUN_TEST_CASE(language_pointer_parameters_infer_scope);
    RUN_TEST_CASE(language_scoped_pointers_accept_engine_locations);
    RUN_TEST_CASE(language_retained_bindings_preserve_owner_and_value_type);
    RUN_TEST_CASE(language_system_retention_rejects_non_system_owners_and_tasks);
    RUN_TEST_CASE(language_system_invocation_locals_cross_calls_only_as_arguments);
    RUN_TEST_CASE(language_optional_component_requires_presence_guard);
    RUN_TEST_CASE(language_coalesce_types_its_operands);
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
    RUN_TEST_CASE(language_functions_with_results_must_return);
    RUN_TEST_CASE(language_hex_literal_fills_integer_storage);
    RUN_TEST_CASE(language_character_literals_take_their_type_from_context);
    RUN_TEST_CASE(language_char_widens_to_int_and_narrows_explicitly);
    RUN_TEST_CASE(language_numeric_conversions_check_modes_and_constants);
    RUN_TEST_CASE(language_number_is_f32_and_f64_narrows_explicitly);
    RUN_TEST_CASE(language_emit_sends_events_to_their_target);
    RUN_TEST_CASE(language_integers_widen_implicitly_and_narrow_explicitly);
    RUN_TEST_CASE(language_shift_counts_are_unsigned_and_keep_their_type);
    RUN_TEST_CASE(language_integer_constants_are_exact_and_must_fit);
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
