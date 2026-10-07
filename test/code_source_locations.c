#include "assets/code_source_internal.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/runtime/instance.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>

void test_check(int ok, const char *message);

static void comparison_driver()
{
    behavior_asset *asset = behavior_asset_new("Integer comparison");
    graph *g = asset->graph;
    node *integer = graph_add_node(g, registry_node_by_name("Integer"));
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, g, "item > 1", &error);
    int input = -1;
    for (int i = 0; call && i < call->input_count; i++) {
        if (!strcmp(call->input_pins[i].name, "item")) {
            input = i;
        }
    }
    bool wired = integer && call && input >= 0 &&
                 !graph_set_pin_type(g, call, false, input, PIN_I32, "int") &&
                 !graph_connect_data(g, integer, 0, call, input);
    test_check(wired, "A typed integer driver connects to a comparison expression");
    if (wired) {
        bool refreshed = !behavior_expression_refresh(asset, call, &error);
        node *driver = NULL;
        int driver_pin = -1;
        test_check(
            refreshed && call->input_pins[input].type == PIN_I32 &&
                graph_input_source(g, call, input, &driver, &driver_pin) &&
                driver == integer && driver_pin == 0,
            "Refreshing a numeric comparison preserves its typed integer driver"
        );
        graph *body = behavior_asset_find_graph(asset, call->reference_id);
        bool widened = false;
        for (int i = 0; body && i < acount(body->edges); i++) {
            const edge *e = &body->edges[i];
            node *to = graph_node_by_id(body, e->to_id);
            widened |= to->desc == &NODE_Greater && e->conversion != NULL;
        }
        test_check(widened, "Integer comparison parameters widen inside the generated calculation");
    }
    behavior_asset_free(asset);
}

static void repeated_operands()
{
    const char
        *text = "behavior OperandSharing { state first: bool = true; state "
                "second: bool = false; "
                "state accepted: bool = false; state total: int = 0; "
                "value check = ((first && !second) || (!first && second)); "
                "value separate = (integer(Value: 1) + integer(Value: 1)); "
                "on Tick() { accepted = check; total = separate; } }";
    code_source *source = code_source_parse(text);
    behavior_asset *asset = code_source_compile_behavior(source);
    test_check(asset != NULL, "Repeated resolved operands and independent native calls compile together");
    if (asset) {
        int comparison_inputs = -1, arithmetic_inputs = -1;
        for (int i = 0; i < acount(asset->graphs); i++) {
            graph *g = asset->graphs[i];
            if (!g->math_expression) {
                continue;
            }
            node *entry = graph_node_by_id(g, g->entry_id);
            int inputs = 0;
            for (int j = 0; j < entry->output_count; j++) {
                inputs += entry->output_pins[j].kind == PIN_DATA;
            }
            if (strstr(g->math_expression, "&&")) {
                comparison_inputs = inputs;
            }
            if (strchr(g->math_expression, '+')) {
                arithmetic_inputs = inputs;
            }
        }
        test_check(comparison_inputs == 2, "Repeated state references share two native expression parameters");
        test_check(arithmetic_inputs == 2, "Separate equal-spelling native invocations retain distinct expression parameters");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        if (instance) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            int accepted = -1, total = -1;
            for (int i = 0; i < acount(asset->variables); i++) {
                if (!strcmp(asset->variables[i].name, "accepted")) {
                    accepted = i;
                }
                if (!strcmp(asset->variables[i].name, "total")) {
                    total = i;
                }
            }
            test_check(
                accepted >= 0 && total >= 0 && instance->variables[accepted].b &&
                    instance->variables[total].i == 2 &&
                    !behavior_instance_last_failure(instance),
                "Shared references and separate calls preserve authored "
                "runtime results"
            );
            behavior_instance_destroy(instance);
        } else {
            test_check(false, "Operand sharing creates a native runtime instance");
        }
        behavior_asset_free(asset);
    }
    code_source_free(source);
}

static void typed_enum_match()
{
    const enum_desc *type = registry_enum_count() ? registry_enum(0) : NULL;
    test_check(type && type->value_count, "Match validation has a reflected enum fixture");
    if (!type || !type->value_count) {
        return;
    }
    char text[1024];
    snprintf(
        text,
        sizeof text,
        "behavior EnumSelection { state selected: enum<\"%s\"> = %d; "
        "on Tick() { match enum (selected) { case 0 {} default {} } } }",
        type->name,
        type->values[0].value
    );
    code_source *source = code_source_parse(text);
    const char *selector = strstr(text, "match enum (selected)") +
                           strlen("match enum (");
    bool rejected = false;
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const code_source_diagnostic *d = &source->diagnostics[i];
        rejected |= strstr(d->message, "typed enum selectors are unsupported") &&
                    d->start == selector - text &&
                    d->end - d->start == (int)strlen("selected");
    }
    test_check(!source->asset && rejected, "Typed enum match selectors fail at their source span before constructing an incompatible native switch");
    code_source_free(source);
}

static int variable_index(const behavior_asset *asset, const char *name)
{
    for (int i = 0; i < acount(asset->variables); i++) {
        if (!strcmp(asset->variables[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static void numeric_captures()
{
    const char *text = "behavior NumericCapture { state input: float = 1.25; "
                       "state state_result: double = 0; state output_result: "
                       "double = 0; "
                       "state literal_result: double = 0; "
                       "pure function Read() -> (Result: float) { result "
                       "Result: float = 1.25; } "
                       "on Tick() { state_result = cast<double>(input) + 0; "
                       "output_result = cast<double>(Read()) + 0; "
                       "literal_result = cast<double>(1) + 0; } }";
    code_source *source = code_source_parse(text);
    behavior_asset *asset = code_source_compile_behavior(source);
    test_check(asset != NULL, "Explicit numeric capture widths compile for state, output and literal operands");
    if (asset) {
        int retained = 0;
        for (int i = 0; i < acount(asset->graphs); i++) {
            graph *g = asset->graphs[i];
            for (int j = 0; j < acount(g->nodes); j++) {
                node *n = g->nodes[j];
                const char *expression = behavior_expression_source(asset, n);
                if (!expression || !strchr(expression, '+')) {
                    continue;
                }
                for (int k = 0; k < n->input_count; k++) {
                    const pin_spec *input = &n->input_pins[k];
                    retained += !strcmp(input->name, "_code_input_1") &&
                                input->type == PIN_F64 &&
                                (input->flags & PIN_FLAG_WILDCARD);
                }
            }
        }
        test_check(retained == 3, "Explicit double capture inputs retain their native wildcard capability after width specialization");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        if (instance) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            int state = variable_index(asset, "state_result");
            int output = variable_index(asset, "output_result");
            int literal = variable_index(asset, "literal_result");
            test_check(
                state >= 0 && output >= 0 && literal >= 0 &&
                    instance->variables[state].d == 1.25 &&
                    instance->variables[output].d == 1.25 &&
                    instance->variables[literal].d == 1.0 &&
                    !behavior_instance_last_failure(instance),
                "Float state and float output captures retain double storage "
                "and exact 1.25 runtime values"
            );
            behavior_instance_destroy(instance);
        } else {
            test_check(false, "Numeric captures create a native runtime instance");
        }
        behavior_asset_free(asset);
    }
    code_source_free(source);
    const char *invalid[] = {
        "behavior InvalidCast { state count: int = 0; on Tick() { count = "
        "cast<string>(1) + 0; } }",
        "behavior InvalidCast { state values: int[] = [1]; state count: int = "
        "0; on Tick() { count = cast<double>(values) + 0; } }",
    };
    for (int i = 0; i < 2; i++) {
        source = code_source_parse(invalid[i]);
        bool diagnosed = false;
        for (int j = 0; j < acount(source->diagnostics); j++) {
            diagnosed |= strstr(source->diagnostics[j].message, "Unsupported scalar numeric cast") !=
                         NULL;
        }
        test_check(!source->asset && diagnosed, "Non-numeric and array casts reject before native expression construction");
        code_source_free(source);
    }
}

static void converted_expression(bool local)
{
    const char
        *text = local
                  ? "behavior ConvertedLocal { state accepted: bool = false; "
                    "state total: int = 0; "
                    "value number = integer(Value: 2); local named = "
                    "number.Value; "
                    "on Tick() { accepted = convert<\"To Real\">(&local.named) "
                    "> 1; total = named + 1; } }"
                  : "behavior ConvertedOutput { state count: int = 2; state "
                    "accepted: bool = false; "
                    "on Tick() { accepted = convert<\"To "
                    "Real\">(get_variable<\"count\">()) > 1; } }";
    code_source *source = code_source_parse(text);
    behavior_asset *asset = code_source_compile_behavior(source);
    test_check(asset != NULL, "Converted expression operands compile with their conversion output type");
    if (asset) {
        bool connected = false, local_valid = true;
        for (int i = 0; i < acount(asset->graphs); i++) {
            graph *g = asset->graphs[i];
            local_valid &= graph_validate_locals(g) == NULL;
            for (int j = 0; j < acount(g->edges); j++) {
                const edge *e = &g->edges[j];
                if (!e->conversion || strcmp(e->conversion->name, "To Real")) {
                    continue;
                }
                node *target = graph_node_by_id(g, e->to_id);
                int output = 0, pin = -1;
                connected |= graph_pin_by_id(target, e->to_pin_id, &output, &pin) &&
                             !output && target->input_pins[pin].type == PIN_F64;
            }
        }
        test_check(connected && local_valid, "Converted expression data edges retain real destinations and valid named local dependencies");
        int accepted = variable_index(asset, "accepted");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        if (instance) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            test_check(
                accepted >= 0 && !behavior_instance_last_failure(instance) &&
                    instance->variables[accepted].b,
                "Converted integer operands evaluate the authored comparison "
                "after native compilation"
            );
            if (local) {
                int total = variable_index(asset, "total");
                if (total < 0 || instance->variables[total].i != 3) {
                    fprintf(
                        stderr,
                        "Composed local total=%d, accepted=%d, failure=%d\n",
                        total < 0 ? -1 : instance->variables[total].i,
                        accepted < 0 ? -1 : instance->variables[accepted].b,
                        behavior_instance_last_failure(instance) != NULL
                    );
                }
                test_check(total >= 0 && instance->variables[total].i == 3, "Bare named locals retain their producer dependency in composed arithmetic");
            }
            behavior_instance_destroy(instance);
        } else {
            test_check(false, "Converted expression creates a native runtime instance");
        }
        behavior_asset_free(asset);
    }
    code_source_free(source);
}

static void match_execution(bool text)
{
    const char
        *code = text
                  ? "behavior TextMatch { state choice: string = \"red\"; "
                    "state count: int = 0; "
                    "on Tick() { match (choice) { case \"red\" { count += 1; } "
                    "case \"blue\" { count += 10; } default { count += 100; } "
                    "} } }"
                  : "behavior OrdinalMatch { state choice: int = 0; state "
                    "count: int = 0; "
                    "on Tick() { match (choice) { case 0 { count += 1; } "
                    "case 2 { count += 20; } default { count += 100; } } count "
                    "+= 1000; } }";
    code_source *source = code_source_parse(code);
    behavior_asset *asset = code_source_compile_behavior(source);
    test_check(asset != NULL, "Match source constructs and compiles native switches with default continuations");
    if (!asset) {
        code_source_free(source);
        return;
    }
    int choice = variable_index(asset, "choice");
    int count = variable_index(asset, "count");
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    test_check(instance && choice >= 0 && count >= 0, "Compiled match source exposes mutable selector state");
    if (instance && choice >= 0 && count >= 0) {
        const char *labels[] = { "red", "blue", "green" };
        const int totals[] = { 1001, 2101, 3121 };
        const int text_totals[] = { 1, 11, 111 };
        for (int i = 0; i < 3; i++) {
            pin_value selector = text
                                   ? (pin_value){ .type = PIN_STR,
                                                  .str = labels[i],
                                                  .type_name = "const char *" }
                                   : (pin_value){ .type = PIN_I32,
                                                  .i = i,
                                                  .type_name = "int" };
            bool set = !behavior_instance_set_variable(
                instance,
                asset->variables[choice].id,
                &selector
            );
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            test_check(
                set && !behavior_instance_last_failure(instance) &&
                    instance->variables[count].i ==
                        (text ? text_totals[i] : totals[i]),
                text ? "Native text match selects both explicit cases and its "
                       "default after state mutations"
                     : "Native ordinal match routes a sparse case hole to "
                       "default and preserves the following continuation"
            );
        }
    }
    if (instance) {
        behavior_instance_destroy(instance);
    }
    behavior_asset_free(asset);
    code_source_free(source);
}

static void shared_match_execution()
{
    code_source *source = code_source_parse(
        "behavior SharedMatch { state choice: int = 0; state count: int = 0; "
        "on Tick() { shared_route: match (choice) { case 0 { count += 1; } "
        "case 1 { count += 10; } default { count += 100; } } } "
        "on Begin() { choice = 1; goto shared_route; } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    test_check(asset != NULL, "A match label can be entered from another handler in its native scope");
    if (!asset) {
        code_source_free(source);
        return;
    }
    node *shared = NULL;
    int switches = 0;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        node *n = asset->graph->nodes[i];
        if (n->desc == &NODE_SwitchInt) {
            shared = n;
            switches++;
        }
    }
    uint64_t first_route = 0;
    bool distinct = false;
    for (int i = 0; shared && i < acount(asset->graph->edges); i++) {
        const edge *e = &asset->graph->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->to_id != shared->id ||
            e->to_pin_id != shared->input_pins[0].id) {
            continue;
        }
        distinct |= first_route && first_route != e->from_id;
        first_route = e->from_id;
    }
    test_check(switches == 1 && distinct && graph_exec_input_count(asset->graph, shared, 0) == 2, "Scope-wide labels preserve one switch with two distinct incoming execution routes");
    int count = variable_index(asset, "count");
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    test_check(instance && count >= 0, "A shared native switch publishes to the behavior runtime");
    if (instance && count >= 0) {
        const char *events[] = { "Tick", "Begin", "Tick" };
        const int totals[] = { 1, 11, 21 };
        for (int i = 0; i < 3; i++) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, events[i], (flow_inputs){ 0 }, &error);
            test_check(
                !behavior_instance_last_failure(instance) &&
                    instance->variables[count].i == totals[i],
                "Both handler routes execute the shared switch using the "
                "current selector state"
            );
        }
    }
    if (instance) {
        behavior_instance_destroy(instance);
    }
    behavior_asset_free(asset);
    code_source_free(source);
}

void run_code_source_location_checks()
{
    comparison_driver();
    numeric_captures();
    repeated_operands();
    converted_expression(false);
    converted_expression(true);
    typed_enum_match();
    match_execution(false);
    match_execution(true);
    shared_match_execution();
    const char *original = "projectile.age += event.dt + unknown";
    const char *resolved = "_component_0_age += _event_1 + unknown";
    code_source source = { .text = (char *)original };
    const char *unknown = strstr(resolved, "unknown");
    code_source_expression_error(&source, 0, original, resolved, (int)(unknown - resolved), 7, "Unknown input");
    test_check(
        acount(source.diagnostics) == 1 &&
            source.diagnostics[0].start == strstr(original, "unknown") - original &&
            source.diagnostics[0].end == (int)strlen(original),
        "Expression diagnostics map offsets after rewritten member bindings "
        "back to authored code"
    );
    afree(source.diagnostics);
    code_source_expression_error(&source, 0, original, resolved, 0, 16, "Invalid component");
    test_check(
        acount(source.diagnostics) == 1 && source.diagnostics[0].start == 0 &&
            source.diagnostics[0].end == (int)strlen("projectile.age"),
        "Diagnostics within generated identifiers highlight the original "
        "qualified member"
    );
    afree(source.diagnostics);
    const char *text = "library Locations {\n"
                       " state count: int = 1;\n"
                       " function First() -> (Result: int) {\n"
                       "  value chosen = get_variable<\"count\">();\n"
                       "  result Result: int = chosen.Value;\n"
                       " }\n"
                       " function Second() -> (Result: int) {\n"
                       "  value chosen = get_variable<\"count\">();\n"
                       "  result Result: int = chosen.Value;\n"
                       " }\n"
                       " value derived = behavior_expression<\"x + 1\">(x: "
                       "float = 2);\n"
                       "}";
    code_source *parsed = code_source_parse(text);
    test_check(parsed->asset != NULL, "Readable scoped callable fixture constructs native graphs");
    if (parsed->asset) {
        graph *first = behavior_asset_find_graph_by_name(parsed->asset, "First");
        graph *second = behavior_asset_find_graph_by_name(parsed->asset, "Second");
        node *chosen = NULL, *derived = NULL;
        for (int i = 0; second && i < acount(second->nodes); i++) {
            if (second->nodes[i]->desc == &NODE_GetVariable) {
                chosen = second->nodes[i];
            }
        }
        for (int i = 0; i < acount(parsed->asset->graph->nodes); i++) {
            if (parsed->asset->graph->nodes[i]->desc == &NODE_BehaviorExpression) {
                derived = parsed->asset->graph->nodes[i];
            }
        }
        int start = -1, end = -1;
        const char *second_at = strstr(text, "function Second");
        const char *chosen_at = strstr(second_at, "chosen =");
        bool found = second && chosen &&
                     code_source_location(parsed, second->graph_id, chosen->id, 0, &start, &end);
        test_check(
            found && start == chosen_at - text && end > start &&
                end <= (int)strlen(text) &&
                strstr(text + start, "get_variable") < text + end,
            "Callable diagnostic locations respect their owning function scope"
        );
        test_check(
            first && chosen &&
                !code_source_location(parsed, first->graph_id, chosen->id, 0, &start, &end),
            "A callable identity cannot resolve a location in another function "
            "scope"
        );
        graph *generated = derived
                             ? behavior_asset_find_graph(parsed->asset, derived->reference_id)
                             : NULL;
        node *entry = generated ? graph_node_by_id(generated, generated->entry_id)
                                : NULL;
        found = generated && entry &&
                code_source_location(parsed, generated->graph_id, entry->id, 0, &start, &end);
        const char *derived_at = strstr(text, "derived =");
        test_check(
            found && start == derived_at - text && end > start &&
                end <= (int)strlen(text) && strstr(text + start, "x + 1") < text + end,
            "Generated graph diagnostics locate their authored expression "
            "declaration"
        );
    }
    code_source_free(parsed);

    const char *obsolete = "asset { version = 3; name = \"Bad\"; graphs = "
                           "[{ kind = \"event\"; nodes = [{ id = 1; type = "
                           "\"Branch\"; }]; }]; }";
    code_source *bad = code_source_parse(obsolete);
    test_check(
        !bad->asset && acount(bad->diagnostics) > 0 && bad->diagnostics[0].start >= 0 &&
            bad->diagnostics[0].end <= (int)strlen(obsolete),
        "Serialized asset records cannot masquerade as executable source"
    );
    code_source_free(bad);

    const char *unknown_call = "behavior Bad { value broken = "
                               "engine[\"NotARegisteredNode\"](); }";
    bad = code_source_parse(unknown_call);
    const char *call_at = strstr(unknown_call, "broken =");
    const char *type_at = strstr(unknown_call, "\"NotARegisteredNode\"");
    test_check(
        !bad->asset && acount(bad->diagnostics) == 1 &&
            bad->diagnostics[0].start == call_at - unknown_call &&
            bad->diagnostics[0].end >= type_at - unknown_call + 20 &&
            bad->diagnostics[0].end <= (int)strlen(unknown_call),
        "Unknown native call diagnostics highlight their authored declaration"
    );
    code_source_free(bad);

    const char *bad_nominal = "behavior Bad { value broken = branch(Condition: "
                              "42 = false); }";
    bad = code_source_parse(bad_nominal);
    const char *nominal_at = strstr(bad_nominal, "42");
    test_check(
        !bad->asset && acount(bad->diagnostics) > 0 &&
            bad->diagnostics[0].start == nominal_at - bad_nominal &&
            bad->diagnostics[0].end - bad->diagnostics[0].start == 2,
        "Malformed named port types diagnose the offending type token"
    );
    code_source_free(bad);
}
