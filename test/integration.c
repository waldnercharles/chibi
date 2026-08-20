/* test/integration.c -- headless pico_unit regression suite for the
 * editor<->core interaction path.
 *
 * self_check covers core invariants in isolation. This suite drives the exact
 * API sequence the editor uses (UI-55/56 edit choke point, spawn, undo/redo
 * snapshot swap, preview instance lifecycle) so a heap-corruption bug of the
 * class reported against the editor (the id_table DYNA-cookie assertion,
 * ADR 0010) is caught headlessly: the target links core + demo + pico_unit
 * only, with no cute/ImGui.
 *
 * TODO(stretch, headless editor): exercise editor_spawn/undo/redo/preview
 * through the real editor by splitting editor_new() into pure-state init and
 * ImGui font init (a headless constructor), moving the ImGui-free state into
 * its own TU or behind an #ifdef so a test can compile editor/ without
 * dcimgui. Deferred because the integration target intentionally links no
 * cute/ImGui; the choke-point sequence below covers the same corruption
 * surface at the core API level.
 */
#define PICO_UNIT_IMPLEMENTATION
#include "../app/demo_graph.h"
#include "../core/blueprint.h"
#include "../core/instance.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- invariants under test ------------------------------------------------ */

/* ADR 0010 / ID-4 / BP-9: the dense id_table must map every live node back to
 * itself, every occupied slot must be a node still present in g->nodes (never
 * a stale pointer to freed memory, which is what the DYNA-cookie assertion
 * trips on), and every pin ID must resolve back to the same pin_spec. */
static int id_table_consistent(const graph *g)
{
    if (!g) return 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (graph_node_by_id(g, n->id) != n) return 0;
    }
    for (int id = 1; id < acount(g->id_table); id++) {
        node *n = g->id_table[id];
        if (!n) continue;
        int in_nodes = 0;
        for (int i = 0; i < acount(g->nodes); i++)
            if (g->nodes[i] == n) {
                in_nodes = 1;
                break;
            }
        if (!in_nodes) return 0;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        for (int p = 0; p < n->input_count; p++) {
            int is_out = 0, idx = -1;
            if (graph_pin_by_id(n, n->input_pins[p].id, &is_out, &idx) !=
                &n->input_pins[p])
                return 0;
            if (is_out || idx != p) return 0;
        }
        for (int p = 0; p < n->output_count; p++) {
            int is_out = 0, idx = -1;
            if (graph_pin_by_id(n, n->output_pins[p].id, &is_out, &idx) !=
                &n->output_pins[p])
                return 0;
            if (!is_out || idx != p) return 0;
        }
    }
    return 1;
}

/* ID-4: a retired ID must resolve to nothing, never to an unrelated node. */
static int retired_resolves_null(const graph *g, const uint64_t *retired, int count)
{
    for (int i = 0; i < count; i++)
        if (graph_node_by_id(g, retired[i]) != NULL) return 0;
    return 1;
}

/* RUN-20 / EXEC-19: the definition's mutation guard balances: mutable exactly
 * when no instance is attached. */
static int guard_balanced(const blueprint *bp)
{ return bp && bp->graph && bp->graph->flow_pause_count == 0; }

static int incident_edge_count(const graph *g, const node *n)
{
    int count = 0;
    for (int i = 0; i < acount(g->edges); i++)
        if (g->edges[i].from_id == n->id || g->edges[i].to_id == n->id) count++;
    return count;
}

/* --- suite: graph mutation integrity under stress -------------------------- */

TEST_CASE(graph_stress_add_remove_reindex)
{
    const node_desc *real = registry_node_by_name("Real");
    const node_desc *addf = registry_node_by_name("add_floats");
    REQUIRE(real != NULL);
    REQUIRE(addf != NULL);

    graph g;
    graph_init(&g);
    uint64_t retired[512];
    int retired_count = 0;

    for (int round = 0; round < 12; round++) {
        int before = acount(g.nodes);
        int edges_before = acount(g.edges);
        /* add a batch: literal pairs feeding add_floats (BP-9/10/11 churn). */
        for (int i = 0; i < 16; i++) {
            node *c1 = graph_add_node(&g, real);
            node *c2 = graph_add_node(&g, real);
            node *a = graph_add_node(&g, addf);
            REQUIRE(c1 && c2 && a);
            REQUIRE(graph_connect(&g, c1, 0, a, 0) == 0);
            REQUIRE(graph_connect(&g, c2, 0, a, 1) == 0);
            REQUIRE(id_table_consistent(&g));
        }
        REQUIRE(acount(g.edges) == edges_before + 16 * 2);
        /* remove roughly half of this round's nodes; their edges vanish. */
        for (int i = acount(g.nodes) - 1; i >= before; i -= 2) {
            node *n = g.nodes[i];
            int incident = incident_edge_count(&g, n);
            int ecount = acount(g.edges);
            if (retired_count < (int)(sizeof retired / sizeof retired[0]))
                retired[retired_count++] = n->id;
            REQUIRE(graph_remove_node(&g, n) == 0);
            REQUIRE(acount(g.edges) == ecount - incident); /* BP-9 */
            REQUIRE(graph_remove_node(&g, n) == -1);       /* BP-10 */
            REQUIRE(id_table_consistent(&g));
        }
        REQUIRE(retired_resolves_null(&g, retired, retired_count)); /* ID-4 */
        graph_reindex(&g); /* rebuild the dense table from scratch */
        REQUIRE(id_table_consistent(&g));
        REQUIRE(retired_resolves_null(&g, retired, retired_count)); /* ID-4 */
    }
    /* the dense table must never hold a pointer to a node that was freed. */
    REQUIRE(id_table_consistent(&g));
    graph_free(&g);
    REQUIRE(g.nodes == NULL && g.id_table == NULL); /* graph_free zeroes */
    return true;
}

/* --- suite: blueprint / instance lifecycle --------------------------------- */

static blueprint *lifecycle_bp(
    blueprint_variable **health_out,
    blueprint_variable **counter_out,
    node **begin_ev_out
)
{
    blueprint *bp = blueprint_new("Lifecycle");
    if (!bp) return NULL;
    blueprint_variable *health = blueprint_add_variable(
        bp,
        "Health",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 100 },
        "Stats"
    );
    blueprint_variable *counter = blueprint_add_variable(
        bp,
        "Counter",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 1 },
        NULL
    );
    node *begin_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
    graph_set_event_name(bp->graph, begin_ev, "Begin");
    node *set = blueprint_add_set_node(bp, health->id);
    graph_set_pin_default(bp->graph, set, 0, 1, &(pin_value){ .type = PIN_I32, .i = 50 });
    graph_connect_exec(bp->graph, begin_ev, 0, set, 0, 0);
    if (!health || !counter || !begin_ev || !set) {
        blueprint_free(bp);
        return NULL;
    }
    *health_out = health;
    *counter_out = counter;
    *begin_ev_out = begin_ev;
    return bp;
}

TEST_CASE(blueprint_guard_and_variables)
{
    for (int round = 0; round < 3; round++) {
        blueprint_variable *health, *counter;
        node *begin_ev;
        blueprint *bp = lifecycle_bp(&health, &counter, &begin_ev);
        REQUIRE(bp != NULL);
        REQUIRE(begin_ev != NULL);
        REQUIRE(guard_balanced(bp)); /* RUN-20: mutable before any instance */

        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        REQUIRE(bi != NULL);
        REQUIRE(!guard_balanced(bp)); /* EXEC-19: instance holds the guard */

        pin_value v;
        REQUIRE(blueprint_instance_get_variable(bi, health->id, &v) == 0 && v.i == 100);
        flow_error err = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Begin", (flow_inputs){ 0 }, &err);
        REQUIRE(act != NULL && flow_run_status(act) == FLOW_COMPLETED);
        REQUIRE(blueprint_instance_get_variable(bi, health->id, &v) == 0 && v.i == 50);

        /* VAR-11: a variable no node writes survives separate activations. */
        REQUIRE(
            blueprint_instance_set_variable(bi, counter->id, &(pin_value){ .type = PIN_I32, .i = 41 }) == 0
        );
        act = blueprint_instance_dispatch(bi, "Begin", (flow_inputs){ 0 }, &err);
        REQUIRE(act != NULL);
        REQUIRE(blueprint_instance_tick(bi, 0.016) == FLOW_OK);
        REQUIRE(blueprint_instance_get_variable(bi, counter->id, &v) == 0 && v.i == 41);
        REQUIRE(blueprint_instance_get_variable(bi, health->id, &v) == 0 && v.i == 50);

        /* mutations are rejected while the instance is attached (EXEC-19). */
        REQUIRE(graph_add_node(bp->graph, registry_node_by_name("Bool")) == NULL);
        REQUIRE(
            blueprint_add_variable(bp, "New", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, NULL) == NULL
        );

        /* RUN-15: destroy cancels activations and releases the guard. */
        REQUIRE(acount(bi->activations) >= 1);
        blueprint_instance_destroy(bi);
        REQUIRE(guard_balanced(bp));
        REQUIRE(blueprint_mutable(bp));

        /* RUN-18: two instances of one definition stay independent. */
        blueprint_instance *bi1 = blueprint_instance_create(bp, NULL, NULL, NULL);
        blueprint_instance *bi2 = blueprint_instance_create(bp, NULL, NULL, NULL);
        REQUIRE(bi1 && bi2);
        REQUIRE(!guard_balanced(bp));
        REQUIRE(
            blueprint_instance_set_variable(bi1, health->id, &(pin_value){ .type = PIN_I32, .i = 7 }) == 0
        );
        REQUIRE(blueprint_instance_get_variable(bi1, health->id, &v) == 0 && v.i == 7);
        REQUIRE(blueprint_instance_get_variable(bi2, health->id, &v) == 0 && v.i == 100);
        blueprint_instance_destroy(bi1);
        REQUIRE(!guard_balanced(bp)); /* one instance still holds the guard */
        blueprint_instance_destroy(bi2);
        REQUIRE(guard_balanced(bp));

        /* create -> dispatch -> tick -> destroy -> recreate repeatedly. */
        for (int i = 0; i < 3; i++) {
            bi = blueprint_instance_create(bp, NULL, NULL, NULL);
            REQUIRE(bi != NULL);
            act = blueprint_instance_dispatch(bi, "Begin", (flow_inputs){ 0 }, &err);
            REQUIRE(act != NULL);
            (void)blueprint_instance_tick(bi, 0.016);
            blueprint_instance_destroy(bi);
            REQUIRE(guard_balanced(bp));
        }
        blueprint_free(bp);
    }
    return true;
}

TEST_CASE(blueprint_destroy_cancels_suspended)
{
    /* RUN-15/EXEC-17: destroying an instance with a suspended Delay
     * activation cancels it and releases the definition guard. */
    blueprint *bp = blueprint_new("Cancel");
    node *wait_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
    graph_set_event_name(bp->graph, wait_ev, "Wait");
    node *delay = graph_add_node(bp->graph, registry_node_by_name("Delay"));
    graph_set_pin_default(bp->graph, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 5.0 });
    graph_connect_exec(bp->graph, wait_ev, 0, delay, 0, 0);
    REQUIRE(wait_ev && delay);

    blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
    REQUIRE(bi != NULL);
    flow_error err = { 0 };
    flow_run *act = blueprint_instance_dispatch(bi, "Wait", (flow_inputs){ 0 }, &err);
    REQUIRE(act != NULL && flow_run_status(act) == FLOW_SUSPENDED);
    REQUIRE(acount(bi->activations) == 1);
    blueprint_instance_destroy(bi); /* cancels the suspended activation */
    REQUIRE(guard_balanced(bp));
    blueprint_free(bp);
    return true;
}

TEST_CASE(blueprint_undo_snapshot_swap)
{
    /* The editor undo/redo path (ed_adopt_blueprint): snapshot the blueprint,
     * mutate, swap back to the snapshot, then forward again. IDs must stay
     * stable and the id_table must stay consistent across every swap. */
    for (int round = 0; round < 3; round++) {
        blueprint_variable *health, *counter;
        node *begin_ev;
        blueprint *bp = lifecycle_bp(&health, &counter, &begin_ev);
        REQUIRE(bp != NULL);
        uint64_t ev_id = begin_ev->id;
        uint64_t health_id = health->id;

        char *snapshot = blueprint_save_json(bp);
        REQUIRE(snapshot != NULL);

        /* authoring mutation (spawn + connect), as editor_spawn does. */
        node *n = graph_add_node(bp->graph, registry_node_by_name("Real"));
        REQUIRE(n != NULL);
        uint64_t n_id = n->id;
        graph_set_pin_default(bp->graph, n, 0, 0, &(pin_value){ .type = PIN_F64, .d = 9.0 });
        REQUIRE(id_table_consistent(bp->graph));

        /* "undo": the edit choke point guarantees no instance is attached. */
        blueprint *reverted = blueprint_load_json(snapshot);
        REQUIRE(reverted != NULL);
        blueprint_free(bp);
        bp = reverted;
        REQUIRE(id_table_consistent(bp->graph));
        REQUIRE(graph_node_by_id(bp->graph, ev_id) != NULL); /* ID-1 stable */

        /* the loaded graph reuses no retired/allocated id (ID-2). */
        uint64_t expected = bp->graph->id_counter;
        node *fresh = graph_add_node(bp->graph, registry_node_by_name("Bool"));
        REQUIRE(fresh != NULL && fresh->id == expected);
        REQUIRE(id_table_consistent(bp->graph));

        /* "redo": swap forward to the mutated snapshot. */
        char *forward = blueprint_save_json(bp);
        REQUIRE(forward != NULL);
        blueprint *redone = blueprint_load_json(forward);
        REQUIRE(redone != NULL);
        blueprint_free(bp);
        bp = redone;
        REQUIRE(id_table_consistent(bp->graph));
        REQUIRE(graph_node_by_id(bp->graph, n_id) != NULL); /* n survived */

        /* the swapped-in definition still hosts a working instance. */
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        REQUIRE(bi != NULL);
        flow_error err = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Begin", (flow_inputs){ 0 }, &err);
        REQUIRE(act != NULL && flow_run_status(act) == FLOW_COMPLETED);
        pin_value v;
        REQUIRE(blueprint_instance_get_variable(bi, health_id, &v) == 0 && v.i == 50);
        blueprint_instance_destroy(bi);
        REQUIRE(guard_balanced(bp));

        blueprint_free(bp);
        free(snapshot);
        free(forward);
    }
    return true;
}

/* --- suite: regression for the reported editor crash ----------------------- */

TEST_CASE(blueprint_paste_allocates_fresh_ids)
{
    /* ID-5: copy/paste (graph_save_nodes_json + graph_append_json, the
     * editor's Duplicate/Copy-Paste path) must allocate fresh IDs for the
     * pasted entities and never reissue a retired or live ID. */
    blueprint *bp = blueprint_new("PasteBP");
    REQUIRE(bp != NULL);
    REQUIRE(demo_graph_seed(bp->graph) == 0);
    node *src[2] = { bp->graph->nodes[0], bp->graph->nodes[2] }; /* c1 + add_floats */
    uint64_t orig_ids[2] = { src[0]->id, src[1]->id };

    char *sel = graph_save_nodes_json(bp->graph, src, 2);
    REQUIRE(sel != NULL);
    for (int paste = 0; paste < 3; paste++) {
        int before = acount(bp->graph->nodes);
        REQUIRE(graph_append_json(bp->graph, sel, 24.0f, 24.0f) == 0);
        REQUIRE(acount(bp->graph->nodes) == before + 2);
        REQUIRE(id_table_consistent(bp->graph));
        /* the pasted nodes carry fresh ids distinct from the originals. */
        for (int i = before; i < before + 2; i++) {
            node *p = bp->graph->nodes[i];
            REQUIRE(p->id != orig_ids[0] && p->id != orig_ids[1]);
            REQUIRE(graph_node_by_id(bp->graph, p->id) == p);
            for (int q = 0; q < before; q++)
                REQUIRE(p->id != bp->graph->nodes[q]->id);
        }
        REQUIRE(graph_node_by_id(bp->graph, orig_ids[0]) == src[0]);
        REQUIRE(graph_node_by_id(bp->graph, orig_ids[1]) == src[1]);
    }
    free(sel);
    blueprint_free(bp);
    return true;
}

TEST_CASE(regression_edit_choke_point_spawn)
{
    /* The editor flow that crashed with the id_table DYNA-cookie assertion:
     * seed the demo graph, attach a preview instance, dispatch Begin + tick
     * (app/main.c demo loop), destroy the instance at the edit choke point
     * (UI-55/56), take the undo snapshot, then spawn a node. Ten cycles. */
    const node_desc *boold = registry_node_by_name("Bool");
    REQUIRE(boold != NULL);

    for (int iter = 0; iter < 10; iter++) {
        blueprint *bp = blueprint_new("RegressionBP");
        REQUIRE(bp != NULL);
        REQUIRE(demo_graph_seed(bp->graph) == 0); /* Real literals + add_floats */
        node *ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        REQUIRE(ev != NULL);
        REQUIRE(graph_set_event_name(bp->graph, ev, "Begin") == 0);
        REQUIRE(id_table_consistent(bp->graph));

        /* editor_preview_create: the instance acquires the mutation guard. */
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        REQUIRE(bi != NULL);
        REQUIRE(!guard_balanced(bp));

        /* host loop: dispatch Begin once per generation, then tick. */
        flow_error err = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Begin", (flow_inputs){ 0 }, &err);
        REQUIRE(act != NULL && flow_run_status(act) == FLOW_COMPLETED);
        REQUIRE(blueprint_instance_tick(bi, 0.016) == FLOW_OK);

        /* ed_begin_authoring_edit: destroy the preview instance (UI-56). */
        blueprint_instance_destroy(bi);
        REQUIRE(guard_balanced(bp));
        REQUIRE(blueprint_mutable(bp));

        /* undo snapshot (ed_snapshot) and release it, as the editor does. */
        char *json = blueprint_save_json(bp);
        REQUIRE(json != NULL);
        free(json);

        /* editor_spawn: the next node must take the exact next counter value
         * and the id_table must stay consistent (this is the assertion the
         * crash tripped). */
        uint64_t expected = bp->graph->id_counter;
        node *n = graph_add_node(bp->graph, boold);
        REQUIRE(n != NULL);
        REQUIRE(n->id == expected); /* ID-2: monotonic, never reused */
        REQUIRE(graph_node_by_id(bp->graph, n->id) == n);
        REQUIRE(id_table_consistent(bp->graph));

        /* the seeded nodes still resolve (BP-9/10/11: removal path never ran
         * here, so every id remains live). */
        REQUIRE(acount(bp->graph->nodes) == 5); /* 4 seeded + 1 spawned */
        for (int i = 0; i < acount(bp->graph->nodes); i++) {
            node *live = bp->graph->nodes[i];
            REQUIRE(graph_node_by_id(bp->graph, live->id) == live);
        }
        REQUIRE(id_table_consistent(bp->graph));

        /* a second spawn after save/load churn stays consistent too. */
        char *json2 = blueprint_save_json(bp);
        REQUIRE(json2 != NULL);
        blueprint *reloaded = blueprint_load_json(json2);
        REQUIRE(reloaded != NULL);
        REQUIRE(id_table_consistent(reloaded->graph));
        uint64_t expected2 = reloaded->graph->id_counter;
        node *n2 = graph_add_node(reloaded->graph, boold);
        REQUIRE(n2 != NULL && n2->id == expected2);
        REQUIRE(id_table_consistent(reloaded->graph));
        blueprint_free(reloaded);
        free(json2);

        blueprint_free(bp);
    }
    return true;
}

/* --- main ------------------------------------------------------------------ */

static void graph_integrity_suite(void)
{ RUN_TEST_CASE(graph_stress_add_remove_reindex); }

static void blueprint_lifecycle_suite(void)
{
    RUN_TEST_CASE(blueprint_guard_and_variables);
    RUN_TEST_CASE(blueprint_destroy_cancels_suspended);
    RUN_TEST_CASE(blueprint_undo_snapshot_swap);
}

static void regression_suite(void)
{
    RUN_TEST_CASE(blueprint_paste_allocates_fresh_ids);
    RUN_TEST_CASE(regression_edit_choke_point_spawn);
}

int main(void)
{
    RUN_TEST_SUITE(graph_integrity_suite);
    RUN_TEST_SUITE(blueprint_lifecycle_suite);
    RUN_TEST_SUITE(regression_suite);
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
