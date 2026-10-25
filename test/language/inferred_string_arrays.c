#include "backend.h"
#include "execution/definition.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Fixed CUT-79 workload: 22 semantic documents and four runtime preparations,
 * 48 direct calls. Each preparation has one production initializer with its
 * existing 100000 budget; each direct call has budget 2000. No files or frames.
 * Success assertions: semantic 156 + runtime 289 = 445; repro alone 8. */
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static const behavior_function *function;
static behavior_function_context context;
static behavior_function_activation activation;
static behavior_run_scope runs;
static arena storage;
static pin_value argument, result;
static const script_catalog catalog;
static const script_lower_bindings bindings;
static struct
{
    int parses, analyses, lowers, finishes, calls;
    uint64_t initializers, bytecode, sljit;
} counts;
static const char *const glyphs[] = { "󰵝", "",  "󰌶", "󰙴",
                                      "󰸊", "󰖊", "󰊗", "󰥻",
                                      "󰳚", "󰋕", "󰮋", "󰠲" };
static const struct
{
    const char *source, *error, *bad;
} specimens[] = {
    /* S01 */
    { "Arrays :: library { Glyphs :: [\"󰵝\", \"\", \"󰌶\", \"󰙴\", "
      "\"󰸊\", \"󰖊\", \"󰊗\", \"󰥻\", \"󰳚\", \"󰋕\", \"󰮋\", "
      "\"󰠲\"]; }",
      NULL,
      NULL },
    /* S02 */
    { "Arrays :: library { Items :: [\"beta\", \"alpha\"]; }", NULL, NULL },
    /* S03 */
    { "Arrays :: library { Run :: () -> string { items := [\"alpha\"]; "
      "items[0] = "
      "\"beta\"; return items[0]; } }",
      NULL,
      NULL },
    /* S04 */
    { "Arrays :: library { Run :: (text: string) -> string { items := "
      "[\"alpha\", "
      "text]; return items[1]; } }",
      NULL,
      NULL },
    /* S05 */
    { "Arrays :: library { A :: \"alpha\"; Alias :: A; B :: \"beta\"; Items :: "
      "[Alias, B]; Only :: union { \"alpha\" }; Keep :: () -> Only { return A; "
      "} }",
      NULL,
      NULL },
    /* S06 */
    { "Arrays :: library { Items :: [(\"alpha\"), (\"beta\")]; }", NULL, NULL },
    /* S07 */
    { "Arrays :: behavior { items := [\"alpha\", \"beta\"]; }", NULL, NULL },
    /* S08 */
    { "Arrays :: library { Run :: () { return [\"alpha\", \"beta\"]; } }", NULL, NULL },
    /* S09 */
    { "Arrays :: library { Strings: string[] : [\"alpha\", \"beta\"]; "
      "Names: name[] : [\"alpha\", \"beta\"]; Empty: "
      "string[] : []; Run :: () -> string[] { return [\"alpha\", "
      "\"beta\"]; } }",
      NULL,
      NULL },
    /* S10 */
    { "Arrays :: library { Choice :: union { \"alpha\" | \"beta\" }; Only :: "
      "union { \"alpha\" }; Both: Choice[] : [\"alpha\", \"beta\"]; One"
      ": Only[] : [\"alpha\"]; }",
      NULL,
      NULL },
    /* S11 */
    { "Arrays :: library { Choice :: union { \"alpha\" | \"beta\" }; Run :: "
      "(first: Choice) -> Choice[] { items := [first, \"beta\"]; return items; "
      "} }",
      NULL,
      NULL },
    /* S12 */
    { "Arrays :: library { Bools :: [true, false]; Numbers :: [1.0, 2.0]; "
      "Ints: "
      "int[] : [1, 2]; Names :: [name(\"alpha\"), \"beta\"]; }",
      NULL,
      NULL },
    /* S13 */
    { "Arrays :: library { Items :: [\"alpha\", 1]; }", "Incompatible types", "1" },
    /* S14 */
    { "Arrays :: library { Items :: [1, \"alpha\"]; }", "Incompatible types", "\"alpha\"" },
    /* S15 */
    { "Arrays :: library { Items :: []; }", "Empty arrays need an explicit element type", "[]" },
    /* S16 */
    { "Arrays :: library { Choice :: union { \"alpha\" | \"beta\" }; Bad: "
      "Choice[] : [\"alpha\", \"gamma\"]; }",
      "Incompatible types",
      "\"gamma\"" },
    /* S17 */
    { "Arrays :: library { Only :: union { \"alpha\" }; Bad: "
      "Only[] : [\"beta\"]; }",
      "Incompatible types",
      "\"beta\"" },
    /* S18 */
    { "Arrays :: library { Only :: union { \"alpha\" }; Bad :: () -> Only { "
      "return \"beta\"; } }",
      "Incompatible types",
      "\"beta\"" },
    /* S19 */
    { "Arrays :: library { Only :: union { \"alpha\" }; Items :: [\"alpha\"]; "
      "Bad "
      ":: () -> Only { return Items[0]; } }",
      "Incompatible types",
      "Items[0]" },
    /* S20 */
    { "Arrays :: library { Choice :: union { \"alpha\" | \"beta\" }; Bad :: "
      "(first: Choice) { items := [first, \"gamma\"]; } }",
      "Incompatible types",
      "\"gamma\"" },
    /* S21 */
    { "Arrays :: library { A :: \"alpha\"; Only :: union { \"beta\" }; Bad :: "
      "() "
      "-> Only { return A; } }",
      "Incompatible types",
      "return A" },
    /* S22 */
    { "Arrays :: library { Bad: int[] : [\"alpha\"]; }", "Incompatible types", "\"alpha\"" },
};
_Static_assert(sizeof specimens / sizeof *specimens == 22, "Fixed semantic count");
_Static_assert(sizeof glyphs / sizeof *glyphs == 12, "Fixed glyph count");

static void finish_call()
{
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    arena_free(&storage);
    argument = result = (pin_value){ 0 };
}
static void cleanup()
{
    finish_call();
    function = NULL;
    execution_module_free(module);
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    module = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
}
static bool parse_source(const char *source)
{
    cleanup();
    counts.parses++;
    syntax = script_parse(source);
    REQUIRE(syntax && !syntax->diagnostic_count);
    counts.analyses++;
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "array semantic %d:%d: %s\n",
            analysis->diagnostics[i].range.line,
            analysis->diagnostics[i].range.column,
            analysis->diagnostics[i].message
        );
    }
    return true;
}
static const script_symbol *symbol(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (!strcmp(analysis->symbols[i].name, name)) {
            return &analysis->symbols[i];
        }
    }
    return NULL;
}
static const script_expression *initializer(const char *name)
{
    const script_symbol *s = symbol(name);
    return s && s->initializer >= 0 && s->initializer < analysis->expression_count
             ? &analysis->expressions[s->initializer]
             : NULL;
}
static bool array_type(script_type type, pin_type storage, uint64_t nominal)
{
    return type.storage == storage && type.container == PIN_CONTAINER_ARRAY &&
           type.nominal == nominal && !type.parameter &&
           !(type.flags &
             (SCRIPT_TYPE_LITERAL | SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_ERROR)) &&
           ((type.flags & SCRIPT_TYPE_UNION) != 0) == (storage == PIN_UNION);
}
static bool named_array(const char *name, pin_type storage, const char *nominal, int count)
{
    const script_symbol *s = symbol(name), *n = nominal ? symbol(nominal) : NULL;
    const script_expression *e = initializer(name);
    if (!s || !e || (nominal && !n) || e->kind != SCRIPT_EXPR_ARRAY ||
        e->argument_count != count) {
        return false;
    }
    uint64_t id = n ? n->type.nominal : 0;
    return array_type(s->type, storage, id) && array_type(e->type, storage, id);
}
static bool result_type(const char *name, pin_type storage, pin_container container, const char *nominal)
{
    const script_symbol *s = symbol(name), *n = nominal ? symbol(nominal) : NULL;
    if (!s || s->function < 0 || s->function >= analysis->function_count ||
        (nominal && !n)) {
        return false;
    }
    const script_function *f = &analysis->functions[s->function];
    if (f->result_count != 1) {
        return false;
    }
    script_type type = analysis->symbols[f->results[0]].type;
    return type.storage == storage && type.container == container &&
           type.nominal == (n ? n->type.nominal : 0) &&
           !(type.flags &
             (SCRIPT_TYPE_LITERAL | SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_ERROR));
}
static bool literal_symbol(const char *name, const char *text)
{
    const script_symbol *s = symbol(name);
    return s && s->kind == SCRIPT_CONSTANT && !s->writable &&
           s->type.storage == PIN_STR && s->type.container == PIN_CONTAINER_SINGLE &&
           (s->type.flags & SCRIPT_TYPE_LITERAL) &&
           s->type.parameter == script_catalog_id("union-literal", text);
}
static bool literal_facts()
{
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *e = &analysis->expressions[i];
        if (e->kind != SCRIPT_EXPR_CONSTANT ||
            (e->constant.type != PIN_STR && e->constant.type != PIN_NAME)) {
            continue;
        }
        if (!pin_text(e->constant).bytes || e->type.storage != e->constant.type ||
            !(e->type.flags & SCRIPT_TYPE_LITERAL) ||
            e->type.parameter !=
                script_catalog_id("union-literal", pin_text(e->constant).bytes)) {
            return false;
        }
    }
    return true;
}
static bool union_constraint(const char *name, const char *first, const char *second)
{
    const script_symbol *s = symbol(name);
    script_catalog own = { .types = analysis->types, .type_count = analysis->type_count };
    const script_catalog_type
        *type = s ? script_catalog_type_by_id(&own, s->type.nominal) : NULL;
    if (!type || type->type.storage != PIN_UNION ||
        !(type->type.flags & SCRIPT_TYPE_UNION) ||
        type->variant_count != (second ? 2 : 1)) {
        return false;
    }
    const char *values[] = { first, second };
    script_type plain = { .storage = PIN_STR };
    script_type wrong = { .storage = PIN_STR,
                          .flags = SCRIPT_TYPE_LITERAL,
                          .parameter = script_catalog_id("union-literal", "outside") };
    for (int i = 0; i < type->variant_count; i++) {
        const script_catalog_variant *v = &type->variants[i];
        script_type matching = {
            .storage = PIN_STR,
            .flags = SCRIPT_TYPE_LITERAL,
            .parameter = script_catalog_id("union-literal", values[i])
        };
        if (!v->is_literal || v->tag != matching.parameter ||
            !(v->type.flags & SCRIPT_TYPE_LITERAL) ||
            v->type.parameter != matching.parameter ||
            !script_catalog_type_assignable(&own, v->type, matching) ||
            script_catalog_type_assignable(&own, v->type, wrong) ||
            script_catalog_type_assignable(&own, v->type, plain) ||
            !script_catalog_type_assignable(&own, type->type, matching)) {
            return false;
        }
    }
    return !script_catalog_type_assignable(&own, type->type, wrong) &&
           !script_catalog_type_assignable(&own, type->type, plain);
}
static bool ordinary_expressions(script_expression_kind kind)
{
    int found = 0;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *e = &analysis->expressions[i];
        if (e->kind != kind) {
            continue;
        }
        found++;
        if (e->type.storage != PIN_STR || e->type.nominal || e->type.parameter ||
            (e->type.flags &
             (SCRIPT_TYPE_LITERAL | SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_ERROR)) ||
            e->type.container != (kind == SCRIPT_EXPR_ARRAY ? PIN_CONTAINER_ARRAY
                                                            : PIN_CONTAINER_SINGLE)) {
            return false;
        }
    }
    return found > 0;
}
static bool contract(int index)
{
    if (!literal_facts()) {
        return false;
    }
    switch (index) {
        case 0: {
            if (!named_array("Glyphs", PIN_STR, NULL, 12)) {
                return false;
            }
            const script_expression *e = initializer("Glyphs");
            for (int i = 0; i < 12; i++) {
                const script_expression *v = &analysis
                                                  ->expressions[e->arguments[i].value];
                if (v->kind != SCRIPT_EXPR_CONSTANT || !pin_text(v->constant).bytes ||
                    strcmp(pin_text(v->constant).bytes, glyphs[i])) {
                    return false;
                }
            }
            return true;
        }
        case 1:
        case 5: return named_array("Items", PIN_STR, NULL, 2);
        case 2:
            return named_array("items", PIN_STR, NULL, 1) && symbol("items")->writable;
        case 3: return named_array("items", PIN_STR, NULL, 2);
        case 4:
            return named_array("Items", PIN_STR, NULL, 2) &&
                   literal_symbol("A", "alpha") &&
                   literal_symbol("Alias", "alpha") && literal_symbol("B", "beta") &&
                   union_constraint("Only", "alpha", NULL) &&
                   result_type("Keep", PIN_UNION, PIN_CONTAINER_SINGLE, "Only");
        case 6:
            return named_array("items", PIN_STR, NULL, 2) &&
                   symbol("items")->kind == SCRIPT_STATE;
        case 7:
            return ordinary_expressions(SCRIPT_EXPR_ARRAY) &&
                   result_type("Run", PIN_STR, PIN_CONTAINER_ARRAY, NULL);
        case 8:
            return named_array("Strings", PIN_STR, NULL, 2) &&
                   named_array("Names", PIN_NAME, NULL, 2) &&
                   named_array("Empty", PIN_STR, NULL, 0) &&
                   result_type("Run", PIN_STR, PIN_CONTAINER_ARRAY, NULL);
        case 9:
            return named_array("Both", PIN_UNION, "Choice", 2) &&
                   named_array("One", PIN_UNION, "Only", 1) &&
                   union_constraint("Choice", "alpha", "beta") &&
                   union_constraint("Only", "alpha", NULL);
        case 10:
            return named_array("items", PIN_UNION, "Choice", 2) &&
                   result_type("Run", PIN_UNION, PIN_CONTAINER_ARRAY, "Choice") &&
                   union_constraint("Choice", "alpha", "beta");
        case 11:
            return named_array("Bools", PIN_BOOL, NULL, 2) &&
                   named_array("Numbers", PIN_F64, NULL, 2) &&
                   named_array("Ints", PIN_I32, NULL, 2) &&
                   named_array("Names", PIN_NAME, NULL, 2);
        case 15: return union_constraint("Choice", "alpha", "beta");
        case 16:
        case 17: return union_constraint("Only", "alpha", NULL);
        case 18:
            return named_array("Items", PIN_STR, NULL, 1) &&
                   ordinary_expressions(SCRIPT_EXPR_INDEX) &&
                   union_constraint("Only", "alpha", NULL);
        case 19:
            return named_array("items", PIN_UNION, "Choice", 2) &&
                   union_constraint("Choice", "alpha", "beta");
        case 20:
            return literal_symbol("A", "alpha") &&
                   union_constraint("Only", "beta", NULL);
        default: return true;
    }
}
static bool diagnostics(int index)
{
    const char *source = specimens[index].source, *error = specimens[index].error;
    if (!error) {
        return analysis->diagnostic_count == 0;
    }
    const char *bad = strstr(source, specimens[index].bad);
    if (!bad) {
        return false;
    }
    int start = (int)(bad - source), end = start + (int)strlen(specimens[index].bad);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        const script_diagnostic *d = &analysis->diagnostics[i];
        if (strstr(d->message, error) && d->range.start >= 0 &&
            d->range.end <= (int)strlen(source) && d->range.start < end &&
            d->range.end > start) {
            return true;
        }
    }
    return false;
}
static bool check_specimen(int index)
{
    fprintf(stderr, "CUT-79 S%02d\n", index + 1);
    REQUIRE(strlen(specimens[index].source) <= 1024);
    REQUIRE(parse_source(specimens[index].source));
    REQUIRE(analysis->valid == (specimens[index].error == NULL));
    REQUIRE(diagnostics(index));
    REQUIRE(contract(index));
    return true;
}
TEST_CASE(inferred_string_array_semantic)
{
    size_t bytes = 0;
    for (int i = 0; i < 22; i++) {
        bytes += strlen(specimens[i].source);
        if (!check_specimen(i)) {
            return false;
        }
    }
    REQUIRE(bytes <= 16384);
    REQUIRE(
        counts.parses == 22 && counts.analyses == 22 && !counts.lowers &&
        !counts.finishes && !counts.calls
    );
    return true;
}
TEST_CASE(inferred_string_array_repro)
{
    if (!check_specimen(0)) {
        return false;
    }
    REQUIRE(
        counts.parses == 1 && counts.analyses == 1 && !counts.lowers &&
        !counts.finishes && !counts.calls
    );
    return true;
}
static bool entries(behavior_backend backend)
{
    return !runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks &&
           !runs.counts.inspected_sljit_entries &&
           !runs.counts.inspected_wasm_entries && !runs.counts.wasm_entries &&
           runs.counts.bytecode_entries == (backend == BEHAVIOR_BYTECODE ? 1u : 0u) &&
           runs.counts.sljit_entries == (backend == BEHAVIOR_SLJIT ? 1u : 0u);
}
static bool prepare(const char *source, behavior_backend backend)
{
    REQUIRE(parse_source(source));
    REQUIRE(analysis->valid && !analysis->diagnostic_count);
    REQUIRE(analysis->function_count == 1 && analysis->functions[0].symbol >= 0);
    uint64_t id = analysis->symbols[analysis->functions[0].symbol].id;
    int constants = 0;
    for (int i = 0; i < analysis->symbol_count; i++) {
        constants += analysis->symbols[i].kind == SCRIPT_CONSTANT;
    }
    REQUIRE(constants == 1 && named_array("glyphs", PIN_STR, NULL, 12));
    behavior_run_scope_reset(&runs);
    behavior_run_scope_begin(&runs);
    counts.lowers++;
    plan = script_lower(analysis, &catalog, &bindings);
    behavior_run_scope_end(&runs);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "array lower: %s\n", plan->diagnostics[i].message);
    }
    REQUIRE(plan && plan->valid);
    /* evaluate_default runs exactly one BYTECODE initializer at budget100000. */
    REQUIRE(entries(BEHAVIOR_BYTECODE));
    counts.initializers += runs.counts.bytecode_entries;
    char error[256] = { 0 };
    counts.finishes++;
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "array module: %s\n", error);
    }
    REQUIRE(module && execution_module_backend(module) == backend);
    REQUIRE(execution_module_function_count(module) == 1);
    execution_module_function_info info = execution_module_function_at(module, 0);
    function = info.body;
    REQUIRE(
        info.id == id && function && function->argument_count == 1 &&
        function->result_count == 1 && !acount(function->calls)
    );
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
    return true;
}
static bool lookup(behavior_backend backend, int index)
{
    finish_call();
    argument = (pin_value){ .type = PIN_I32, .i = index };
    result = function->result_defaults[0];
    behavior_function_prepare_results(function, &result, &storage);
    context = (behavior_function_context){ .activation = &activation,
                                           .arguments = &argument,
                                           .argument_count = 1,
                                           .results = &result,
                                           .budget = 2000 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope_reset(&runs);
    behavior_run_scope_begin(&runs);
    counts.calls++;
    int status = behavior_function_run(function, &context, backend == BEHAVIOR_SLJIT);
    behavior_run_scope_end(&runs);
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(entries(backend));
    counts.bytecode += runs.counts.bytecode_entries;
    counts.sljit += runs.counts.sljit_entries;
    REQUIRE(
        result.type == PIN_STR && result.container == PIN_CONTAINER_SINGLE &&
        pin_text(result).bytes
    );
    REQUIRE(!strcmp(pin_text(result).bytes, glyphs[index]));
    finish_call();
    return true;
}
TEST_CASE(inferred_string_array_runtime)
{
    static const char *const sources[] = {
        "Glyphs :: library { Lookup :: (index: int) -> string { glyphs :: "
        "[\"󰵝\", \"\", \"󰌶\", \"󰙴\", \"󰸊\", \"󰖊\", \"󰊗\", "
        "\"󰥻\", "
        "\"󰳚\", \"󰋕\", \"󰮋\", \"󰠲\"]; return glyphs[index]; } }",
        "Glyphs :: library { Lookup :: (index: int) -> string { glyphs: "
        "string[] "
        ": "
        "[\"󰵝\", \"\", \"󰌶\", \"󰙴\", \"󰸊\", "
        "\"󰖊\", \"󰊗\", "
        "\"󰥻\", \"󰳚\", \"󰋕\", \"󰮋\", \"󰠲\"]; return "
        "glyphs[index]; } }"
    };
    static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    REQUIRE(TEST_COMPILED_BACKEND == BEHAVIOR_SLJIT);
    for (int s = 0; s < 2; s++) {
        REQUIRE(strlen(sources[s]) <= 1024);
        for (int b = 0; b < 2; b++) {
            if (!prepare(sources[s], backends[b])) {
                return false;
            }
            for (int i = 0; i < 12; i++) {
                if (!lookup(backends[b], i)) {
                    return false;
                }
            }
            cleanup();
        }
    }
    REQUIRE(
        counts.parses == 26 && counts.analyses == 26 && counts.lowers == 4 &&
        counts.finishes == 4 && counts.calls == 48
    );
    REQUIRE(counts.initializers == 4 && counts.bytecode == 24 && counts.sljit == 24);
    return true;
}
void test_language_inferred_string_arrays(bool repro)
{
    counts.parses = counts.analyses = counts.lowers = counts.finishes = counts.calls = 0;
    counts.initializers = counts.bytecode = counts.sljit = 0;
    pu_setup(NULL, cleanup);
    if (repro) {
        RUN_TEST_CASE(inferred_string_array_repro);
    } else {
        RUN_TEST_CASE(inferred_string_array_semantic);
        if (!pu_test_failed()) {
            RUN_TEST_CASE(inferred_string_array_runtime);
        }
    }
    pu_clear_setup();
    fprintf(
        stderr,
        "CUT-79 parses=%d analyses=%d lowers=%d finishes=%d calls=%d "
        "initializer_bytecode=%" PRIu64 " direct_bytecode=%" PRIu64
        " direct_sljit=%" PRIu64 "\n",
        counts.parses,
        counts.analyses,
        counts.lowers,
        counts.finishes,
        counts.calls,
        counts.initializers,
        counts.bytecode,
        counts.sljit
    );
}
