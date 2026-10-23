#include "authoring/source_catalog.h"
#include "language/semantic.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static script_syntax *syntax;
static script_analysis *analysis;
static source_catalog *imports;
static char root[128];
static const script_catalog_parameter integer[] = {
    { .name = "value", .type = { .storage = PIN_I32 } }
};
static const script_catalog_capability capabilities[] = {
    { .id = 1, .name = "Mutate", .effects = SCRIPT_EFFECT_WRITE },
    { .id = 2,
      .name = "Random",
      .effects = SCRIPT_EFFECT_RANDOM,
      .results = integer,
      .result_count = 1 },
    { .id = 3,
      .name = "Store",
      .qualified_name = "effects.Store",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = integer,
      .parameter_count = 1,
      .results = integer,
      .result_count = 1,
      .component_access = SCRIPT_COMPONENT_ACCESS_WRITE,
      .component_entity = -1,
      .component_value = 0,
      .effects = SCRIPT_EFFECT_WRITE,
      .completion_outcome = 0 },
    { .id = 4,
      .name = "Opaque",
      .qualified_name = "effects.Opaque",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .results = integer,
      .result_count = 1,
      .completion_outcome = 0 },
};
static const script_catalog catalog = {
    .capabilities = capabilities,
    .capability_count = sizeof capabilities / sizeof *capabilities,
};
static void cleanup()
{
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
    source_catalog_free(imports);
    imports = NULL;
    if (*root) {
        rmdir(root);
        *root = 0;
    }
}
static bool parse(const char *source)
{
    cleanup();
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    return !syntax->diagnostic_count && analysis->valid;
}
static const script_function *function(const char *name)
{
    for (int i = 0; i < analysis->function_count; i++) {
        const script_function *f = &analysis->functions[i];
        if (f->symbol >= 0 && !strcmp(analysis->symbols[f->symbol].name, name)) {
            return f;
        }
    }
    return NULL;
}
TEST_CASE(effects_infer_recursive_and_transitive_callables)
{
    REQUIRE(parse(
        "Effects :: library { "
        "Even :: (n: int) -> bool { if n == 0 { return true; } return Odd(n - "
        "1); } "
        "Odd :: (n: int) -> bool { if n == 0 { return false; } return Even(n - "
        "1); } "
        "Write :: () { Mutate(); } Forward :: () { Write(); } "
        "Roll :: () -> int { return Random(); } "
        "Wait :: () { yield; } Start :: () { pending := Wait(); } }"
    ));
    REQUIRE(function("Even") && function("Even")->pure);
    REQUIRE(function("Odd") && function("Odd")->pure);
    REQUIRE(function("Forward") && !function("Forward")->pure);
    REQUIRE(function("Forward")->effects & SCRIPT_EFFECT_WRITE);
    REQUIRE(function("Roll") && !function("Roll")->pure);
    REQUIRE(function("Wait") && !function("Wait")->pure);
    REQUIRE(function("Start") && !function("Start")->pure);
    return true;
}
TEST_CASE(effects_indexed_writes_follow_retained_and_reference_roots)
{
    REQUIRE(parse(
        "Effects :: library { items: retain int[] = [1]; "
        "WriteState :: () { items[0] = 2; } "
        "WriteReference :: (values: ref int[]) { values[0] = 3; } "
        "Forward :: (values: ref int[]) { WriteReference(ref values); } "
        "WriteLocal :: () -> int { values: int[] = [1]; values[0] = 4; return "
        "values[0]; } }"
    ));
    REQUIRE(function("WriteState") && (function("WriteState")->effects & SCRIPT_EFFECT_WRITE));
    REQUIRE(!function("WriteState")->pure);
    REQUIRE(function("WriteReference") && (function("WriteReference")->effects & SCRIPT_EFFECT_WRITE));
    REQUIRE(!function("WriteReference")->pure);
    REQUIRE(function("Forward") && (function("Forward")->effects & SCRIPT_EFFECT_WRITE));
    REQUIRE(!function("Forward")->pure);
    REQUIRE(function("WriteLocal") && function("WriteLocal")->pure);
    return true;
}

TEST_CASE(effects_query_checks_inferred_helpers_and_retained_reads)
{
    REQUIRE(parse(
        "Filter :: system( if Match(3)) { Match :: (n: int) -> bool { local := "
        "n + 1; return local "
        "> 0; } }"
    ));
    REQUIRE(function("Match") && function("Match")->pure);
    REQUIRE(!parse("Filter :: system( if Match()) { Match :: () -> bool { Mutate(); return true; } }"));
    REQUIRE(!parse(
        "Filter :: system( if Match()) { count: retain int = 0; Match :: () -> "
        "bool { return count "
        "> 0; } }"
    ));
    return true;
}
TEST_CASE(effects_component_await_is_synchronous_but_host_await_is_not)
{
    REQUIRE(parse(
        "Effects :: library { "
        "Set :: () -> int { await Store(1); return 2; } "
        "Run :: () -> int { return Set(); } "
        "Host :: () -> int { return await Opaque(); } }"
    ));
    REQUIRE(function("Set") && !function("Set")->pure);
    REQUIRE(!(function("Set")->effects & SCRIPT_EFFECT_SUSPEND));
    REQUIRE(function("Run") && !(function("Run")->effects & SCRIPT_EFFECT_SUSPEND));
    REQUIRE(function("Host") && function("Host")->effects & SCRIPT_EFFECT_SUSPEND);
    bool store = false, opaque = false;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *e = &analysis->expressions[i];
        if (e->kind != SCRIPT_EXPR_CALL) {
            continue;
        }
        if (e->capability == 3) {
            REQUIRE(!e->awaited && !e->task);
            store = true;
        }
        if (e->capability == 4) {
            REQUIRE(e->awaited && !e->task);
            opaque = true;
        }
    }
    REQUIRE(store && opaque);
    return true;
}
TEST_CASE(effects_imports_publish_transitive_effects_before_query_analysis)
{
    strcpy(root, "/tmp/chibi-effects-XXXXXX");
    REQUIRE(mkdtemp(root));
    const char *texts[] = {
        "A :: library { Check :: () -> bool { return B.Check(); } "
        "Start :: () { pending := B.Wait(); } }",
        "B :: library { saved: retain int = 0; "
        "Read :: () -> bool { return saved > 0; } "
        "Check :: () -> bool { Mutate(); return true; } "
        "Value :: () -> int { return 7; } Wait :: () { yield; } }",
        "C :: library { Bad :: () -> bool { return Missing(); } }",
    };
    char paths[3][256];
    source_catalog_edit edits[3];
    for (int i = 0; i < 3; i++) {
        snprintf(paths[i], sizeof paths[i], "%s/libraries/%c.chibi", root, 'A' + i);
        edits[i] = (source_catalog_edit){ .path = paths[i], .text = texts[i] };
    }
    imports = source_catalog_replaced(root, &catalog, edits, 3);
    REQUIRE(imports && !source_catalog_diagnostic_count(imports));
    const script_catalog *view = source_catalog_view(imports);
    int check = script_catalog_find_capability(view, "A.Check", SCRIPT_CAPABILITY_FUNCTION);
    int value = script_catalog_find_capability(view, "B.Value", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(check >= 0 && view->capabilities[check].effects & SCRIPT_EFFECT_WRITE);
    REQUIRE(!view->capabilities[check].pure);
    REQUIRE(
        value >= 0 && view->capabilities[value].pure &&
        !view->capabilities[value].effects
    );
    int start = script_catalog_find_capability(view, "A.Start", SCRIPT_CAPABILITY_FUNCTION);
    int bad = script_catalog_find_capability(view, "C.Bad", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(start >= 0 && view->capabilities[start].effects & SCRIPT_EFFECT_HOST);
    REQUIRE(bad >= 0 && !view->capabilities[bad].pure);
    REQUIRE(view->capabilities[bad].effects & SCRIPT_EFFECT_SUSPEND);
    int read = script_catalog_find_capability(view, "B.Read", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(read >= 0 && view->capabilities[read].pure);
    REQUIRE(view->capabilities[read].effects & SCRIPT_EFFECT_INVOCATION_READ);
    REQUIRE(!(view->capabilities[read].effects & SCRIPT_EFFECT_HOST));
    syntax = script_parse("Filter :: system( if A.Check()) {}");
    analysis = script_analyze(syntax, view);
    REQUIRE(!syntax->diagnostic_count && !analysis->valid);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    syntax = script_parse("Filter :: system( if B.Read()) {}");
    analysis = script_analyze(syntax, view);
    REQUIRE(!syntax->diagnostic_count && !analysis->valid);
    return true;
}
void test_language_effects()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(effects_infer_recursive_and_transitive_callables);
    RUN_TEST_CASE(effects_indexed_writes_follow_retained_and_reference_roots);
    RUN_TEST_CASE(effects_query_checks_inferred_helpers_and_retained_reads);
    RUN_TEST_CASE(effects_component_await_is_synchronous_but_host_await_is_not);
    RUN_TEST_CASE(effects_imports_publish_transitive_effects_before_query_analysis);
    pu_clear_setup();
}
