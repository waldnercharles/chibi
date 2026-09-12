#include "graph/compiler/behavior_function_internal.h"
#include "graph/model/registry.h"
#include "graph/nodes/operator.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "support/xalloc.h"
#include "test/whole_loop_fixture.h"
#include "test/whole_suspend_fixture.h"

#include <stdlib.h>

void whole_suspend_instrument(behavior_function *f, bool expressions)
{
    assert(!f->code && !f->suspensions);
    behavior_function_free(f->fast);
    f->fast = NULL;
    int count = acount(f->instructions);
    int *map = xcalloc((size_t)count, sizeof *map);
    behavior_function_instruction *out = NULL;
    for (int pc = 0; pc < count; pc++) {
        behavior_function_instruction i = f->instructions[pc];
        map[pc] = acount(out);
        apush(out, i);
        if ((!expressions && i.op != BF_CHECK) || i.op == BF_JUMP || i.op == BF_BRANCH ||
            i.op == BF_RETURN || i.op == BF_END || i.op == BF_FAIL) {
            continue;
        }
        int frame = i.op == BF_ENTER ? i.target
                  : i.op == BF_LEAVE ? f->frames[i.target].source.parent
                                     : i.frame;
        apush(
            out,
            ((behavior_function_instruction){ .op = BF_SUSPEND,
                                              .frame = frame,
                                              .source = f->frames[frame].source_index,
                                              .stack = i.stack,
                                              .node_id = i.node_id })
        );
    }
    for (int pc = 0; pc < acount(out); pc++) {
        if (out[pc].op == BF_JUMP || out[pc].op == BF_BRANCH) {
            out[pc].target = map[out[pc].target];
        }
    }
    afree(f->instructions);
    f->instructions = out;
    free(map);
    behavior_function_suspensions(f);
}
void whole_suspend_smoke()
{
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    behavior_asset *asset = whole_loop_fixture(WHOLE_LOOP_NESTED_CALL);
    graph *g = asset->graphs[1];
    behavior_function_diagnostic diagnostic;
    behavior_function *f = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
    assert(f);
    whole_suspend_instrument(f, true);
    bool native = behavior_default_backend() == BEHAVIOR_SLJIT;
    if (native) {
        assert(behavior_function_compile(f));
    }
    behavior_function_activation activation = { 0 };
    behavior_function_activation_init(f, &activation);
    pin_value args[] = { { .type = PIN_I32, .i = 1 }, { .type = PIN_I32, .i = 4 } };
    pin_value variables[] = { asset->variables[0].default_value,
                              asset->variables[1].default_value };
    pin_value result[2] = { 0 };
    behavior_function_context ctx = { .activation = &activation,
                                      .arguments = args,
                                      .argument_count = 2,
                                      .variables = variables,
                                      .results = result,
                                      .budget = 1000 };
    int status, yields = 0;
    do {
        status = behavior_function_run(f, &ctx, native && yields % 2 == 0);
        if (status == FLOW_SUSPENDED_RESULT) {
            yields++;
            assert(activation.pc > 0 && yields < 5000);
        }
    } while (status == FLOW_SUSPENDED_RESULT);
    assert(!status && !activation.pc && yields > 100 && variables[0].i == 8);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_function_activation_free(&activation);
    behavior_function_free(f);
    behavior_asset_free(asset);
    for (int wide = 0; wide < 2; wide++) {
        f = whole_suspend_float_ir(wide ? PIN_F64 : PIN_F32);
        if (native) {
            assert(behavior_function_compile(f));
        }
        behavior_function_activation_init(f, &activation);
        pin_value returned[2] = { 0 };
        ctx = (behavior_function_context){ .activation = &activation,
                                           .results = returned,
                                           .budget = 1000 };
        assert(behavior_function_run(f, &ctx, false) == FLOW_SUSPENDED_RESULT);
        assert(!behavior_function_run(f, &ctx, native));
        assert((wide ? returned[1].d : returned[1].f) == 7.);
        behavior_function_activation_free(&activation);
        behavior_function_free(f);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    printf(
        "PASS: %s continuation state, nested loops, floating live values and "
        "backend switches; zero "
        "oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}

behavior_asset *whole_suspend_float_fixture(pin_type type)
{
    behavior_asset *a = behavior_asset_new("Suspended floating-point expression");
    graph *g = behavior_asset_add_function(a, "Compute", false);
    const char *name = type == PIN_F32 ? "float" : "double";
    for (int j = 0; j < 3; j++) {
        pin_spec spec = { .name = (const char *[]){ "A", "B", "C" }[j],
                          .type = type,
                          .type_name = name };
        assert(graph_add_pin(g, g->nodes[0], 1, &spec) == j + 1);
    }
    pin_spec spec = { .name = "Result", .type = type, .type_name = name };
    assert(graph_add_pin(g, g->nodes[1], 0, &spec) == 1);
    node *add = graph_add_node(g, &NODE_Add), *div = graph_add_node(g, &NODE_Divide);
    node *nodes[] = { add, div };
    for (int j = 0; j < 2; j++) {
        assert(!graph_set_pin_type(g, nodes[j], 0, 0, type, name));
        assert(!graph_set_pin_type(g, nodes[j], 0, 1, type, name));
        assert(!graph_set_pin_type(g, nodes[j], 1, 0, type, name));
    }
    assert(!graph_connect_data(g, g->nodes[0], 1, add, 0));
    assert(!graph_connect_data(g, g->nodes[0], 2, div, 0));
    assert(!graph_connect_data(g, g->nodes[0], 3, div, 1));
    assert(!graph_connect_data(g, div, 0, add, 1));
    assert(!graph_connect_data(g, add, 0, g->nodes[1], 1));
    assert(!graph_connect_exec(g, g->nodes[0], 0, g->nodes[1], 0, 0));
    return a;
}

/* A + B/C, with A live across suspension before the division. Floating DIV's
 * unused target field must not kill slot zero as an integer remainder would. */
behavior_function *whole_suspend_float_ir(pin_type type)
{
    behavior_function *f = xcalloc(1, sizeof *f);
    apush(f->frames, ((behavior_function_frame){ .source.parent = -1 }));
    for (int j = 0; j < 5; j++) {
        apush(f->slots, type);
    }
    double inputs[] = { 3.5, 7., 2. };
    for (int j = 0; j < 3; j++) {
        pin_value value = { .type = type };
        if (type == PIN_F32) {
            value.f = inputs[j];
        }
        if (type == PIN_F64) {
            value.d = inputs[j];
        }
        apush(
            f->instructions,
            ((
                behavior_function_instruction
            ){ .op = BF_CONSTANT, .type = type, .output = j, .value = value })
        );
    }
    apush(f->instructions, ((behavior_function_instruction){ .op = BF_SUSPEND }));
    apush(
        f->instructions,
        ((
            behavior_function_instruction
        ){ .op = BF_DIV, .type = type, .output = 3, .left = 1, .right = 2 })
    );
    apush(
        f->instructions,
        ((
            behavior_function_instruction
        ){ .op = BF_ADD, .type = type, .output = 4, .left = 0, .right = 3 })
    );
    apush(
        f->instructions,
        ((behavior_function_instruction){ .op = BF_RESULT, .type = type, .left = 4, .target = 1 })
    );
    apush(f->instructions, ((behavior_function_instruction){ .op = BF_END }));
    behavior_function_suspensions(f);
    return f;
}
