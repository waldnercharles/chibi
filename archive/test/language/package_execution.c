#include "execution/status.h"
#include "language/lower.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Seven finite package-member cases. Each snapshot has at most three documents,
 * every fixed source is below1KiB, and calls have budget2000. No imports are
 * executed in this first package-analysis slice. No files/processes/frames. */
enum
{
    MAX_DOCUMENTS = 3,
    MAX_ANALYSES = 3
};
typedef struct
{
    const char *path, *key, *text;
} input;
static script_syntax *syntaxes[MAX_DOCUMENTS];
static script_analysis *analyses[MAX_ANALYSES];
static execution_module_plan *plan;
static execution_module *module;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static pin_value argument, result;
static const script_catalog catalog = { 0 };
static const script_lower_bindings bindings = { .generation = 1 };
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
static const input siblings[] = {
    { "/display/pkg/a.chibi", "pkg/a.chibi", "package sample\nMain :: (value: int) -> int { return Add(value) + Bias(); }\n" },
    { "/display/pkg/z.chibi", "pkg/z.chibi", "package sample\nAdd :: (value: int) -> int { return value + 2; }\nBias :: () -> int { return 3; }\nBase: int : 4;\n" },
};
static const input unrelated = {
    "/display/pkg/00-extra.chibi",
    "pkg/00-extra.chibi",
    "package sample\nPair :: struct { left: int; right: int = Base; }\n"
    "Choice :: union { None | Value(amount: int) };\n"
    "Unrelated :: () -> int { return 99; }\n",
};
static const input good = {
    "/display/pkg/good.chibi",
    "pkg/good.chibi",
    "package sample\nGood :: () -> int { return 1; }\n",
};
static const input bad = {
    "/display/pkg/bad.chibi",
    "pkg/bad.chibi",
    "package sample\nBroken :: () -> int { return missing; }\n",
};

static void clear_runtime()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    execution_module_plan_free(plan);
    execution_module_free(module);
    plan = NULL;
    module = NULL;
}
static void cleanup()
{
    clear_runtime();
    for (int i = 0; i < MAX_DOCUMENTS; i++) {
        script_syntax_free(syntaxes[i]);
        syntaxes[i] = NULL;
    }
    for (int i = 0; i < MAX_ANALYSES; i++) {
        script_analysis_free(analyses[i]);
        analyses[i] = NULL;
    }
}
static bool analyze(int slot, const input *inputs, int count, bool declarations)
{
    REQUIRE(slot >= 0 && slot < MAX_ANALYSES && count > 0 && count <= MAX_DOCUMENTS);
    script_analysis_free(analyses[slot]);
    analyses[slot] = NULL;
    script_package_document documents[MAX_DOCUMENTS];
    for (int i = 0; i < count; i++) {
        REQUIRE(strlen(inputs[i].text) < 1024);
        syntaxes[i] = script_parse(inputs[i].text);
        REQUIRE(syntaxes[i]);
        documents[i] = (script_package_document){ inputs[i].path,
                                                  inputs[i].key,
                                                  syntaxes[i] };
    }
    const script_package_options options = { .documents = documents,
                                             .document_count = count,
                                             .identity = "project:sample",
                                             .native = &catalog };
    analyses[slot] = declarations ? script_declarations_package(&options)
                                  : script_analyze_package(&options);
    for (int i = 0; i < count; i++) {
        script_syntax_free(syntaxes[i]);
        syntaxes[i] = NULL;
    }
    REQUIRE(analyses[slot]);
    REQUIRE(analyses[slot]->document_count == count);
    return true;
}
static bool valid(int slot)
{
    for (int i = 0; i < analyses[slot]->diagnostic_count; i++) {
        const script_diagnostic *diagnostic = &analyses[slot]->diagnostics[i];
        fprintf(
            stderr,
            "package %d:%d:%d: %s\n",
            diagnostic->range.document,
            diagnostic->range.line,
            diagnostic->range.column,
            diagnostic->message
        );
    }
    return analyses[slot]->valid && !analyses[slot]->diagnostic_count;
}
static uint64_t function_id(int slot, const char *name)
{
    const script_analysis *analysis = analyses[slot];
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->kind == SCRIPT_FUNCTION && !strcmp(symbol->name, name)) {
            return symbol->id;
        }
    }
    return 0;
}
static bool lower(int slot, behavior_backend backend)
{
    clear_runtime();
    REQUIRE(valid(slot));
    plan = script_lower(analyses[slot], &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    char error[256] = { 0 };
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "package module: %s\n", error);
    }
    REQUIRE(module && execution_module_backend(module) == backend);
    return true;
}
static const behavior_function *body(uint64_t id)
{
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info info = execution_module_function_at(module, i);
        if (info.id == id) {
            return info.body;
        }
    }
    return NULL;
}
static bool call(uint64_t id, behavior_backend backend, int value, int expected)
{
    const behavior_function *function = body(id);
    REQUIRE(function);
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    activation = (behavior_function_activation){ 0 };
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    argument = (pin_value){ .type = PIN_I32, .i = value };
    result = (pin_value){ .type = PIN_I32, .i = -999 };
    context = (behavior_function_context){
        .activation = behavior_function_resumable(function) ? &activation : NULL,
        .arguments = &argument,
        .argument_count = 1,
        .results = &result,
        .budget = 2000,
    };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    int status = behavior_function_run(function, &context, backend == BEHAVIOR_SLJIT);
    behavior_run_scope_end(&scope);
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(result.type == PIN_I32 && result.i == expected);
    REQUIRE(!scope.counts.overflow && !scope.counts.inspection_bytecode_fallbacks);
    REQUIRE(
        !scope.counts.inspected_sljit_entries &&
        !scope.counts.inspected_wasm_entries && !scope.counts.wasm_entries
    );
    REQUIRE(
        backend == BEHAVIOR_SLJIT
            ? scope.counts.sljit_entries > 0 && !scope.counts.bytecode_entries
            : scope.counts.bytecode_entries > 0 && !scope.counts.sljit_entries
    );
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    return true;
}

TEST_CASE(package_sibling_functions_execute_on_each_native_backend)
{
    REQUIRE(analyze(0, siblings, 2, false));
    REQUIRE(valid(0));
    uint64_t main = function_id(0, "Main");
    REQUIRE(main && analyses[0]->function_count == 3);
    for (int b = 0; b < 2; b++) {
        REQUIRE(lower(0, backends[b]));
        REQUIRE(call(main, backends[b], 7, 12));
    }
    return true;
}
TEST_CASE(package_sibling_mutual_recursion_executes_on_each_native_backend)
{
    const input recursive[] = {
        { "/display/pkg/even.chibi", "pkg/even.chibi", "package sample\nEven :: (n: int) -> int { if (n == 0) { return 1; } return Odd(n - 1); }\n" },
        { "/display/pkg/odd.chibi", "pkg/odd.chibi", "package sample\nOdd :: (n: int) -> int { if (n == 0) { return 0; } return Even(n - 1); }\n" },
    };
    REQUIRE(analyze(0, recursive, 2, false));
    REQUIRE(valid(0));
    uint64_t even = function_id(0, "Even");
    REQUIRE(even && function_id(0, "Odd"));
    static const int inputs[] = { 0, 1, 6, 7 };
    for (int b = 0; b < 2; b++) {
        REQUIRE(lower(0, backends[b]));
        for (int i = 0; i < 4; i++) {
            REQUIRE(call(even, backends[b], inputs[i], !(inputs[i] % 2)));
        }
    }
    return true;
}
TEST_CASE(package_sibling_records_unions_and_constants_execute)
{
    const input consumer = {
        "/display/pkg/typed.chibi",
        "pkg/typed.chibi",
        "package sample\nTyped :: (value: int) -> int { "
        "pair: Pair = {left: value}; "
        "choice := Choice.Value(pair.left + pair.right); "
        "if (value == 0) { choice = Choice.None; } "
        "switch choice { case .None: return 0; case .Value(amount): return "
        "amount + Base; } }\n",
    };
    const input documents[] = { consumer, unrelated, siblings[1] };
    REQUIRE(analyze(0, documents, 3, false));
    REQUIRE(valid(0));
    REQUIRE(analyses[0]->type_count == 2);
    uint64_t typed = function_id(0, "Typed");
    REQUIRE(typed);
    for (int b = 0; b < 2; b++) {
        REQUIRE(lower(0, backends[b]));
        REQUIRE(call(typed, backends[b], 0, 0));
        REQUIRE(call(typed, backends[b], 7, 15));
    }
    return true;
}
TEST_CASE(package_member_ids_ignore_input_order_and_unrelated_sibling)
{
    REQUIRE(analyze(0, siblings, 2, false));
    const input reverse[] = { siblings[1], siblings[0] };
    REQUIRE(analyze(1, reverse, 2, false));
    const input expanded[] = { unrelated, siblings[1], siblings[0] };
    REQUIRE(analyze(2, expanded, 3, false));
    static const char *names[] = { "Main", "Add", "Bias" };
    for (int slot = 0; slot < 3; slot++) {
        REQUIRE(valid(slot));
    }
    for (int i = 0; i < 3; i++) {
        uint64_t id = function_id(0, names[i]);
        REQUIRE(id && function_id(1, names[i]) == id && function_id(2, names[i]) == id);
    }
    REQUIRE(function_id(2, "Unrelated"));
    return true;
}
TEST_CASE(package_secondary_document_diagnostics_keep_local_origins)
{
    const input invalid[] = {
        { "/display/pkg/syntax.chibi",
          "pkg/syntax.chibi",
          "package sample\nBroken :: () -> int { return 1;\n" },
        bad,
        { "/display/pkg/header.chibi",
          "pkg/header.chibi",
          "package other\nOther :: () -> int { return 2; }\n" },
        { "/display/pkg/import.chibi",
          "pkg/import.chibi",
          "package sample\nimport math \"core:math\"\n" },
    };
    for (int sample = 0; sample < 4; sample++) {
        const input documents[] = { good, invalid[sample] };
        REQUIRE(analyze(0, documents, 2, false));
        REQUIRE(!analyses[0]->valid && analyses[0]->diagnostic_count > 0);
        bool located = false, missing_token = false;
        for (int i = 0; i < analyses[0]->diagnostic_count; i++) {
            const script_diagnostic *diagnostic = &analyses[0]->diagnostics[i];
            if (diagnostic->range.document != 1) {
                continue;
            }
            const script_source_document *
                origin = script_analysis_document(analyses[0], diagnostic->range.document);
            REQUIRE(origin && !strcmp(origin->path, invalid[sample].path));
            REQUIRE(
                diagnostic->range.start >= 0 &&
                diagnostic->range.end >= diagnostic->range.start
            );
            REQUIRE((size_t)diagnostic->range.end <= strlen(invalid[sample].text));
            if (sample == 1) {
                const char *missing = strstr(invalid[sample].text, "missing");
                missing_token |= diagnostic->range.start <=
                                     missing - invalid[sample].text &&
                                 diagnostic->range.end >=
                                     missing - invalid[sample].text + 7;
            }
            located = true;
        }
        REQUIRE(located && (sample != 1 || missing_token));
        clear_runtime();
        plan = script_lower(analyses[0], &catalog, &bindings);
        REQUIRE(plan && !plan->valid && plan->diagnostic_count > 0);
        bool lower_located = false;
        for (int i = 0; i < plan->diagnostic_count; i++) {
            execution_source_range range = plan->diagnostics[i].range;
            if (range.document != 1) {
                continue;
            }
            REQUIRE(range.start >= 0 && range.end >= range.start);
            REQUIRE((size_t)range.end <= strlen(invalid[sample].text));
            for (int j = 0; j < analyses[0]->diagnostic_count; j++) {
                script_range original = analyses[0]->diagnostics[j].range;
                lower_located |= original.document == range.document &&
                                 original.start == range.start &&
                                 original.end == range.end &&
                                 original.line == range.line &&
                                 original.column == range.column;
            }
        }
        REQUIRE(lower_located);
    }
    return true;
}
TEST_CASE(package_duplicate_exports_reject_and_declaration_discovery_skips_bodies)
{
    const input duplicate = {
        "/display/pkg/duplicate.chibi",
        "pkg/duplicate.chibi",
        "package sample\nGood :: () -> int { return 9; }\n"
    };
    const input documents[] = { good, duplicate };
    REQUIRE(analyze(0, documents, 2, false));
    REQUIRE(!analyses[0]->valid && analyses[0]->diagnostic_count > 0);
    bool duplicate_site = false;
    for (int i = 0; i < analyses[0]->diagnostic_count; i++) {
        duplicate_site |= analyses[0]->diagnostics[i].range.document == 1;
    }
    REQUIRE(duplicate_site);
    const input unchecked[] = { good, bad };
    REQUIRE(analyze(1, unchecked, 2, true));
    REQUIRE(valid(1));
    REQUIRE(function_id(1, "Good") && function_id(1, "Broken"));
    REQUIRE(analyze(2, unchecked, 2, false));
    REQUIRE(!analyses[2]->valid);
    return true;
}
TEST_CASE(package_analysis_and_lowered_sources_own_document_lifetimes)
{
    for (int b = 0; b < 2; b++) {
        char *paths[2], *keys[2], *texts[2];
        input documents[2];
        for (int i = 0; i < 2; i++) {
            paths[i] = strdup(siblings[i].path);
            keys[i] = strdup(siblings[i].key);
            texts[i] = strdup(siblings[i].text);
            documents[i] = (input){ paths[i], keys[i], texts[i] };
        }
        bool prepared = analyze(0, documents, 2, false);
        for (int i = 0; i < 2; i++) {
            free(paths[i]);
            free(keys[i]);
            free(texts[i]);
        }
        REQUIRE(prepared && valid(0));
        for (int i = 0; i < 2; i++) {
            const script_source_document *document = script_analysis_document(analyses[0], i);
            REQUIRE(document && !strcmp(document->path, siblings[i].path));
            REQUIRE(
                !strcmp(document->key, siblings[i].key) &&
                !strcmp(document->text, siblings[i].text)
            );
        }
        REQUIRE(!script_analysis_document(analyses[0], -1));
        REQUIRE(!script_analysis_document(analyses[0], 2));
        uint64_t main = function_id(0, "Main"), add = function_id(0, "Add");
        REQUIRE(main && add && lower(0, backends[b]));
        script_analysis_free(analyses[0]);
        analyses[0] = NULL;
        REQUIRE(call(main, backends[b], 9, 14));
        const behavior_function *function = body(add);
        REQUIRE(function && behavior_function_source_count(function) > 0);
        bool mapped = false;
        for (int i = 0; i < behavior_function_source_count(function); i++) {
            const behavior_function_source *origin = behavior_function_source_at(function, i);
            if (!origin || !origin->asset_name ||
                strcmp(origin->asset_name, siblings[1].path)) {
                continue;
            }
            REQUIRE(origin->start >= 0 && origin->end >= origin->start);
            REQUIRE((size_t)origin->end <= strlen(siblings[1].text));
            REQUIRE(origin->line >= 2);
            mapped = true;
        }
        REQUIRE(mapped);
    }
    return true;
}
void test_language_package_execution()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_sibling_functions_execute_on_each_native_backend);
    RUN_TEST_CASE(package_sibling_mutual_recursion_executes_on_each_native_backend);
    RUN_TEST_CASE(package_sibling_records_unions_and_constants_execute);
    RUN_TEST_CASE(package_member_ids_ignore_input_order_and_unrelated_sibling);
    RUN_TEST_CASE(package_secondary_document_diagnostics_keep_local_origins);
    RUN_TEST_CASE(package_duplicate_exports_reject_and_declaration_discovery_skips_bodies);
    RUN_TEST_CASE(package_analysis_and_lowered_sources_own_document_lifetimes);
    pu_clear_setup();
}
