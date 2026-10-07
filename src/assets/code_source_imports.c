#include "assets/code_program_internal.h"
#include "assets/code_source.h"
#include "graph/document/library.h"
#include "graph/document/problems.h"
#include "graph/document/serialize.h"
#include "graph/nodes/component_nodes.h"
#include "graph/runtime/value_array.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool input_authored(const code_program *program, const node *call, int input)
{
    for (int i = 0; program && i < acount(program->call_inputs); i++) {
        const code_program_call_inputs *record = &program->call_inputs[i];
        if (record->native != call) {
            continue;
        }
        for (int j = 0; j < acount(record->explicit_inputs); j++) {
            if (record->explicit_inputs[j] == input) {
                return true;
            }
        }
        return false;
    }
    /* No proven source ownership means the value must be retained. */
    return true;
}
static void inherited_defaults(const code_source *source, graph *caller, node *call, const graph *target)
{
    const graph *baseline = behavior_asset_find_graph(source->asset, caller->graph_id);
    const node *old = baseline ? graph_node_by_id(baseline, call->id) : NULL;
    if (!old || !old->reference_asset || !old->reference_name ||
        strcmp(old->reference_asset, call->reference_asset) ||
        strcmp(old->reference_name, call->reference_name)) {
        return;
    }
    const node *entry = graph_node_by_id(target, target->entry_id);
    for (int i = 0; i < call->input_count; i++) {
        const pin_spec *current = &call->input_pins[i];
        int prior = -1;
        for (int j = 0; j < old->input_count; j++) {
            if (old->input_pins[j].id == current->id) {
                prior = j;
                break;
            }
        }
        if (prior < 0 || input_authored(source->program, old, prior) ||
            !pin_value_equal(&current->default_value, &old->input_pins[prior].default_value)) {
            continue;
        }
        for (int j = 0; j < entry->output_count; j++) {
            const pin_spec *parameter = &entry->output_pins[j];
            if (parameter->kind == PIN_DATA && parameter->id == current->declaration_id) {
                int result = graph_set_pin_default(
                    caller,
                    call,
                    false,
                    i,
                    &parameter->default_value
                );
                assert(!result);
                break;
            }
        }
    }
}

/* Return values belong to the callee body. Refreshing its signature must not
 * turn a new result pin into a caller-side constant default. */
static void result_defaults(graph *caller, node *call, const pin_spec *previous)
{
    for (int i = 0; i < call->output_count; i++) {
        const pin_spec *pin = &call->output_pins[i];
        if (pin->kind != PIN_DATA || pin->field_source) {
            continue;
        }
        const pin_spec *old = NULL;
        for (int j = 0; j < acount(previous); j++) {
            if (previous[j].kind == PIN_DATA && !previous[j].field_source &&
                (previous[j].id == pin->id || !strcmp(previous[j].name, pin->name))) {
                old = &previous[j];
                break;
            }
        }
        bool compatible = old && old->type == pin->type &&
                          old->container == pin->container &&
                          !strcmp(
                              old->type_name ? old->type_name : "",
                              pin->type_name ? pin->type_name : ""
                          );
        pin_value zero = { .type = pin->type,
                           .type_name = pin->type_name,
                           .container = pin->container };
        int result = graph_set_pin_default(caller, call, true, i, compatible ? &old->default_value : &zero);
        assert(!result);
    }
}

behavior_asset *code_source_refresh_imports(
    const code_source *source,
    const behavior_asset *working,
    char *error,
    size_t size
)
{
    if (size) {
        error[0] = 0;
    }
    behavior_asset *candidate = behavior_asset_clone(working);
    for (int g = 0; g < acount(candidate->graphs); g++) {
        graph *caller = candidate->graphs[g];
        for (int n = 0; n < acount(caller->nodes); n++) {
            node *call = caller->nodes[n];
            if (!call->reference_asset || !call->reference_asset[0]) {
                continue;
            }
            /* Only callable references import library signatures. Nominal
             * components and other engine references keep their own schema. */
            if (call->desc != &NODE_CallFunction && call->desc != &NODE_CallPureFunction &&
                call->desc != &NODE_BehaviorExpression &&
                call->desc != &NODE_CallMacro) {
                continue;
            }
            char message[256];
            const graph *target = code_source_context_graph(
                &source->context,
                NULL,
                call->reference_asset,
                call->reference_name,
                call->desc == &NODE_CallMacro ? GRAPH_MACRO : GRAPH_FUNCTION,
                NULL,
                message,
                sizeof message
            );
            pin_spec *outputs = NULL;
            for (int i = 0; target && i < call->output_count; i++) {
                pin_spec old = call->output_pins[i];
                int copied = pin_value_copy_to_graph(
                    caller,
                    &old.default_value,
                    &call->output_pins[i].default_value
                );
                assert(!copied);
                apush(outputs, old);
            }
            const char *problem = target
                                    ? graph_library_refresh_call(caller, call, target)
                                    : message;
            if (problem) {
                if (size) {
                    snprintf(
                        error,
                        size,
                        "%s.%s: %s",
                        call->reference_asset,
                        call->reference_name ? call->reference_name : "",
                        problem
                    );
                }
                afree(outputs);
                behavior_asset_free(candidate);
                return NULL;
            }
            result_defaults(caller, call, outputs);
            afree(outputs);
            inherited_defaults(source, caller, call, target);
        }
    }
    const behavior_asset **catalog = NULL;
    for (int i = 0; i < source->context.library_count; i++) {
        apush(catalog, source->context.libraries[i]);
    }
    for (int i = 0; i < source->context.event_count; i++) {
        apush(catalog, source->context.events[i]);
    }
    compile_report report;
    compile_report_init(&report);
    behavior_asset *linked = behavior_asset_validate(candidate, catalog, acount(catalog), &report);
    afree(catalog);
    bool blocked = compile_report_blocked(&report);
    if (blocked && size) {
        snprintf(error, size, "Imported call validation failed.");
        for (int i = 0; i < acount(report.items); i++) {
            if (report.items[i].severity == CMPL_ERROR) {
                snprintf(error, size, "%s", report.items[i].message);
                break;
            }
        }
    }
    behavior_asset_free(linked);
    compile_report_free(&report);
    if (blocked) {
        behavior_asset_free(candidate);
        return NULL;
    }
    return candidate;
}
