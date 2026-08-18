/* test/self_check.c -- constructs small graphs in code and asserts outputs.
 *
 * Exercises: generated Make/Break/call nodes (from registry.c), the
 * hand-written constant escape hatch, per-run memoization, the cycle guard,
 * and arena-backed struct values.
 */
#include "app/demo_graph.h"
#include "core/arena.h"
#include "core/core.h"
#include "core/eval.h"
#include "core/graph.h"
#include "core/nodes.h"
#include "core/pin_fmt.h"
#include "core/registry.h"
#include "core/serialize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern node_desc *const REGISTRY_NODES[];
extern const int REGISTRY_NODE_COUNT;

static node_desc *find_node(const char *name)
{
    for (int i = 0; i < REGISTRY_NODE_COUNT; i++)
        if (strcmp(REGISTRY_NODES[i]->name, name) == 0)
            return REGISTRY_NODES[i];
    return NULL;
}

static int fails;

static void check(int ok, const char *msg)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) fails++;
}

/* Hand-written constant node: no inputs, one output, eval pre-sets nothing
 * (the caller sets output[0]). This is the escape hatch: a node_desc authored
 * directly in C, not emitted by reflect. */
static pin_spec g_const_out[] = {
    { "value", PIN_F32, "float" },
};
static node_desc g_const_desc = {
    .name = "const_f32",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = g_const_out,
    .eval = node_eval_const,
};

/* Hand-written counting node for the memo check: increments once per eval. */
static int g_count;
static void eval_counter(node *n, arena *a)
{
    (void)a;
    g_count++;
    n->outputs[0].type = PIN_F32;
    n->outputs[0].type_name = "float";
    n->outputs[0].f = (float)g_count;
}
static pin_spec g_counter_out[] = {
    { "count", PIN_F32, "float" },
};
static node_desc g_counter_desc = {
    .name = "counter",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = g_counter_out,
    .eval = eval_counter,
};

static node *make_const(graph *g, float v)
{
    node *n = graph_add_node(g, &g_const_desc);
    n->outputs[0].type = PIN_F32;
    n->outputs[0].type_name = "float";
    n->outputs[0].f = v;
    return n;
}

int main(void)
{
    arena a;
    arena_init(&a, 4096);

    graph g;
    graph_init(&g);

    node_desc *make = find_node("Make_Vec3");
    node_desc *brk = find_node("Break_Vec3");
    node_desc *add = find_node("add_floats");
    check(make && brk && add, "registry exposes Make_Vec3, Break_Vec3, add_floats");

    /* 1. two constants -> add call node */
    node *c1 = make_const(&g, 2.0f);
    node *c2 = make_const(&g, 3.0f);
    node *a1 = graph_add_node(&g, add);
    check(graph_connect(&g, c1, 0, a1, 0) == 0, "connect c1 -> add.a");
    check(graph_connect(&g, c2, 0, a1, 1) == 0, "connect c2 -> add.b");

    pin_value sum;
    check(graph_eval_output(&g, a1, 0, &a, &sum) == 0, "eval add node");
    check(sum.type == PIN_F32 && sum.f == 5.0f, "2.0 + 3.0 == 5.0");

    /* 2. Make(Vec3) -> Break(Vec3) round trip */
    graph_eval_reset(&g);
    arena_reset(&a);

    node *mk = graph_add_node(&g, make);
    node *br = graph_add_node(&g, brk);
    node *cx = make_const(&g, 1.0f);
    node *cy = make_const(&g, 2.0f);
    node *cz = make_const(&g, 3.0f);
    graph_connect(&g, cx, 0, mk, 0);
    graph_connect(&g, cy, 0, mk, 1);
    graph_connect(&g, cz, 0, mk, 2);
    graph_connect(&g, mk, 0, br, 0);

    pin_value oy;
    check(graph_eval_output(&g, br, 1, &a, &oy) == 0, "eval Break(Vec3).y");
    check(oy.type == PIN_F32 && oy.f == 2.0f, "Make(Vec3) -> Break(Vec3): y == 2.0");

    /* 3. memo: one eval across two reads */
    graph_eval_reset(&g);
    arena_reset(&a);
    g_count = 0;
    node *ctr = graph_add_node(&g, &g_counter_desc);
    pin_value v1, v2;
    check(graph_eval_output(&g, ctr, 0, &a, &v1) == 0, "eval counter (1st read)");
    check(graph_eval_output(&g, ctr, 0, &a, &v2) == 0, "eval counter (2nd read)");
    check(g_count == 1, "memo: eval ran once for two reads");
    check(v1.f == 1.0f && v2.f == 1.0f, "memo: both reads see the same value");

    /* 4. cycle guard */
    graph_eval_reset(&g);
    arena_reset(&a);
    node *A = graph_add_node(&g, add);
    node *B = graph_add_node(&g, add);
    graph_connect(&g, A, 0, B, 0); /* A.out -> B.a */
    graph_connect(&g, B, 0, A, 0); /* B.out -> A.a */
    pin_value junk;
    check(graph_eval_output(&g, A, 0, &a, &junk) != 0, "cycle detected (eval returns error)");

    /* 5. remove a node and its edges */
    int nb = g.node_count;
    int eb = g.edge_count;
    check(graph_remove_node(&g, A) == 0, "remove node A");
    check(g.node_count == nb - 1, "node count drops by one");
    check(g.edge_count == eb - 2, "A's two edges dropped");
    check(graph_remove_node(&g, A) == -1, "removing a missing node fails");

    /* 6. graph_remove_edge drops exactly the first matching edge */
    {
        graph g6;
        graph_init(&g6);
        node *rc1 = make_const(&g6, 4.0f);
        node *rc2 = make_const(&g6, 5.0f);
        node *radd = graph_add_node(&g6, add);
        check(graph_connect(&g6, rc1, 0, radd, 0) == 0, "remove_edge: c1 -> add.a");
        check(graph_connect(&g6, rc2, 0, radd, 0) == 0, "remove_edge: c2 -> add.a (same pin)");
        check(graph_connect(&g6, rc2, 0, radd, 1) == 0, "remove_edge: c2 -> add.b");
        check(g6.edge_count == 3, "remove_edge: three edges");
        check(graph_remove_edge(&g6, rc1, 0, radd, 0) == 0, "remove_edge: c1 -> add.a removed");
        check(g6.edge_count == 2, "remove_edge: two edges remain");
        check(graph_remove_edge(&g6, rc1, 0, radd, 0) == -1, "remove_edge: missing edge fails");

        arena a6;
        arena_init(&a6, 4096);
        pin_value out;
        check(graph_eval_output(&g6, radd, 0, &a6, &out) == 0, "remove_edge: eval after removal");
        check(out.type == PIN_F32 && out.f == 10.0f, "remove_edge: 5.0 + 5.0 == 10.0 (c2 drives a and b)");
        arena_free(&a6);
        graph_free(&g6);
    }

    /* 6. serialization round trip: constants + add + edges */
    {
        graph g2;
        graph_init(&g2);
        const node_desc *cc = registry_node_by_name("const_f32");
        check(cc != NULL, "registry exposes const_f32");

        node *rc1 = graph_add_node(&g2, cc);
        rc1->outputs[0].type = PIN_F32;
        rc1->outputs[0].type_name = "float";
        rc1->outputs[0].f = 2.0f;
        node *rc2 = graph_add_node(&g2, cc);
        rc2->outputs[0].type = PIN_F32;
        rc2->outputs[0].type_name = "float";
        rc2->outputs[0].f = 3.0f;
        node *radd = graph_add_node(&g2, add);
        check(graph_connect(&g2, rc1, 0, radd, 0) == 0, "roundtrip: c1 -> add.a");
        check(graph_connect(&g2, rc2, 0, radd, 1) == 0, "roundtrip: c2 -> add.b");

        rc1->x = -220.0f;
        rc1->y = 40.0f;
        radd->x = 100.0f;
        radd->y = -30.0f;

        char *json = graph_save_json(&g2);
        check(json != NULL, "roundtrip: serialize to JSON");
        if (json) {
            printf("\n--- serialized graph ---\n%s--- end ---\n", json);

            graph *g3 = graph_load_json(json);
            check(g3 != NULL, "roundtrip: load from JSON");
            if (g3) {
                check(g3->node_count == 3, "roundtrip: node count survives");
                check(g3->edge_count == 2, "roundtrip: edge count survives");
                check(g3->nodes[0]->x == -220.0f && g3->nodes[0]->y == 40.0f, "roundtrip: node position survives");
                check(g3->nodes[2]->x == 100.0f && g3->nodes[2]->y == -30.0f, "roundtrip: second node position survives");

                arena a2;
                arena_init(&a2, 4096);
                pin_value out;
                check(graph_eval_output(g3, g3->nodes[2], 0, &a2, &out) == 0, "roundtrip: eval loaded add node");
                check(out.type == PIN_F32 && out.f == 5.0f, "roundtrip: 2.0 + 3.0 == 5.0");
                arena_free(&a2);
                graph_free(g3);
            }
            free(json);
        }
        graph_free(&g2);
    }

    /* 7. string constant round trip */
    {
        graph g4;
        graph_init(&g4);
        const node_desc *cs = registry_node_by_name("const_str");
        check(cs != NULL, "registry exposes const_str");
        node *s1 = graph_add_node(&g4, cs);
        s1->outputs[0].type = PIN_STR;
        s1->outputs[0].type_name = "const char *";
        s1->outputs[0].str = "hello, world";

        char *json = graph_save_json(&g4);
        check(json != NULL, "string const: serialize");
        if (json) {
            graph *g5 = graph_load_json(json);
            check(g5 && g5->node_count == 1, "string const: load");
            if (g5) {
                check(
                    g5->nodes[0]->outputs[0].type == PIN_STR &&
                        strcmp(g5->nodes[0]->outputs[0].str, "hello, world") == 0,
                    "string const: value round-trips"
                );
                graph_free(g5);
            }
            free(json);
        }
        graph_free(&g4);
    }

    /* 8. load error handling: malformed JSON, missing fields, unknown type,
     * out-of-range pin. */
    {
        const char *bad[] = {
            "{ not json",
            "{\"nodes\": []}",
            "{\"nodes\":[{\"type\":\"nope\"}],\"edges\":[]}",
            "{\"nodes\":[{\"type\":\"add_floats\",\"consts\":{\"in9\":1}}],"
            "\"edges\":[]}",
        };
        check(graph_load_json(bad[0]) == NULL && graph_last_error()[0] != '\0', "malformed JSON rejected");
        check(graph_load_json(bad[1]) == NULL && graph_last_error()[0] != '\0', "missing edges array rejected");
        check(graph_load_json(bad[2]) == NULL && strstr(graph_last_error(), "unknown"), "unknown node type rejected");
        check(graph_load_json(bad[3]) == NULL && strstr(graph_last_error(), "range"), "out-of-range pin index rejected");
    }

    /* 9. pin_value_to_string formatting */
    {
        pin_value fv = { .type = PIN_F32, .f = 2.5f };
        pin_value five = { .type = PIN_F32, .f = 5.0f };
        pin_value sv = { .type = PIN_STR, .str = "hello" };
        pin_value bv = { .type = PIN_BOOL, .b = true };
        pin_value iv = { .type = PIN_I32, .i = -42 };

        const char *s_f32 = pin_value_to_string(&fv);
        const char *s_five = pin_value_to_string(&five);
        const char *s_str = pin_value_to_string(&sv);
        const char *s_bool = pin_value_to_string(&bv);
        printf("  fmt samples: f32 2.5 -> \"%s\", f32 5.0 -> \"%s\", str -> \"%s\", bool -> \"%s\"\n", s_f32, s_five, s_str, s_bool);

        check(strcmp(s_f32, "2.5") == 0, "fmt: f32 2.5 -> \"2.5\"");
        check(strcmp(s_five, "5") == 0, "fmt: f32 5.0 -> \"5\"");
        check(strcmp(s_str, "hello") == 0, "fmt: str -> \"hello\"");
        check(strcmp(s_bool, "true") == 0, "fmt: bool true -> \"true\"");
        check(strcmp(pin_value_to_string(&iv), "-42") == 0, "fmt: i32 -42 -> \"-42\"");
    }

    /* 10. seed graph evaluates to 5.0 */
    {
        graph gs;
        graph_init(&gs);
        check(demo_graph_seed(&gs) == 0, "seed graph builds");

        const node_desc *adddesc = registry_node_by_name("add_floats");
        node *addn = NULL;
        for (int i = 0; i < gs.node_count; i++)
            if (gs.nodes[i]->desc == adddesc) {
                addn = gs.nodes[i];
                break;
            }
        check(addn != NULL, "seed graph has an add_floats node");

        if (addn) {
            arena as;
            arena_init(&as, 4096);
            pin_value seed_sum;
            check(graph_eval_output(&gs, addn, 0, &as, &seed_sum) == 0, "seed graph evals");
            check(seed_sum.type == PIN_F32 && seed_sum.f == 5.0f, "seed graph: 2.0 + 3.0 == 5.0");
            arena_free(&as);
        }
        graph_free(&gs);
    }

    /* 11. subset serialization: only selected nodes + their inter-edges */
    {
        graph g2;
        graph_init(&g2);
        const node_desc *cc = registry_node_by_name("const_f32");

        node *c1 = graph_add_node(&g2, cc);
        c1->outputs[0].type = PIN_F32;
        c1->outputs[0].type_name = "float";
        c1->outputs[0].f = 2.0f;
        node *c2 = graph_add_node(&g2, cc);
        c2->outputs[0].type = PIN_F32;
        c2->outputs[0].type_name = "float";
        c2->outputs[0].f = 3.0f;
        node *ad = graph_add_node(&g2, add);
        graph_connect(&g2, c1, 0, ad, 0);
        graph_connect(&g2, c2, 0, ad, 1);
        c1->x = 10.0f;
        c1->y = 20.0f;
        c2->x = 30.0f;
        c2->y = 40.0f;
        ad->x = 50.0f;
        ad->y = 60.0f;

        node *subset[] = { c1, ad }; /* c2 is outside the subset */
        char *json = graph_save_nodes_json(&g2, subset, 2);
        check(json != NULL, "subset: serialize");
        if (json) {
            printf("\n--- serialized subset ---\n%s--- end ---\n", json);

            graph *g3 = graph_load_json(json);
            check(g3 != NULL, "subset: load");
            if (g3) {
                check(g3->node_count == 2, "subset: node count is 2");
                check(g3->edge_count == 1, "subset: crossing edge dropped");
                check(g3->nodes[0]->x == 10.0f && g3->nodes[0]->y == 20.0f, "subset: node 0 position");
                check(g3->nodes[1]->x == 50.0f && g3->nodes[1]->y == 60.0f, "subset: node 1 position");
                check(g3->nodes[0]->outputs[0].f == 2.0f, "subset: const value survives");
                graph_free(g3);
            }
            free(json);
        }
        graph_free(&g2);
    }

    /* 12. append: paste a serialized subgraph with a position offset */
    {
        graph g4;
        graph_init(&g4);
        const node_desc *cc = registry_node_by_name("const_f32");

        node *c1 = graph_add_node(&g4, cc);
        c1->outputs[0].type = PIN_F32;
        c1->outputs[0].type_name = "float";
        c1->outputs[0].f = 2.0f;
        node *c2 = graph_add_node(&g4, cc);
        c2->outputs[0].type = PIN_F32;
        c2->outputs[0].type_name = "float";
        c2->outputs[0].f = 3.0f;
        node *ad = graph_add_node(&g4, add);
        graph_connect(&g4, c1, 0, ad, 0);
        graph_connect(&g4, c2, 0, ad, 1);
        c1->x = 10.0f;
        c1->y = 20.0f;
        ad->x = 50.0f;
        ad->y = 60.0f;

        node *subset[] = { c1, ad };
        char *json = graph_save_nodes_json(&g4, subset, 2);
        check(json != NULL, "append: serialize source subset");
        if (json) {
            graph g5;
            graph_init(&g5);
            check(g5.node_count == 0, "append: fresh graph starts empty");
            check(graph_append_json(&g5, json, 100.0f, 200.0f) == 0, "append: returns 0");
            check(g5.node_count == 2, "append: node count");
            check(g5.edge_count == 1, "append: edge count");
            check(g5.nodes[0]->x == 110.0f && g5.nodes[0]->y == 220.0f, "append: node 0 offset");
            check(g5.nodes[1]->x == 150.0f && g5.nodes[1]->y == 260.0f, "append: node 1 offset");
            check(g5.nodes[0]->outputs[0].f == 2.0f, "append: const survives");

            int n_before = g5.node_count;
            check(graph_append_json(&g5, "{ not json", 0.0f, 0.0f) == -1, "append: bad json fails");
            check(g5.node_count == n_before, "append: graph untouched on error");

            graph_free(&g5);
            free(json);
        }
        graph_free(&g4);
    }

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");

    graph_free(&g);
    arena_free(&a);
    return fails ? 1 : 0;
}
