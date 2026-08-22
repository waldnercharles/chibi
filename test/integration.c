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
#include "../editor/arrange.h"
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
    if (!g) {
        return 0;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (graph_node_by_id(g, n->id) != n) {
            return 0;
        }
    }
    for (int id = 1; id < acount(g->id_table); id++) {
        node *n = g->id_table[id];
        if (!n) {
            continue;
        }
        int in_nodes = 0;
        for (int i = 0; i < acount(g->nodes); i++) {
            if (g->nodes[i] == n) {
                in_nodes = 1;
                break;
            }
        }
        if (!in_nodes) {
            return 0;
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        for (int p = 0; p < n->input_count; p++) {
            int is_out = 0, idx = -1;
            if (graph_pin_by_id(n, n->input_pins[p].id, &is_out, &idx) !=
                &n->input_pins[p]) {
                return 0;
            }
            if (is_out || idx != p) {
                return 0;
            }
        }
        for (int p = 0; p < n->output_count; p++) {
            int is_out = 0, idx = -1;
            if (graph_pin_by_id(n, n->output_pins[p].id, &is_out, &idx) !=
                &n->output_pins[p]) {
                return 0;
            }
            if (!is_out || idx != p) {
                return 0;
            }
        }
    }
    return 1;
}

/* ID-4: a retired ID must resolve to nothing, never to an unrelated node. */
static int retired_resolves_null(const graph *g, const uint64_t *retired, int count)
{
    for (int i = 0; i < count; i++) {
        if (graph_node_by_id(g, retired[i]) != NULL) {
            return 0;
        }
    }
    return 1;
}

/* RUN-20 / EXEC-19: the definition's mutation guard balances: mutable exactly
 * when no instance is attached. */
static int guard_balanced(const blueprint *bp)
{
    return bp && bp->graph && bp->graph->flow_pause_count == 0;
}

static int incident_edge_count(const graph *g, const node *n)
{
    int count = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].from_id == n->id || g->edges[i].to_id == n->id) {
            count++;
        }
    }
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
            if (retired_count < (int)(sizeof retired / sizeof retired[0])) {
                retired[retired_count++] = n->id;
            }
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
    if (!bp) {
        return NULL;
    }
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
            for (int q = 0; q < before; q++) {
                REQUIRE(p->id != bp->graph->nodes[q]->id);
            }
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

/* --- auto arrange (UI-37, ADR 0015) --------------------------------------- */

/* The arrange module is ImGui-free on purpose, so the readability claim is
 * measurable here rather than only visible on screen. */
static arrange_box arrange_test_box(int rows)
{
    const float row = 22.0f;
    const float title = 26.0f;
    const float pad = 8.0f;
    return (arrange_box){ 160.0f, title + pad + (float)rows * row + pad, title, row, pad };
}

/* Wires leaving one node are ordered by that node's pin stack, so none of them
 * should cross another wire from the same node. Reported against --flow-demo:
 * a Sequence fed its Then1 to a Branch and its Then2 to a Sequence, and the
 * two exec wires were drawn over each other because the Branch also had a
 * second producer below it, which dragged it under its sibling. */
static bool arrange_fan_out_follows_pin_order(const arrange_graph *g, const arrange_point *pos)
{
    for (int i = 0; i < g->link_count; i++) {
        for (int j = 0; j < g->link_count; j++) {
            const arrange_link *a = &g->links[i];
            const arrange_link *b = &g->links[j];
            if (a->from != b->from || a->from_pin >= b->from_pin) {
                continue;
            }
            const arrange_box *ab = &g->boxes[a->to];
            const arrange_box *bb = &g->boxes[b->to];
            float a_y = pos[a->to].y + ab->title_height + ab->pad +
                        ((float)a->to_pin + 0.5f) * ab->row_height;
            float b_y = pos[b->to].y + bb->title_height + bb->pad +
                        ((float)b->to_pin + 0.5f) * bb->row_height;
            /* An earlier pin must not arrive below a later pin's target. */
            if (a_y > b_y) {
                return false;
            }
        }
    }
    return true;
}

/* The smallest vertical gap between two nodes sharing a column. */
static float arrange_min_column_gap(const arrange_graph *g, const arrange_point *pos)
{
    float smallest = 1.0e9f;
    for (int i = 0; i < g->box_count; i++) {
        for (int j = 0; j < g->box_count; j++) {
            if (i == j || pos[i].x != pos[j].x || pos[i].y > pos[j].y) {
                continue;
            }
            float gap = pos[j].y - (pos[i].y + g->boxes[i].height);
            if (gap >= 0.0f && gap < smallest) {
                smallest = gap;
            }
        }
    }
    return smallest;
}

TEST_CASE(auto_arrange_untangles_a_graph)
{
    enum
    {
        NODES = 9
    };
    arrange_box boxes[NODES];
    for (int i = 0; i < NODES; i++) {
        boxes[i] = arrange_test_box(2);
    }
    /* Three columns of three, wired crosswise on purpose, and connected, so
     * the ordering stage has real work and columns hold several nodes. */
    arrange_link links[] = {
        { 0, 5, 0, 0 }, { 1, 4, 0, 0 }, { 2, 3, 0, 0 }, { 0, 3, 1, 1 },
        { 1, 5, 1, 1 }, { 3, 8, 0, 0 }, { 4, 7, 0, 0 }, { 5, 6, 0, 0 },
        { 3, 7, 1, 1 }, { 4, 6, 1, 1 },
    };
    arrange_graph g = { boxes, NODES, links, (int)(sizeof links / sizeof *links) };
    arrange_point seed[NODES];
    for (int i = 0; i < NODES; i++) {
        seed[i] = (arrange_point){ 0.0f, (float)i * 30.0f };
    }
    arrange_point pos[NODES];
    for (int i = 0; i < NODES; i++) {
        pos[i] = seed[i];
    }

    arrange_score before = arrange_measure(&g, pos);
    arrange_layout(&g, pos);
    arrange_score after = arrange_measure(&g, pos);

    /* Hard guarantees: nodes never overlap, and no wire is drawn through a
     * node it does not connect to. */
    REQUIRE(after.node_overlaps == 0);
    REQUIRE(after.wire_node_hits == 0);
    REQUIRE(before.wire_node_hits > 0); /* the seed layout really was tangled */

    /* Absolute bounds, not merely "no worse than the tangle we started from":
     * a relative check passes even when the crossing-reduction stages are
     * disabled outright. Ordering reaches 6 here; 9 is what skipping it
     * gives. */
    REQUIRE(after.wire_crossings <= 6);
    REQUIRE(after.wire_crossings < before.wire_crossings);
    REQUIRE(arrange_cost(after) < arrange_cost(before));

    /* Columns stay separated rather than merely non-overlapping. */
    REQUIRE(arrange_min_column_gap(&g, pos) >= 24.0f);

    /* Pin-order fan-out is a weighted preference, not a guarantee: on this
     * deliberately crosswise graph it loses to removing more crossings
     * elsewhere, so it is asserted in the targeted test below, not here. */

    /* Wires run left to right, so the graph reads in execution order. */
    for (int i = 0; i < (int)(sizeof links / sizeof *links); i++) {
        REQUIRE(pos[links[i].from].x < pos[links[i].to].x);
    }

    /* Same input, same output: no run-to-run drift. */
    arrange_point again[NODES];
    for (int i = 0; i < NODES; i++) {
        again[i] = seed[i];
    }
    arrange_layout(&g, again);
    for (int i = 0; i < NODES; i++) {
        REQUIRE(again[i].x == pos[i].x);
        REQUIRE(again[i].y == pos[i].y);
    }
    return true;
}

/* A wire spanning several columns must not be drawn through the nodes in
 * between. This is the case the corridor and refinement stages exist for. */
TEST_CASE(auto_arrange_routes_long_wires_clear_of_nodes)
{
    enum
    {
        NODES = 8
    };
    arrange_box boxes[NODES];
    for (int i = 0; i < NODES; i++) {
        boxes[i] = arrange_test_box(3);
    }
    arrange_link links[NODES];
    int link_count = 0;
    for (int i = 0; i + 1 < NODES; i++) {
        links[link_count++] = (arrange_link){ i, i + 1, 0, 0 };
    }
    links[link_count++] = (arrange_link){ 0, NODES - 1, 1, 1 };

    arrange_graph g = { boxes, NODES, links, link_count };
    arrange_point pos[NODES];
    for (int i = 0; i < NODES; i++) {
        pos[i] = (arrange_point){ 0.0f, (float)i * 30.0f };
    }
    arrange_layout(&g, pos);
    arrange_score after = arrange_measure(&g, pos);
    REQUIRE(after.wire_node_hits == 0);
    REQUIRE(after.wire_crossings == 0);
    REQUIRE(after.node_overlaps == 0);
    return true;
}

/* A quality budget on a fixed mid-size graph. The bounds are what the full
 * pipeline reaches; they exist to catch a stage silently stopping to pull its
 * weight, which a "no worse than the tangle we started from" check cannot. */
TEST_CASE(auto_arrange_meets_a_quality_budget)
{
    enum
    {
        NODES = 24
    };
    arrange_box boxes[NODES];
    for (int i = 0; i < NODES; i++) {
        boxes[i] = arrange_test_box(2 + (i % 3));
    }
    /* A fixed forward-fanning graph: deterministic, and dense enough that the
     * ordering, coordinate, and refinement stages all have work to do. */
    unsigned state = 7u;
    arrange_link links[48];
    int link_count = 0;
    for (int i = 0; i < NODES; i++) {
        state = state * 1103515245u + 12345u;
        int fan = 1 + (int)((state >> 16 & 0x7fff) % 3);
        for (int k = 0; k < fan && link_count < 48; k++) {
            state = state * 1103515245u + 12345u;
            int to = i + 1 + (int)((state >> 16 & 0x7fff) % 5);
            state = state * 1103515245u + 12345u;
            int from_pin = (int)((state >> 16 & 0x7fff) % 2);
            state = state * 1103515245u + 12345u;
            int to_pin = (int)((state >> 16 & 0x7fff) % 2);
            if (to >= NODES) {
                continue;
            }
            links[link_count++] = (arrange_link){ i, to, from_pin, to_pin };
        }
    }
    arrange_graph g = { boxes, NODES, links, link_count };
    arrange_point pos[NODES];
    for (int i = 0; i < NODES; i++) {
        pos[i] = (arrange_point){ 0.0f, (float)i * 30.0f };
    }
    arrange_score before = arrange_measure(&g, pos);
    arrange_layout(&g, pos);
    arrange_score after = arrange_measure(&g, pos);

    REQUIRE(before.wire_node_hits > 100); /* the seed layout is a pile */
    REQUIRE(after.node_overlaps == 0);
    REQUIRE(after.wire_node_hits <= 3);
    REQUIRE(after.wire_crossings <= 20);
    return true;
}

/* Connected pins should end up level, so a wire between them is drawn flat.
 * This is what the coordinate stage buys: the crossing counts alone cannot
 * tell a readable column layout from a stair-stepped one. */
TEST_CASE(auto_arrange_levels_connected_pins)
{
    enum
    {
        NODES = 4
    };
    arrange_box boxes[NODES];
    for (int i = 0; i < NODES; i++) {
        boxes[i] = arrange_test_box(4);
    }
    /* A chain wired through different pin rows, so level pins require the
     * boxes themselves to be offset vertically. */
    arrange_link links[] = { { 0, 1, 0, 3 }, { 1, 2, 2, 0 }, { 2, 3, 3, 1 } };
    int link_count = (int)(sizeof links / sizeof *links);
    arrange_graph g = { boxes, NODES, links, link_count };
    arrange_point pos[NODES];
    for (int i = 0; i < NODES; i++) {
        pos[i] = (arrange_point){ 0.0f, (float)i * 30.0f };
    }
    arrange_layout(&g, pos);
    for (int i = 0; i < link_count; i++) {
        const arrange_box *from = &boxes[links[i].from];
        const arrange_box *to = &boxes[links[i].to];
        float from_y = pos[links[i].from].y + from->title_height + from->pad +
                       ((float)links[i].from_pin + 0.5f) * from->row_height;
        float to_y = pos[links[i].to].y + to->title_height + to->pad +
                     ((float)links[i].to_pin + 0.5f) * to->row_height;
        float off = from_y > to_y ? from_y - to_y : to_y - from_y;
        REQUIRE(off <= 1.0f);
    }
    return true;
}

/* A node with no inputs belongs beside the node it feeds, not in the leftmost
 * column it would be allowed. This is the rule that keeps a pure data node
 * next to its consumer instead of stranded at the far left. */
TEST_CASE(auto_arrange_places_producers_beside_consumers)
{
    enum
    {
        NODES = 5
    };
    arrange_box boxes[NODES];
    for (int i = 0; i < NODES; i++) {
        boxes[i] = arrange_test_box(2);
    }
    /* A chain 0->1->2->3, plus a lone producer 4 feeding the chain's end. */
    arrange_link links[] = {
        { 0, 1, 0, 0 },
        { 1, 2, 0, 0 },
        { 2, 3, 0, 0 },
        { 4, 3, 0, 1 },
    };
    arrange_graph g = { boxes, NODES, links, (int)(sizeof links / sizeof *links) };
    arrange_point pos[NODES];
    for (int i = 0; i < NODES; i++) {
        pos[i] = (arrange_point){ 0.0f, (float)i * 30.0f };
    }
    arrange_layout(&g, pos);

    REQUIRE(pos[0].x < pos[1].x);
    REQUIRE(pos[1].x < pos[2].x);
    REQUIRE(pos[2].x < pos[3].x);
    /* The producer sits in the column just before its consumer, which here is
     * the same column as node 2, not back at node 0's. */
    REQUIRE(pos[4].x == pos[2].x);
    REQUIRE(pos[4].x > pos[0].x);
    return true;
}

TEST_CASE(auto_arrange_keeps_fan_out_in_pin_order)
{
    enum
    {
        EVENT = 0,
        SEQUENCE,
        DELAY,
        SIBLING,
        BRANCH,
        BOOL,
        DOWNSTREAM,
        NODES
    };
    arrange_box boxes[NODES];
    for (int i = 0; i < NODES; i++) {
        boxes[i] = arrange_test_box(3);
    }
    /* The Branch takes a second wire from the Bool, which is what pulled it
     * below its sibling and inverted the Sequence's fan-out. */
    arrange_link links[] = {
        { EVENT, SEQUENCE, 0, 0 },   { SEQUENCE, DELAY, 0, 0 },
        { SEQUENCE, BRANCH, 1, 0 },  { SEQUENCE, SIBLING, 2, 0 },
        { DELAY, DOWNSTREAM, 0, 0 }, { BOOL, DOWNSTREAM, 0, 1 },
        { BOOL, BRANCH, 0, 1 },
    };
    arrange_graph g = { boxes, NODES, links, (int)(sizeof links / sizeof *links) };
    arrange_point pos[NODES];
    for (int i = 0; i < NODES; i++) {
        pos[i] = (arrange_point){ 0.0f, (float)i * 30.0f };
    }
    arrange_layout(&g, pos);

    REQUIRE(arrange_fan_out_follows_pin_order(&g, pos));
    /* The two siblings share a column and the earlier pin sits above. */
    REQUIRE(pos[BRANCH].x == pos[SIBLING].x);
    REQUIRE(pos[BRANCH].y < pos[SIBLING].y);
    REQUIRE(arrange_measure(&g, pos).node_overlaps == 0);
    return true;
}

/* The --flow-demo graph, as a whole: it must come out with no crossings.
 *
 * A strict column grid cannot do this. The Sequence's pin stack puts Delay,
 * Branch, Sequence down the next column, the Bool must sit in an earlier
 * column than the Branch it feeds, and its wire lands on the Branch's *second*
 * row - so with every node pinned to its column's x, one crossing is forced.
 * The fix is that a node's x does not have to equal its rank's x: the trailing
 * Sequence is a leaf, so it can sit in the gutter between two columns, which
 * vacates the band the Bool's wire needs. Reported from --flow-demo. */
/* Node boxes as the editor measures them at its default font: 13px line, so a
 * 19px pin row and a 25px title bar. The widths are the real measured ones,
 * because this case turns on the Bool being wide relative to its column - with
 * every box the same width there is no forced crossing to fix. */
static arrange_box arrange_editor_box(float width, int rows)
{
    const float row = 19.0f;
    const float title = 25.0f;
    const float pad = 8.0f;
    return (arrange_box){ width, title + pad + (float)rows * row + pad, title, row, pad };
}

TEST_CASE(auto_arrange_reaches_the_optimum_on_the_flow_demo)
{
    enum
    {
        EVENT = 0,   /* id 1  */
        SEQUENCE,    /* id 3  */
        DELAY,       /* id 7  */
        BRANCH_MAIN, /* id 11 */
        CONDITION,   /* id 16, the Bool */
        FORLOOP,     /* id 19 */
        BODY,        /* id 26 */
        COMPLETED,   /* id 30 */
        SIBLING,     /* id 34 */
        BRANCH_ALT,  /* id 38 */
        NODES
    };
    arrange_box boxes[NODES] = {
        arrange_editor_box(72.0f, 1),  arrange_editor_box(107.0f, 3),
        arrange_editor_box(265.0f, 2), arrange_editor_box(142.0f, 2),
        arrange_editor_box(248.0f, 1), arrange_editor_box(353.0f, 3),
        arrange_editor_box(107.0f, 2), arrange_editor_box(107.0f, 2),
        arrange_editor_box(107.0f, 2), arrange_editor_box(142.0f, 2),
    };
    arrange_link links[] = {
        { EVENT, SEQUENCE, 0, 0 },        { SEQUENCE, DELAY, 0, 0 },
        { SEQUENCE, BRANCH_ALT, 1, 0 },   { SEQUENCE, SIBLING, 2, 0 },
        { DELAY, BRANCH_MAIN, 0, 0 },     { BRANCH_MAIN, FORLOOP, 0, 0 },
        { CONDITION, BRANCH_MAIN, 0, 1 }, { CONDITION, BRANCH_ALT, 0, 1 },
        { FORLOOP, BODY, 0, 0 },          { FORLOOP, COMPLETED, 2, 0 },
    };
    arrange_graph g = { boxes, NODES, links, (int)(sizeof links / sizeof *links) };
    arrange_point pos[NODES];
    for (int i = 0; i < NODES; i++) {
        pos[i] = (arrange_point){ 0.0f, (float)i * 40.0f };
    }
    arrange_layout(&g, pos);
    arrange_score after = arrange_measure(&g, pos);

    REQUIRE(after.node_overlaps == 0);
    REQUIRE(after.wire_node_hits == 0);
    REQUIRE(after.wire_crossings == 0);
    REQUIRE(arrange_fan_out_follows_pin_order(&g, pos));

    /* The leaf left its column to buy that, landing in the gutter: clear of
     * the producer it hangs off, and short of the column it came from. */
    REQUIRE(pos[SIBLING].x > pos[SEQUENCE].x + boxes[SEQUENCE].width);
    REQUIRE(pos[SIBLING].x < pos[DELAY].x);
    return true;
}

/* A cycle has no left-to-right ranking, so the layout must still terminate,
 * still separate the nodes, and not run wires through them. */
TEST_CASE(auto_arrange_handles_cycles_and_islands)
{
    enum
    {
        NODES = 9
    };
    arrange_box boxes[NODES];
    for (int i = 0; i < NODES; i++) {
        boxes[i] = arrange_test_box(2);
    }
    arrange_link links[NODES];
    int link_count = 0;
    for (int i = 0; i < 5; i++) { /* a 5-cycle */
        links[link_count++] = (arrange_link){ i, (i + 1) % 5, 0, 0 };
    }
    links[link_count++] = (arrange_link){ 5, 6, 0, 0 }; /* a separate island */
    links[link_count++] = (arrange_link){ 7, 7, 0, 0 }; /* a self loop */
    /* node 8 has no wires at all */

    arrange_graph g = { boxes, NODES, links, link_count };
    arrange_point pos[NODES];
    for (int i = 0; i < NODES; i++) {
        pos[i] = (arrange_point){ 0.0f, 0.0f };
    }
    arrange_layout(&g, pos);
    arrange_score after = arrange_measure(&g, pos);
    REQUIRE(after.node_overlaps == 0);
    REQUIRE(after.wire_node_hits == 0);
    return true;
}

/* --- main ------------------------------------------------------------------ */

static void graph_integrity_suite(void)
{
    RUN_TEST_CASE(graph_stress_add_remove_reindex);
}

static void blueprint_lifecycle_suite(void)
{
    RUN_TEST_CASE(blueprint_guard_and_variables);
    RUN_TEST_CASE(blueprint_destroy_cancels_suspended);
    RUN_TEST_CASE(blueprint_undo_snapshot_swap);
}

static void auto_arrange_suite(void)
{
    RUN_TEST_CASE(auto_arrange_untangles_a_graph);
    RUN_TEST_CASE(auto_arrange_routes_long_wires_clear_of_nodes);
    RUN_TEST_CASE(auto_arrange_meets_a_quality_budget);
    RUN_TEST_CASE(auto_arrange_levels_connected_pins);
    RUN_TEST_CASE(auto_arrange_places_producers_beside_consumers);
    RUN_TEST_CASE(auto_arrange_keeps_fan_out_in_pin_order);
    RUN_TEST_CASE(auto_arrange_reaches_the_optimum_on_the_flow_demo);
    RUN_TEST_CASE(auto_arrange_handles_cycles_and_islands);
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
    RUN_TEST_SUITE(auto_arrange_suite);
    RUN_TEST_SUITE(regression_suite);
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
