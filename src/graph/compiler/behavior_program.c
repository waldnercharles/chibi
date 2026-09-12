#include "graph/compiler/behavior_program.h"

#include "graph/compiler/behavior_expression.h"
#include "graph/compiler/behavior_jit.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/model/registry.h"
#include "graph/runtime/behavior_data.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/runtime.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*data_entry)(behavior_data_context *);
typedef int (*flow_entry)(flow_context *);

typedef struct
{
    int *inputs;
    int *outputs;
    node **targets;
    int *target_pins;
    void *flow_code;
    int control;
} compiled_node;

struct behavior_graph_program
{
    graph *source;
    graph_ir ir;
    compiled_node *nodes; /* indexed by stable authored node ID */
    void **code;          /* one native entry per data block */
    void **expressions;   /* helper-free numeric regions, when eligible */
};

struct behavior_program
{
    behavior_backend backend;
    unsigned long long revision;
    compile_diagnostic diagnostic;
    behavior_graph_program *graphs;
};

const char *behavior_backend_name(behavior_backend backend)
{
    switch (backend) {
        case BEHAVIOR_SLJIT: return "SLJIT";
        case BEHAVIOR_BYTECODE: return "BYTECODE";
        case BEHAVIOR_ORACLE: return "ORACLE";
        case BEHAVIOR_UNAVAILABLE: return "UNAVAILABLE";
    }
    return "UNAVAILABLE";
}

static void diagnostic(behavior_program *p, graph *g, node *n, int pin, const char *message)
{
    p->backend = BEHAVIOR_UNAVAILABLE;
    p->diagnostic = (compile_diagnostic){
        .severity = CMPL_ERROR,
        .rule = CMPL_RULE_BACKEND,
        .graph_id = g ? g->graph_id : 0,
        .node_id = n ? n->id : 0,
        .pin_id = n && pin >= 0 ? n->input_pins[pin].id : 0,
    };
    snprintf(p->diagnostic.message, sizeof p->diagnostic.message, "%s", message);
}

static int emit(behavior_graph_program *p, graph_ir_behavior_instruction instruction)
{
    apush(p->ir.behavior, instruction);
    return acount(p->ir.behavior) - 1;
}

static int lower_node(behavior_graph_program *p, node *n, int *visited);

static int lower_input(behavior_graph_program *p, node *n, int pin, int *visited)
{
    node *source = NULL;
    int source_pin = -1;
    if (!graph_input_source(p->source, n, pin, &source, &source_pin)) {
        emit(p, (graph_ir_behavior_instruction){ .op = GRAPH_IR_BIND_INPUT, .node = n, .pin = pin });
        return 0;
    }
    if (lower_node(p, source, visited)) {
        return -1;
    }
    int aggregate = source_pin;
    const type_field *field = NULL;
    if (source->output_pins[source_pin].field_source) {
        field = graph_output_field(source, source_pin, &aggregate);
        if (!field) {
            return -1;
        }
    }
    const node_desc *conversion = graph_input_conversion(p->source, n, pin);
    if (conversion && !conversion->fn) {
        return -1;
    }
    emit(
        p,
        (graph_ir_behavior_instruction){
            .op = GRAPH_IR_READ_SLOT,
            .node = n,
            .pin = pin,
            .source = source,
            .source_pin = aggregate,
            .target = source_pin,
            .field = field,
            .conversion = conversion ? conversion->fn : NULL,
        }
    );
    return 0;
}

static int lower_node(behavior_graph_program *p, node *n, int *visited)
{
    if (visited[n->id] == 1) {
        return -1;
    }
    if (visited[n->id] == 2) {
        return 0;
    }
    visited[n->id] = 1;
    int guard = emit(p, (graph_ir_behavior_instruction){ .op = GRAPH_IR_MEMO_BRANCH, .node = n });
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind == PIN_DATA && lower_input(p, n, i, visited)) {
            return -1;
        }
    }
    emit(p, (graph_ir_behavior_instruction){ .op = GRAPH_IR_EVALUATE, .node = n });
    p->ir.behavior[guard].target = acount(p->ir.behavior);
    visited[n->id] = 2;
    return 0;
}

static int lower_block(behavior_graph_program *p, node *n, int pin, bool output)
{
    int *visited = xcalloc((size_t)acount(p->nodes), sizeof *visited);
    int first = acount(p->ir.behavior);
    int result = output ? lower_node(p, n, visited) : lower_input(p, n, pin, visited);
    free(visited);
    if (result) {
        return -1;
    }
    apush(
        p->ir.blocks,
        ((graph_ir_block){
            .first = first,
            .count = acount(p->ir.behavior) - first,
            .output = output,
            .graph_id = p->source->graph_id,
            .node_id = n->id,
            .pin_id = output ? n->output_pins[pin].id : n->input_pins[pin].id,
        })
    );
    return acount(p->ir.blocks) - 1;
}

static graph_ir_control lower_control(graph *g, node *n)
{
    graph_ir_control_op op = GRAPH_IR_HOST_EFFECT;
    switch (n->desc->flow_kind) {
        case NODE_FLOW_EVENT: op = GRAPH_IR_ENTRY; break;
        case NODE_FLOW_BRANCH:
            op = n->desc == &NODE_Branch ? GRAPH_IR_BRANCH : GRAPH_IR_HOST_EFFECT;
            break;
        case NODE_FLOW_SEQUENCE: op = GRAPH_IR_SEQUENCE; break;
        case NODE_FLOW_FOR_LOOP: op = GRAPH_IR_LOOP; break;
        case NODE_FLOW_DELAY: op = GRAPH_IR_YIELD; break;
        default: break;
    }
    if (n->desc == &NODE_FunctionEntry) {
        op = GRAPH_IR_ENTRY;
    }
    if (n->desc == &NODE_Return) {
        op = GRAPH_IR_FUNCTION_RETURN;
    }
    if (n->desc == &NODE_CallFunction || n->desc == &NODE_CallMacro) {
        op = GRAPH_IR_FUNCTION_CALL;
    }
    if (n->desc == &NODE_WhileLoop) {
        op = GRAPH_IR_WHILE;
    }
    if (n->desc == &NODE_ForEach || n->desc == &NODE_ForEachWithBreak) {
        op = GRAPH_IR_FOREACH;
    }
    if (n->desc == &NODE_Break) {
        op = GRAPH_IR_BREAK;
    }
    if (n->desc->latent) {
        op = GRAPH_IR_YIELD;
    }
    return (graph_ir_control){ .op = op,
                               .node = n,
                               .host = n->desc->flow,
                               .graph_id = g->graph_id,
                               .callee_id = n->reference_id,
                               .effects = n->desc->access ? n->desc->access->flags : 0 };
}

void behavior_program_free(behavior_program *program)
{
    if (!program) {
        return;
    }
    for (int i = 0; i < acount(program->graphs); i++) {
        behavior_graph_program *p = &program->graphs[i];
        for (int j = 0; j < acount(p->nodes); j++) {
            compiled_node *n = &p->nodes[j];
            afree(n->inputs);
            afree(n->outputs);
            afree(n->targets);
            afree(n->target_pins);
            behavior_jit_free(n->flow_code);
        }
        for (int j = 0; j < acount(p->code); j++) {
            behavior_jit_free(p->code[j]);
        }
        for (int j = 0; j < acount(p->expressions); j++) {
            behavior_jit_free(p->expressions[j]);
        }
        afree(p->expressions);
        afree(p->nodes);
        afree(p->code);
        graph_ir_free(&p->ir);
    }
    afree(program->graphs);
    free(program);
}

static bool lower_graph(behavior_program *program, behavior_graph_program *p)
{
    uint64_t maximum = 0;
    for (int i = 0; i < acount(p->source->nodes); i++) {
        node *n = p->source->nodes[i];
        if (n->id > maximum) {
            maximum = n->id;
        }
    }
    asetlen(p->nodes, (int)maximum + 1);
    memset(p->nodes, 0, (size_t)acount(p->nodes) * sizeof *p->nodes);
    for (int i = 0; i < acount(p->source->nodes); i++) {
        node *n = p->source->nodes[i];
        compiled_node *compiled = &p->nodes[n->id];
        compiled->control = acount(p->ir.control);
        apush(p->ir.control, lower_control(p->source, n));
        for (int j = 0; j < n->input_count; j++) {
            int block = n->input_pins[j].kind == PIN_DATA
                          ? lower_block(p, n, j, false)
                          : -1;
            if (block < 0 && n->input_pins[j].kind == PIN_DATA) {
                diagnostic(program, p->source, n, j, "cannot lower behavior data dependency");
                return false;
            }
            apush(compiled->inputs, block);
            graph_ir_add_value(
                &p->ir,
                (graph_ir_value){
                    .type = n->input_pins[j].type,
                    .container = n->input_pins[j].container,
                    .type_name = n->input_pins[j].type_name,
                    .size = sizeof(pin_value),
                    .alignment = _Alignof(pin_value),
                    .kind = n->input_pins[j].container != PIN_CONTAINER_SINGLE ||
                                    n->input_pins[j].type == PIN_STRUCT ||
                                    n->input_pins[j].type == PIN_STR ||
                                    n->input_pins[j].type == PIN_NAME
                              ? GRAPH_IR_VALUE_MANAGED
                              : GRAPH_IR_VALUE_FIXED }
            );
        }
        for (int j = 0; j < n->output_count; j++) {
            int block = n->output_pins[j].kind == PIN_DATA
                          ? lower_block(p, n, j, true)
                          : -1;
            if (block < 0 && n->output_pins[j].kind == PIN_DATA) {
                diagnostic(program, p->source, n, -1, "cannot lower behavior output dependency");
                return false;
            }
            apush(compiled->outputs, block);
            node *target = NULL;
            int pin = -1;
            if (n->output_pins[j].kind == PIN_EXEC) {
                const edge *e = NULL;
                int count = graph_exec_output_edges(p->source, n, j, &e);
                if (count > 1) {
                    diagnostic(program, p->source, n, -1, "ambiguous compiled continuation");
                    return false;
                }
                if (count) {
                    target = graph_node_by_id(p->source, e->to_id);
                    int output;
                    if (!target ||
                        !graph_pin_by_id(target, e->to_pin_id, &output, &pin) ||
                        output) {
                        diagnostic(program, p->source, n, -1, "invalid compiled continuation");
                        return false;
                    }
                }
            }
            apush(compiled->targets, target);
            apush(compiled->target_pins, pin);
        }
    }
    return true;
}

uint64_t behavior_asset_revision(const behavior_asset *asset)
{
    uint64_t revision = UINT64_C(1469598103934665603);
    for (int i = 0; i < acount(asset->graphs); i++) {
        revision = (revision ^ asset->graphs[i]->lifetime_id) * UINT64_C(1099511628211);
        revision = (revision ^ graph_topology_revision(asset->graphs[i])) *
                   UINT64_C(1099511628211);
    }
    return revision;
}

behavior_program *behavior_program_publish(behavior_asset *asset)
{
    uint64_t revision = behavior_asset_revision(asset);
    if (asset->program && asset->program->revision == revision) {
        return asset->program;
    }
    behavior_program_free(asset->program);
    behavior_program *program = xcalloc(1, sizeof *program);
    asset->program = program;
    program->revision = revision;
    compile_report report;
    compile_report_init(&report);
    behavior_asset_compile(asset, &report);
    if (compile_report_blocked(&report)) {
        program->diagnostic = *compile_report_first_error(&report);
        compile_report_free(&report);
        return program;
    }
    compile_report_free(&report);
#ifdef __EMSCRIPTEN__
    program->backend = BEHAVIOR_BYTECODE;
#else
    program->backend = BEHAVIOR_SLJIT;
    if (!graph_sljit_available()) {
        diagnostic(program, NULL, NULL, -1, "native behavior publication requires SLJIT; interpreter fallback is disabled");
        return program;
    }
#endif
    for (int i = 0; i < acount(asset->graphs); i++) {
        apush(program->graphs, ((behavior_graph_program){ .source = asset->graphs[i] }));
        behavior_graph_program *p = &alast(program->graphs);
        graph_ir_init(&p->ir);
        if (!lower_graph(program, p)) {
            return program;
        }
    }
#if CHIBI_HAVE_SJLIT
    /* IR buffers must have their final addresses before embedding pointers. */
    for (int i = 0; i < acount(program->graphs); i++) {
        behavior_graph_program *p = &program->graphs[i];
        for (int j = 0; j < acount(p->ir.blocks); j++) {
            void *expression = NULL;
            if (!p->ir.blocks[j].output &&
                !behavior_expression_compile(&p->ir, &p->ir.blocks[j], asset, &expression)) {
                diagnostic(program, p->source, NULL, -1, "SLJIT numeric region emission failed");
                return program;
            }
            apush(p->expressions, expression);
            void *code = behavior_jit_data(&p->ir, &p->ir.blocks[j]);
            apush(p->code, code);
            if (!code) {
                diagnostic(program, p->source, NULL, -1, "SLJIT behavior data emission failed");
                return program;
            }
        }
        for (int j = 0; j < acount(p->source->nodes); j++) {
            node *n = p->source->nodes[j];
            if (!n->desc->flow) {
                continue;
            }
            p->nodes[n->id].flow_code = behavior_jit_control(
                &p->ir.control[p->nodes[n->id].control]
            );
            if (!p->nodes[n->id].flow_code) {
                diagnostic(program, p->source, n, -1, "SLJIT behavior block emission failed");
                return program;
            }
        }
    }
#endif
    return program;
}

behavior_backend behavior_program_backend(const behavior_program *program)
{
    return program ? program->backend : BEHAVIOR_UNAVAILABLE;
}

const compile_diagnostic *behavior_program_diagnostic(const behavior_program *program)
{
    return program && program->backend == BEHAVIOR_UNAVAILABLE ? &program->diagnostic
                                                               : NULL;
}

const behavior_graph_program *behavior_program_graph(const behavior_program *program, const graph *g)
{
    for (int i = 0; program && i < acount(program->graphs); i++) {
        if (program->graphs[i].source == g) {
            return &program->graphs[i];
        }
    }
    return NULL;
}

const graph_ir *behavior_graph_ir(const behavior_graph_program *program)
{
    return &program->ir;
}

static int execute_block(
    const behavior_graph_program *program,
    int block,
    arena *a,
    eval_context *ctx,
    behavior_backend backend
)
{
    assert(block >= 0 && block < acount(program->ir.blocks));
    ctx->program = program;
    ctx->backend = backend;
    behavior_data_context context = { program, ctx, a };
    if (backend == BEHAVIOR_SLJIT) {
        assert(program->code[block]);
        return ((data_entry)program->code[block])(&context);
    }
    assert(backend == BEHAVIOR_BYTECODE);
    const graph_ir_block *b = &program->ir.blocks[block];
    for (int pc = b->first; pc < b->first + b->count;) {
        const graph_ir_behavior_instruction *instruction = &program->ir.behavior[pc];
        int result = behavior_data_instruction(&context, instruction);
        if (instruction->op == GRAPH_IR_MEMO_BRANCH) {
            pc = result ? instruction->target : pc + 1;
            continue;
        }
        if (result) {
            return result;
        }
        pc++;
    }
    return 0;
}

bool behavior_program_input_is_direct(const behavior_graph_program *program, const node *n, int pin)
{
    int block = program->nodes[n->id].inputs[pin];
    return block >= 0 && block < acount(program->expressions) &&
           program->expressions[block];
}

int behavior_program_input(
    const behavior_graph_program *program,
    node *n,
    int pin,
    arena *a,
    pin_value *out,
    eval_context *ctx,
    behavior_backend backend
)
{
    int block = program->nodes[n->id].inputs[pin];
    if (backend == BEHAVIOR_SLJIT && program->expressions[block] &&
        ctx->instance && !ctx->on_data_edge && !ctx->instance->trace_enabled) {
        const pin_spec *spec = &n->input_pins[pin];
        *out = (pin_value){ .type = spec->type,
                            .container = spec->container,
                            .type_name = spec->type_name };
        ctx->program = program;
        ctx->backend = backend;
        int result = ((int (*)(const behavior_instance *, pin_value *))
                          program->expressions[block])(ctx->instance, out);
        runtime_node_inputs(runtime_store_node(ctx->runtime, n))[pin] = *out;
        return result;
    }
    int result = execute_block(program, block, a, ctx, backend);
    if (!result) {
        *out = runtime_node_inputs(runtime_store_node(ctx->runtime, n))[pin];
    }
    return result;
}

int behavior_program_output(
    const behavior_graph_program *program,
    node *n,
    int pin,
    arena *a,
    pin_value *out,
    eval_context *ctx,
    behavior_backend backend
)
{
    int result = execute_block(program, program->nodes[n->id].outputs[pin], a, ctx, backend);
    return result ? result : eval_context_output(ctx, n, pin, out);
}

int behavior_program_flow(const behavior_graph_program *program, node *n, flow_context *ctx, behavior_backend backend)
{
    if (backend == BEHAVIOR_SLJIT) {
        assert(program->nodes[n->id].flow_code);
        return ((flow_entry)program->nodes[n->id].flow_code)(ctx);
    }
    assert(backend == BEHAVIOR_BYTECODE);
    const graph_ir_control *instruction = &program->ir
                                               .control[program->nodes[n->id].control];
    if (instruction->op == GRAPH_IR_BRANCH) {
        int output = behavior_control_branch(ctx);
        return output < 0 ? output : flow_context_continue(ctx, output);
    }
    return instruction->host(n, ctx);
}

int behavior_program_target(
    const behavior_graph_program *program,
    const node *n,
    int output,
    node **target,
    int *input
)
{
    const compiled_node *compiled = &program->nodes[n->id];
    *target = compiled->targets[output];
    *input = compiled->target_pins[output];
    return *target != NULL;
}

int behavior_program_block(const behavior_graph_program *program, const node *n)
{
    return program->nodes[n->id].control;
}

node *behavior_program_block_node(const behavior_graph_program *program, int block)
{
    assert(block >= 0 && block < acount(program->ir.control));
    return (node *)program->ir.control[block].node;
}
