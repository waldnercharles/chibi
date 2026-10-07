#include "assets/code_calls.h"

#include "graph/authoring/pin_edit.h"
#include "graph/document/events.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/runtime/value_array.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message, const char *name)
{
    if (size) {
        snprintf(error, size, message, name ? name : "");
    }
    return false;
}

static bool normalize(const char *text, char *out, size_t size)
{
    size_t used = 0;
    for (int i = 0; text && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        bool delimiter = !isalnum(c);
        bool boundary = isupper(c) && i &&
                        (islower((unsigned char)text[i - 1]) ||
                         isdigit((unsigned char)text[i - 1]) ||
                         (isupper((unsigned char)text[i - 1]) &&
                          islower((unsigned char)text[i + 1])));
        if ((delimiter || boundary) && used && out[used - 1] != '_') {
            if (used + 1 >= size) {
                return false;
            }
            out[used++] = '_';
        }
        if (delimiter) {
            continue;
        }
        if (used + 1 >= size) {
            return false;
        }
        out[used++] = (char)tolower(c);
    }
    if (used && out[used - 1] == '_') {
        used--;
    }
    if (!used || !size) {
        return false;
    }
    out[used] = 0;
    return true;
}

static const char *source_name(const node_desc *desc)
{
    if (!strcmp(desc->name, "Normalize Vector 2")) {
        return "vec2_norm";
    }
    if (!strcmp(desc->name, "Spawn Prototype")) {
        return "spawn";
    }
    return NULL;
}

code_reference_kind code_call_reference_kind(const node_desc *desc, const char *port)
{
    if (!desc || !port) {
        return CODE_REFERENCE_NONE;
    }
    if (!strcmp(desc->name, "Spawn Prototype") && !strcmp(port, "Prototype")) {
        return CODE_REFERENCE_PROTOTYPE;
    }
    if (!strcmp(port, "Component") &&
        (component_node_kind_of(desc) ||
         !strcmp(desc->name, "Has Entities With Component") ||
         !strcmp(desc->name, "Find Entities With Component"))) {
        return CODE_REFERENCE_COMPONENT;
    }
    return CODE_REFERENCE_NONE;
}

const node_desc *code_call_descriptor(const char *name, char *error, size_t size)
{
    const node_desc *exact = name ? registry_node_by_name(name) : NULL;
    if (exact) {
        return exact;
    }
    const node_desc *found = NULL;
    for (int i = 0; name && i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        char alias[256];
        const char *canonical = source_name(desc);
        if (canonical ? strcmp(canonical, name)
                      : !normalize(desc->name, alias, sizeof alias) ||
                            strcmp(alias, name)) {
            continue;
        }
        if (found) {
            fail(error, size, "Ambiguous native callable '%s'; use its exact registered name", name);
            return NULL;
        }
        found = desc;
    }
    if (!found) {
        fail(error, size, "Unknown native callable '%s'", name);
    }
    return found;
}

bool code_call_name(const node_desc *desc, char *name, size_t size)
{
    if (!desc) {
        return false;
    }
    const char *canonical = source_name(desc);
    if (canonical && strlen(canonical) < size) {
        strcpy(name, canonical);
    } else if (canonical || !normalize(desc->name, name, size)) {
        return false;
    }
    char error[256];
    return code_call_descriptor(name, error, sizeof error) == desc;
}

static bool same_name(const char *actual, const char *authored)
{
    if (!actual || !authored) {
        return false;
    }
    if (!strcmp(actual, authored)) {
        return true;
    }
    char alias[256];
    return normalize(actual, alias, sizeof alias) && !strcmp(alias, authored);
}

static int port(const node *n, bool output, const char *name, char *error, size_t size)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    int exact = -1;
    for (int i = 0; name && i < count; i++) {
        if (!pins[i].name || strcmp(pins[i].name, name)) {
            continue;
        }
        if (exact >= 0) {
            fail(error, size, "Ambiguous callable port '%s'", name);
            return -2;
        }
        exact = i;
    }
    if (exact >= 0) {
        return exact;
    }
    int found = -1;
    for (int i = 0; i < count; i++) {
        if (!same_name(pins[i].name, name)) {
            continue;
        }
        if (found >= 0) {
            fail(error, size, "Ambiguous callable port '%s'", name);
            return -2;
        }
        found = i;
    }
    return found;
}

int code_call_port_index(const node *n, bool output, const char *name, char *error, size_t size)
{
    return port(n, output, name, error, size);
}

typedef struct
{
    char *path;
    uint64_t id;
} projection;

static int member(graph *g, node *n, bool output, const char *name, projection **projections, char *error, size_t size)
{
    int direct = port(n, output, name, error, size);
    if (direct >= 0 || direct == -2) {
        return direct;
    }
    const char *dot = name ? strrchr(name, '.') : NULL;
    if (!dot) {
        return -1;
    }
    char parent[512];
    size_t length = (size_t)(dot - name);
    if (length >= sizeof parent) {
        return -1;
    }
    memcpy(parent, name, length);
    parent[length] = 0;
    if (!output && component_node_kind_of(n->desc) == COMPONENT_NODE_SET &&
        same_name("Value", parent)) {
        pin_spec field;
        if (!component_node_field_pin(n, dot + 1, &field)) {
            fail(error, size, "Unknown component field '%s'", dot + 1);
            return -2;
        }
        if (component_node_show_field(g, n, dot + 1, true)) {
            fail(error, size, "Cannot expose component field '%s'", dot + 1);
            return -2;
        }
        return port(n, false, dot + 1, error, size);
    }
    uint64_t identity = 0;
    for (int i = 0; i < acount(*projections); i++) {
        if (!strcmp((*projections)[i].path, parent)) {
            identity = (*projections)[i].id;
            break;
        }
    }
    if (!identity) {
        int aggregate = member(g, n, output, parent, projections, error, size);
        if (aggregate < 0) {
            return aggregate;
        }
        identity = (output ? n->output_pins : n->input_pins)[aggregate].id;
        apush(*projections, ((projection){ xstrdup(parent), identity }));
        if (output ? graph_expose_output_fields(g, n, aggregate)
                   : graph_split_pin(g, n, 0, aggregate) < 0) {
            return -1;
        }
    }
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    int found = -1;
    for (int i = 0; i < count; i++) {
        if ((output ? pins[i].field_source : pins[i].split_parent) != identity ||
            !same_name(
                output && pins[i].field_name ? pins[i].field_name : pins[i].name,
                dot + 1
            )) {
            continue;
        }
        if (found >= 0) {
            fail(error, size, "Ambiguous aggregate member '%s'", name);
            return -2;
        }
        found = i;
    }
    return found;
}

static void free_projections(projection **projections)
{
    for (int i = 0; i < acount(*projections); i++) {
        free((*projections)[i].path);
    }
    afree(*projections);
}

bool code_call_resolve_output(graph *g, node *n, const char *name, code_call_output *out, char *error, size_t size)
{
    if (!g || !n || graph_node_by_id(g, n->id) != n || !name || !out) {
        return fail(error, size, "Invalid output selection '%s'", name);
    }
    projection *projections = NULL;
    int index = member(g, n, true, name, &projections, error, size);
    free_projections(&projections);
    if (index < 0) {
        return fail(error, size, "Unknown or ambiguous output '%s'", name);
    }
    *out = (code_call_output){ n, index };
    return true;
}

static bool authored_run(graph *g, node *n, const code_call_spec *spec)
{
    graph_pin_run run;
    if (!graph_pin_run_of(n, &run) || run.output) {
        return true;
    }
    for (int i = run.first + run.count - 1; i >= run.first; i--) {
        const char *name = n->input_pins[i].name;
        bool provided = false;
        for (int j = 0; j < spec->argument_count; j++) {
            provided |= same_name(name, spec->arguments[j].name);
        }
        for (int j = 0; j < spec->input_count; j++) {
            provided |= same_name(name, spec->inputs[j].name);
        }
        if (!provided && graph_remove_pin(g, n, false, i)) {
            return false;
        }
    }
    return true;
}

static bool own_signature(graph *g, node *n, char *error, size_t size)
{
    for (int side = 0; side < 2; side++) {
        pin_spec *pins = side ? n->output_pins : n->input_pins;
        int count = side ? n->output_count : n->input_count;
        for (int i = 0; i < count; i++) {
            pin_value owned;
            if (pin_value_copy_to_graph(g, &owned, &pins[i].default_value)) {
                return fail(
                    error,
                    size,
                    "Cannot own callable default '%s'",
                    pins[i].name
                );
            }
            if (pins[i].type_name) {
                pins[i].type_name = graph_own_str(
                    g,
                    pins[i].type_name,
                    strlen(pins[i].type_name)
                );
            }
            owned.type_name = pins[i].type_name;
            pins[i].default_value = owned;
        }
    }
    return true;
}

static bool signature(graph *g, node *n, const graph *target, char *error, size_t size)
{
    if (target->kind == GRAPH_FUNCTION) {
        const char *why = graph_library_refresh_call(g, n, target);
        return !why || fail(error, size, "%s", why);
    }
    if (target->kind != GRAPH_MACRO || n->desc != &NODE_CallMacro) {
        return fail(
            error,
            size,
            "Callable signature kind does not match '%s'",
            target->name
        );
    }
    const node *entry = graph_node_by_id(target, target->entry_id);
    const node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret) {
        return fail(error, size, "Missing callable signature '%s'", target->name);
    }
    for (int side = 0; side < 2; side++) {
        int count = side ? n->output_count : n->input_count;
        for (int i = count - 1; i >= 0; i--) {
            if (graph_remove_pin(g, n, side, i)) {
                return false;
            }
        }
        const pin_spec *pins = side ? ret->input_pins : entry->output_pins;
        count = side ? ret->input_count : entry->output_count;
        for (int i = 0; i < count; i++) {
            pin_spec pin = pins[i];
            pin.binding = NULL;
            pin.field_source = 0;
            pin.split_parent = 0;
            pin.declaration_id = pins[i].id;
            if (graph_add_pin(g, n, side, &pin) < 0) {
                return false;
            }
        }
    }
    return true;
}

static bool ports(
    graph *g,
    node *n,
    bool output,
    const code_call_port *schema,
    int count,
    bool complete,
    projection **projections,
    char *error,
    size_t size
)
{
    if (complete) {
        int total = output ? n->output_count : n->input_count;
        for (int p = total - 1; p >= 0; p--) {
            const pin_spec *pin = &(output ? n->output_pins : n->input_pins)[p];
            bool declared = false;
            for (int i = 0; i < count; i++) {
                declared |= same_name(pin->name, schema[i].name) ||
                            same_name(pin->name, schema[i].aggregate);
            }
            if (!declared && graph_remove_pin(g, n, output, p)) {
                return fail(
                    error,
                    size,
                    "Cannot replace callable signature '%s'",
                    n->desc->name
                );
            }
        }
    }
    for (int i = 0; i < count; i++) {
        const code_call_port *value = &schema[i];
        if (!value->name || !value->name[0]) {
            return fail(error, size, "Callable port needs a name '%s'", value->name);
        }
        for (int j = 0; j < i; j++) {
            if (same_name(schema[j].name, value->name) ||
                same_name(value->name, schema[j].name)) {
                return fail(error, size, "Repeated callable port '%s'", value->name);
            }
        }
        int index = member(g, n, output, value->name, projections, error, size);
        if (value->aggregate && value->field) {
            char path[512];
            snprintf(path, sizeof path, "%s.%s", value->aggregate, value->field);
            index = member(g, n, output, path, projections, error, size);
            if (index < 0) {
                return fail(error, size, "Unknown aggregate member '%s'", path);
            }
        }
        if (index == -2) {
            return false;
        }
        if (index < 0) {
            pin_spec pin = { .name = value->name,
                             .type = value->type,
                             .type_name = value->type_name,
                             .container = value->container,
                             .kind = value->kind,
                             .flags = value->has_traits ? value->flags : 0,
                             .group = value->group,
                             .default_value = { .type = value->type,
                                                .type_name = value->type_name,
                                                .container = value->container } };
            /* An unresolved variadic port keeps the native Generic spare schema. */
            if (!value->has_traits && !output && value->type == PIN_FLOW &&
                n->desc->host_call && n->desc->host_call->variadic) {
                pin.flags |= n->desc->inputs[n->desc->input_count - 1].flags &
                             PIN_FLAG_WILDCARD;
            }
            index = graph_append_pin(g, n, output, &pin);
        }
        if (index < 0) {
            return fail(error, size, "Cannot declare callable port '%s'", value->name);
        }
        pin_spec *pin = &(output ? n->output_pins : n->input_pins)[index];
        if (pin->kind != value->kind) {
            return fail(error, size, "Callable port kind differs '%s'", value->name);
        }
        if (pin->flags & PIN_FLAG_SETTING) {
            if (pin->type != value->type || pin->container != value->container ||
                (value->has_traits && pin->flags != value->flags)) {
                return fail(
                    error,
                    size,
                    "Callable setting type differs '%s'",
                    value->name
                );
            }
            if (value->has_default &&
                graph_set_pin_default(g, n, output, index, &value->default_value)) {
                return fail(
                    error,
                    size,
                    "Cannot configure callable setting '%s'",
                    value->name
                );
            }
            continue;
        }
        if (value->has_traits) {
            pin->flags = value->flags;
            if (value->flags & PIN_FLAG_WILDCARD) {
                pin->group = value->group;
            }
        }
        if (graph_set_pin_type(g, n, output, index, value->type, value->type_name) ||
            graph_set_pin_container(g, n, output, index, value->container) ||
            (value->has_default &&
             graph_set_pin_default(g, n, output, index, &value->default_value))) {
            return fail(
                error,
                size,
                "Cannot configure callable port '%s'",
                value->name
            );
        }
    }
    if (complete) {
        /* The schema states the authored order, including fixed and dynamic ports. */
        for (int i = 0; i < count; i++) {
            int index = port(n, output, schema[i].name, error, size);
            if (index < 0 || graph_move_pin(g, n, output, index, i) < 0) {
                return fail(
                    error,
                    size,
                    "Cannot order callable port '%s'",
                    schema[i].name
                );
            }
        }
    }
    return true;
}

int code_call_use_local(graph *g, uint64_t id, node *target, int input)
{
    const graph_local *local = graph_local_by_id(g, id);
    node *source = local ? graph_node_by_id(g, local->source_id) : NULL;
    int output = 0, pin = -1;
    if (!source || !graph_pin_by_id(source, local->source_pin_id, &output, &pin) ||
        !output || input < 0 || input >= target->input_count) {
        return -1;
    }
    pin_spec *producer = &source->output_pins[pin];
    unsigned flags = producer->flags;
    if (producer->type != PIN_FLOW && target->input_pins[input].type != PIN_FLOW &&
        pin_compatible(producer, &target->input_pins[input])) {
        producer->flags &= ~PIN_FLAG_WILDCARD;
    }
    int result = graph_use_local(g, id, target, input);
    producer->flags = flags;
    return result;
}

static bool materialize_cast(
    behavior_asset *asset,
    graph *g,
    code_call_argument *argument,
    bool input_fallback,
    char *error,
    size_t size
)
{
    assert(argument->has_type);
    if (argument->kind == CODE_CALL_LITERAL) {
        return true;
    }
    code_call_argument input = *argument;
    if (!input_fallback) {
        input.has_fallback_binding = false;
        input.has_fallback_default = false;
    }
    input.name = "_cast_value";
    input.positional = false;
    code_call_port parameter = { .name = input.name,
                                 .type = input.type,
                                 .type_name = pin_type_name(input.type),
                                 .kind = PIN_DATA };
    if (input.has_fallback_default) {
        pin_spec from = { .kind = PIN_DATA,
                          .type = input.fallback_default.type,
                          .container = input.fallback_default.container };
        pin_spec target = { .kind = PIN_DATA, .type = input.type };
        const node_desc *conversion = conversion_for_pins(&from, &target);
        if (!pin_compatible(&from, &target) && !conversion) {
            fail(error, size, "Fallback default is incompatible with numeric cast storage%s", "");
            return false;
        }
        if (conversion) {
            input.fallback_default = conversion->fn(&input.fallback_default, 1, NULL);
        }
        pin_coerce(&input.fallback_default, &target);
    }
    code_call_port result = parameter;
    result.name = "Result";
    code_call_spec cast = { .name = "Behavior Expression",
                            .expression = input.name,
                            .expression_mode = GRAPH_EXPRESSION_VALUE,
                            .arguments = &input,
                            .argument_count = 1,
                            .inputs = &parameter,
                            .input_count = 1,
                            .results = &result,
                            .result_count = 1 };
    node *boundary = code_call_build(asset, g, &cast, error, size);
    if (!boundary) {
        return false;
    }
    const char *name = argument->name;
    bool positional = argument->positional;
    int ordinal = argument->ordinal;
    code_call_argument fallback = input_fallback ? (code_call_argument){ 0 } : *argument;
    *argument = (code_call_argument){
        .name = name,
        .positional = positional,
        .ordinal = ordinal,
        .kind = CODE_CALL_OUTPUT,
        .source = { boundary, 0 },
        .has_fallback_binding = fallback.has_fallback_binding,
        .fallback_binding = fallback.fallback_binding,
        .has_fallback_default = fallback.has_fallback_default,
        .fallback_default = fallback.fallback_default
    };
    return true;
}

bool code_call_materialize_cast(
    behavior_asset *asset,
    graph *g,
    code_call_argument *argument,
    char *error,
    size_t size
)
{
    return materialize_cast(asset, g, argument, false, error, size);
}

bool code_call_materialize_cast_fallback(
    behavior_asset *asset,
    graph *g,
    code_call_argument *argument,
    char *error,
    size_t size
)
{
    return materialize_cast(asset, g, argument, true, error, size);
}

int code_call_apply_fallback(graph *g, node *n, int input, const code_call_argument *argument)
{
    if (argument->has_fallback_default &&
        graph_set_pin_default(g, n, false, input, &argument->fallback_default)) {
        return -1;
    }
    return argument->has_fallback_binding
             ? graph_set_pin_binding(g, n, false, input, &argument->fallback_binding)
             : 0;
}

static bool component_setting_known(component_node_kind kind, const char *key, char *error, size_t size)
{
    if (!kind || !key || !key[0] || component_node_canonical_key(kind, key)) {
        return true;
    }
    return fail(
        error,
        size,
        kind == COMPONENT_NODE_MAKE || kind == COMPONENT_NODE_BREAK
            ? "Unknown struct type '%s'"
            : "Unknown component '%s'",
        key
    );
}

/* Local names and producer identities belong to the graph arena. Native
   callable shaping may prune their unused declarations before arguments feed. */
static void retain_argument_local(graph *g, const pin_binding *binding, graph_local **locals)
{
    if (binding->kind != PIN_BINDING_LOCAL) {
        return;
    }
    for (int i = 0; i < acount(*locals); i++) {
        if ((*locals)[i].id == binding->local_id) {
            return;
        }
    }
    const graph_local *local = graph_local_by_id(g, binding->local_id);
    if (local) {
        apush(*locals, *local);
    }
}

static bool restore_argument_local(graph *g, const pin_binding *binding, const graph_local *locals)
{
    if (binding->kind != PIN_BINDING_LOCAL || graph_local_by_id(g, binding->local_id)) {
        return true;
    }
    for (int i = 0; i < acount(locals); i++) {
        const graph_local *local = &locals[i];
        if (local->id != binding->local_id) {
            continue;
        }
        node *source = graph_node_by_id(g, local->source_id);
        int output = 0;
        const pin_spec *pin = source
                                ? graph_pin_by_id(source, local->source_pin_id, &output, NULL)
                                : NULL;
        if (!pin || !output || pin->kind != PIN_DATA || pin->type == PIN_FLOW) {
            return false;
        }
        apush(g->locals, *local);
        return true;
    }
    return false;
}

node *code_call_build(behavior_asset *asset, graph *g, const code_call_spec *spec, char *error, size_t size)
{
    if (size) {
        error[0] = 0;
    }
    if (!asset || !g || !spec || !behavior_asset_mutable(asset) ||
        behavior_asset_find_graph(asset, g->graph_id) != g ||
        spec->argument_count < 0 || spec->input_count < 0 ||
        spec->result_count < 0 || (spec->argument_count && !spec->arguments) ||
        (spec->input_count && !spec->inputs) ||
        (spec->result_count && !spec->results)) {
        fail(error, size, "Invalid native callable candidate '%s'", spec ? spec->name : NULL);
        return NULL;
    }
    const node_desc *desc = code_call_descriptor(spec->name, error, size);
    if (!desc) {
        return NULL;
    }
    graph_local *argument_locals = NULL;
    for (int i = 0; i < spec->argument_count; i++) {
        const code_call_argument *arg = &spec->arguments[i];
        if (arg->kind == CODE_CALL_BINDING) {
            retain_argument_local(g, &arg->binding, &argument_locals);
        }
        if (arg->has_fallback_binding) {
            retain_argument_local(g, &arg->fallback_binding, &argument_locals);
        }
    }
    node *n = desc == &NODE_FunctionEntry ? graph_node_by_id(g, g->entry_id)
            : desc == &NODE_Return        ? graph_node_by_id(g, g->return_id)
                                          : NULL;
    bool reused = n != NULL;
    if (reused && n->desc != desc) {
        fail(error, size, "Declared signature differs from callable '%s'", spec->name);
        afree(argument_locals);
        return NULL;
    }
    if (reused) {
        /* Signatures belong to their declared scope and already have stable IDs. */
    } else if (desc == &NODE_BehaviorExpression && spec->expression) {
        behavior_expression_error why = { 0 };
        n = behavior_expression_add_mode(asset, g, spec->expression, spec->expression_mode, &why);
        if (!n) {
            fail(error, size, "%s", why.message);
            afree(argument_locals);
            return NULL;
        }
    } else if ((desc == &NODE_GetVariable || desc == &NODE_SetVariable) && spec->reference) {
        behavior_variable *variable = behavior_asset_find_variable_by_name(asset, spec->reference);
        if (!variable) {
            fail(error, size, "Unknown variable '%s'", spec->reference);
            afree(argument_locals);
            return NULL;
        }
        n = desc == &NODE_GetVariable
              ? behavior_asset_add_get_node_to_graph(asset, g, variable->id)
              : behavior_asset_add_set_node_to_graph(asset, g, variable->id);
    } else {
        n = graph_add_node(g, desc);
    }
    if (!n) {
        fail(error, size, "Cannot construct native callable '%s'", spec->name);
        afree(argument_locals);
        return NULL;
    }
    projection *projections = NULL;
    unsigned *input_flags = NULL;
    if (desc == &NODE_Self && asset->owner_type_name &&
        graph_set_pin_type(g, n, true, 0, PIN_HANDLE, asset->owner_type_name)) {
        goto refused;
    }
    if (component_node_kind_of(desc) && spec->reference) {
        if (!component_setting_known(component_node_kind_of(desc), spec->reference, error, size) ||
            component_node_retarget(g, n, spec->reference)) {
            goto refused;
        }
    } else if (spec->target) {
        if (graph_set_node_reference(g, n, spec->target->graph_id, spec->target->name) ||
            !signature(g, n, spec->target, error, size)) {
            goto refused;
        }
    } else if (spec->reference && desc != &NODE_GetVariable && desc != &NODE_SetVariable) {
        if (graph_set_node_reference(g, n, 0, spec->reference)) {
            goto refused;
        }
        if (desc == &NODE_Event && graph_host_event_apply(g, n)) {
            goto refused;
        }
    }
    if (spec->reference_asset &&
        graph_set_node_reference_asset(g, n, spec->reference_asset)) {
        goto refused;
    }
    if (spec->event && graph_event_apply(g, n, spec->event)) {
        goto refused;
    }
    if ((spec->target || spec->event) && !own_signature(g, n, error, size)) {
        goto refused;
    }
    if (spec->target) {
        for (int i = 0; i < n->output_count; i++) {
            pin_spec *pin = &n->output_pins[i];
            if (pin->kind == PIN_DATA) {
                /* A callee Return input is executable body state, not a caller
                 * result default. Authored call defaults are applied below. */
                pin->default_value = (pin_value){ .type = pin->type,
                                                  .type_name = pin->type_name,
                                                  .container = pin->container };
            }
        }
    }
    if (!authored_run(g, n, spec)) {
        goto refused;
    }
    for (int i = 0; i < spec->argument_count; i++) {
        const code_call_argument *arg = &spec->arguments[i];
        int setting = port(n, false, arg->name, error, size);
        if (setting >= 0 && (n->input_pins[setting].flags & PIN_FLAG_SETTING)) {
            if (component_node_kind_of(desc) && setting == component_node_setting(n) &&
                arg->kind == CODE_CALL_LITERAL && arg->literal.type == PIN_NAME &&
                !component_setting_known(
                    component_node_kind_of(desc),
                    arg->literal.str,
                    error,
                    size
                )) {
                goto refused;
            }
            if (arg->kind != CODE_CALL_LITERAL ||
                graph_set_pin_default(g, n, false, setting, &arg->literal)) {
                goto refused;
            }
        }
    }
    if (!ports(g, n, false, spec->inputs, spec->input_count, spec->complete_inputs, &projections, error, size) ||
        !ports(g, n, true, spec->results, spec->result_count, spec->complete_results, &projections, error, size)) {
        goto refused;
    }
    for (int i = 0; i < n->input_count; i++) {
        apush(input_flags, n->input_pins[i].flags);
    }
    for (int i = 0; i < spec->argument_count; i++) {
        code_call_argument argument = spec->arguments[i];
        const code_call_argument *arg = &argument;
        int index = -1;
        if (arg->positional) {
            int ordinal = 0;
            for (int j = 0; j < n->input_count; j++) {
                if (n->input_pins[j].kind != PIN_DATA || n->input_pins[j].field_source) {
                    continue;
                }
                if (ordinal++ == arg->ordinal) {
                    index = j;
                    break;
                }
            }
        } else {
            index = member(g, n, false, arg->name, &projections, error, size);
        }
        for (int j = 0; j < i; j++) {
            const code_call_argument *prior = &spec->arguments[j];
            int previous = -1;
            if (prior->positional) {
                int ordinal = 0;
                for (int k = 0; k < n->input_count; k++) {
                    if (n->input_pins[k].kind != PIN_DATA ||
                        n->input_pins[k].field_source) {
                        continue;
                    }
                    if (ordinal++ == prior->ordinal) {
                        previous = k;
                        break;
                    }
                }
            } else {
                previous = member(g, n, false, prior->name, &projections, error, size);
            }
            if (index >= 0 && index == previous) {
                fail(error, size, "Repeated callable argument '%s'", arg->name);
                goto refused;
            }
        }
        if (index < 0) {
            fail(error, size, "Unknown callable argument '%s'", arg->name);
            goto refused;
        }
        if (n->input_pins[index].flags & PIN_FLAG_SETTING) {
            continue;
        }
        if (arg->has_type && arg->type != n->input_pins[index].type &&
            !code_call_materialize_cast(asset, g, &argument, error, size)) {
            goto refused;
        }
        if (arg->has_type && arg->type != PIN_FLOW &&
            n->input_pins[index].type == arg->type) {
            n->input_pins[index].flags &= ~PIN_FLAG_WILDCARD;
        }
        unsigned flags = n->input_pins[index].flags;
        bool typed_input = false;
        for (int j = 0; j < spec->input_count; j++) {
            const code_call_port *input = &spec->inputs[j];
            if (same_name(input->name, arg->name) && input->type != PIN_FLOW) {
                /* Explicit storage also fixes the nominal aggregate type;
                 * compatible drivers do not replace the authored schema. */
                n->input_pins[index].flags &= ~PIN_FLAG_WILDCARD;
                typed_input = true;
            }
        }
        if (arg->kind == CODE_CALL_LITERAL) {
            if (graph_set_pin_default(g, n, false, index, &arg->literal)) {
                goto refused;
            }
        } else if (arg->kind == CODE_CALL_BINDING) {
            if (!restore_argument_local(g, &arg->binding, argument_locals)) {
                goto refused;
            }
            if (arg->has_binding_type && n->input_pins[index].type == PIN_FLOW &&
                (n->input_pins[index].flags & PIN_FLAG_WILDCARD)) {
                if (arg->binding_type == PIN_FLOW ||
                    n->input_pins[index].container != arg->binding_container ||
                    graph_set_pin_type(g, n, false, index, arg->binding_type, arg->binding_type_name)) {
                    fail(error, size, "Cannot infer callable binding type '%s'", arg->name);
                    goto refused;
                }
            }
            if (arg->binding.kind == PIN_BINDING_LOCAL) {
                if (!arg->conversion) {
                    if (code_call_use_local(g, arg->binding.local_id, n, index)) {
                        goto refused;
                    }
                    if (!arg->has_type) {
                        n->input_pins[index].flags = flags;
                    }
                    if ((arg->has_fallback_binding &&
                         !restore_argument_local(g, &arg->fallback_binding, argument_locals)) ||
                        code_call_apply_fallback(g, n, index, arg)) {
                        goto refused;
                    }
                    continue;
                }
                const graph_local *local = graph_local_by_id(g, arg->binding.local_id);
                node *source = local ? graph_node_by_id(g, local->source_id) : NULL;
                int output = 0, pin = -1;
                if (!source ||
                    !graph_pin_by_id(source, local->source_pin_id, &output, &pin) ||
                    !output ||
                    graph_connect_data_conversion(g, source, pin, n, index, arg->conversion)) {
                    goto refused;
                }
            }
            if (graph_set_pin_binding(g, n, false, index, &arg->binding)) {
                goto refused;
            }
        } else if (arg->kind == CODE_CALL_OUTPUT) {
            if (!arg->source.node ||
                graph_node_by_id(g, arg->source.node->id) != arg->source.node ||
                arg->source.pin < 0 ||
                arg->source.pin >= arg->source.node->output_count) {
                goto refused;
            }
            const node_desc *conversion = arg->conversion;
            if (!conversion && typed_input) {
                conversion = conversion_for_pins(
                    &arg->source.node->output_pins[arg->source.pin],
                    &n->input_pins[index]
                );
            }
            pin_spec *source_pin = &arg->source.node->output_pins[arg->source.pin];
            unsigned source_flags = source_pin->flags;
            if (source_pin->type != PIN_FLOW && n->input_pins[index].type != PIN_FLOW &&
                pin_compatible(source_pin, &n->input_pins[index])) {
                source_pin->flags &= ~PIN_FLAG_WILDCARD;
            }
            int result = n->input_pins[index].kind == PIN_EXEC
                           ? graph_connect_exec(g, arg->source.node, arg->source.pin, n, index, 0)
                           : graph_connect_data_conversion(
                                 g,
                                 arg->source.node,
                                 arg->source.pin,
                                 n,
                                 index,
                                 conversion
                             );
            source_pin->flags = source_flags;
            if (result) {
                snprintf(
                    error,
                    size,
                    "Cannot connect callable argument '%s' (%s) to '%s' (%s): "
                    "%s",
                    arg->name,
                    source_pin->type_name ? source_pin->type_name
                                          : pin_type_name(source_pin->type),
                    spec->reference ? spec->reference : spec->name,
                    n->input_pins[index].type_name
                        ? n->input_pins[index].type_name
                        : pin_type_name(n->input_pins[index].type),
                    graph_last_error()
                );
                goto refused;
            }
        } else {
            goto refused;
        }
        if (!arg->has_type) {
            n->input_pins[index].flags = flags;
        }
        if ((arg->has_fallback_binding &&
             !restore_argument_local(g, &arg->fallback_binding, argument_locals)) ||
            code_call_apply_fallback(g, n, index, arg)) {
            goto refused;
        }
    }
    for (int i = 0; i < acount(input_flags); i++) {
        n->input_pins[i].flags = input_flags[i];
    }
    if (!spec->result_count && desc->pure_kind == NODE_PURE_COPY &&
        n->input_count == 1 && n->output_count == 1 &&
        n->input_pins[0].kind == PIN_DATA && n->output_pins[0].kind == PIN_DATA &&
        (n->input_pins[0].flags & PIN_FLAG_WILDCARD) &&
        (n->output_pins[0].flags & PIN_FLAG_WILDCARD) &&
        n->input_pins[0].group == n->output_pins[0].group &&
        graph_set_pin_container(g, n, true, 0, n->input_pins[0].container)) {
        goto refused;
    }
    if (desc == &NODE_BehaviorExpression && spec->expression) {
        behavior_expression_error why = { 0 };
        if ((!spec->result_count &&
             behavior_expression_infer_assignment_result(asset, n, &why)) ||
            behavior_expression_refresh_mode(asset, n, spec->expression_mode, &why)) {
            fail(error, size, "%s", why.message);
            goto refused;
        }
    }
    afree(argument_locals);
    afree(input_flags);
    free_projections(&projections);
    return n;
refused:
    afree(argument_locals);
    afree(input_flags);
    free_projections(&projections);
    if (size && !error[0]) {
        fail(error, size, "Cannot configure native callable '%s'", spec->name);
    }
    if (!reused) {
        graph_remove_node(g, n);
    }
    return NULL;
}
