#include "graph/compiler/behavior_function_internal.h"
#include "graph/model/registry.h"
#include "graph/nodes/set_map_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/value_array.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static behavior_asset *fixture(pin_type type)
{
    behavior_asset *a = behavior_asset_new("Set lookup");
    graph *g = behavior_asset_add_function(a, "Contains", true);
    pin_spec set = { .name = "Set", .type = type, .container = PIN_CONTAINER_SET };
    pin_spec item = { .name = "Item", .type = type };
    pin_spec result = { .name = "Found", .type = PIN_BOOL };
    assert(graph_add_pin(g, g->nodes[0], true, &set) == 0);
    assert(graph_add_pin(g, g->nodes[0], true, &item) == 1);
    assert(graph_add_pin(g, g->nodes[1], false, &result) == 0);
    node *n = graph_add_node(g, &NODE_SetContains);
    assert(!graph_connect_data(g, g->nodes[0], 0, n, 0));
    assert(!graph_connect_data(g, g->nodes[0], 1, n, 1));
    assert(!graph_connect_data(g, n, 0, g->nodes[1], 0));
    assert(behavior_program_publish(a));
    return a;
}
static const behavior_function *function(behavior_asset *a)
{
    graph *g = a->graphs[1];
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, g),
        g->nodes[0]
    );
    assert(f);
    return f;
}
void emit_container_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int i = 0; i < 2; i++) {
        behavior_asset *a = fixture(i ? PIN_STR : PIN_I32);
        behavior_function_write_c(function(a), out, i ? "set_string" : "set_integer");
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
/* The old ordered deduplication, including the same deep result snapshot. */
static pin_value flat_make(const pin_value *items, int count, arena *storage)
{
    pin_array out = { .element_type = items[0].type, .type_name = items[0].type_name };
    out.items = arena_alloc(storage, (size_t)count * sizeof *out.items);
    for (int i = 0; i < count; i++) {
        if (pin_array_find(&out, &items[i]) < 0) {
            out.items[out.count++] = items[i];
        }
    }
    return (pin_value){ .type = out.element_type,
                        .container = PIN_CONTAINER_SET,
                        .ptr = pin_array_snapshot_to_arena(storage, &out) };
}
static void construction(const pin_array *input, int kind, int rotation)
{
    if (input->count > 4096) {
        return;
    }
    for (int order = 0; order < 2; order++) {
        bool indexed = (order + rotation) % 2;
        arena storage = { 0 };
        double start = 0;
        int checksum = 0;
        int repeats = input->count <= 64 ? 1000 : 8;
        const node_desc *make = registry_node_by_name("Make Set");
        for (int j = -2; j < repeats; j++) {
            if (!j) {
                start = seconds();
            }
            arena_reset(&storage);
            pin_value v = indexed
                            ? make->fn(input->items, input->count, &storage)
                            : flat_make(input->items, input->count, &storage);
            const pin_array *out = v.ptr;
            assert(out->count == input->count);
            assert(
                pin_value_equal(&out->items[out->count - 1], &input->items[input->count - 1])
            );
            if (j >= 0) {
                checksum += out->count;
            }
        }
        printf(
            "MAKE kind=%d size=%d storage=%s us=%.3f checksum=%d\n",
            kind,
            input->count,
            indexed ? "auto" : "flat",
            (seconds() - start) * 1e6 / repeats,
            checksum
        );
        arena_free(&storage);
    }
}
int bench_containers(const char *path, int repeats, int rotation)
{
    assert(repeats > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    const int sizes[] = { 8, 64, 4096, 20000 };
    for (int kind = 0; kind < 2; kind++) {
        pin_type type = kind ? PIN_STR : PIN_I32;
        behavior_asset *a = fixture(type);
        const behavior_function *f = function(a);
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))dlsym(library, kind ? "set_string" : "set_integer")
        };
        assert(entries[0] && entries[1]);
        for (int size = 0; size < 4; size++) {
            arena input = { 0 }, storage = { 0 };
            pin_array *array = pin_array_allocate(&input, type, NULL, sizes[size]);
            for (int i = 0; i < array->count; i++) {
                if (!kind) {
                    array->items[i].i = i;
                } else {
                    char *s = arena_alloc(&input, 32);
                    snprintf(s, 32, "member %d", i);
                    array->items[i].str = s;
                }
            }
            construction(array, kind, rotation);
            double start = seconds();
            pin_array_index(&input, array);
            printf(
                "INDEX kind=%d size=%d build_us=%.3f bytes=%zu\n",
                kind,
                array->count,
                (seconds() - start) * 1e6,
                array->buckets ? ((size_t)array->bucket_mask + 1) * sizeof(int) : 0
            );
            pin_array flat = *array;
            flat.buckets = NULL;
            for (int order = 0; order < 6; order++) {
                int variant = (order + rotation) % 6, mode = variant / 2;
                bool indexed = variant % 2;
                pin_value args[] = { { .type = type,
                                       .container = PIN_CONTAINER_SET,
                                       .ptr = indexed ? array : &flat },
                                     { .type = type } };
                pin_value result = { 0 };
                behavior_function_context ctx = { .arguments = args,
                                                  .argument_count = 2,
                                                  .results = &result,
                                                  .budget = 100 };
                behavior_function_activation activation = { 0 };
                behavior_function_activation_init(f, &activation);
                ctx.activation = &activation;
                behavior_function_bind_storage(&ctx, &storage);
                behavior_function_prepare_host(f, &ctx);
                int iterations = mode == 2 && array->count > 64 ? repeats / 32 : repeats;
                if (iterations < 1) {
                    iterations = 1;
                }
                uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
                uint64_t blocks = 0, data = 0, vm = 0;
                int checksum = 0;
                double begin = 0;
                for (int i = -16; i < iterations; i++) {
                    bool hit = i % 2 == 0;
                    args[1] = array->items[array->count - 1];
                    if (!hit) {
                        if (kind) {
                            args[1].str = "absent";
                        } else {
                            args[1].i = -1;
                        }
                    }
                    if (!i) {
                        blocks = arena_block_allocations();
                        data = behavior_function_data_allocations();
                        vm = behavior_function_vm_allocations();
                        begin = seconds();
                    }
                    arena_reset(&storage);
                    ctx.steps = 0;
                    int status = mode == 2 ? behavior_function_run(f, &ctx, false)
                                           : entries[mode](&ctx);
                    assert(!status && result.b == hit);
                    if (i >= 0) {
                        checksum += result.b;
                    }
                }
                double ns = (seconds() - begin) * 1e9 / iterations;
                assert(
                    blocks == arena_block_allocations() &&
                    data == behavior_function_data_allocations() &&
                    vm == behavior_function_vm_allocations()
                );
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                printf(
                    "CONTAINER kind=%d size=%d backend=%s storage=%s ns=%.3f "
                    "iterations=%d checksum=%d\n",
                    kind,
                    array->count,
                    (const char *[]){ "SLJIT", "C", "BYTECODE" }[mode],
                    indexed ? "auto" : "flat",
                    ns,
                    iterations,
                    checksum
                );
                behavior_function_release_host(&ctx);
                behavior_function_activation_free(&activation);
            }
            arena_free(&storage);
            arena_free(&input);
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    return 0;
}
