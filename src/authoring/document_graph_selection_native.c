#include "authoring/document_graph_selection_definition.h"
#include "execution/function.h"
#include "support/array.h"
#include "types/array.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy(arena *storage, const char *value)
{
    const char *text = value ? value : "";
    char *result = arena_alloc(storage, strlen(text) + 1);
    strcpy(result, text);
    return result;
}
static bool identity(const char *text, uint64_t *value)
{
    if (!text || !*text) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    char *end;
    *value = strtoull(text, &end, 10);
    return !errno && !*end;
}
static bool identities(const pin_array *array, document_graph_selection_request *request, uint64_t *ids)
{
    if (!array || array->count < 1 || array->count > DOCUMENT_GRAPH_SELECTION_MAX) {
        return false;
    }
    request->nodes = ids;
    request->count = array->count;
    for (int i = 0; i < array->count; i++) {
        if (!identity(array->items[i].str, &ids[i])) {
            return false;
        }
    }
    return true;
}
static bool points(
    const pin_array *x,
    const pin_array *y,
    document_graph_selection_request *request,
    float *xs,
    float *ys
)
{
    if (!x && !y) {
        return true;
    }
    if (!x || !y || x->count != y->count || x->count < 0 ||
        x->count > DOCUMENT_GRAPH_SELECTION_MAX) {
        return false;
    }
    request->x = xs;
    request->y = ys;
    request->points = x->count;
    for (int i = 0; i < x->count; i++) {
        xs[i] = x->items[i].f;
        ys[i] = y->items[i].f;
    }
    return true;
}
static const char *run(
    int operation,
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    document_semantics_service *service = host;
    if (!service) {
        out[0].b = false;
        out[1].str = copy(context->value_storage, "Semantic document service is unavailable");
        out[2].str = copy(context->value_storage, "");
        out[3].str = copy(context->value_storage, "");
        out[4].ptr = pin_array_snapshot_allocate(context->value_storage, PIN_STR, "const char *", 0);
        out[5].str = copy(context->value_storage, "0");
        return NULL;
    }
    document_graph_selection_request request = { 0 };
    uint64_t ids[DOCUMENT_GRAPH_SELECTION_MAX];
    float x[DOCUMENT_GRAPH_SELECTION_MAX], y[DOCUMENT_GRAPH_SELECTION_MAX];
    bool valid = operation == 4 || identity(in[2].str, &request.graph);
    switch (operation) {
        case 0:
            request.operation = in[3].i;
            valid = valid && in[3].i >= DOCUMENT_SELECTION_DELETE &&
                    in[3].i <= DOCUMENT_SELECTION_PLACE &&
                    identities(in[4].ptr, &request, ids) &&
                    points(in[5].ptr, in[6].ptr, &request, x, y);
            request.dx = in[7].f;
            request.dy = in[8].f;
            break;
        case 1:
            request.operation = DOCUMENT_SELECTION_COMMENT;
            valid = valid && identity(in[3].str, &request.node);
            request.text = in[4].str;
            request.hidden = in[5].b;
            break;
        case 2:
            request.operation = in[4].b ? DOCUMENT_SELECTION_REMOVE_BOX
                                        : DOCUMENT_SELECTION_BOX;
            valid = valid && identity(in[3].str, &request.box) && in[6].u64 <= UINT32_MAX;
            request.text = in[5].str;
            request.color = (uint32_t)in[6].u64;
            request.dx = in[7].f;
            request.dy = in[8].f;
            request.width = in[9].f;
            request.height = in[10].f;
            break;
        case 3:
            request.operation = DOCUMENT_SELECTION_REROUTE;
            valid = valid && identity(in[3].str, &request.from_node) &&
                    identity(in[4].str, &request.from_pin) &&
                    identity(in[5].str, &request.to_node) &&
                    identity(in[6].str, &request.to_pin) &&
                    points(in[7].ptr, in[8].ptr, &request, x, y);
            break;
        case 4:
            request.operation = DOCUMENT_SELECTION_SOURCE_COPY;
            valid = valid && identities(in[2].ptr, &request, ids);
            break;
        case 5:
            request.operation = DOCUMENT_SELECTION_COPY;
            valid = valid && identities(in[3].ptr, &request, ids);
            break;
        case 6:
            request.operation = DOCUMENT_SELECTION_PASTE;
            request.text = in[3].str;
            request.dx = in[4].f;
            request.dy = in[5].f;
            break;
    }
    char error[512] = { 0 };
    document_semantics *snapshot = valid ? document_semantics_service_snapshot(
                                               service,
                                               in[0].i,
                                               in[1].i,
                                               error,
                                               sizeof error
                                           )
                                         : NULL;
    document_graph_selection_result result = { 0 };
    if (snapshot) {
        valid = document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error);
    } else {
        valid = false;
        if (!*error) {
            strcpy(error, "Graph request requires bounded arrays and decimal identities");
        }
    }
    out[0].b = valid;
    out[1].str = copy(context->value_storage, error);
    out[2].str = copy(context->value_storage, result.text);
    out[3].str = copy(context->value_storage, result.metadata);
    pin_array *created = pin_array_snapshot_allocate(
        context->value_storage,
        PIN_STR,
        "const char *",
        acount(result.created)
    );
    for (int i = 0; i < created->count; i++) {
        char text[32];
        snprintf(text, sizeof text, "%llu", (unsigned long long)result.created[i]);
        created->items[i] = (pin_value){ .type = PIN_STR,
                                         .str = copy(context->value_storage, text) };
    }
    out[4].ptr = created;
    char box[32];
    snprintf(box, sizeof box, "%llu", (unsigned long long)result.box);
    out[5].str = copy(context->value_storage, box);
    document_graph_selection_result_free(&result);
    document_semantics_free(snapshot);
    return NULL;
}
static char *id_text(arena *storage, uint64_t value)
{
    char text[32];
    snprintf(text, sizeof text, "%llu", (unsigned long long)value);
    return copy(storage, text);
}
static const char *inspect(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    document_semantics_service *service = host;
    char error[512] = { 0 };
    uint64_t graph_id = 0;
    bool valid = service && identity(in[2].str, &graph_id);
    if (!service) {
        strcpy(error, "Semantic document service is unavailable");
    }
    document_semantics *snapshot = valid ? document_semantics_service_snapshot(
                                               service,
                                               in[0].i,
                                               in[1].i,
                                               error,
                                               sizeof error
                                           )
                                         : NULL;
    const behavior_asset *asset = snapshot ? document_semantics_graph(snapshot) : NULL;
    const graph *g = asset ? behavior_asset_find_graph(asset, graph_id) : NULL;
    valid = g && acount(g->nodes) <= 4096 && acount(g->edges) <= 16384;
    int notes = g ? acount(g->comment_boxes) : 0, routes = 0;
    for (int i = 0; valid && i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->comment && *n->comment) {
            notes++;
            valid = strlen(n->comment) <= 16384;
        }
    }
    for (int i = 0; valid && i < acount(g->comment_boxes); i++) {
        valid = strlen(g->comment_boxes[i].title) <= 1024;
    }
    for (int i = 0; valid && i < acount(g->edges); i++) {
        valid = g->edges[i].reroute_count >= 0 &&
                g->edges[i].reroute_count <= GRAPH_EDGE_MAX_REROUTES;
        routes += g->edges[i].reroute_count;
    }
    valid = valid && notes <= DOCUMENT_GRAPH_SELECTION_MAX && routes <= 512;
    if (!valid && !*error) {
        strcpy(error, "Presentation requires a valid graph within 4096 nodes, 256 notes and 512 reroute points");
    }
    pin_array *note_rows = pin_array_snapshot_allocate(
        context->value_storage,
        PIN_STRUCT,
        "DocumentGraphNote",
        valid ? notes : 0
    );
    pin_array *route_rows = pin_array_snapshot_allocate(
        context->value_storage,
        PIN_STRUCT,
        "DocumentGraphRoute",
        valid ? routes : 0
    );
    int n = 0, p = 0;
    for (int i = 0; valid && i < acount(g->comment_boxes); i++) {
        const graph_comment_box *box = &g->comment_boxes[i];
        document_graph_note *row = arena_alloc(context->value_storage, sizeof *row);
        *row = (document_graph_note){ .id = id_text(context->value_storage, box->id),
                                      .node = copy(context->value_storage, "0"),
                                      .title = copy(context->value_storage, box->title),
                                      .x = box->x,
                                      .y = box->y,
                                      .width = box->width,
                                      .height = box->height,
                                      .color = box->color,
                                      .box = true };
        note_rows->items[n++] = (pin_value){ .type = PIN_STRUCT,
                                             .type_name = "DocumentGraphNote",
                                             .ptr = row };
    }
    for (int i = 0; valid && i < acount(g->nodes); i++) {
        const node *node = g->nodes[i];
        if (!node->comment || !*node->comment) {
            continue;
        }
        document_graph_note *row = arena_alloc(context->value_storage, sizeof *row);
        *row = (document_graph_note){
            .id = id_text(context->value_storage, node->id),
            .node = id_text(context->value_storage, node->id),
            .title = copy(context->value_storage, node->comment),
            .x = node->x,
            .y = node->y,
            .hidden = node->comment_hidden
        };
        note_rows->items[n++] = (pin_value){ .type = PIN_STRUCT,
                                             .type_name = "DocumentGraphNote",
                                             .ptr = row };
    }
    for (int i = 0; valid && i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        for (int j = 0; j < e->reroute_count; j++) {
            document_graph_route *row = arena_alloc(context->value_storage, sizeof *row);
            *row = (document_graph_route){
                .from_node = id_text(context->value_storage, e->from_id),
                .from_pin = id_text(context->value_storage, e->from_pin_id),
                .to_node = id_text(context->value_storage, e->to_id),
                .to_pin = id_text(context->value_storage, e->to_pin_id),
                .index = j,
                .x = e->reroutes[j].x,
                .y = e->reroutes[j].y
            };
            route_rows->items[p++] = (pin_value){
                .type = PIN_STRUCT,
                .type_name = "DocumentGraphRoute",
                .ptr = row
            };
        }
    }
    out[0].b = valid;
    out[1].str = copy(context->value_storage, error);
    out[2].ptr = note_rows;
    out[3].ptr = route_rows;
    document_semantics_free(snapshot);
    return NULL;
}
static const char *flag(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    if (!host || !in) {
        out[0].b = false;
        out[1].str = copy(c->value_storage, !host ? "Semantic document service is unavailable" : "Missing node flag inputs");
        out[2].str = copy(c->value_storage, "");
        out[3].str = copy(c->value_storage, "");
        out[4].ptr = pin_array_snapshot_allocate(c->value_storage, PIN_STR, "const char *", 0);
        out[5].str = copy(c->value_storage, "0");
        return NULL;
    }
    char error[256] = "";
    uint64_t ids[DOCUMENT_GRAPH_SELECTION_MAX];
    document_graph_selection_request request = { .operation = DOCUMENT_SELECTION_NODE_FLAG,
                                                 .flag_kind = in[4].i,
                                                 .hidden = in[5].b };
    document_graph_selection_result result = { 0 };
    document_semantics *
        snapshot = document_semantics_service_snapshot(host, in[0].i, in[1].i, error, sizeof error);
    bool valid = snapshot && identity(in[2].str, &request.graph) &&
                 identities(in[3].ptr, &request, ids) &&
                 document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error);
    out[0].b = valid;
    out[1].str = copy(
        c->value_storage,
        *error  ? error
        : valid ? ""
                : "Invalid node flag request"
    );
    out[2].str = copy(c->value_storage, result.text);
    out[3].str = copy(c->value_storage, result.metadata);
    out[4].ptr = pin_array_snapshot_allocate(c->value_storage, PIN_STR, "const char *", 0);
    out[5].str = copy(c->value_storage, "0");
    document_graph_selection_result_free(&result);
    document_semantics_free(snapshot);
    return NULL;
}
#define WRAP(I)                                                                \
    static const char *run##I(                                                 \
        const behavior_function_context *context,                              \
        void *host,                                                            \
        const pin_value *in,                                                   \
        pin_value *out                                                         \
    )                                                                          \
    {                                                                          \
        return run(I, context, host, in, out);                                 \
    }
WRAP(0) WRAP(1) WRAP(2) WRAP(3) WRAP(4) WRAP(5) WRAP(6)
#define CALL(I)                                                                \
    { .service = "document_semantics",                                         \
      .optional_service = true,                                                \
      .missing_service = "Semantic document service is unavailable",           \
      .context = run##I }
const node_host_call DOCUMENT_GRAPH_SELECTION_CALLS[DOCUMENT_GRAPH_SELECTION_CAPABILITY_COUNT] = {
    CALL(0), CALL(1), CALL(2), CALL(3), CALL(4), CALL(5), CALL(6),
    { .service = "document_semantics", .optional_service = true, .missing_service = "Semantic document service is unavailable", .context = inspect },
    {.service="document_semantics",.optional_service=true,.missing_service="Semantic document service is unavailable",.context=flag}
};
const node_access DOCUMENT_GRAPH_SELECTION_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
