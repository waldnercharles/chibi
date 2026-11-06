#include "graph/modeling/model_internal.h"
#include "runtime/shaders/volume.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int owner, point;
    bool untagged;
} hit_value;
typedef struct
{
    compiler compiler;
    int fallback, root;
    int *remap;
} hit_builder;
static const model_hit_node *record(const hit_builder *b, const node *n)
{
    const struct model_bounds_program *p = b->compiler.program->bounds_program;
    for (int i = 0; i < acount(p->hit_nodes); i++) {
        if (p->hit_nodes[i].node == n->id) {
            return &p->hit_nodes[i];
        }
    }
    return NULL;
}
static int select_value(compiler *c, const node *n, int width, int no, int yes, int condition)
{
    if (no == yes) {
        return no;
    }
    return model_emit(c, n, SHADER_SELECT, width, no, yes, condition);
}
static hit_value lower_hit(hit_builder *b, const node *n)
{
    compiler *c = &b->compiler;
    hit_value fallback = { b->fallback, b->root, true };
    if (c->failed) {
        return fallback;
    }
    if (!model_enter(c, n)) {
        c->failed = true;
        return fallback;
    }
    hit_value result = fallback;
    int op = model_operation(n);
    if (op == MODEL_PART) {
        hit_value child = lower_hit(b, model_child(c->source, n, 0));
        int index = (int)n->reference_id - 1;
        const model_hit_node *own = record(b, n);
        if (index < 0 || index >= acount(c->program->parts) || !own) {
            c->failed = true;
            model_fail(c->error, n->id, "Part hit metadata is missing");
        } else if (child.owner == b->fallback) {
            result = (hit_value){ model_scalar(c, n, index),
                                  b->remap[own->position],
                                  false };
        } else if (!child.untagged) {
            result = child;
        } else {
            result.untagged = false;
            int named = model_binary(c, n, SHADER_STEP, 1, child.owner, model_scalar(c, n, 0));
            result
                .owner = select_value(c, n, 1, model_scalar(c, n, index), child.owner, named);
            result.point = select_value(c, n, 3, b->remap[own->position], child.point, named);
        }
    } else if ((op >= MODEL_TRANSLATE && op <= MODEL_REFLECT) || op == MODEL_OFFSET || op == MODEL_SHELL) {
        result = lower_hit(b, model_child(c->source, n, 0));
    } else if (op >= MODEL_UNION && op <= MODEL_SMOOTH_UNION) {
        const node *left = model_child(c->source, n, 0),
                   *right = model_child(c->source, n, 1);
        hit_value a = lower_hit(b, left);
        if (op == MODEL_DIFFERENCE) {
            result = a;
        } else {
            hit_value d = lower_hit(b, right);
            const model_hit_node *ar = record(b, left), *br = record(b, right);
            if (a.owner == b->fallback && d.owner == b->fallback) {
                result = fallback;
            } else if (!ar || !br) {
                c->failed = true;
                model_fail(c->error, n->id, "CSG hit metadata is missing");
            } else {
                result.untagged = a.untagged || d.untagged;
                int left_wins = model_binary(
                    c,
                    n,
                    SHADER_STEP,
                    1,
                    b->remap[op == MODEL_INTERSECTION ? ar->distance : br->distance],
                    b->remap[op == MODEL_INTERSECTION ? br->distance : ar->distance]
                );
                result.owner = select_value(c, n, 1, d.owner, a.owner, left_wins);
                result.point = select_value(c, n, 3, d.point, a.point, left_wins);
            }
        }
    }
    c->depth--;
    return result;
}
static void require_value(const shader_graph *graph, bool *used, int index)
{
    if (used[index]) {
        return;
    }
    used[index] = true;
    const shader_node *n = &graph->nodes[index];
    for (int i = 0; i < shader_op_inputs(n->op); i++) {
        require_value(graph, used, n->input[i]);
    }
}
static bool require_hit(hit_builder *b, const node *n, bool *used)
{
    int op = model_operation(n);
    const graph *source = b->compiler.source;
    if (op == MODEL_PART) {
        require_hit(b, model_child(source, n, 0), used);
        require_value(b->compiler.shader, used, record(b, n)->position);
        return true;
    }
    if ((op >= MODEL_TRANSLATE && op <= MODEL_REFLECT) || op == MODEL_OFFSET ||
        op == MODEL_SHELL) {
        return require_hit(b, model_child(source, n, 0), used);
    }
    if (op >= MODEL_UNION && op <= MODEL_SMOOTH_UNION) {
        const node *left = model_child(source, n, 0),
                   *right = model_child(source, n, 1);
        bool a = require_hit(b, left, used);
        if (op == MODEL_DIFFERENCE) {
            return a;
        }
        bool d = require_hit(b, right, used);
        if (a || d) {
            require_value(b->compiler.shader, used, record(b, left)->distance);
            require_value(b->compiler.shader, used, record(b, right)->distance);
        }
        return a || d;
    }
    return false;
}
static bool build_hit(model_program *p, const graph *source, const node *root, shader_error *error)
{
    struct model_bounds_program *bp = p->bounds_program;
    hit_builder builder = { .compiler = { .source = source,
                                          .shader = &p->hit_shader,
                                          .program = p,
                                          .error = error } };
    compiler *c = &builder.compiler;
    shader_graph_copy_images(&p->hit_shader, &p->shader);
    bool named = false;
    for (int i = 0; i < acount(p->parts); i++) {
        named |= p->parts[i].distance >= 0;
    }
    if (named) {
        shader_graph owned;
        shader_graph_init(&owned);
        if (!shader_graph_clone(&p->shader, &owned, error)) {
            return false;
        }
        int count = acount(p->shader.nodes);
        bool *used = xcalloc(count, sizeof *used);
        builder.remap = xmalloc(count * sizeof *builder.remap);
        c->shader = &p->shader;
        require_value(&p->shader, used, bp->root_position);
        require_hit(&builder, root, used);
        bp->hit_numeric_count = acount(bp->numeric.graph.nodes);
        for (int i = 0; i < count; i++) {
            used[i] |= i < bp->hit_numeric_count;
            builder.remap[i] = -1;
            if (!used[i]) {
                continue;
            }
            shader_node node = owned.nodes[i];
            owned.nodes[i].ramp = NULL;
            for (int j = 0; j < shader_op_inputs(node.op); j++) {
                node.input[j] = builder.remap[node.input[j]];
            }
            builder.remap[i] = acount(p->hit_shader.nodes);
            apush(p->hit_shader.nodes, node);
        }
        free(used);
        shader_graph_free(&owned);
        c->shader = &p->hit_shader;
        builder.root = builder.remap[bp->root_position];
    } else {
        builder.root = model_emit(c, root, SHADER_OBJECT_POSITION, 3, -1, -1, -1);
        bp->hit_numeric_count = 0;
    }
    int copied = acount(p->hit_shader.nodes);
    builder.fallback = model_scalar(c, root, -1);
    hit_value value = named ? lower_hit(&builder, root)
                            : (hit_value){ builder.fallback, builder.root, true };
    free(builder.remap);
    if (c->failed) {
        if (error && strstr(error->message, "1024")) {
            snprintf(
                error->message,
                sizeof error->message,
                "hit resolver 1024: march=%d numeric=%d copy=%d owner>=%d",
                acount(p->shader.nodes),
                bp->hit_numeric_count,
                copied,
                acount(p->hit_shader.nodes) - copied + 1
            );
        }
        return false;
    }
    p->hit_shader.output_count = 2;
    p->hit_shader.output[0] = value.owner;
    p->hit_shader.output[1] = value.point;
    return shader_graph_validate(&p->hit_shader, error);
}
bool model_build_hit(model_program *p, const graph *g, shader_error *error)
{
    if (!model_build_frames(p, error)) {
        return false;
    }
    const node *output = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (model_operation(g->nodes[i]) == MODEL_OUTPUT) {
            output = g->nodes[i];
        }
    }
    return build_hit(p, g, model_child(g, output, 0), error);
}
static bool prepare_part_shaders(
    const model_program *p,
    const graph *g,
    int part,
    shader_graph *distance,
    shader_graph *hit,
    shader_error *error
)
{
    if (part < 0 || part >= acount(p->parts) || p->parts[part].distance < 0) {
        return model_fail(error, 0, "Selected Part has no compiled geometry");
    }
    struct model_bounds_program bp = *p->bounds_program;
    const node *root = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (model_operation(g->nodes[i]) == MODEL_PART &&
            g->nodes[i]->reference_id == (uint64_t)part + 1) {
            root = g->nodes[i];
        }
    }
    if (!root) {
        return model_fail(error, 0, "Selected Part metadata is missing");
    }
    model_program temporary = { .shader = bp.proof,
                                .parts = p->parts,
                                .bounds_program = &bp };
    shader_graph_init(&temporary.hit_shader);
    if (!build_hit(&temporary, g, root, error)) {
        shader_graph_free(&temporary.hit_shader);
        return false;
    }
    shader_graph owned;
    shader_graph_init(&owned);
    if (!shader_graph_clone(&bp.proof, &owned, error)) {
        shader_graph_free(&temporary.hit_shader);
        return false;
    }
    shader_graph selected;
    shader_graph_init(&selected);
    shader_graph_copy_images(&selected, &bp.proof);
    int count = acount(bp.proof.nodes), *remap = xmalloc(count * sizeof *remap);
    bool *used = xcalloc(count, sizeof *used);
    require_value(&bp.proof, used, p->parts[part].distance);
    for (int i = 0; i < count; i++) {
        remap[i] = -1;
        if (!used[i]) {
            continue;
        }
        shader_node n = owned.nodes[i];
        owned.nodes[i].ramp = NULL;
        for (int j = 0; j < shader_op_inputs(n.op); j++) {
            n.input[j] = remap[n.input[j]];
        }
        remap[i] = acount(selected.nodes);
        apush(selected.nodes, n);
    }
    shader_graph_free(&owned);
    selected.output_count = 1;
    selected.output[0] = remap[p->parts[part].distance];
    free(used);
    free(remap);
    if (!shader_graph_validate(&selected, error)) {
        shader_graph_free(&selected);
        shader_graph_free(&temporary.hit_shader);
        return false;
    }
    shader_graph_free(distance);
    shader_graph_free(hit);
    *distance = selected;
    *hit = temporary.hit_shader;
    return true;
}

bool model_build_part_shaders(model_program *p, const graph *source, shader_error *error)
{
    for (int part = 0; part < acount(p->parts); part++) {
        shader_graph distance, hit;
        shader_graph_init(&distance);
        shader_graph_init(&hit);
        if (p->parts[part].distance >= 0 &&
            !prepare_part_shaders(p, source, part, &distance, &hit, error)) {
            shader_graph_free(&distance);
            shader_graph_free(&hit);
            return false;
        }
        apush(p->bounds_program->part_distance, distance);
        apush(p->bounds_program->part_hit, hit);
    }
    return true;
}
