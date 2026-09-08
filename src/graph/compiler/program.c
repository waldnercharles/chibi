#include "graph/compiler/program.h"

#include <assert.h>

void graph_program_init(graph_program *program)
{
    assert(program);
    *program = (graph_program){ 0 };
    graph_ir_init(&program->ir);
}

void graph_program_free(graph_program *program)
{
    if (!program) {
        return;
    }
    graph_sljit_free(program->sljit);
    graph_ir_free(&program->ir);
    *program = (graph_program){ 0 };
}

int graph_program_compile_backend(graph_program *program, const char **reason)
{
    assert(program);
    graph_sljit_free(program->sljit);
    program->sljit = graph_sljit_compile(&program->ir, reason);
    return program->sljit ? 0 : -1;
}
