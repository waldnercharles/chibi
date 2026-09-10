#ifndef CHIBI_PROGRAM_H
#define CHIBI_PROGRAM_H

#include "graph/compiler/graph_ir.h"
#include "graph/compiler/graph_jit_sljit.h"

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

#endif
