#include "assets/code_source.h"

#include "assets/code_print.h"
#include "assets/code_value.h"
#include "graph/authoring/pin_edit.h"
#include "graph/compiler/behavior_program.h"
#include "graph/compiler/compile.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"
#include "runtime/systems/system.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);
static node *first_expression(behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        if (asset->graph->nodes[i]->desc == &NODE_BehaviorExpression) {
            return asset->graph->nodes[i];
        }
    }
    return NULL;
}
static void diagnostics(const code_source *source)
{
    for (int i = 0; i < acount(source->diagnostics); i++) {
        fprintf(
            stderr,
            "code source %d:%d: %s\n",
            source->diagnostics[i].line,
            source->diagnostics[i].column,
            source->diagnostics[i].message
        );
    }
}
static void examples()
{
    const char *paths[] = { "examples/behaviors/ProjectileLifecycle.code",
                            "examples/behaviors/ProjectilePulse.code",
                            "examples/behaviors/ContactRegistry.code" };
    for (int i = 0; i < 3; i++) {
        char *text = text_file_read(paths[i]);
        test_check(text != NULL, "Structured behavior example is available");
        if (!text) {
            continue;
        }
        code_source *source = code_source_parse(text);
        behavior_asset *candidate = code_source_compile_behavior(source);
        if (!candidate) {
            diagnostics(source);
        }
        test_check(candidate != NULL, "Structured behavior example constructs and validates native graph directly");
        if (candidate) {
            char *metadata = NULL;
            char *formatted = code_source_format_with_metadata(candidate, &metadata);
            code_source *roundtrip = code_source_parse(formatted);
            test_check(code_source_metadata_restore(roundtrip, metadata), "Example graph metadata restores independently from executable source");
            free(metadata);
            if (!roundtrip->asset) {
                fprintf(stderr, "Failed formatted source example: %s\n%s\n", paths[i], formatted);
                diagnostics(roundtrip);
            }
            test_check(roundtrip->asset && behavior_asset_equal(roundtrip->asset, candidate), "Readable canonical source preserves example native topology and identities");
            if (roundtrip->asset && !behavior_asset_equal(roundtrip->asset, candidate)) {
                const behavior_asset *models[] = { candidate, roundtrip->asset };
                const char *labels[] = { "expected", "actual" };
                for (int side = 0; side < 2; side++) {
                    av_doc *doc = av_doc_new();
                    char *model = code_value_write(
                        behavior_asset_to_value(doc, models[side])
                    );
                    char path[128];
                    snprintf(path, sizeof path, "build/code-source-example-%d-%s.txt", i, labels[side]);
                    FILE *file = fopen(path, "wb");
                    if (file && model) {
                        fwrite(model, 1, strlen(model), file);
                    }
                    if (file) {
                        fclose(file);
                    }
                    free(model);
                    av_doc_free(doc);
                }
            }
            code_source_free(roundtrip);
            free(formatted);
        }
        behavior_asset_free(candidate);
        code_source_free(source);
        free(text);
    }
}
static void incremental()
{
    const char
        *text = "// Behavior comment\nbehavior Counter {\n state float count = "
                "1;\n on Tick() {\n // Action comment\n count += 2;\n }\n}";
    code_source *source = code_source_parse(text);
    test_check(source->asset != NULL, "Scalar state and assignments construct a native behavior");
    if (!source->asset) {
        diagnostics(source);
        code_source_free(source);
        return;
    }
    node *n = first_expression(source->asset);
    graph_set_node_comment(source->asset->graph, n, "Graph comment", false);
    graph_set_breakpoint(source->asset->graph, n, true);
    n->placed = true;
    n->x = 400;
    n->y = 200;
    uint64_t id = n->id, generated = n->reference_id;
    const char
        *edited = "// Behavior comment\nbehavior Counter {\n state float count "
                  "= 3;\n on Tick() {\n // Action comment\n count += 4;\n }\n}";
    code_source *next = code_source_parse_previous(edited, source);
    if (!next->asset) {
        diagnostics(next);
    }
    node *updated = next->asset ? graph_node_by_id(next->asset->graph, id) : NULL;
    test_check(
        updated && updated->reference_id == generated && updated->placed &&
            updated->x == 400 && updated->breakpoint && updated->comment &&
            !strcmp(updated->comment, "Graph comment"),
        "Source edits preserve authored and generated identities, layout, "
        "debugger state and comments"
    );
    char *persisted = code_source_persist(source);
    code_source *reopened = persisted ? code_source_parse(persisted) : NULL;
    char *metadata = code_source_metadata_export_source(source);
    if (reopened) {
        code_source_metadata_restore(reopened, metadata);
    }
    free(metadata);
    test_check(
        reopened && reopened->asset &&
            behavior_asset_equal(reopened->asset, source->asset),
        "Saved structured source reloads independently with complete "
        "identities and authoring metadata"
    );
    char *again = reopened ? code_source_persist(reopened) : NULL;
    test_check(
        persisted && again && !strcmp(persisted, again),
        "Structured persistence is idempotent and keeps authored whitespace"
    );
    free(again);
    code_source_free(reopened);
    free(persisted);
    code_source *inserted = code_source_parse_previous(
        "// Behavior comment\nbehavior Counter {\n state float count = 1;\n on "
        "Tick() {\n // Action comment\n count += 2;\n count += 9;\n }\n}",
        source
    );
    node *surviving = inserted->asset ? graph_node_by_id(inserted->asset->graph, id)
                                      : NULL;
    test_check(
        surviving && surviving->comment &&
            !strcmp(surviving->comment, "Graph comment") && surviving->placed &&
            surviving->breakpoint,
        "Unambiguous statement insertion preserves surviving native authoring "
        "metadata"
    );
    code_source_free(inserted);
    behavior_asset *visual = behavior_asset_clone(source->asset);
    node *changed = graph_node_by_id(visual->graph, id);
    behavior_expression_error error;
    char why[256];
    char *native_expression = code_source_resolve_expression(source, visual, changed, "count += 7", why, sizeof why);
    test_check(
        native_expression &&
            behavior_expression_set(visual, changed, native_expression, &error) == 0,
        "Visual expression edit changes the shared native model"
    );
    free(native_expression);
    char *reconciled = code_source_reconcile(source, visual, why, sizeof why);
    if (!reconciled || !strstr(reconciled, "count += 7")) {
        fprintf(stderr, "Visual source reconciliation: %s\n%s\n", why, reconciled ? reconciled : "(no source)");
    }
    test_check(
        reconciled && strstr(reconciled, "behavior Counter") &&
            strstr(reconciled, "count += 7") && strstr(reconciled, "// Action comment"),
        "Visual expression edits preserve readable structured code and comments"
    );
    free(reconciled);
    behavior_asset *debug = behavior_asset_clone(source->asset);
    graph_set_breakpoint(debug->graph, graph_node_by_id(debug->graph, id), false);
    metadata = NULL;
    reconciled = code_source_reconcile_with_metadata(source, debug, &metadata, why, sizeof why);
    code_source *debug_source = reconciled ? code_source_parse(reconciled) : NULL;
    if (debug_source) {
        code_source_metadata_restore(debug_source, metadata);
    }
    test_check(
        debug_source && debug_source->program && debug_source->asset &&
            behavior_asset_equal(debug_source->asset, debug) &&
            strstr(reconciled, "// Action comment"),
        "Breakpoint changes preserve structured source, comments and complete "
        "authoring metadata"
    );
    code_source *debug_previous = reconciled
                                    ? code_source_parse_previous(reconciled, source)
                                    : NULL;
    if (debug_previous) {
        code_source_metadata_restore(debug_previous, metadata);
    }
    free(metadata);
    metadata = NULL;
    test_check(
        debug_previous && debug_previous->asset &&
            behavior_asset_equal(debug_previous->asset, debug),
        "Graph breakpoint metadata is applied when changed persisted text uses "
        "incremental parsing"
    );
    code_source_free(debug_previous);
    code_source_free(debug_source);
    free(reconciled);
    behavior_asset_free(debug);
    behavior_asset *topology = behavior_asset_clone(source->asset);
    graph_add_node(topology->graph, &NODE_PrintString);
    reconciled = code_source_reconcile_with_metadata(source, topology, &metadata, why, sizeof why);
    code_source *explicit = reconciled ? code_source_parse(reconciled) : NULL;
    if (explicit) {
        code_source_metadata_restore(explicit, metadata);
    }
    free(metadata);
    if (!explicit || !explicit->asset ||
        !behavior_asset_equal(explicit->asset, topology)) {
        fprintf(stderr, "Topology source reconciliation: %s\n%s\n", why, reconciled ? reconciled : "(no source)");
    }
    test_check(
        explicit && explicit->asset && behavior_asset_equal(explicit->asset, topology) &&
            strstr(reconciled, "// Action comment"),
        "Graph topology edits remain representable as complete "
        "readable source with comments"
    );
    code_source_free(explicit);
    free(reconciled);
    behavior_asset_free(topology);
    behavior_asset_free(visual);
    code_source_free(next);
    code_source_free(source);
}
static void failed_edits()
{
    code_source *source = code_source_parse(
        "behavior Counter { state float count = 1; on Tick() { count += 1; } }"
    );
    behavior_asset *copy = source->asset ? behavior_asset_clone(source->asset) : NULL;
    code_source *invalid = code_source_parse_previous("behavior Counter { state float count = 1; on Tick() { missing += 1; } }", source);
    test_check(
        !invalid->asset && acount(invalid->diagnostics) > 0 && copy &&
            behavior_asset_equal(copy, source->asset),
        "Failed source edits retain the prior native generation unchanged"
    );
    code_source_free(invalid);
    invalid = code_source_parse_previous("behavior Counter { state float", source);
    test_check(!invalid->asset && acount(invalid->diagnostics) > 0, "Incomplete declaration drafts produce diagnostics without touching prior identities");
    code_source_free(invalid);
    behavior_asset_free(copy);
    code_source_free(source);
    source = code_source_parse("behavior Counter { state float count = 1; on Tick() { count += 1; count += 1; } }");
    invalid = code_source_parse_previous("behavior Counter { state float count = 1; on Tick() { count += 2; count += 1; } }", source);
    test_check(invalid->asset != NULL, "Ambiguous repeated statement identities cannot invalidate executable source");
    code_source_free(invalid);
    code_source_free(source);
}
static void repeated_statement_persistence()
{
    const char *text = "behavior Repeated { state int count = 0; on Tick() { "
                       "count += 1; count += 1; } }";
    code_source *source = code_source_parse(text);
    char *saved = source->asset ? code_source_persist(source) : NULL;
    code_source *reopened = saved ? code_source_parse(saved) : NULL;
    char *metadata = source->asset ? code_source_metadata_export_source(source) : NULL;
    if (reopened && metadata) {
        code_source_metadata_restore(reopened, metadata);
    }
    free(metadata);
    char *again = reopened && reopened->asset ? code_source_persist(reopened) : NULL;
    test_check(
        reopened && reopened->asset &&
            behavior_asset_equal(source->asset, reopened->asset),
        "One-line repeated statements reopen independently with exact native "
        "identities"
    );
    test_check(saved && again && !strcmp(saved, again), "Repeated statements persist idempotently without code annotations");
    behavior_instance *instance = reopened && reopened->asset
                                    ? behavior_instance_create(reopened->asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].i == 2,
        "Independent repeated statement reload executes both assignments once"
    );
    behavior_instance_destroy(instance);
    free(again);
    code_source_free(reopened);
    free(saved);
    code_source_free(source);
}
static void execution()
{
    const char *text = "behavior Sum { state int[] values = [1, 2, 3]; state "
                       "float total = 0; on Tick() { for (item in values) { "
                       "if (item > 1) { total += item; } } total += 1; } }";
    code_source *source = code_source_parse(text);
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    test_check(asset != NULL, "Structured control flow supports branch joins followed by statements");
    if (!asset) {
        code_source_free(source);
        return;
    }
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    test_check(instance != NULL, "Structured source publishes through the existing runtime compiler");
    if (instance) {
        flow_error error = { 0 };
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        int index = 0;
        for (int i = 0; i < acount(asset->variables); i++) {
            if (!strcmp(asset->variables[i].name, "total")) {
                index = i;
            }
        }
        if (behavior_instance_last_failure(instance) ||
            instance->variables[index].f != 6) {
            fprintf(
                stderr,
                "foreach result: %g (expected 6), failed: %d\n",
                instance->variables[index].f,
                behavior_instance_last_failure(instance) != NULL
            );
        }
        test_check(
            !behavior_instance_last_failure(instance) &&
                instance->variables[index].f == 6,
            "Structured native foreach and branch code executes the expected "
            "result"
        );
        behavior_asset *oracle_asset = behavior_asset_clone(asset);
        behavior_instance *oracle = behavior_instance_create(oracle_asset, NULL, NULL, NULL);
        behavior_instance_set_backend(oracle, BEHAVIOR_ORACLE);
        behavior_instance_dispatch(oracle, "Tick", (flow_inputs){ 0 }, &error);
        test_check(
            !behavior_instance_last_failure(oracle) &&
                oracle->variables[index].f == instance->variables[index].f,
            "Structured source compiled execution matches the independent "
            "graph oracle"
        );
        behavior_instance_destroy(oracle);
        behavior_asset_free(oracle_asset);
        code_source *defaults = code_source_parse_previous(
            "behavior Sum { state int[] values = [1, 2, 3]; state float total "
            "= 100; on Tick() { for (item in values) { if (item > 1) { total "
            "+= item; } } total += 1; } }",
            source
        );
        behavior_instance *fresh = behavior_instance_create(asset, NULL, NULL, NULL);
        test_check(
            defaults->asset && behavior_instance_can_update(fresh, defaults->asset) ==
                                   INSTANCE_UPDATE_READY,
            "State default edits remain compatible before an activation "
            "borrows loop scratch"
        );
        if (defaults->asset && behavior_instance_can_update(fresh, defaults->asset) ==
                                   INSTANCE_UPDATE_READY) {
            behavior_instance_update(fresh, defaults->asset);
            test_check(fresh->variables[index].f == 0, "Compatible live update keeps current values rather than applying changed defaults");
        }
        behavior_instance_destroy(fresh);
        code_source *layout = code_source_parse_previous(
            "behavior Sum { state int[] values = [1, 2, 3]; state int total = "
            "0; on Tick() { for (item in values) { if (item > 1) { total += "
            "item; } } total += 1; } }",
            source
        );
        test_check(
            layout->asset && behavior_instance_can_update(instance, layout->asset) ==
                                 INSTANCE_UPDATE_RESTART,
            "State type edits construct valid source and require explicit "
            "runtime restart"
        );
        code_source_free(defaults);
        code_source_free(layout);
        behavior_instance_destroy(instance);
    }
    behavior_asset_free(asset);
    code_source_free(source);
}
static void expression_labels()
{
    const char *text = "behavior Labels { state age := 0; on "
                       "Tick(GameProjectile projectile) { "
                       "age = projectile.age + event.dt; } }";
    code_source *source = code_source_parse(text);
    node *expression = source->asset ? first_expression(source->asset) : NULL;
    char *readable = expression
                       ? code_source_expression(source, source->asset, expression)
                       : NULL;
    char error[256];
    char *lowered = readable
                      ? code_source_resolve_expression(source, source->asset, expression, readable, error, sizeof error)
                      : NULL;
    test_check(
        readable && strstr(readable, "projectile.age") &&
            strstr(readable, "event.dt") && !strstr(readable, "_component_"),
        "Graph expressions display readable reflected source labels"
    );
    test_check(lowered && !strcmp(lowered, behavior_expression_source(source->asset, expression)), "Readable graph labels resolve to the same bound native expression without model mutation");
    if (!lowered ||
        strcmp(lowered, behavior_expression_source(source->asset, expression))) {
        fprintf(
            stderr,
            "Readable graph label roundtrip: readable=%s lowered=%s native=%s "
            "error=%s\n",
            readable ? readable : "NULL",
            lowered ? lowered : "NULL",
            expression ? behavior_expression_source(source->asset, expression) : "NULL",
            lowered ? "" : error
        );
    }
    free(readable);
    free(lowered);
    code_source_free(source);
    source = code_source_parse(
        "behavior EventLabels { state count: float = 0; state amount: float = "
        "1; "
        "on Tick() { count += amount * event.dt; } }"
    );
    char *metadata = NULL;
    char *formatted = source->asset
                        ? code_source_format_with_metadata(source->asset, &metadata)
                        : NULL;
    code_source *reopened = formatted ? code_source_parse(formatted) : NULL;
    if (reopened) {
        code_source_metadata_restore(reopened, metadata);
    }
    free(metadata);
    expression = reopened && reopened->asset ? first_expression(reopened->asset) : NULL;
    bool count_label = false, amount_label = false, event_label = false;
    for (int i = 0; expression && i < expression->input_count; i++) {
        const char *label = expression->input_pins[i].display_name;
        count_label |= label && !strcmp(label, "count");
        amount_label |= label && !strcmp(label, "amount");
        event_label |= label && !strcmp(label, "event.dt");
    }
    test_check(count_label && amount_label && event_label, "Generated graph capture pins retain readable authored labels across source persistence");
    behavior_asset *legacy_labels = reopened ? code_source_build_behavior(reopened)
                                             : NULL;
    node *capture = legacy_labels ? first_expression(legacy_labels) : NULL;
    count_label = amount_label = event_label = false;
    for (int i = 0; capture && i < capture->input_count; i++) {
        graph_set_pin_display_name(legacy_labels->graph, capture, false, i, NULL);
        const char *label = code_source_expression_input_label(legacy_labels, capture, i);
        count_label |= !strcmp(label, "count");
        amount_label |= !strcmp(label, "amount");
        event_label |= !strcmp(label, "event.dt");
        test_check(capture->input_pins[i].display_name == NULL, "Readable legacy capture label lookup leaves optional presentation untouched");
    }
    test_check(count_label && amount_label && event_label, "Native capture bindings recover readable labels when old presentation has no display names");
    behavior_asset_free(legacy_labels);
    readable = expression ? code_source_expression(NULL, reopened->asset, expression)
                          : NULL;
    lowered = readable
                ? code_source_resolve_expression(NULL, reopened->asset, expression, readable, error, sizeof error)
                : NULL;
    test_check(
        readable && strstr(readable, "count") && strstr(readable, "amount") &&
            strstr(readable, "event.dt") && !strstr(readable, "_code_input_") && lowered &&
            !strcmp(lowered, behavior_expression_source(reopened->asset, expression)),
        "Reopened graph labels recover Tick payload names from their actual "
        "native capture edges"
    );
    free(lowered);
    free(readable);
    code_source_free(reopened);
    free(formatted);
    code_source_free(source);
    text = "behavior Labels { on Tick(GameProjectile projectile) { "
           "projectile.age += missingValue * event.dt; } }";
    source = code_source_parse(text);
    bool exact = false;
    const char *missing = strstr(text, "missingValue");
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const code_source_diagnostic *diagnostic = &source->diagnostics[i];
        if (strstr(diagnostic->message, "Unresolved source")) {
            exact = diagnostic->start == missing - text &&
                    diagnostic->end - diagnostic->start == (int)strlen("missingValue");
        }
    }
    test_check(exact, "Unresolved reflected expressions diagnose the exact missing token");
    code_source_free(source);
}
static int exec_port(node *n, bool output, const char *name)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    for (int i = 0; i < count; i++) {
        if (pins[i].kind == PIN_EXEC && !strcmp(pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}
static node *golden_expression(behavior_asset *asset, uint64_t variable, const char *text)
{
    behavior_expression_error error;
    node *n = behavior_expression_add(asset, asset->graph, text, &error);
    if (!n) {
        return NULL;
    }
    pin_binding binding = { .kind = PIN_BINDING_VARIABLE, .variable_id = variable };
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind == PIN_DATA &&
            !strcmp(n->input_pins[i].name, "value")) {
            graph_set_pin_type(asset->graph, n, false, i, PIN_I32, "int");
            graph_set_pin_binding(asset->graph, n, false, i, &binding);
        }
    }
    return behavior_expression_refresh(asset, n, &error) ? NULL : n;
}
/* Construct the golden graph without the source frontend. Its three Sequence
 * outcomes must see earlier writes and schedule the final continuation once. */
static behavior_asset *sequence_golden()
{
    behavior_asset *asset = behavior_asset_new("SequenceShared");
    pin_value zero = { .type = PIN_I32 };
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "value",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &zero,
        NULL
    );
    uint64_t id = variable->id;
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, event, "Tick");
    node *sequence = graph_add_node(g, &NODE_Sequence);
    pin_spec third = { .name = "Then2", .type = PIN_FLOW, .kind = PIN_EXEC };
    graph_append_pin(g, sequence, true, &third);
    node *first = golden_expression(asset, id, "value += 1");
    node *condition = golden_expression(asset, id, "value == 1");
    node *yes = golden_expression(asset, id, "value += 10");
    node *no = golden_expression(asset, id, "value += 100");
    node *last = golden_expression(asset, id, "value += 1000");
    if (!first || !condition || !yes || !no || !last) {
        behavior_asset_free(asset);
        return NULL;
    }
    node *branch = graph_add_node(g, &NODE_Branch);
    graph_connect_exec(g, event, 0, sequence, 0, 0);
    graph_connect_exec(g, sequence, 0, first, exec_port(first, false, "Exec"), 0);
    graph_connect_exec(g, sequence, 1, branch, 0, 0);
    graph_connect_exec(g, sequence, 2, last, exec_port(last, false, "Exec"), 0);
    int result = -1;
    for (int i = 0; i < condition->output_count; i++) {
        if (condition->output_pins[i].kind == PIN_DATA) {
            result = i;
        }
    }
    graph_connect_data(g, condition, result, branch, 1);
    graph_connect_exec(g, branch, 0, yes, exec_port(yes, false, "Exec"), 0);
    graph_connect_exec(g, branch, 1, no, exec_port(no, false, "Exec"), 0);
    return asset;
}
static void ordinary_runtime_parity()
{
    const char
        *text = "behavior SequenceShared { state int count = 0; on Tick() { "
                "sequence { branch { count += 1; } branch { "
                "if (count == 1) { count += 10; } else { count += 100; } } } "
                "count += 1000; } }";
    code_source *source = code_source_parse(text);
    behavior_asset *candidate = code_source_compile_behavior(source);
    if (!candidate) {
        diagnostics(source);
    }
    test_check(candidate != NULL, "Ordinary sequence branches compile through the shared native frontend");
    behavior_asset *golden = sequence_golden();
    test_check(golden != NULL, "Independent native sequence fixture constructs without source parsing");
    if (candidate && golden) {
        behavior_instance *compiled = behavior_instance_create(candidate, NULL, NULL, NULL);
        behavior_instance *oracle = behavior_instance_create(golden, NULL, NULL, NULL);
        test_check(compiled && oracle, "Source and independently authored graph publish executable native generations");
        if (compiled && oracle) {
            behavior_instance_set_backend(oracle, BEHAVIOR_ORACLE);
            flow_error error = { 0 };
            behavior_instance_dispatch(compiled, "Tick", (flow_inputs){ 0 }, &error);
            behavior_instance_dispatch(oracle, "Tick", (flow_inputs){ 0 }, &error);
            test_check(
                !behavior_instance_last_failure(compiled) &&
                    !behavior_instance_last_failure(oracle) &&
                    compiled->variables[0].i == 1011 && oracle->variables[0].i == 1011,
                "Source sequence branches share current state and match the "
                "independent graph oracle"
            );
            behavior_instance_dispatch(compiled, "Tick", (flow_inputs){ 0 }, &error);
            behavior_instance_dispatch(oracle, "Tick", (flow_inputs){ 0 }, &error);
            test_check(
                !behavior_instance_last_failure(compiled) &&
                    !behavior_instance_last_failure(oracle) &&
                    compiled->variables[0].i == 2112 && oracle->variables[0].i == 2112,
                "Repeated dispatch takes the other branch while preserving one "
                "final continuation"
            );
        }
        behavior_instance_destroy(compiled);
        behavior_instance_destroy(oracle);
    }
    behavior_asset_free(golden);
    behavior_asset_free(candidate);
    code_source_free(source);
}
static void callable_name_collisions()
{
    code_source *library = code_source_parse(
        "library Shared { pure function Multiply() -> (Result: int) { "
        "result Result: int = 7; } }"
    );
    const behavior_asset *catalog[] = { library->asset };
    code_source_context context = { .libraries = catalog, .library_count = 1 };
    code_source *source = code_source_parse_context(
        "behavior CallableNames { state int native_result = 0; "
        "state int local_result = 0; state int external_result = 0; "
        "pure function Multiply() -> (Result: int) { result Result: int = 9; } "
        "on Tick() { native_result = engine[\"Multiply\"](A: int = 2, B: int = "
        "3).Result; "
        "local_result = Multiply().Result; external_result = "
        "Shared.Multiply().Result; } }",
        NULL,
        &context
    );
    behavior_asset *authored = code_source_compile_behavior(source);
    if (!authored) {
        diagnostics(source);
    }
    char link_error[256] = { 0 };
    behavior_asset *asset = authored
                              ? behavior_asset_link_libraries(authored, catalog, 1, link_error, sizeof link_error)
                              : NULL;
    test_check(asset != NULL, "Qualified external callable source links against its shared project catalogue");
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    if (instance &&
        (behavior_instance_last_failure(instance) || instance->variables[0].i != 6 ||
         instance->variables[1].i != 9 || instance->variables[2].i != 7)) {
        fprintf(
            stderr,
            "Callable collisions native=%d local=%d external=%d failed=%d\n",
            instance->variables[0].i,
            instance->variables[1].i,
            instance->variables[2].i,
            behavior_instance_last_failure(instance) != NULL
        );
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].i == 6 && instance->variables[1].i == 9 &&
            instance->variables[2].i == 7,
        "Explicit native, local authored and qualified external callees with "
        "one spelling retain distinct runtime meanings"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    behavior_asset_free(authored);
    code_source_free(source);
    code_source_free(library);
}
static void direct_predicate_branch()
{
    code_source *source = code_source_parse(
        "behavior DirectPredicate { state int count = 0; "
        "on Tick() { if (count == 0) { count = 3; } else { count = 7; } } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    node *condition = asset ? first_expression(asset) : NULL;
    graph *body = condition
                    ? behavior_asset_find_graph(asset, condition->reference_id)
                    : NULL;
    test_check(
        body && body->expression_mode == GRAPH_EXPRESSION_BRANCH &&
            condition->output_count == 3 && condition->output_pins[0].kind == PIN_EXEC &&
            !strcmp(condition->output_pins[0].name, "True") &&
            condition->output_pins[1].kind == PIN_EXEC &&
            !strcmp(condition->output_pins[1].name, "False") &&
            condition->output_pins[2].kind == PIN_DATA &&
            condition->output_pins[2].type == PIN_BOOL &&
            !strcmp(condition->output_pins[2].name, "Result"),
        "Direct if formulas retain the native branch-expression capability"
    );
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    for (int i = 0; instance && i < 2; i++) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        test_check(
            !behavior_instance_last_failure(instance) &&
                instance->variables[0].i == (i ? 7 : 3),
            "Direct predicate branch evaluates current state and runs its "
            "selected outcome"
        );
    }
    test_check(instance != NULL, "Direct branch expressions publish through the native runtime");
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void nominal_numeric_storage()
{
    code_source *source = code_source_parse(
        "behavior NominalStorage { state velocity: struct<\"GameVelocity\"> = "
        "null; "
        "state input: struct<\"GameInput\"> = {move:{x:2,y:3}}; "
        "value product = multiply(A: struct<\"CF_V2\"> = "
        "get_variable<\"input\">(), B: float = 2.0); "
        "on Tick() { velocity = product; } }"
    );
    behavior_asset *clone = source->asset ? behavior_asset_clone(source->asset) : NULL;
    test_check(clone && behavior_asset_equal(source->asset, clone), "Compatible nominal struct aliases preserve authored types through native snapshot replay");
    behavior_asset_free(clone);
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    node *product = NULL;
    for (int i = 0; asset && i < acount(asset->graph->nodes); i++) {
        node *n = asset->graph->nodes[i];
        if (n->desc == &NODE_Multiply) {
            product = n;
        }
    }
    test_check(
        product && !strcmp(product->input_pins[0].type_name, "CF_V2") &&
            !strcmp(product->output_pins[0].type_name, "CF_V2"),
        "A compatible GameVelocity consumer retains the producer's explicit "
        "CF_V2 schema"
    );
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    float x = 0, y = 0;
    const type_desc *vector = registry_type_by_name("CF_V2");
    if (instance && instance->variables[0].ptr) {
        for (int i = 0; i < vector->field_count; i++) {
            const type_field *field = &vector->fields[i];
            if (!strcmp(field->name, "x")) {
                memcpy(&x, (char *)instance->variables[0].ptr + field->offset, sizeof x);
            }
            if (!strcmp(field->name, "y")) {
                memcpy(&y, (char *)instance->variables[0].ptr + field->offset, sizeof y);
            }
        }
    }
    test_check(instance && !behavior_instance_last_failure(instance) && x == 4 && y == 6, "Typed vector multiplication executes unchanged through its compatible component alias");
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
    asset = behavior_asset_new("UnresolvedInference");
    node *unknown = graph_add_node(asset->graph, &NODE_Add);
    node *integer = graph_add_node(asset->graph, registry_node_by_name("Integer"));
    graph_set_pin_type(asset->graph, unknown, false, 0, PIN_FLOW, NULL);
    test_check(
        !graph_connect_data(asset->graph, integer, 0, unknown, 0) &&
            unknown->input_pins[0].type == PIN_I32 &&
            unknown->output_pins[0].type == PIN_I32,
        "Unresolved native wildcard groups still infer their concrete type "
        "from the driver"
    );
    behavior_asset_free(asset);
}
static void arithmetic_float_boundary()
{
    code_source *source = code_source_parse(
        "behavior ArithmeticBoundary { state float total=0; "
        "on Tick(){total=cast<double>(16777217.0)-cast<double>(16777216.0);} }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    bool double_operation = false;
    bool float_assignment = false;
    for (int i = 0; asset && i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (!strcmp(n->desc->name, "Subtract")) {
                double_operation = n->input_pins[0].type == PIN_F64 &&
                                   n->input_pins[1].type == PIN_F64 &&
                                   n->output_pins[0].type == PIN_F64;
            }
            if (n->desc == &NODE_BehaviorExpression) {
                for (int k = 0; k < n->output_count; k++) {
                    if (n->output_pins[k].kind == PIN_DATA &&
                        !strcmp(n->output_pins[k].name, "Result")) {
                        float_assignment |= n->output_pins[k].type == PIN_F32;
                    }
                }
            }
        }
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].f == 1 && double_operation && float_assignment,
        "Ordinary double arithmetic keeps its native precision until the float "
        "assignment boundary"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void native_expression_result_storage()
{
    behavior_asset *asset = behavior_asset_new("NativeResultStorage");
    behavior_expression_error error;
    node *expression = behavior_expression_add(asset, asset->graph, "max(16777217.0-16777216.0,0)", &error);
    int result = -1;
    for (int i = 0; expression && i < expression->output_count; i++) {
        if (expression->output_pins[i].kind == PIN_DATA &&
            !strcmp(expression->output_pins[i].name, "Result")) {
            result = i;
        }
    }
    bool valid = result >= 0 &&
                 graph_set_pin_type(asset->graph, expression, true, result, PIN_F32, "float") ==
                     0 &&
                 behavior_expression_refresh(asset, expression, &error) == 0;
    behavior_asset *copy = valid ? behavior_asset_clone(asset) : NULL;
    node *reopened = copy ? first_expression(copy) : NULL;
    test_check(reopened && reopened->output_pins[result].type == PIN_F32, "Native expression refresh and snapshot reopen retain an explicit float Result contract");
    behavior_asset_free(copy);
    behavior_asset_free(asset);
}
static void native_numeric_snapshot_storage()
{
    behavior_asset *asset = behavior_asset_new("SnapshotPrecision");
    pin_value zero = { .type = PIN_F64 };
    behavior_variable *precise = behavior_asset_add_variable(
        asset,
        "precise",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        &zero,
        NULL
    );
    uint64_t precise_id = precise->id;
    zero = (pin_value){ .type = PIN_F32 };
    behavior_variable *rounded = behavior_asset_add_variable(
        asset,
        "rounded",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &zero,
        NULL
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, registry_node_by_name("Event"));
    graph_set_event_name(g, event, "Tick");
    node *real = graph_add_node(g, registry_node_by_name("Real"));
    pin_value number = { .type = PIN_F64, .d = 16777217 };
    graph_set_pin_default(g, real, false, 0, &number);
    node *negate = graph_add_node(g, registry_node_by_name("Negate"));
    graph_set_pin_type(g, negate, false, 0, PIN_F64, "double");
    graph_set_pin_type(g, negate, true, 0, PIN_F64, "double");
    graph_connect_data(g, real, 0, negate, 0);
    node *set_precise = graph_add_node(g, registry_node_by_name("Set Variable"));
    graph_set_node_reference(g, set_precise, precise_id, "precise");
    node *set_rounded = graph_add_node(g, registry_node_by_name("Set Variable"));
    graph_set_node_reference(g, set_rounded, rounded->id, "rounded");
    graph_set_pin_type(g, set_precise, false, 1, PIN_F64, "double");
    graph_set_pin_type(g, set_precise, true, 1, PIN_F64, "double");
    graph_set_pin_type(g, set_rounded, false, 1, PIN_F32, "float");
    graph_set_pin_type(g, set_rounded, true, 1, PIN_F32, "float");
    graph_connect_data(g, negate, 0, set_precise, 1);
    unsigned flags = negate->output_pins[0].flags;
    negate->output_pins[0].flags &= ~PIN_FLAG_WILDCARD;
    graph_connect_data(g, negate, 0, set_rounded, 1);
    negate->output_pins[0].flags = flags;
    graph_connect_exec(g, event, 0, set_precise, 0, 0);
    graph_connect_exec(g, set_precise, 0, set_rounded, 0, 0);
    behavior_asset *copy = behavior_asset_clone(asset);
    node *restored = copy ? graph_node_by_id(copy->graph, negate->id) : NULL;
    behavior_instance *instance = copy ? behavior_instance_create(copy, NULL, NULL, NULL)
                                       : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        restored && restored->input_pins[0].type == PIN_F64 &&
            restored->output_pins[0].type == PIN_F64 && instance &&
            !behavior_instance_last_failure(instance) &&
            instance->variables[0].d == -16777217 &&
            instance->variables[1].f == -16777216,
        "Native snapshots preserve a shared double operation and round only "
        "its float consumer"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(copy);
    behavior_asset_free(asset);
}
static void inline_expression_storage()
{
    code_source *source = code_source_parse(
        "behavior InlineStorage { state float contextual=0; state float "
        "chosen=0; "
        "on "
        "Tick(){set_variable<\"contextual\">(Value:float=16777217.0-16777216.0)"
        ";"
        "set_variable<\"chosen\">(Value:float=cast<double>(16777217.0)-cast<"
        "double>(16777216.0));} }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].f == 0 && instance->variables[1].f == 1,
        "Inline numeric context preserves float arithmetic while explicit "
        "double casts retain precision until consumption"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void positional_native_arguments()
{
    code_source *source = code_source_parse(
        "behavior Positional { state int[] first=[1,2]; state int[] "
        "second=[3,4]; "
        "state int[] joined=[]; value "
        "combined=append(&state.first,&state.second); "
        "on Tick(){joined=combined;} }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    const pin_array *array = instance ? instance->variables[2].ptr : NULL;
    test_check(
        instance && !behavior_instance_last_failure(instance) && array &&
            array->count == 4 && array->items[0].i == 1 && array->items[1].i == 2 &&
            array->items[2].i == 3 && array->items[3].i == 4,
        "Positional calls distinguish duplicate native parameter names and "
        "retain ordered values"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void computed_value_qualifiers()
{
    code_source *source = code_source_parse(
        "behavior Qualified { state total:float=1; "
        "value writable:ref float=get_variable<\"total\">(); "
        "on Tick(){writable += 2;} }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].f == 3,
        "Matching computed reference qualifiers retain the writable native "
        "capability"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
    code_source *library = code_source_parse(
        "library Traits { pure function Read() -> (Result:ref float=1.0) {} }"
    );
    graph *function = library->asset
                        ? behavior_asset_find_graph_by_name(library->asset, "Read")
                        : NULL;
    node *returned = function ? graph_node_by_id(function, function->return_id) : NULL;
    if (returned) {
        returned->input_pins[0].flags |= PIN_FLAG_WATCH | PIN_FLAG_COLOR |
                                         PIN_FLAG_ADVANCED;
    }
    const behavior_asset *catalog[] = { library->asset };
    code_source_context context = { .libraries = catalog, .library_count = 1 };
    source = code_source_parse_context(
        "behavior TraitsConsumer { value borrowed:ref float=Traits.Read(); }",
        NULL,
        &context
    );
    test_check(returned && source->asset && !acount(source->diagnostics), "Debug and presentation flags do not invalidate matching executable qualifiers");
    code_source_free(source);
    code_source_free(library);
    const char *invalid[] = {
        ("behavior Plain { state total:float=1; value copy:plain "
         "float=get_variable<\"total\">(); }"),
        "behavior Reference { value writable:ref double=real(Value:1.0); }",
        "behavior Service { value resource:service double=real(Value:1.0); }"
    };
    for (int i = 0; i < 3; i++) {
        source = code_source_parse(invalid[i]);
        asset = code_source_compile_behavior(source);
        bool rejected = false;
        for (int j = 0; j < acount(source->diagnostics); j++) {
            rejected |= strstr(source->diagnostics[j].message, "declared qualifiers") !=
                        NULL;
        }
        test_check(!asset && rejected, "Computed qualifier mismatch is diagnosed without mutating the native producer ABI");
        behavior_asset_free(asset);
        code_source_free(source);
    }
}
static void system_state_invocations()
{
    code_source *source = code_source_parse(
        "system QueryState { owner \"ecs.entity\"; query "
        "has(\"GAME.GameTransform\"); "
        "state elapsed: float = 10; on Tick(GameTransform transform) { "
        "elapsed += event.dt; transform.x = elapsed; } }"
    );
    if (!source->asset) {
        diagnostics(source);
    }
    behavior_asset *asset = source->asset ? behavior_asset_clone(source->asset) : NULL;
    compile_report report;
    compile_report_init(&report);
    if (asset) {
        behavior_asset_compile_for_system(asset, &report);
    }
    bool warned = false;
    for (int i = 0; i < acount(report.items); i++) {
        warned |= report.items[i].severity == CMPL_WARNING;
    }
    test_check(asset && !compile_report_blocked(&report) && !warned, "System persistent state compiles without the obsolete scratch-lifetime warning");
    compile_report_free(&report);
    ecs_world *world = ecs_world_new();
    ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL);
    ecs_entity entity = ecs_world_create_entity(world);
    GAME_GameTransform_add(world, entity, 0);
    ecs_world_apply_commands(world);
    game_system *system = asset ? game_system_new(world, asset) : NULL;
    flow_error error = { 0 };
    int first = system ? game_system_tick_mode(system, 0, .25, &error, GAME_SYSTEM_EXEC_AUTO)
                       : -1;
    bool first_value = first == FLOW_OK &&
                       GAME_GameTransform_get_const(world, entity)->x == 10.25f;
    int second = system ? game_system_tick_mode(system, 1, .5, &error, GAME_SYSTEM_EXEC_AUTO)
                        : -1;
    if (!first_value || second != FLOW_OK ||
        GAME_GameTransform_get_const(world, entity)->x != 10.75f) {
        fprintf(
            stderr,
            "System state: first=%d second=%d x=%g error=%s\n",
            first,
            second,
            (double)GAME_GameTransform_get_const(world, entity)->x,
            error.message ? error.message : "none"
        );
    }
    test_check(
        first_value && second == FLOW_OK &&
            GAME_GameTransform_get_const(world, entity)->x == 10.75f,
        "Query invocations retain state and accumulate Tick delta "
        "across successive ticks"
    );
    game_system_free(system);
    if (!system) {
        behavior_asset_free(asset);
    }
    ecs_world_free(world);
    code_source_free(source);
}
static void projected_call_assignment()
{
    code_source *source = code_source_parse(
        "behavior ProjectedCall { state position: struct<\"CF_V2\"> = "
        "{x:2,y:3}; "
        "on Tick() { get_variable<\"position\">().Value.x += 1; } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    float x = 0;
    const type_desc *vector = registry_type_by_name("CF_V2");
    if (instance && instance->variables[0].ptr) {
        for (int i = 0; i < vector->field_count; i++) {
            if (!strcmp(vector->fields[i].name, "x")) {
                memcpy(
                    &x,
                    (char *)instance->variables[0].ptr + vector->fields[i].offset,
                    sizeof x
                );
            }
        }
    }
    test_check(instance && !behavior_instance_last_failure(instance) && x == 3, "Assignments beginning with a projected native call mutate the referenced aggregate field");
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void named_numeric_casts()
{
    code_source *source = code_source_parse(
        "behavior NamedCasts { state untyped_result: double = 0; "
        "state typed_result: double = 0; "
        "value untyped = cast<float>(real(Value: 16777217.0)); "
        "value typed: float = cast<float>(real(Value: 16777217.0)); "
        "on Tick() { untyped_result = untyped + 0; "
        "typed_result = typed + 0; } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    int aliases = 0;
    for (int i = 0; asset && i < acount(asset->graph->nodes); i++) {
        node *n = asset->graph->nodes[i];
        if (n->desc == &NODE_BehaviorExpression && n->output_count == 1 &&
            n->output_pins[0].kind == PIN_DATA && n->output_pins[0].type == PIN_F32) {
            aliases++;
        }
    }
    test_check(aliases >= 2, "Typed and untyped named casts retain native float result storage");
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].type == PIN_F64 &&
            instance->variables[0].d == 16777216 &&
            instance->variables[1].type == PIN_F64 && instance->variables[1].d == 16777216,
        "Later arithmetic consumes the rounded float values of both named cast "
        "aliases"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void inline_numeric_casts()
{
    code_source *source = code_source_parse(
        "behavior InlineCasts { state sample:double=16777217; "
        "state direct:double=0; state nested:double=0; state "
        "composed:double=0; "
        "state named:double=0; state returned:double=0; "
        "pure function Named(Sample:double)->(Result:double){"
        "result Result:double=cast<float>(Sample);} "
        "function Returned(Sample:double)->(Result:double){"
        "after entry{return cast<float>(entry.Sample);}} "
        "action returned_value=Returned(Sample:&state.sample); "
        "on "
        "Tick(){set_variable<\"direct\">(Value:double=cast<float>(&state."
        "sample));"
        "set_variable<\"nested\">(Value:double=cast<double>(cast<float>(&state."
        "sample)));"
        "set_variable<\"composed\">(Value:double=cast<float>(&state.sample)+0."
        "0);named=Named(Sample:&state.sample);await returned_value();"
        "returned=returned_value.Result;} }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    char message[192];
    char *printed = asset ? code_print_behavior(asset, message, sizeof message) : NULL;
    if (printed) {
        char *write = printed;
        for (const char *line = printed; *line;) {
            const char *end = strchr(line, '\n');
            size_t length = end ? (size_t)(end - line + 1) : strlen(line);
            const char *content = line;
            while (*content == ' ') {
                content++;
            }
            if (strncmp(content, "// @node", 8)) {
                memmove(write, line, length);
                write += length;
            }
            line += length;
        }
        *write = 0;
    }
    code_source *reopened = printed ? code_source_parse(printed) : NULL;
    behavior_asset *roundtrip = reopened ? code_source_compile_behavior(reopened)
                                         : NULL;
    if (reopened && !roundtrip) {
        diagnostics(reopened);
    }
    behavior_asset *models[] = { asset, roundtrip };
    for (int i = 0; i < 2; i++) {
        behavior_instance *instance = models[i]
                                        ? behavior_instance_create(models[i], NULL, NULL, NULL)
                                        : NULL;
        flow_error error = { 0 };
        if (instance) {
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        }
        test_check(
            instance && !behavior_instance_last_failure(instance) &&
                instance->variables[1].d == 16777216 &&
                instance->variables[2].d == 16777216 &&
                instance->variables[3].d == 16777216 &&
                instance->variables[4].d == 16777216 &&
                instance->variables[5].d == 16777216,
            "Inline, nested, named-result and return-expression float casts "
            "round before double consumers in "
            "original and metadata-free source"
        );
        behavior_instance_destroy(instance);
    }
    behavior_asset_free(roundtrip);
    code_source_free(reopened);
    free(printed);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void lazy_predicate_value()
{
    code_source *source = code_source_parse(
        "behavior LazyPredicate { state int selected = 1; "
        "state bool observed = false; state int count = 0; "
        "value predicate = (selected > 0); "
        "on Tick() { observed = predicate.Result; "
        "if (predicate.Result) { count += 1; } } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    node *predicate = asset ? first_expression(asset) : NULL;
    graph *body = predicate
                    ? behavior_asset_find_graph(asset, predicate->reference_id)
                    : NULL;
    bool value_interface = body && body->expression_mode == GRAPH_EXPRESSION_VALUE;
    for (int side = 0; predicate && side < 2; side++) {
        const pin_spec *pins = side ? predicate->output_pins : predicate->input_pins;
        int count = side ? predicate->output_count : predicate->input_count;
        for (int i = 0; i < count; i++) {
            value_interface &= pins[i].kind != PIN_EXEC;
        }
    }
    test_check(value_interface, "Shared lazy predicates retain a native value interface when consumed by if");
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[1].b && instance->variables[2].i == 1,
        "One lazy predicate supplies both its boolean value and conditional "
        "execution"
    );
    if (instance) {
        instance->variables[0].i = 0;
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            !instance->variables[1].b && instance->variables[2].i == 1,
        "Lazy predicates reevaluate current state on the next native dispatch"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void function_return_reuse()
{
    const char *bodies[] = {
        "result Result: int = 7; after entry { if (entry.Choice) { return(); } "
        "else { return(); } }",
        "after entry { if (entry.Choice) { return(Result: 7); } else { "
        "return(Result: 9); } }",
        "result Result: int = 7; after entry { if (entry.Choice) { "
        "return(Result: "
        "9); } else { return(); } }",
        "result Result: int = 7; after entry { if (entry.Choice) { return(); } "
        "else { return(Result: 9); } }",
    };
    for (int scenario = 0; scenario < 4; scenario++) {
        char text[1024];
        snprintf(
            text,
            sizeof text,
            "behavior ReturnCases { state choice: bool = true; state count: "
            "int = 0; "
            "function Read(Choice: bool) -> (Result: int) { %s } "
            "action read = Read(Choice: choice); "
            "on Tick() { await read(); count = read.Result; } }",
            bodies[scenario]
        );
        code_source *source = code_source_parse(text);
        behavior_asset *asset = code_source_compile_behavior(source);
        if (!asset) {
            diagnostics(source);
        }
        int returns = 0;
        for (int i = 0; asset && i < acount(asset->graphs); i++) {
            graph *g = asset->graphs[i];
            if (strcmp(g->name, "Read")) {
                continue;
            }
            for (int j = 0; j < acount(g->nodes); j++) {
                returns += g->nodes[j]->desc == &NODE_Return;
            }
        }
        test_check(returns == (scenario ? 2 : 1), "Signature returns share configured results while distinct explicit return values retain separate nodes");
        behavior_instance *instance = asset
                                        ? behavior_instance_create(asset, NULL, NULL, NULL)
                                        : NULL;
        flow_error error = { 0 };
        if (instance) {
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        }
        bool first = instance && !behavior_instance_last_failure(instance) &&
                     instance->variables[1].i == (scenario == 2 ? 9 : 7);
        if (instance) {
            instance->variables[0].b = false;
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        }
        test_check(
            first && !behavior_instance_last_failure(instance) &&
                instance->variables[1].i == (scenario == 1 || scenario == 3 ? 9 : 7),
            "Both function branches execute the selected native return result"
        );
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
        code_source_free(source);
    }
}
static void converted_local_snapshot()
{
    code_source *source = code_source_parse(
        "behavior ConvertedLocal { state count: double = 0; "
        "value producer = integer(Value: 7); local current = producer.Value; "
        "value remaining = (clamp(convert<\"To Real\">(&local.current), 0, "
        "10)); "
        "on Tick() { count = remaining; } }"
    );
    behavior_asset *asset = source->asset ? behavior_asset_clone(source->asset) : NULL;
    test_check(asset && behavior_asset_equal(source->asset, asset), "Native snapshots replay validated conversions into concrete wildcard expression captures");
    behavior_asset_free(asset);
    asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].d == 7,
        "Converted local expression captures retain their producer and numeric "
        "value after publication"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void forward_local_dependencies()
{
    code_source *source = code_source_parse(
        "behavior ForwardLocals { state count: int = 0; "
        "value producer = integer(Value: 7); "
        "local derived = &local.later + 1; local later = producer.Value; "
        "on Tick() { count = derived; } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].i == 8,
        "Forward local dependencies resolve their actual producer before "
        "native execution"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
    source = code_source_parse(
        "behavior CyclicLocals { local first = &local.second + 1; "
        "local second = &local.first + 1; }"
    );
    bool cycle = false;
    for (int i = 0; i < acount(source->diagnostics); i++) {
        cycle |= strstr(source->diagnostics[i].message, "Cyclic local producer dependency") !=
                 NULL;
    }
    test_check(!source->asset && cycle, "Cyclic local producers fail with a source diagnostic before publication");
    code_source_free(source);
}
static void local_function_return()
{
    code_source *source = code_source_parse(
        "behavior LocalReturn { state int count = 0; "
        "pure function Read() -> (Result: int) { value number = integer(Value: "
        "7); "
        "local named = number.Value; result Result: int = named; } "
        "on Tick() { count = Read().Result; } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].i == 7,
        "Pure function named-local return retains its producer data edge and "
        "executes its value"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void shared_action_execution()
{
    const char *texts[] = {
        "behavior SharedGate { state int count = 0; "
        "action latch = gate(\"Start Closed\": bool = true); "
        "after latch.Exit { count += 10; } "
        "on Tick() { latch.close(); count += 1; latch.open(); latch.enter(); "
        "count += 100; } }",
        "behavior SharedChoice { state int count = 0; action chooser = "
        "flip_flop(); "
        "after chooser.A { count += 1; } after chooser.B { count += 10; } "
        "on Tick() { chooser(); count += 100; } }",
        "behavior NestedGate { state int count = 0; action latch = gate(); "
        "on Tick() { if (true) { latch.close(); } count += 1; "
        "match (0) { case 0 { latch.open(); } } count += 10; } }"
    };
    const int first[] = { 111, 101, 11 }, second[] = { 222, 211, 22 };
    for (int i = 0; i < 3; i++) {
        code_source *source = code_source_parse(texts[i]);
        behavior_asset *asset = code_source_compile_behavior(source);
        if (!asset) {
            diagnostics(source);
        }
        test_check(asset != NULL, "Shared action invocations keep separately handled native outcomes");
        behavior_instance *instance = asset
                                        ? behavior_instance_create(asset, NULL, NULL, NULL)
                                        : NULL;
        if (instance) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            test_check(
                !behavior_instance_last_failure(instance) &&
                    instance->variables[0].i == first[i],
                "Shared method invocation continues after non-emitting entries "
                "and dispatched outcomes"
            );
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            test_check(
                !behavior_instance_last_failure(instance) &&
                    instance->variables[0].i == second[i],
                "Repeated shared action calls preserve one stateful native "
                "instance"
            );
        }
        test_check(instance != NULL, "Shared invocation sequence publishes through the existing native runtime");
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
        code_source_free(source);
    }
    code_source *source = code_source_parse(
        "behavior AwaitDelay { state int count = 0; on Tick() { "
        "await delay(Duration: double = 1); count += 1; } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    test_check(instance != NULL, "Explicit await selects a native conventional completion");
    if (instance) {
        flow_error error = { 0 };
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        test_check(instance->variables[0].i == 0, "Await keeps the continuation suspended until native delay completion");
        behavior_instance_tick(instance, 2);
        test_check(
            !behavior_instance_last_failure(instance) && instance->variables[0].i == 1,
            "Await resumes its continuation once after native delay completion"
        );
    }
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
    source = code_source_parse("behavior AwaitAmbiguous { action chooser = flip_flop(); on Tick() { await chooser.exec(); } }");
    bool diagnosed = false;
    for (int i = 0; i < acount(source->diagnostics); i++) {
        diagnosed |= strstr(source->diagnostics[i].message, "conventional Then or Completed") !=
                     NULL;
    }
    test_check(!source->asset && diagnosed, "Await rejects an action without a unique conventional completion");
    code_source_free(source);
}
static void shared_assignment_storage()
{
    code_source *source = code_source_parse(
        "behavior SharedAssignment { state float elapsed=16777216; state "
        "double observed=0; "
        "action advance=behavior_expression<\"elapsed += "
        "dt\">(elapsed:float=&state.elapsed,dt:double=3.0);"
        "after advance { observed=advance.Result+advance.Result; } "
        "on Begin(){observed=advance.Result;} on Tick(){advance();} }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    node *action = asset ? first_expression(asset) : NULL;
    bool float_result = false;
    for (int i = 0; action && i < action->output_count; i++) {
        if (action->output_pins[i].kind == PIN_DATA &&
            !strcmp(action->output_pins[i].name, "Result")) {
            float_result = action->output_pins[i].type == PIN_F32;
        }
    }
    for (behavior_backend backend = BEHAVIOR_SLJIT; backend <= BEHAVIOR_ORACLE;
         backend++) {
        behavior_instance *instance = asset
                                        ? behavior_instance_create(asset, NULL, NULL, NULL)
                                        : NULL;
        if (instance && behavior_instance_set_backend(instance, backend)) {
            behavior_instance_destroy(instance);
            continue;
        }
        flow_error error = { 0 };
        if (instance) {
            behavior_instance_dispatch(instance, "Begin", (flow_inputs){ 0 }, &error);
        }
        test_check(
            instance && !behavior_instance_last_failure(instance) &&
                instance->variables[0].f == 16777216 && instance->variables[1].d == 0,
            "Reading an unpublished action result retains its default without "
            "executing it"
        );
        if (instance) {
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        }
        test_check(
            float_result && instance && !behavior_instance_last_failure(instance) &&
                instance->variables[0].f == 16777220 &&
                instance->variables[1].d == 33554440,
            "Shared native assignments retain float Result storage and expose "
            "the "
            "rounded written value without executing again"
        );
        behavior_instance_destroy(instance);
    }
    behavior_asset_free(asset);
    code_source_free(source);
}
static void typed_statement_arguments()
{
    code_source *source = code_source_parse(
        "behavior TypedStatements { state float count = 0; on Tick() { "
        "set_variable<\"count\">(Value: float = 3); } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    test_check(asset != NULL, "Typed named statement arguments parse their shared type grammar before the source value");
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    if (instance) {
        flow_error error = { 0 };
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        test_check(
            !behavior_instance_last_failure(instance) && instance->variables[0].f == 3,
            "Typed generic statement invocation retains the native variable "
            "write semantics"
        );
    }
    test_check(instance != NULL, "Typed statement source publishes through the existing runtime compiler");
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void host_event_alias()
{
    code_source *source = code_source_parse("behavior ContactPayload { on ContactBegin() { destroy(event.other); } }");
    if (!source->asset) {
        diagnostics(source);
    }
    test_check(source->asset != NULL, "Ergonomic host event names infer their native payload signature");
    node *event = NULL;
    for (int i = 0; source->asset && i < acount(source->asset->graph->nodes); i++) {
        node *n = source->asset->graph->nodes[i];
        if (n->desc == &NODE_Event) {
            event = n;
        }
    }
    bool payload = false;
    for (int i = 0; event && i < event->output_count; i++) {
        const pin_spec *pin = &event->output_pins[i];
        payload |= !strcmp(pin->name, "Other") && pin->type == PIN_HANDLE &&
                   pin->type_name && !strcmp(pin->type_name, "ecs.entity");
    }
    test_check(
        event && event->reference_name &&
            !strcmp(event->reference_name, "Contact Begin") && payload,
        "Host event alias retains the canonical runtime dispatch name and "
        "typed entity payload"
    );
    code_source_free(source);
}
static void aggregate_expression_boundaries()
{
    code_source *source = code_source_parse(
        "behavior Projection { state position: struct<\"CF_V2\"> = {x:2,y:3}; "
        "state float total=0; value get_variable=get_variable<\"position\">(); "
        "value x:float=get_variable.Value.x; local position_read=get_variable; "
        "on Tick(){ "
        "total = get_variable.Value.x + get_variable.Value.y; total += x; "
        "total += get_variable<\"position\">().Value.x; "
        "total += &state.position.x; total += &local.position_read.y; } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    test_check(asset != NULL, "Aggregate expression captures and explicit scalar field aliases compile independently");
    int aggregate_inputs = 0;
    for (int i = 0; asset && i < acount(asset->graph->nodes); i++) {
        const node *n = asset->graph->nodes[i];
        if (n->desc != &NODE_BehaviorExpression) {
            continue;
        }
        for (int j = 0; j < n->input_count; j++) {
            aggregate_inputs += n->input_pins[j].kind == PIN_DATA &&
                                n->input_pins[j].type == PIN_STRUCT;
        }
    }
    test_check(aggregate_inputs == 4, "Declared and inline aggregate producers retain member reads inside the expression graph");
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    if (instance) {
        flow_error error = { 0 };
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        test_check(
            !behavior_instance_last_failure(instance) && instance->variables[1].f == 14,
            "Aggregate and explicit scalar capture boundaries preserve native "
            "runtime field values"
        );
    }
    test_check(instance != NULL, "Aggregate capture source publishes through the native compiler");
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void entry_aggregate_expression()
{
    code_source *source = code_source_parse(
        "behavior EntryProjection { state double total=0; "
        "pure function Basis(Move:struct<\"CF_V2\">, "
        "Aim:struct<\"CF_V2\">)->(Result:double){"
        "result "
        "Result:double=entry.Move.y*entry.Aim.x-entry.Move.x*entry.Aim.y;}"
        "on Tick(){total=Basis(Move:{x:2,y:3},Aim:{x:5,y:7});} }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    int aggregate_inputs = 0;
    for (int i = 0; asset && i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (n->desc != &NODE_BehaviorExpression) {
                continue;
            }
            for (int k = 0; k < n->input_count; k++) {
                aggregate_inputs += n->input_pins[k].kind == PIN_DATA &&
                                    n->input_pins[k].type == PIN_STRUCT;
            }
        }
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            aggregate_inputs == 2 && instance->variables[0].d == 1,
        "Repeated entry field projections share two aggregate captures and "
        "retain vector arithmetic values"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}
static void variadic_generic_spare()
{
    code_source *source = code_source_parse(
        "behavior FormatSpare { state title: string = \"\"; "
        "on Run { title = format_text(Format: "
        "\"{:.0f}\", "
        "Arg0: float = 7.0, Arg1: any = null); } }"
    );
    behavior_asset *asset = code_source_build_behavior(source);
    test_check(asset != NULL, "Readable variadic calls retain their unresolved spare argument");
    if (!asset) {
        diagnostics(source);
        code_source_free(source);
        return;
    }
    node *entry = NULL;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        node *n = asset->graph->nodes[i];
        if (n->desc == &NODE_Event) {
            entry = n;
        }
    }
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    const behavior_graph_program
        *body = behavior_program_graph(asset->program, asset->graph);
    test_check(
        instance && entry && behavior_program_function(body, entry),
        "A variadic host call with a Generic spare compiles as a whole event"
    );
    behavior_asset *oracle_asset = behavior_asset_clone(asset);
    behavior_instance *oracle = behavior_instance_create(oracle_asset, NULL, NULL, NULL);
    test_check(instance && oracle, "Formatting source publishes independent compiled and oracle instances");
    if (instance && oracle) {
        behavior_instance_set_backend(oracle, BEHAVIOR_ORACLE);
        for (int i = 0; i < 2; i++) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
            behavior_instance_dispatch(oracle, "Run", (flow_inputs){ 0 }, &error);
            pin_value compiled, interpreted;
            test_check(
                !behavior_instance_last_failure(instance) &&
                    !behavior_instance_last_failure(oracle) &&
                    behavior_instance_get_variable(instance, asset->variables[0].id, &compiled) ==
                        0 &&
                    behavior_instance_get_variable(
                        oracle,
                        oracle_asset->variables[0].id,
                        &interpreted
                    ) == 0 &&
                    compiled.type == PIN_STR && interpreted.type == PIN_STR &&
                    !strcmp(compiled.str, "7") &&
                    !strcmp(compiled.str, interpreted.str),
                "Whole-event formatting retains its owned string across "
                "activations and agrees with the oracle"
            );
        }
    }
    behavior_instance_destroy(oracle);
    behavior_asset_free(oracle_asset);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
}

static void concrete_capture_copies()
{
    code_source *source = code_source_parse(
        "behavior Copies { state huge:uint64=9007199254740993; state "
        "copied:uint64=0;"
        "state source_byte:byte=255; state copied_byte:byte=0;"
        "state source_handle:handle<\"ecs.entity\">=18446744073709551615;"
        "state copied_handle:handle<\"ecs.entity\">=0;"
        "state source_name:name=\"token\"; state copied_name:name=\"\";"
        "on Run "
        "{copied=huge;copied_byte=source_byte;copied_handle=source_handle;"
        "copied_name=source_name;} }"
    );
    behavior_asset *original = code_source_compile_behavior(source);
    if (!original) {
        diagnostics(source);
    }
    char *text = original ? code_print_behavior(original, NULL, 0) : NULL;
    code_source *reopened = text ? code_source_parse(text) : NULL;
    behavior_asset *copy = reopened ? code_source_compile_behavior(reopened) : NULL;
    test_check(original && copy, "Concrete wildcard captures compile and reopen without numeric ABI substitution");
    for (int version = 0; version < 2; version++) {
        behavior_asset *asset = version ? copy : original;
        for (behavior_backend backend = BEHAVIOR_SLJIT;
             asset && backend <= BEHAVIOR_ORACLE;
             backend++) {
            behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
            if (instance && behavior_instance_set_backend(instance, backend)) {
                behavior_instance_destroy(instance);
                continue;
            }
            flow_error error = { 0 };
            if (instance) {
                behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
            }
            test_check(
                instance && !behavior_instance_last_failure(instance) &&
                    instance->variables[1].type == PIN_U64 &&
                    instance->variables[1].u64 == UINT64_C(9007199254740993) &&
                    instance->variables[3].type == PIN_U8 &&
                    instance->variables[3].u8 == 255 &&
                    instance->variables[5].type == PIN_HANDLE &&
                    instance->variables[5].u64 == UINT64_MAX &&
                    instance->variables[7].type == PIN_NAME &&
                    instance->variables[7].str &&
                    !strcmp(instance->variables[7].str, "token"),
                "Plain assignments preserve large unsigned, byte, handle, and "
                "name values across backends and bare reopen"
            );
            behavior_instance_destroy(instance);
        }
    }
    behavior_asset_free(copy);
    code_source_free(reopened);
    free(text);
    behavior_asset_free(original);
    code_source_free(source);
    code_source *invalid = code_source_parse("behavior Invalid { state text_value:string=\"text\"; state number:double=0; on Run {number=text_value*2;} }");
    behavior_asset *bad = code_source_compile_behavior(invalid);
    test_check(!bad && acount(invalid->diagnostics) > 0, "Preserving concrete captures does not permit strings as arithmetic operands");
    behavior_asset_free(bad);
    code_source_free(invalid);
}

static void native_arithmetic_operand_validation()
{
    behavior_asset *asset = behavior_asset_new("ArithmeticOperands");
    behavior_expression_error error = { 0 };
    node *expression = behavior_expression_add(asset, asset->graph, "x * 2", &error);
    node *string = graph_add_node(asset->graph, registry_node_by_name("String"));
    bool connected = expression && string &&
                     graph_connect_data(asset->graph, string, 0, expression, 0) == 0;
    char *before = connected ? behavior_asset_save(asset) : NULL;
    int result = connected ? behavior_expression_refresh(asset, expression, &error)
                           : 0;
    char *after = connected ? behavior_asset_save(asset) : NULL;
    test_check(
        connected && result != 0 && strstr(error.message, "numeric operand") &&
            before && after && !strcmp(before, after),
        "Native arithmetic refuses string operands without changing its "
        "authored candidate"
    );
    free(before);
    free(after);
    behavior_asset_free(asset);
    code_source *source = code_source_parse("behavior InvalidCompound { state text_value:string=\"text\"; on Run {text_value += 2;} }");
    behavior_asset *invalid = code_source_compile_behavior(source);
    bool diagnosed = false;
    for (int i = 0; i < acount(source->diagnostics); i++) {
        diagnosed |= strstr(source->diagnostics[i].message, "numeric operands") != NULL;
    }
    if (!diagnosed) {
        diagnostics(source);
    }
    test_check(!invalid && diagnosed, "Compound arithmetic rejects string targets with an operand diagnostic");
    behavior_asset_free(invalid);
    code_source_free(source);
}

static void awaited_result_locals()
{
    code_source *source = code_source_parse(
        "behavior CapturedResult { state calls: int = 0; state total: int = 0; "
        "function Next() -> (Result: int) { after entry { calls += 1; "
        "return(Result: calls); } } "
        "on Run { first := await Next(); calls += 10; total = first; } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        diagnostics(source);
    }
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    if (instance) {
        flow_error error = { 0 };
        behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].i == 11 && instance->variables[1].i == 1,
        "Awaited inferred local captures the completed result once before "
        "later state changes"
    );
    char message[256];
    char *printed = asset ? code_print_behavior(asset, message, sizeof message) : NULL;
    test_check(printed && strstr(printed, ":= await"), "Canonical source keeps awaited result capture in the event body");
    code_source *roundtrip = printed ? code_source_parse(printed) : NULL;
    behavior_asset *rebuilt = roundtrip ? code_source_compile_behavior(roundtrip)
                                        : NULL;
    test_check(rebuilt != NULL, "Printed awaited local result source recompiles");
    behavior_asset_free(rebuilt);
    code_source_free(roundtrip);
    free(printed);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(source);
    source = code_source_parse(
        "behavior IndependentCaptures { state calls: int = 0; state total: int "
        "= 0; "
        "function next() -> (Result: int) { after entry { calls += 1; "
        "return(Result: calls); } } "
        "on Run { first := await next(); second := await next(); total = first "
        "+ second; } }"
    );
    asset = code_source_compile_behavior(source);
    printed = asset ? code_print_behavior(asset, message, sizeof message) : NULL;
    roundtrip = printed ? code_source_parse(printed) : NULL;
    rebuilt = roundtrip ? code_source_compile_behavior(roundtrip) : NULL;
    instance = rebuilt ? behavior_instance_create(rebuilt, NULL, NULL, NULL) : NULL;
    if (instance) {
        flow_error error = { 0 };
        behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].i == 2 && instance->variables[1].i == 3,
        "Fresh awaited invocations retain independent results after canonical "
        "roundtrip"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(rebuilt);
    code_source_free(roundtrip);
    free(printed);
    behavior_asset_free(asset);
    code_source_free(source);
    const char *invalid[] = {
        "behavior OutOfScope { state total: int = 0; function Read() -> "
        "(Result: int) { result Result: int = 7; after entry { return(); } } "
        "on "
        "Run { if (true) { current := await Read(); } total = current; } }",
        "behavior BeforeDeclaration { state total: int = 0; function Read() -> "
        "(Result: int) { result Result: int = 7; after entry { return(); } } "
        "on "
        "Run { total = current; current := await Read(); } }",
        "behavior SharedCapture { state calls: int = 0; state total: int = 0; "
        "function Next() -> (Result: int) { after entry { calls += 1; "
        "return(Result: calls); } } "
        "action read = Next(); on Run { first := await read(); second := await "
        "read(); total = first; } }",
        "behavior BuiltinCapture { function Read() -> (Result: int) { result "
        "Result: int = 7; after entry { return(); } } "
        "on Run { self := await Read(); } }",
        "behavior LocalCapture { value producer = integer(Value: 7); local "
        "current = producer.Value; "
        "function Read() -> (Result: int) { result Result: int = 7; after "
        "entry { "
        "return(); } } "
        "on Run { current := await Read(); } }",
        "behavior ParameterCapture { function Read() -> (Result: int) { result "
        "Result: int = 7; after entry { return(); } } "
        "function Wrap(Sample: int) -> (Result: int) { after entry { Sample := "
        "await Read(); return(Result: Sample); } } }",
        "behavior SkippedCapture { state total: int = 0; function Read() -> "
        "(Result: int) { "
        "result Result: int = 7; after entry { return(); } } on Run { goto "
        "After; "
        "current := await Read(); After: total = current; } }"
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        source = code_source_parse(invalid[i]);
        asset = code_source_compile_behavior(source);
        test_check(!asset && acount(source->diagnostics), "Block local refuses escaped lifetime, shared producer capture and conflicting names");
        if (i >= 2) {
            bool diagnosed = false;
            for (int j = 0; j < acount(source->diagnostics); j++) {
                diagnosed |= strstr(
                                 source->diagnostics[j].message,
                                 i == 2   ? "shared action result"
                                 : i == 6 ? "bypass initialization"
                                          : "shadowed local declaration"
                             ) != NULL;
            }
            test_check(diagnosed, "Unsafe capture diagnoses its shared producer, name collision or bypassed initialization directly");
        }
        behavior_asset_free(asset);
        code_source_free(source);
    }
}

void run_code_source_checks()
{
    awaited_result_locals();
    examples();
    incremental();
    repeated_statement_persistence();
    failed_edits();
    execution();
    expression_labels();
    ordinary_runtime_parity();
    host_event_alias();
    typed_statement_arguments();
    shared_action_execution();
    shared_assignment_storage();
    local_function_return();
    lazy_predicate_value();
    direct_predicate_branch();
    callable_name_collisions();
    named_numeric_casts();
    inline_numeric_casts();
    forward_local_dependencies();
    function_return_reuse();
    converted_local_snapshot();
    nominal_numeric_storage();
    projected_call_assignment();
    arithmetic_float_boundary();
    native_expression_result_storage();
    native_numeric_snapshot_storage();
    inline_expression_storage();
    positional_native_arguments();
    computed_value_qualifiers();
    system_state_invocations();
    aggregate_expression_boundaries();
    entry_aggregate_expression();
    variadic_generic_spare();
    concrete_capture_copies();
    native_arithmetic_operand_validation();
}
