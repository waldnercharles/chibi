#include "runtime/models/model_bounds.h"
#include "runtime/models/model_program_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool model_fail(shader_error *error, uint64_t id, const char *message)
{
    if (error) {
        error->node = id;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return false;
}
void model_program_init(model_program *program)
{
    *program = (model_program){ .bounds.empty = true };
    shader_graph_init(&program->shader);
    shader_graph_init(&program->hit_shader);
}
void model_program_free(model_program *program)
{
    shader_graph_free(&program->shader);
    shader_graph_free(&program->hit_shader);
    for (int i = 0; i < acount(program->dependencies); i++) {
        free(program->dependencies[i]);
    }
    afree(program->dependencies);
    for (int i = 0; i < acount(program->parts); i++) {
        free(program->parts[i].name);
        free(program->parts[i].path);
        free(program->parts[i].asset);
    }
    afree(program->parts);
    for (int i = 0; i < acount(program->anchors); i++) {
        free(program->anchors[i].asset);
        free(program->anchors[i].name);
        free(program->anchors[i].scope);
    }
    afree(program->anchors);
    model_material_bindings bindings = { .slots = program->material_slots,
                                         .dependencies = program->material_dependencies };
    model_material_bindings_free(&bindings);
    for (int i = 0; i < acount(program->parameters); i++) {
        model_parameter_free(&program->parameters[i]);
    }
    afree(program->parameters);
    model_bounds_program_free(program->bounds_program);
    model_program_init(program);
}
void model_parameter_free(model_parameter *p)
{
    free(p->asset);
    free(p->name);
    free(p->type_name);
    free(p->scope);
}
void model_numeric_init(model_numeric *n)
{
    *n = (model_numeric){ 0 };
    shader_graph_init(&n->graph);
}
void model_numeric_free(model_numeric *n)
{
    shader_graph_free(&n->graph);
    for (int i = 0; i < acount(n->parameters); i++) {
        model_parameter_free(&n->parameters[i]);
    }
    afree(n->parameters);
    for (int i = 0; i < acount(n->constraints); i++) {
        free(n->constraints[i].asset);
    }
    afree(n->constraints);
}
void model_material_slot_free(model_material_slot *s)
{
    free(s->asset);
    free(s->name);
    free(s->scope);
    free(s->material);
}
void model_material_bindings_init(model_material_bindings *b)
{
    *b = (model_material_bindings){ 0 };
}
void model_material_bindings_free(model_material_bindings *b)
{
    for (int i = 0; i < acount(b->slots); i++) {
        model_material_slot_free(&b->slots[i]);
    }
    afree(b->slots);
    afree(b->part_slots);
    for (int i = 0; i < acount(b->dependencies); i++) {
        free(b->dependencies[i]);
    }
    afree(b->dependencies);
    model_material_bindings_init(b);
}

void model_bounds_program_free(struct model_bounds_program *p)
{
    if (!p) {
        return;
    }
    model_bounds_plan_free(p->plan);
    model_numeric_free(&p->numeric);
    afree(p->inputs);
    for (int i = 0; i < acount(p->sources); i++) {
        free(p->sources[i].asset);
        free(p->sources[i].path);
    }
    afree(p->sources);
    afree(p->rotations);
    shader_graph_free(&p->proof);
    shader_graph_free(&p->hit_proof);
    afree(p->hit_nodes);
    for (int i = 0; i < acount(p->part_frames); i++) {
        shader_graph_free(&p->part_frames[i]);
    }
    afree(p->part_frames);
    for (int i = 0; i < acount(p->part_distance); i++) {
        shader_graph_free(&p->part_distance[i]);
    }
    afree(p->part_distance);
    for (int i = 0; i < acount(p->part_hit); i++) {
        shader_graph_free(&p->part_hit[i]);
    }
    afree(p->part_hit);
    free(p);
}
