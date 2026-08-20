#include "serialize.h"

#include "../third_party/yyjson/yyjson.h"
#include "nodes.h"
#include "registry.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local char serialize_error[256];
static void set_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(serialize_error, sizeof serialize_error, fmt, ap);
    va_end(ap);
}
const char *graph_last_error(void) { return serialize_error; }

#define GRAPH_SCHEMA_VERSION 3

static void write_const(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *key,
    pin_type t,
    const char *type_name,
    const pin_value *v
)
{
    yyjson_mut_val *value = NULL;
    switch (t) {
        case PIN_BOOL: value = yyjson_mut_bool(doc, v->b); break;
        case PIN_I8: value = yyjson_mut_sint(doc, v->i8); break;
        case PIN_U8: value = yyjson_mut_uint(doc, v->u8); break;
        case PIN_I16: value = yyjson_mut_sint(doc, v->i16); break;
        case PIN_U16: value = yyjson_mut_uint(doc, v->u16); break;
        case PIN_I32: value = yyjson_mut_sint(doc, v->i); break;
        case PIN_U32: value = yyjson_mut_uint(doc, v->u); break;
        case PIN_I64: value = yyjson_mut_sint(doc, v->i64); break;
        case PIN_U64: value = yyjson_mut_uint(doc, v->u64); break;
        case PIN_F32: value = yyjson_mut_real(doc, v->f); break;
        case PIN_F64: value = yyjson_mut_real(doc, v->d); break;
        case PIN_ENUM: value = yyjson_mut_sint(doc, v->e); break;
        case PIN_STR:
            value = yyjson_mut_strcpy(doc, v->str ? v->str : "");
            break;
        case PIN_STRUCT:
            if (v->ptr && type_name) {
                const type_desc *type = NULL;
                for (int i = 0; i < REGISTRY_TYPE_COUNT; i++)
                    if (strcmp(REGISTRY_TYPES[i]->name, type_name) == 0) {
                        type = REGISTRY_TYPES[i];
                        break;
                    }
                if (type) {
                    value = yyjson_mut_obj(doc);
                    for (int i = 0; i < type->field_count; i++) {
                        pin_value field;
                        pin_read_field(v->ptr, &type->fields[i], &field);
                        write_const(
                            doc,
                            value,
                            type->fields[i].name,
                            type->fields[i].type,
                            type->fields[i].type_name,
                            &field
                        );
                    }
                }
            }
            if (!value) value = yyjson_mut_null(doc);
            break;
        case PIN_FLOW: value = yyjson_mut_null(doc); break;
    }
    if (value) yyjson_mut_obj_add(obj, yyjson_mut_strcpy(doc, key), value);
}

static const type_desc *find_type(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < REGISTRY_TYPE_COUNT; i++)
        if (strcmp(REGISTRY_TYPES[i]->name, name) == 0)
            return REGISTRY_TYPES[i];
    return NULL;
}

static int read_signed(yyjson_val *v, int64_t *out)
{
    if (yyjson_is_sint(v)) {
        *out = yyjson_get_sint(v);
        return 0;
    }
    if (yyjson_is_uint(v) && yyjson_get_uint(v) <= INT64_MAX) {
        *out = (int64_t)yyjson_get_uint(v);
        return 0;
    }
    return -1;
}

static int read_const(pin_type t, yyjson_val *v, pin_value *out, graph *g)
{
    memset(out, 0, sizeof *out);
    out->type = t;
    switch (t) {
        case PIN_BOOL:
            if (!yyjson_is_bool(v)) return -1;
            out->b = yyjson_get_bool(v);
            break;
        case PIN_I8: {
            int64_t value;
            if (read_signed(v, &value) || value < INT8_MIN || value > INT8_MAX)
                return -1;
            out->i8 = (int8_t)value;
            break;
        }
        case PIN_U8:
            if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT8_MAX) return -1;
            out->u8 = (uint8_t)yyjson_get_uint(v);
            break;
        case PIN_I16: {
            int64_t value;
            if (read_signed(v, &value) || value < INT16_MIN || value > INT16_MAX)
                return -1;
            out->i16 = (int16_t)value;
            break;
        }
        case PIN_U16:
            if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT16_MAX)
                return -1;
            out->u16 = (uint16_t)yyjson_get_uint(v);
            break;
        case PIN_I32: {
            int64_t value;
            if (read_signed(v, &value) || value < INT32_MIN || value > INT32_MAX)
                return -1;
            out->i = (int32_t)value;
            break;
        }
        case PIN_U32:
            if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT32_MAX)
                return -1;
            out->u = (uint32_t)yyjson_get_uint(v);
            break;
        case PIN_I64:
            if (read_signed(v, &out->i64)) return -1;
            break;
        case PIN_U64:
            if (!yyjson_is_uint(v)) return -1;
            out->u64 = yyjson_get_uint(v);
            break;
        case PIN_F32:
            if (!yyjson_is_num(v)) return -1;
            out->f = (float)yyjson_get_real(v);
            break;
        case PIN_F64:
            if (!yyjson_is_num(v)) return -1;
            out->d = yyjson_get_real(v);
            break;
        case PIN_ENUM:
            if (yyjson_is_sint(v)) out->e = (int32_t)yyjson_get_sint(v);
            else if (yyjson_is_uint(v) && yyjson_get_uint(v) <= INT32_MAX)
                out->e = (int32_t)yyjson_get_uint(v);
            else return -1;
            break;
        case PIN_STR:
            if (!yyjson_is_str(v)) return -1;
            out->str = graph_own_str(g, yyjson_get_str(v), yyjson_get_len(v));
            if (!out->str) return -1;
            break;
        case PIN_STRUCT:
        case PIN_FLOW:
            if (!yyjson_is_null(v)) return -1;
            break;
    }
    return 0;
}

static void write_pin(yyjson_mut_doc *doc, yyjson_mut_val *arr, const pin_spec *p)
{
    yyjson_mut_val *o = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, o, "id", p->id);
    yyjson_mut_obj_add_str(doc, o, "name", p->name ? p->name : "");
    yyjson_mut_obj_add_int(doc, o, "type", p->type);
    if (p->type_name) yyjson_mut_obj_add_str(doc, o, "type_name", p->type_name);
    yyjson_mut_obj_add_str(doc, o, "kind", p->kind == PIN_EXEC ? "exec" : "data");
    yyjson_mut_obj_add_int(doc, o, "container", p->container);
    yyjson_mut_obj_add_uint(doc, o, "split_parent", p->split_parent);
    yyjson_mut_obj_add_uint(doc, o, "flags", p->flags);
    yyjson_mut_obj_add_int(doc, o, "group", p->group);
    write_const(doc, o, "default", p->type, p->type_name, &p->default_value);
    yyjson_mut_arr_add_val(arr, o);
}

static void write_edge(yyjson_mut_doc *doc, yyjson_mut_val *arr, const edge *e)
{
    yyjson_mut_val *o = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, o, "from", e->from_id);
    yyjson_mut_obj_add_uint(doc, o, "from_pin", e->from_pin_id);
    yyjson_mut_obj_add_uint(doc, o, "to", e->to_id);
    yyjson_mut_obj_add_uint(doc, o, "to_pin", e->to_pin_id);
    yyjson_mut_obj_add_str(doc, o, "kind", graph_edge_kind_name(e->kind));
    if (e->kind == GRAPH_EDGE_EXEC)
        yyjson_mut_obj_add_int(doc, o, "order", e->exec_order);
    yyjson_mut_arr_add_val(arr, o);
}

static void write_node(yyjson_mut_doc *doc, yyjson_mut_val *arr, const node *n)
{
    yyjson_mut_val *o = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, o, "id", n->id);
    yyjson_mut_obj_add_str(doc, o, "type", n->desc->name);
    yyjson_mut_obj_add_real(doc, o, "x", n->x);
    yyjson_mut_obj_add_real(doc, o, "y", n->y);
    if (n->title_override)
        yyjson_mut_obj_add_str(doc, o, "title", n->title_override);
    if (n->comment) yyjson_mut_obj_add_str(doc, o, "comment", n->comment);
    yyjson_mut_obj_add_uint(doc, o, "reference_id", n->reference_id);
    if (n->reference_name)
        yyjson_mut_obj_add_str(doc, o, "reference_name", n->reference_name);
    yyjson_mut_obj_add_bool(doc, o, "disabled", n->disabled != 0);
    yyjson_mut_val *ins = yyjson_mut_arr(doc), *outs = yyjson_mut_arr(doc);
    for (int i = 0; i < n->input_count; i++)
        write_pin(doc, ins, &n->input_pins[i]);
    for (int i = 0; i < n->output_count; i++) {
        pin_spec p = n->output_pins[i];
        if (n->desc->pure_kind == NODE_PURE_CONST)
            p.default_value = n->output_pins[i].default_value;
        write_pin(doc, outs, &p);
    }
    yyjson_mut_obj_add_val(doc, o, "inputs", ins);
    yyjson_mut_obj_add_val(doc, o, "outputs", outs);
    yyjson_mut_arr_add_val(arr, o);
}

static int compare_nodes(const void *a, const void *b)
{
    const node *left = *(node *const *)a, *right = *(node *const *)b;
    return left->id < right->id ? -1 : left->id > right->id;
}

static int compare_edges(const void *a, const void *b)
{
    const edge *left = a, *right = b;
#define CMP_FIELD(field)                                                       \
    if (left->field != right->field) return left->field < right->field ? -1 : 1
    CMP_FIELD(from_id);
    CMP_FIELD(from_pin_id);
    CMP_FIELD(to_id);
    CMP_FIELD(to_pin_id);
    CMP_FIELD(kind);
    CMP_FIELD(exec_order);
#undef CMP_FIELD
    return 0;
}

static char *write_graph(const graph *g, node *const *subset, int count)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return NULL;
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "version", GRAPH_SCHEMA_VERSION);
    yyjson_mut_obj_add_uint(doc, root, "id_counter", g->id_counter);
    yyjson_mut_obj_add_str(doc, root, "name", g->name ? g->name : "");
    yyjson_mut_obj_add_str(doc, root, "description", g->description ? g->description : "");
    yyjson_mut_val *nodes = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "nodes", nodes);
    int node_total = subset ? count : acount(g->nodes);
    node **ordered_nodes = node_total
                             ? malloc((size_t)node_total * sizeof *ordered_nodes)
                             : NULL;
    if (node_total && !ordered_nodes) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    if (!subset)
        memcpy(ordered_nodes, g->nodes, (size_t)node_total * sizeof *ordered_nodes);
    else
        for (int i = 0; i < node_total; i++) ordered_nodes[i] = subset[i];
    qsort(ordered_nodes, (size_t)node_total, sizeof *ordered_nodes, compare_nodes);
    for (int i = 0; i < node_total; i++)
        if (ordered_nodes[i]) write_node(doc, nodes, ordered_nodes[i]);
    free(ordered_nodes);
    yyjson_mut_val *edges = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "edges", edges);
    edge *ordered_edges = acount(g->edges)
                            ? malloc((size_t)acount(g->edges) * sizeof *ordered_edges)
                            : NULL;
    int edge_total = 0;
    if (acount(g->edges) && !ordered_edges) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (subset) {
            node *from_node = graph_node_by_id(g, e->from_id);
            node *to_node = graph_node_by_id(g, e->to_id);
            int from = 0, to = 0;
            for (int j = 0; j < count; j++) {
                from |= subset[j] == from_node;
                to |= subset[j] == to_node;
            }
            if (!from || !to) continue;
        }
        ordered_edges[edge_total++] = *e;
    }
    qsort(ordered_edges, (size_t)edge_total, sizeof *ordered_edges, compare_edges);
    for (int i = 0; i < edge_total; i++)
        write_edge(doc, edges, &ordered_edges[i]);
    free(ordered_edges);
    char *out = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    return out;
}

char *graph_save_json(const graph *g)
{ return g ? write_graph(g, NULL, 0) : NULL; }
char *graph_save_nodes_json(const graph *g, node *const *subset, int count)
{
    return g && count >= 0 && (count == 0 || subset) ? write_graph(g, subset, count)
                                                     : NULL;
}

static int json_uint(yyjson_val *v, uint64_t *out, const char *path)
{
    if (!v || !yyjson_is_uint(v) || yyjson_get_uint(v) == 0) {
        set_err("%s must be a non-zero unsigned integer", path);
        return -1;
    }
    *out = yyjson_get_uint(v);
    return 0;
}

static int read_struct_default(const type_desc *type, yyjson_val *def, pin_value *out, graph *g, int pin)
{
    void *block = graph_own_block(g, type->size);
    if (!block) {
        set_err("bad struct default for pin %d", pin);
        return -1;
    }
    for (int f = 0; f < type->field_count; f++) {
        yyjson_val *field_json = yyjson_obj_get(def, type->fields[f].name);
        if (!field_json) {
            set_err("missing struct field %s", type->fields[f].name);
            return -1;
        }
        pin_value field;
        if (read_const(type->fields[f].type, field_json, &field, g)) {
            set_err("bad struct field %s", type->fields[f].name);
            return -1;
        }
        field.type_name = type->fields[f].type_name;
        pin_write_field(block, &type->fields[f], field);
    }
    *out = (pin_value){ .type = PIN_STRUCT, .ptr = block };
    return 0;
}

static int load_pins(graph *g, node *n, yyjson_val *arr, int output)
{
    if (!arr || !yyjson_is_arr(arr)) {
        set_err(
            "node %llu has a missing or invalid %s pin list",
            (unsigned long long)n->id,
            output ? "output" : "input"
        );
        return -1;
    }
    yyjson_arr_iter it;
    yyjson_arr_iter_init(arr, &it);
    yyjson_val *v;
    int i = 0;
    int count = output ? n->output_count : n->input_count;
    while ((v = yyjson_arr_iter_next(&it))) {
        if (!yyjson_is_obj(v)) {
            set_err("node %llu has invalid %s pin list", (unsigned long long)n->id, output ? "output" : "input");
            return -1;
        }
        if (i >= count) {
            const pin_spec *template_pin = output
                                             ? &n->desc->outputs[n->desc->output_count - 1]
                                             : &n->desc->inputs[n->desc->input_count - 1];
            if (graph_add_pin(g, n, output, template_pin) < 0) {
                set_err(
                    "node %llu cannot extend %s pin list",
                    (unsigned long long)n->id,
                    output ? "output" : "input"
                );
                return -1;
            }
            count++;
        }
        pin_spec *p = output ? &n->output_pins[i] : &n->input_pins[i];
        uint64_t id = 0;
        if (json_uint(yyjson_obj_get(v, "id"), &id, "pin.id")) return -1;
        p->id = id;
        yyjson_val *name = yyjson_obj_get(v, "name");
        yyjson_val *type = yyjson_obj_get(v, "type");
        if (!yyjson_is_str(name) || !yyjson_is_int(type) ||
            yyjson_get_sint(type) < PIN_FLOW || yyjson_get_sint(type) > PIN_ENUM) {
            set_err("node %llu has malformed pin %d", (unsigned long long)n->id, i);
            return -1;
        }
        p->name = graph_own_str(g, yyjson_get_str(name), yyjson_get_len(name));
        p->type = (pin_type)yyjson_get_sint(type);
        yyjson_val *tn = yyjson_obj_get(v, "type_name");
        p->type_name = yyjson_is_str(tn)
                         ? graph_own_str(g, yyjson_get_str(tn), yyjson_get_len(tn))
                         : NULL;
        yyjson_val *kind = yyjson_obj_get(v, "kind");
        if (!yyjson_is_str(kind)) {
            set_err("pin %d missing kind", i);
            return -1;
        }
        p->kind = strcmp(yyjson_get_str(kind), "exec") == 0 ? PIN_EXEC : PIN_DATA;
        yyjson_val *container = yyjson_obj_get(v, "container");
        if (container && yyjson_is_int(container))
            p->container = (pin_container)yyjson_get_sint(container);
        yyjson_val *split = yyjson_obj_get(v, "split_parent");
        if (split && yyjson_is_uint(split))
            p->split_parent = yyjson_get_uint(split);
        yyjson_val *flags = yyjson_obj_get(v, "flags");
        if (flags && yyjson_is_uint(flags))
            p->flags = (unsigned)yyjson_get_uint(flags);
        yyjson_val *group = yyjson_obj_get(v, "group");
        if (group && yyjson_is_int(group))
            p->group = (int)yyjson_get_sint(group);
        yyjson_val *def = yyjson_obj_get(v, "default");
        if (def) {
            pin_value pv;
            if (p->type == PIN_STRUCT && yyjson_is_obj(def)) {
                const type_desc *type = find_type(p->type_name);
                if (!type) {
                    set_err("bad struct default for pin %d", i);
                    return -1;
                }
                if (read_struct_default(type, def, &pv, g, i)) return -1;
            } else if (read_const(p->type, def, &pv, g)) {
                set_err("bad default for pin %d on %s (type %d)", i, n->desc->name, p->type);
                return -1;
            }
            pv.type_name = p->type_name;
            p->default_value = pv;
            if (!output) n->inputs[i] = pv;
            else n->outputs[i] = pv;
        }
        i++;
    }
    return i == count ? 0
                      : (set_err(
                             "node %llu pin count does not match descriptor",
                             (unsigned long long)n->id
                         ),
                         -1);
}

graph *graph_load_json(const char *json)
{
    if (!json) {
        set_err("null JSON input");
        return NULL;
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        set_err("malformed JSON");
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *version = yyjson_is_obj(root) ? yyjson_obj_get(root, "version") : NULL;
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != GRAPH_SCHEMA_VERSION) {
        set_err("unsupported graph schema version (expected 3)");
        yyjson_doc_free(doc);
        return NULL;
    }
    yyjson_val *nodes = yyjson_obj_get(root, "nodes"),
               *edges = yyjson_obj_get(root, "edges");
    if (!yyjson_is_arr(nodes) || !yyjson_is_arr(edges)) {
        set_err("missing or invalid nodes or edges array");
        yyjson_doc_free(doc);
        return NULL;
    }
    graph *g = (graph *)calloc(1, sizeof *g);
    if (!g) {
        set_err("out of memory");
        yyjson_doc_free(doc);
        return NULL;
    }
    graph_init(g);
    uint64_t requested_counter = 1;
    yyjson_val *counter = yyjson_obj_get(root, "id_counter");
    if (!counter || !yyjson_is_uint(counter) || yyjson_get_uint(counter) == 0) {
        set_err("missing or invalid id_counter");
        goto fail;
    }
    requested_counter = yyjson_get_uint(counter);
    g->id_counter = requested_counter;
    yyjson_val *metadata_name = yyjson_obj_get(root, "name");
    yyjson_val *metadata_description = yyjson_obj_get(root, "description");
    if (yyjson_is_str(metadata_name))
        g->name = graph_own_str(g, yyjson_get_str(metadata_name), yyjson_get_len(metadata_name));
    if (yyjson_is_str(metadata_description))
        g->description = graph_own_str(g, yyjson_get_str(metadata_description), yyjson_get_len(metadata_description));
    if (!g->name || !g->description) {
        set_err("missing blueprint metadata");
        goto fail;
    }
    yyjson_arr_iter it;
    yyjson_arr_iter_init(nodes, &it);
    yyjson_val *v;
    while ((v = yyjson_arr_iter_next(&it))) {
        if (!yyjson_is_obj(v)) {
            set_err("node is not an object");
            goto fail;
        }
        yyjson_val *type = yyjson_obj_get(v, "type");
        if (!yyjson_is_str(type)) {
            set_err("node is missing type");
            goto fail;
        }
        const node_desc *desc = registry_node_by_name(yyjson_get_str(type));
        if (!desc) {
            set_err("unknown node type \"%s\"", yyjson_get_str(type));
            goto fail;
        }
        node *n = graph_add_node(g, desc);
        if (!n) {
            set_err("out of memory");
            goto fail;
        }
        uint64_t id = 0;
        if (json_uint(yyjson_obj_get(v, "id"), &id, "node.id")) goto fail;
        n->id = id;
        yyjson_val *x = yyjson_obj_get(v, "x"), *y = yyjson_obj_get(v, "y");
        if (x && yyjson_is_num(x)) n->x = (float)yyjson_get_real(x);
        if (y && yyjson_is_num(y)) n->y = (float)yyjson_get_real(y);
        yyjson_val *title = yyjson_obj_get(v, "title"),
                   *comment = yyjson_obj_get(v, "comment"),
                   *refname = yyjson_obj_get(v, "reference_name");
        if (title && yyjson_is_str(title))
            n->title_override = graph_own_str(g, yyjson_get_str(title), yyjson_get_len(title));
        if (comment && yyjson_is_str(comment))
            n->comment = graph_own_str(g, yyjson_get_str(comment), yyjson_get_len(comment));
        yyjson_val *refid = yyjson_obj_get(v, "reference_id");
        if (refid && yyjson_is_uint(refid))
            n->reference_id = yyjson_get_uint(refid);
        if (refname && yyjson_is_str(refname))
            n->reference_name = graph_own_str(g, yyjson_get_str(refname), yyjson_get_len(refname));
        yyjson_val *disabled = yyjson_obj_get(v, "disabled");
        n->disabled = disabled && yyjson_is_bool(disabled) &&
                      yyjson_get_bool(disabled);
        if (load_pins(g, n, yyjson_obj_get(v, "inputs"), 0) ||
            load_pins(g, n, yyjson_obj_get(v, "outputs"), 1))
            goto fail;
        if (n->id == 0) {
            set_err("node ID zero is not allowed");
            goto fail;
        }
        for (int j = 0; j < acount(g->nodes) - 1; j++) {
            node *other = g->nodes[j];
            if (other->id == n->id) {
                set_err("duplicate node ID");
                goto fail;
            }
            for (int p = 0; p < other->input_count; p++)
                if (other->input_pins[p].id == n->id) {
                    set_err("node ID collides with pin ID");
                    goto fail;
                }
            for (int p = 0; p < other->output_count; p++)
                if (other->output_pins[p].id == n->id) {
                    set_err("node ID collides with pin ID");
                    goto fail;
                }
            for (int p = 0; p < n->input_count; p++)
                for (int q = 0; q < other->input_count; q++)
                    if (other->input_pins[q].id == n->input_pins[p].id) {
                        set_err("duplicate pin ID");
                        goto fail;
                    }
            for (int p = 0; p < n->output_count; p++)
                for (int q = 0; q < other->output_count; q++)
                    if (other->output_pins[q].id == n->output_pins[p].id) {
                        set_err("duplicate pin ID");
                        goto fail;
                    }
        }
        for (int p = 0; p < n->input_count; p++) {
            if (n->input_pins[p].id == 0) {
                set_err("pin ID zero is not allowed");
                goto fail;
            }
            for (int q = p + 1; q < n->input_count; q++)
                if (n->input_pins[p].id == n->input_pins[q].id) {
                    set_err("duplicate pin ID");
                    goto fail;
                }
            for (int q = 0; q < n->output_count; q++)
                if (n->input_pins[p].id == n->output_pins[q].id) {
                    set_err("duplicate pin ID");
                    goto fail;
                }
        }
        for (int p = 0; p < n->output_count; p++) {
            if (n->output_pins[p].id == 0) {
                set_err("pin ID zero is not allowed");
                goto fail;
            }
            for (int q = p + 1; q < n->output_count; q++)
                if (n->output_pins[p].id == n->output_pins[q].id) {
                    set_err("duplicate pin ID");
                    goto fail;
                }
        }
        if (n->id >= g->id_counter) g->id_counter = n->id + 1;
        for (int i = 0; i < n->input_count; i++)
            if (n->input_pins[i].id >= g->id_counter)
                g->id_counter = n->input_pins[i].id + 1;
        for (int i = 0; i < n->output_count; i++)
            if (n->output_pins[i].id >= g->id_counter)
                g->id_counter = n->output_pins[i].id + 1;
    }
    g->id_counter = requested_counter;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (n->id >= g->id_counter) g->id_counter = n->id + 1;
        for (int p = 0; p < n->input_count; p++)
            if (n->input_pins[p].id >= g->id_counter)
                g->id_counter = n->input_pins[p].id + 1;
        for (int p = 0; p < n->output_count; p++)
            if (n->output_pins[p].id >= g->id_counter)
                g->id_counter = n->output_pins[p].id + 1;
    }
    graph_reindex(g);
    yyjson_arr_iter eit;
    yyjson_arr_iter_init(edges, &eit);
    while ((v = yyjson_arr_iter_next(&eit))) {
        if (!yyjson_is_obj(v)) {
            set_err("edge is not an object");
            goto fail;
        }
        uint64_t from_id, from_pin, to_id, to_pin;
        if (json_uint(yyjson_obj_get(v, "from"), &from_id, "edge.from") ||
            json_uint(yyjson_obj_get(v, "from_pin"), &from_pin, "edge.from_pin") ||
            json_uint(yyjson_obj_get(v, "to"), &to_id, "edge.to") ||
            json_uint(yyjson_obj_get(v, "to_pin"), &to_pin, "edge.to_pin"))
            goto fail;
        node *from = graph_node_by_id(g, from_id), *to = graph_node_by_id(g, to_id);
        int fp = -1, tp = -1, fo = 0, to_out = 0;
        const pin_spec *fps = graph_pin_by_id(from, from_pin, &fo, &fp),
                       *tps = graph_pin_by_id(to, to_pin, &to_out, &tp);
        yyjson_val *kind = yyjson_obj_get(v, "kind");
        if (!from || !to || !fps || !tps || fo != 1 || to_out != 0 ||
            !yyjson_is_str(kind)) {
            set_err("edge references missing node or pin ID");
            goto fail;
        }
        const char *kind_name = yyjson_get_str(kind);
        int r;
        if (strcmp(kind_name, "data") == 0) {
            if (yyjson_obj_get(v, "order")) {
                set_err("data edge has an exec order");
                goto fail;
            }
            r = graph_connect_data(g, from, fp, to, tp);
        } else if (strcmp(kind_name, "exec") == 0) {
            yyjson_val *order = yyjson_obj_get(v, "order");
            if (!yyjson_is_int(order) || yyjson_get_sint(order) < 0 ||
                yyjson_get_sint(order) > INT_MAX) {
                set_err("exec edge has an invalid order");
                goto fail;
            }
            r = graph_connect_exec(g, from, fp, to, tp, (int)yyjson_get_sint(order));
        } else {
            set_err("unsupported edge kind");
            goto fail;
        }
        if (r) {
            set_err("invalid edge endpoints or incompatible types");
            goto fail;
        }
    }
    yyjson_doc_free(doc);
    return g;
fail:
    yyjson_doc_free(doc);
    graph_free(g);
    free(g);
    return NULL;
}

static uint64_t remap_id(uint64_t old, const uint64_t *old_ids, const uint64_t *new_ids, int count)
{
    for (int i = 0; i < count; i++)
        if (old_ids[i] == old) return new_ids[i];
    return old;
}

int graph_append_json(graph *g, const char *json, float dx, float dy)
{
    if (!g || g->flow_pause_count) {
        set_err("cannot append while a flow run is non-idle");
        return -1;
    }
    graph *tmp = graph_load_json(json);
    if (!tmp) return -1;
    int node_count = acount(tmp->nodes);
    int id_count = node_count;
    for (int i = 0; i < node_count; i++)
        id_count += tmp->nodes[i]->input_count + tmp->nodes[i]->output_count;
    uint64_t *old_ids = (uint64_t *)xmalloc(
        (size_t)(id_count ? id_count : 1) * sizeof *old_ids
    );
    uint64_t *new_ids = (uint64_t *)xmalloc(
        (size_t)(id_count ? id_count : 1) * sizeof *new_ids
    );
    int k = 0;
    for (int i = 0; i < node_count; i++) {
        node *n = tmp->nodes[i];
        old_ids[k] = n->id;
        new_ids[k] = graph_alloc_id(g);
        n->id = new_ids[k];
        k++;
        n->x += dx;
        n->y += dy;
        for (int p = 0; p < n->input_count; p++) {
            old_ids[k] = n->input_pins[p].id;
            new_ids[k] = graph_alloc_id(g);
            n->input_pins[p].id = new_ids[k];
            k++;
        }
        for (int p = 0; p < n->output_count; p++) {
            old_ids[k] = n->output_pins[p].id;
            new_ids[k] = graph_alloc_id(g);
            n->output_pins[p].id = new_ids[k];
            k++;
        }
    }
    for (int i = 0; i < acount(tmp->edges); i++) {
        edge *e = &tmp->edges[i];
        e->from_id = remap_id(e->from_id, old_ids, new_ids, k);
        e->to_id = remap_id(e->to_id, old_ids, new_ids, k);
        e->from_pin_id = remap_id(e->from_pin_id, old_ids, new_ids, k);
        e->to_pin_id = remap_id(e->to_pin_id, old_ids, new_ids, k);
    }
    free(old_ids);
    free(new_ids);
    int nn = acount(g->nodes) + acount(tmp->nodes),
        ne = acount(g->edges) + acount(tmp->edges),
        ns = acount(g->strings) + acount(tmp->strings),
        nb = acount(g->blocks) + acount(tmp->blocks);
    asetcap(g->nodes, nn);
    asetcap(g->edges, ne);
    asetcap(g->strings, ns);
    asetcap(g->blocks, nb);
    memcpy(
        g->nodes + acount(g->nodes),
        tmp->nodes,
        (size_t)acount(tmp->nodes) * sizeof *tmp->nodes
    );
    memcpy(
        g->edges + acount(g->edges),
        tmp->edges,
        (size_t)acount(tmp->edges) * sizeof *tmp->edges
    );
    memcpy(
        g->strings + acount(g->strings),
        tmp->strings,
        (size_t)acount(tmp->strings) * sizeof *tmp->strings
    );
    memcpy(
        g->blocks + acount(g->blocks),
        tmp->blocks,
        (size_t)acount(tmp->blocks) * sizeof *tmp->blocks
    );
    asetlen(g->nodes, nn);
    asetlen(g->edges, ne);
    asetlen(g->strings, ns);
    asetlen(g->blocks, nb);
    graph_reindex(g);
    g->topology_revision++;
    afree(tmp->nodes);
    afree(tmp->edges);
    afree(tmp->strings);
    afree(tmp->blocks);
    free(tmp);
    return 0;
}
