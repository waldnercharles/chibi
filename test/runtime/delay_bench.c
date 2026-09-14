#include "graph/compiler/behavior_function.h"
#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_delay_fixture.h"
#include "test/whole_loop_fixture.h"

#include <dlfcn.h>
#include <time.h>

typedef struct
{
    int points, watches, edges;
    uint64_t hash;
} delay_observer;
typedef struct
{
    int status, pc, frame, source, stack, count, limit;
    uintptr_t steps, frame_steps, error_steps;
    uint64_t node, error_node;
    double wake;
    delay_observer observer;
} delay_checkpoint;
static void observe_word(delay_observer *o, uint64_t word)
{
    o->hash = (o->hash ^ word) * UINT64_C(1099511628211);
}
static int pause_point(void *user, const behavior_function_point *point)
{
    delay_observer *o = user;
    assert(point->graph && (point->node || point->kind != BEHAVIOR_POINT_NODE));
    ++o->points;
    observe_word(o, point->node ? point->node->id : 0);
    observe_word(o, point->kind);
    return FLOW_PAUSED_RESULT;
}
static void observe_watch(void *user, const behavior_function_watch *source, const pin_value *value)
{
    delay_observer *o = user;
    o->watches++;
    observe_word(o, source->node ? source->node->id : 0);
    observe_word(o, source->frame);
    observe_word(o, source->pending_frame);
    observe_word(o, source->pin);
    observe_word(o, source->output);
    if (!value) {
        return;
    }
    observe_word(o, value->type);
    observe_word(o, value->container);
    uint64_t bits = 0;
    memcpy(&bits, &value->d, sizeof bits);
    observe_word(o, bits);
    observe_word(o, value->type_name != NULL);
    for (const char *s = value->type_name; s && *s; s++) {
        observe_word(o, (unsigned char)*s);
    }
}
static void observe_edge(void *user, const behavior_function_edge *edge, uintptr_t steps)
{
    delay_observer *o = user;
    o->edges++;
    observe_word(o, edge->from_id);
    observe_word(o, edge->from_pin_id);
    observe_word(o, edge->to_id);
    observe_word(o, edge->to_pin_id);
    observe_word(o, edge->kind);
    observe_word(o, steps);
}
static void parity(int kind, int budget, int inspection, void *library)
{
    behavior_asset *a = whole_delay_fixture(kind);
    graph *g = a->graphs[1];
    behavior_function_diagnostic diagnostic;
    behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
    assert(f && behavior_function_compile(f));
    char name[40];
    snprintf(name, sizeof name, "generated_delay%d", kind);
    int (*generated)(
        behavior_function_context *
    ) = library ? (int (*)(behavior_function_context *))dlsym(library, name) : NULL;
    assert(!library || generated);
    delay_checkpoint *expected = NULL;
    for (int mode = 0; mode < (generated ? 4 : 3); mode++) {
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        pin_value variables[] = { a->variables[0].default_value,
                                  a->variables[1].default_value };
        pin_value args[] = { { .type = PIN_I32, .i = 1 }, { .type = PIN_I32, .i = 3 } };
        pin_value result[2] = { 0 };
        delay_observer observer = { 0 };
        behavior_function_context ctx = { .activation = &activation,
                                          .variables = variables,
                                          .arguments = args,
                                          .argument_count = 2,
                                          .results = result,
                                          .budget = budget,
                                          .inspection_user = &observer };
        behavior_function_prepare_host(f, &ctx);
        int at = 0, status;
        do {
            ctx.inspect = inspection == 1 || (inspection == 2 && at % 2) ? pause_point
                                                                         : NULL;
            ctx.watch = ctx.inspect || inspection == 3 ? observe_watch : NULL;
            ctx.trace = ctx.inspect || inspection == 3 ? observe_edge : NULL;
            status = mode == 3
                       ? generated(&ctx)
                       : behavior_function_run(f, &ctx, mode == 1 || (mode == 2 && at % 2));
            delay_checkpoint c = { .status = status,
                                   .pc = activation.pc,
                                   .frame = activation.frame,
                                   .source = activation.source,
                                   .stack = activation.stack,
                                   .count = variables[0].i,
                                   .limit = variables[1].i,
                                   .steps = ctx.steps,
                                   .frame_steps = activation.steps,
                                   .error_steps = ctx.error_steps,
                                   .node = activation.node_id,
                                   .error_node = ctx.node_id,
                                   .wake = ctx.wake_time,
                                   .observer = observer };
            if (!mode) {
                apush(expected, c);
            }
            if (mode) {
                assert(at < acount(expected));
                delay_checkpoint e = expected[at];
                assert(
                    c.observer.points == e.observer.points &&
                    c.observer.watches == e.observer.watches &&
                    c.observer.edges == e.observer.edges &&
                    c.observer.hash == e.observer.hash
                );
                assert(
                    c.status == e.status && c.pc == e.pc && c.frame == e.frame &&
                    c.source == e.source && c.stack == e.stack
                );
                assert(
                    c.count == e.count && c.limit == e.limit &&
                    c.steps == e.steps && c.frame_steps == e.frame_steps
                );
                if ((status < 0 && (c.error_steps != e.error_steps ||
                                    c.error_node != e.error_node)) ||
                    c.node != e.node || c.wake != e.wake) {
                    fprintf(
                        stderr,
                        "kind=%d budget=%d inspection=%d mode=%d at=%d "
                        "status=%d error_steps=%lu/%lu node=%llu/%llu "
                        "error=%llu/%llu wake=%g/%g\n",
                        kind,
                        budget,
                        inspection,
                        mode,
                        at,
                        status,
                        (unsigned long)c.error_steps,
                        (unsigned long)e.error_steps,
                        (unsigned long long)c.node,
                        (unsigned long long)e.node,
                        (unsigned long long)c.error_node,
                        (unsigned long long)e.error_node,
                        c.wake,
                        e.wake
                    );
                    assert(false);
                }
            }
            if (status == FLOW_SUSPENDED_RESULT) {
                ctx.now = ctx.wake_time;
            }
            assert(++at < 1000);
        } while (status == FLOW_SUSPENDED_RESULT || status == FLOW_PAUSED_RESULT);
        assert(at == acount(expected));
        assert(
            inspection != 1 || budget == 1 ||
            (observer.points && observer.watches && observer.edges)
        );
        behavior_function_release_host(&ctx);
        behavior_function_activation_free(&activation);
    }
    afree(expected);
    behavior_function_free(f);
    behavior_asset_free(a);
}
void test_delay_entries(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    int kinds[] = { WHOLE_LOOP_RANGE, WHOLE_LOOP_NESTED_CALL, WHOLE_LOOP_SNAPSHOT };
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int k = 0; k < 3; k++) {
        for (int budget = 1; budget <= 45; budget++) {
            for (int inspection = 0; inspection < 4; inspection++) {
                parity(kinds[k], budget, inspection, library);
            }
        }
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    if (library) {
        assert(!dlclose(library));
    }
    puts("PASS: authored Delay, safe points, budgets and native/bytecode variant switches across every suspension");
}
void emit_delay_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    int kinds[] = { WHOLE_LOOP_RANGE, WHOLE_LOOP_NESTED_CALL, WHOLE_LOOP_SNAPSHOT };
    for (int k = 0; k < 3; k++) {
        behavior_asset *a = whole_delay_fixture(kinds[k]);
        graph *g = a->graphs[1];
        behavior_function_diagnostic diagnostic;
        behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
        assert(f);
        char name[40];
        snprintf(name, sizeof name, "generated_delay%d", kinds[k]);
        behavior_function_write_c(f, out, name);
        behavior_function_free(f);
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void bench_activation(behavior_asset *a, int count, int rotation)
{
    graph *g = a->graphs[1];
    for (int order = 0; order < 3; order++) {
        int mode = (order + rotation) % 3;
        behavior_backend backend = (behavior_backend[]){ BEHAVIOR_SLJIT,
                                                         BEHAVIOR_BYTECODE,
                                                         BEHAVIOR_ORACLE }[mode];
        behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(instance, backend));
        behavior_instance_set_trace_enabled(instance, false);
        pin_value args[] = { { .type = PIN_I32, .i = 1 }, { .type = PIN_I32, .i = 4 } };
        flow_options options = { .instance = instance, .step_budget = 1000 };
        flow_run *run = NULL;
        double start = 0;
        uint64_t checksum = 0, calls = 0;
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        for (int iteration = -10000; iteration < count; iteration++) {
            if (!iteration) {
                start = seconds();
            }
            instance->variables[0].i = 0;
            args[1].i = 2 + (iteration & 3);
            run = run
                    ? flow_run_start_reuse(run, g, g->nodes[0], (flow_inputs){ args, 2 }, &options, NULL)
                    : flow_run_start(g, g->nodes[0], (flow_inputs){ args, 2 }, &options, NULL);
            assert(run);
            int status = flow_run_resume(run), entries = 1;
            while (status == FLOW_SUSPENDED_RESULT) {
                status = flow_run_tick_at(run, flow_run_wake_time(run));
                entries++;
            }
            assert(!status && instance->variables[0].i == args[1].i * 2);
            assert(mode == 2 || (run->whole_function_executed && !acount(run->children)));
            if (iteration >= 0) {
                checksum += instance->variables[0].i;
                calls += entries;
            }
            assert(!flow_run_recycle(run));
        }
        printf(
            "%s delay_activation ns=%.3f checksum=%llu calls=%llu\n",
            behavior_backend_name(backend),
            (seconds() - start) * 1e9 / count,
            (unsigned long long)checksum,
            (unsigned long long)calls
        );
        assert(mode == 2 || (eval == eval_oracle_calls() && flow == flow_oracle_calls()));
        flow_run_free(run);
        behavior_instance_destroy(instance);
    }
}
int bench_delay(const char *path, int count, int rotation)
{
    test_delay_entries(path);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    behavior_asset *a = whole_delay_fixture(WHOLE_LOOP_NESTED_CALL);
    graph *g = a->graphs[1];
    behavior_function_diagnostic diagnostic;
    behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
    assert(f && behavior_function_compile(f));
    char name[40];
    snprintf(name, sizeof name, "generated_delay%d", WHOLE_LOOP_NESTED_CALL);
    int (*entries[])(behavior_function_context *) = {
        (int (*)(behavior_function_context *))behavior_function_entry(f),
        (int (*)(behavior_function_context *))dlsym(library, name)
    };
    assert(entries[1]);
    behavior_function_activation activation = { 0 };
    behavior_function_activation_init(f, &activation);
    pin_value variables[] = { a->variables[0].default_value,
                              a->variables[1].default_value };
    pin_value args[] = { { .type = PIN_I32, .i = 1 }, { .type = PIN_I32, .i = 4 } };
    pin_value result[2] = { 0 };
    behavior_function_context ctx = { .activation = &activation,
                                      .arguments = args,
                                      .argument_count = 2,
                                      .variables = variables,
                                      .results = result,
                                      .budget = 1000 };
    behavior_function_prepare_host(f, &ctx);
    for (int order = 0; order < 3; order++) {
        int mode = (order + rotation) % 3;
        double start = 0;
        uint64_t checksum = 0, resumes = 0;
        for (int iteration = -10000; iteration < count; iteration++) {
            if (!iteration) {
                start = seconds();
            }
            ctx.steps = 0;
            ctx.now = 0;
            variables[0].i = 0;
            args[1].i = 2 + (iteration & 3);
            int status, calls = 0;
            do {
                status = mode < 2 ? entries[mode](&ctx)
                                  : behavior_function_run(f, &ctx, false);
                ctx.now = ctx.wake_time;
                calls++;
            } while (status == FLOW_SUSPENDED_RESULT);
            assert(!status && variables[0].i == args[1].i * 2);
            if (iteration >= 0) {
                checksum += variables[0].i;
                resumes += calls;
            }
        }
        printf(
            "%s delay_body ns=%.3f checksum=%llu calls=%llu\n",
            (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
            (seconds() - start) * 1e9 / count,
            (unsigned long long)checksum,
            (unsigned long long)resumes
        );
    }
    behavior_function_release_host(&ctx);
    behavior_function_activation_free(&activation);
    behavior_function_free(f);
    bench_activation(a, count, rotation);
    behavior_asset_free(a);
    assert(!dlclose(library));
    return 0;
}
