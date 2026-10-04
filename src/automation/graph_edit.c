#include "automation/graph_edit.h"

#include "assets/new_asset.h"
#include "graph/authoring/expand.h"
#include "graph/authoring/layout.h"
#include "graph/authoring/optimize.h"
#include "graph/authoring/pin_edit.h"
#include "graph/compiler/compile.h"
#include "graph/document/events.h"
#include "graph/document/extract.h"
#include "graph/document/graph_signature.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/modeling/model_authoring.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    GRAPH_EDIT_MAX_OPERATIONS = 128,
    GRAPH_EDIT_MAX_REFS = 128,
};

typedef struct
{
    const char *name;
    uint64_t id;
    node *node;
} node_ref;

typedef struct
{
    const char *name;
    uint64_t id;
    graph *graph;
} graph_ref;

typedef struct
{
    const char *name;
    uint64_t id;
} variable_ref;

typedef struct
{
    behavior_asset *asset;
    graph *graph;
    yyjson_mut_doc *doc;
    yyjson_mut_val *result;
    const behavior_asset *const *libraries;
    int library_count;
    node_ref *nodes;
    graph_ref *graphs;
    variable_ref *variables;
    graph **layout_graphs;
    int operation;
    char *error;
    size_t error_size;
} edit_context;

static int request_read_value(
    pin_type type,
    const char *type_name,
    yyjson_val *value,
    pin_value *out,
    graph *storage
)
{
    av_doc *doc = av_doc_new();
    int result = serialize_read_value(type, type_name, av_from_json(doc, value), out, storage);
    av_doc_free(doc);
    return result;
}

static int request_read_array(
    pin_type type,
    const char *type_name,
    yyjson_val *value,
    pin_value *out,
    graph *storage
)
{
    av_doc *doc = av_doc_new();
    int result = serialize_read_array(type, type_name, av_from_json(doc, value), out, storage);
    av_doc_free(doc);
    return result;
}

static void free_edit_context(edit_context *ctx)
{
    afree(ctx->nodes);
    afree(ctx->graphs);
    afree(ctx->variables);
    afree(ctx->layout_graphs);
    behavior_asset_free(ctx->asset);
    ctx->asset = NULL;
}

static void set_error(edit_context *ctx, const char *fmt, ...)
{
    if (!ctx->error || !ctx->error_size) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->error, ctx->error_size, fmt, ap);
    va_end(ap);
}

static int add_layout_graph(edit_context *ctx, graph *graph)
{
    for (int i = 0; i < acount(ctx->layout_graphs); i++) {
        if (ctx->layout_graphs[i] == graph) {
            return 0;
        }
    }
    if (acount(ctx->layout_graphs) >= GRAPH_EDIT_MAX_REFS) {
        set_error(ctx, "edit batch touches too many graphs to arrange");
        return -1;
    }
    apush(ctx->layout_graphs, graph);
    return 0;
}

static void add_failure(edit_context *ctx)
{
    if (!ctx->result) {
        return;
    }
    yyjson_mut_obj_add_int(ctx->doc, ctx->result, "failed_operation", ctx->operation);
    yyjson_mut_obj_add_strcpy(ctx->doc, ctx->result, "error", ctx->error ? ctx->error : "edit failed");
}

static int fail_request(edit_context *ctx, const char *fmt, ...)
{
    if (ctx->error && ctx->error_size) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(ctx->error, ctx->error_size, fmt, ap);
        va_end(ap);
    }
    add_failure(ctx);
    return CHIBI_AUTOMATION_INVALID_REQUEST;
}

static int fail_validation(edit_context *ctx, const char *fmt, ...)
{
    if (ctx->error && ctx->error_size) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(ctx->error, ctx->error_size, fmt, ap);
        va_end(ap);
    }
    add_failure(ctx);
    return CHIBI_AUTOMATION_VALIDATION_ERROR;
}

static bool value_uint(yyjson_val *value, uint64_t *out)
{
    if (!value || !yyjson_is_uint(value)) {
        return false;
    }
    *out = yyjson_get_uint(value);
    return true;
}

static bool value_nonnegative_int(yyjson_val *value, int *out)
{
    if (yyjson_is_sint(value)) {
        int64_t number = yyjson_get_sint(value);
        if (number < 0 || number > INT_MAX) {
            return false;
        }
        *out = (int)number;
        return true;
    }
    if (yyjson_is_uint(value)) {
        uint64_t number = yyjson_get_uint(value);
        if (number > INT_MAX) {
            return false;
        }
        *out = (int)number;
        return true;
    }
    return false;
}

static bool string_value(yyjson_val *value, const char **out)
{
    if (!yyjson_is_str(value)) {
        return false;
    }
    *out = yyjson_get_str(value);
    return true;
}

static int type_from_value(yyjson_val *value, pin_type *type, const char **type_name)
{
    *type_name = NULL;
    if (yyjson_is_uint(value) || yyjson_is_sint(value)) {
        int number;
        if (!value_nonnegative_int(value, &number) || number < PIN_FLOW ||
            number > PIN_ENUM) {
            return -1;
        }
        *type = (pin_type)number;
        return 0;
    }
    const char *name = NULL;
    if (!string_value(value, &name)) {
        return -1;
    }
    if (!strcmp(name, "float")) {
        *type = PIN_F32;
        return 0;
    }
    if (!strcmp(name, "double")) {
        *type = PIN_F64;
        return 0;
    }
    if (!strcmp(name, "int")) {
        *type = PIN_I32;
        return 0;
    }
    if (!strcmp(name, "uint")) {
        *type = PIN_U32;
        return 0;
    }
    for (int i = PIN_FLOW; i <= PIN_ENUM; i++) {
        const char *candidate = pin_type_name(i);
        if (candidate && !strcmp(name, candidate)) {
            *type = (pin_type)i;
            return 0;
        }
    }
    if (registry_type_by_name(name)) {
        *type = PIN_STRUCT;
        *type_name = name;
        return 0;
    }
    return -1;
}

static int container_from_value(yyjson_val *value, pin_container *container)
{
    if (!value) {
        *container = PIN_CONTAINER_SINGLE;
        return 0;
    }
    if (yyjson_is_uint(value) || yyjson_is_sint(value)) {
        int number;
        if (!value_nonnegative_int(value, &number) || number > PIN_CONTAINER_MAP) {
            return -1;
        }
        *container = (pin_container)number;
        return 0;
    }
    const char *name = NULL;
    if (!string_value(value, &name)) {
        return -1;
    }
    if (!strcmp(name, "single")) {
        *container = PIN_CONTAINER_SINGLE;
    } else if (!strcmp(name, "array")) {
        *container = PIN_CONTAINER_ARRAY;
    } else if (!strcmp(name, "set")) {
        *container = PIN_CONTAINER_SET;
    } else if (!strcmp(name, "map")) {
        *container = PIN_CONTAINER_MAP;
    } else {
        return -1;
    }
    return 0;
}

static behavior_domain domain_from_kind(const char *kind)
{
    if (!kind || !strcmp(kind, "behavior") || !strcmp(kind, "system")) {
        return BEHAVIOR_DOMAIN_EVENT;
    }
    if (!strcmp(kind, "event")) {
        return BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    }
    if (!strcmp(kind, "library")) {
        return BEHAVIOR_DOMAIN_LIBRARY;
    }
    if (!strcmp(kind, "material") || !strcmp(kind, "shader")) {
        return BEHAVIOR_DOMAIN_MATERIAL;
    }
    if (!strcmp(kind, "field")) {
        return BEHAVIOR_DOMAIN_FIELD;
    }
    if (!strcmp(kind, "model")) {
        return BEHAVIOR_DOMAIN_MODEL;
    }
    if (!strcmp(kind, "shape")) {
        return BEHAVIOR_DOMAIN_SHAPE;
    }
    if (!strcmp(kind, "shader-library")) {
        return BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    }
    return (behavior_domain)-1;
}

static int is_shader_domain(const behavior_asset *asset)
{
    return asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
           asset->domain == BEHAVIOR_DOMAIN_SHAPE;
}

static int asset_has_external_graph_reference(const behavior_asset *asset)
{
    for (int graph_index = 0; graph_index < acount(asset->graphs); graph_index++) {
        const graph *g = asset->graphs[graph_index];
        for (int node_index = 0; node_index < acount(g->nodes); node_index++) {
            const node *n = g->nodes[node_index];
            if (n->reference_asset &&
                (n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
                 n->desc == &NODE_BehaviorExpression || n->desc == &NODE_CallMacro)) {
                return 1;
            }
        }
    }
    return 0;
}

static const graph *reference_graph(const edit_context *ctx, uint64_t id, const char *asset_name)
{
    if (!asset_name || !asset_name[0]) {
        return behavior_asset_find_graph(ctx->asset, id);
    }
    for (int i = 0; i < ctx->library_count; i++) {
        const behavior_asset *library = ctx->libraries[i];
        if (library && library->name && !strcmp(library->name, asset_name)) {
            return behavior_asset_find_graph(library, id);
        }
    }
    return NULL;
}

static int refresh_call_reference(edit_context *ctx, node *call, uint64_t reference_id, const char *reference_asset)
{
    if (call->desc != &NODE_CallFunction && call->desc != &NODE_CallPureFunction &&
        call->desc != &NODE_BehaviorExpression && call->desc != &NODE_CallMacro) {
        return 0;
    }
    const graph *target = reference_graph(ctx, reference_id, reference_asset);
    if (!reference_id && call->reference_name) {
        const behavior_asset *owner = ctx->asset;
        if (reference_asset) {
            owner = NULL;
            for (int i = 0; i < ctx->library_count; i++) {
                if (!strcmp(ctx->libraries[i]->name, reference_asset)) {
                    owner = ctx->libraries[i];
                    break;
                }
            }
        }
        target = owner ? behavior_asset_find_graph_by_name(owner, call->reference_name)
                       : NULL;
        if (!target && !reference_asset) {
            for (int i = 0; i < acount(ctx->graphs); i++) {
                if (!strcmp(ctx->graphs[i].name, call->reference_name)) {
                    target = ctx->graphs[i].graph;
                    break;
                }
            }
        }
    }
    if (!target) {
        set_error(ctx, "referenced graph %llu does not exist", (unsigned long long)reference_id);
        return -1;
    }
    if (graph_set_node_reference(ctx->graph, call, target->graph_id, target->name)) {
        set_error(ctx, "could not apply function reference");
        return -1;
    }
    const char *error = graph_library_refresh_call(ctx->graph, call, target);
    if (error) {
        set_error(ctx, "%s", error);
        return -1;
    }
    return 0;
}

static int refresh_event(edit_context *ctx, node *n)
{
    if (!n->reference_name || (n->desc != &NODE_Event && n->desc != &NODE_CallEvent &&
                               strcmp(n->desc->name, "Send Event"))) {
        return 0;
    }
    if (n->desc == &NODE_Event && graph_host_event_apply(ctx->graph, n)) {
        set_error(ctx, "could not apply host event payload");
        return -1;
    }
    for (int i = 0; i < ctx->library_count; i++) {
        const behavior_asset *event = ctx->libraries[i];
        if (event->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION &&
            !strcmp(event->name, n->reference_name)) {
            if (graph_event_apply(ctx->graph, n, event)) {
                set_error(ctx, "event signature changed; disconnect incompatible payload pins first");
                return -1;
            }
            break;
        }
    }
    return 0;
}

static node *node_by_selector(edit_context *ctx, yyjson_val *selector)
{
    if (yyjson_is_uint(selector)) {
        uint64_t id = yyjson_get_uint(selector);
        node *n = graph_node_by_id(ctx->graph, id);
        if (!n) {
            set_error(ctx, "node id %llu does not exist in graph", (unsigned long long)id);
        }
        return n;
    }
    const char *name = NULL;
    if (!string_value(selector, &name)) {
        set_error(ctx, "node selector must be an integer ID or string ref");
        return NULL;
    }
    for (int i = 0; i < acount(ctx->nodes); i++) {
        if (!strcmp(ctx->nodes[i].name, name)) {
            if (graph_node_by_id(ctx->graph, ctx->nodes[i].id) != ctx->nodes[i].node) {
                set_error(ctx, "node ref '%s' belongs to another graph; select that graph first", name);
                return NULL;
            }
            return ctx->nodes[i].node;
        }
    }
    set_error(ctx, "unknown node ref '%s'", name);
    return NULL;
}

static behavior_variable *variable_by_selector(edit_context *ctx, yyjson_val *selector)
{
    if (yyjson_is_uint(selector)) {
        behavior_variable *variable = behavior_asset_find_variable_by_id(
            ctx->asset,
            yyjson_get_uint(selector)
        );
        if (!variable) {
            set_error(ctx, "variable id %llu does not exist", (unsigned long long)yyjson_get_uint(selector));
        }
        return variable;
    }
    const char *name = NULL;
    if (!string_value(selector, &name)) {
        set_error(ctx, "variable selector must be an integer ID or string name");
        return NULL;
    }
    for (int i = 0; i < acount(ctx->variables); i++) {
        if (!strcmp(ctx->variables[i].name, name)) {
            behavior_variable *variable = behavior_asset_find_variable_by_id(
                ctx->asset,
                ctx->variables[i].id
            );
            if (!variable) {
                set_error(ctx, "variable ref '%s' no longer exists", name);
            }
            return variable;
        }
    }
    behavior_variable *variable = behavior_asset_find_variable_by_name(ctx->asset, name);
    if (!variable) {
        set_error(ctx, "unknown variable '%s'", name);
    }
    return variable;
}

static int pin_index(edit_context *ctx, node *n, yyjson_val *selector, int output, const pin_spec **pin)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (yyjson_is_uint(selector) || yyjson_is_sint(selector)) {
        int index;
        if (!value_nonnegative_int(selector, &index) || index >= count) {
            set_error(ctx, "%s pin index must be between 0 and %d", output ? "output" : "input", count - 1);
            return -1;
        }
        *pin = &pins[index];
        return index;
    }
    const char *name = NULL;
    if (!string_value(selector, &name)) {
        set_error(ctx, "%s pin selector must be a name or zero-based index", output ? "output" : "input");
        return -1;
    }
    int found = -1;
    for (int i = 0; i < count; i++) {
        if (pins[i].name && !strcmp(pins[i].name, name)) {
            if (found >= 0) {
                set_error(ctx, "%s pin name '%s' is ambiguous", output ? "output" : "input", name);
                return -1;
            }
            found = i;
        }
    }
    /* Documents print an exposed field as Value.current. */
    if (found < 0 && strchr(name, '.')) {
        found = serialize_pin_by_key(n, output, name);
    }
    /* Naming a field Set Component can write shows that field input. */
    if (found < 0 && !output && component_node_kind_of(n->desc) == COMPONENT_NODE_SET &&
        component_node_show_field(ctx->graph, n, name, true) == 0) {
        return pin_index(ctx, n, selector, output, pin);
    }
    if (found < 0) {
        /* Naming the pins lets the caller correct the name in one retry. */
        char names[160] = "";
        size_t used = 0;
        for (int i = 0; i < count && used < sizeof names; i++) {
            if (pins[i].name) {
                used += (size_t)snprintf(
                    names + used,
                    sizeof names - used,
                    "%s%s",
                    used ? ", " : "",
                    pins[i].name
                );
            }
        }
        const char *side = output ? "output" : "input";
        set_error(ctx, "unknown %s pin '%s'; %ss: %s", side, name, side, names);
        return -1;
    }
    *pin = &pins[found];
    return found;
}

static int add_node_ref(edit_context *ctx, yyjson_val *ref, node *n)
{
    if (!ref) {
        return 0;
    }
    const char *name = NULL;
    if (!string_value(ref, &name) || !name[0]) {
        set_error(ctx, "node ref must be a non-empty string");
        return -1;
    }
    if (strlen(name) > 64) {
        set_error(ctx, "node ref '%s' is too long", name);
        return -1;
    }
    if (acount(ctx->nodes) >= GRAPH_EDIT_MAX_REFS) {
        set_error(ctx, "edit batch has too many node refs");
        return -1;
    }
    for (int i = 0; i < acount(ctx->nodes); i++) {
        if (!strcmp(ctx->nodes[i].name, name)) {
            set_error(ctx, "duplicate node ref '%s'", name);
            return -1;
        }
    }
    apush(ctx->nodes, ((node_ref){ .name = name, .id = n->id, .node = n }));
    return 0;
}

static int add_node_ref_name(edit_context *ctx, const char *name, node *n)
{
    if (!name || !name[0]) {
        set_error(ctx, "node ref must be a non-empty string");
        return -1;
    }
    if (strlen(name) > 64) {
        set_error(ctx, "node ref '%s' is too long", name);
        return -1;
    }
    if (acount(ctx->nodes) >= GRAPH_EDIT_MAX_REFS) {
        set_error(ctx, "edit batch has too many node refs");
        return -1;
    }
    for (int i = 0; i < acount(ctx->nodes); i++) {
        if (!strcmp(ctx->nodes[i].name, name)) {
            set_error(ctx, "duplicate node ref '%s'", name);
            return -1;
        }
    }
    apush(ctx->nodes, ((node_ref){ .name = name, .id = n->id, .node = n }));
    return 0;
}

static int add_generated_node_ref(
    edit_context *ctx,
    graph *graph,
    const char *prefix,
    const char *suffix,
    node *n
)
{
    char name[96];
    int length = snprintf(name, sizeof name, "%s.%s", prefix, suffix);
    if (length < 0 || (size_t)length >= sizeof name) {
        set_error(ctx, "generated node ref is too long");
        return -1;
    }
    char *owned = graph_own_str(graph, name, (size_t)length);
    if (!owned) {
        set_error(ctx, "could not allocate generated node ref");
        return -1;
    }
    return add_node_ref_name(ctx, owned, n);
}

static int add_graph_ref(edit_context *ctx, yyjson_val *ref, graph *g)
{
    if (!ref) {
        return 0;
    }
    const char *name = NULL;
    if (!string_value(ref, &name) || !name[0]) {
        set_error(ctx, "graph ref must be a non-empty string");
        return -1;
    }
    if (acount(ctx->graphs) >= GRAPH_EDIT_MAX_REFS) {
        set_error(ctx, "edit batch has too many graph refs");
        return -1;
    }
    for (int i = 0; i < acount(ctx->graphs); i++) {
        if (!strcmp(ctx->graphs[i].name, name)) {
            set_error(ctx, "duplicate graph ref '%s'", name);
            return -1;
        }
    }
    apush(ctx->graphs, ((graph_ref){ .name = name, .id = g->graph_id, .graph = g }));
    return 0;
}

static int add_variable_ref(edit_context *ctx, yyjson_val *ref, behavior_variable *variable)
{
    if (!ref) {
        return 0;
    }
    const char *name = NULL;
    if (!string_value(ref, &name) || !name[0]) {
        set_error(ctx, "variable ref must be a non-empty string");
        return -1;
    }
    for (int i = 0; i < acount(ctx->variables); i++) {
        if (!strcmp(ctx->variables[i].name, name)) {
            set_error(ctx, "duplicate variable ref '%s'", name);
            return -1;
        }
    }
    apush(ctx->variables, ((variable_ref){ .name = name, .id = variable->id }));
    return 0;
}

static int parse_pin_value(
    edit_context *ctx,
    const pin_spec *pin,
    yyjson_val *json,
    yyjson_val *type_json,
    pin_value *value
)
{
    pin_type type = pin->type;
    const char *type_name = pin->type_name;
    if (type_json) {
        if (type_from_value(type_json, &type, &type_name)) {
            set_error(ctx, "invalid input type");
            return -1;
        }
        if (type == PIN_STRUCT && !type_name) {
            type_name = pin->type_name;
        }
    }
    int read_error = pin->container >= PIN_CONTAINER_ARRAY
                       ? request_read_array(type, type_name, json, value, ctx->graph)
                       : request_read_value(type, type_name, json, value, ctx->graph);
    if (read_error) {
        set_error(
            ctx,
            "value does not match %s pin '%s'",
            pin_type_name(pin->type),
            pin->name ? pin->name : "?"
        );
        return -1;
    }
    if (!(pin->flags & PIN_FLAG_WILDCARD) &&
        (value->type != pin->type ||
         (value->type == PIN_STRUCT && (value->type_name || pin->type_name) &&
          (!value->type_name || !pin->type_name ||
           strcmp(value->type_name, pin->type_name))))) {
        set_error(ctx, "value type does not match pin '%s'", pin->name ? pin->name : "?");
        return -1;
    }
    return 0;
}

/* Model instance inputs are sparse authored overrides. An omitted parameter
 * inherits its definition's current default, including after that default changes. */
static int model_override(edit_context *ctx, node *instance, const char *name)
{
    for (int i = 0; i < instance->input_count; i++) {
        if (!strcmp(instance->input_pins[i].name, name)) {
            return i;
        }
    }
    const behavior_asset *target = NULL;
    for (int i = -1; i < ctx->library_count; i++) {
        const behavior_asset *candidate = i < 0 ? ctx->asset : ctx->libraries[i];
        if (candidate->domain == BEHAVIOR_DOMAIN_MODEL && instance->reference_asset &&
            !strcmp(candidate->name, instance->reference_asset)) {
            target = candidate;
            break;
        }
    }
    shader_error error = { 0 };
    int pin = model_instance_override(ctx->graph, instance, target, name, &error);
    if (pin < 0) {
        set_error(ctx, "%s", error.message);
    }
    return pin;
}
static int model_anchor_output(edit_context *ctx, node *instance, const char *name)
{
    const behavior_asset *target = NULL;
    for (int i = -1; i < ctx->library_count; i++) {
        const behavior_asset *candidate = i < 0 ? ctx->asset : ctx->libraries[i];
        if (candidate->domain == BEHAVIOR_DOMAIN_MODEL && instance->reference_asset &&
            !strcmp(candidate->name, instance->reference_asset)) {
            target = candidate;
            break;
        }
    }
    shader_error error = { 0 };
    int pin = model_instance_anchor(ctx->graph, instance, target, name, &error);
    if (pin < 0) {
        set_error(ctx, "%s", error.message);
    }
    return pin;
}

static int set_input(edit_context *ctx, node *n, yyjson_val *pin_json, yyjson_val *value_json, yyjson_val *type_json)
{
    if (!strcmp(n->desc->name, "Model Instance") && yyjson_is_str(pin_json) &&
        model_override(ctx, n, yyjson_get_str(pin_json)) < 0) {
        return -1;
    }
    const pin_spec *pin = NULL;
    int index = pin_index(ctx, n, pin_json, 0, &pin);
    if (index < 0) {
        return -1;
    }
    if (pin->kind != PIN_DATA) {
        set_error(ctx, "input pin '%s' is an execution pin", pin->name ? pin->name : "?");
        return -1;
    }
    pin_value value;
    if (parse_pin_value(ctx, pin, value_json, type_json, &value)) {
        return -1;
    }
    if ((value.type != pin->type ||
         (value.type == PIN_STRUCT && (value.type_name || pin->type_name) &&
          (!value.type_name || !pin->type_name ||
           strcmp(value.type_name, pin->type_name)))) &&
        (pin->flags & PIN_FLAG_WILDCARD) &&
        graph_set_pin_type(ctx->graph, n, 0, index, value.type, value.type_name)) {
        set_error(ctx, "could not resolve wildcard input pin '%s'", pin->name ? pin->name : "?");
        return -1;
    }
    return graph_set_pin_default(ctx->graph, n, 0, index, &value);
}

static int set_output(edit_context *ctx, node *n, yyjson_val *op)
{
    if (n->desc->pure_kind != NODE_PURE_CONST) {
        set_error(ctx, "set_output only applies to constant nodes");
        return -1;
    }
    yyjson_val *value_json = yyjson_obj_get(op, "value");
    if (!value_json) {
        set_error(ctx, "set_output.value is required");
        return -1;
    }
    const pin_spec *pin = NULL;
    int index = pin_index(ctx, n, yyjson_obj_get(op, "pin"), 1, &pin);
    if (index < 0) {
        return -1;
    }
    if (pin->kind != PIN_DATA) {
        set_error(ctx, "output pin '%s' is an execution pin", pin->name ? pin->name : "?");
        return -1;
    }
    pin_value value;
    if (parse_pin_value(ctx, pin, value_json, yyjson_obj_get(op, "type"), &value)) {
        return -1;
    }
    if ((value.type != pin->type ||
         (value.type == PIN_STRUCT && (value.type_name || pin->type_name) &&
          (!value.type_name || !pin->type_name ||
           strcmp(value.type_name, pin->type_name)))) &&
        (pin->flags & PIN_FLAG_WILDCARD) &&
        graph_set_pin_type(ctx->graph, n, 1, index, value.type, value.type_name)) {
        set_error(ctx, "could not resolve wildcard output pin '%s'", pin->name ? pin->name : "?");
        return -1;
    }
    return graph_set_pin_default(ctx->graph, n, 1, index, &value);
}

static bool names_setting(const node *n, const char *name)
{
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *pin = &n->input_pins[i];
        if ((pin->flags & PIN_FLAG_SETTING) && pin->name && !strcmp(pin->name, name)) {
            return true;
        }
    }
    return false;
}

static int add_node_inputs(edit_context *ctx, node *n, yyjson_val *inputs)
{
    if (!inputs) {
        return 0;
    }
    if (!yyjson_is_obj(inputs)) {
        set_error(ctx, "inputs must be an object keyed by pin name");
        return -1;
    }
    /* Settings shape the other pins, so they apply first in any order. */
    for (int pass = 0; pass < 2; pass++) {
        yyjson_obj_iter iterator = yyjson_obj_iter_with(inputs);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&iterator))) {
            if (names_setting(n, yyjson_get_str(key)) != (pass == 0)) {
                continue;
            }
            yyjson_val *value = yyjson_obj_iter_get_val(key);
            if (set_input(ctx, n, key, value, NULL)) {
                return -1;
            }
        }
    }
    return 0;
}

static int operation_add_node(edit_context *ctx, yyjson_val *op)
{
    const char *type = NULL;
    if (!string_value(yyjson_obj_get(op, "type"), &type) || !type[0]) {
        set_error(ctx, "add_node.type is required");
        return -1;
    }
    const node_desc *desc = registry_node_by_name(type);
    if (!desc) {
        set_error(ctx, "unknown node type '%s'", type);
        return -1;
    }
    yyjson_val *source_text = yyjson_obj_get(op, "source");
    if (desc == &NODE_BehaviorExpression) {
        const char *source = NULL;
        if (!source_text || !string_value(source_text, &source) || !source[0]) {
            set_error(ctx, "Behavior Expression add_node.source is required");
            return -1;
        }
        behavior_expression_error error = { 0 };
        node *expression = behavior_expression_add(ctx->asset, ctx->graph, source, &error);
        if (!expression) {
            set_error(ctx, "%s", error.message[0] ? error.message : "could not add Behavior Expression");
            return -1;
        }
        if (add_node_ref(ctx, yyjson_obj_get(op, "ref"), expression) ||
            add_node_inputs(ctx, expression, yyjson_obj_get(op, "inputs"))) {
            return -1;
        }
        return 0;
    }
    node *n = graph_add_node(ctx->graph, desc);
    if (!n) {
        set_error(ctx, "could not add node '%s'", type);
        return -1;
    }
    yyjson_val *event_name = yyjson_obj_get(op, "event_name");
    if (event_name &&
        (!yyjson_is_str(event_name) ||
         graph_set_event_name(ctx->graph, n, yyjson_get_str(event_name)))) {
        set_error(ctx, "event_name requires an Event node and a valid event name");
        return -1;
    }
    if (type[0] == 'S' && !strncmp(type, "Shader ", 7) &&
        !shader_graph_init_node(ctx->graph, n)) {
        set_error(ctx, "could not initialize shader node '%s'", type);
        return -1;
    }
    /* Another Return in a function returns the same values as the first. */
    const node *first = desc == &NODE_Return
                          ? graph_node_by_id(ctx->graph, ctx->graph->return_id)
                          : NULL;
    for (int i = 0; first && i < first->input_count; i++) {
        const pin_spec *value = &first->input_pins[i];
        pin_spec pin = { .name = value->name,
                         .type = value->type,
                         .type_name = value->type_name,
                         .kind = value->kind,
                         .container = value->container,
                         .default_value = { .type = value->type,
                                            .type_name = value->type_name } };
        if (value->kind == PIN_DATA && graph_add_pin(ctx->graph, n, 0, &pin) < 0) {
            set_error(ctx, "could not add Return pin '%s'", value->name);
            return -1;
        }
    }
    yyjson_val *reference_id = yyjson_obj_get(op, "reference_id");
    yyjson_val *reference_name = yyjson_obj_get(op, "reference_name");
    yyjson_val *reference_asset_json = yyjson_obj_get(op, "reference_asset");
    if (reference_id || reference_name || reference_asset_json) {
        uint64_t id = 0;
        const char *reference_name_value = NULL;
        if (reference_id && !value_uint(reference_id, &id)) {
            set_error(ctx, "add_node.reference_id must be an unsigned integer");
            return -1;
        }
        if (reference_name && !string_value(reference_name, &reference_name_value)) {
            set_error(ctx, "add_node.reference_name must be a string");
            return -1;
        }
        if (graph_set_node_reference(ctx->graph, n, id, reference_name_value)) {
            set_error(ctx, "could not set node reference");
            return -1;
        }
        if (reference_asset_json) {
            const char *reference_asset = NULL;
            if (!string_value(reference_asset_json, &reference_asset)) {
                set_error(ctx, "add_node.reference_asset must be a string");
                return -1;
            }
            n->reference_asset = graph_own_str(ctx->graph, reference_asset, strlen(reference_asset));
        }
        if (refresh_call_reference(ctx, n, id, n->reference_asset)) {
            return -1;
        }
    } else if (reference_asset_json) {
        set_error(ctx, "add_node.reference_asset requires reference_id");
        return -1;
    }
    if (refresh_event(ctx, n) || add_node_ref(ctx, yyjson_obj_get(op, "ref"), n)) {
        return -1;
    }
    if (add_node_inputs(ctx, n, yyjson_obj_get(op, "inputs"))) {
        return -1;
    }
    return 0;
}

static int operation_set_expression(edit_context *ctx, yyjson_val *op)
{
    node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
    const char *source = NULL;
    if (!n || n->desc != &NODE_BehaviorExpression ||
        !string_value(yyjson_obj_get(op, "source"), &source) || !source[0]) {
        set_error(ctx, "set_expression requires a Behavior Expression node and source text");
        return -1;
    }
    char **lost = behavior_expression_fed_inputs(ctx->graph, n);
    behavior_expression_error error = { 0 };
    int result = behavior_expression_set(ctx->asset, n, source, &error);
    if (result != 0) {
        set_error(ctx, "%s", error.message[0] ? error.message : "could not set Behavior Expression source");
    }
    behavior_expression_keep_unfed(ctx->graph, n, &lost);
    for (int i = 0; !result && i < acount(lost); i++) {
        yyjson_mut_val *dropped = yyjson_mut_obj_get(ctx->result, "unfed_inputs");
        dropped = dropped
                    ? dropped
                    : yyjson_mut_obj_add_arr(ctx->doc, ctx->result, "unfed_inputs");
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(ctx->doc, dropped);
        yyjson_mut_obj_add_int(ctx->doc, entry, "operation", ctx->operation);
        yyjson_mut_obj_add_uint(ctx->doc, entry, "node", n->id);
        yyjson_mut_obj_add_strcpy(ctx->doc, entry, "input", lost[i]);
    }
    behavior_expression_free_names(lost);
    return result;
}

static int operation_remove_node(edit_context *ctx, yyjson_val *op)
{
    node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
    if (!n) {
        return -1;
    }
    int result = graph_remove_node(ctx->graph, n);
    if (result) {
        set_error(ctx, "could not remove node %llu", (unsigned long long)n->id);
        return -1;
    }
    for (int i = acount(ctx->nodes) - 1; i >= 0; i--) {
        if (ctx->nodes[i].node == n) {
            adel(ctx->nodes, i);
        }
    }
    return 0;
}

static int refresh_expression(edit_context *ctx, node *n)
{
    graph *generated = n->desc == &NODE_CallPureFunction || n->desc == &NODE_BehaviorExpression
                         ? behavior_asset_find_graph(ctx->asset, n->reference_id)
                         : NULL;
    if (!generated || !generated->math_expression) {
        return 0;
    }
    behavior_expression_error error = { 0 };
    if (behavior_expression_refresh(ctx->asset, n, &error) != 0) {
        set_error(ctx, "%s", error.message[0] ? error.message : "could not refresh behavior expression");
        return -1;
    }
    return 0;
}

/* A wire into an expression changes its input's type, and one out of it can
   resolve its result, so both ends regenerate. */
static int refresh_expression_connection(edit_context *ctx, node *from, node *to)
{
    return refresh_expression(ctx, to) || refresh_expression(ctx, from) ? -1 : 0;
}

/* "Equal.Result (bool)": a pin by its node, name and type. */
static void describe_pin(char *text, size_t size, const node *n, const pin_spec *pin)
{
    const char *type = pin->type_name && pin->type_name[0] ? pin->type_name
                     : pin->kind == PIN_EXEC ? "exec"
                                             : pin_type_name(pin->type);
    snprintf(text, size, "%s.%s (%s)", n->desc->name, pin->name ? pin->name : "?", type);
}

/* What to try when a data connection has no way to carry its value. */
static const char *rejection_hint(const edit_context *ctx, const pin_spec *from, const pin_spec *to)
{
    bool shader = ctx->asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                  ctx->asset->domain == BEHAVIOR_DOMAIN_SHAPE;
    if (shader && ((from->flags | to->flags) & PIN_FLAG_WILDCARD)) {
        return "shader wildcard pins need an explicit type such as ShaderVec3 "
               "or a fixed typed shader node";
    }
    if (shader && !pin_compatible(from, to)) {
        return "shader pins need matching types; use a shader vector or math "
               "node for conversion";
    }
    return pin_compatible(from, to) ? "" : "the pin types differ and no conversion joins them";
}

static bool wire_present(const graph *g, const node *from, uint64_t from_pin, const node *to, uint64_t to_pin)
{
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind == GRAPH_EDGE_DATA && e->from_id == from->id &&
            e->from_pin_id == from_pin && e->to_id == to->id && e->to_pin_id == to_pin) {
            return true;
        }
    }
    return false;
}

static int operation_connect(edit_context *ctx, yyjson_val *op, bool disconnect)
{
    node *from = node_by_selector(ctx, yyjson_obj_get(op, "from"));
    node *to = node_by_selector(ctx, yyjson_obj_get(op, "to"));
    if (!from || !to) {
        return -1;
    }
    const pin_spec *from_pin = NULL;
    const pin_spec *to_pin = NULL;
    int from_index = pin_index(ctx, from, yyjson_obj_get(op, "from_pin"), 1, &from_pin);
    int to_index = pin_index(ctx, to, yyjson_obj_get(op, "to_pin"), 0, &to_pin);
    if (from_index < 0 || to_index < 0) {
        return -1;
    }
    yyjson_val *kind_json = yyjson_obj_get(op, "kind");
    const char *kind = NULL;
    if (!kind_json || !string_value(kind_json, &kind)) {
        kind = from_pin->kind == PIN_EXEC || to_pin->kind == PIN_EXEC ? "exec" : "data";
    }
    if (strcmp(kind, "data") && strcmp(kind, "exec")) {
        set_error(ctx, "connection kind must be 'data' or 'exec'");
        return -1;
    }
    if (!strcmp(kind, "data")) {
        if (from_pin->kind != PIN_DATA || to_pin->kind != PIN_DATA) {
            set_error(ctx, "data connection requires data pins");
            return -1;
        }
        /* The pins are named as they were asked for, before a wildcard or an
           expression retypes them. */
        char wire[320], to_text[160];
        describe_pin(wire, sizeof wire, from, from_pin);
        describe_pin(to_text, sizeof to_text, to, to_pin);
        size_t used = strlen(wire);
        snprintf(wire + used, sizeof wire - used, " to %s", to_text);
        const char *hint = rejection_hint(ctx, from_pin, to_pin);
        uint64_t from_id = from_pin->id, to_id = to_pin->id;
        yyjson_val *type_json = yyjson_obj_get(op, "type");
        if (type_json && !disconnect) {
            pin_type type;
            const char *type_name = NULL;
            if (type_from_value(type_json, &type, &type_name) ||
                type == PIN_FLOW || (type == PIN_STRUCT && !type_name)) {
                set_error(ctx, "data connection type must be a scalar or named struct");
                return -1;
            }
            if (!(from_pin->flags & PIN_FLAG_WILDCARD)) {
                pin_spec requested = *from_pin;
                requested.type = type;
                requested.type_name = type_name;
                if (!pin_compatible(from_pin, &requested)) {
                    set_error(
                        ctx,
                        "explicit connection type does not match source pin "
                        "'%s'",
                        from_pin->name ? from_pin->name : "?"
                    );
                    return -1;
                }
            }
            if (!(to_pin->flags & PIN_FLAG_WILDCARD)) {
                pin_spec requested = *to_pin;
                requested.type = type;
                requested.type_name = type_name;
                if (!pin_compatible(to_pin, &requested)) {
                    set_error(
                        ctx,
                        "explicit connection type does not match destination "
                        "pin '%s'",
                        to_pin->name ? to_pin->name : "?"
                    );
                    return -1;
                }
            }
            if ((from_pin->flags & PIN_FLAG_WILDCARD) &&
                graph_set_pin_type(ctx->graph, from, 1, from_index, type, type_name)) {
                set_error(
                    ctx,
                    "could not resolve source wildcard pin '%s'",
                    from_pin->name ? from_pin->name : "?"
                );
                return -1;
            }
            if ((to_pin->flags & PIN_FLAG_WILDCARD) &&
                graph_set_pin_type(ctx->graph, to, 0, to_index, type, type_name)) {
                set_error(
                    ctx,
                    "could not resolve destination wildcard pin '%s'",
                    to_pin->name ? to_pin->name : "?"
                );
                return -1;
            }
        }
        const node_desc *conversion = NULL;
        if (!disconnect && !(from_pin->flags & PIN_FLAG_WILDCARD) &&
            !(to_pin->flags & PIN_FLAG_WILDCARD) &&
            ctx->asset->domain != BEHAVIOR_DOMAIN_MATERIAL &&
            ctx->asset->domain != BEHAVIOR_DOMAIN_SHAPE &&
            ctx->asset->domain != BEHAVIOR_DOMAIN_MODEL) {
            conversion = conversion_for_pins(from_pin, to_pin);
        }
        int result = disconnect
                       ? graph_remove_edge(ctx->graph, from, from_index, to, to_index)
                   : conversion
                       ? graph_connect_data_conversion(ctx->graph, from, from_index, to, to_index, conversion)
                       : graph_connect_data(ctx->graph, from, from_index, to, to_index);
        if (result) {
            set_error(
                ctx,
                "data connection %s: %s%s%s",
                disconnect ? "was not present" : "was rejected",
                wire,
                hint[0] ? "; " : "",
                hint
            );
            return -1;
        }
        /* A named output is read by name, like the consumers it already has. */
        const graph_local *local = disconnect
                                     ? NULL
                                     : graph_output_local(ctx->graph, from, from_index);
        pin_binding binding = { .kind = PIN_BINDING_LOCAL,
                                .local_id = local ? local->id : 0 };
        if (local && graph_set_pin_binding(ctx->graph, to, 0, to_index, &binding)) {
            set_error(ctx, "could not read named value '%s'", local->name);
            return -1;
        }
        if (refresh_expression_connection(ctx, from, to) != 0) {
            return -1;
        }
        if (!disconnect && !wire_present(ctx->graph, from, from_id, to, to_id)) {
            set_error(ctx, "data connection was dropped when its expression regenerated: %s", wire);
            return -1;
        }
        return 0;
    }
    if (from_pin->kind != PIN_EXEC || to_pin->kind != PIN_EXEC) {
        set_error(ctx, "exec connection requires execution pins");
        return -1;
    }
    int order = 0;
    yyjson_val *order_json = yyjson_obj_get(op, "order");
    if (order_json && !value_nonnegative_int(order_json, &order)) {
        set_error(ctx, "exec connection order must be a nonnegative integer");
        return -1;
    }
    if (disconnect) {
        if (graph_remove_edge(ctx->graph, from, from_index, to, to_index)) {
            set_error(ctx, "exec connection was not present");
            return -1;
        }
        return 0;
    }
    if (graph_connect_exec(ctx->graph, from, from_index, to, to_index, order)) {
        set_error(ctx, "exec connection was rejected");
        return -1;
    }
    return 0;
}

static int operation_set_node(edit_context *ctx, yyjson_val *op)
{
    node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
    if (!n) {
        return -1;
    }
    yyjson_val *event_name = yyjson_obj_get(op, "event_name");
    if (event_name) {
        const char *name = NULL;
        if (!string_value(event_name, &name) ||
            graph_set_event_name(ctx->graph, n, name)) {
            set_error(ctx, "event_name can only rename an Event node");
            return -1;
        }
    }
    yyjson_val *reference_id = yyjson_obj_get(op, "reference_id");
    yyjson_val *reference_name = yyjson_obj_get(op, "reference_name");
    yyjson_val *reference_asset_json = yyjson_obj_get(op, "reference_asset");
    if (reference_id || reference_name || reference_asset_json) {
        uint64_t id = 0;
        const char *name = NULL;
        const char *reference_asset = NULL;
        if (reference_id && !value_uint(reference_id, &id)) {
            set_error(ctx, "reference_id must be an unsigned integer");
            return -1;
        }
        if (reference_name && !string_value(reference_name, &name)) {
            set_error(ctx, "reference_name must be a string");
            return -1;
        }
        if (reference_asset_json &&
            !string_value(reference_asset_json, &reference_asset)) {
            set_error(ctx, "reference_asset must be a string");
            return -1;
        }
        if (graph_set_node_reference(ctx->graph, n, id, name)) {
            set_error(ctx, "could not set node reference");
            return -1;
        }
        n->reference_asset = reference_asset
                               ? graph_own_str(ctx->graph, reference_asset, strlen(reference_asset))
                               : NULL;
        if (refresh_call_reference(ctx, n, id, n->reference_asset)) {
            return -1;
        }
    }
    yyjson_val *disabled = yyjson_obj_get(op, "disabled");
    if (disabled) {
        if (!yyjson_is_bool(disabled) ||
            graph_set_node_disabled(ctx->graph, n, yyjson_get_bool(disabled))) {
            set_error(ctx, "disabled must be a boolean");
            return -1;
        }
    }
    yyjson_val *presentation = yyjson_obj_get(op, "presentation");
    if (presentation) {
        int mode = graph_presentation_by_name(yyjson_get_str(presentation));
        if (mode < 0 ||
            graph_set_node_presentation(ctx->graph, n, (node_presentation)mode)) {
            set_error(ctx, "presentation must be auto, inline or expanded");
            return -1;
        }
    }
    return refresh_event(ctx, n);
}

/* A node comment: `comment` replaces the text ("" removes it) and `hidden`
   shows it only on hover; either one alone keeps the other. */
static int operation_set_comment(edit_context *ctx, yyjson_val *op)
{
    node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
    if (!n) {
        return -1;
    }
    yyjson_val *comment = yyjson_obj_get(op, "comment");
    yyjson_val *hidden = yyjson_obj_get(op, "hidden");
    if ((!comment && !hidden) || (comment && !yyjson_is_str(comment)) ||
        (hidden && !yyjson_is_bool(hidden))) {
        set_error(ctx, "set_comment needs a string comment or a boolean hidden");
        return -1;
    }
    const char *text = comment ? yyjson_get_str(comment) : n->comment;
    bool hide = hidden ? yyjson_get_bool(hidden) : n->comment_hidden;
    if (graph_set_node_comment(ctx->graph, n, text, hide)) {
        set_error(ctx, "could not set the node comment");
        return -1;
    }
    return 0;
}

static int shader_stop_value(edit_context *ctx, yyjson_val *json, float out[4])
{
    if (yyjson_is_arr(json)) {
        if (yyjson_arr_size(json) != 4) {
            set_error(ctx, "shader ramp color arrays must contain four numbers");
            return -1;
        }
        for (size_t i = 0; i < 4; i++) {
            yyjson_val *item = yyjson_arr_get(json, i);
            if (!yyjson_is_num(item)) {
                set_error(ctx, "shader ramp colors must contain numbers");
                return -1;
            }
            out[i] = (float)yyjson_get_num(item);
        }
        return 0;
    }
    if (!yyjson_is_obj(json)) {
        set_error(ctx, "shader ramp color must be an array or {x,y,z,w} object");
        return -1;
    }
    const char *names[] = { "x", "y", "z", "w" };
    for (int i = 0; i < 4; i++) {
        yyjson_val *item = yyjson_obj_get(json, names[i]);
        if (!item || !yyjson_is_num(item)) {
            set_error(ctx, "shader ramp color is missing numeric '%s'", names[i]);
            return -1;
        }
        out[i] = (float)yyjson_get_num(item);
    }
    return 0;
}

static int operation_set_ramp(edit_context *ctx, yyjson_val *op)
{
    node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
    if (!n) {
        return -1;
    }
    yyjson_val *stops_json = yyjson_obj_get(op, "stops");
    if (!stops_json || !yyjson_is_arr(stops_json) ||
        yyjson_arr_size(stops_json) > SHADER_RAMP_MAX) {
        set_error(ctx, "set_ramp.stops must contain between 2 and %d items", SHADER_RAMP_MAX);
        return -1;
    }
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    size_t count = yyjson_arr_size(stops_json);
    for (size_t i = 0; i < count; i++) {
        yyjson_val *stop = yyjson_arr_get(stops_json, i);
        if (!yyjson_is_obj(stop)) {
            set_error(ctx, "shader ramp stop must be an object");
            return -1;
        }
        yyjson_val *position = yyjson_obj_get(stop, "position");
        yyjson_val *color = yyjson_obj_get(stop, "color");
        if (!position || !yyjson_is_num(position) || !color) {
            set_error(ctx, "shader ramp stop requires numeric position and color");
            return -1;
        }
        if (shader_stop_value(ctx, color, stops[i].color)) {
            return -1;
        }
        stops[i].position = (float)yyjson_get_num(position);
    }
    if (!shader_node_ramp_set(ctx->graph, n, stops, (int)count)) {
        set_error(ctx, "node '%s' is not a valid shader color ramp", n->desc->name);
        return -1;
    }
    return 0;
}

static int operation_set_model_reference(edit_context *ctx, yyjson_val *op)
{
    yyjson_val *value = yyjson_obj_get(op, "value");
    if (yyjson_is_null(value)) {
        model_reference reference = { 0 };
        if (!behavior_asset_set_model_reference(ctx->asset, &reference)) {
            set_error(ctx, "set_model_reference is only valid for model assets");
            return -1;
        }
        return 0;
    }
    const char *image = yyjson_get_str(yyjson_obj_get(value, "image"));
    yyjson_val *center = yyjson_obj_get(value, "center");
    yyjson_val *span = yyjson_obj_get(value, "span");
    yyjson_val *angle = yyjson_obj_get(value, "angle");
    yyjson_val *locked = yyjson_obj_get(value, "locked");
    if (!image || !yyjson_is_arr(center) || yyjson_arr_size(center) != 2 ||
        !yyjson_is_num(yyjson_arr_get(center, 0)) ||
        !yyjson_is_num(yyjson_arr_get(center, 1)) || !yyjson_is_num(span) ||
        !yyjson_is_num(angle) || !yyjson_is_bool(locked)) {
        set_error(ctx, "set_model_reference.value requires image, center[2], positive span, angle radians and locked boolean; null clears it");
        return -1;
    }
    model_reference reference = {
        .image = (char *)image,
        .center = { (float)yyjson_get_num(yyjson_arr_get(center, 0)),
                    (float)yyjson_get_num(yyjson_arr_get(center, 1)) },
        .span = (float)yyjson_get_num(span),
        .angle = (float)yyjson_get_num(angle),
        .locked = yyjson_get_bool(locked)
    };
    if (!behavior_asset_set_model_reference(ctx->asset, &reference)) {
        set_error(ctx, "model reference requires a model asset, a content-relative image key and finite alignment with positive span");
        return -1;
    }
    return 0;
}

static int operation_set_bounds(edit_context *ctx, yyjson_val *op)
{
    if (ctx->asset->domain != BEHAVIOR_DOMAIN_SHAPE) {
        set_error(ctx, "set_bounds is only valid for shape assets");
        return -1;
    }
    yyjson_val *value = yyjson_obj_get(op, "value");
    if (!value || !yyjson_is_arr(value) || yyjson_arr_size(value) != 2) {
        set_error(ctx, "set_bounds.value must contain two positive numbers");
        return -1;
    }
    float bounds[2];
    for (int i = 0; i < 2; i++) {
        yyjson_val *item = yyjson_arr_get(value, (size_t)i);
        if (!item || !yyjson_is_num(item)) {
            set_error(ctx, "set_bounds.value must contain two positive numbers");
            return -1;
        }
        bounds[i] = (float)yyjson_get_num(item);
        if (!(bounds[i] > 0.0f) || !isfinite(bounds[i])) {
            set_error(ctx, "shape bounds must be finite positive half extents");
            return -1;
        }
    }
    ctx->asset->bounds[0] = bounds[0];
    ctx->asset->bounds[1] = bounds[1];
    return 0;
}

static int operation_add_graph(edit_context *ctx, yyjson_val *op)
{
    const char *name = NULL;
    if (!string_value(yyjson_obj_get(op, "name"), &name) || !name[0]) {
        set_error(ctx, "add_graph.name is required");
        return -1;
    }
    graph_kind graph_kind = GRAPH_FUNCTION;
    yyjson_val *kind_json = yyjson_obj_get(op, "kind");
    if (kind_json) {
        const char *kind = NULL;
        if (!string_value(kind_json, &kind) || graph_kind_from_name(kind, &graph_kind)) {
            set_error(ctx, "add_graph.kind must be event, function, or macro");
            return -1;
        }
    }
    int pure = 0;
    yyjson_val *pure_json = yyjson_obj_get(op, "pure");
    if (pure_json) {
        if (!yyjson_is_bool(pure_json)) {
            set_error(ctx, "add_graph.pure must be a boolean");
            return -1;
        }
        pure = yyjson_get_bool(pure_json);
    }
    graph *g = graph_kind == GRAPH_FUNCTION
                 ? behavior_asset_add_function(ctx->asset, name, pure)
             : graph_kind == GRAPH_MACRO
                 ? behavior_asset_add_macro(ctx->asset, name)
                 : behavior_asset_add_graph(ctx->asset, graph_kind, name);
    if (!g) {
        set_error(ctx, "could not add graph '%s'", name);
        return -1;
    }
    yyjson_val *ref = yyjson_obj_get(op, "ref");
    if (add_graph_ref(ctx, ref, g)) {
        return -1;
    }
    if (ref && g->entry_id && g->return_id) {
        const char *prefix = NULL;
        if (!string_value(ref, &prefix) ||
            add_generated_node_ref(ctx, g, prefix, "entry", graph_node_by_id(g, g->entry_id)) ||
            add_generated_node_ref(ctx, g, prefix, "return", graph_node_by_id(g, g->return_id))) {
            return -1;
        }
    }
    ctx->graph = g;
    if (add_layout_graph(ctx, g)) {
        return -1;
    }
    return 0;
}

/* Entry outputs and Return inputs declare a function's or macro's signature,
 * and edits there go through graph_signature so its calls follow. The side:
 * 0 an input the Entry declares, 1 a result on Return, -1 any other pin. */
static int signature_side(const edit_context *ctx, const node *n, bool output)
{
    const graph *g = ctx->graph;
    if (g->kind != GRAPH_FUNCTION && g->kind != GRAPH_MACRO) {
        return -1;
    }
    return output && n->id == g->entry_id   ? 0
         : !output && n->id == g->return_id ? 1
                                            : -1;
}

static int signature_refused(edit_context *ctx, const char *operation)
{
    set_error(ctx, "%s could not change the signature: names are unique, only macros take exec pins, and a paused call blocks edits", operation);
    return -1;
}

static int operation_add_pin(edit_context *ctx, yyjson_val *op)
{
    node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
    if (!n) {
        return -1;
    }
    if (component_node_kind_of(n->desc)) {
        set_error(
            ctx,
            "%s pins follow its Component; show or hide fields instead",
            n->desc->name
        );
        return -1;
    }
    const char *side = NULL;
    const char *name = NULL;
    if (!string_value(yyjson_obj_get(op, "side"), &side) ||
        (strcmp(side, "input") && strcmp(side, "output"))) {
        set_error(ctx, "add_pin.side must be input or output");
        return -1;
    }
    if (!string_value(yyjson_obj_get(op, "name"), &name) || !name[0]) {
        set_error(ctx, "add_pin.name is required");
        return -1;
    }
    if (!strcmp(n->desc->name, "Model Instance")) {
        if (!strcmp(side, "output")) {
            const char *type = yyjson_get_str(yyjson_obj_get(op, "type"));
            const char *named = yyjson_get_str(yyjson_obj_get(op, "type_name"));
            if ((type && strcmp(type, "handle") && strcmp(type, "Anchor3D")) ||
                (named && strcmp(named, "Anchor3D")) ||
                yyjson_obj_get(op, "default")) {
                set_error(ctx, "Model Instance anchor output requires Anchor3D and no literal default");
                return -1;
            }
            return model_anchor_output(ctx, n, name) < 0 ? -1 : 0;
        }
        int index = model_override(ctx, n, name);
        if (index < 0) {
            return -1;
        }
        const pin_spec *pin = &n->input_pins[index];
        pin_type type = pin->type;
        const char *type_name = pin->type_name;
        yyjson_val *requested = yyjson_obj_get(op, "type");
        yyjson_val *named = yyjson_obj_get(op, "type_name");
        pin_container container;
        if ((requested && type_from_value(requested, &type, &type_name)) ||
            (named && !string_value(named, &type_name)) ||
            container_from_value(yyjson_obj_get(op, "container"), &container) ||
            container != PIN_CONTAINER_SINGLE || type != pin->type ||
            ((type_name || pin->type_name) &&
             (!type_name || !pin->type_name || strcmp(type_name, pin->type_name)))) {
            set_error(ctx, "Model Instance override type must match declaration '%s'", name);
            return -1;
        }
        yyjson_val *value = yyjson_obj_get(op, "default");
        return value ? set_input(ctx, n, yyjson_obj_get(op, "name"), value, requested)
                     : 0;
    }
    pin_type type = PIN_FLOW;
    const char *type_name = NULL;
    yyjson_val *type_json = yyjson_obj_get(op, "type");
    if (type_json) {
        if (type_from_value(type_json, &type, &type_name)) {
            set_error(ctx, "add_pin.type is invalid");
            return -1;
        }
    }
    yyjson_val *type_name_json = yyjson_obj_get(op, "type_name");
    if (type_name_json && !string_value(type_name_json, &type_name)) {
        set_error(ctx, "add_pin.type_name must be a string");
        return -1;
    }
    /* A typed pin is data; only an untyped one defaults to execution. */
    pin_kind kind = type_json || type_name_json ? PIN_DATA : PIN_EXEC;
    yyjson_val *kind_json = yyjson_obj_get(op, "kind");
    if (kind_json) {
        const char *kind_name = NULL;
        if (!string_value(kind_json, &kind_name)) {
            set_error(ctx, "add_pin.kind must be data or exec");
            return -1;
        }
        if (!strcmp(kind_name, "data")) {
            kind = PIN_DATA;
        } else if (strcmp(kind_name, "exec")) {
            set_error(ctx, "add_pin.kind must be data or exec");
            return -1;
        }
    }
    if (kind == PIN_EXEC) {
        type = PIN_FLOW;
        type_name = NULL;
    } else if (type == PIN_FLOW) {
        set_error(ctx, "data pins require a type");
        return -1;
    }
    pin_container container;
    if (container_from_value(yyjson_obj_get(op, "container"), &container)) {
        set_error(ctx, "add_pin.container is invalid");
        return -1;
    }
    bool output = !strcmp(side, "output");
    pin_spec template_pin = { .name = name,
                              .type = type,
                              .type_name = type_name,
                              .kind = kind,
                              .container = container,
                              .default_value = { .type = type, .type_name = type_name } };
    int signature = signature_side(ctx, n, output);
    if (signature >= 0 &&
        graph_signature_add(ctx->asset, ctx->graph, signature, &template_pin)) {
        return signature_refused(ctx, "add_pin");
    }
    int index = signature >= 0
                  ? (output ? n->output_count : n->input_count) - 1
                  : graph_append_pin(ctx->graph, n, output, &template_pin);
    if (index < 0) {
        set_error(ctx, "could not add pin '%s'", name);
        return -1;
    }
    yyjson_val *default_json = yyjson_obj_get(op, "default");
    if (default_json) {
        pin_value value;
        int read_error = container >= PIN_CONTAINER_ARRAY
                           ? request_read_array(type, type_name, default_json, &value, ctx->graph)
                           : request_read_value(type, type_name, default_json, &value, ctx->graph);
        /* An input's default is the one its calls show. */
        int set = read_error ? -1
                : signature == 0
                    ? graph_signature_set_default(ctx->asset, ctx->graph, index, &value)
                    : graph_set_pin_default(ctx->graph, n, output, index, &value);
        if (set) {
            set_error(ctx, "add_pin.default does not match pin '%s'", name);
            return -1;
        }
    }
    return 0;
}

static int operation_add_variable(edit_context *ctx, yyjson_val *op)
{
    const char *name = NULL;
    if (!string_value(yyjson_obj_get(op, "name"), &name) || !name[0]) {
        set_error(ctx, "add_variable.name is required");
        return -1;
    }
    pin_type type;
    const char *type_name = NULL;
    if (type_from_value(yyjson_obj_get(op, "type"), &type, &type_name)) {
        set_error(ctx, "add_variable.type is invalid");
        return -1;
    }
    yyjson_val *type_name_json = yyjson_obj_get(op, "type_name");
    if (type_name_json && !string_value(type_name_json, &type_name)) {
        set_error(ctx, "add_variable.type_name must be a string");
        return -1;
    }
    if (type == PIN_STRUCT && !type_name) {
        set_error(ctx, "add_variable.type_name is required for struct variables");
        return -1;
    }
    pin_container container;
    if (container_from_value(yyjson_obj_get(op, "container"), &container)) {
        set_error(ctx, "add_variable.container is invalid");
        return -1;
    }
    pin_value value = { .type = type, .container = container, .type_name = type_name };
    yyjson_val *default_json = yyjson_obj_get(op, "default");
    int read_error = default_json ? container >= PIN_CONTAINER_ARRAY
                                      ? request_read_array(
                                            type,
                                            type_name,
                                            default_json,
                                            &value,
                                            ctx->asset->graph
                                        )
                                      : request_read_value(
                                            type,
                                            type_name,
                                            default_json,
                                            &value,
                                            ctx->asset->graph
                                        )
                                  : 0;
    if (read_error) {
        set_error(ctx, "add_variable.default does not match the variable type");
        return -1;
    }
    const char *category = NULL;
    yyjson_val *category_json = yyjson_obj_get(op, "category");
    if (category_json && !string_value(category_json, &category)) {
        set_error(ctx, "add_variable.category must be a string");
        return -1;
    }
    behavior_variable *variable = behavior_asset_add_variable(
        ctx->asset,
        name,
        type,
        type_name,
        container,
        default_json ? &value : NULL,
        category
    );
    if (!variable) {
        set_error(ctx, "could not add variable '%s'", name);
        return -1;
    }
    return add_variable_ref(ctx, yyjson_obj_get(op, "ref"), variable);
}

static int operation_set_variable(edit_context *ctx, yyjson_val *op)
{
    behavior_variable *variable = variable_by_selector(ctx, yyjson_obj_get(op, "variable"));
    if (!variable) {
        return -1;
    }
    yyjson_val *category = yyjson_obj_get(op, "category");
    if (category &&
        (!yyjson_is_str(category) ||
         behavior_asset_set_variable_category(ctx->asset, variable->id, yyjson_get_str(category)))) {
        set_error(ctx, "could not set variable category");
        return -1;
    }
    yyjson_val *name_json = yyjson_obj_get(op, "name");
    if (name_json) {
        const char *name = NULL;
        if (!string_value(name_json, &name) ||
            behavior_asset_rename_variable(ctx->asset, variable->id, name)) {
            set_error(ctx, "could not rename variable");
            return -1;
        }
    }
    yyjson_val *type_json = yyjson_obj_get(op, "type");
    if (type_json) {
        pin_type type;
        const char *type_name = NULL;
        if (type_from_value(type_json, &type, &type_name)) {
            set_error(ctx, "set_variable.type is invalid");
            return -1;
        }
        yyjson_val *type_name_json = yyjson_obj_get(op, "type_name");
        if (type_name_json && !string_value(type_name_json, &type_name)) {
            set_error(ctx, "set_variable.type_name must be a string");
            return -1;
        }
        if (type == PIN_STRUCT && !type_name) {
            set_error(ctx, "set_variable.type_name is required for struct variables");
            return -1;
        }
        if (behavior_asset_retype_variable(ctx->asset, variable->id, type, type_name)) {
            set_error(ctx, "could not retype variable");
            return -1;
        }
    }
    yyjson_val *container_json = yyjson_obj_get(op, "container");
    if (container_json) {
        pin_container container;
        if (container_from_value(container_json, &container) ||
            behavior_asset_set_variable_container(ctx->asset, variable->id, container)) {
            set_error(ctx, "could not set variable container");
            return -1;
        }
    }
    yyjson_val *default_json = yyjson_obj_get(op, "default");
    if (default_json) {
        pin_value value;
        int read_error = variable->container >= PIN_CONTAINER_ARRAY
                           ? request_read_array(
                                 variable->type,
                                 variable->type_name,
                                 default_json,
                                 &value,
                                 ctx->asset->graph
                             )
                           : request_read_value(
                                 variable->type,
                                 variable->type_name,
                                 default_json,
                                 &value,
                                 ctx->asset->graph
                             );
        if (read_error ||
            behavior_asset_set_variable_default(ctx->asset, variable->id, &value)) {
            set_error(ctx, "could not set variable default");
            return -1;
        }
    }
    return 0;
}

static int operation_add_variable_node(edit_context *ctx, yyjson_val *op)
{
    behavior_variable *variable = variable_by_selector(ctx, yyjson_obj_get(op, "variable"));
    if (!variable) {
        return -1;
    }
    const char *kind = NULL;
    if (!string_value(yyjson_obj_get(op, "kind"), &kind)) {
        set_error(ctx, "add_variable_node.kind must be get or set");
        return -1;
    }
    if (!behavior_asset_mutable(ctx->asset) ||
        (strcmp(kind, "get") && strcmp(kind, "set"))) {
        set_error(ctx, "add_variable_node.kind must be get or set on a mutable asset");
        return -1;
    }
    bool setter = !strcmp(kind, "set");
    node *n = graph_add_node(ctx->graph, setter ? &NODE_SetVariable : &NODE_GetVariable);
    if (!n) {
        set_error(ctx, "could not add variable %s node", kind);
        return -1;
    }
    if (graph_set_node_reference(ctx->graph, n, variable->id, variable->name)) {
        set_error(ctx, "could not reference variable '%s'", variable->name);
        return -1;
    }
    int type_error = graph_set_pin_type(
        ctx->graph,
        n,
        setter ? 0 : 1,
        setter ? 1 : 0,
        variable->type,
        variable->type_name
    );
    if (!type_error && setter) {
        type_error = graph_set_pin_type(
            ctx->graph,
            n,
            1,
            1,
            variable->type,
            variable->type_name
        );
    }
    int container_error = graph_set_pin_container(
        ctx->graph,
        n,
        setter ? 0 : 1,
        setter ? 1 : 0,
        variable->container
    );
    if (!container_error && setter) {
        container_error = graph_set_pin_container(ctx->graph, n, 1, 1, variable->container);
    }
    if (type_error || container_error) {
        set_error(ctx, "could not configure variable %s node", kind);
        return -1;
    }
    return add_node_ref(ctx, yyjson_obj_get(op, "ref"), n) ||
                   add_node_inputs(ctx, n, yyjson_obj_get(op, "inputs"))
             ? -1
             : 0;
}

static int operation_add_self_node(edit_context *ctx, yyjson_val *op)
{
    if (!behavior_asset_mutable(ctx->asset) || !ctx->asset->owner_type_name) {
        set_error(ctx, "asset has no owner type for a Self node");
        return -1;
    }
    node *n = graph_add_node(ctx->graph, &NODE_Self);
    if (!n || graph_set_pin_type(ctx->graph, n, 1, 0, PIN_HANDLE, ctx->asset->owner_type_name)) {
        set_error(ctx, "could not add Self node");
        return -1;
    }
    return add_node_ref(ctx, yyjson_obj_get(op, "ref"), n);
}

/* The node with this ID in any of the asset's graphs, and its graph. */
static node *asset_node(const behavior_asset *asset, uint64_t id, graph **owner)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        node *n = graph_node_by_id(asset->graphs[i], id);
        if (n) {
            *owner = asset->graphs[i];
            return n;
        }
    }
    return NULL;
}

/* Drops refs to nodes an operation removed. */
static void prune_node_refs(edit_context *ctx)
{
    graph *owner = NULL;
    for (int i = acount(ctx->nodes) - 1; i >= 0; i--) {
        if (!asset_node(ctx->asset, ctx->nodes[i].id, &owner)) {
            adel(ctx->nodes, i);
        }
    }
}

/* Collapsing and expanding return a replacement asset that keeps node and
 * graph IDs. Take it over and find each ref, the current graph and the graphs
 * to arrange again by ID; a generated ref name moves to its node's graph. */
static void replace_asset(edit_context *ctx, behavior_asset *replacement)
{
    uint64_t current = ctx->graph->graph_id;
    uint64_t *arranged = NULL;
    for (int i = 0; i < acount(ctx->layout_graphs); i++) {
        apush(arranged, ctx->layout_graphs[i]->graph_id);
    }
    for (int i = acount(ctx->nodes) - 1; i >= 0; i--) {
        node_ref *ref = &ctx->nodes[i];
        graph *owner = NULL;
        ref->node = asset_node(replacement, ref->id, &owner);
        if (!ref->node) {
            adel(ctx->nodes, i);
            continue;
        }
        ref->name = graph_own_str(owner, ref->name, strlen(ref->name));
    }
    for (int i = acount(ctx->graphs) - 1; i >= 0; i--) {
        ctx->graphs[i]
            .graph = behavior_asset_find_graph(replacement, ctx->graphs[i].id);
        if (!ctx->graphs[i].graph) {
            adel(ctx->graphs, i);
        }
    }
    asetlen(ctx->layout_graphs, 0);
    for (int i = 0; i < acount(arranged); i++) {
        graph *g = behavior_asset_find_graph(replacement, arranged[i]);
        if (g) {
            apush(ctx->layout_graphs, g);
        }
    }
    afree(arranged);
    behavior_asset_free(ctx->asset);
    ctx->asset = replacement;
    ctx->graph = behavior_asset_find_graph(replacement, current);
    assert(ctx->graph);
}

/* The nodes an operation's "nodes" array selects in the current graph. */
static node **selected_nodes(edit_context *ctx, yyjson_val *op, const char *operation)
{
    yyjson_val *list = yyjson_obj_get(op, "nodes");
    if (!yyjson_is_arr(list) || !yyjson_arr_size(list)) {
        set_error(ctx, "%s requires a nodes array of node IDs or refs", operation);
        return NULL;
    }
    node **nodes = NULL;
    size_t at, max;
    yyjson_val *selector;
    yyjson_arr_foreach(list, at, max, selector)
    {
        node *n = node_by_selector(ctx, selector);
        if (!n) {
            afree(nodes);
            return NULL;
        }
        apush(nodes, n);
    }
    return nodes;
}

/* Collapse to Function or Macro: the selection becomes a new graph named
 * `name` and a call to it; `ref` names both. */
static int operation_collapse(edit_context *ctx, yyjson_val *op, bool macro)
{
    const char *operation = macro ? "collapse_to_macro" : "collapse_to_function";
    const char *name = NULL;
    if (ctx->graph->dataflow) {
        set_error(ctx, "%s is unavailable in System graphs", operation);
        return -1;
    }
    if (!string_value(yyjson_obj_get(op, "name"), &name) || !name[0]) {
        set_error(ctx, "%s.name is required", operation);
        return -1;
    }
    node **selection = selected_nodes(ctx, op, operation);
    if (!selection) {
        return -1;
    }
    behavior_asset *replacement = NULL;
    uint64_t call_id = 0;
    int count = acount(selection);
    const char *error = macro ? behavior_asset_extract_macro(
                                    ctx->asset,
                                    ctx->graph,
                                    selection,
                                    count,
                                    name,
                                    &replacement,
                                    &call_id
                                )
                              : behavior_asset_extract_function(
                                    ctx->asset,
                                    ctx->graph,
                                    selection,
                                    count,
                                    name,
                                    &replacement,
                                    &call_id
                                );
    afree(selection);
    if (error) {
        set_error(ctx, "%s", error);
        return -1;
    }
    replace_asset(ctx, replacement);
    node *call = graph_node_by_id(ctx->graph, call_id);
    graph *made = behavior_asset_find_graph(ctx->asset, call->reference_id);
    yyjson_val *ref = yyjson_obj_get(op, "ref");
    return add_node_ref(ctx, ref, call) || add_graph_ref(ctx, ref, made) ||
                   add_layout_graph(ctx, made)
             ? -1
             : 0;
}

/* Expand Node: a call to one of the asset's own functions or macros becomes a
 * copy of its body. */
static int operation_expand_node(edit_context *ctx, yyjson_val *op)
{
    node *call = node_by_selector(ctx, yyjson_obj_get(op, "node"));
    if (!call) {
        return -1;
    }
    if (call->desc == &NODE_BehaviorExpression) {
        set_error(ctx, "expand_node does not expand a Behavior Expression; edit its source instead");
        return -1;
    }
    behavior_asset *replacement = NULL;
    uint64_t *ids = NULL;
    const char *error = behavior_asset_expand_call(ctx->asset, ctx->graph, call, &replacement, &ids);
    afree(ids);
    if (error) {
        set_error(ctx, "%s", error);
        return -1;
    }
    replace_asset(ctx, replacement);
    return 0;
}

/* Promote to Variable: a new variable typed as the pin, with a Get wired to
 * an input or a Set wired from an output. `ref` names the variable and that
 * node; the name defaults to the pin's, numbered past existing variables. */
static int operation_promote_to_variable(edit_context *ctx, yyjson_val *op)
{
    node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
    const char *side = "input";
    yyjson_val *side_json = yyjson_obj_get(op, "side");
    if (!n || (side_json && !string_value(side_json, &side)) ||
        (strcmp(side, "input") && strcmp(side, "output"))) {
        set_error(ctx, "promote_to_variable requires a node and input or output side");
        return -1;
    }
    int output = !strcmp(side, "output");
    const pin_spec *pin = NULL;
    int index = pin_index(ctx, n, yyjson_obj_get(op, "pin"), output, &pin);
    if (index < 0) {
        return -1;
    }
    if (ctx->asset->domain != BEHAVIOR_DOMAIN_EVENT || ctx->graph->dataflow) {
        set_error(ctx, "promote_to_variable needs a behavior's graph");
        return -1;
    }
    if (pin->kind != PIN_DATA || pin->type == PIN_FLOW) {
        set_error(ctx, "promote_to_variable needs a typed data pin; connect a wildcard first");
        return -1;
    }
    char name[128];
    const char *requested = NULL;
    if (string_value(yyjson_obj_get(op, "name"), &requested) && requested[0]) {
        snprintf(name, sizeof name, "%s", requested);
    } else {
        snprintf(name, sizeof name, "%s", pin->name);
        for (int i = 2; behavior_asset_find_variable_by_name(ctx->asset, name); i++) {
            snprintf(name, sizeof name, "%s %d", pin->name, i);
        }
    }
    behavior_variable *
        variable = behavior_asset_promote_pin_to_variable(ctx->asset, n, output, index, name);
    if (!variable) {
        set_error(ctx, "could not promote %s pin '%s' to a variable", side, pin->name);
        return -1;
    }
    yyjson_val *ref = yyjson_obj_get(op, "ref");
    return add_variable_ref(ctx, ref, variable) ||
                   add_node_ref(ctx, ref, alast(ctx->graph->nodes))
             ? -1
             : 0;
}

/* A connected, pure calculation becomes one Behavior Expression call, which
 * `ref` names. */
static int operation_convert_to_expression(edit_context *ctx, yyjson_val *op)
{
    node **selection = selected_nodes(ctx, op, "convert_to_expression");
    if (!selection) {
        return -1;
    }
    behavior_expression_error error = { 0 };
    node *call = behavior_expression_convert_selection(
        ctx->asset,
        ctx->graph,
        selection,
        acount(selection),
        &error
    );
    afree(selection);
    if (!call) {
        set_error(ctx, "%s", error.message[0] ? error.message : "could not convert the nodes to an expression");
        return -1;
    }
    prune_node_refs(ctx);
    return add_node_ref(ctx, yyjson_obj_get(op, "ref"), call);
}

/* Moving, renaming, retyping or removing a signature pin updates every call;
   a move applies first, so the other fields edit the pin where it lands. */
static int signature_pin(
    edit_context *ctx,
    yyjson_val *op,
    const char *operation,
    int result,
    pin_spec pin,
    int index
)
{
    behavior_asset *asset = ctx->asset;
    graph *g = ctx->graph;
    if (!strcmp(operation, "remove_pin")) {
        return graph_signature_remove(asset, g, result, index)
                 ? signature_refused(ctx, operation)
                 : 0;
    }
    yyjson_val *order = yyjson_obj_get(op, "order");
    int to = index;
    if (order && (!value_nonnegative_int(order, &to) ||
                  graph_signature_move(asset, g, result, index, to))) {
        set_error(ctx, "set_pin.order must be the index of another input or output the signature declares on that side");
        return -1;
    }
    index = to;
    yyjson_val *name = yyjson_obj_get(op, "name");
    if (name &&
        (!yyjson_is_str(name) ||
         graph_signature_rename(asset, g, result, index, yyjson_get_str(name)))) {
        return signature_refused(ctx, operation);
    }
    yyjson_val *type_json = yyjson_obj_get(op, "type");
    yyjson_val *named = yyjson_obj_get(op, "type_name");
    yyjson_val *container_json = yyjson_obj_get(op, "container");
    if (!type_json && !named && !container_json) {
        return 0;
    }
    if ((type_json && type_from_value(type_json, &pin.type, &pin.type_name)) ||
        (named && !string_value(named, &pin.type_name)) ||
        (container_json && container_from_value(container_json, &pin.container)) ||
        (pin.type == PIN_STRUCT && !pin.type_name)) {
        set_error(ctx, "set_pin.type, type_name or container is invalid");
        return -1;
    }
    return graph_signature_retype(asset, g, result, index, &pin)
             ? signature_refused(ctx, operation)
             : 0;
}

static int operation_pin(edit_context *ctx, yyjson_val *op, const char *operation)
{
    node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
    /* Locals and exposed fields exist only on outputs. */
    bool output_only = !strcmp(operation, "create_local") ||
                       !strcmp(operation, "expand_local") ||
                       !strcmp(operation, "expose_fields") ||
                       !strcmp(operation, "hide_fields");
    const char *side = output_only ? "output" : "input";
    yyjson_val *side_json = yyjson_obj_get(op, "side");
    if (!n || (side_json && !string_value(side_json, &side)) ||
        (strcmp(side, "input") && strcmp(side, "output"))) {
        set_error(ctx, "pin operation requires a node and input or output side");
        return -1;
    }
    int output = !strcmp(side, "output");
    const pin_spec *pin = NULL;
    int index = pin_index(ctx, n, yyjson_obj_get(op, "pin"), output, &pin);
    if (index < 0) {
        return -1;
    }
    /* Removing a Set field input hides it; other component pins are fixed. */
    if (component_node_kind_of(n->desc) &&
        (!strcmp(operation, "remove_pin") || !strcmp(operation, "set_pin"))) {
        if (strcmp(operation, "remove_pin") || output ||
            component_node_show_field(ctx->graph, n, pin->name, false)) {
            set_error(
                ctx,
                "%s pins follow its Component; show or hide fields instead",
                n->desc->name
            );
            return -1;
        }
        return 0;
    }
    int signature = signature_side(ctx, n, output);
    if (signature >= 0 &&
        (!strcmp(operation, "set_pin") || !strcmp(operation, "remove_pin"))) {
        return signature_pin(
            ctx,
            op,
            operation,
            signature,
            output ? n->output_pins[index] : n->input_pins[index],
            index
        );
    }
    int result = -1;
    if (!strcmp(operation, "remove_pin")) {
        result = graph_remove_pin(ctx->graph, n, output, index);
    } else if (!strcmp(operation, "split_pin")) {
        result = graph_split_pin(ctx->graph, n, output, index);
    } else if (!strcmp(operation, "recombine_pin")) {
        result = graph_recombine_pin(ctx->graph, n, output, index);
    } else if (!strcmp(operation, "expose_fields")) {
        result = output ? graph_expose_output_fields(ctx->graph, n, index) : -1;
    } else if (!strcmp(operation, "hide_fields")) {
        result = output ? graph_hide_output_fields(ctx->graph, n, index) : -1;
    } else if (!strcmp(operation, "clear_binding")) {
        result = graph_set_pin_binding(ctx->graph, n, output, index, NULL);
    } else if (!strcmp(operation, "create_local")) {
        /* Naming an output that already has a local renames that local; with
         * no name, a new local takes the one Name Value would give it. */
        const char *name = NULL;
        char generated[128];
        const graph_local *local = output ? graph_output_local(ctx->graph, n, index)
                                          : NULL;
        if (output && !string_value(yyjson_obj_get(op, "name"), &name) && !local) {
            graph_local_name(ctx->graph, n, index, generated, sizeof generated);
            name = generated;
        }
        if (output && name) {
            result = local ? graph_rename_local(ctx->graph, local->id, name)
                   : graph_create_local(ctx->graph, n, index, name) ? 0
                                                                    : -1;
        }
    } else if (!strcmp(operation, "expand_local")) {
        /* The local's readers take the wire from its output again. */
        const graph_local *local = output ? graph_output_local(ctx->graph, n, index)
                                          : NULL;
        result = local ? graph_expand_local(ctx->graph, local->id) : -1;
    } else if (
        !strcmp(operation, "set_pin") && n->desc == &NODE_BehaviorExpression &&
        !output && yyjson_is_str(yyjson_obj_get(op, "name"))
    ) {
        /* Renaming an expression input renames it in the source and keeps its
           wire or binding. */
        behavior_expression_error error = { 0 };
        result = behavior_expression_rename_input(
            ctx->asset,
            n,
            pin->name,
            yyjson_get_str(yyjson_obj_get(op, "name")),
            &error
        );
        if (result) {
            set_error(ctx, "%s", error.message);
            return -1;
        }
    } else if (!strcmp(operation, "set_pin")) {
        result = 0;
        yyjson_val *name = yyjson_obj_get(op, "name");
        if (name &&
            (!yyjson_is_str(name) ||
             graph_set_pin_name(ctx->graph, n, output, index, yyjson_get_str(name)))) {
            result = -1;
        }
        yyjson_val *type_json = yyjson_obj_get(op, "type");
        if (!result && type_json) {
            pin_type type;
            const char *type_name = NULL;
            yyjson_val *named = yyjson_obj_get(op, "type_name");
            if (type_from_value(type_json, &type, &type_name) ||
                (named && !string_value(named, &type_name)) ||
                (type == PIN_STRUCT && !type_name)) {
                result = -1;
            }
            int declared_count = output ? n->desc->output_count : n->desc->input_count;
            const pin_spec *declared = output ? n->desc->outputs : n->desc->inputs;
            if (!result && index < declared_count && declared[index].type != PIN_FLOW &&
                !(declared[index].flags & PIN_FLAG_WILDCARD) &&
                type != declared[index].type) {
                set_error(
                    ctx,
                    "%s.%s has a fixed type; use a matching node or a "
                    "conversion",
                    n->desc->name,
                    pin->name
                );
                return -1;
            }
            if (!result) {
                result = graph_set_pin_type(ctx->graph, n, output, index, type, type_name);
            }
        }
        yyjson_val *container_json = yyjson_obj_get(op, "container");
        if (!result && container_json) {
            pin_container container;
            if (container_from_value(container_json, &container) ||
                graph_set_pin_container(ctx->graph, n, output, index, container)) {
                result = -1;
            }
        }
        yyjson_val *order = yyjson_obj_get(op, "order");
        int target;
        if (!result && order &&
            (!value_nonnegative_int(order, &target) ||
             graph_move_pin(ctx->graph, n, output, index, target) < 0)) {
            result = -1;
        }
    } else if (!strcmp(operation, "bind_input")) {
        const char *kind = NULL;
        pin_binding binding = { 0 };
        if (output || !string_value(yyjson_obj_get(op, "kind"), &kind)) {
            set_error(ctx, "bind_input requires an input pin and binding kind");
            return -1;
        }
        if (!strcmp(kind, "variable")) {
            behavior_variable *variable = variable_by_selector(ctx, yyjson_obj_get(op, "variable"));
            if (!variable) {
                return -1;
            }
            binding.kind = PIN_BINDING_VARIABLE;
            binding.variable_id = variable->id;
            binding.source_asset = ctx->asset->name;
        } else if (!strcmp(kind, "delta_time")) {
            binding.kind = PIN_BINDING_DELTA_TIME;
        } else if (!strcmp(kind, "event_output")) {
            binding.kind = PIN_BINDING_EVENT_OUTPUT;
            if (!value_uint(yyjson_obj_get(op, "event_node_id"), &binding.event_node_id) ||
                !value_uint(yyjson_obj_get(op, "event_pin_id"), &binding.event_pin_id)) {
                set_error(ctx, "event_output binding requires event_node_id and event_pin_id");
                return -1;
            }
        } else if (!strcmp(kind, "component")) {
            if (!string_value(yyjson_obj_get(op, "component"), &binding.component_key)) {
                set_error(ctx, "component binding requires the reflected stable component key");
                return -1;
            }
            /* A component's name resolves to its key; an unknown one would bind
               and read nothing. */
            const ecs_component_desc *component = component_by_key(binding.component_key);
            if (!component || !component->key) {
                set_error(ctx, "unknown component '%s'", binding.component_key);
                return -1;
            }
            binding.component_key = component->key;
            yyjson_val *field = yyjson_obj_get(op, "field");
            if (field && !string_value(field, &binding.field_name)) {
                set_error(ctx, "component field must be a string");
                return -1;
            }
            binding.kind = field ? PIN_BINDING_COMPONENT_FIELD : PIN_BINDING_COMPONENT;
        } else {
            set_error(ctx, "binding kind must be variable, component, delta_time, or event_output");
            return -1;
        }
        bool reference_source_removed = true;
        if (n->input_pins[index].flags & PIN_FLAG_REFERENCE) {
            node *source = NULL;
            int source_pin = -1;
            if (graph_input_source(ctx->graph, n, index, &source, &source_pin) &&
                graph_remove_edge(ctx->graph, source, source_pin, n, index) != 0) {
                reference_source_removed = false;
            }
        }
        result = reference_source_removed
                   ? graph_set_pin_binding(ctx->graph, n, 0, index, &binding)
                   : -1;
        if (!result && n->desc == &NODE_BehaviorExpression) {
            behavior_expression_error error = { 0 };
            if (behavior_expression_refresh(ctx->asset, n, &error) != 0) {
                set_error(ctx, "%s", error.message[0] ? error.message : "could not refresh behavior expression");
                result = -1;
            }
        }
    }
    if (result < 0) {
        set_error(ctx, "%s could not be applied to the selected pin", operation);
        return -1;
    }
    return 0;
}

static graph *operation_graph(edit_context *ctx, yyjson_val *op)
{
    yyjson_val *selector = yyjson_obj_get(op, "graph");
    if (yyjson_is_uint(selector)) {
        return behavior_asset_find_graph(ctx->asset, yyjson_get_uint(selector));
    }
    if (!yyjson_is_str(selector)) {
        return NULL;
    }
    const char *name = yyjson_get_str(selector);
    for (int i = 0; i < acount(ctx->graphs); i++) {
        if (!strcmp(ctx->graphs[i].name, name)) {
            return ctx->graphs[i].graph;
        }
    }
    return behavior_asset_find_graph_by_name(ctx->asset, name);
}

static int operation_graph_declaration(edit_context *ctx, yyjson_val *op, const char *operation)
{
    graph *g = operation_graph(ctx, op);
    if (!g) {
        set_error(ctx, "graph must select an existing graph ID, name, or request ref");
        return -1;
    }
    if (!strcmp(operation, "select_graph")) {
        ctx->graph = g;
        return add_layout_graph(ctx, g);
    }
    if (!strcmp(operation, "rename_graph")) {
        const char *name = NULL;
        if (!string_value(yyjson_obj_get(op, "name"), &name) ||
            behavior_asset_rename_graph(ctx->asset, g->graph_id, name)) {
            set_error(ctx, "could not rename graph");
            return -1;
        }
        return 0;
    }
    if (g == ctx->asset->graph) {
        set_error(ctx, "the default graph cannot be removed");
        return -1;
    }
    /* Remove request aliases before freeing their graph-owned names and nodes. */
    for (int i = acount(ctx->nodes) - 1; i >= 0; i--) {
        if (graph_node_by_id(g, ctx->nodes[i].id) == ctx->nodes[i].node) {
            adel(ctx->nodes, i);
        }
    }
    for (int i = acount(ctx->graphs) - 1; i >= 0; i--) {
        if (ctx->graphs[i].graph == g) {
            adel(ctx->graphs, i);
        }
    }
    for (int i = acount(ctx->layout_graphs) - 1; i >= 0; i--) {
        if (ctx->layout_graphs[i] == g) {
            adel(ctx->layout_graphs, i);
        }
    }
    if (ctx->graph == g) {
        ctx->graph = ctx->asset->graph;
    }
    if (behavior_asset_delete_graph(ctx->asset, g->graph_id)) {
        set_error(ctx, "could not remove graph");
        return -1;
    }
    return 0;
}

static int apply_operation(edit_context *ctx, yyjson_val *op)
{
    if (!yyjson_is_obj(op)) {
        set_error(ctx, "operation must be an object");
        return -1;
    }
    const char *name = NULL;
    if (!string_value(yyjson_obj_get(op, "op"), &name)) {
        set_error(ctx, "operation.op is required");
        return -1;
    }
    if (!strcmp(name, "select_graph") || !strcmp(name, "rename_graph") ||
        !strcmp(name, "remove_graph")) {
        return operation_graph_declaration(ctx, op, name);
    }
    if (!strcmp(name, "remove_variable")) {
        behavior_variable *variable = variable_by_selector(ctx, yyjson_obj_get(op, "variable"));
        if (!variable || behavior_asset_delete_variable(ctx->asset, variable->id)) {
            set_error(ctx, "could not remove variable");
            return -1;
        }
        return 0;
    }
    if (!strcmp(name, "set_pin") || !strcmp(name, "remove_pin") ||
        !strcmp(name, "split_pin") || !strcmp(name, "recombine_pin") ||
        !strcmp(name, "expose_fields") || !strcmp(name, "hide_fields") ||
        !strcmp(name, "bind_input") || !strcmp(name, "clear_binding") ||
        !strcmp(name, "create_local") || !strcmp(name, "expand_local")) {
        return operation_pin(ctx, op, name);
    }
    if (!strcmp(name, "add_node")) {
        return operation_add_node(ctx, op);
    }
    if (!strcmp(name, "collapse_to_function") || !strcmp(name, "collapse_to_macro")) {
        return operation_collapse(ctx, op, !strcmp(name, "collapse_to_macro"));
    }
    if (!strcmp(name, "expand_node")) {
        return operation_expand_node(ctx, op);
    }
    if (!strcmp(name, "promote_to_variable")) {
        return operation_promote_to_variable(ctx, op);
    }
    if (!strcmp(name, "convert_to_expression")) {
        return operation_convert_to_expression(ctx, op);
    }
    if (!strcmp(name, "set_expression")) {
        return operation_set_expression(ctx, op);
    }
    if (!strcmp(name, "remove_node")) {
        return operation_remove_node(ctx, op);
    }
    if (!strcmp(name, "connect")) {
        return operation_connect(ctx, op, false);
    }
    if (!strcmp(name, "disconnect")) {
        return operation_connect(ctx, op, true);
    }
    if (!strcmp(name, "set_input")) {
        node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
        if (!n) {
            return -1;
        }
        yyjson_val *value = yyjson_obj_get(op, "value");
        if (!value) {
            set_error(ctx, "set_input.value is required");
            return -1;
        }
        return set_input(ctx, n, yyjson_obj_get(op, "pin"), value, yyjson_obj_get(op, "type"));
    }
    if (!strcmp(name, "set_output")) {
        node *n = node_by_selector(ctx, yyjson_obj_get(op, "node"));
        return n ? set_output(ctx, n, op) : -1;
    }
    if (!strcmp(name, "set_node")) {
        return operation_set_node(ctx, op);
    }
    if (!strcmp(name, "set_comment")) {
        return operation_set_comment(ctx, op);
    }
    if (!strcmp(name, "set_ramp")) {
        return operation_set_ramp(ctx, op);
    }
    if (!strcmp(name, "set_model_reference")) {
        return operation_set_model_reference(ctx, op);
    }
    if (!strcmp(name, "set_bounds")) {
        return operation_set_bounds(ctx, op);
    }
    if (!strcmp(name, "add_graph")) {
        return operation_add_graph(ctx, op);
    }
    if (!strcmp(name, "add_pin")) {
        return operation_add_pin(ctx, op);
    }
    if (!strcmp(name, "add_variable")) {
        return operation_add_variable(ctx, op);
    }
    if (!strcmp(name, "set_variable")) {
        return operation_set_variable(ctx, op);
    }
    if (!strcmp(name, "add_variable_node")) {
        return operation_add_variable_node(ctx, op);
    }
    if (!strcmp(name, "add_self_node")) {
        return operation_add_self_node(ctx, op);
    }
    set_error(ctx, "unknown graph edit operation '%s'", name);
    return -1;
}

static void add_compile_diagnostic(edit_context *ctx, yyjson_mut_val *diagnostics, const compile_diagnostic *item)
{
    yyjson_mut_val *entry = yyjson_mut_arr_add_obj(ctx->doc, diagnostics);
    yyjson_mut_obj_add_strcpy(ctx->doc, entry, "severity", item->severity == CMPL_ERROR ? "error" : "warning");
    yyjson_mut_obj_add_uint(ctx->doc, entry, "graph", item->graph_id);
    yyjson_mut_obj_add_uint(ctx->doc, entry, "node", item->node_id);
    yyjson_mut_obj_add_uint(ctx->doc, entry, "pin", item->pin_id);
    yyjson_mut_obj_add_int(ctx->doc, entry, "rule", item->rule);
    yyjson_mut_obj_add_strcpy(ctx->doc, entry, "rule_name", cmpl_rule_name(item->rule));
    yyjson_mut_obj_add_strcpy(ctx->doc, entry, "message", item->message);
}

static void add_shader_diagnostic(edit_context *ctx, yyjson_mut_val *diagnostics, const shader_error *error)
{
    yyjson_mut_val *entry = yyjson_mut_arr_add_obj(ctx->doc, diagnostics);
    yyjson_mut_obj_add_str(ctx->doc, entry, "severity", "error");
    yyjson_mut_obj_add_uint(ctx->doc, entry, "node", error->node);
    yyjson_mut_obj_add_strcpy(ctx->doc, entry, "message", error->message);
}

static void add_message_diagnostic(
    edit_context *ctx,
    yyjson_mut_val *diagnostics,
    const char *severity,
    const char *message
)
{
    yyjson_mut_val *entry = yyjson_mut_arr_add_obj(ctx->doc, diagnostics);
    yyjson_mut_obj_add_str(ctx->doc, entry, "severity", severity);
    yyjson_mut_obj_add_strcpy(ctx->doc, entry, "message", message ? message : "asset validation failed");
}

static int validate_asset(edit_context *ctx)
{
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(ctx->doc, ctx->result, "diagnostics");
    behavior_asset *linked = NULL;
    const behavior_asset *validation_asset = ctx->asset;
    char link_error[256] = { 0 };
    if (asset_has_external_graph_reference(ctx->asset) ||
        (ctx->library_count && ctx->asset->domain == BEHAVIOR_DOMAIN_EVENT)) {
        linked = behavior_asset_link_libraries(
            ctx->asset,
            ctx->libraries,
            ctx->library_count,
            link_error,
            sizeof link_error
        );
        if (!linked) {
            add_message_diagnostic(ctx, diagnostics, "error", link_error);
            set_error(ctx, "could not link referenced library: %s", link_error);
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        validation_asset = linked;
    }
    if (validation_asset->domain == BEHAVIOR_DOMAIN_SHAPE &&
        (!(validation_asset->bounds[0] > 0.0f) || !(validation_asset->bounds[1] > 0.0f) ||
         !isfinite(validation_asset->bounds[0]) ||
         !isfinite(validation_asset->bounds[1]))) {
        const char
            *message = "Shape bounds must be finite positive half extents.";
        add_message_diagnostic(ctx, diagnostics, "error", message);
        set_error(ctx, "%s", message);
        behavior_asset_free(linked);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    if (validation_asset->domain == BEHAVIOR_DOMAIN_LIBRARY ||
        validation_asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        const char *library_error = behavior_asset_validate_library(validation_asset);
        if (library_error) {
            add_message_diagnostic(ctx, diagnostics, "error", library_error);
            set_error(ctx, "%s", library_error);
            behavior_asset_free(linked);
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
    }
    if (validation_asset->domain == BEHAVIOR_DOMAIN_MODEL) {
        model_program program;
        model_program_init(&program);
        shader_error error = { 0 };
        bool valid = model_compile_with_catalog(
            validation_asset,
            ctx->libraries,
            ctx->library_count,
            &program,
            &error
        );
        model_program_free(&program);
        behavior_asset_free(linked);
        if (!valid) {
            add_shader_diagnostic(ctx, diagnostics, &error);
            set_error(ctx, "%s", error.message);
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        return CHIBI_AUTOMATION_OK;
    }
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(validation_asset, &report);
    for (int i = 0; i < acount(report.items); i++) {
        add_compile_diagnostic(ctx, diagnostics, &report.items[i]);
    }
    compile_report_free(&report);
    if (errors) {
        set_error(ctx, "asset compilation produced %d error%s", errors, errors == 1 ? "" : "s");
        behavior_asset_free(linked);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    if (is_shader_domain(validation_asset)) {
        shader_graph ir;
        shader_graph_init(&ir);
        shader_error shader_error = { 0 };
        if (!shader_graph_from_graph(validation_asset->graph, &ir, &shader_error)) {
            add_shader_diagnostic(ctx, diagnostics, &shader_error);
            set_error(ctx, "shader graph validation failed: %s", shader_error.message);
            shader_graph_free(&ir);
            behavior_asset_free(linked);
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        shader_graph_free(&ir);
    }
    behavior_asset_free(linked);
    return CHIBI_AUTOMATION_OK;
}

static void add_refs(edit_context *ctx)
{
    yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(ctx->doc, ctx->result, "nodes");
    for (int i = 0; i < acount(ctx->nodes); i++) {
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(ctx->doc, nodes);
        yyjson_mut_obj_add_strcpy(ctx->doc, entry, "ref", ctx->nodes[i].name);
        yyjson_mut_obj_add_uint(ctx->doc, entry, "id", ctx->nodes[i].id);
        yyjson_mut_obj_add_strcpy(
            ctx->doc,
            entry,
            "type",
            ctx->nodes[i].node->desc->name
        );
    }
    yyjson_mut_val *graphs = yyjson_mut_obj_add_arr(ctx->doc, ctx->result, "graphs");
    for (int i = 0; i < acount(ctx->graphs); i++) {
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(ctx->doc, graphs);
        yyjson_mut_obj_add_strcpy(ctx->doc, entry, "ref", ctx->graphs[i].name);
        yyjson_mut_obj_add_uint(ctx->doc, entry, "id", ctx->graphs[i].id);
        yyjson_mut_obj_add_strcpy(
            ctx->doc,
            entry,
            "name",
            ctx->graphs[i].graph->name ? ctx->graphs[i].graph->name : ""
        );
    }
    yyjson_mut_val *variables = yyjson_mut_obj_add_arr(ctx->doc, ctx->result, "variables");
    for (int i = 0; i < acount(ctx->variables); i++) {
        behavior_variable *variable = behavior_asset_find_variable_by_id(
            ctx->asset,
            ctx->variables[i].id
        );
        if (!variable) {
            continue;
        }
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(ctx->doc, variables);
        yyjson_mut_obj_add_strcpy(ctx->doc, entry, "ref", ctx->variables[i].name);
        yyjson_mut_obj_add_uint(ctx->doc, entry, "id", ctx->variables[i].id);
        yyjson_mut_obj_add_strcpy(ctx->doc, entry, "name", variable->name);
    }
}

static graph *select_graph(edit_context *ctx, yyjson_val *request)
{
    yyjson_val *id_json = yyjson_obj_get(request, "graph_id");
    yyjson_val *name_json = yyjson_obj_get(request, "graph_name");
    if (id_json && name_json) {
        set_error(ctx, "use graph_id or graph_name, not both");
        return NULL;
    }
    if (id_json) {
        uint64_t id;
        if (!value_uint(id_json, &id)) {
            set_error(ctx, "graph_id must be an unsigned integer");
            return NULL;
        }
        graph *g = behavior_asset_find_graph(ctx->asset, id);
        if (!g) {
            set_error(ctx, "graph id %llu does not exist", (unsigned long long)id);
        }
        return g;
    }
    if (name_json) {
        const char *name = NULL;
        if (!string_value(name_json, &name)) {
            set_error(ctx, "graph_name must be a string");
            return NULL;
        }
        graph *g = behavior_asset_find_graph_by_name(ctx->asset, name);
        if (!g) {
            set_error(ctx, "graph '%s' does not exist", name);
        }
        return g;
    }
    return ctx->asset->graph;
}

static int apply_metadata(edit_context *ctx, yyjson_val *request)
{
    yyjson_val *description = yyjson_obj_get(request, "description");
    if (description) {
        if (!yyjson_is_str(description) || yyjson_get_len(description) > 2048) {
            set_error(ctx, "description must be a string of at most 2048 characters");
            return -1;
        }
        behavior_asset_set_description(ctx->asset, yyjson_get_str(description));
    }

    yyjson_val *owner_type = yyjson_obj_get(request, "owner_type");
    if (owner_type) {
        if (!yyjson_is_str(owner_type) || !yyjson_get_len(owner_type) ||
            yyjson_get_len(owner_type) > 256 ||
            behavior_asset_set_owner_type(ctx->asset, yyjson_get_str(owner_type))) {
            set_error(ctx, "owner_type must be a non-empty string of at most 256 characters");
            return -1;
        }
    }

    yyjson_val *graph_description = yyjson_obj_get(request, "graph_description");
    if (graph_description) {
        if (!yyjson_is_str(graph_description) ||
            yyjson_get_len(graph_description) > 2048 ||
            graph_set_metadata(
                ctx->graph,
                ctx->graph->name ? ctx->graph->name : "",
                yyjson_get_str(graph_description)
            )) {
            set_error(ctx, "graph_description must be a string of at most 2048 characters");
            return -1;
        }
    }
    return 0;
}

int automation_graph_edit(
    const char *source_text,
    const char *kind,
    const char *name,
    const behavior_asset *const *libraries,
    int library_count,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited_text,
    char *error,
    size_t error_size
)
{
    if (edited_text) {
        *edited_text = NULL;
    }
    if (!request || !yyjson_is_obj(request) || !doc || !result || !edited_text) {
        if (error && error_size) {
            snprintf(error, error_size, "graph edit requires a request object and output document");
        }
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (library_count < 0 || (library_count && !libraries)) {
        if (error && error_size) {
            snprintf(error, error_size, "library_count must be nonnegative with a library array");
        }
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    edit_context ctx = { .doc = doc,
                         .result = result,
                         .libraries = libraries,
                         .library_count = library_count,
                         .error = error,
                         .error_size = error_size };
    behavior_domain requested_domain = (behavior_domain)-1;
    if (kind) {
        requested_domain = domain_from_kind(kind);
        if ((int)requested_domain < 0) {
            return fail_request(&ctx, "unknown asset kind '%s'", kind);
        }
    }
    if (source_text) {
        ctx.asset = behavior_asset_load(source_text);
        if (!ctx.asset) {
            return fail_validation(&ctx, "could not load asset: %s", graph_last_error());
        }
        if (kind && ctx.asset->domain != requested_domain) {
            const char *actual = behavior_domain_name(ctx.asset->domain);
            int status = fail_request(
                &ctx,
                "source asset domain '%s' does not match requested kind '%s'",
                actual,
                kind
            );
            free_edit_context(&ctx);
            return status;
        }
    } else {
        if (!kind) {
            return fail_request(&ctx, "asset kind is required when creating an asset");
        }
        ctx.asset = new_asset_graph(requested_domain, name && name[0] ? name : "Untitled");
        /* A material's, shape's or field's output node, its only one. */
        node **nodes = ctx.asset->graph->nodes;
        if (acount(nodes) && add_node_ref_name(&ctx, "output", nodes[0])) {
            int status = fail_validation(&ctx, "could not name the output node");
            free_edit_context(&ctx);
            return status;
        }
    }
    ctx.graph = select_graph(&ctx, request);
    if (!ctx.graph) {
        if (!error || !error[0]) {
            set_error(&ctx, "selected graph does not exist");
        }
        add_failure(&ctx);
        free_edit_context(&ctx);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (add_layout_graph(&ctx, ctx.graph)) {
        add_failure(&ctx);
        free_edit_context(&ctx);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (apply_metadata(&ctx, request)) {
        add_failure(&ctx);
        free_edit_context(&ctx);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_val *operations = yyjson_obj_get(request, "operations");
    if (!operations || !yyjson_is_arr(operations) ||
        yyjson_arr_size(operations) > GRAPH_EDIT_MAX_OPERATIONS) {
        int status = fail_request(&ctx, "operations must be an array of at most %d items", GRAPH_EDIT_MAX_OPERATIONS);
        free_edit_context(&ctx);
        return status;
    }
    if (acount(ctx.graph->nodes) > SHADER_NODE_MAX) {
        int status = fail_validation(&ctx, "selected graph already exceeds the %d node authoring limit", SHADER_NODE_MAX);
        free_edit_context(&ctx);
        return status;
    }
    yyjson_arr_iter iterator;
    yyjson_arr_iter_init(operations, &iterator);
    yyjson_val *operation;
    int index = 0;
    while ((operation = yyjson_arr_iter_next(&iterator))) {
        ctx.operation = index++;
        if (apply_operation(&ctx, operation)) {
            add_failure(&ctx);
            free_edit_context(&ctx);
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
    }
    ctx.operation = index;
    int validation = validate_asset(&ctx);
    if (validation) {
        add_failure(&ctx);
        free_edit_context(&ctx);
        return validation;
    }
    for (int i = 0; i < acount(ctx.layout_graphs); i++) {
        graph_layout(ctx.layout_graphs[i]);
    }
    /* Publish only what loads back, in the form --format writes. */
    char *saved = behavior_asset_save(ctx.asset);
    behavior_asset *reloaded = saved ? behavior_asset_load(saved) : NULL;
    free(saved);
    if (!reloaded) {
        set_error(&ctx, "edited asset does not load back: %s", graph_last_error());
        add_failure(&ctx);
        free_edit_context(&ctx);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    char *serialized = behavior_asset_save(reloaded);
    behavior_asset_free(reloaded);
    assert(serialized);
    yyjson_mut_obj_add_uint(doc, result, "graph_id", ctx.graph->graph_id);
    yyjson_mut_obj_add_strcpy(
        doc,
        result,
        "description",
        ctx.asset->description ? ctx.asset->description : ""
    );
    yyjson_mut_obj_add_strcpy(
        doc,
        result,
        "graph_description",
        ctx.graph->description ? ctx.graph->description : ""
    );
    if (ctx.asset->owner_type_name) {
        yyjson_mut_obj_add_strcpy(doc, result, "owner_type", ctx.asset->owner_type_name);
    }
    yyjson_mut_obj_add_int(doc, result, "operation_count", index);
    add_refs(&ctx);
    *edited_text = serialized;
    free_edit_context(&ctx);
    return CHIBI_AUTOMATION_OK;
}
