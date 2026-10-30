#include "runtime/shaders/prepared.h"

#include "runtime/shaders/prepared_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bounded_count(execution_wire *w, int *count, int maximum)
{
    execution_wire_count(w, count, 1);
    if (!w->error && *count > maximum) {
        w->error = "Prepared shader table exceeds limit";
    }
}
void shader_graph_wire(execution_wire *w, shader_graph *g)
{
    int images = w->reading ? 0 : acount(g->images);
    bounded_count(w, &images, SHADER_NODE_MAX);
    for (int i = 0; i < images && !w->error; i++) {
        shader_image_source decoded = { 0 };
        shader_image_source *s = w->reading ? &decoded : &g->images[i];
        execution_wire_owned_string(w, &s->key);
        int color = s->color_space, filter = s->filter, wrap = s->wrap;
        execution_wire_int(w, &color);
        execution_wire_int(w, &filter);
        execution_wire_int(w, &wrap);
        if (!w->error && (!s->key || !*s->key || color < 0 || color > SHADER_IMAGE_LINEAR ||
                          filter < 0 || filter > SHADER_IMAGE_BILINEAR ||
                          wrap < 0 || wrap > SHADER_IMAGE_CLAMP)) {
            w->error = "Invalid prepared image source";
        }
        if (w->reading) {
            s->color_space = color;
            s->filter = filter;
            s->wrap = wrap;
            if (!w->error && execution_wire_reserve(w, sizeof *s)) {
                apush(g->images, decoded);
            } else {
                free(decoded.key);
            }
        }
    }
    int nodes = w->reading ? 0 : acount(g->nodes);
    bounded_count(w, &nodes, SHADER_NODE_MAX);
    for (int i = 0; i < nodes && !w->error; i++) {
        shader_node decoded = { 0 };
        shader_node *n = w->reading ? &decoded : &g->nodes[i];
        int op = n->op;
        execution_wire_int(w, &op);
        execution_wire_int(w, &n->width);
        for (int j = 0; j < 4; j++) {
            execution_wire_int(w, &n->input[j]);
        }
        for (int j = 0; j < 4; j++) {
            execution_wire_float(w, &n->value[j]);
        }
        execution_wire_int(w, &n->parameter);
        execution_wire_u64(w, &n->source_id);
        execution_wire_float(w, &n->volume_range);
        execution_wire_float(w, &n->volume_lipschitz);
        execution_wire_int(w, &n->image);
        int stops = w->reading ? 0 : acount(n->ramp);
        bounded_count(w, &stops, SHADER_RAMP_MAX);
        if (!w->error &&
            (op < 0 || op >= SHADER_OP_COUNT || n->width < 1 || n->width > 4)) {
            w->error = "Invalid prepared shader opcode or width";
        }
        for (int j = 0; j < 4 && !w->error; j++) {
            if (n->input[j] < -1 || n->input[j] >= i) {
                w->error = "Invalid prepared shader input bank";
            }
        }
        if (w->reading) {
            n->op = op;
        }
        for (int j = 0; j < stops && !w->error; j++) {
            shader_ramp_stop stop = { 0 };
            shader_ramp_stop *r = w->reading ? &stop : &n->ramp[j];
            execution_wire_float(w, &r->position);
            for (int k = 0; k < 4; k++) {
                execution_wire_float(w, &r->color[k]);
            }
            if (w->reading && !w->error) {
                execution_wire_reserve(w, sizeof stop);
                if (!w->error) {
                    apush(n->ramp, stop);
                }
            }
        }
        if (w->reading) {
            execution_wire_reserve(w, sizeof decoded);
            if (!w->error) {
                apush(g->nodes, decoded);
            } else {
                afree(decoded.ramp);
            }
        }
    }
    bounded_count(w, &g->output_count, SHADER_OUTPUT_MAX);
    for (int i = 0; i < g->output_count && !w->error; i++) {
        execution_wire_int(w, &g->output[i]);
    }
    if (!w->error) {
        shader_error error = { 0 };
        if ((acount(g->nodes) || g->output_count || acount(g->images)) &&
            !shader_graph_validate(g, &error)) {
            w->error = "Invalid prepared shader IR";
        }
    }
}
static void header(execution_wire *w)
{
    uint64_t magic = UINT64_C(0x3152494441485343), version = SHADER_PREPARED_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    if (!w->error && (magic != UINT64_C(0x3152494441485343) ||
                      version != SHADER_PREPARED_VERSION)) {
        w->error = "Unknown prepared shader format";
    }
}
static bool report(execution_wire *w, shader_error *error)
{
    if (!w->error) {
        return true;
    }
    if (error) {
        error->node = 0;
        snprintf(error->message, sizeof error->message, "%s", w->error);
    }
    return false;
}
bool shader_graph_prepared_write(const shader_graph *g, unsigned char **bytes, size_t *length, shader_error *error)
{
    assert(g && bytes && length);
    *bytes = NULL;
    *length = 0;
    if (!shader_graph_validate(g, error)) {
        return false;
    }
    execution_wire w = { 0 };
    header(&w);
    shader_graph view = *g;
    shader_graph_wire(&w, &view);
    if (!report(&w, error)) {
        afree(w.data);
        return false;
    }
    *bytes = xmalloc(w.size);
    memcpy(*bytes, w.data, w.size);
    *length = w.size;
    afree(w.data);
    return true;
}
bool shader_graph_prepared_read_budget(
    const void *bytes,
    size_t length,
    size_t *remaining,
    shader_graph *result,
    shader_error *error
)
{
    assert(remaining && result);
    size_t budget = *remaining < EXECUTION_WIRE_MAX_BYTES ? *remaining
                                                          : EXECUTION_WIRE_MAX_BYTES;
    arena storage = { 0 };
    execution_wire w = { .data = (unsigned char *)bytes,
                         .size = length,
                         .reading = true,
                         .storage = &storage,
                         .owned_bytes = EXECUTION_WIRE_MAX_BYTES - budget };
    shader_graph decoded;
    shader_graph_init(&decoded);
    header(&w);
    shader_graph_wire(&w, &decoded);
    if (!w.error && w.offset != w.size) {
        w.error = "Trailing prepared shader bytes";
    }
    if (!w.error && !shader_graph_validate(&decoded, error)) {
        w.error = "Invalid prepared shader IR";
    }
    arena_free(&storage);
    if (!report(&w, error)) {
        shader_graph_free(&decoded);
        return false;
    }
    size_t used = w.owned_bytes - (EXECUTION_WIRE_MAX_BYTES - budget);
    shader_graph_free(result);
    *result = decoded;
    *remaining -= used;
    return true;
}
bool shader_graph_prepared_read(const void *bytes, size_t length, shader_graph *result, shader_error *error)
{
    size_t remaining = EXECUTION_WIRE_MAX_BYTES;
    return shader_graph_prepared_read_budget(bytes, length, &remaining, result, error);
}
void shader_prepared_asset_init(shader_prepared_asset *a)
{
    *a = (shader_prepared_asset){ 0 };
}
void shader_prepared_asset_free(shader_prepared_asset *a)
{
    free(a->name);
    shader_graph_free(&a->graph);
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        free(a->names[i]);
    }
    shader_prepared_asset_init(a);
}
static bool asset_valid(const shader_prepared_asset *a, shader_error *error)
{
    if (!a->name || !*a->name || !shader_graph_validate(&a->graph, error)) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        if (!isfinite(a->bounds[i]) || a->bounds[i] < 0) {
            return false;
        }
    }
    if (a->shape && (!(a->bounds[0] > 0) || !(a->bounds[1] > 0))) {
        return false;
    }
    /* Only a material has an alpha mode; a shape keeps Blend's zero. */
    if (a->alpha_mode < SHADER_ALPHA_BLEND || a->alpha_mode > SHADER_ALPHA_MASK ||
        (a->shape && a->alpha_mode != SHADER_ALPHA_BLEND)) {
        return false;
    }
    if (a->graph.output_count == 3 &&
        (a->shape || !shader_graph_particle_validate(&a->graph, error) ||
         !shader_particle_parameters_validate(a->names, a->widths, error))) {
        return false;
    }
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        if (a->widths[i] < 0 || a->widths[i] > 4) {
            return false;
        }
        for (int j = 0; j < 4; j++) {
            if (!isfinite(a->defaults[i][j])) {
                return false;
            }
        }
    }
    for (int i = 0; i < acount(a->graph.nodes); i++) {
        const shader_node *n = &a->graph.nodes[i];
        if (n->op == SHADER_PARAMETER &&
            (a->widths[n->parameter] || a->graph.output_count == 3) &&
            a->widths[n->parameter] != n->width) {
            return false;
        }
    }
    return true;
}
static void asset_wire(execution_wire *w, shader_prepared_asset *a)
{
    uint64_t magic = UINT64_C(0x3154455353484343), version = SHADER_PREPARED_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    if (!w->error && (magic != UINT64_C(0x3154455353484343) ||
                      version != SHADER_PREPARED_VERSION)) {
        w->error = "Unknown prepared shader asset format";
    }
    execution_wire_owned_string(w, &a->name);
    execution_wire_bool(w, &a->shape);
    int alpha_mode = a->alpha_mode;
    execution_wire_int(w, &alpha_mode);
    if (w->reading) {
        a->alpha_mode = alpha_mode;
    }
    for (int i = 0; i < 2; i++) {
        execution_wire_float(w, &a->bounds[i]);
    }
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        execution_wire_owned_string(w, &a->names[i]);
        execution_wire_int(w, &a->widths[i]);
        for (int j = 0; j < 4; j++) {
            execution_wire_float(w, &a->defaults[i][j]);
        }
    }
    shader_graph_wire(w, &a->graph);
}
bool shader_asset_prepared_write(
    const shader_prepared_asset *a,
    unsigned char **bytes,
    size_t *length,
    shader_error *error
)
{
    assert(a && bytes && length);
    *bytes = NULL;
    *length = 0;
    if (!asset_valid(a, error)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Invalid prepared shader asset metadata");
        }
        return false;
    }
    execution_wire w = { 0 };
    shader_prepared_asset view = *a;
    asset_wire(&w, &view);
    if (!report(&w, error)) {
        afree(w.data);
        return false;
    }
    *bytes = xmalloc(w.size);
    memcpy(*bytes, w.data, w.size);
    *length = w.size;
    afree(w.data);
    return true;
}
bool shader_asset_prepared_read_budget(
    const void *bytes,
    size_t length,
    size_t *remaining,
    shader_prepared_asset *result,
    shader_error *error
)
{
    assert(remaining && result);
    size_t budget = *remaining < EXECUTION_WIRE_MAX_BYTES ? *remaining
                                                          : EXECUTION_WIRE_MAX_BYTES;
    arena storage = { 0 };
    execution_wire w = { .data = (unsigned char *)bytes,
                         .size = length,
                         .reading = true,
                         .storage = &storage,
                         .owned_bytes = EXECUTION_WIRE_MAX_BYTES - budget };
    shader_prepared_asset decoded;
    shader_prepared_asset_init(&decoded);
    asset_wire(&w, &decoded);
    if (!w.error && w.offset != w.size) {
        w.error = "Trailing prepared shader asset bytes";
    }
    if (!w.error && !asset_valid(&decoded, error)) {
        w.error = "Invalid prepared shader asset metadata";
    }
    arena_free(&storage);
    if (!report(&w, error)) {
        shader_prepared_asset_free(&decoded);
        return false;
    }
    size_t used = w.owned_bytes - (EXECUTION_WIRE_MAX_BYTES - budget);
    shader_prepared_asset_free(result);
    *result = decoded;
    *remaining -= used;
    return true;
}
