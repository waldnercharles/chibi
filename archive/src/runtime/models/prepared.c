#include "runtime/models/prepared.h"

#include "runtime/models/model_bounds.h"
#include "runtime/shaders/prepared_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void count(execution_wire *w, int *n, int limit)
{
    execution_wire_count(w, n, 1);
    if (!w->error && *n > limit) {
        w->error = "Prepared model table exceeds limit";
    }
}
static void vector(execution_wire *w, float *v, int n)
{
    for (int i = 0; i < n && !w->error; i++) {
        execution_wire_float(w, &v[i]);
    }
}
static void strings(execution_wire *w, char ***items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(char *))) {
                break;
            }
            apush(*items, NULL);
        }
        execution_wire_owned_string(w, &(*items)[i]);
        if (!w->error && !(*items)[i]) {
            w->error = "Absent model resource name";
        }
    }
}
static void parameter(execution_wire *w, model_parameter *v)
{
    execution_wire_int(w, &v->slot);
    execution_wire_int(w, &v->width);
    execution_wire_u64(w, &v->node);
    execution_wire_owned_string(w, &v->asset);
    execution_wire_owned_string(w, &v->name);
    execution_wire_owned_string(w, &v->type_name);
    execution_wire_owned_string(w, &v->scope);
    int type = v->type;
    execution_wire_int(w, &type);
    if (w->reading) {
        v->type = type;
    }
    vector(w, v->defaults, 4);
    vector(w, v->minimum, 4);
    vector(w, v->maximum, 4);
    execution_wire_bool(w, &v->hidden);
}
static void parameter_array(execution_wire *w, model_parameter **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_PARAMETER_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_parameter))) {
                break;
            }
            model_parameter value = { 0 };
            apush(*items, value);
        }
        parameter(w, &(*items)[i]);
    }
}
static void part(execution_wire *w, model_part *v)
{
    execution_wire_u64(w, &v->node);
    execution_wire_owned_string(w, &v->name);
    execution_wire_owned_string(w, &v->path);
    execution_wire_owned_string(w, &v->asset);
    execution_wire_int(w, &v->distance);
    execution_wire_int(w, &v->position);
    execution_wire_int(w, &v->material);
}
static void part_array(execution_wire *w, model_part **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_part))) {
                break;
            }
            model_part value = { 0 };
            apush(*items, value);
        }
        part(w, &(*items)[i]);
    }
}
static void anchor(execution_wire *w, model_anchor *v)
{
    execution_wire_u64(w, &v->node);
    execution_wire_owned_string(w, &v->asset);
    execution_wire_owned_string(w, &v->name);
    execution_wire_owned_string(w, &v->scope);
    execution_wire_int(w, &v->position);
    execution_wire_int(w, &v->angles);
}
static void anchor_array(execution_wire *w, model_anchor **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_anchor))) {
                break;
            }
            model_anchor value = { 0 };
            apush(*items, value);
        }
        anchor(w, &(*items)[i]);
    }
}
static void material(execution_wire *w, model_material_slot *v)
{
    execution_wire_int(w, &v->slot);
    execution_wire_u64(w, &v->node);
    execution_wire_owned_string(w, &v->asset);
    execution_wire_owned_string(w, &v->name);
    execution_wire_owned_string(w, &v->scope);
    execution_wire_owned_string(w, &v->material);
}
static void material_array(execution_wire *w, model_material_slot **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_material_slot))) {
                break;
            }
            model_material_slot value = { 0 };
            apush(*items, value);
        }
        material(w, &(*items)[i]);
    }
}
static void constraint(execution_wire *w, model_numeric_constraint *v)
{
    execution_wire_u64(w, &v->node);
    execution_wire_int(w, &v->expression);
    execution_wire_int(w, &v->width);
    vector(w, v->minimum, 4);
    vector(w, v->maximum, 4);
    execution_wire_owned_string(w, &v->asset);
}
static void constraint_array(execution_wire *w, model_numeric_constraint **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_numeric_constraint))) {
                break;
            }
            model_numeric_constraint value = { 0 };
            apush(*items, value);
        }
        constraint(w, &(*items)[i]);
    }
}
static void input(execution_wire *w, model_numeric_input *v)
{
    execution_wire_u64(w, &v->node);
    execution_wire_int(w, &v->pin);
    execution_wire_int(w, &v->expression);
}
static void input_array(execution_wire *w, model_numeric_input **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_numeric_input))) {
                break;
            }
            model_numeric_input value = { 0 };
            apush(*items, value);
        }
        input(w, &(*items)[i]);
    }
}
static void source(execution_wire *w, model_source *v)
{
    execution_wire_u64(w, &v->generated);
    execution_wire_u64(w, &v->authored);
    execution_wire_owned_string(w, &v->asset);
    execution_wire_owned_string(w, &v->path);
}
static void source_array(execution_wire *w, model_source **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_source))) {
                break;
            }
            model_source value = { 0 };
            apush(*items, value);
        }
        source(w, &(*items)[i]);
    }
}
static void rotation(execution_wire *w, model_rotation_uniform *v)
{
    execution_wire_u64(w, &v->node);
    execution_wire_int(w, &v->expression);
    execution_wire_int(w, &v->slot);
}
static void rotation_array(execution_wire *w, model_rotation_uniform **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_rotation_uniform))) {
                break;
            }
            model_rotation_uniform value = { 0 };
            apush(*items, value);
        }
        rotation(w, &(*items)[i]);
    }
}
static void hit(execution_wire *w, model_hit_node *v)
{
    execution_wire_u64(w, &v->node);
    execution_wire_int(w, &v->distance);
    execution_wire_int(w, &v->position);
}
static void hit_array(execution_wire *w, model_hit_node **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_hit_node))) {
                break;
            }
            model_hit_node value = { 0 };
            apush(*items, value);
        }
        hit(w, &(*items)[i]);
    }
}
static void graphs(execution_wire *w, shader_graph **items)
{
    int n = w->reading ? 0 : acount(*items);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(shader_graph))) {
                break;
            }
            shader_graph value = { 0 };
            apush(*items, value);
        }
        shader_graph_wire(w, &(*items)[i]);
    }
}
static void plan(execution_wire *w, model_bounds_plan *p)
{
    strings(w, &p->resources);
    execution_wire_int(w, &p->root);
    int n = w->reading ? 0 : acount(p->nodes);
    count(w, &n, SHADER_NODE_MAX);
    for (int i = 0; i < n && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(model_bound_node))) {
                break;
            }
            model_bound_node value = { 0 };
            apush(p->nodes, value);
        }
        model_bound_node *v = &p->nodes[i];
        execution_wire_u64(w, &v->id);
        execution_wire_u64(w, &v->part);
        int op = v->op;
        execution_wire_int(w, &op);
        if (w->reading) {
            v->op = op;
        }
        count(w, &v->input_count, MODEL_BOUND_INPUT_MAX);
        count(w, &v->point_count, MODEL_BOUND_POINT_MAX);
        for (int j = 0; j < v->input_count && !w->error; j++) {
            model_bound_input *a = &v->inputs[j];
            execution_wire_int(w, &a->child);
            execution_wire_int(w, &a->expression);
            execution_wire_int(w, &a->resource);
            execution_wire_int(w, &a->width);
            int type = a->type;
            execution_wire_int(w, &type);
            if (w->reading) {
                a->type = type;
            }
            vector(w, a->value, 4);
            execution_wire_int(w, &a->choice);
        }
        for (int j = 0; j < v->point_count && !w->error; j++) {
            vector(w, v->points[j], 3);
        }
    }
}
static void program_wire(execution_wire *w, model_program *p)
{
    shader_graph_wire(w, &p->shader);
    shader_graph_wire(w, &p->hit_shader);
    vector(w, p->bounds.min, 3);
    vector(w, p->bounds.max, 3);
    execution_wire_bool(w, &p->bounds.empty);
    strings(w, &p->dependencies);
    strings(w, &p->material_dependencies);
    part_array(w, &p->parts);
    anchor_array(w, &p->anchors);
    material_array(w, &p->material_slots);
    parameter_array(w, &p->parameters);
    if (w->error) {
        return;
    }
    if (w->reading) {
        p->bounds_program = execution_wire_allocate_owned(w, sizeof *p->bounds_program);
        if (w->error) {
            return;
        }
        p->bounds_program
            ->plan = execution_wire_allocate_owned(w, sizeof *p->bounds_program->plan);
        if (w->error) {
            return;
        }
    }
    struct model_bounds_program *b = p->bounds_program;
    if (!b || !b->plan) {
        w->error = "Model has no compiled construction plan";
        return;
    }
    plan(w, b->plan);
    shader_graph_wire(w, &b->numeric.graph);
    parameter_array(w, &b->numeric.parameters);
    constraint_array(w, &b->numeric.constraints);
    input_array(w, &b->inputs);
    source_array(w, &b->sources);
    rotation_array(w, &b->rotations);
    shader_graph_wire(w, &b->proof);
    shader_graph_wire(w, &b->hit_proof);
    hit_array(w, &b->hit_nodes);
    execution_wire_int(w, &b->root_position);
    execution_wire_int(w, &b->hit_numeric_count);
    graphs(w, &b->part_frames);
    graphs(w, &b->part_distance);
    graphs(w, &b->part_hit);
}
static bool root(const shader_graph *g, int index, int width)
{
    return index >= 0 && index < acount(g->nodes) && g->nodes[index].width == width;
}
static bool optional_root(const shader_graph *g, int index, int width)
{
    return index == -1 || root(g, index, width);
}
static bool parameters_valid(const model_parameter *items)
{
    for (int i = 0; i < acount(items); i++) {
        const model_parameter *p = &items[i];
        if (p->slot < 0 || p->slot >= SHADER_PARAMETER_MAX || p->width < 1 ||
            p->width > 4 || !p->asset || !p->name || !p->scope ||
            p->type < PIN_BOOL || p->type > PIN_ENUM) {
            return false;
        }
        for (int j = 0; j < p->width; j++) {
            if (!isfinite(p->defaults[j]) || !isfinite(p->minimum[j]) ||
                !isfinite(p->maximum[j]) || p->minimum[j] > p->maximum[j] ||
                (!p->hidden && (p->defaults[j] < p->minimum[j] ||
                                p->defaults[j] > p->maximum[j]))) {
                return false;
            }
        }
        for (int j = 0; j < i; j++) {
            if (items[j].slot == p->slot) {
                return false;
            }
        }
    }
    return true;
}
static bool graph_valid(const shader_graph *g, bool optional, shader_error *error)
{
    if (!acount(g->nodes) && optional && !g->output_count && !acount(g->images)) {
        return true;
    }
    if (!shader_graph_validate(g, error)) {
        return false;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        for (int j = 0; j < 4; j++) {
            if (g->nodes[i].input[j] < -1 || g->nodes[i].input[j] >= i) {
                return model_fail(error, g->nodes[i].source_id, "Invalid prepared shader input bank");
            }
        }
    }
    return true;
}
static bool valid(const model_program *p, shader_error *error)
{
    const struct model_bounds_program *b = p->bounds_program;
    if (!b || !b->plan || !parameters_valid(p->parameters) ||
        !parameters_valid(b->numeric.parameters)) {
        return model_fail(error, 0, "Invalid prepared model parameter or construction metadata");
    }
    if (!graph_valid(&p->shader, true, error) ||
        !graph_valid(&p->hit_shader, false, error) ||
        !graph_valid(&b->numeric.graph, true, error) ||
        !graph_valid(&b->proof, false, error) ||
        !graph_valid(&b->hit_proof, false, error)) {
        return false;
    }
    for (int i = 0; i < acount(b->part_frames); i++) {
        if (!graph_valid(&b->part_frames[i], true, error)) {
            return false;
        }
    }
    for (int i = 0; i < acount(b->part_distance); i++) {
        if (!graph_valid(&b->part_distance[i], true, error)) {
            return false;
        }
    }
    for (int i = 0; i < acount(b->part_hit); i++) {
        if (!graph_valid(&b->part_hit[i], true, error)) {
            return false;
        }
    }
    int parts = acount(p->parts), numeric = acount(b->numeric.graph.nodes);
    if (parts > SHADER_NODE_MAX || acount(b->part_frames) != parts ||
        acount(b->part_distance) != parts || acount(b->part_hit) != parts ||
        b->hit_numeric_count < 0 || b->hit_numeric_count > numeric ||
        b->hit_numeric_count > acount(b->hit_proof.nodes) ||
        b->hit_proof.output_count != 2 ||
        !root(&b->hit_proof, b->hit_proof.output[0], 1) ||
        !root(&b->hit_proof, b->hit_proof.output[1], 3) ||
        !optional_root(&b->proof, b->root_position, 3)) {
        return model_fail(error, 0, "Invalid prepared model part or hit banks");
    }
    for (int i = 0; i < parts; i++) {
        const model_part *v = &p->parts[i];
        if (!v->name || !v->path || !v->asset || v->material < -1 ||
            v->material >= acount(p->material_slots) ||
            !optional_root(&b->proof, v->distance, 1) ||
            !optional_root(&b->proof, v->position, 3) ||
            (v->distance >= 0 && (b->part_distance[i].output_count != 1 ||
                                  b->part_hit[i].output_count != 2)) ||
            (b->part_frames[i].output_count &&
             (b->part_frames[i].output_count != 1 ||
              !root(&b->part_frames[i], b->part_frames[i].output[0], 3)))) {
            return model_fail(error, v->node, "Invalid prepared model part roots");
        }
    }
    for (int i = 0; i < acount(p->anchors); i++) {
        const model_anchor *a = &p->anchors[i];
        if (!a->asset || !a->name || !a->scope ||
            !root(&b->numeric.graph, a->position, 3) ||
            !root(&b->numeric.graph, a->angles, 3)) {
            return model_fail(error, a->node, "Invalid prepared anchor");
        }
    }
    for (int i = 0; i < acount(p->material_slots); i++) {
        const model_material_slot *v = &p->material_slots[i];
        if (v->slot != i || !v->asset || !v->name || !v->scope ||
            !v->material || i >= MODEL_MATERIAL_SLOT_MAX) {
            return model_fail(error, v->node, "Invalid prepared material slot");
        }
    }
    for (int i = 0; i < acount(b->numeric.constraints); i++) {
        const model_numeric_constraint *v = &b->numeric.constraints[i];
        if (!v->asset || v->width < 1 || v->width > 4 ||
            !root(&b->numeric.graph, v->expression, v->width)) {
            return model_fail(error, v->node, "Invalid prepared numeric constraint");
        }
        for (int j = 0; j < v->width; j++) {
            if (!isfinite(v->minimum[j]) || !isfinite(v->maximum[j]) ||
                v->minimum[j] > v->maximum[j]) {
                return model_fail(error, v->node, "Invalid constraint range");
            }
        }
    }
    for (int i = 0; i < acount(b->rotations); i++) {
        const model_rotation_uniform *v = &b->rotations[i];
        if (!root(&b->numeric.graph, v->expression, 3) || v->slot < 0 ||
            v->slot > SHADER_PARAMETER_MAX - 3) {
            return model_fail(error, v->node, "Invalid prepared rotation bank");
        }
    }
    for (int i = 0; i < acount(b->inputs); i++) {
        const model_numeric_input *v = &b->inputs[i];
        if (v->pin < 0 || v->pin >= MODEL_BOUND_INPUT_MAX ||
            v->expression < 0 || v->expression >= numeric) {
            return model_fail(error, v->node, "Invalid prepared numeric input");
        }
    }
    for (int i = 0; i < acount(b->hit_nodes); i++) {
        const model_hit_node *v = &b->hit_nodes[i];
        if (!root(&b->proof, v->distance, 1) || !root(&b->proof, v->position, 3)) {
            return model_fail(error, v->node, "Invalid prepared hit root");
        }
    }
    const model_bounds_plan *plan = b->plan;
    if (plan->root < 0 || plan->root >= acount(plan->nodes)) {
        return model_fail(error, 0, "Invalid construction root");
    }
    for (int i = 0; i < acount(plan->nodes); i++) {
        const model_bound_node *n = &plan->nodes[i];
        if (n->input_count < 0 || n->input_count > MODEL_BOUND_INPUT_MAX ||
            n->point_count < 0 || n->point_count > MODEL_BOUND_POINT_MAX ||
            n->op < MODEL_SPHERE || n->op > MODEL_SAMPLED_SDF ||
            n->part > (uint64_t)parts) {
            return model_fail(error, n->id, "Invalid construction row");
        }
        for (int j = 0; j < n->input_count; j++) {
            const model_bound_input *v = &n->inputs[j];
            if (v->child < -1 || v->child >= acount(plan->nodes) ||
                v->width < 0 || v->width > 4 || v->expression < -1 ||
                (v->expression >= 0 && !root(&b->numeric.graph, v->expression, v->width)) ||
                v->resource < -1 || v->resource >= acount(plan->resources) ||
                v->type < PIN_FLOW || v->type > PIN_ENUM) {
                return model_fail(error, n->id, "Invalid construction input");
            }
        }
        for (int j = 0; j < n->point_count; j++) {
            for (int k = 0; k < 3; k++) {
                if (!isfinite(n->points[j][k])) {
                    return model_fail(error, n->id, "Invalid construction point");
                }
            }
        }
    }
    float payload[SHADER_PARAMETER_MAX][4];
    model_bounds bounds;
    return model_program_prepare(p, NULL, payload, &bounds, error);
}
static void header(execution_wire *w)
{
    uint64_t magic = UINT64_C(0x314c444f4d484343), version = MODEL_PREPARED_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    if (!w->error && (magic != UINT64_C(0x314c444f4d484343) ||
                      version != MODEL_PREPARED_VERSION)) {
        w->error = "Unknown prepared model format";
    }
}
static bool report(execution_wire *w, shader_error *error)
{
    if (!w->error) {
        return true;
    }
    return model_fail(error, 0, w->error);
}
bool model_program_prepared_write(
    const model_program *p,
    unsigned char **bytes,
    size_t *length,
    shader_error *error
)
{
    assert(p && bytes && length);
    *bytes = NULL;
    *length = 0;
    if (!valid(p, error)) {
        return false;
    }
    execution_wire w = { 0 };
    header(&w);
    model_program view = *p;
    program_wire(&w, &view);
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
bool model_program_prepared_read_budget(
    const void *bytes,
    size_t length,
    size_t *remaining,
    model_program *result,
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
    model_program decoded;
    model_program_init(&decoded);
    header(&w);
    program_wire(&w, &decoded);
    if (!w.error && w.offset != w.size) {
        w.error = "Trailing prepared model bytes";
    }
    bool okay = report(&w, error) && valid(&decoded, error);
    arena_free(&storage);
    if (!okay) {
        model_program_free(&decoded);
        return false;
    }
    size_t used = w.owned_bytes - (EXECUTION_WIRE_MAX_BYTES - budget);
    model_program_free(result);
    *result = decoded;
    *remaining -= used;
    return true;
}
bool model_program_prepared_read(const void *bytes, size_t length, model_program *result, shader_error *error)
{
    size_t remaining = EXECUTION_WIRE_MAX_BYTES;
    return model_program_prepared_read_budget(bytes, length, &remaining, result, error);
}
