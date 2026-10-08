#include "execution/jit_internal.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

/* Standalone scalar functions keep this evidence independent of query lowering. */
typedef struct
{
    pin_type type;
    behavior_function_op op;
    int mode;
    bool poison;
} predicate_case;

static void emit(behavior_function *f, behavior_function_instruction i)
{
    i.left_slot = behavior_function_left_slot(i.op);
    i.right_slot = behavior_function_right_slot(i.op);
    apush(f->instructions, i);
}
static behavior_function *fixture(predicate_case c, int *comparison)
{
    behavior_function *f = xcalloc(1, sizeof *f);
    f->artifact = execution_artifact_new();
    for (int slot = 0; slot < 6; slot++) {
        apush(f->slots, slot < 2 ? c.type : PIN_BOOL);
    }
    if (c.poison) {
        /* Two live integer values force both boolean operands to stack slots.
         * Explicit word writes seed their otherwise unspecified upper bytes. */
        for (int slot = 6; slot < 8; slot++) {
            apush(f->slots, PIN_U64);
            emit(
                f,
                (behavior_function_instruction){ .op = BF_VARIABLE,
                                                 .type = PIN_U64,
                                                 .output = slot,
                                                 .left = slot - 3 }
            );
        }
        for (int slot = 0; slot < 2; slot++) {
            emit(
                f,
                (behavior_function_instruction){
                    .op = BF_CONSTANT,
                    .type = PIN_U64,
                    .output = slot,
                    .value = { .type = PIN_U64,
                               .u64 = slot ? UINT64_C(0x8877665544332200)
                                           : UINT64_C(0x1122334455667700) } }
            );
        }
    }
    for (int slot = 0; slot < 2; slot++) {
        emit(
            f,
            (
                behavior_function_instruction
            ){ .op = BF_VARIABLE, .type = c.type, .output = slot, .left = slot }
        );
    }
    *comparison = acount(f->instructions);
    emit(
        f,
        (
            behavior_function_instruction
        ){ .op = c.op, .type = PIN_BOOL, .output = 2, .left = 0, .right = 1 }
    );
    int condition = 2;
    if (c.mode) {
        emit(
            f,
            (behavior_function_instruction){
                .op = BF_CONSTANT,
                .type = PIN_BOOL,
                .output = 3,
                .value = { .type = PIN_BOOL, .b = !(c.mode & 1) } }
        );
        emit(
            f,
            (behavior_function_instruction){ .op = c.mode <= 2 ? BF_EQ : BF_NE,
                                             .type = PIN_BOOL,
                                             .output = 4,
                                             .left = 2,
                                             .right = 3 }
        );
        condition = 4;
    }
    int branch = acount(f->instructions);
    emit(f, (behavior_function_instruction){ .op = BF_BRANCH, .left = condition });
    emit(
        f,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .output = 5,
                                         .value = { .type = PIN_BOOL } }
    );
    int jump = acount(f->instructions);
    emit(f, (behavior_function_instruction){ .op = BF_JUMP });
    f->instructions[branch].target = acount(f->instructions);
    emit(
        f,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .output = 5,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    f->instructions[jump].target = acount(f->instructions);
    emit(f, (behavior_function_instruction){ .op = BF_STORE, .type = PIN_BOOL, .left = 5, .target = 2 });
    if (c.poison) {
        for (int slot = 6; slot < 8; slot++) {
            emit(
                f,
                (
                    behavior_function_instruction
                ){ .op = BF_STORE, .type = PIN_U64, .left = slot, .target = slot - 3 }
            );
        }
    }
    emit(f, (behavior_function_instruction){ .op = BF_END });
    return f;
}
static predicate_case case_at(int index)
{
    if (index < 30) {
        return (predicate_case){ PIN_F64, BF_LT + index / 5, index % 5, false };
    }
    index -= 30;
    if (index < 40) {
        return (predicate_case){
            (pin_type[]){ PIN_BOOL, PIN_U32, PIN_U64, PIN_HANDLE }[index / 10],
            BF_EQ + (index % 10) / 5,
            index % 5,
            false
        };
    }
    index -= 40;
    if (index < 10) {
        return (predicate_case){ PIN_BOOL, BF_EQ + index / 5, index % 5, true };
    }
    index -= 10;
    assert(index < 60);
    return (
        predicate_case
    ){ index < 30 ? PIN_I32 : PIN_I64, BF_LT + (index % 30) / 5, index % 5, false };
}
static pin_value input(pin_type type, int sample)
{
    pin_value value = { .type = type };
    static const double real[] = { NAN, -INFINITY, INFINITY, -0.,
                                   0.,  -1.,       1.,       .375 };
    static const uint64_t integer[] = { 0,
                                        1,
                                        UINT32_MAX,
                                        UINT64_C(0x80000000),
                                        UINT64_C(0x100000001),
                                        UINT64_C(0x8000000000000000),
                                        UINT64_MAX,
                                        UINT64_C(0x100000000) };
    switch (type) {
        case PIN_F64: value.d = real[sample]; break;
        case PIN_BOOL: value.b = sample & 1; break;
        case PIN_I32:
            value.i = (int32_t[]){ INT32_MIN, INT32_MIN + 1, -1,        0,
                                   1,         INT32_MAX - 1, INT32_MAX, -2 }[sample];
            break;
        case PIN_I64:
            value.i64 = (int64_t[]){ INT64_MIN, INT64_MIN + 1, -1,        0,
                                     1,         INT64_MAX - 1, INT64_MAX, -2 }[sample];
            break;
        case PIN_U32: value.u = (uint32_t)integer[sample]; break;
        default: value.u64 = integer[sample]; break;
    }
    return value;
}
static bool expected(predicate_case c, pin_value a, pin_value b)
{
    bool result;
    if (c.type == PIN_F64) {
        switch (c.op) {
            case BF_LT: result = a.d < b.d; break;
            case BF_LE: result = a.d <= b.d; break;
            case BF_GT: result = a.d > b.d; break;
            case BF_GE: result = a.d >= b.d; break;
            case BF_EQ: result = a.d == b.d; break;
            default: result = a.d != b.d; break;
        }
    } else if (c.type == PIN_I32 || c.type == PIN_I64) {
        int64_t left = c.type == PIN_I32 ? a.i : a.i64;
        int64_t right = c.type == PIN_I32 ? b.i : b.i64;
        switch (c.op) {
            case BF_LT: result = left < right; break;
            case BF_LE: result = left <= right; break;
            case BF_GT: result = left > right; break;
            case BF_GE: result = left >= right; break;
            case BF_EQ: result = left == right; break;
            default: result = left != right; break;
        }
    } else {
        result = behavior_scalar_bits(a) == behavior_scalar_bits(b);
        if (c.op == BF_NE) {
            result = !result;
        }
    }
    if (c.mode) {
        bool literal = !(c.mode & 1);
        result = c.mode <= 2 ? result == literal : result != literal;
    }
    return result;
}
static void check(behavior_function *f, predicate_case c, int (*generated)(behavior_function_context *))
{
    for (int a = 0; a < 8; a++) {
        for (int b = 0; b < 8; b++) {
            for (int backend = 0; backend < (generated ? 3 : 2); backend++) {
                pin_value variables[] = {
                    input(c.type, a),
                    input(c.type, b),
                    { .type = PIN_BOOL,
                      .b = !expected(c, input(c.type, a), input(c.type, b)) },
                    { .type = PIN_U64, .u64 = UINT64_C(0x123456789abcdef0) },
                    { .type = PIN_U64, .u64 = UINT64_C(0xfedcba9876543210) }
                };
                behavior_function_context ctx = { .variables = variables,
                                                  .budget = 100 };
                assert(
                    !(backend == 2 ? generated(&ctx)
                                   : behavior_function_run(f, &ctx, backend == 1))
                );
                assert(variables[2].b == expected(c, variables[0], variables[1]));
                assert(variables[3].u64 == UINT64_C(0x123456789abcdef0));
                assert(variables[4].u64 == UINT64_C(0xfedcba9876543210));
                assert(ctx.steps == 0);
            }
        }
    }
}
static void rejections()
{
    for (int scenario = 0; scenario < 7; scenario++) {
        int pc;
        behavior_function *f = fixture((predicate_case){ PIN_F64, BF_LT, 1, false }, &pc);
        if (scenario < 3) {
            /* A consumed intermediate must still be published by the compare,
             * literal, or inversion when the branch is compiled. */
            f->instructions[acount(f->instructions) - 1] = (behavior_function_instruction){
                .op = BF_STORE,
                .type = PIN_BOOL,
                .left = scenario + 2,
                .target = 3,
                .left_slot = true
            };
            emit(f, (behavior_function_instruction){ .op = BF_END });
        } else if (scenario < 6) {
            behavior_function_instruction *old = f->instructions;
            f->instructions = NULL;
            emit(f, old[0]);
            emit(f, old[1]);
            for (int slot = 2; slot < 5; slot++) {
                emit(
                    f,
                    (behavior_function_instruction){
                        .op = BF_CONSTANT,
                        .type = PIN_BOOL,
                        .output = slot,
                        .value = { .type = PIN_BOOL, .b = true } }
                );
            }
            /* Enter each interior instruction with an already defined predicate.
             * Aliasing its label to the first comparison would change the result. */
            emit(f, (behavior_function_instruction){ .op = BF_JUMP, .target = pc + 4 + scenario - 2 });
            for (int at = 2; at < acount(old); at++) {
                behavior_function_instruction i = old[at];
                if (i.op == BF_BRANCH || i.op == BF_JUMP) {
                    i.target += 4;
                }
                emit(f, i);
            }
            afree(old);
        } else {
            behavior_function_instruction *old = f->instructions;
            f->instructions = NULL;
            emit(f, old[0]);
            emit(f, old[1]);
            emit(
                f,
                (
                    behavior_function_instruction
                ){ .op = BF_VARIABLE, .type = PIN_BOOL, .output = 3, .left = 3 }
            );
            emit(f, (behavior_function_instruction){ .op = BF_JUMP, .target = 6 });
            /* This earlier false literal is skipped. The comparison entry sees
             * the dynamic true value, so textual constant lookup cannot invert. */
            emit(f, old[3]);
            emit(f, (behavior_function_instruction){ .op = BF_JUMP, .target = 6 });
            emit(f, old[2]);
            for (int at = 4; at < acount(old); at++) {
                behavior_function_instruction i = old[at];
                if (i.op == BF_BRANCH || i.op == BF_JUMP) {
                    i.target += 3;
                }
                emit(f, i);
            }
            afree(old);
        }
        assert(behavior_function_compile(f));
        assert(behavior_function_entry(f));
        for (int sample = 0; sample < 2; sample++) {
            bool comparison = sample == 0;
            for (int native = 0; native < 2; native++) {
                pin_value variables[] = { { .type = PIN_F64, .d = sample ? 3. : 1. },
                                          { .type = PIN_F64, .d = 2. },
                                          { .type = PIN_BOOL },
                                          { .type = PIN_BOOL, .b = true } };
                behavior_function_context ctx = { .variables = variables,
                                                  .budget = 100 };
                assert(!behavior_function_run(f, &ctx, native));
                assert(
                    variables[2].b == (scenario < 3    ? !comparison
                                       : scenario == 6 ? comparison
                                                       : scenario > 3)
                );
                if (scenario < 3) {
                    assert(
                        variables[3].b == (scenario == 0   ? comparison
                                           : scenario == 2 ? !comparison
                                                           : false)
                    );
                }
            }
        }
        behavior_function_free(f);
    }
}
static behavior_asset *graph_fixture(predicate_case c)
{
    behavior_asset *a = behavior_asset_new("Scalar predicate branch");
    for (int index = 0; index < 3; index++) {
        behavior_asset_add_variable(
            a,
            (const char *[]){ "A", "B", "Result" }[index],
            index < 2 ? PIN_F64 : PIN_BOOL,
            index < 2 ? "double" : "bool",
            PIN_CONTAINER_SINGLE,
            NULL,
            ""
        );
    }
    graph *g = behavior_asset_add_function(a, "Choose", false);
    node *compare = graph_add_node(
        g,
        (node_desc *[]){ &NODE_Less,
                         &NODE_Less_Or_Equal,
                         &NODE_Greater,
                         &NODE_Greater_Or_Equal,
                         &NODE_Equal,
                         &NODE_Not_Equal }[c.op - BF_LT]
    );
    for (int index = 0; index < 2; index++) {
        node *get = graph_add_node(g, &NODE_GetVariable);
        assert(!graph_set_node_reference(
            g,
            get,
            a->variables[index].id,
            a->variables[index].name
        ));
        assert(!graph_set_pin_type(g, get, true, 0, PIN_F64, "double"));
        assert(!graph_set_pin_type(g, compare, false, index, PIN_F64, "double"));
        assert(!graph_connect_data(g, get, 0, compare, index));
    }
    node *condition = compare;
    if (c.mode == 1 || c.mode == 4) {
        condition = graph_add_node(g, &NODE_Not);
        assert(!graph_connect_data(g, compare, 0, condition, 0));
    }
    node *branch = graph_add_node(g, &NODE_Branch);
    assert(!graph_connect_data(g, condition, 0, branch, 1));
    assert(!graph_connect_exec(g, g->nodes[0], 0, branch, 0, 0));
    for (int outcome = 0; outcome < 2; outcome++) {
        node *set = graph_add_node(g, &NODE_SetVariable);
        assert(!graph_set_node_reference(
            g,
            set,
            a->variables[2].id,
            a->variables[2].name
        ));
        assert(!graph_set_pin_type(g, set, false, 1, PIN_BOOL, "bool"));
        assert(!graph_set_pin_type(g, set, true, 1, PIN_BOOL, "bool"));
        assert(
            !graph_set_pin_default(g, set, false, 1, &(pin_value){ .type = PIN_BOOL, .b = outcome == 0 })
        );
        assert(!graph_connect_exec(g, branch, outcome, set, 0, 0));
        assert(!graph_connect_exec(g, set, 0, g->nodes[1], 0, 0));
    }
    return a;
}
static const behavior_function *graph_body(behavior_asset *a)
{
    behavior_program_publish(a);
    graph *g = a->graphs[1];
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(behavior_program_cached(a), g),
        g->nodes[0]
    );
    assert(f);
    return f;
}
static void inspect_trace(void *user, const behavior_function_edge *edge, uintptr_t steps)
{
    (void)user;
    (void)edge;
    (void)steps;
}
static void graph_check(behavior_asset *a, predicate_case c, int (*generated)(behavior_function_context *))
{
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    graph *g = a->graphs[1];
    const unsigned budgets[] = { 1, 2, 3, 4, 100 };
    for (int inspected = 0; inspected < 2; inspected++) {
        behavior_instance_set_trace_enabled(bi, inspected);
        for (int left = 0; left < 8; left++) {
            for (int right = 0; right < 8; right++) {
                for (int limit = 0; limit < 5; limit++) {
                    uintptr_t steps = 0, error_steps = 0;
                    uint64_t error_node = 0;
                    int status = 0, error_pin = 0;
                    bool reference = false;
                    for (int mode = 0; mode < (generated ? 4 : 3); mode++) {
                        bi->execution.variables[0] = input(PIN_F64, left);
                        bi->execution.variables[1] = input(PIN_F64, right);
                        bi->execution.variables[2] = (pin_value){
                            .type = PIN_BOOL,
                            .b = !expected(
                                c,
                                bi->execution.variables[0],
                                bi->execution.variables[1]
                            )
                        };
                        if (mode == 3 || (inspected && mode)) {
                            behavior_function_context ctx = {
                                .variables = bi->execution.variables,
                                .budget = budgets[limit],
                                .trace = inspected ? inspect_trace : NULL
                            };
                            int actual = mode == 3
                                           ? generated(&ctx)
                                           : behavior_function_run(graph_body(a), &ctx, mode == 2);
                            assert(actual == status);
                            assert(
                                ctx.steps == steps && bi->execution.variables[2].b == reference
                            );
                            if (status) {
                                assert(
                                    ctx.error_steps == error_steps &&
                                    ctx.node_id == error_node && ctx.error_pin == error_pin
                                );
                            }
                            continue;
                        }
                        assert(!behavior_instance_set_backend(
                            bi,
                            (behavior_backend[]){ BEHAVIOR_ORACLE,
                                                  BEHAVIOR_BYTECODE,
                                                  BEHAVIOR_SLJIT }[mode]
                        ));
                        flow_error error = { 0 };
                        flow_options options = { .instance = bi,
                                                 .step_budget = budgets[limit] };
                        uint64_t eval = eval_oracle_calls(),
                                 flow = flow_oracle_calls();
                        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ 0 }, &options, &error);
                        assert(run);
                        int actual = flow_run_resume(run);
                        if (!mode) {
                            status = actual;
                            reference = bi->execution.variables[2].b;
                            steps = flow_run_steps(run);
                            if (status) {
                                const flow_error *failed = flow_run_error(run);
                                error_steps = failed->steps;
                                error_pin = failed->pin;
                                error_node = failed->node->id;
                            } else {
                                assert(
                                    reference == expected(
                                                     c,
                                                     bi->execution.variables[0],
                                                     bi->execution.variables[1]
                                                 )
                                );
                            }
                        } else {
                            if (!run->whole_function_executed ||
                                actual != status || flow_run_steps(run) != steps) {
                                fprintf(
                                    stderr,
                                    "predicate graph op=%d chain=%d left=%d "
                                    "right=%d mode=%d budget=%u inspected=%d "
                                    "whole=%d status=%d/%d steps=%lu/%lu\n",
                                    c.op,
                                    c.mode,
                                    left,
                                    right,
                                    mode,
                                    budgets[limit],
                                    inspected,
                                    run->whole_function_executed,
                                    actual,
                                    status,
                                    (unsigned long)flow_run_steps(run),
                                    (unsigned long)steps
                                );
                            }
                            assert(
                                run->whole_function_executed &&
                                actual == status && flow_run_steps(run) == steps
                            );
                            assert(bi->execution.variables[2].b == reference);
                            if (status) {
                                const flow_error *failed = flow_run_error(run);
                                assert(
                                    failed->steps == error_steps &&
                                    failed->pin == error_pin && failed->node->id == error_node
                                );
                            }
                            if (!inspected) {
                                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                            }
                        }
                        flow_run_free(run);
                    }
                }
            }
        }
    }
    behavior_instance_destroy(bi);
}
void test_predicate_branches()
{
    rejections();
    for (int index = 0; index < 140; index++) {
        int pc;
        predicate_case c = case_at(index);
        behavior_function *f = fixture(c, &pc);
        assert(behavior_function_compile(f));
        check(f, c, NULL);
        behavior_function_free(f);
    }
    for (int index = 0; index < 30; index++) {
        predicate_case c = case_at(index);
        behavior_asset *a = graph_fixture(c);
        graph_check(a, c, NULL);
        behavior_asset_free(a);
    }
    puts("PASS: oracle/bytecode/SLJIT scalar predicates, floating inversions, boolean byte widths and unsigned/handle equality");
}
void emit_predicate_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int index = 0; index < 140; index++) {
        int pc;
        behavior_function *f = fixture(case_at(index), &pc);
        char name[40];
        snprintf(name, sizeof name, "generated_predicate%d", index);
        behavior_function_write_c(f, out, name);
        behavior_function_free(f);
    }
    for (int index = 0; index < 30; index++) {
        behavior_asset *a = graph_fixture(case_at(index));
        char name[40];
        snprintf(name, sizeof name, "generated_predicate_graph%d", index);
        behavior_function_write_c(graph_body(a), out, name);
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
void test_predicate_c(const char *path)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int index = 0; index < 140; index++) {
        int pc;
        predicate_case c = case_at(index);
        behavior_function *f = fixture(c, &pc);
        assert(behavior_function_compile(f));
        char name[40];
        snprintf(name, sizeof name, "generated_predicate%d", index);
        int (*generated)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        assert(generated);
        check(f, c, generated);
        behavior_function_free(f);
    }
    for (int index = 0; index < 30; index++) {
        predicate_case c = case_at(index);
        behavior_asset *a = graph_fixture(c);
        char name[40];
        snprintf(name, sizeof name, "generated_predicate_graph%d", index);
        int (*generated)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        assert(generated);
        graph_check(a, c, generated);
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    puts("PASS: oracle/bytecode/SLJIT/generated-C scalar predicate parity");
}
