#ifndef CBLUEPRINT_GRAPH_JIT_SJLIT_H
#define CBLUEPRINT_GRAPH_JIT_SJLIT_H

#include "graph/compiler/graph_ir.h"
#include "graph/compiler/jit_abi.h"

typedef struct graph_sljit_program graph_sljit_program;

typedef struct
{
    uint32_t abi_version;
    uint32_t entity_count;
    float delta_seconds;
    const graph_jit_column *columns;
    const uint32_t *slots;
    int column_count;
    const graph_jit_host *host;
} graph_sljit_batch_context;

typedef int (*graph_sljit_batch_entry_fn)(const graph_sljit_batch_context *context);

int graph_sljit_available();

graph_sljit_program *graph_sljit_compile(const graph_ir *ir, const char **reason);
void graph_sljit_free(graph_sljit_program *program);
graph_jit_entry_fn graph_sljit_entry(const graph_sljit_program *program);
graph_sljit_batch_entry_fn graph_sljit_batch_entry(const graph_sljit_program *program);
int graph_sljit_uses_registers(const graph_sljit_program *program);
int graph_sljit_uses_batch(const graph_sljit_program *program);
int graph_sljit_uses_simd(const graph_sljit_program *program);
int graph_sljit_column_count(const graph_sljit_program *program);
uint64_t graph_sljit_column_id_at(const graph_sljit_program *program, int index);

#endif
