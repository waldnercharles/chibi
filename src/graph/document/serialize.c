#include "../third_party/yyjson/yyjson.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/runtime/value_array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

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

static int json_uint(yyjson_val *v, uint64_t *out, const char *path)
{
    if (!v || !yyjson_is_uint(v) || yyjson_get_uint(v) == 0) {
        serialize_error_set("%s must be a non-zero unsigned integer", path);
        return -1;
    }
    *out = yyjson_get_uint(v);
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
static int read_pin_str(graph *g, yyjson_val *pin, const char *key, const char **out)
{
    yyjson_val *value = yyjson_obj_get(pin, key);
    if (!value) {
        return 0;
    }
    if (yyjson_is_null(value)) {
        *out = NULL;
        return 0;
    }
    if (!yyjson_is_str(value)) {
        return -1;
    }
    *out = graph_own_str(g, yyjson_get_str(value), yyjson_get_len(value));
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

static int read_pin_default(graph *g, node *n, int output, int i, yyjson_val *def)
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

static int apply_pin(graph *g, node *n, int output, int i, yyjson_val *v, load_refs *refs)
{
    pin_spec *p = output ? &n->output_pins[i] : &n->input_pins[i];
    yyjson_val *name = yyjson_obj_get(v, "name");
    yyjson_val *type = yyjson_obj_get(v, "type");
    yyjson_val *kind = yyjson_obj_get(v, "kind");
    pin_type type_value = p->type;
    bool retyped = type && serialize_read_pin_type(type, &type_value) &&
                   type_value != p->type;
    if (retyped) {
        p->type_name = serialize_implied_type_name(type_value, p->type_name);
    }
    if ((name && !yyjson_is_str(name)) || (kind && !yyjson_is_str(kind)) ||
        (type && !serialize_read_pin_type(type, &type_value)) ||
        read_pin_str(g, v, "type_name", &p->type_name) ||
        read_pin_str(g, v, "split_type_name", &p->split_type_name)) {
        serialize_error_set(
            "node %llu has malformed pin %d",
            (unsigned long long)n->id,
            i
        );
        return -1;
    }
    if (name) {
        p->name = graph_own_str(g, yyjson_get_str(name), yyjson_get_len(name));
    }
    p->type = type_value;
    if (kind) {
        p->kind = strcmp(yyjson_get_str(kind), "exec") == 0 ? PIN_EXEC : PIN_DATA;
    }
    yyjson_val *container = yyjson_obj_get(v, "container");
    if (container && yyjson_is_int(container) &&
        (pin_container)yyjson_get_sint(container) != p->container) {
        p->container = (pin_container)yyjson_get_sint(container);
        retyped = true;
    }
    yyjson_val *split = yyjson_obj_get(v, "split_parent");
    if (split && yyjson_is_uint(split)) {
        p->split_parent = yyjson_get_uint(split);
    }
    yyjson_val *field_source = yyjson_obj_get(v, "field_source");
    yyjson_val *field_name = yyjson_obj_get(v, "field_name");
    if ((field_source || field_name) &&
        (!output || !yyjson_is_uint(field_source) || !yyjson_get_uint(field_source) ||
         !yyjson_is_str(field_name) || !yyjson_get_len(field_name))) {
        serialize_error_set("invalid exposed field metadata");
        return -1;
    }
    if (field_source) {
        p->field_source = yyjson_get_uint(field_source);
        p->field_name = graph_own_str(g, yyjson_get_str(field_name), yyjson_get_len(field_name));
    }
    yyjson_val *flags = yyjson_obj_get(v, "flags");
    yyjson_val *declaration = yyjson_obj_get(v, "declaration_id");
    if (declaration) {
        p->declaration_id = yyjson_get_uint(declaration);
    } else {
        mark_derived_declaration(g, n, p);
    }
    if (flags && yyjson_is_uint(flags)) {
        p->flags = (unsigned)yyjson_get_uint(flags);
    }
    yyjson_val *group = yyjson_obj_get(v, "group");
    if (group && yyjson_is_int(group)) {
        p->group = (int)yyjson_get_sint(group);
    }
    yyjson_val *def = yyjson_obj_get(v, "default");
    if (retyped && !def) {
        p->default_value = serialize_zero_value(p->type, p->container, p->type_name);
    }
    if (def && read_pin_default(g, n, output, i, def)) {
        return -1;
    }
    yyjson_val *binding_json = yyjson_obj_get(v, "binding");
    pin_ref_binding pending = { 0 };
    if (binding_json) {
        yyjson_val *binding_kind = yyjson_obj_get(binding_json, "kind");
        if (output || !yyjson_is_obj(binding_json) || !yyjson_is_str(binding_kind)) {
            serialize_error_set("invalid pin binding on %s pin %d", output ? "output" : "input", i);
            return -1;
        }
        const char *kind_name = yyjson_get_str(binding_kind);
        pin_binding binding = { 0 };
        if (!strcmp(kind_name, "variable")) {
            binding.kind = PIN_BINDING_VARIABLE;
            yyjson_val *id = yyjson_obj_get(binding_json, "variable_id");
            if (!id || !yyjson_is_uint(id) || !yyjson_get_uint(id)) {
                serialize_error_set("variable pin binding %d has no variable_id", i);
                return -1;
            }
            binding.variable_id = yyjson_get_uint(id);
        } else if (!strcmp(kind_name, "component_field")) {
            binding.kind = PIN_BINDING_COMPONENT_FIELD;
            yyjson_val *component = yyjson_obj_get(binding_json, "component");
            yyjson_val *field = yyjson_obj_get(binding_json, "field");
            if (!yyjson_is_str(component) || !yyjson_get_len(component) ||
                !yyjson_is_str(field) || !yyjson_get_len(field)) {
                serialize_error_set("component pin binding %d is incomplete", i);
                return -1;
            }
            binding.component_key = yyjson_get_str(component);
            binding.field_name = yyjson_get_str(field);
        } else if (!strcmp(kind_name, "component")) {
            binding.kind = PIN_BINDING_COMPONENT;
            yyjson_val *component = yyjson_obj_get(binding_json, "component");
            if (!yyjson_is_str(component) || !yyjson_get_len(component)) {
                serialize_error_set("component pin binding %d has no component key", i);
                return -1;
            }
            binding.component_key = yyjson_get_str(component);
        } else if (!strcmp(kind_name, "local")) {
            binding.kind = PIN_BINDING_LOCAL;
            yyjson_val *local_id = yyjson_obj_get(binding_json, "local_id");
            if (!yyjson_is_uint(local_id) || !yyjson_get_uint(local_id)) {
                serialize_error_set("local binding has no local_id");
                return -1;
            }
            binding.local_id = yyjson_get_uint(local_id);
        } else if (!strcmp(kind_name, "event_output")) {
            binding.kind = PIN_BINDING_EVENT_OUTPUT;
            yyjson_val *from = yyjson_obj_get(binding_json, "from");
            yyjson_val *event_node_id = yyjson_is_uint(from)
                                          ? from
                                          : yyjson_obj_get(binding_json, "event_node_id");
            yyjson_val *event_pin_id = yyjson_obj_get(binding_json, "event_pin_id");
            if (yyjson_is_str(from)) {
                /* The event node may follow this one; resolve after loading nodes. */
                pending = (pin_ref_binding){ n, i, yyjson_get_str(from) };
            } else if (
                !yyjson_is_uint(event_node_id) || !yyjson_get_uint(event_node_id) ||
                !yyjson_is_uint(event_pin_id) || !yyjson_get_uint(event_pin_id)
            ) {
                serialize_error_set("event output binding is incomplete");
                return -1;
            } else {
                binding.event_node_id = yyjson_get_uint(event_node_id);
                binding.event_pin_id = yyjson_get_uint(event_pin_id);
            }
        } else if (!strcmp(kind_name, "delta_time")) {
            binding.kind = PIN_BINDING_DELTA_TIME;
        } else {
            serialize_error_set("unknown pin binding kind '%s'", kind_name);
            return -1;
        }
        yyjson_val *source_asset = yyjson_obj_get(binding_json, "source_asset");
        if (source_asset && !yyjson_is_str(source_asset)) {
            serialize_error_set("invalid pin binding source asset");
            return -1;
        }
        binding.source_asset = source_asset ? yyjson_get_str(source_asset) : NULL;
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
static int load_pin_list(graph *g, node *n, yyjson_val *arr, int output, uint64_t **ids, load_refs *refs)
{
    yyjson_arr_iter it;
    yyjson_arr_iter_init(arr, &it);
    yyjson_val *v;
    int i = 0;
    int count = output ? n->output_count : n->input_count;
    int serialized_count = (int)yyjson_arr_size(arr);
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
    while ((v = yyjson_arr_iter_next(&it))) {
        if (!yyjson_is_obj(v) && !yyjson_is_uint(v)) {
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
        if (json_uint(yyjson_is_uint(v) ? v : yyjson_obj_get(v, "id"), &id, "pin.id")) {
            return -1;
        }
        set_explicit_id(ids, i, id);
        if (!yyjson_is_obj(v)) {
            mark_derived_declaration(g, n, output ? &n->output_pins[i] : &n->input_pins[i]);
        }
        if (yyjson_is_obj(v) && apply_pin(g, n, output, i, v, refs)) {
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
static int load_pin_map(graph *g, node *n, yyjson_val *map, int output, uint64_t **ids, load_refs *refs)
{
    /* A component node's template is its prepared pins. */
    bool component = component_node_kind_of(n->desc);
    int template_count = component ? component_node_template_count(n, output)
                       : output    ? n->desc->output_count
                                   : n->desc->input_count;
    size_t at, max;
    yyjson_val *key, *v;
    yyjson_obj_foreach(map, at, max, key, v)
    {
        const pin_spec *template_pins = component
                                          ? (output ? n->output_pins : n->input_pins)
                                      : output ? n->desc->outputs
                                               : n->desc->inputs;
        const char *name = yyjson_get_str(key);
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
        yyjson_val *id = yyjson_is_obj(v) ? yyjson_obj_get(v, "id") : NULL;
        uint64_t explicit_id = 0;
        if (id && json_uint(id, &explicit_id, "pin.id")) {
            return -1;
        }
        set_explicit_id(ids, i, explicit_id);
        int failed = yyjson_is_obj(v) ? apply_pin(g, n, output, i, v, refs)
                                      : read_pin_default(g, n, output, i, v);
        if (failed) {
            return -1;
        }
        if (!yyjson_is_obj(v)) {
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

static int load_node_pins(graph *g, node *n, yyjson_val *v, load_refs *refs)
{
    uint64_t *ids[2] = { NULL, NULL };
    int failed = 0;
    for (int output = 0; output < 2 && !failed; output++) {
        yyjson_val *pins = yyjson_obj_get(v, output ? "outputs" : "inputs");
        if (yyjson_is_arr(pins)) {
            failed = load_pin_list(g, n, pins, output, &ids[output], refs);
        } else if (yyjson_is_obj(pins)) {
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

static int load_comment_boxes(graph *g, yyjson_val *json)
{
    if (!json) {
        return 0;
    }
    if (!yyjson_is_arr(json)) {
        serialize_error_set("comment_boxes must be an array");
        return -1;
    }
    yyjson_arr_iter it;
    yyjson_arr_iter_init(json, &it);
    yyjson_val *value;
    while ((value = yyjson_arr_iter_next(&it))) {
        uint64_t id = 0;
        yyjson_val *title = yyjson_obj_get(value, "title");
        if (!yyjson_is_obj(value) ||
            json_uint(yyjson_obj_get(value, "id"), &id, "comment_box.id") ||
            !yyjson_is_str(title)) {
            serialize_error_set("malformed comment box");
            return -1;
        }
        graph_comment_box box = {
            .id = id,
            .x = (float)yyjson_get_num(yyjson_obj_get(value, "x")),
            .y = (float)yyjson_get_num(yyjson_obj_get(value, "y")),
            .width = (float)yyjson_get_num(yyjson_obj_get(value, "width")),
            .height = (float)yyjson_get_num(yyjson_obj_get(value, "height")),
            .title = graph_own_str(g, yyjson_get_str(title), yyjson_get_len(title)),
            .color = (uint32_t)yyjson_get_uint(yyjson_obj_get(value, "color")),
        };
        apush(g->comment_boxes, box);
        if (id >= g->id_counter) {
            g->id_counter = id + 1;
        }
    }
    return 0;
}

/* Documents omit `auto`. Older documents mark explicit inline sources with
 * `inline_source: true`. */
static int load_node_presentation(node *n, yyjson_val *v)
{
    yyjson_val *presentation = yyjson_obj_get(v, "presentation");
    yyjson_val *inline_source = yyjson_obj_get(v, "inline_source");
    if (inline_source && !yyjson_is_bool(inline_source)) {
        serialize_error_set("node inline_source must be a boolean");
        return -1;
    }
    n->presentation = yyjson_is_true(inline_source) ? NODE_PRESENTATION_INLINE
                                                    : NODE_PRESENTATION_AUTO;
    if (!presentation) {
        return 0;
    }
    int mode = graph_presentation_by_name(yyjson_get_str(presentation));
    if (mode < 0) {
        serialize_error_set("node presentation must be auto, inline or expanded");
        return -1;
    }
    n->presentation = (node_presentation)mode;
    return 0;
}

/* A component node's Value pins follow its Component setting, and a Set that
 * names field inputs writes those fields instead of Value. Documents may name
 * the component; the setting stores its key. */
static int load_component_pins(graph *g, node *n, yyjson_val *v, load_refs *refs)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    if (!kind) {
        return load_node_pins(g, n, v, refs);
    }
    yyjson_val *inputs = yyjson_obj_get(v, "inputs");
    if (inputs && !yyjson_is_obj(inputs)) {
        serialize_error_set(
            "component node %llu must key its inputs",
            (unsigned long long)n->id
        );
        return -1;
    }
    yyjson_val *setting = yyjson_obj_get(inputs, "Component");
    yyjson_val *key = yyjson_is_obj(setting) ? yyjson_obj_get(setting, "default")
                                             : setting;
    const char *component = yyjson_get_str(key);
    bool split = false;
    size_t at, max;
    yyjson_val *name, *entry;
    if (inputs) {
        yyjson_obj_foreach(inputs, at, max, name, entry)
        {
            split |= kind == COMPONENT_NODE_SET &&
                     component_field_settable(component_by_key(component), yyjson_get_str(name));
        }
    }
    if (component_node_prepare(g, n, component, split)) {
        serialize_error_set("unknown component \"%s\"", component);
        return -1;
    }
    if (load_node_pins(g, n, v, refs)) {
        return -1;
    }
    const ecs_component_desc *found = component_by_key(component);
    n->input_pins[component_node_setting(kind)].default_value.str = found
                                                                      ? found->key
                                                                      : "";
    return 0;
}

static int load_node_list(graph *g, yyjson_val *nodes, uint64_t requested_counter, load_refs *refs)
{
    yyjson_arr_iter it;
    yyjson_arr_iter_init(nodes, &it);
    yyjson_val *v;
    while ((v = yyjson_arr_iter_next(&it))) {
        if (!yyjson_is_obj(v)) {
            serialize_error_set("node is not an object");
            return -1;
        }
        yyjson_val *type = yyjson_obj_get(v, "type");
        if (!yyjson_is_str(type)) {
            serialize_error_set("node is missing type");
            return -1;
        }
        const node_desc *desc = registry_node_by_name(yyjson_get_str(type));
        if (!desc) {
            serialize_error_set("unknown node type \"%s\"", yyjson_get_str(type));
            return -1;
        }
        node *n = graph_add_node(g, desc);
        if (!n) {
            serialize_error_set("out of memory");
            return -1;
        }
        uint64_t id = 0;
        if (json_uint(yyjson_obj_get(v, "id"), &id, "node.id")) {
            return -1;
        }
        n->id = id;
        yyjson_val *x = yyjson_obj_get(v, "x"), *y = yyjson_obj_get(v, "y");
        yyjson_val *placed = yyjson_obj_get(v, "placed");
        if ((x && !yyjson_is_num(x)) || (y && !yyjson_is_num(y)) || !x != !y ||
            (placed && !yyjson_is_bool(placed))) {
            serialize_error_set("node %llu has an invalid position", (unsigned long long)id);
            return -1;
        }
        /* Nodes without a position wait for the editor to arrange them. */
        n->x = x ? (float)yyjson_get_num(x) : 0.0f;
        n->y = y ? (float)yyjson_get_num(y) : 0.0f;
        n->placed = x && (!placed || yyjson_get_bool(placed));
        yyjson_val *refasset = yyjson_obj_get(v, "reference_asset");
        if (yyjson_is_str(refasset)) {
            n->reference_asset = graph_own_str(g, yyjson_get_str(refasset), yyjson_get_len(refasset));
        }
        yyjson_val *refname = yyjson_obj_get(v, "reference_name");
        yyjson_val *refid = yyjson_obj_get(v, "reference_id");
        if (refid && yyjson_is_uint(refid)) {
            n->reference_id = yyjson_get_uint(refid);
        }
        if (refname && yyjson_is_str(refname)) {
            n->reference_name = graph_own_str(g, yyjson_get_str(refname), yyjson_get_len(refname));
        }
        yyjson_val *disabled = yyjson_obj_get(v, "disabled");
        n->disabled = disabled && yyjson_is_bool(disabled) &&
                      yyjson_get_bool(disabled);
        if (load_node_presentation(n, v)) {
            return -1;
        }
        yyjson_val *breakpoint = yyjson_obj_get(v, "breakpoint");
        yyjson_val *breakpoint_enabled = yyjson_obj_get(v, "breakpoint_enabled");
        n->show_advanced = yyjson_get_bool(yyjson_obj_get(v, "show_advanced"));
        n->breakpoint = breakpoint && yyjson_is_bool(breakpoint) &&
                        yyjson_get_bool(breakpoint);
        n->breakpoint_enabled = breakpoint_enabled &&
                                yyjson_is_bool(breakpoint_enabled) &&
                                yyjson_get_bool(breakpoint_enabled);
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

static int load_nodes(graph *g, yyjson_val *nodes, uint64_t requested_counter)
{
    load_refs refs = { 0 };
    int failed = load_node_list(g, nodes, requested_counter, &refs);
    afree(refs.bindings);
    return failed;
}

/* An endpoint is "<node>.<pin key>", or a node ID with a pin ID field. */
static int read_endpoint(
    graph *g,
    yyjson_val *v,
    const char *key,
    const char *pin_key,
    int output,
    node **n,
    uint64_t *node_id,
    uint64_t *pin_id
)
{
    yyjson_val *ref = yyjson_obj_get(v, key);
    int index = -1;
    if (yyjson_is_str(ref)) {
        if (!serialize_resolve_ref(g, yyjson_get_str(ref), output, n, &index)) {
            serialize_error_set("edge names no pin: %s", yyjson_get_str(ref));
            return -1;
        }
        *node_id = (*n)->id;
        *pin_id = output ? (*n)->output_pins[index].id : (*n)->input_pins[index].id;
        return 0;
    }
    if (json_uint(ref, node_id, key) ||
        json_uint(yyjson_obj_get(v, pin_key), pin_id, pin_key)) {
        return -1;
    }
    *n = graph_node_by_id(g, *node_id);
    return 0;
}

static int load_edges(graph *g, yyjson_val *edges)
{
    yyjson_arr_iter eit;
    yyjson_arr_iter_init(edges, &eit);
    yyjson_val *v;
    while ((v = yyjson_arr_iter_next(&eit))) {
        if (!yyjson_is_obj(v)) {
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
        yyjson_val *kind = yyjson_obj_get(v, "kind");
        if (!from || !to || !fps || !tps || fo != 1 || to_out != 0 ||
            (kind && !yyjson_is_str(kind))) {
            serialize_error_set("edge references missing node or pin ID");
            return -1;
        }
        const char *kind_name = kind                  ? yyjson_get_str(kind)
                              : fps->kind == PIN_EXEC ? "exec"
                                                      : "data";
        yyjson_val *conversion_value = yyjson_obj_get(v, "conversion");
        const node_desc *conversion = NULL;
        if (conversion_value) {
            if (!yyjson_is_str(conversion_value)) {
                serialize_error_set("edge conversion is not a node name");
                return -1;
            }
            conversion = registry_node_by_name(yyjson_get_str(conversion_value));
            if (!conversion) {
                serialize_error_set("unknown edge conversion \"%s\"", yyjson_get_str(conversion_value));
                return -1;
            }
        }
        int r;
        if (strcmp(kind_name, "data") == 0) {
            if (yyjson_obj_get(v, "order")) {
                serialize_error_set("data edge has an exec order");
                return -1;
            }
            r = graph_connect_data_conversion(g, from, fp, to, tp, conversion);
        } else if (strcmp(kind_name, "exec") == 0) {
            if (conversion) {
                serialize_error_set("exec edge cannot have a conversion");
                return -1;
            }
            yyjson_val *order = yyjson_obj_get(v, "order");
            if (order && (!yyjson_is_int(order) || yyjson_get_sint(order) < 0 ||
                          yyjson_get_sint(order) > INT_MAX)) {
                serialize_error_set("exec edge has an invalid order");
                return -1;
            }
            r = graph_connect_exec(g, from, fp, to, tp, order ? (int)yyjson_get_sint(order) : 0);
        } else {
            serialize_error_set("unsupported edge kind");
            return -1;
        }
        if (r) {
            serialize_error_set("invalid edge endpoints or incompatible types");
            return -1;
        }
        yyjson_val *handle = yyjson_obj_get(v, "from_handle_x");
        yyjson_val *handle_y = yyjson_obj_get(v, "from_handle_y");
        yyjson_val *to_handle = yyjson_obj_get(v, "to_handle_x");
        yyjson_val *to_handle_y = yyjson_obj_get(v, "to_handle_y");
        if (handle || handle_y || to_handle || to_handle_y) {
            if (!handle || !handle_y || !to_handle || !to_handle_y ||
                !yyjson_is_num(handle) || !yyjson_is_num(handle_y) ||
                !yyjson_is_num(to_handle) || !yyjson_is_num(to_handle_y)) {
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
            loaded->from_handle_x = (float)yyjson_get_num(handle);
            loaded->from_handle_y = (float)yyjson_get_num(handle_y);
            loaded->to_handle_x = (float)yyjson_get_num(to_handle);
            loaded->to_handle_y = (float)yyjson_get_num(to_handle_y);
            loaded->custom_handles = true;
        }
        yyjson_val *points = yyjson_obj_get(v, "reroutes");
        if (points) {
            if (!yyjson_is_arr(points) ||
                yyjson_arr_size(points) > GRAPH_EDGE_MAX_REROUTES) {
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
            yyjson_arr_iter pit;
            yyjson_arr_iter_init(points, &pit);
            yyjson_val *point;
            while ((point = yyjson_arr_iter_next(&pit))) {
                yyjson_val *x = yyjson_obj_get(point, "x"),
                           *y = yyjson_obj_get(point, "y");
                if (!yyjson_is_obj(point) || !yyjson_is_num(x) || !yyjson_is_num(y)) {
                    serialize_error_set("edge has invalid reroute point");
                    return -1;
                }
                loaded->reroutes[loaded->reroute_count].x = (float)yyjson_get_num(x);
                loaded->reroutes[loaded->reroute_count].y = (float)yyjson_get_num(y);
                loaded->reroute_count++;
            }
        }
    }
    return serialize_migrate_component_reads(g);
}

graph *graph_load_json(const char *json)
{
    if (!json) {
        serialize_error_set("null JSON input");
        return NULL;
    }
    char error[160];
    yyjson_doc *doc = asset_text_read(json, error, sizeof error);
    if (!doc) {
        serialize_error_set("%s", error);
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *version = yyjson_is_obj(root) ? yyjson_obj_get(root, "version") : NULL;
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != GRAPH_SCHEMA_VERSION) {
        serialize_error_set("unsupported graph schema version (expected 3)");
        yyjson_doc_free(doc);
        return NULL;
    }
    yyjson_val *nodes = yyjson_obj_get(root, "nodes"),
               *edges = yyjson_obj_get(root, "edges");
    if ((nodes && !yyjson_is_arr(nodes)) || (edges && !yyjson_is_arr(edges))) {
        serialize_error_set("invalid nodes or edges array");
        yyjson_doc_free(doc);
        return NULL;
    }
    graph *g = (graph *)calloc(1, sizeof *g);
    if (!g) {
        serialize_error_set("out of memory");
        yyjson_doc_free(doc);
        return NULL;
    }
    graph_init(g);
    uint64_t requested_counter = 1;
    yyjson_val *counter = yyjson_obj_get(root, "id_counter");
    if (!counter || !yyjson_is_uint(counter) || yyjson_get_uint(counter) == 0) {
        serialize_error_set("missing or invalid id_counter");
        goto fail;
    }
    requested_counter = yyjson_get_uint(counter);
    g->id_counter = requested_counter;
    yyjson_val *metadata_name = yyjson_obj_get(root, "name");
    yyjson_val *metadata_description = yyjson_obj_get(root, "description");
    if (yyjson_is_str(metadata_name)) {
        g->name = graph_own_str(g, yyjson_get_str(metadata_name), yyjson_get_len(metadata_name));
    }
    const char *description = yyjson_is_str(metadata_description)
                                ? yyjson_get_str(metadata_description)
                                : "";
    g->description = graph_own_str(g, description, strlen(description));
    if (!g->name || (metadata_description && !yyjson_is_str(metadata_description))) {
        serialize_error_set("missing behavior_asset metadata");
        goto fail;
    }
    yyjson_val *binding_owner = yyjson_obj_get(root, "binding_owner_id");
    if (binding_owner) {
        if (!yyjson_is_uint(binding_owner) || !yyjson_get_uint(binding_owner)) {
            serialize_error_set("invalid binding clipboard owner");
            goto fail;
        }
        g->clipboard_owner_id = yyjson_get_uint(binding_owner);
    }
    yyjson_val *breakpoints = yyjson_obj_get(root, "breakpoints_enabled");
    if (breakpoints && yyjson_is_bool(breakpoints)) {
        g->breakpoints_enabled = yyjson_get_bool(breakpoints);
    }
    yyjson_val *dataflow = yyjson_obj_get(root, "dataflow");
    if (dataflow && yyjson_is_bool(dataflow)) {
        g->dataflow = yyjson_get_bool(dataflow);
    }
    if (load_comment_boxes(g, yyjson_obj_get(root, "comment_boxes"))) {
        goto fail;
    }
    if (serialize_read_locals(g, root) || load_nodes(g, nodes, requested_counter) ||
        serialize_resolve_locals(g, root) || load_edges(g, edges) ||
        serialize_validate_locals(g)) {
        goto fail;
    }
    yyjson_doc_free(doc);
    return g;
fail:
    yyjson_doc_free(doc);
    graph_free(g);
    free(g);
    return NULL;
}

graph *serialize_load_graph_body(yyjson_val *obj, uint64_t id_counter)
{
    graph *g = xcalloc(1, sizeof *g);
    graph_init(g);
    g->id_counter = id_counter;
    yyjson_val *nodes = yyjson_obj_get(obj, "nodes"),
               *edges = yyjson_obj_get(obj, "edges");
    if (serialize_read_locals(g, obj) || load_nodes(g, nodes, id_counter) ||
        serialize_resolve_locals(g, obj) || load_edges(g, edges) ||
        serialize_validate_locals(g)) {
        graph_free(g);
        free(g);
        return NULL;
    }
    return g;
}

static int load_variable(behavior_asset *asset, yyjson_val *v)
{
    if (!yyjson_is_obj(v)) {
        serialize_error_set("variable is not an object");
        return -1;
    }
    uint64_t id = 0;
    if (json_uint(yyjson_obj_get(v, "id"), &id, "variable.id")) {
        return -1;
    }
    yyjson_val *name = yyjson_obj_get(v, "name");
    pin_type pt;
    if (!yyjson_is_str(name) ||
        !serialize_read_pin_type(yyjson_obj_get(v, "type"), &pt)) {
        serialize_error_set("variable has malformed name or type");
        return -1;
    }
    yyjson_val *tn = yyjson_obj_get(v, "type_name");
    const char *type_name = yyjson_is_str(tn) ? yyjson_get_str(tn) : NULL;
    yyjson_val *container = yyjson_obj_get(v, "container");
    pin_container pc = container && yyjson_is_int(container)
                         ? (pin_container)yyjson_get_sint(container)
                         : PIN_CONTAINER_SINGLE;
    yyjson_val *category = yyjson_obj_get(v, "category");
    const char *cat = yyjson_is_str(category) ? yyjson_get_str(category) : NULL;
    yyjson_val *def = yyjson_obj_get(v, "default");
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
        yyjson_get_str(name),
        pt,
        type_name,
        pc,
        def ? &pv : NULL,
        cat
    );
    if (!var) {
        serialize_error_set("cannot create variable %s", yyjson_get_str(name));
        return -1;
    }
    var->id = id;
    return 0;
}

static int load_behavior_asset_graph(behavior_asset *asset, yyjson_val *obj, uint64_t requested_counter, int is_default)
{
    if (!yyjson_is_obj(obj)) {
        serialize_error_set("graph is not an object");
        return -1;
    }
    yyjson_val *kind_json = yyjson_obj_get(obj, "kind");
    yyjson_val *name_json = yyjson_obj_get(obj, "name");
    yyjson_val *description_json = yyjson_obj_get(obj, "description");
    yyjson_val *nodes = yyjson_obj_get(obj, "nodes");
    yyjson_val *edges = yyjson_obj_get(obj, "edges");
    graph_kind kind;
    if (!yyjson_is_str(kind_json) ||
        graph_kind_from_name(yyjson_get_str(kind_json), &kind) ||
        !yyjson_is_str(name_json) ||
        (description_json && !yyjson_is_str(description_json)) ||
        (nodes && !yyjson_is_arr(nodes)) || (edges && !yyjson_is_arr(edges))) {
        serialize_error_set("graph is missing kind or name, or has invalid nodes or edges");
        return -1;
    }
    if (is_default && kind != GRAPH_EVENT) {
        serialize_error_set("the first Behavior Asset graph must be an event graph");
        return -1;
    }
    uint64_t graph_id = 0;
    if (!is_default && json_uint(yyjson_obj_get(obj, "id"), &graph_id, "graph.id")) {
        return -1;
    }
    graph *g = is_default ? asset->graph : NULL;
    if (!g) {
        g = (graph *)xcalloc(1, sizeof *g);
        graph_init(g);
        g->owner = asset;
        g->shared_id_counter = &asset->graph->id_counter;
        g->graph_id = graph_id;
        g->kind = kind;
        g->name = graph_own_str(g, yyjson_get_str(name_json), yyjson_get_len(name_json));
        apush(asset->graphs, g);
    } else {
        g->kind = kind;
        g->graph_id = graph_id;
        g->name = graph_own_str(g, yyjson_get_str(name_json), yyjson_get_len(name_json));
    }
    if (!g->name) {
        serialize_error_set("graph name allocation failed");
        return -1;
    }
    const char *description = description_json ? yyjson_get_str(description_json) : "";
    g->description = graph_own_str(g, description, strlen(description));
    if (!g->description) {
        serialize_error_set("graph description allocation failed");
        return -1;
    }
    yyjson_val *source_asset = yyjson_obj_get(obj, "source_asset");
    if (source_asset) {
        if (!yyjson_is_str(source_asset) ||
            json_uint(yyjson_obj_get(obj, "source_graph_id"), &g->source_graph_id, "graph.source_graph_id")) {
            serialize_error_set("linked graph source requires an asset name and graph ID");
            return -1;
        }
        g->source_asset = graph_own_str(g, yyjson_get_str(source_asset), yyjson_get_len(source_asset));
    }
    yyjson_val *expression = yyjson_obj_get(obj, "math_expression");
    if (expression) {
        if (!yyjson_is_str(expression) || kind != GRAPH_FUNCTION) {
            serialize_error_set("math_expression must be source text on a function graph");
            return -1;
        }
        g->math_expression = graph_own_str(g, yyjson_get_str(expression), yyjson_get_len(expression));
    }
    yyjson_val *expression_mode = yyjson_obj_get(obj, "expression_mode");
    if (expression_mode) {
        if (!yyjson_is_int(expression_mode) ||
            yyjson_get_sint(expression_mode) < GRAPH_EXPRESSION_NONE ||
            yyjson_get_sint(expression_mode) > GRAPH_EXPRESSION_BRANCH) {
            serialize_error_set("expression_mode must be a valid expression mode");
            return -1;
        }
        g->expression_mode = (graph_expression_mode)yyjson_get_sint(expression_mode);
    } else if (g->math_expression) {
        /* Documents written before mode metadata are pure value expressions. */
        g->expression_mode = GRAPH_EXPRESSION_VALUE;
    }
    yyjson_val *breakpoints = yyjson_obj_get(obj, "breakpoints_enabled");
    if (breakpoints && yyjson_is_bool(breakpoints)) {
        g->breakpoints_enabled = yyjson_get_bool(breakpoints);
    }
    yyjson_val *dataflow = yyjson_obj_get(obj, "dataflow");
    if (dataflow && yyjson_is_bool(dataflow)) {
        g->dataflow = yyjson_get_bool(dataflow);
    }
    yyjson_val *entry_json = yyjson_obj_get(obj, "entry_id");
    yyjson_val *return_json = yyjson_obj_get(obj, "return_id");
    yyjson_val *depth_json = yyjson_obj_get(obj, "call_depth_limit");
    if (entry_json && yyjson_is_uint(entry_json)) {
        g->entry_id = yyjson_get_uint(entry_json);
    }
    if (return_json && yyjson_is_uint(return_json)) {
        g->return_id = yyjson_get_uint(return_json);
    }
    if (depth_json && yyjson_is_int(depth_json)) {
        g->call_depth_limit = (int)yyjson_get_sint(depth_json);
    }
    if (load_comment_boxes(g, yyjson_obj_get(obj, "comment_boxes"))) {
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

behavior_asset *behavior_asset_load_json(const char *json)
{
    behavior_asset *asset = NULL;
    if (!json) {
        serialize_error_set("null JSON input");
        return NULL;
    }
    char error[160];
    yyjson_doc *doc = asset_text_read(json, error, sizeof error);
    if (!doc) {
        serialize_error_set("%s", error);
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *version = yyjson_is_obj(root) ? yyjson_obj_get(root, "version") : NULL;
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != GRAPH_SCHEMA_VERSION) {
        serialize_error_set("unsupported behavior_asset schema version (expected 3)");
        goto fail;
    }
    yyjson_val *graphs = yyjson_obj_get(root, "graphs");
    if (!yyjson_is_arr(graphs) || yyjson_arr_size(graphs) < 1) {
        serialize_error_set("behavior_asset requires at least one graph");
        goto fail;
    }
    yyjson_val *metadata_name = yyjson_obj_get(root, "name");
    yyjson_val *metadata_description = yyjson_obj_get(root, "description");
    if (!yyjson_is_str(metadata_name) ||
        (metadata_description && !yyjson_is_str(metadata_description))) {
        serialize_error_set("behavior_asset requires a name");
        goto fail;
    }
    uint64_t requested_counter = 1;
    yyjson_val *counter = yyjson_obj_get(root, "id_counter");
    if (!counter || !yyjson_is_uint(counter) || yyjson_get_uint(counter) == 0) {
        serialize_error_set("missing or invalid id_counter");
        goto fail;
    }
    requested_counter = yyjson_get_uint(counter);
    asset = behavior_asset_new(yyjson_get_str(metadata_name));
    if (!asset) {
        serialize_error_set("out of memory");
        goto fail;
    }
    asset->graph->id_counter = requested_counter;
    const char *domain = yyjson_get_str(yyjson_obj_get(root, "domain"));
    if (domain) {
        int found = 0;
        for (int i = BEHAVIOR_DOMAIN_EVENT; i <= BEHAVIOR_DOMAIN_FIELD; i++) {
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
    asset->bounds[0] = (float)yyjson_get_num(yyjson_obj_get(root, "bounds_x"));
    asset->bounds[1] = (float)yyjson_get_num(yyjson_obj_get(root, "bounds_y"));
    behavior_asset_set_description(asset, yyjson_get_str(metadata_description));
    yyjson_val *owner = yyjson_obj_get(root, "owner_type");
    if (yyjson_is_str(owner) &&
        behavior_asset_set_owner_type(asset, yyjson_get_str(owner))) {
        serialize_error_set("cannot set owner type");
        goto fail;
    }
    yyjson_val *vars = yyjson_obj_get(root, "variables");
    if (vars && !yyjson_is_arr(vars)) {
        serialize_error_set("variables must be an array");
        goto fail;
    }
    if (vars) {
        yyjson_arr_iter vit;
        yyjson_arr_iter_init(vars, &vit);
        yyjson_val *vv;
        while ((vv = yyjson_arr_iter_next(&vit))) {
            if (load_variable(asset, vv)) {
                goto fail;
            }
        }
    }
    yyjson_arr_iter git;
    yyjson_arr_iter_init(graphs, &git);
    yyjson_val *graph_json;
    int graph_index = 0;
    while ((graph_json = yyjson_arr_iter_next(&git))) {
        if (load_behavior_asset_graph(asset, graph_json, requested_counter, graph_index == 0)) {
            goto fail;
        }
        graph_index++;
    }
    if (graph_index == 0) {
        serialize_error_set("behavior_asset requires a default event graph");
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
    yyjson_doc_free(doc);
    return asset;
fail:
    yyjson_doc_free(doc);
    if (asset) {
        behavior_asset_free(asset);
    }
    return NULL;
}
