#include "assets/code_source_internal.h"
#include "graph/document/serialize.h"
#include "graph/runtime/instance.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

static code_source *parse_statements(const char *text, const char *message)
{
    code_source *source = code_source_parse(text);
    if (!source->asset) {
        for (int i = 0; i < acount(source->diagnostics); i++) {
            const code_source_diagnostic *d = &source->diagnostics[i];
            int length = (int)strlen(source->text);
            int start = d->start >= 0 && d->start <= length ? d->start : 0;
            int end = d->end >= start && d->end <= length ? d->end : start;
            fprintf(
                stderr,
                "%s:%d:%d [%d..%d] %s; source: %.*s\n",
                message,
                d->line,
                d->column,
                d->start,
                d->end,
                d->message,
                end - start,
                source->text + start
            );
        }
    }
    test_check(source->asset != NULL, message);
    return source;
}

static void optional_semicolons()
{
    const char *free_motion = "system FreeMotion(position: Transform, "
                              "velocity: Velocity)\n"
                              "without Collider, Body\n"
                              "{\n"
                              "    position += velocity * dt\n"
                              "}\n";
    code_source *source = parse_statements(free_motion, "FreeMotion accepts its concise component formula without a semicolon");
    if (source->asset) {
        char *saved = code_source_persist(source);
        test_check(!strcmp(saved, free_motion), "Persisting FreeMotion retains its semicolon-free authored text");
        free(saved);
    }
    code_source_free(source);

    const char
        *text = "behavior Newlines {\n"
                "    state total := 0 // double precision inferred state\n"
                "    exact: int64 = 9007199254740993\n"
                "    samples := [\n"
                "        1, /* comma inside a comment: , */\n"
                "        2.5\n"
                "    ]\n"
                "    value step = real(\n"
                "        Value: 0.125 // parenthesis remains open\n"
                "    )\n"
                "    on Tick {\n"
                "        saved := step\n"
                "        total += saved + // trailing operator continues\n"
                "            0.125\n"
                "        total += 0.25\n"
                "            +0.25 // leading signed number continues\n"
                "    }\n"
                "}\n";
    source = parse_statements(text, "Newlines separate declarations, eager locals and assignments while nested expressions continue");
    if (source->asset) {
        char *saved = code_source_persist(source);
        char *metadata = code_source_metadata_export_source(source);
        code_source *reopened = code_source_parse(saved);
        test_check(
            !strcmp(saved, text) && reopened->asset &&
                code_source_metadata_restore(reopened, metadata) &&
                behavior_asset_equal(source->asset, reopened->asset),
            "Semicolon-free source preserves comments, precision and exact "
            "native identity on reopen"
        );
        char error[256] = { 0 };
        char *reconciled_metadata = NULL;
        char *reconciled = code_source_reconcile_with_metadata(
            source,
            source->asset,
            &reconciled_metadata,
            error,
            sizeof error
        );
        if (!reconciled) {
            fprintf(stderr, "Semicolon-free reconcile: %s\n", error);
        }
        test_check(reconciled && !strcmp(reconciled, text), "Unchanged graph reconciliation preserves semicolon-free source and comments");
        free(reconciled_metadata);
        free(reconciled);
        code_source_free(reopened);
        free(metadata);
        free(saved);
        behavior_asset *asset = code_source_compile_behavior(source);
        behavior_instance *instance = asset
                                        ? behavior_instance_create(asset, NULL, NULL, NULL)
                                        : NULL;
        flow_error error_run = { 0 };
        if (instance) {
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error_run);
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error_run);
        }
        test_check(
            instance && !error_run.message && instance->variables[0].d == 1.5 &&
                instance->variables[1].i64 == INT64_C(9007199254740993),
            "Optional semicolons preserve fractional runtime arithmetic and "
            "exact explicit integers"
        );
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
    }
    code_source_free(source);

    source = parse_statements(
        "behavior Transfers {\n"
        "    samples := [1, 2]\n"
        "    function Visit() -> () {\n"
        "        after entry {\n"
        "            for (item in samples) {\n"
        "                if item > 1 { continue }\n"
        "                break\n"
        "            }\n"
        "            return\n"
        "        }\n"
        "    }\n"
        "    on Begin {\n"
        "        Visit()\n"
        "        await delay(\n"
        "            Duration: real(Value: 1)\n"
        "        )\n"
        "    }\n"
        "}",
        "Calls, await, return, break and continue accept newline or "
        "closing-brace terminators"
    );
    code_source_free(source);
    source = parse_statements(
        "behavior MixedDelimiters {\r\n"
        "    total := 0; other := 1\r\n"
        "    on Tick { total += other; total += 0.5 }\r\n"
        "}",
        "Explicit semicolons can mix with CRLF newlines and final statements "
        "before a brace"
    );
    code_source_free(source);
    source = parse_statements("behavior ReturnBrace { function Exit() -> () { after entry { return } } }", "A bare return may end directly before its closing brace");
    code_source_free(source);
    source = parse_statements(
        "behavior CommentBoundary { first := 0 /* boundary\n"
        "        inside multiline comment */ second := 1\n"
        "    on Begin { await delay(Duration: 1) /* another\n"
        "        boundary */ first = second } }",
        "A newline inside block-comment trivia separates complete statements"
    );
    code_source_free(source);
    source = parse_statements(
        "behavior BindingLines {\n"
        "    bind self.ParticleEmitter.Active =\n"
        "        self.ParticleEmitter.Parent.ActionBinding.Pressed\n"
        "    state total := 0\n"
        "}",
        "A binding RHS continues after equals and ends before the next "
        "declaration"
    );
    code_source_free(source);
    const char *invalid[] = {
        "behavior Missing { first := 0 second := 1 }",
        "behavior Missing { total := 0; on Tick { total += 1 total += 2 } }",
        "behavior Missing { on Begin { delay(Duration: 1) delay(Duration: 2) } "
        "}"
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof invalid[0]); i++) {
        source = code_source_parse(invalid[i]);
        test_check(!source->asset && acount(source->diagnostics), "Same-line statements still require an explicit separator");
        code_source_free(source);
    }
}

void run_code_declaration_checks()
{
    optional_semicolons();
    code_source *source = code_source_parse(
        "behavior Counter { elapsed := 0; enabled := true; title := "
        "\"Counter\"; "
        "samples := [1, 2.5]; exact: int64 = 9007199254740993; "
        "on Tick() { elapsed += 0.25; } }"
    );
    test_check(source->asset != NULL, "Inferred behavior declarations accept numeric, boolean, string and array defaults");
    behavior_asset *asset = code_source_compile_behavior(source);
    if (asset) {
        test_check(
            asset->variables[0].type == PIN_F64 && asset->variables[3].type == PIN_F64 &&
                asset->variables[3].container == PIN_CONTAINER_ARRAY &&
                asset->variables[4].type == PIN_I64 &&
                asset->variables[4].default_value.i64 == INT64_C(9007199254740993),
            "Inferred numbers use double precision while explicit integer "
            "identity remains exact"
        );
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error error = { 0 };
        test_check(instance != NULL, "Inferred declarations construct runtime state");
        if (instance) {
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            test_check(!error.message && instance->variables[0].d == 0.5, "An integer-looking inferred default accumulates fractions across event invocations");
        }
        behavior_instance_destroy(instance);
        char *printed = code_source_format(asset);
        code_source *again = printed ? code_source_parse(printed) : NULL;
        test_check(
            again && again->asset && again->asset->variables[0].type == PIN_F64 &&
                again->asset->variables[4].default_value.i64 == INT64_C(9007199254740993),
            "Canonical printing retains inferred numeric storage and explicit "
            "integer precision"
        );
        code_source_free(again);
        free(printed);
    }
    behavior_asset_free(asset);
    code_source_free(source);

    source = code_source_parse("behavior Explicit { elapsed: number = 0; on Tick() { elapsed += 0.25; } }");
    test_check(
        source->asset && source->asset->variables[0].type == PIN_F64,
        "The number spelling selects double precision storage"
    );
    code_source_free(source);
    source = code_source_parse("behavior Empty { values := []; }");
    test_check(!source->asset && acount(source->diagnostics), "Empty collections require an explicit element type");
    code_source_free(source);
    source = code_source_parse("behavior Mixed { values := [true, 1]; }");
    test_check(!source->asset && acount(source->diagnostics), "Inference rejects mixed collection element types");
    code_source_free(source);
}
