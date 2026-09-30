#include "graph/model/registry.h"
#include "graph/nodes/set_map_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/value_array.h"
#include "test/support/report.h"
#include "test/whole_array_op_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int linear(const pin_array *a, const pin_value *v)
{
    for (int i = 0; i < a->count; i++) {
        if (pin_value_equal(&a->items[i], v)) {
            return i;
        }
    }
    return -1;
}
static pin_value member(pin_type type, int i, arena *a)
{
    pin_value v = { .type = type, .type_name = type == PIN_HANDLE ? "ecs.entity" : NULL };
    switch (type) {
        case PIN_BOOL: v.b = i % 2; break;
        case PIN_I8: v.i8 = i; break;
        case PIN_U8: v.u8 = i; break;
        case PIN_I16: v.i16 = i; break;
        case PIN_U16: v.u16 = i; break;
        case PIN_I32: v.i = i; break;
        case PIN_U32: v.u = i; break;
        case PIN_I64: v.i64 = INT64_MIN + i; break;
        case PIN_U64:
        case PIN_HANDLE: v.u64 = UINT64_MAX - i; break;
        case PIN_ENUM:
            v.e = i;
            v.type_name = "SearchKind";
            break;
        case PIN_F32: v.f = i == 7 ? NAN : i; break;
        case PIN_F64: v.d = i == 7 ? NAN : i; break;
        case PIN_STR:
        case PIN_NAME: {
            char *s = arena_alloc(a, 32);
            snprintf(s, 32, "member %d", i);
            v.str = s;
            break;
        }
        default: assert(false);
    }
    return v;
}
void test_value_index()
{
    for (pin_type type = PIN_BOOL; type <= PIN_ENUM; type++) {
        if (type == PIN_STRUCT) {
            continue;
        }
        for (int count = 8; count <= 128; count *= 2) {
            arena a = { 0 }, owned = { 0 };
            pin_array input = { .element_type = type, .count = count };
            input.items = arena_alloc(&a, (size_t)count * sizeof *input.items);
            for (int i = 0; i < count; i++) {
                input.items[i] = member(type, i / 2, &a);
            }
            pin_array_index(&a, &input);
            assert((input.buckets != NULL) == (count >= 64));
            for (int i = 0; i <= count; i++) {
                pin_value needle = member(type, i, &a);
                assert(pin_array_find(&input, &needle) == linear(&input, &needle));
                needle.type_name = "different nominal type";
                assert(pin_array_find(&input, &needle) == linear(&input, &needle));
            }
            pin_value made = registry_node_by_name("Make Set")->fn(input.items, count, &owned);
            pin_array *set = made.ptr;
            int unique = 0;
            for (int i = 0; i < count; i++) {
                pin_array prefix = input;
                prefix.count = i;
                if (linear(&prefix, &input.items[i]) >= 0) {
                    continue;
                }
                /* NaN is deliberately non-reflexive, but must retain its place. */
                bool nan = type == PIN_F32
                             ? isnan(input.items[i].f)
                             : type == PIN_F64 && isnan(input.items[i].d);
                assert(nan || pin_value_equal(&set->items[unique], &input.items[i]));
                unique++;
            }
            assert(set->count == unique);
            arena_free(&a);
            assert(set->count == unique); /* Owned values survive their inputs. */
            for (int i = 0; i < unique; i++) {
                assert(pin_array_find(set, &set->items[i]) == linear(set, &set->items[i]));
            }
            arena_free(&owned);
        }
    }
    /* Signed zero shares a key; NaNs still never compare equal. */
    arena a = { 0 };
    pin_array *zero = pin_array_allocate(&a, PIN_F64, NULL, 128);
    zero->items[0].d = -0.0;
    zero->items[1].d = NAN;
    pin_array_index(&a, zero);
    assert(pin_array_find(zero, &(pin_value){ .type = PIN_F64, .d = 0.0 }) == 0);
    assert(pin_array_find(zero, &(pin_value){ .type = PIN_F64, .d = NAN }) == -1);
    for (int i = 0; i < zero->count; i++) {
        zero->items[i].d = NAN;
    }
    pin_value made = registry_node_by_name("Make Set")->fn(zero->items, zero->count, &a);
    pin_array *nans = made.ptr;
    assert(nans->count == zero->count && nans->buckets);
    for (unsigned i = 0; i <= nans->bucket_mask; i++) {
        assert(!nans->buckets[i]);
    }
    assert(pin_array_find(nans, &nans->items[0]) == -1);
    arena_free(&a);
    /* Large Sets enter the indexed branch through real compiled activations,
     * persistent copies and waits, using the native or WASM default backend. */
    for (int miss = 0; miss < 2; miss++) {
        behavior_asset *asset = whole_array_op_fixture(ARRAY_OP_INTS, ARRAY_OP_SET_CONTAINS, true);
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        const compile_report *report = behavior_program_report(asset->program);
        bool diagnostic = false;
        for (int i = 0; i < acount(report->items); i++) {
            const compile_diagnostic *d = &report->items[i];
            if (d->rule != CMPL_RULE_CONTAINER) {
                continue;
            }
            graph *g = behavior_asset_find_graph(asset, d->graph_id);
            node *n = graph_node_by_id(g, d->node_id);
            assert(n && d->pin_id == n->input_pins[0].id);
            diagnostic |= d->severity == CMPL_NOTE;
        }
        assert(diagnostic);
        behavior_instance_set_trace_enabled(bi, false);
        arena input = { 0 };
        pin_value args[5];
        whole_array_op_arguments(ARRAY_OP_INTS, ARRAY_OP_SET_CONTAINS, args, &input);
        pin_array *large = pin_array_allocate(&input, PIN_I32, "int", 4096);
        for (int i = 0; i < large->count; i++) {
            large->items[i].i = i;
        }
        args[0].ptr = large;
        args[2].i = 4095 + miss;
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 5 }, NULL);
        assert(run && flow_run_status(run) == FLOW_SUSPENDED);
        arena_free(&input);
        assert(!flow_run_tick_at(run, .25));
        assert(bi->variables[2].b == !miss);
        pin_array *saved = bi->variables[0].ptr;
        assert(saved->buckets && saved->items[4095].i == 4095);
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }
    test_note("PASS: adaptive ordered Set indexes, equality, duplicates, ownership and compiled waits\n");
}
