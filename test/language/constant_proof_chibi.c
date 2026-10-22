#include "constant_proof_chibi.h"

#include "compiler_fixture.h"
#include "constant_proof_chibi_source.h"
#include "language/constant_proof.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <string.h>

/* Three cases, 36 fixed IR requests and two source-derived requests. Each of
 * the 38 requests runs one C proof and both authored backends: 76 calls, each
 * with a two-million-instruction ceiling. At most 257 expressions, 32 symbols,
 * four arguments and 256 proof visits coexist. IR/Proof preparation parses two
 * sources per backend; two small semantic source fixtures are parsed once each.
 * No filesystem, services, workers, generated combinations or timing loops. */
enum
{
    MAX_EXPRESSIONS = 257,
    MAX_SYMBOLS = 32,
    MAX_ARGUMENTS = 4,
    RAW_CASES = 36
};
static const char IR[] = "project:constant_ir",
                  PROOF[] = "project:constant_admissibility";
static compiler_fixture fixture;
static arena input, detached, retained[2];
static pin_value previous[2];
static script_syntax *syntax;
static script_analysis *parsed;
static script_expression expressions[MAX_EXPRESSIONS];
static script_symbol symbols[MAX_SYMBOLS];
static script_analysis model;
static const char *const names[RAW_CASES] = { "boolean",
                                              "scalar-boolean",
                                              "scalar-int",
                                              "scalar-string",
                                              "scalar-flow",
                                              "scalar-record",
                                              "scalar-array",
                                              "scalar-optional",
                                              "scalar-literal-untyped",
                                              "scalar-unknown-flag",
                                              "nil",
                                              "scalar-nil",
                                              "effect",
                                              "pure-call",
                                              "unary",
                                              "cast",
                                              "member",
                                              "scalar-member",
                                              "index",
                                              "array",
                                              "empty-record",
                                              "union-first-invalid",
                                              "constant-symbol",
                                              "mutable-symbol",
                                              "negative-child",
                                              "zero-steps",
                                              "negative-steps",
                                              "same-depth-cache",
                                              "deeper-then-shallower",
                                              "shallower-then-deeper-budget",
                                              "shallower-then-deeper",
                                              "depth-255",
                                              "depth-256",
                                              "cycle-no-step-budget",
                                              "no-step-budget",
                                              "left-failure-order" };
typedef struct
{
    int expression, steps;
    bool scalar, has_steps;
} request;

static void call_cleanup()
{
    arena_free(&input);
    arena_free(&detached);
}
static void model_cleanup()
{
    for (int i = 0; i < model.expression_count; i++) {
        afree(expressions[i].arguments);
    }
    model = (script_analysis){ 0 };
}
static void cleanup()
{
    call_cleanup();
    model_cleanup();
    script_analysis_free(parsed);
    script_syntax_free(syntax);
    parsed = NULL;
    syntax = NULL;
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
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
static bool record(int backend, const char *identity, const char *name, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, backend, identity, name, &input);
    REQUIRE(shape);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .type_name = shape->type.name,
                        .record = shape,
                        .ptr = arena_alloc(&input, shape->type.size) };
    memset(out->ptr, 0, shape->type.size);
    return true;
}
static bool rows(int backend, const char *name, int count, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, backend, IR, name, &input);
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
static int bits(unsigned value)
{
    _Static_assert(sizeof(unsigned) == sizeof(int), "raw mask width");
    int result;
    memcpy(&result, &value, sizeof result);
    return result;
}
static bool pack(int backend, const script_analysis *analysis, request query, pin_value args[5])
{
    REQUIRE(analysis->expression_count > 0 && analysis->expression_count <= MAX_EXPRESSIONS);
    REQUIRE(analysis->symbol_count >= 0 && analysis->symbol_count <= MAX_SYMBOLS);
    int argument_count = 0;
    for (int i = 0; i < analysis->expression_count; i++) {
        argument_count += acount(analysis->expressions[i].arguments);
    }
    REQUIRE(argument_count <= MAX_ARGUMENTS);
    REQUIRE(rows(backend, "Expression", analysis->expression_count, &args[0]));
    REQUIRE(rows(backend, "Type", analysis->expression_count, &args[1]));
    REQUIRE(rows(backend, "Symbol", analysis->symbol_count, &args[2]));
    REQUIRE(rows(backend, "Argument", argument_count, &args[3]));
    pin_array *exprs = args[0].ptr, *types = args[1].ptr, *syms = args[2].ptr,
              *arguments = args[3].ptr;
    int at = 0;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *e = &analysis->expressions[i];
        pin_value *row = &exprs->items[i], *type = &types->items[i];
        REQUIRE(
            put(row, "kind", integer(e->kind)) && put(row, "op", integer(e->op)) &&
            put(row, "literal", integer(-1)) && put(row, "type", integer(i)) &&
            put(row, "symbol", integer(e->symbol)) &&
            put(row, "left", integer(e->left)) &&
            put(row, "right", integer(e->right)) &&
            put(row, "arg_start", integer(at)) &&
            put(row, "arg_count", integer(acount(e->arguments))) &&
            put(row, "effects", integer(bits(e->effects)))
        );
        REQUIRE(
            put(type, "storage", integer(e->type.storage)) &&
            put(type, "container", integer(e->type.container)) &&
            put(type, "flags", integer(bits(e->type.flags))) &&
            put(type, "has_nominal", boolean(e->type.nominal != 0))
        );
        for (int j = 0; j < acount(e->arguments); j++) {
            REQUIRE(
                put(&arguments->items[at++], "value", integer(e->arguments[j].value))
            );
        }
    }
    for (int i = 0; i < analysis->symbol_count; i++) {
        REQUIRE(
            put(&syms->items[i], "kind", integer(analysis->symbols[i].kind)) &&
            put(&syms->items[i], "initializer", integer(analysis->symbols[i].initializer))
        );
    }
    REQUIRE(record(backend, PROOF, "Request", &args[4]));
    REQUIRE(
        put(&args[4], "expression", integer(query.expression)) &&
        put(&args[4], "steps", integer(query.steps)) &&
        put(&args[4], "scalar", boolean(query.scalar)) &&
        put(&args[4], "has_steps", boolean(query.has_steps))
    );
    return true;
}
static bool checked(int backend, const pin_value *result)
{
    REQUIRE(
        result->type == PIN_UNION &&
        result->container == PIN_CONTAINER_SINGLE && result->ptr
    );
    const authoring_package *package = compiler_fixture_package(&fixture, backend, PROOF);
    REQUIRE(package && package->exports && package->exports->metadata);
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, backend, PROOF, "Proof")
    );
    const pin_union *value = result->ptr;
    REQUIRE(
        type && type->variant_count == 1 && type->variants[0].field_count == 3 &&
        !strcmp(type->variants[0].name, "Checked")
    );
    REQUIRE(value->tag == type->variants[0].tag && value->count == 3);
    const char *fields[] = { "allowed", "steps_remaining", "depths" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(!strcmp(type->variants[0].fields[i].name, fields[i]));
    }
    REQUIRE(value->values[0].type == PIN_BOOL && value->values[1].type == PIN_I32);
    REQUIRE(value->values[2].type == PIN_I32 && value->values[2].container == PIN_CONTAINER_ARRAY);
    return true;
}
static bool compare(const char *name, const script_analysis *analysis, request query, bool expected, bool retain)
{
    REQUIRE(fixture.owners[0] && fixture.owners[1]);
    REQUIRE(
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1])
    );
    REQUIRE(analysis->expression_count > 0 && analysis->expression_count <= MAX_EXPRESSIONS);
    constant_proof proofs[MAX_EXPRESSIONS] = { 0 };
    int steps = query.steps;
    bool allowed = script_constant_prove(
        analysis,
        proofs,
        1,
        query.expression,
        query.has_steps ? &steps : NULL,
        query.scalar
    );
    REQUIRE(allowed == expected);
    for (int b = 0; b < 2; b++) {
        call_cleanup();
        pin_value args[5], result;
        REQUIRE(pack(b, analysis, query, args));
        REQUIRE(
            compiler_fixture_call(&fixture, b, PROOF, "Check", args, 5, 2000000, &detached, &result)
        );
        arena_free(&input);
        REQUIRE(checked(b, &result));
        const pin_union *value = result.ptr;
        const pin_array *depths = value->values[2].ptr;
        REQUIRE(arena_contains(&detached, value));
        if (value->values[0].b != allowed || value->values[1].i != steps) {
            fprintf(stderr, "constant proof mismatch: %s backend%d\n", name, b);
        }
        REQUIRE(value->values[0].b == allowed && value->values[1].i == steps);
        REQUIRE(depths && depths->count == analysis->expression_count);
        REQUIRE(
            arena_contains(&detached, depths) &&
            arena_contains(&detached, depths->items)
        );
        for (int i = 0; i < analysis->expression_count; i++) {
            REQUIRE(
                depths->items[i].type == PIN_I32 &&
                depths->items[i].i == (proofs[i].generation == 1 ? proofs[i].depth : -1)
            );
        }
        if (retain) {
            REQUIRE(!compiler_fixture_snapshot(&retained[b], &previous[b], &result));
        }
    }
    return true;
}
static void reset(int count)
{
    model_cleanup();
    memset(expressions, 0, sizeof expressions);
    memset(symbols, 0, sizeof symbols);
    model = (script_analysis){ .expressions = expressions,
                               .expression_count = count,
                               .symbols = symbols };
    for (int i = 0; i < count; i++) {
        expressions[i] = (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                              .type = { .storage = PIN_I32 },
                                              .constant = { .type = PIN_I32 },
                                              .symbol = -1,
                                              .left = -1,
                                              .right = -1 };
    }
}
static void aggregate(script_expression_kind kind, int first, int second)
{
    expressions[0].kind = kind;
    apush(expressions[0].arguments, ((script_argument){ .value = first }));
    apush(expressions[0].arguments, ((script_argument){ .value = second }));
    expressions[0].argument_count = 2;
}
static bool raw_case(int index)
{
    reset(3);
    request query = { .expression = 0, .steps = 1024, .has_steps = true };
    bool expected = true;
    switch (index) {
        case 0: expressions[0].type.storage = PIN_BOOL; break;
        case 1:
            expressions[0].type.storage = PIN_BOOL;
            query.scalar = true;
            break;
        case 2: query.scalar = true; break;
        case 3:
            expressions[0].type.storage = PIN_STR;
            query.scalar = true;
            break;
        case 4:
            expressions[0].type.storage = PIN_FLOW;
            query.scalar = true;
            expected = false;
            break;
        case 5:
            expressions[0].type.storage = PIN_STRUCT;
            query.scalar = true;
            expected = false;
            break;
        case 6:
            expressions[0].type.container = PIN_CONTAINER_ARRAY;
            query.scalar = true;
            expected = false;
            break;
        case 7:
            expressions[0].type.flags = SCRIPT_TYPE_OPTIONAL;
            query.scalar = true;
            expected = false;
            break;
        case 8:
            expressions[0].type.flags = SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL;
            query.scalar = true;
            break;
        case 9:
            expressions[0].type.flags = 1u << 31;
            query.scalar = true;
            expected = false;
            break;
        case 10: expressions[0].kind = SCRIPT_EXPR_NIL; break;
        case 11:
            expressions[0].kind = SCRIPT_EXPR_NIL;
            query.scalar = true;
            expected = false;
            break;
        case 12:
            expressions[0].effects = 1u << 31;
            expected = false;
            break;
        case 13:
            expressions[0].kind = SCRIPT_EXPR_CALL;
            expected = false;
            break;
        case 14:
            expressions[0].kind = SCRIPT_EXPR_UNARY;
            expressions[0].left = 1;
            break;
        case 15:
            expressions[0].kind = SCRIPT_EXPR_CAST;
            expressions[0].left = 1;
            break;
        case 16:
            expressions[0].kind = SCRIPT_EXPR_MEMBER;
            expressions[0].left = 1;
            break;
        case 17:
            expressions[0].kind = SCRIPT_EXPR_MEMBER;
            expressions[0].left = 1;
            query.scalar = true;
            expected = false;
            break;
        case 18:
            expressions[0].kind = SCRIPT_EXPR_INDEX;
            expressions[0].left = 1;
            expressions[0].right = 2;
            break;
        case 19: aggregate(SCRIPT_EXPR_ARRAY, 1, 2); break;
        case 20: expressions[0].kind = SCRIPT_EXPR_STRUCT; break;
        case 21:
            aggregate(SCRIPT_EXPR_UNION, 1, 2);
            expressions[1].effects = SCRIPT_EFFECT_WRITE;
            expected = false;
            break;
        case 22:
        case 23:
            expressions[0].kind = SCRIPT_EXPR_SYMBOL;
            expressions[0].symbol = 0;
            model.symbol_count = 1;
            symbols[0] = (script_symbol){ .kind = index == 22 ? SCRIPT_CONSTANT
                                                              : SCRIPT_LOCAL,
                                          .initializer = 1 };
            expected = index == 22;
            break;
        case 24:
            expressions[0].kind = SCRIPT_EXPR_UNARY;
            expected = false;
            break;
        case 25:
            query.steps = 0;
            expected = false;
            break;
        case 26:
            query.steps = -1;
            expected = false;
            break;
        case 27:
            expressions[0].kind = SCRIPT_EXPR_BINARY;
            expressions[0].left = expressions[0].right = 1;
            query.steps = 2;
            break;
        case 28:
            expressions[0].kind = SCRIPT_EXPR_BINARY;
            expressions[0].left = 1;
            expressions[0].right = 2;
            expressions[1].kind = SCRIPT_EXPR_UNARY;
            expressions[1].left = 2;
            query.steps = 3;
            break;
        case 29:
        case 30:
            expressions[0].kind = SCRIPT_EXPR_BINARY;
            expressions[0].left = 1;
            expressions[0].right = 2;
            expressions[2].kind = SCRIPT_EXPR_UNARY;
            expressions[2].left = 1;
            query.steps = index == 29 ? 3 : 4;
            expected = index == 30;
            break;
        case 31:
        case 32:
            reset(index == 31 ? 256 : 257);
            query.steps = 256;
            for (int i = 0; i < model.expression_count - 1; i++) {
                expressions[i].kind = SCRIPT_EXPR_UNARY;
                expressions[i].left = i + 1;
            }
            expected = index == 31;
            break;
        case 33:
            expressions[0].kind = SCRIPT_EXPR_SYMBOL;
            expressions[0].symbol = 0;
            model.symbol_count = 1;
            symbols[0] = (script_symbol){ .kind = SCRIPT_CONSTANT, .initializer = 0 };
            query.has_steps = false;
            expected = false;
            break;
        case 34:
            expressions[0].kind = SCRIPT_EXPR_BINARY;
            expressions[0].left = 1;
            expressions[0].right = 2;
            query.has_steps = false;
            query.steps = 0;
            break;
        case 35:
            expressions[0].kind = SCRIPT_EXPR_BINARY;
            expressions[0].left = 1;
            expressions[0].right = 2;
            expressions[1].kind = SCRIPT_EXPR_CALL;
            expected = false;
            break;
        default: REQUIRE(false);
    }
    return compare(names[index], &model, query, expected, index == 0);
}
TEST_CASE(constant_proof_raw_facts_match)
{
    source_package_input files[] = {
        { "constant_ir/IR.chibi",
          CONSTANT_IR_CHIBI_SOURCE,
          sizeof CONSTANT_IR_CHIBI_SOURCE - 1 },
        { "constant_admissibility/Proof.chibi",
          CONSTANT_PROOF_CHIBI_SOURCE,
          sizeof CONSTANT_PROOF_CHIBI_SOURCE - 1 },
    };
    REQUIRE(files[0].size < 16384 && files[1].size < 16384);
    REQUIRE(compiler_fixture_prepare(&fixture, files, 2));
    for (int i = 0; i < RAW_CASES; i++) {
        REQUIRE(raw_case(i));
    }
    return true;
}
TEST_CASE(constant_proof_analyzer_facts_match)
{
    const char *sources[] = {
        "Facts :: library { Seed: int : 2; Sum :: Seed + 3; Run :: "
        "(value: int) -> int { return value + Sum; } }",
        "Facts :: library { Run :: () -> int { local: int = 1; return "
        "local; } }",
    };
    const script_catalog catalog = { 0 };
    for (int c = 0; c < 2; c++) {
        script_analysis_free(parsed);
        script_syntax_free(syntax);
        syntax = script_parse(sources[c]);
        REQUIRE(!syntax->diagnostic_count);
        parsed = script_analyze(syntax, &catalog);
        REQUIRE(parsed->valid);
        int expression = -1;
        for (int i = 0; i < parsed->symbol_count; i++) {
            if (c == 0 && !strcmp(parsed->symbols[i].name, "Sum")) {
                expression = parsed->symbols[i].initializer;
            }
        }
        if (c == 1) {
            for (int i = 0; i < parsed->expression_count; i++) {
                const script_expression *e = &parsed->expressions[i];
                if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
                    parsed->symbols[e->symbol].kind == SCRIPT_LOCAL) {
                    expression = i;
                    break;
                }
            }
        }
        REQUIRE(expression >= 0);
        REQUIRE(compare(
            c ? "analyzed-local" : "analyzed-constant",
            parsed,
            (request){ .expression = expression, .steps = 1024, .has_steps = true },
            c == 0,
            false
        ));
    }
    return true;
}
TEST_CASE(constant_proof_results_outlive_inputs_and_modules)
{
    compiler_fixture_free(&fixture);
    cleanup();
    for (int b = 0; b < 2; b++) {
        REQUIRE(previous[b].type == PIN_UNION && previous[b].ptr);
        const pin_union *value = previous[b].ptr;
        REQUIRE(arena_contains(&retained[b], value));
        REQUIRE(value->count == 3 && value->values[0].b && value->values[1].i == 1023);
        const pin_array *depths = value->values[2].ptr;
        REQUIRE(
            depths && depths->count == 3 && depths->items[0].i == 0 &&
            depths->items[1].i == -1 && depths->items[2].i == -1
        );
        REQUIRE(
            arena_contains(&retained[b], depths) &&
            arena_contains(&retained[b], depths->items)
        );
    }
    return true;
}
void test_language_constant_proof_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(constant_proof_raw_facts_match);
    RUN_TEST_CASE(constant_proof_analyzer_facts_match);
    RUN_TEST_CASE(constant_proof_results_outlive_inputs_and_modules);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        arena_free(&retained[b]);
        previous[b] = (pin_value){ 0 };
    }
}
