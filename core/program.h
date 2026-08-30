#ifndef CBLUEPRINT_PROGRAM_H
#define CBLUEPRINT_PROGRAM_H

#include "graph_ir.h"
#include "graph_jit_sljit.h"

/* Shared compiled storage. Domain adapters own graph lowering; this object
 * owns only the immutable IR and optional backend code. */
typedef struct graph_program
{
    graph_ir ir;
    graph_sljit_program *sljit;
} graph_program;

void graph_program_init(graph_program *program);
void graph_program_free(graph_program *program);
int graph_program_compile_backend(graph_program *program, const char **reason);
const graph_ir *graph_program_ir(const graph_program *program);
graph_sljit_program *graph_program_sljit(const graph_program *program);

#endif
