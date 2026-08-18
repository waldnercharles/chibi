#include "serialize.h"

#include "../third_party/yyjson/yyjson.h"
#include "nodes.h"
#include "registry.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Serialization diagnostics are thread-local so one load cannot overwrite a
 * concurrent caller's error. Runtime evaluation uses explicit contexts. */
static _Thread_local char serialize_error[256];

static void set_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(serialize_error, sizeof serialize_error, fmt, ap);
    va_end(ap);
}

const char *graph_last_error(void) { return serialize_error; }

static int node_index(const graph *g, const node *n)
{
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i] == n) return i;
    return -1;
}

static void write_edge(yyjson_mut_doc *doc, yyjson_mut_val *arr, const edge *e, int from, int to)
{
    yyjson_mut_val *eo = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, eo, "from", from);
    yyjson_mut_obj_add_int(doc, eo, "from_pin", e->from_pin);
    yyjson_mut_obj_add_int(doc, eo, "to", to);
    yyjson_mut_obj_add_int(doc, eo, "to_pin", e->to_pin);
    yyjson_mut_obj_add_str(doc, eo, "kind", e->kind == GRAPH_EDGE_EXEC ? "exec" : "data");
    if (e->kind == GRAPH_EDGE_EXEC)
        yyjson_mut_obj_add_int(doc, eo, "order", e->exec_order);
    yyjson_mut_arr_add_val(arr, eo);
}

/* Write a constant pin value, interpreted as pin type t. */
/* Write a constant pin value, interpreted as pin type t. The key and any
 * string value are copied into the document, so callers may pass stack or
 * temporary buffers. */
static void write_const(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *key,
    pin_type t,
    const pin_value *v
)
{
    yyjson_mut_val *k = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val = NULL;
    switch (t) {
        case PIN_BOOL: val = yyjson_mut_bool(doc, v->b); break;
        case PIN_I8: val = yyjson_mut_sint(doc, v->i8); break;
        case PIN_U8: val = yyjson_mut_uint(doc, v->u8); break;
        case PIN_I16: val = yyjson_mut_sint(doc, v->i16); break;
        case PIN_U16: val = yyjson_mut_uint(doc, v->u16); break;
        case PIN_I32: val = yyjson_mut_sint(doc, v->i); break;
        case PIN_U32: val = yyjson_mut_uint(doc, v->u); break;
        case PIN_I64: val = yyjson_mut_sint(doc, v->i64); break;
        case PIN_U64: val = yyjson_mut_uint(doc, v->u64); break;
        case PIN_F32: val = yyjson_mut_real(doc, v->f); break;
        case PIN_F64: val = yyjson_mut_real(doc, v->d); break;
        case PIN_ENUM: val = yyjson_mut_sint(doc, v->e); break;
        case PIN_STR: val = yyjson_mut_strcpy(doc, v->str ? v->str : ""); break;
        case PIN_STRUCT:
        case PIN_FLOW:
        default: return; /* not a serializable constant */
    }
    if (k && val) yyjson_mut_obj_add(obj, k, val);
}

/* Read a JSON constant into out, interpreted as pin type t. Returns 0 on
 * success, -1 on type mismatch or OOM. */
static int read_const(pin_type t, yyjson_val *v, pin_value *out, graph *g)
{
    memset(out, 0, sizeof *out);
    switch (t) {
        case PIN_BOOL:
            if (!yyjson_is_bool(v)) return -1;
            out->b = yyjson_get_bool(v);
            break;
        case PIN_I8:
            if (!yyjson_is_sint(v) || yyjson_get_sint(v) < INT8_MIN ||
                yyjson_get_sint(v) > INT8_MAX)
                return -1;
            out->i8 = (int8_t)yyjson_get_sint(v);
            break;
        case PIN_U8:
            if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT8_MAX) return -1;
            out->u8 = (uint8_t)yyjson_get_uint(v);
            break;
        case PIN_I16:
            if (!yyjson_is_sint(v) || yyjson_get_sint(v) < INT16_MIN ||
                yyjson_get_sint(v) > INT16_MAX)
                return -1;
            out->i16 = (int16_t)yyjson_get_sint(v);
            break;
        case PIN_U16:
            if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT16_MAX)
                return -1;
            out->u16 = (uint16_t)yyjson_get_uint(v);
            break;
        case PIN_I32:
            if (!yyjson_is_sint(v) || yyjson_get_sint(v) < INT32_MIN ||
                yyjson_get_sint(v) > INT32_MAX)
                return -1;
            out->i = (int32_t)yyjson_get_sint(v);
            break;
        case PIN_U32:
            if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT32_MAX)
                return -1;
            out->u = (uint32_t)yyjson_get_uint(v);
            break;
        case PIN_I64:
            if (!yyjson_is_sint(v)) return -1;
            out->i64 = (int64_t)yyjson_get_sint(v);
            break;
        case PIN_U64:
            if (!yyjson_is_uint(v)) return -1;
            out->u64 = (uint64_t)yyjson_get_uint(v);
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
            if (!yyjson_is_sint(v) || yyjson_get_sint(v) < INT32_MIN ||
                yyjson_get_sint(v) > INT32_MAX)
                return -1;
            out->e = (int32_t)yyjson_get_sint(v);
            break;
        case PIN_STR: {
            if (!yyjson_is_str(v)) return -1;
            const char *s = yyjson_get_str(v);
            out->str = graph_own_str(g, s, yyjson_get_len(v));
            if (!out->str) return -1;
            break;
        }
        case PIN_STRUCT:
        case PIN_FLOW:
        default: return -1; /* no constant representation */
    }
    out->type = t;
    return 0;
}

#define GRAPH_SCHEMA_VERSION 2

static void write_node(yyjson_mut_doc *doc, yyjson_mut_val *arr, const graph *g, const node *n, int id)
{
    yyjson_mut_val *no = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, no, "id", id);
    yyjson_mut_obj_add_str(doc, no, "type", n->desc->name);
    if (n->event_name) yyjson_mut_obj_add_str(doc, no, "event", n->event_name);
    yyjson_mut_obj_add_real(doc, no, "x", (double)n->x);
    yyjson_mut_obj_add_real(doc, no, "y", (double)n->y);

    yyjson_mut_val *consts = yyjson_mut_obj(doc);
    char key[16];
    for (int p = 0; p < n->desc->input_count; p++) {
        node *src;
        int sp;
        if (!graph_input_source(g, n, p, &src, &sp)) {
            snprintf(key, sizeof key, "in%d", p);
            write_const(doc, consts, key, n->desc->inputs[p].type, &n->inputs[p]);
        }
    }
    if (n->desc->eval == node_eval_const) {
        for (int p = 0; p < n->desc->output_count; p++) {
            snprintf(key, sizeof key, "out%d", p);
            write_const(doc, consts, key, n->desc->outputs[p].type, &n->outputs[p]);
        }
    }
    yyjson_mut_obj_add_val(doc, no, "consts", consts);
    yyjson_mut_arr_add_val(arr, no);
}

char *graph_save_json(const graph *g)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return NULL;
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "version", GRAPH_SCHEMA_VERSION);

    yyjson_mut_val *nodes = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "nodes", nodes);

    for (int i = 0; i < g->node_count; i++)
        write_node(doc, nodes, g, g->nodes[i], i);

    yyjson_mut_val *edges = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "edges", edges);
    for (int i = 0; i < g->edge_count; i++) {
        const edge *e = &g->edges[i];
        int fi = node_index(g, e->from);
        int ti = node_index(g, e->to);
        if (fi < 0 || ti < 0) continue;
        write_edge(doc, edges, e, fi, ti);
    }

    yyjson_write_err werr;
    char *out = yyjson_mut_write_opts(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL, NULL, &werr);
    if (!out) set_err("write failed: %s", werr.msg ? werr.msg : "unknown");
    yyjson_mut_doc_free(doc);
    return out;
}

static int subset_index(node *const *subset, int count, const node *n)
{
    for (int i = 0; i < count; i++)
        if (subset[i] == n) return i;
    return -1;
}

char *graph_save_nodes_json(const graph *g, node *const *subset, int count)
{
    if (count < 0 || (count > 0 && !subset)) return NULL;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return NULL;
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "version", GRAPH_SCHEMA_VERSION);

    yyjson_mut_val *nodes = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "nodes", nodes);
    for (int i = 0; i < count; i++) write_node(doc, nodes, g, subset[i], i);

    yyjson_mut_val *edges = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "edges", edges);
    for (int i = 0; i < g->edge_count; i++) {
        const edge *e = &g->edges[i];
        int fi = subset_index(subset, count, e->from);
        int ti = subset_index(subset, count, e->to);
        if (fi < 0 || ti < 0) continue;
        write_edge(doc, edges, e, fi, ti);
    }

    char *out = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    return out;
}

static int parse_pin_key(const char *k, int *is_in, int *pin)
{
    const char *digits;
    if (strncmp(k, "in", 2) == 0) {
        *is_in = 1;
        digits = k + 2;
    } else if (strncmp(k, "out", 3) == 0) {
        *is_in = 0;
        digits = k + 3;
    } else {
        return -1;
    }
    if (!*digits || strspn(digits, "0123456789") != strlen(digits)) return -1;
    *pin = atoi(digits);
    return 0;
}

static int load_consts(const node_desc *desc, node *n, yyjson_val *consts, graph *g)
{
    yyjson_obj_iter it;
    yyjson_obj_iter_init(consts, &it);
    yyjson_val *kv;
    while ((kv = yyjson_obj_iter_next(&it))) {
        const char *k = yyjson_get_str(kv);
        yyjson_val *val = yyjson_obj_iter_get_val(kv);
        int is_in, pin;
        if (!k || parse_pin_key(k, &is_in, &pin)) {
            set_err("bad constant key \"%s\"", k ? k : "");
            return -1;
        }
        if (pin < 0) {
            set_err("negative pin index in constants for \"%s\"", desc->name);
            return -1;
        }
        if (is_in) {
            if (pin >= desc->input_count) {
                set_err("input pin index %d out of range for \"%s\"", pin, desc->name);
                return -1;
            }
            pin_value pv;
            if (read_const(desc->inputs[pin].type, val, &pv, g)) {
                set_err("bad value for \"%s\" input %d", desc->name, pin);
                return -1;
            }
            pv.type_name = desc->inputs[pin].type_name;
            n->inputs[pin] = pv;
        } else {
            if (pin >= desc->output_count) {
                set_err("output pin index %d out of range for \"%s\"", pin, desc->name);
                return -1;
            }
            pin_value pv;
            if (read_const(desc->outputs[pin].type, val, &pv, g)) {
                set_err("bad value for \"%s\" output %d", desc->name, pin);
                return -1;
            }
            pv.type_name = desc->outputs[pin].type_name;
            n->outputs[pin] = pv;
        }
    }
    return 0;
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
    if (!yyjson_is_obj(root)) {
        set_err("root is not a JSON object");
        yyjson_doc_free(doc);
        return NULL;
    }

    yyjson_val *version = yyjson_obj_get(root, "version");
    int schema = 0; /* versionless and v1 are data-only legacy schemas. */
    if (version) {
        if (!yyjson_is_uint(version)) {
            set_err("unsupported graph schema version");
            yyjson_doc_free(doc);
            return NULL;
        }
        uint64_t value = yyjson_get_uint(version);
        if (value != 1 && value != GRAPH_SCHEMA_VERSION) {
            set_err("unsupported graph schema version");
            yyjson_doc_free(doc);
            return NULL;
        }
        schema = (int)value;
    }
    yyjson_val *nodes = yyjson_obj_get(root, "nodes");
    yyjson_val *edges = yyjson_obj_get(root, "edges");
    if (!yyjson_is_arr(nodes)) {
        set_err("missing or invalid \"nodes\" array");
        yyjson_doc_free(doc);
        return NULL;
    }
    if (!yyjson_is_arr(edges)) {
        set_err("missing or invalid \"edges\" array");
        yyjson_doc_free(doc);
        return NULL;
    }

    graph *g = (graph *)calloc(1, sizeof *g);
    if (!g) {
        set_err("out of memory");
        yyjson_doc_free(doc);
        return NULL;
    }

    yyjson_val *nv;
    yyjson_arr_iter nit;
    yyjson_arr_iter_init(nodes, &nit);
    size_t idx = 0;
    while ((nv = yyjson_arr_iter_next(&nit))) {
        if (!yyjson_is_obj(nv)) {
            set_err("node %zu is not an object", idx);
            goto fail;
        }
        yyjson_val *typev = yyjson_obj_get(nv, "type");
        if (!yyjson_is_str(typev)) {
            set_err("node %zu is missing its \"type\"", idx);
            goto fail;
        }
        const char *tname = yyjson_get_str(typev);
        const node_desc *desc = registry_node_by_name(tname);
        if (!desc) {
            set_err("unknown node type \"%s\"", tname);
            goto fail;
        }
        node *n = graph_add_node(g, desc);
        if (!n) {
            set_err("out of memory");
            goto fail;
        }
        yyjson_val *xv = yyjson_obj_get(nv, "x");
        yyjson_val *yv = yyjson_obj_get(nv, "y");
        if (xv && !yyjson_is_num(xv)) {
            set_err("node %zu has invalid x position", idx);
            goto fail;
        }
        if (yv && !yyjson_is_num(yv)) {
            set_err("node %zu has invalid y position", idx);
            goto fail;
        }
        if (yyjson_is_num(xv)) n->x = (float)yyjson_get_real(xv);
        if (yyjson_is_num(yv)) n->y = (float)yyjson_get_real(yv);
        yyjson_val *consts = yyjson_obj_get(nv, "consts");
        if (consts && !yyjson_is_obj(consts)) {
            set_err("node %zu has invalid constants", idx);
            goto fail;
        }
        yyjson_val *eventv = yyjson_obj_get(nv, "event");
        if (eventv && !yyjson_is_str(eventv)) {
            set_err("node %zu has an invalid event name", idx);
            goto fail;
        }
        if (eventv) {
            if (graph_set_event_name(g, n, yyjson_get_str(eventv))) {
                set_err("node %zu has an event name but is not an Event node", idx);
                goto fail;
            }
        }
        if (consts && load_consts(desc, n, consts, g)) goto fail;
        idx++;
    }

    yyjson_val *ev;
    yyjson_arr_iter eit;
    yyjson_arr_iter_init(edges, &eit);
    while ((ev = yyjson_arr_iter_next(&eit))) {
        if (!yyjson_is_obj(ev)) {
            set_err("edge is not an object");
            goto fail;
        }
        yyjson_val *kindv = yyjson_obj_get(ev, "kind");
        yyjson_val *orderv = yyjson_obj_get(ev, "order");
        graph_edge_kind kind = GRAPH_EDGE_DATA;
        int order = 0;
        if (schema >= 2) {
            if (!yyjson_is_str(kindv)) {
                set_err("v2 edge is missing its kind");
                goto fail;
            }
            const char *kind_name = yyjson_get_str(kindv);
            if (strcmp(kind_name, "data") == 0) {
                if (orderv) {
                    set_err("data edge has an exec order");
                    goto fail;
                }
            } else if (strcmp(kind_name, "exec") == 0) {
                if (!yyjson_is_int(orderv) || yyjson_get_sint(orderv) < 0 ||
                    yyjson_get_sint(orderv) > INT_MAX) {
                    set_err("exec edge has an invalid order");
                    goto fail;
                }
                kind = GRAPH_EDGE_EXEC;
                order = (int)yyjson_get_sint(orderv);
            } else {
                set_err("unsupported edge kind");
                goto fail;
            }
        } else {
            if (orderv || (kindv && (!yyjson_is_str(kindv) ||
                                     strcmp(yyjson_get_str(kindv), "data") != 0))) {
                set_err("legacy data-only edge has flow metadata");
                goto fail;
            }
        }
        yyjson_val *fv = yyjson_obj_get(ev, "from");
        yyjson_val *fpv = yyjson_obj_get(ev, "from_pin");
        yyjson_val *tv = yyjson_obj_get(ev, "to");
        yyjson_val *tpv = yyjson_obj_get(ev, "to_pin");
        if (!yyjson_is_int(fv) || !yyjson_is_int(fpv) || !yyjson_is_int(tv) ||
            !yyjson_is_int(tpv) || yyjson_get_sint(fv) < INT_MIN ||
            yyjson_get_sint(fv) > INT_MAX || yyjson_get_sint(fpv) < INT_MIN ||
            yyjson_get_sint(fpv) > INT_MAX || yyjson_get_sint(tv) < INT_MIN ||
            yyjson_get_sint(tv) > INT_MAX || yyjson_get_sint(tpv) < INT_MIN ||
            yyjson_get_sint(tpv) > INT_MAX) {
            set_err("edge has a missing, non-integer, or out-of-range endpoint");
            goto fail;
        }
        int from = (int)yyjson_get_sint(fv);
        int from_pin = (int)yyjson_get_sint(fpv);
        int to = (int)yyjson_get_sint(tv);
        int to_pin = (int)yyjson_get_sint(tpv);
        if (from_pin < 0 || to_pin < 0) {
            set_err("edge has a negative pin index");
            goto fail;
        }
        if (from < 0 || from >= g->node_count || to < 0 || to >= g->node_count) {
            set_err("edge references an out-of-range node index");
            goto fail;
        }
        int connected = kind == GRAPH_EDGE_EXEC
                          ? graph_connect_exec(g, g->nodes[from], from_pin, g->nodes[to], to_pin, order)
                          : graph_connect_data(g, g->nodes[from], from_pin, g->nodes[to], to_pin);
        if (connected) {
            set_err("invalid edge: bad pin, kind, order, or incompatible types");
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

int graph_append_json(graph *g, const char *json, float dx, float dy)
{
    if (!g || g->flow_pause_count) {
        set_err("cannot append while a flow run is non-idle");
        return -1;
    }
    graph *tmp = graph_load_json(json);
    if (!tmp) return -1;

    for (int i = 0; i < tmp->node_count; i++) {
        tmp->nodes[i]->x += dx;
        tmp->nodes[i]->y += dy;
    }

    /* Pre-grow g's arrays so the append is atomic on OOM: counts change only
     * after every realloc has succeeded. */
    int nn = g->node_count + tmp->node_count;
    int ne = g->edge_count + tmp->edge_count;
    int ns = g->string_count + tmp->string_count;

    if (nn > g->node_cap) {
        int cap = g->node_cap ? g->node_cap : 8;
        while (cap < nn) cap *= 2;
        node **v = (node **)realloc(g->nodes, (size_t)cap * sizeof *v);
        if (!v) goto oom;
        g->nodes = v;
        g->node_cap = cap;
    }
    if (ne > g->edge_cap) {
        int cap = g->edge_cap ? g->edge_cap : 8;
        while (cap < ne) cap *= 2;
        edge *v = (edge *)realloc(g->edges, (size_t)cap * sizeof *v);
        if (!v) goto oom;
        g->edges = v;
        g->edge_cap = cap;
    }
    if (ns > g->string_cap) {
        int cap = g->string_cap ? g->string_cap : 8;
        while (cap < ns) cap *= 2;
        char **v = (char **)realloc(g->strings, (size_t)cap * sizeof *v);
        if (!v) goto oom;
        g->strings = v;
        g->string_cap = cap;
    }

    memcpy(
        &g->nodes[g->node_count],
        tmp->nodes,
        (size_t)tmp->node_count * sizeof *tmp->nodes
    );
    memcpy(
        &g->edges[g->edge_count],
        tmp->edges,
        (size_t)tmp->edge_count * sizeof *tmp->edges
    );
    memcpy(
        &g->strings[g->string_count],
        tmp->strings,
        (size_t)tmp->string_count * sizeof *tmp->strings
    );
    g->node_count = nn;
    g->edge_count = ne;
    g->string_count = ns;
    g->topology_revision++;

    /* Transfer ownership: free tmp's pointer arrays, but keep the node structs
     * and string literals they point to (now owned by g). */
    free(tmp->nodes);
    free(tmp->edges);
    free(tmp->strings);
    free(tmp);
    return 0;

oom:
    set_err("out of memory");
    graph_free(tmp);
    free(tmp);
    return -1;
}
