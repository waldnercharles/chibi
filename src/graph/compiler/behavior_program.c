#include "graph/compiler/behavior_program.h"

#include "graph/compiler/behavior_expression.h"
#include "graph/compiler/behavior_jit.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
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
    behavior_function *function;
    int control;
} compiled_node;

typedef struct
{
    const node *entry;
    behavior_function_diagnostic diagnostic;
} function_exclusion;

typedef struct
{
    node *entry;
    const behavior_function *function;
} batch_event;

struct behavior_graph_program
{
    graph *source;
    function_exclusion *exclusions;
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
    compile_report report;
    behavior_graph_program *graphs;
    node **events; /* Named root entries, including duplicates on invalid assets. */
    batch_event *batches;
    uint64_t *states;
};

int behavior_asset_state_index(const behavior_asset *asset, uint64_t node_id)
{
    int index = 0;
    for (int i = 0; i < acount(asset->graphs); i++) {
        for (int j = 0; j < acount(asset->graphs[i]->nodes); j++) {
            const node *n = asset->graphs[i]->nodes[j];
            if (!n->desc->instance_state) {
                continue;
            }
            if (n->id == node_id) {
                return index;
            }
            index++;
        }
    }
    return -1;
}
const uint64_t *behavior_program_states(const behavior_program *program, int *count)
{
    *count = acount(program->states);
    return program->states;
}

uint64_t behavior_program_revision(const behavior_program *program)
{
    return program->revision;
}
node *behavior_program_event(const behavior_program *program, const char *name, bool *duplicate)
{
    node *found = NULL;
    *duplicate = false;
    for (int i = 0; i < acount(program->events); i++) {
        node *entry = program->events[i];
        if (strcmp(entry->reference_name, name)) {
            continue;
        }
        if (found) {
            *duplicate = true;
            return entry;
        }
        found = entry;
    }
    return found;
}

const behavior_function *behavior_program_batch_event(
    const behavior_program *program,
    const char *name,
    node **entry
)
{
    for (int i = 0; i < acount(program->batches); i++) {
        const batch_event *event = &program->batches[i];
        if (!strcmp(event->entry->reference_name, name)) {
            *entry = event->entry;
            return event->function;
        }
    }
    return NULL;
}

void behavior_program_event_batch_info(
    const behavior_program *program,
    const node *entry,
    behavior_event_batch_info *out
)
{
    assert(out);
    *out = (behavior_event_batch_info){
        .entry = entry,
        .backend = behavior_program_backend(program),
        .reason = BEHAVIOR_BATCH_UNAVAILABLE,
    };
    if (!program || !entry || !entry->reference_name) {
        return;
    }
    bool duplicate = false;
    node *found = behavior_program_event(program, entry->reference_name, &duplicate);
    out->found = found != NULL;
    out->duplicate = duplicate;
    out->entry = found;
    if (!found || duplicate || found != entry) {
        return;
    }
    out->program_diagnostic = behavior_program_diagnostic(program);
    if (out->program_diagnostic) {
        return;
    }
    const behavior_graph_program *graph_program = NULL;
    for (int i = 0; i < acount(program->graphs) && !graph_program; i++) {
        graph *source = program->graphs[i].source;
        for (int j = 0; j < acount(source->nodes); j++) {
            if (source->nodes[j] == entry) {
                graph_program = &program->graphs[i];
                break;
            }
        }
    }
    if (!graph_program) {
        return;
    }
    out->function = behavior_program_function(graph_program, entry);
    if (!out->function) {
        out->function_diagnostic = behavior_program_function_diagnostic(graph_program, entry);
        return;
    }
    out->reason = behavior_function_batch_reason(out->function, graph_program->source, entry);
}

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
    size_t field_offset = 0;
    int depth = 0;
    while (source->output_pins[aggregate].field_source) {
        if (depth++ >= source->output_count) {
            return -1;
        }
        const type_field *member = graph_output_field(source, aggregate, &aggregate);
        if (!member) {
            return -1;
        }
        if (!field) {
            field = member;
        }
        field_offset += member->offset;
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
            .field_offset = field_offset,
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
    graph *expression = n->desc == &NODE_BehaviorExpression && g->owner
                          ? behavior_asset_find_graph(g->owner, n->reference_id)
                          : NULL;
    if (n->desc == &NODE_CallFunction || n->desc == &NODE_CallMacro ||
        (expression && expression->expression_mode == GRAPH_EXPRESSION_ACTION)) {
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
    compile_report_free(&program->report);
    for (int i = 0; i < acount(program->graphs); i++) {
        behavior_graph_program *p = &program->graphs[i];
        for (int j = 0; j < acount(p->nodes); j++) {
            compiled_node *n = &p->nodes[j];
            afree(n->inputs);
            afree(n->outputs);
            afree(n->targets);
            afree(n->target_pins);
            behavior_jit_free(n->flow_code);
            behavior_function_free(n->function);
        }
        for (int j = 0; j < acount(p->code); j++) {
            behavior_jit_free(p->code[j]);
        }
        for (int j = 0; j < acount(p->expressions); j++) {
            behavior_jit_free(p->expressions[j]);
        }
        afree(p->exclusions);
        afree(p->expressions);
        afree(p->nodes);
        afree(p->code);
        graph_ir_free(&p->ir);
    }
    afree(program->graphs);
    afree(program->events);
    afree(program->batches);
    afree(program->states);
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
    for (int i = 0; i < acount(asset->graphs); i++) {
        for (int j = 0; j < acount(asset->graphs[i]->nodes); j++) {
            const node *n = asset->graphs[i]->nodes[j];
            if (n->desc->instance_state) {
                apush(program->states, n->id);
            }
        }
    }
    /* Dispatch distinguishes unknown/duplicate names before compile errors.
     * Keep the index available even when validation or native emission fails. */
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        node *entry = asset->graph->nodes[i];
        if (entry->desc->flow_kind == NODE_FLOW_EVENT && entry->reference_name) {
            apush(program->events, entry);
        }
    }
    if (behavior_asset_has_component_nodes(asset)) {
        diagnostic(program, NULL, NULL, -1, "component nodes must be lowered before publication");
        return program;
    }
    compile_report report;
    compile_report_init(&report);
    behavior_asset_compile(asset, &report);
    program->report = report;
    if (compile_report_blocked(&report)) {
        program->diagnostic = *compile_report_first_error(&report);
        return program;
    }
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
        for (int j = 0; j < acount(p->source->nodes); j++) {
            node *n = p->source->nodes[j];
            bool function_entry = n->desc == &NODE_FunctionEntry &&
                                  p->source->kind == GRAPH_FUNCTION &&
                                  n->id == p->source->entry_id;
            if (n->desc != &NODE_Event && !function_entry) {
                continue;
            }
            behavior_function_diagnostic exclusion;
            behavior_function *f = behavior_function_lower(asset, p->source, n, &exclusion);
            if (!f) {
                apush(p->exclusions, ((function_exclusion){ n, exclusion }));
            }
            p->nodes[n->id].function = f;
            if (f && program->backend == BEHAVIOR_SLJIT &&
                !behavior_function_compile(f)) {
                diagnostic(program, p->source, n, -1, "SLJIT whole-function emission failed");
                return program;
            }
            if (f && program->backend == BEHAVIOR_SLJIT && n->reference_name &&
                !strcmp(n->reference_name, "Tick") && behavior_function_batchable(f) &&
                !behavior_function_compile_system(f)) {
                diagnostic(program, p->source, n, -1, "SLJIT Tick system emission failed");
                return program;
            }
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
    const behavior_graph_program *root = behavior_program_graph(program, asset->graph);
    for (int i = 0; i < acount(program->events); i++) {
        node *entry = program->events[i];
        const behavior_function *f = behavior_program_function(root, entry);
        if (!behavior_function_batchable(f)) {
            continue;
        }
        bool duplicate;
        behavior_program_event(program, entry->reference_name, &duplicate);
        if (!duplicate) {
            apush(program->batches, ((batch_event){ entry, f }));
        }
    }
    return program;
}

behavior_backend behavior_program_backend(const behavior_program *program)
{
    return program ? program->backend : BEHAVIOR_UNAVAILABLE;
}
const compile_report *behavior_program_report(const behavior_program *program)
{
    return program ? &program->report : NULL;
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

const behavior_function *behavior_program_function(
    const behavior_graph_program *program,
    const node *entry
)
{
    return program->nodes[entry->id].function;
}

const behavior_function_diagnostic *behavior_program_function_diagnostic(
    const behavior_graph_program *program,
    const node *entry
)
{
    for (int i = 0; i < acount(program->exclusions); i++) {
        if (program->exclusions[i].entry == entry) {
            return &program->exclusions[i].diagnostic;
        }
    }
    return NULL;
}
