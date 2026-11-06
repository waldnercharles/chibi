#include "graph/authoring/graph_edit.h"
#include "graph/authoring/pin_edit.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local char serialize_error[256];
void serialize_error_set(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(serialize_error, sizeof serialize_error, fmt, ap);
    va_end(ap);
}
const char *graph_last_error(void)
{
    return serialize_error;
}

static int read_id(av_value *v, uint64_t *out, const char *path)
{
    if (!v || !av_is_uint(v) || av_get_uint(v) == 0) {
        serialize_error_set("%s must be a non-zero unsigned integer", path);
        return -1;
    }
    *out = av_get_uint(v);
    return 0;
}

static int compact_component_node(const graph *g, const node *n)
{
    return g && g->dataflow && n && n->desc && n->desc->type &&
           n->desc->access && n->desc->name &&
           ((!strncmp(n->desc->name, "Get_", 4) && n->desc->access->read_count == 1) ||
            (!strncmp(n->desc->name, "Set_", 4) && n->desc->access->write_count == 1));
}

/* Absent keeps the template string; null clears it. */
static int read_pin_str(graph *g, av_value *pin, const char *key, const char **out)
{
    av_value *value = av_obj_get(pin, key);
    if (!value) {
        return 0;
    }
    if (av_is_null(value)) {
        *out = NULL;
        return 0;
    }
    if (!av_is_str(value)) {
        return -1;
    }
    *out = graph_own_str(g, av_get_str(value), av_get_len(value));
    return 0;
}

/* Event output bindings that name a pin resolve once every node is loaded. */
typedef struct pin_ref_binding
{
    node *node;
    int index;
    const char *ref;
    pin_binding binding;
} pin_ref_binding;

typedef struct load_refs
{
    pin_ref_binding *bindings;
} load_refs;

static int read_pin_default(graph *g, node *n, int output, int i, av_value *def)
{
    pin_spec *p = output ? &n->output_pins[i] : &n->input_pins[i];
    pin_value pv;
    if (p->container >= PIN_CONTAINER_ARRAY) {
        if (serialize_read_array(p->type, p->type_name, def, &pv, g)) {
            return -1;
        }
    } else if (serialize_read_value(p->type, p->type_name, def, &pv, g)) {
        if (p->type != PIN_STRUCT) {
            serialize_error_set(
                "bad default for pin %d on %s (type %d)",
                i,
                n->desc->name,
                p->type
            );
        }
        return -1;
    }
    pv.type_name = p->type_name;
    pv.container = p->container;
    p->default_value = pv;
    return 0;
}

/* Applies a pin's serialized fields over the pin it starts from. */
static void mark_derived_declaration(const graph *g, const node *n, pin_spec *p)
{
    if (g->owner && serialize_local_call(n)) {
        p->declaration_id = SERIALIZE_DERIVED_DECLARATION;
    }
}

static int apply_pin(graph *g, node *n, int output, int i, av_value *v, load_refs *refs)
{
    pin_spec *p = output ? &n->output_pins[i] : &n->input_pins[i];
    av_value *name = av_obj_get(v, "name");
    av_value *type = av_obj_get(v, "type");
    av_value *kind = av_obj_get(v, "kind");
    pin_type type_value = p->type;
    bool retyped = type && serialize_read_pin_type(type, &type_value) &&
                   type_value != p->type;
    if (retyped) {
        p->type_name = serialize_implied_type_name(type_value, p->type_name);
    }
    if ((name && !av_is_str(name)) || (kind && !av_is_str(kind)) ||
        (type && !serialize_read_pin_type(type, &type_value)) ||
        read_pin_str(g, v, "type_name", &p->type_name) ||
        read_pin_str(g, v, "split_type_name", &p->split_type_name) ||
        read_pin_str(g, v, "display_name", &p->display_name)) {
        serialize_error_set(
            "node %llu has malformed pin %d",
            (unsigned long long)n->id,
            i
        );
        return -1;
    }
    if (name) {
        p->name = graph_own_str(g, av_get_str(name), av_get_len(name));
    }
    if (p->display_name && !p->display_name[0]) {
        p->display_name = NULL;
    }
    p->type = type_value;
    if (kind) {
        p->kind = strcmp(av_get_str(kind), "exec") == 0 ? PIN_EXEC : PIN_DATA;
    }
    av_value *container = av_obj_get(v, "container");
    if (container && av_is_int(container) &&
        (pin_container)av_get_sint(container) != p->container) {
        p->container = (pin_container)av_get_sint(container);
        retyped = true;
    }
    av_value *split = av_obj_get(v, "split_parent");
    if (split && av_is_uint(split)) {
        p->split_parent = av_get_uint(split);
    }
    av_value *field_source = av_obj_get(v, "field_source");
    av_value *field_name = av_obj_get(v, "field_name");
    if ((field_source || field_name) &&
        (!output || !av_is_uint(field_source) || !av_get_uint(field_source) ||
         !av_is_str(field_name) || !av_get_len(field_name))) {
        serialize_error_set("invalid exposed field metadata");
        return -1;
    }
    if (field_source) {
        p->field_source = av_get_uint(field_source);
        p->field_name = graph_own_str(g, av_get_str(field_name), av_get_len(field_name));
    }
    av_value *flags = av_obj_get(v, "flags");
    av_value *declaration = av_obj_get(v, "declaration_id");
    if (declaration) {
        p->declaration_id = av_get_uint(declaration);
    } else {
        mark_derived_declaration(g, n, p);
    }
    if (flags && av_is_uint(flags)) {
        p->flags = (unsigned)av_get_uint(flags);
    } else if (n->desc == &NODE_BehaviorExpression && p->kind == PIN_EXEC) {
        /* An expression call's template is a wildcard data pin. */
        p->flags &= ~PIN_FLAG_WILDCARD;
    }
    av_value *group = av_obj_get(v, "group");
    if (group && av_is_int(group)) {
        p->group = (int)av_get_sint(group);
    }
    av_value *def = av_obj_get(v, "default");
    if (retyped && !def) {
        p->default_value = serialize_zero_value(p->type, p->container, p->type_name);
    }
    if (def && read_pin_default(g, n, output, i, def)) {
        return -1;
    }
    av_value *binding_value = av_obj_get(v, "binding");
    pin_ref_binding pending = { 0 };
    if (binding_value) {
        av_value *binding_kind = av_obj_get(binding_value, "kind");
        if (output || !av_is_obj(binding_value) || !av_is_str(binding_kind)) {
            serialize_error_set("invalid pin binding on %s pin %d", output ? "output" : "input", i);
            return -1;
        }
        const char *kind_name = av_get_str(binding_kind);
        pin_binding binding = { 0 };
        if (!strcmp(kind_name, "variable")) {
            binding.kind = PIN_BINDING_VARIABLE;
            av_value *id = av_obj_get(binding_value, "variable_id");
            av_value *name = av_obj_get(binding_value, "name");
            const behavior_variable *variable = av_is_str(name)
                                                  ? behavior_asset_find_variable_by_name(
                                                        g->owner,
                                                        av_get_str(name)
                                                    )
                                                  : NULL;
            if (name && (!variable || id)) {
                serialize_error_set("variable pin binding %d has an unknown name or conflicting ID", i);
                return -1;
            }
            if (!variable && (!av_is_uint(id) || !av_get_uint(id))) {
                serialize_error_set("variable pin binding %d has no variable identity", i);
                return -1;
            }
            binding.variable_id = variable ? variable->id : av_get_uint(id);
        } else if (!strcmp(kind_name, "component_field")) {
            binding.kind = PIN_BINDING_COMPONENT_FIELD;
            av_value *component = av_obj_get(binding_value, "component");
            av_value *field = av_obj_get(binding_value, "field");
            if (!av_is_str(component) || !av_get_len(component) ||
                !av_is_str(field) || !av_get_len(field)) {
                serialize_error_set("component pin binding %d is incomplete", i);
                return -1;
            }
            binding.component_key = av_get_str(component);
            binding.field_name = av_get_str(field);
        } else if (!strcmp(kind_name, "component")) {
            binding.kind = PIN_BINDING_COMPONENT;
            av_value *component = av_obj_get(binding_value, "component");
            if (!av_is_str(component) || !av_get_len(component)) {
                serialize_error_set("component pin binding %d has no component key", i);
                return -1;
            }
            binding.component_key = av_get_str(component);
        } else if (!strcmp(kind_name, "local")) {
            binding.kind = PIN_BINDING_LOCAL;
            av_value *local_id = av_obj_get(binding_value, "local_id");
            av_value *name = av_obj_get(binding_value, "name");
            const graph_local *local = NULL;
            for (int at = 0; av_is_str(name) && at < acount(g->locals); at++) {
                if (!strcmp(g->locals[at].name, av_get_str(name))) {
                    local = &g->locals[at];
                    break;
                }
            }
            if (name && (!local || local_id)) {
                serialize_error_set("local binding has an unknown name or conflicting ID");
                return -1;
            }
            if (!local && (!av_is_uint(local_id) || !av_get_uint(local_id))) {
                serialize_error_set("local binding has no local identity");
                return -1;
            }
            binding.local_id = local ? local->id : av_get_uint(local_id);
        } else if (!strcmp(kind_name, "event_output")) {
            binding.kind = PIN_BINDING_EVENT_OUTPUT;
            av_value *from = av_obj_get(binding_value, "from");
            av_value *event_node_id = av_is_uint(from)
                                        ? from
                                        : av_obj_get(binding_value, "event_node_id");
            av_value *event_pin_id = av_obj_get(binding_value, "event_pin_id");
            if (av_is_str(from)) {
                /* The event node may follow this one; resolve after loading nodes. */
                pending = (pin_ref_binding){ .node = n,
                                             .index = i,
                                             .ref = av_get_str(from) };
            } else if (!av_is_uint(event_node_id) || !av_get_uint(event_node_id) || !av_is_uint(event_pin_id) || !av_get_uint(event_pin_id)) {
                serialize_error_set("event output binding is incomplete");
                return -1;
            } else {
                binding.event_node_id = av_get_uint(event_node_id);
                binding.event_pin_id = av_get_uint(event_pin_id);
            }
        } else {
            serialize_error_set("unknown pin binding kind '%s'", kind_name);
            return -1;
        }
        av_value *source_asset = av_obj_get(binding_value, "source_asset");
        if (source_asset && !av_is_str(source_asset)) {
            serialize_error_set("invalid pin binding source asset");
            return -1;
        }
        binding.source_asset = source_asset ? av_get_str(source_asset) : NULL;
        if (pending.ref) {
            pending.binding = binding;
            apush(refs->bindings, pending);
        } else if (graph_set_pin_binding(g, n, 0, i, &binding) != 0) {
            serialize_error_set("could not store pin binding %d", i);
            return -1;
        }
    }
    return 0;
}

static void set_explicit_id(uint64_t **ids, int index, uint64_t id)
{
    while (acount(*ids) <= index) {
        apush(*ids, 0);
    }
    (*ids)[index] = id;
}

/* A listed side is positional: each entry is a pin ID or an object with its
 * ID and the fields that differ from the node descriptor's template pin. */
static int load_pin_list(graph *g, node *n, av_value *arr, int output, uint64_t **ids, load_refs *refs)
{
    av_arr_iter it;
    av_arr_iter_init(arr, &it);
    av_value *v;
    int i = 0;
    int count = output ? n->output_count : n->input_count;
    int serialized_count = (int)av_arr_size(arr);
    graph_pin_run run;
    if (serialized_count < count && graph_pin_run_of(n, &run) &&
        run.output == output && serialized_count >= count - run.count) {
        while (count > serialized_count) {
            if (graph_remove_pin(g, n, output, run.first + run.count - 1)) {
                serialize_error_set(
                    "node %llu cannot shrink its variadic pin run",
                    (unsigned long long)n->id
                );
                return -1;
            }
            run.count--;
            count--;
        }
    }
    if (serialized_count < count &&
        (n->desc == &NODE_FunctionEntry || n->desc == &NODE_Return ||
         compact_component_node(g, n))) {
        while (count > serialized_count) {
            if (graph_remove_pin(g, n, output, count - 1)) {
                serialize_error_set(
                    "node %llu cannot shrink %s pin list",
                    (unsigned long long)n->id,
                    output ? "output" : "input"
                );
                return -1;
            }
            count--;
        }
    }
    while ((v = av_arr_iter_next(&it))) {
        if (!av_is_obj(v) && !av_is_uint(v)) {
            serialize_error_set(
                "node %llu has invalid %s pin list",
                (unsigned long long)n->id,
                output ? "output" : "input"
            );
            return -1;
        }
        if (i >= count) {
            if (graph_add_pin(g, n, output, serialize_pin_template(n->desc, output, i)) <
                0) {
                serialize_error_set(
                    "node %llu cannot extend %s pin list",
                    (unsigned long long)n->id,
                    output ? "output" : "input"
                );
                return -1;
            }
            count++;
        }
        uint64_t id = 0;
        if (read_id(av_is_uint(v) ? v : av_obj_get(v, "id"), &id, "pin.id")) {
            return -1;
        }
        set_explicit_id(ids, i, id);
        if (!av_is_obj(v)) {
            mark_derived_declaration(g, n, output ? &n->output_pins[i] : &n->input_pins[i]);
        }
        if (av_is_obj(v) && apply_pin(g, n, output, i, v, refs)) {
            return -1;
        }
        i++;
    }
    if (i != count) {
        serialize_error_set(
            "node %llu pin count does not match descriptor",
            (unsigned long long)n->id
        );
        return -1;
    }
    return 0;
}

/* The pin a keyed entry that is not a template pin starts from: the exposed
 * field its path names, or the descriptor's extension template. */
static int keyed_template(node *n, int output, const char *key, pin_spec *out)
{
    const char *dot = output ? strrchr(key, '.') : NULL;
    if (dot) {
        char source_key[SERIALIZE_KEY_SIZE];
        snprintf(source_key, sizeof source_key, "%.*s", (int)(dot - key), key);
        int source = serialize_pin_by_key(n, 1, source_key);
        const type_desc *type = source >= 0 ? graph_output_struct(n, source) : NULL;
        for (int i = 0; type && i < type->field_count; i++) {
            if (!strcmp(type->fields[i].name, dot + 1)) {
                *out = graph_field_pin(n, source, &type->fields[i]);
                return 0;
            }
        }
        return -1;
    }
    if (component_node_kind_of(n->desc)) {
        return !output && component_node_field_pin(n, key, out) ? 0 : -1;
    }
    int count = output ? n->output_count : n->input_count;
    *out = *serialize_pin_template(n->desc, output, count);
    out->name = key;
    return 0;
}

/* A keyed side maps pin keys to the fields that differ. Template pins keep
 * their positions; other keys add pins in order. */
static int load_pin_map(graph *g, node *n, av_value *map, int output, uint64_t **ids, load_refs *refs)
{
    /* A component node's template is its prepared pins. */
    bool component = component_node_kind_of(n->desc);
    int template_count = component ? component_node_template_count(n, output)
                       : output    ? n->desc->output_count
                                   : n->desc->input_count;
    size_t at, max;
    av_value *key, *v;
    av_obj_foreach(map, at, max, key, v)
    {
        const pin_spec *template_pins = component
                                          ? (output ? n->output_pins : n->input_pins)
                                      : output ? n->desc->outputs
                                               : n->desc->inputs;
        const char *name = av_get_str(key);
        int i = -1;
        for (int t = 0; t < template_count && i < 0; t++) {
            i = template_pins[t].name && !strcmp(template_pins[t].name, name) ? t : -1;
        }
        pin_spec base;
        if (i < 0 && (keyed_template(n, output, name, &base) ||
                      (i = graph_add_pin(g, n, output, &base)) < 0)) {
            serialize_error_set("node %llu has no pin %s", (unsigned long long)n->id, name);
            return -1;
        }
        av_value *id = av_is_obj(v) ? av_obj_get(v, "id") : NULL;
        uint64_t explicit_id = 0;
        if (id && read_id(id, &explicit_id, "pin.id")) {
            return -1;
        }
        set_explicit_id(ids, i, explicit_id);
        int failed = av_is_obj(v) ? apply_pin(g, n, output, i, v, refs)
                                  : read_pin_default(g, n, output, i, v);
        if (failed) {
            return -1;
        }
        if (!av_is_obj(v)) {
            mark_derived_declaration(g, n, output ? &n->output_pins[i] : &n->input_pins[i]);
        }
    }
    return 0;
}

static uint64_t remap_pin_id(uint64_t id, const uint64_t *old_ids, const uint64_t *new_ids, int count)
{
    for (int i = 0; id && i < count; i++) {
        if (old_ids[i] == id) {
            return new_ids[i];
        }
    }
    return id;
}

/* Unwritten pin IDs continue from the node ID: inputs, then outputs. */
static void assign_pin_ids(node *n, uint64_t **ids)
{
    uint64_t *old_ids = NULL, *new_ids = NULL;
    uint64_t previous = n->id;
    for (int output = 0; output < 2; output++) {
        pin_spec *pins = output ? n->output_pins : n->input_pins;
        int count = output ? n->output_count : n->input_count;
        for (int i = 0; i < count; i++) {
            uint64_t id = i < acount(ids[output]) && ids[output][i]
                            ? ids[output][i]
                            : previous + 1;
            apush(old_ids, pins[i].id);
            apush(new_ids, id);
            pins[i].id = id;
            previous = id;
        }
    }
    for (int output = 0; output < 2; output++) {
        pin_spec *pins = output ? n->output_pins : n->input_pins;
        int count = output ? n->output_count : n->input_count;
        for (int i = 0; i < count; i++) {
            pins[i].field_source = remap_pin_id(pins[i].field_source, old_ids, new_ids, acount(old_ids));
            pins[i].split_parent = remap_pin_id(pins[i].split_parent, old_ids, new_ids, acount(old_ids));
        }
    }
    afree(old_ids);
    afree(new_ids);
}

static int load_node_pins(graph *g, node *n, av_value *v, load_refs *refs)
{
    uint64_t *ids[2] = { NULL, NULL };
    int failed = 0;
    for (int output = 0; output < 2 && !failed; output++) {
        av_value *pins = av_obj_get(v, output ? "outputs" : "inputs");
        if (av_is_arr(pins)) {
            failed = load_pin_list(g, n, pins, output, &ids[output], refs);
        } else if (av_is_obj(pins)) {
            failed = load_pin_map(g, n, pins, output, &ids[output], refs);
        } else if (pins) {
            serialize_error_set(
                "node %llu has an invalid %s pin list",
                (unsigned long long)n->id,
                output ? "output" : "input"
            );
            failed = -1;
        }
    }
    if (!failed) {
        assign_pin_ids(n, ids);
    }
    afree(ids[0]);
    afree(ids[1]);
    return failed;
}

static int resolve_pin_refs(graph *g, load_refs *refs)
{
    for (int i = 0; i < acount(refs->bindings); i++) {
        pin_ref_binding *pending = &refs->bindings[i];
        node *event = NULL;
        int index = -1;
        if (!serialize_resolve_ref(g, pending->ref, 1, &event, &index)) {
            serialize_error_set("event output binding names no pin: %s", pending->ref);
            return -1;
        }
        pending->binding.event_node_id = event->id;
        pending->binding.event_pin_id = event->output_pins[index].id;
        if (graph_set_pin_binding(g, pending->node, 0, pending->index, &pending->binding) !=
            0) {
            serialize_error_set("could not store pin binding %d", pending->index);
            return -1;
        }
    }
    return 0;
}

static int load_comment_boxes(graph *g, av_value *document)
{
    if (!document) {
        return 0;
    }
    if (!av_is_arr(document)) {
        serialize_error_set("comment_boxes must be an array");
        return -1;
    }
    av_arr_iter it;
    av_arr_iter_init(document, &it);
    av_value *value;
    while ((value = av_arr_iter_next(&it))) {
        uint64_t id = 0;
        av_value *title = av_obj_get(value, "title");
        if (!av_is_obj(value) ||
            read_id(av_obj_get(value, "id"), &id, "comment_box.id") ||
            !av_is_str(title)) {
            serialize_error_set("malformed comment box");
            return -1;
        }
        graph_comment_box box = {
            .id = id,
            .x = (float)av_get_num(av_obj_get(value, "x")),
            .y = (float)av_get_num(av_obj_get(value, "y")),
            .width = (float)av_get_num(av_obj_get(value, "width")),
            .height = (float)av_get_num(av_obj_get(value, "height")),
            .title = graph_own_str(g, av_get_str(title), av_get_len(title)),
            .color = (uint32_t)av_get_uint(av_obj_get(value, "color")),
        };
        apush(g->comment_boxes, box);
        if (id >= g->id_counter) {
            g->id_counter = id + 1;
        }
    }
    return 0;
}

/* Documents omit an empty comment, and a hidden flag only a comment has. */
static int load_node_comment(graph *g, node *n, av_value *v)
{
    av_value *comment = av_obj_get(v, "comment");
    av_value *hidden = av_obj_get(v, "comment_hidden");
    if ((comment && !av_is_str(comment)) || (hidden && !av_is_bool(hidden))) {
        serialize_error_set(
            "node %llu has an invalid comment",
            (unsigned long long)n->id
        );
        return -1;
    }
    if (av_get_len(comment)) {
        n->comment = graph_own_str(g, av_get_str(comment), av_get_len(comment));
        n->comment_hidden = av_get_bool(hidden);
    }
    return 0;
}

/* Documents omit `auto`, which new nodes already hold. */
static int load_node_presentation(node *n, av_value *v)
{
    av_value *presentation = av_obj_get(v, "presentation");
    if (!presentation) {
        return 0;
    }
    int mode = graph_presentation_by_name(av_get_str(presentation));
    if (mode < 0) {
        serialize_error_set("node presentation must be auto, inline or expanded");
        return -1;
    }
    n->presentation = (node_presentation)mode;
    return 0;
}

/* A component node's pins follow its Component or Type setting, and a Set
 * that names field inputs writes those fields instead of Value. Documents may
 * name the component; the setting stores its key. */
static int load_component_pins(graph *g, node *n, av_value *v, load_refs *refs)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    if (!kind) {
        return load_node_pins(g, n, v, refs);
    }
    av_value *inputs = av_obj_get(v, "inputs");
    if (inputs && !av_is_obj(inputs)) {
        serialize_error_set(
            "component node %llu must key its inputs",
            (unsigned long long)n->id
        );
        return -1;
    }
    const char *setting_name = n->input_pins[component_node_setting(n)].name;
    av_value *setting = av_obj_get(inputs, setting_name);
    av_value *key = av_is_obj(setting) ? av_obj_get(setting, "default") : setting;
    const char *component = av_get_str(key);
    bool split = false;
    size_t at, max;
    av_value *name, *entry;
    if (inputs) {
        av_obj_foreach(inputs, at, max, name, entry)
        {
            split |= kind == COMPONENT_NODE_SET &&
                     component_field_settable(component_by_key(component), av_get_str(name));
        }
    }
    if (component_node_prepare(g, n, component, split)) {
        serialize_error_set("unknown %s \"%s\"", setting_name, component);
        return -1;
    }
    if (load_node_pins(g, n, v, refs)) {
        return -1;
    }
    const char *canonical = component_node_canonical_key(kind, component);
    n->input_pins[component_node_setting(n)].default_value.str = canonical ? canonical
                                                                           : "";
    return 0;
}

static int load_node_list(graph *g, av_value *nodes, uint64_t requested_counter, load_refs *refs)
{
    av_arr_iter it;
    av_arr_iter_init(nodes, &it);
    av_value *v;
    while ((v = av_arr_iter_next(&it))) {
        if (!av_is_obj(v)) {
            serialize_error_set("node is not an object");
            return -1;
        }
        av_value *type = av_obj_get(v, "type");
        if (!av_is_str(type)) {
            serialize_error_set("node is missing type");
            return -1;
        }
        const node_desc *desc = registry_node_by_name(av_get_str(type));
        if (!desc) {
            serialize_error_set("unknown node type \"%s\"", av_get_str(type));
            return -1;
        }
        node *n = graph_add_node(g, desc);
        if (!n) {
            serialize_error_set("out of memory");
            return -1;
        }
        uint64_t id = 0;
        if (read_id(av_obj_get(v, "id"), &id, "node.id")) {
            return -1;
        }
        n->id = id;
        av_value *x = av_obj_get(v, "x"), *y = av_obj_get(v, "y");
        av_value *placed = av_obj_get(v, "placed");
        if ((x && !av_is_num(x)) || (y && !av_is_num(y)) || !x != !y ||
            (placed && !av_is_bool(placed))) {
            serialize_error_set("node %llu has an invalid position", (unsigned long long)id);
            return -1;
        }
        /* Nodes without a position wait for the editor to arrange them. */
        n->x = x ? (float)av_get_num(x) : 0.0f;
        n->y = y ? (float)av_get_num(y) : 0.0f;
        n->placed = x && (!placed || av_get_bool(placed));
        av_value *refasset = av_obj_get(v, "reference_asset");
        if (av_is_str(refasset)) {
            n->reference_asset = graph_own_str(g, av_get_str(refasset), av_get_len(refasset));
        }
        av_value *refname = av_obj_get(v, "reference_name");
        av_value *refid = av_obj_get(v, "reference_id");
        if (refid && av_is_uint(refid)) {
            n->reference_id = av_get_uint(refid);
        }
        if (refname && av_is_str(refname)) {
            n->reference_name = graph_own_str(g, av_get_str(refname), av_get_len(refname));
        }
        /* Reference-valued getter edges need their variable identity while
         * connections load. Variables precede every graph in a full asset. */
        if (!n->reference_id && !n->reference_asset && n->reference_name &&
            (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable)) {
            const behavior_variable
                *variable = behavior_asset_find_variable_by_name(g->owner, n->reference_name);
            if (!variable) {
                serialize_error_set("unknown variable reference: %s", n->reference_name);
                return -1;
            }
            n->reference_id = variable->id;
        }
        av_value *disabled = av_obj_get(v, "disabled");
        n->disabled = disabled && av_is_bool(disabled) && av_get_bool(disabled);
        if (load_node_presentation(n, v) || load_node_comment(g, n, v)) {
            return -1;
        }
        av_value *breakpoint = av_obj_get(v, "breakpoint");
        av_value *breakpoint_enabled = av_obj_get(v, "breakpoint_enabled");
        n->show_advanced = av_get_bool(av_obj_get(v, "show_advanced"));
        n->breakpoint = breakpoint && av_is_bool(breakpoint) &&
                        av_get_bool(breakpoint);
        n->breakpoint_enabled = breakpoint_enabled &&
                                av_is_bool(breakpoint_enabled) &&
                                av_get_bool(breakpoint_enabled);
        if (load_component_pins(g, n, v, refs)) {
            return -1;
        }
        if (n->id == 0) {
            serialize_error_set("node ID zero is not allowed");
            return -1;
        }
        for (int j = 0; j < acount(g->nodes) - 1; j++) {
            node *other = g->nodes[j];
            if (other->id == n->id) {
                serialize_error_set("duplicate node ID");
                return -1;
            }
            for (int p = 0; p < other->input_count; p++) {
                if (other->input_pins[p].id == n->id) {
                    serialize_error_set("node ID collides with pin ID");
                    return -1;
                }
            }
            for (int p = 0; p < other->output_count; p++) {
                if (other->output_pins[p].id == n->id) {
                    serialize_error_set("node ID collides with pin ID");
                    return -1;
                }
            }
            for (int p = 0; p < n->input_count; p++) {
                for (int q = 0; q < other->input_count; q++) {
                    if (other->input_pins[q].id == n->input_pins[p].id) {
                        serialize_error_set("duplicate pin ID");
                        return -1;
                    }
                }
            }
            for (int p = 0; p < n->output_count; p++) {
                for (int q = 0; q < other->output_count; q++) {
                    if (other->output_pins[q].id == n->output_pins[p].id) {
                        serialize_error_set("duplicate pin ID");
                        return -1;
                    }
                }
            }
        }
        for (int p = 0; p < n->input_count; p++) {
            if (n->input_pins[p].id == 0) {
                serialize_error_set("pin ID zero is not allowed");
                return -1;
            }
            for (int q = p + 1; q < n->input_count; q++) {
                if (n->input_pins[p].id == n->input_pins[q].id) {
                    serialize_error_set("duplicate pin ID");
                    return -1;
                }
            }
            for (int q = 0; q < n->output_count; q++) {
                if (n->input_pins[p].id == n->output_pins[q].id) {
                    serialize_error_set("duplicate pin ID");
                    return -1;
                }
            }
        }
        for (int p = 0; p < n->output_count; p++) {
            if (n->output_pins[p].id == 0) {
                serialize_error_set("pin ID zero is not allowed");
                return -1;
            }
            for (int q = p + 1; q < n->output_count; q++) {
                if (n->output_pins[p].id == n->output_pins[q].id) {
                    serialize_error_set("duplicate pin ID");
                    return -1;
                }
            }
        }
        if (n->id >= g->id_counter) {
            g->id_counter = n->id + 1;
        }
        for (int i = 0; i < n->input_count; i++) {
            if (n->input_pins[i].id >= g->id_counter) {
                g->id_counter = n->input_pins[i].id + 1;
            }
        }
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].id >= g->id_counter) {
                g->id_counter = n->output_pins[i].id + 1;
            }
        }
    }
    /* graph_add_node inflates the counter while allocating scratch ids;
     * restore the serialized floor, then ID-2 keeps the counter at or above
     * every loaded id so new allocations never reissue a retired ID. */
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        for (int p = 0; p < n->output_count; p++) {
            if ((n->output_pins[p].field_source || n->output_pins[p].field_name) &&
                !graph_output_field(n, p, NULL)) {
                serialize_error_set("invalid exposed field metadata");
                return -1;
            }
        }
    }
    g->id_counter = requested_counter;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (n->id >= g->id_counter) {
            g->id_counter = n->id + 1;
        }
        for (int p = 0; p < n->input_count; p++) {
            if (n->input_pins[p].id >= g->id_counter) {
                g->id_counter = n->input_pins[p].id + 1;
            }
        }
        for (int p = 0; p < n->output_count; p++) {
            if (n->output_pins[p].id >= g->id_counter) {
                g->id_counter = n->output_pins[p].id + 1;
            }
        }
    }
    graph_reindex(g);
    return resolve_pin_refs(g, refs);
}

static int load_nodes(graph *g, av_value *nodes, uint64_t requested_counter)
{
    load_refs refs = { 0 };
    int failed = load_node_list(g, nodes, requested_counter, &refs);
    afree(refs.bindings);
    return failed;
}

/* An endpoint is "<node>.<pin key>", or a node ID with a pin ID field. */
static int read_endpoint(
    graph *g,
    av_value *v,
    const char *key,
    const char *pin_key,
    int output,
    node **n,
    uint64_t *node_id,
    uint64_t *pin_id
)
{
    av_value *ref = av_obj_get(v, key);
    int index = -1;
    if (av_is_str(ref)) {
        if (!serialize_resolve_ref(g, av_get_str(ref), output, n, &index)) {
            serialize_error_set("edge names no pin: %s", av_get_str(ref));
            return -1;
        }
        *node_id = (*n)->id;
        *pin_id = output ? (*n)->output_pins[index].id : (*n)->input_pins[index].id;
        return 0;
    }
    if (read_id(ref, node_id, key) || read_id(av_obj_get(v, pin_key), pin_id, pin_key)) {
        return -1;
    }
    *n = graph_node_by_id(g, *node_id);
    return 0;
}

static int load_edges(graph *g, av_value *edges)
{
    av_arr_iter eit;
    av_arr_iter_init(edges, &eit);
    av_value *v;
    while ((v = av_arr_iter_next(&eit))) {
        if (!av_is_obj(v)) {
            serialize_error_set("edge is not an object");
            return -1;
        }
        uint64_t from_id, from_pin, to_id, to_pin;
        node *from = NULL, *to = NULL;
        if (read_endpoint(g, v, "from", "from_pin", 1, &from, &from_id, &from_pin) ||
            read_endpoint(g, v, "to", "to_pin", 0, &to, &to_id, &to_pin)) {
            return -1;
        }
        int fp = -1, tp = -1, fo = 0, to_out = 0;
        const pin_spec *fps = graph_pin_by_id(from, from_pin, &fo, &fp),
                       *tps = graph_pin_by_id(to, to_pin, &to_out, &tp);
        av_value *kind = av_obj_get(v, "kind");
        if (!from || !to || !fps || !tps || fo != 1 || to_out != 0 ||
            (kind && !av_is_str(kind))) {
            serialize_error_set("edge references missing node or pin ID");
            return -1;
        }
        const char *kind_name = kind                  ? av_get_str(kind)
                              : fps->kind == PIN_EXEC ? "exec"
                                                      : "data";
        av_value *conversion_value = av_obj_get(v, "conversion");
        const node_desc *conversion = NULL;
        if (conversion_value) {
            if (!av_is_str(conversion_value)) {
                serialize_error_set("edge conversion is not a node name");
                return -1;
            }
            conversion = registry_node_by_name(av_get_str(conversion_value));
            if (!conversion) {
                serialize_error_set("unknown edge conversion \"%s\"", av_get_str(conversion_value));
                return -1;
            }
        }
        int r;
        if (strcmp(kind_name, "data") == 0) {
            if (av_obj_get(v, "order")) {
                serialize_error_set("data edge has an exec order");
                return -1;
            }
            unsigned flags = to->input_pins[tp].flags;
            unsigned source_flags = from->output_pins[fp].flags;
            bool fixed_conversion = conversion &&
                                    conversion_matches_pins(fps, tps, conversion);
            if (fixed_conversion || (!conversion && pin_compatible(fps, tps))) {
                /* Snapshot pins already declare storage. Replaying a compatible
                 * driver must preserve the consumer's nominal type and width. */
                to->input_pins[tp].flags &= ~PIN_FLAG_WILDCARD;
                if (!conversion && fps->type != PIN_FLOW && tps->type != PIN_FLOW) {
                    from->output_pins[fp].flags &= ~PIN_FLAG_WILDCARD;
                }
            }
            r = graph_connect_data_conversion(g, from, fp, to, tp, conversion);
            to->input_pins[tp].flags = flags;
            from->output_pins[fp].flags = source_flags;
        } else if (strcmp(kind_name, "exec") == 0) {
            if (conversion) {
                serialize_error_set("exec edge cannot have a conversion");
                return -1;
            }
            av_value *order = av_obj_get(v, "order");
            if (order && (!av_is_int(order) || av_get_sint(order) < 0 ||
                          av_get_sint(order) > INT_MAX)) {
                serialize_error_set("exec edge has an invalid order");
                return -1;
            }
            r = graph_connect_exec(g, from, fp, to, tp, order ? (int)av_get_sint(order) : 0);
        } else {
            serialize_error_set("unsupported edge kind");
            return -1;
        }
        if (r) {
            serialize_error_set("invalid edge endpoints or incompatible types");
            return -1;
        }
        av_value *handle = av_obj_get(v, "from_handle_x");
        av_value *handle_y = av_obj_get(v, "from_handle_y");
        av_value *to_handle = av_obj_get(v, "to_handle_x");
        av_value *to_handle_y = av_obj_get(v, "to_handle_y");
        if (handle || handle_y || to_handle || to_handle_y) {
            if (!handle || !handle_y || !to_handle || !to_handle_y ||
                !av_is_num(handle) || !av_is_num(handle_y) ||
                !av_is_num(to_handle) || !av_is_num(to_handle_y)) {
                serialize_error_set("edge has invalid bezier handles");
                return -1;
            }
            edge *loaded = NULL;
            for (int i = acount(g->edges) - 1; i >= 0; i--) {
                if (g->edges[i].from_id == from_id && g->edges[i].from_pin_id == from_pin &&
                    g->edges[i].to_id == to_id && g->edges[i].to_pin_id == to_pin) {
                    loaded = &g->edges[i];
                    break;
                }
            }
            assert(loaded);
            loaded->from_handle_x = (float)av_get_num(handle);
            loaded->from_handle_y = (float)av_get_num(handle_y);
            loaded->to_handle_x = (float)av_get_num(to_handle);
            loaded->to_handle_y = (float)av_get_num(to_handle_y);
            loaded->custom_handles = true;
        }
        av_value *points = av_obj_get(v, "reroutes");
        if (points) {
            if (!av_is_arr(points) || av_arr_size(points) > GRAPH_EDGE_MAX_REROUTES) {
                serialize_error_set("edge has invalid reroute points");
                return -1;
            }
            edge *loaded = NULL;
            for (int i = acount(g->edges) - 1; i >= 0; i--) {
                if (g->edges[i].from_id == from_id && g->edges[i].from_pin_id == from_pin &&
                    g->edges[i].to_id == to_id && g->edges[i].to_pin_id == to_pin) {
                    loaded = &g->edges[i];
                    break;
                }
            }
            assert(loaded);
            loaded->reroute_count = 0;
            av_arr_iter pit;
            av_arr_iter_init(points, &pit);
            av_value *point;
            while ((point = av_arr_iter_next(&pit))) {
                av_value *x = av_obj_get(point, "x"), *y = av_obj_get(point, "y");
                if (!av_is_obj(point) || !av_is_num(x) || !av_is_num(y)) {
                    serialize_error_set("edge has invalid reroute point");
                    return -1;
                }
                loaded->reroutes[loaded->reroute_count].x = (float)av_get_num(x);
                loaded->reroutes[loaded->reroute_count].y = (float)av_get_num(y);
                loaded->reroute_count++;
            }
        }
    }
    return 0;
}

graph *graph_load(const char *text)
{
    char error[160];
    av_doc *doc = text ? av_doc_read(text, error, sizeof error) : NULL;
    if (!doc) {
        serialize_error_set("%s", text ? error : "null graph input");
        return NULL;
    }
    graph *result = graph_from_value(av_doc_root(doc));
    av_doc_free(doc);
    return result;
}

graph *graph_from_value(av_value *root)
{
    av_value *version = av_is_obj(root) ? av_obj_get(root, "version") : NULL;
    if (!av_is_uint(version) || av_get_uint(version) != GRAPH_SCHEMA_VERSION) {
        serialize_error_set("unsupported graph schema version (expected 3)");
        return NULL;
    }
    av_value *nodes = av_obj_get(root, "nodes"), *edges = av_obj_get(root, "edges");
    if ((nodes && !av_is_arr(nodes)) || (edges && !av_is_arr(edges))) {
        serialize_error_set("invalid nodes or edges array");
        return NULL;
    }
    graph *g = (graph *)calloc(1, sizeof *g);
    if (!g) {
        serialize_error_set("out of memory");
        return NULL;
    }
    graph_init(g);
    uint64_t requested_counter = 1;
    av_value *counter = av_obj_get(root, "id_counter");
    if (!counter || !av_is_uint(counter) || av_get_uint(counter) == 0) {
        serialize_error_set("missing or invalid id_counter");
        goto fail;
    }
    requested_counter = av_get_uint(counter);
    g->id_counter = requested_counter;
    av_value *metadata_name = av_obj_get(root, "name");
    av_value *metadata_description = av_obj_get(root, "description");
    if (av_is_str(metadata_name)) {
        g->name = graph_own_str(g, av_get_str(metadata_name), av_get_len(metadata_name));
    }
    const char *description = av_is_str(metadata_description)
                                ? av_get_str(metadata_description)
                                : "";
    g->description = graph_own_str(g, description, strlen(description));
    if (!g->name || (metadata_description && !av_is_str(metadata_description))) {
        serialize_error_set("missing behavior_asset metadata");
        goto fail;
    }
    av_value *binding_owner = av_obj_get(root, "binding_owner_id");
    if (binding_owner) {
        if (!av_is_uint(binding_owner) || !av_get_uint(binding_owner)) {
            serialize_error_set("invalid binding clipboard owner");
            goto fail;
        }
        g->clipboard_owner_id = av_get_uint(binding_owner);
    }
    av_value *breakpoints = av_obj_get(root, "breakpoints_enabled");
    if (breakpoints && av_is_bool(breakpoints)) {
        g->breakpoints_enabled = av_get_bool(breakpoints);
    }
    av_value *dataflow = av_obj_get(root, "dataflow");
    if (dataflow && av_is_bool(dataflow)) {
        g->dataflow = av_get_bool(dataflow);
    }
    if (load_comment_boxes(g, av_obj_get(root, "comment_boxes"))) {
        goto fail;
    }
    if (serialize_read_locals(g, root) || load_nodes(g, nodes, requested_counter) ||
        serialize_resolve_locals(g, root) || load_edges(g, edges) ||
        serialize_validate_locals(g)) {
        goto fail;
    }
    return g;
fail:
    graph_free(g);
    free(g);
    return NULL;
}

graph *serialize_load_graph_body(av_value *obj, uint64_t id_counter)
{
    graph *g = xcalloc(1, sizeof *g);
    graph_init(g);
    g->id_counter = id_counter;
    av_value *nodes = av_obj_get(obj, "nodes"), *edges = av_obj_get(obj, "edges");
    if (serialize_read_locals(g, obj) || load_nodes(g, nodes, id_counter) ||
        serialize_resolve_locals(g, obj) || load_edges(g, edges) ||
        serialize_validate_locals(g)) {
        graph_free(g);
        free(g);
        return NULL;
    }
    return g;
}

static int load_variable(behavior_asset *asset, av_value *v)
{
    if (!av_is_obj(v)) {
        serialize_error_set("variable is not an object");
        return -1;
    }
    uint64_t id = 0;
    if (read_id(av_obj_get(v, "id"), &id, "variable.id")) {
        return -1;
    }
    if (behavior_asset_find_variable_by_id(asset, id)) {
        serialize_error_set("duplicate variable ID");
        return -1;
    }
    av_value *name = av_obj_get(v, "name");
    pin_type pt;
    if (!av_is_str(name) || !serialize_read_pin_type(av_obj_get(v, "type"), &pt)) {
        serialize_error_set("variable has malformed name or type");
        return -1;
    }
    av_value *tn = av_obj_get(v, "type_name");
    const char *type_name = av_is_str(tn) ? av_get_str(tn) : NULL;
    av_value *container = av_obj_get(v, "container");
    pin_container pc = container && av_is_int(container)
                         ? (pin_container)av_get_sint(container)
                         : PIN_CONTAINER_SINGLE;
    av_value *category = av_obj_get(v, "category");
    const char *cat = av_is_str(category) ? av_get_str(category) : NULL;
    av_value *def = av_obj_get(v, "default");
    pin_value pv = { 0 };
    if (def) {
        if (pc >= PIN_CONTAINER_ARRAY) {
            if (serialize_read_array(pt, type_name, def, &pv, asset->graph)) {
                return -1;
            }
        } else if (serialize_read_value(pt, type_name, def, &pv, asset->graph)) {
            if (pt != PIN_STRUCT) {
                serialize_error_set("bad default for variable %llu", (unsigned long long)id);
            }
            return -1;
        }
    }
    behavior_variable *var = behavior_asset_add_variable(
        asset,
        av_get_str(name),
        pt,
        type_name,
        pc,
        def ? &pv : NULL,
        cat
    );
    if (!var) {
        serialize_error_set("cannot create variable %s", av_get_str(name));
        return -1;
    }
    var->id = id;
    var->shared = av_get_bool(av_obj_get(v, "shared"));
    return 0;
}

/* Calls reach an expression by ID, so two expressions with one source may
   share the name that source gives them. Every other graph name is unique. */
static bool expression_titled_by_source(const graph *g)
{
    return g->math_expression && !strcmp(g->name, g->math_expression);
}

static int load_behavior_asset_graph(behavior_asset *asset, av_value *obj, uint64_t requested_counter, int is_default)
{
    if (!av_is_obj(obj)) {
        serialize_error_set("graph is not an object");
        return -1;
    }
    av_value *kind_value = av_obj_get(obj, "kind");
    av_value *name_value = av_obj_get(obj, "name");
    av_value *description_value = av_obj_get(obj, "description");
    av_value *nodes = av_obj_get(obj, "nodes");
    av_value *edges = av_obj_get(obj, "edges");
    graph_kind kind;
    /* An expression's name defaults to its source. */
    if (!name_value) {
        name_value = av_obj_get(obj, "math_expression");
    }
    if (!av_is_str(kind_value) ||
        graph_kind_from_name(av_get_str(kind_value), &kind) || !av_is_str(name_value) ||
        (description_value && !av_is_str(description_value)) ||
        (nodes && !av_is_arr(nodes)) || (edges && !av_is_arr(edges))) {
        serialize_error_set("graph is missing kind or name, or has invalid nodes or edges");
        return -1;
    }
    if (is_default && kind != GRAPH_EVENT) {
        serialize_error_set("the first Behavior Asset graph must be an event graph");
        return -1;
    }
    uint64_t graph_id = 0;
    if (!is_default && read_id(av_obj_get(obj, "id"), &graph_id, "graph.id")) {
        return -1;
    }
    av_value *expression = av_obj_get(obj, "math_expression");
    bool titled_by_source = av_is_str(expression) &&
                            !strcmp(av_get_str(expression), av_get_str(name_value));
    for (int i = 0; !is_default && i < acount(asset->graphs); i++) {
        if (asset->graphs[i]->graph_id == graph_id) {
            serialize_error_set("duplicate graph ID");
            return -1;
        }
        if (titled_by_source && expression_titled_by_source(asset->graphs[i])) {
            continue;
        }
        if (!strcmp(asset->graphs[i]->name, av_get_str(name_value))) {
            serialize_error_set("duplicate graph name: %s", av_get_str(name_value));
            return -1;
        }
    }
    graph *g = is_default ? asset->graph : NULL;
    if (!g) {
        g = (graph *)xcalloc(1, sizeof *g);
        graph_init(g);
        g->owner = asset;
        g->shared_id_counter = &asset->graph->id_counter;
        g->graph_id = graph_id;
        g->kind = kind;
        g->name = graph_own_str(g, av_get_str(name_value), av_get_len(name_value));
        apush(asset->graphs, g);
    } else {
        g->kind = kind;
        g->graph_id = graph_id;
        g->name = graph_own_str(g, av_get_str(name_value), av_get_len(name_value));
    }
    if (!g->name) {
        serialize_error_set("graph name allocation failed");
        return -1;
    }
    const char *description = description_value ? av_get_str(description_value) : "";
    g->description = graph_own_str(g, description, strlen(description));
    if (!g->description) {
        serialize_error_set("graph description allocation failed");
        return -1;
    }
    av_value *source_asset = av_obj_get(obj, "source_asset");
    if (source_asset) {
        if (!av_is_str(source_asset) ||
            read_id(av_obj_get(obj, "source_graph_id"), &g->source_graph_id, "graph.source_graph_id")) {
            serialize_error_set("linked graph source requires an asset name and graph ID");
            return -1;
        }
        g->source_asset = graph_own_str(g, av_get_str(source_asset), av_get_len(source_asset));
    }
    if (expression) {
        if (!av_is_str(expression) || kind != GRAPH_FUNCTION) {
            serialize_error_set("math_expression must be source text on a function graph");
            return -1;
        }
        g->math_expression = graph_own_str(g, av_get_str(expression), av_get_len(expression));
    }
    av_value *expression_mode = av_obj_get(obj, "expression_mode");
    if (expression_mode) {
        if (!av_is_int(expression_mode) ||
            av_get_sint(expression_mode) < GRAPH_EXPRESSION_NONE ||
            av_get_sint(expression_mode) > GRAPH_EXPRESSION_BRANCH) {
            serialize_error_set("expression_mode must be a valid expression mode");
            return -1;
        }
        g->expression_mode = (graph_expression_mode)av_get_sint(expression_mode);
    } else if (g->math_expression) {
        /* Documents written before mode metadata are pure value expressions. */
        g->expression_mode = GRAPH_EXPRESSION_VALUE;
    }
    av_value *breakpoints = av_obj_get(obj, "breakpoints_enabled");
    if (breakpoints && av_is_bool(breakpoints)) {
        g->breakpoints_enabled = av_get_bool(breakpoints);
    }
    av_value *dataflow = av_obj_get(obj, "dataflow");
    if (dataflow && av_is_bool(dataflow)) {
        g->dataflow = av_get_bool(dataflow);
    }
    av_value *entry_value = av_obj_get(obj, "entry_id");
    av_value *return_value = av_obj_get(obj, "return_id");
    av_value *depth_value = av_obj_get(obj, "call_depth_limit");
    if (entry_value && av_is_uint(entry_value)) {
        g->entry_id = av_get_uint(entry_value);
    }
    if (return_value && av_is_uint(return_value)) {
        g->return_id = av_get_uint(return_value);
    }
    if (depth_value && av_is_int(depth_value)) {
        g->call_depth_limit = (int)av_get_sint(depth_value);
    }
    if (load_comment_boxes(g, av_obj_get(obj, "comment_boxes"))) {
        return -1;
    }
    if (serialize_read_locals(g, obj) || load_nodes(g, nodes, requested_counter) ||
        serialize_resolve_locals(g, obj) || load_edges(g, edges) ||
        serialize_validate_locals(g)) {
        return -1;
    }
    if (g->id_counter > asset->graph->id_counter) {
        asset->graph->id_counter = g->id_counter;
    }
    return 0;
}

/* Node and pin constructors allocate temporary IDs during loading. Only
 * identities retained in the loaded asset should advance its shared counter. */
static int loaded_asset_counter(behavior_asset *asset, uint64_t requested)
{
    uint64_t maximum = 0;
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id > maximum) {
            maximum = asset->variables[i].id;
        }
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        if (g->graph_id > maximum) {
            maximum = g->graph_id;
        }
        for (int j = 0; j < acount(g->comment_boxes); j++) {
            if (g->comment_boxes[j].id > maximum) {
                maximum = g->comment_boxes[j].id;
            }
        }
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (n->id > maximum) {
                maximum = n->id;
            }
            for (int output = 0; output < 2; output++) {
                pin_spec *pins = output ? n->output_pins : n->input_pins;
                int count = output ? n->output_count : n->input_count;
                for (int p = 0; p < count; p++) {
                    if (pins[p].id > maximum) {
                        maximum = pins[p].id;
                    }
                    if (pins[p].split_parent > maximum) {
                        maximum = pins[p].split_parent;
                    }
                }
            }
        }
    }
    if (maximum == UINT64_MAX) {
        serialize_error_set("behavior_asset IDs leave no room for a next ID");
        return -1;
    }
    uint64_t next = maximum >= requested ? maximum + 1 : requested;
    for (int i = 0; i < acount(asset->graphs); i++) {
        asset->graphs[i]->id_counter = next;
    }
    return 0;
}

static int resolve_named_references(behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (n->reference_asset || n->reference_id || !n->reference_name) {
                continue;
            }
            bool variable = n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable;
            bool call = serialize_local_call(n) || n->desc == &NODE_CallMacro;
            if (!variable && !call) {
                continue;
            }
            if (variable) {
                const behavior_variable
                    *target = behavior_asset_find_variable_by_name(asset, n->reference_name);
                if (target) {
                    n->reference_id = target->id;
                    continue;
                }
            }
            if (call) {
                const graph *target = behavior_asset_find_graph_by_name(asset, n->reference_name);
                if (target && target->graph_id) {
                    n->reference_id = target->graph_id;
                    continue;
                }
            }
            serialize_error_set(
                "unknown %s reference: %s",
                variable ? "variable" : "function",
                n->reference_name ? n->reference_name : ""
            );
            return -1;
        }
    }
    return 0;
}

static int serialize_repair_math_calls(behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *call = caller->nodes[j];
            if ((call->desc != &NODE_CallPureFunction &&
                 call->desc != &NODE_BehaviorExpression &&
                 call->desc != &NODE_CallFunction) ||
                call->reference_asset) {
                continue;
            }
            graph *generated = behavior_asset_find_graph(asset, call->reference_id);
            if (!generated || !generated->math_expression || generated->source_asset) {
                continue;
            }
            if (behavior_expression_repair(asset, call) != 0) {
                serialize_error_set("could not repair Behavior Expression call");
                return -1;
            }
        }
    }
    return 0;
}

behavior_asset *behavior_asset_load(const char *text)
{
    char error[160];
    av_doc *doc = text ? av_doc_read(text, error, sizeof error) : NULL;
    if (!doc) {
        serialize_error_set("%s", text ? error : "null graph input");
        return NULL;
    }
    behavior_asset *result = behavior_asset_from_value(av_doc_root(doc));
    av_doc_free(doc);
    if (result && (result->domain == BEHAVIOR_DOMAIN_EVENT ||
                   result->domain == BEHAVIOR_DOMAIN_LIBRARY)) {
        behavior_asset_free(result);
        serialize_error_set("Executable behaviors require readable .chibi source");
        return NULL;
    }
    return result;
}

behavior_asset *behavior_asset_from_value(av_value *root)
{
    behavior_asset *asset = NULL;
    av_value *version = av_is_obj(root) ? av_obj_get(root, "version") : NULL;
    if (!av_is_uint(version) || av_get_uint(version) != GRAPH_SCHEMA_VERSION) {
        serialize_error_set("unsupported behavior_asset schema version (expected 3)");
        goto fail;
    }
    av_value *graphs = av_obj_get(root, "graphs");
    if (!av_is_arr(graphs) || av_arr_size(graphs) < 1) {
        serialize_error_set("behavior_asset requires at least one graph");
        goto fail;
    }
    av_value *metadata_name = av_obj_get(root, "name");
    av_value *metadata_description = av_obj_get(root, "description");
    if (!av_is_str(metadata_name) ||
        (metadata_description && !av_is_str(metadata_description))) {
        serialize_error_set("behavior_asset requires a name");
        goto fail;
    }
    uint64_t requested_counter = 1;
    av_value *counter = av_obj_get(root, "id_counter");
    if (!counter || !av_is_uint(counter) || av_get_uint(counter) == 0) {
        serialize_error_set("missing or invalid id_counter");
        goto fail;
    }
    requested_counter = av_get_uint(counter);
    asset = behavior_asset_new(av_get_str(metadata_name));
    if (!asset) {
        serialize_error_set("out of memory");
        goto fail;
    }
    asset->graph->id_counter = requested_counter;
    const char *domain = av_get_str(av_obj_get(root, "domain"));
    if (domain) {
        int found = 0;
        for (int i = BEHAVIOR_DOMAIN_EVENT; i <= BEHAVIOR_DOMAIN_MODEL; i++) {
            if (!strcmp(domain, behavior_domain_name((behavior_domain)i))) {
                asset->domain = (behavior_domain)i;
                found = 1;
                break;
            }
        }
        if (!found) {
            serialize_error_set("unknown graph domain: %s", domain);
            goto fail;
        }
    }
    asset->bounds[0] = (float)av_get_num(av_obj_get(root, "bounds_x"));
    asset->bounds[1] = (float)av_get_num(av_obj_get(root, "bounds_y"));
    av_value *reference = av_obj_get(root, "reference");
    if (reference) {
        av_value *image = av_obj_get(reference, "image");
        const char *keys[] = { "x", "y", "span", "angle" };
        bool valid = av_is_obj(reference) && av_is_str(image) &&
                     av_is_bool(av_obj_get(reference, "locked"));
        for (int i = 0; i < 4; i++) {
            valid = valid && av_is_num(av_obj_get(reference, keys[i]));
        }
        model_reference value = {
            .image = (char *)av_get_str(image),
            .center = { (float)av_get_num(av_obj_get(reference, "x")),
                        (float)av_get_num(av_obj_get(reference, "y")) },
            .span = (float)av_get_num(av_obj_get(reference, "span")),
            .angle = (float)av_get_num(av_obj_get(reference, "angle")),
            .locked = av_get_bool(av_obj_get(reference, "locked"))
        };
        if (!valid || !behavior_asset_set_model_reference(asset, &value)) {
            serialize_error_set("invalid model reference image alignment");
            goto fail;
        }
    }
    behavior_asset_set_description(asset, av_get_str(metadata_description));
    av_value *owner = av_obj_get(root, "owner_type");
    if (av_is_str(owner) && behavior_asset_set_owner_type(asset, av_get_str(owner))) {
        serialize_error_set("cannot set owner type");
        goto fail;
    }
    av_value *parameters = av_obj_get(root, "component_parameters");
    if (parameters && !av_is_arr(parameters)) {
        serialize_error_set("component_parameters must be an array");
        goto fail;
    }
    for (size_t i = 0; i < av_arr_size(parameters); i++) {
        const av_value *parameter = av_arr_get(parameters, i);
        const char *name = av_get_str(av_obj_get(parameter, "name"));
        const char *key = av_get_str(av_obj_get(parameter, "key"));
        if (!name || !*name || !key || !*key) {
            serialize_error_set("invalid component parameter");
            goto fail;
        }
        behavior_asset_add_component_parameter(
            asset,
            name,
            key,
            av_get_bool(av_obj_get(parameter, "optional"))
        );
    }
    av_value *vars = av_obj_get(root, "variables");
    av_value *reactives = av_obj_get(root, "reactives");
    if (reactives && !av_is_arr(reactives)) {
        serialize_error_set("reactives must be an array");
        goto fail;
    }
    if (reactives) {
        av_arr_iter iter;
        av_arr_iter_init(reactives, &iter);
        av_value *value;
        while ((value = av_arr_iter_next(&iter))) {
            const char *fields[] = { "kind",          "graph_id",
                                     "event_id",      "source_node_id",
                                     "source_pin_id", "statement_node_id",
                                     "value_node_id", "value_pin_id" };
            for (int i = 0; i < 8; i++) {
                if (!av_is_uint(av_obj_get(value, fields[i]))) {
                    serialize_error_set("invalid executable reactive record");
                    goto fail;
                }
            }
            behavior_reactive r = {
                .kind = av_get_uint(av_obj_get(value, "kind")),
                .graph_id = av_get_uint(av_obj_get(value, "graph_id")),
                .event_id = av_get_uint(av_obj_get(value, "event_id")),
                .source_node_id = av_get_uint(av_obj_get(value, "source_node_id")),
                .source_pin_id = av_get_uint(av_obj_get(value, "source_pin_id")),
                .statement_node_id = av_get_uint(av_obj_get(value, "statement_node_id")),
                .value_node_id = av_get_uint(av_obj_get(value, "value_node_id")),
                .value_pin_id = av_get_uint(av_obj_get(value, "value_pin_id")),
            };
            if (r.kind > BEHAVIOR_REACTIVE_PRESSED_CHANGED || !r.event_id) {
                serialize_error_set("invalid executable reactive kind or event");
                goto fail;
            }
            apush(asset->reactives, r);
        }
    }
    if (vars && !av_is_arr(vars)) {
        serialize_error_set("variables must be an array");
        goto fail;
    }
    if (vars) {
        av_arr_iter vit;
        av_arr_iter_init(vars, &vit);
        av_value *vv;
        while ((vv = av_arr_iter_next(&vit))) {
            if (load_variable(asset, vv)) {
                goto fail;
            }
        }
    }
    av_arr_iter git;
    av_arr_iter_init(graphs, &git);
    av_value *graph_value;
    int graph_index = 0;
    while ((graph_value = av_arr_iter_next(&git))) {
        if (load_behavior_asset_graph(asset, graph_value, requested_counter, graph_index == 0)) {
            goto fail;
        }
        graph_index++;
    }
    if (graph_index == 0) {
        serialize_error_set("behavior_asset requires a default event graph");
        goto fail;
    }
    if (resolve_named_references(asset)) {
        goto fail;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        if (g->math_expression && !acount(g->nodes) &&
            behavior_expression_restore(asset, g)) {
            serialize_error_set("could not rebuild expression %s", g->name);
            goto fail;
        }
    }
    serialize_derive_declarations(asset);
    if (serialize_repair_math_calls(asset)) {
        goto fail;
    }
    if (loaded_asset_counter(asset, requested_counter)) {
        goto fail;
    }
    return asset;
fail:
    if (asset) {
        behavior_asset_free(asset);
    }
    return NULL;
}
