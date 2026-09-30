#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_async_fixture.h"

#include <dlfcn.h>
#include <time.h>

static behavior_function *entry(behavior_asset *asset)
{
    behavior_program *p = behavior_program_publish(asset);
    bool duplicate;
    node *n = behavior_program_event(p, "Run", &duplicate);
    assert(n && !duplicate && !behavior_program_diagnostic(p));
    behavior_function *f = (behavior_function *)
        behavior_program_function(behavior_program_graph(p, asset->graph), n);
    assert(f && f->waits);
    return f;
}

void emit_async_c(const char *path)
{
    behavior_asset *asset = whole_async_fixture();
    FILE *out = fopen(path, "w");
    assert(out);
    behavior_function_write_c(entry(asset), out, "generated_async");
    assert(!fclose(out));
    behavior_asset_free(asset);
}

static void checkpoint(
    test_flow_snapshot **expected,
    int mode,
    int *at,
    flow_run *run,
    behavior_instance *instance,
    int result
)
{
    test_flow_snapshot s = test_flow_snapshot_take(run, instance, result);
    if (!mode) {
        apush(*expected, s);
    }
    if (mode) {
        assert(*at < acount(*expected));
        test_flow_snapshot_same(s, (*expected)[*at]);
        test_flow_snapshot_free(&s);
    }
    assert(++*at < 300);
}

static void parity(bool trace, int stepping, int failure, bool no_output, void *library)
{
    behavior_asset *asset = whole_async_fixture();
    if (stepping == 3) {
        graph *child = asset->graphs[1];
        for (int i = 0; i < acount(child->nodes); i++) {
            node *n = child->nodes[i];
            if (n->desc->command && n->desc->command->begin) {
                assert(!graph_set_breakpoint(child, n, true));
            }
        }
    }
    behavior_function *f = entry(asset);
    void *code = f->code, *inspection_code = f->inspection_code, *dispatch = f->dispatch;
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            /* Compare emitted C through the same activation and host lifecycle.
             * Only this fresh test-owned program has its entry replaced. */
            f->code = dlsym(library, "generated_async");
            assert(f->code);
            f->inspection_code = f->dispatch = NULL;
        }
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(entry(asset) == f);
        assert(!behavior_instance_set_backend(
            instance,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(instance, trace);
        whole_async_capture capture = { .fail_at = failure, .immediate = stepping == 6 };
        if (failure != -1) {
            assert(!behavior_instance_bind_service(instance, "test.async", &capture));
        }
        char *name = strdup("runtime \"name\"\nwith \\ escape");
        pin_value arg = { .type = PIN_NAME, .str = name };
        flow_options options = { .instance = instance };
        flow_run *run = flow_run_start(
            asset->graph,
            asset->graph->nodes[0],
            (flow_inputs){ &arg, 1 },
            &options,
            NULL
        );
        assert(run);
        free(name);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = stepping == 2 ? flow_run_step(run) : flow_run_resume(run),
            at = 0;
        while (true) {
            if (mode) {
                assert(run->whole_function_executed && run->compiled && !acount(run->children));
            }
            checkpoint(&expected, mode, &at, run, instance, result);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                break;
            }
            if (state == FLOW_PAUSED) {
                result = stepping == 3 ? flow_run_resume(run) : flow_run_step(run);
                continue;
            }
            assert(state == FLOW_SUSPENDED);
            if (stepping == 4 && mode == 2) {
                run->backend = at % 2 ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
            }
            if (!flow_run_waiting_for_host(run)) {
                double wake = flow_run_wake_time(run);
                assert(flow_run_tick_at(run, wake - .01) == FLOW_SUSPENDED_RESULT);
                result = flow_run_tick_at(run, wake);
                continue;
            }
            int index = acount(capture.requests) - 1;
            assert(index >= 0 && capture.arguments[index] == index);
            assert(!strcmp(capture.names[index], "runtime \"name\"\nwith \\ escape"));
            flow_host_request *request = capture.requests[index];
            if (capture.immediate) {
                assert(flow_host_status(request) == FLOW_HOST_READY);
                result = flow_host_resume(request);
                continue;
            }
            assert(flow_host_status(request) == FLOW_HOST_PENDING);
            assert(flow_host_run(request) == run && !flow_run_ready(run, 1000.));
            assert(flow_host_resume(request) == FLOW_SUSPENDED_RESULT);
            pin_value value = { .type = PIN_HANDLE,
                                .type_name = "ecs.entity",
                                .u64 = 100 + index };
            assert(!flow_host_complete(request, 0, value));
            assert(!flow_host_complete(request, 2, value));
            assert(!flow_host_complete(request, 1, (pin_value){ .type = PIN_I32, .i = 42 }));
            pin_value invalid = value;
            invalid.type_name = "wrong.entity";
            assert(!flow_host_complete(request, 1, invalid));
            if (stepping == 1) {
                assert(!flow_run_pause(run));
            }
            assert(flow_host_complete(request, no_output && index == 1 ? -1 : 1, value));
            assert(!flow_host_complete(request, 1, value));
            checkpoint(&expected, mode, &at, run, instance, result);
            result = flow_host_resume(request);
            if (stepping == 5) {
                flow_host_release(request);
                capture.requests[index] = NULL;
            }
        }
        assert(at == acount(expected));
        assert(flow_run_status(run) == (failure ? FLOW_FAILED : FLOW_COMPLETED));
        assert(instance->variables[1].i == !failure);
        assert(capture.calls == (failure == -1 ? 0 : failure ? failure : 3));
        if (mode) {
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        }
        flow_run_free(run);
        for (int i = 0; i < acount(capture.requests); i++) {
            if (!capture.requests[i]) {
                continue;
            }
            assert(!flow_host_run(capture.requests[i]));
            assert(!flow_host_complete(capture.requests[i], -1, (pin_value){ 0 }));
        }
        whole_async_clear(&capture);
        behavior_instance_destroy(instance);
    }
    f->code = code;
    f->inspection_code = inspection_code;
    f->dispatch = dispatch;
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    behavior_asset_free(asset);
}

static void late_inspection()
{
    behavior_asset *asset = whole_async_fixture();
    test_flow_snapshot expected = { 0 };
    for (int mode = 0; mode < 3; mode++) {
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            instance,
            (behavior_backend[]){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(instance, false);
        whole_async_capture capture = { 0 };
        assert(!behavior_instance_bind_service(instance, "test.async", &capture));
        pin_value arg = { .type = PIN_NAME, .str = "late inspection" };
        flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ &arg, 1 }, NULL);
        assert(run && behavior_instance_tick(instance, flow_run_wake_time(run)) >= 0);
        assert(acount(capture.requests) == 1);
        assert(flow_host_complete(capture.requests[0], -1, (pin_value){ 0 }));
        behavior_instance_set_trace_enabled(instance, true);
        assert(flow_host_resume(capture.requests[0]) == FLOW_SUSPENDED_RESULT);
        int result = behavior_instance_tick(instance, flow_run_wake_time(run));
        assert(result >= 0);
        test_flow_snapshot s = test_flow_snapshot_take(run, instance, result);
        if (!mode) {
            expected = s;
        }
        if (mode) {
            test_flow_snapshot_same(s, expected);
            test_flow_snapshot_free(&s);
        }
        behavior_instance_destroy(instance);
        whole_async_clear(&capture);
    }
    test_flow_snapshot_free(&expected);
    behavior_asset_free(asset);
}

static void lifecycle(behavior_backend backend)
{
    behavior_asset *asset = whole_async_fixture(), *next = whole_async_fixture();
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    assert(!behavior_instance_set_activation_pool(instance, true));
    whole_async_capture capture = { 0 };
    assert(!behavior_instance_bind_service(instance, "test.async", &capture));
    pin_value arg = { .type = PIN_NAME, .str = "captured" };
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *pooled = NULL;
    for (int iteration = 0; iteration < 6; iteration++) {
        behavior_instance_set_trace_enabled(instance, iteration % 2);
        flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ &arg, 1 }, NULL);
        assert(run && (!pooled || pooled == run));
        assert(backend == BEHAVIOR_ORACLE || (run->whole_function_executed && run->compiled));
        assert(behavior_instance_can_update(instance, next) == INSTANCE_UPDATE_BUSY);
        assert(behavior_instance_tick(instance, flow_run_wake_time(run)) >= 0);
        assert(acount(capture.requests) == 1);
        flow_host_request *request = capture.requests[0];
        assert(flow_host_run(request) == run);
        if (iteration % 3 == 0) {
            flow_host_cancel(request);
            assert(flow_run_status(run) == FLOW_IDLE);
            assert(!flow_host_complete(request, -1, (pin_value){ 0 }));
            assert(!flow_host_run(request));
        } else {
            assert(!flow_run_pause(run));
            assert(flow_host_complete(
                request,
                1,
                (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 42 }
            ));
            assert(flow_host_resume(request) >= 0);
            assert(!behavior_instance_stop_activation(instance, run));
            assert(!flow_host_complete(request, -1, (pin_value){ 0 }));
        }
        assert(instance->variables[0].u64 == 0 && instance->variables[1].i == 0);
        whole_async_clear(&capture);
        assert(!behavior_instance_release_activation(instance, run));
        pooled = run;
    }
    assert(behavior_instance_can_update(instance, next) == INSTANCE_UPDATE_READY);
    behavior_instance_update(instance, next);
    behavior_asset_free(asset);
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ &arg, 1 }, NULL);
    assert(run && behavior_instance_tick(instance, flow_run_wake_time(run)) >= 0);
    flow_host_request *request = capture.requests[0];
    behavior_instance_destroy(instance);
    assert(!flow_host_run(request));
    assert(!flow_host_complete(request, -1, (pin_value){ 0 }));
    assert(flow_host_resume(request) == FLOW_ERR_STATE);
    whole_async_clear(&capture);
    behavior_asset_free(next);
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
}

void test_compiled_async(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (int trace = 0; trace < 2; trace++) {
        for (int stepping = 0; stepping < 7; stepping++) {
            for (int failure = -1; failure < 3; failure++) {
                parity(trace, stepping, failure, false, library);
            }
            parity(trace, stepping, 0, true, library);
        }
    }
    if (library) {
        assert(!dlclose(library));
    }
    lifecycle(BEHAVIOR_ORACLE);
    lifecycle(BEHAVIOR_BYTECODE);
    lifecycle(BEHAVIOR_SLJIT);
    late_inspection();
    test_note("PASS: asynchronous commands, nested waits, arguments, errors, stepping, breakpoints, watches and traversals across whole backends\n");
}

static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int bench_async(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int order = 0; order < 4; order++) {
        int mode = (order + rotation) % 4;
        behavior_asset *asset = whole_async_fixture();
        behavior_function *f = entry(asset);
        void *code = f->code, *inspection = f->inspection_code, *dispatch = f->dispatch;
        if (mode == 2) {
            f->code = dlsym(library, "generated_async");
            assert(f->code);
            f->inspection_code = f->dispatch = NULL;
        }
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(entry(asset) == f);
        assert(!behavior_instance_set_backend(
            instance,
            (
                behavior_backend[]
            ){ BEHAVIOR_SLJIT, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_ORACLE }[mode]
        ));
        assert(!behavior_instance_set_activation_pool(instance, true));
        behavior_instance_set_trace_enabled(instance, false);
        whole_async_capture capture = { 0 };
        assert(!behavior_instance_bind_service(instance, "test.async", &capture));
        pin_value arg = { .type = PIN_NAME, .str = "benchmark input" };
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(),
                 checksum = 0, stores = 0;
        double start = 0.;
        for (int iteration = -32; iteration < count; iteration++) {
            if (!iteration) {
                stores = runtime_store_creations();
                start = seconds();
            }
            flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ &arg, 1 }, NULL);
            assert(run && (mode == 3 || (run->compiled && run->whole_function_executed)));
            int resumes = 0;
            while (flow_run_status(run) == FLOW_SUSPENDED) {
                if (flow_run_waiting_for_host(run)) {
                    int index = acount(capture.requests) - 1;
                    flow_host_request *request = capture.requests[index];
                    assert(flow_host_complete(
                        request,
                        1,
                        (pin_value){ .type = PIN_HANDLE,
                                     .type_name = "ecs.entity",
                                     .u64 = 100 + index }
                    ));
                    assert(flow_host_resume(request) >= 0);
                } else {
                    assert(behavior_instance_tick(instance, flow_run_wake_time(run)) >= 0);
                }
                assert(++resumes <= 9);
            }
            assert(flow_run_status(run) == FLOW_COMPLETED && resumes == 9 && capture.calls == 3);
            assert(instance->variables[0].u64 == 102 && instance->variables[1].i == 1);
            if (iteration >= 0) {
                checksum += instance->variables[0].u64;
            }
            whole_async_clear(&capture);
            assert(!behavior_instance_release_activation(instance, run));
        }
        double elapsed = seconds() - start;
        stores = runtime_store_creations() - stores;
        assert(
            mode == 3 ||
            (!stores && eval == eval_oracle_calls() && flow == flow_oracle_calls())
        );
        printf(
            "%s async_activation ns=%.3f checksum=%llu runtime_stores=%llu\n",
            (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C", "ORACLE" }[mode],
            elapsed * 1e9 / count,
            (unsigned long long)checksum,
            (unsigned long long)stores
        );
        behavior_instance_destroy(instance);
        f->code = code;
        f->inspection_code = inspection;
        f->dispatch = dispatch;
        behavior_asset_free(asset);
    }
    assert(!dlclose(library));
    return 0;
}
