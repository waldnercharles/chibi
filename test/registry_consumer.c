#include "../core/arena.h"
#include "../core/blueprint.h"
#include "../core/compile.h"
#include "../core/eval.h"
#include "../core/graph.h"
#include "../core/registry.h"
#include "../core/serialize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    float x;
    int y;
} ConsumerPoint;

extern const type_desc T_ConsumerPoint;
extern node_desc CONSUMER_NODE_Make_Point;
extern node_desc CONSUMER_NODE_Break_Point;

static int fails;

static void check(int ok, const char *message)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) {
        fails++;
    }
}

static int report_has_rule(const compile_report *report, int rule)
{
    for (int i = 0; i < acount(report->items); i++) {
        if (report->items[i].rule == rule) {
            return 1;
        }
    }
    return 0;
}

static void duplicate_identity_check(void)
{
    static node_desc first_node = { .name = "duplicate_node" };
    static node_desc second_node = { .name = "duplicate_node" };
    static node_desc *const first_nodes[] = { &first_node };
    static node_desc *const second_nodes[] = { &second_node };
    static const registry_pack node_packs[] = {
        { .name = "first.nodes", .nodes = first_nodes, .node_count = 1 },
        { .name = "second.nodes", .nodes = second_nodes, .node_count = 1 },
    };
    static const registry_pack *const ordered_nodes[] = { &node_packs[0],
                                                          &node_packs[1] };
    check(!registry_validate_packs(ordered_nodes, 2), "registry rejects duplicate node identities in pack order");

    static const type_desc first_type = { .name = "duplicate_type" };
    static const type_desc second_type = { .name = "duplicate_type" };
    static const type_desc *const first_types[] = { &first_type };
    static const type_desc *const second_types[] = { &second_type };
    static const registry_pack type_packs[] = {
        { .name = "first.types", .types = first_types, .type_count = 1 },
        { .name = "second.types", .types = second_types, .type_count = 1 },
    };
    static const registry_pack *const ordered_types[] = { &type_packs[0],
                                                          &type_packs[1] };
    check(!registry_validate_packs(ordered_types, 2), "registry rejects duplicate type identities in pack order");

    static const service_desc first_service = { "first_service", UINT64_C(7) };
    static const service_desc second_service = { "second_service", UINT64_C(7) };
    static const service_desc *const first_services[] = { &first_service };
    static const service_desc *const second_services[] = { &second_service };
    static const registry_pack service_packs[] = {
        { .name = "first.services", .services = first_services, .service_count = 1 },
        { .name = "second.services", .services = second_services, .service_count = 1 },
    };
    static const registry_pack *const ordered_services[] = { &service_packs[0],
                                                             &service_packs[1] };
    check(!registry_validate_packs(ordered_services, 2), "registry rejects duplicate service IDs in pack order");

    static const operator_signature first_operator = { "duplicate_operator",
                                                       PIN_I32,
                                                       PIN_I32,
                                                       PIN_I32,
                                                       0 };
    static const operator_signature second_operator = { "duplicate_operator",
                                                        PIN_I32,
                                                        PIN_I32,
                                                        PIN_I32,
                                                        0 };
    static const registry_pack operator_packs[] = {
        { .name = "first.operators", .operators = &first_operator, .operator_count = 1 },
        { .name = "second.operators", .operators = &second_operator, .operator_count = 1 },
    };
    static const registry_pack *const ordered_operators[] = { &operator_packs[0],
                                                              &operator_packs[1] };
    check(!registry_validate_packs(ordered_operators, 2), "registry rejects duplicate operator identities in pack order");
}

int main(void)
{
    registry_validate();
    duplicate_identity_check();

    const node_desc *make = registry_node_by_name("Consumer_Make_Point");
    const node_desc *brk = registry_node_by_name("Consumer_Break_Point");
    const type_desc *point = registry_type_by_name("ConsumerPoint");
    check(make == &CONSUMER_NODE_Make_Point, "consumer node lookup preserves descriptor identity");
    check(brk == &CONSUMER_NODE_Break_Point, "consumer break node is in the global catalog");
    check(point == &T_ConsumerPoint && point->field_count == 2, "consumer type lookup uses the aggregate registry");

    int found_make = 0;
    for (int i = 0; i < registry_node_count(); i++) {
        found_make |= registry_node(i) == make;
    }
    check(found_make, "consumer node is visible to registry enumeration");
    check(
        registry_node(0) && strcmp(registry_node(0)->name, "Make_Vec3") == 0 &&
            registry_node(registry_node_count() - 2) == make &&
            registry_node(registry_node_count() - 1) == brk,
        "registry enumeration preserves explicit pack order"
    );
    const service_desc *consumer_service = registry_service_by_name("ConsumerService");
    check(
        consumer_service && consumer_service->stable_id == UINT64_C(0x2ae66b95c2b59798),
        "consumer service has a stable identity"
    );
    check(consumer_service != NULL, "consumer service metadata is aggregated");
    check(registry_operator_count() > 0 && registry_operator(0) != NULL, "consumer operator metadata is aggregated");
    const operator_signature *consumer_operator = operator_resolve("consumer_add", PIN_I32, PIN_I32);
    check(
        consumer_operator && consumer_operator->result == PIN_I32,
        "consumer operator metadata participates in operator resolution"
    );

    graph split;
    graph_init(&split);
    node *split_node = graph_add_node(&split, make);
    int split_at = split_node ? graph_split_pin(&split, split_node, 1, 0) : -1;
    check(split_at >= 0 && split_node->output_count == 2, "consumer struct type drives aggregate pin splitting");
    char *split_json = graph_save_json(&split);
    graph *split_loaded = split_json ? graph_load_json(split_json) : NULL;
    int loaded_recombined = -1;
    if (split_loaded && split_loaded->nodes[0]->output_count == 2) {
        loaded_recombined = graph_recombine_pin(split_loaded, split_loaded->nodes[0], 1, 0);
    }
    check(split_json && strstr(split_json, "split_type_name"), "split pins serialize their parent nominal type");
    check(
        loaded_recombined == 0 &&
            strcmp(split_loaded->nodes[0]->output_pins[0].type_name, "ConsumerPoint") == 0,
        "serialized consumer split recombines with its original type"
    );
    int recombined = split_at >= 0
                       ? graph_recombine_pin(&split, split_node, 1, split_at)
                       : -1;
    check(
        recombined == split_at && recombined >= 0 &&
            strcmp(split_node->output_pins[recombined].type_name, "ConsumerPoint") == 0,
        "consumer struct type recombines through the aggregate lookup"
    );
    if (split_loaded) {
        graph_free(split_loaded);
        free(split_loaded);
    }
    free(split_json);
    graph_free(&split);

    blueprint *bp = blueprint_new("ConsumerCompile");
    node *compile_node = bp ? graph_add_node(bp->graph, make) : NULL;
    int spawned = compile_node != NULL;
    compile_report report;
    compile_report_init(&report);
    int compile_result = bp ? blueprint_compile(bp, &report) : -1;
    check(compile_result >= 0, "consumer node reaches the blueprint compile pass");
    check(!report_has_rule(&report, CMPL_RULE_UNKNOWN_NOMINAL_TYPE), "consumer type is known to compile validation");
    compile_report_free(&report);
    if (bp) {
        blueprint_free(bp);
    }
    check(spawned, "consumer node can be spawned into a blueprint graph");

    graph authored;
    graph_init(&authored);
    node *authored_node = graph_add_node(&authored, make);
    graph_set_pin_default(
        &authored,
        authored_node,
        0,
        0,
        &(pin_value){ .type = PIN_F32, .type_name = "float", .f = 4.5f }
    );
    graph_set_pin_default(
        &authored,
        authored_node,
        0,
        1,
        &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 7 }
    );
    char *json = graph_save_json(&authored);
    graph *loaded = json ? graph_load_json(json) : NULL;
    check(loaded && acount(loaded->nodes) == 1, "consumer node survives JSON save/load");
    check(
        loaded && loaded->nodes[0]->desc == make &&
            loaded->nodes[0]->input_pins[0].default_value.f == 4.5f &&
            loaded->nodes[0]->input_pins[1].default_value.i == 7,
        "consumer descriptor and authored values survive the same-build round "
        "trip"
    );

    if (loaded) {
        arena a;
        arena_init(&a, 256);
        eval_context eval;
        eval_context_init(&eval);
        pin_value value;
        int evaluated = graph_eval_output(loaded, loaded->nodes[0], 0, &a, &value, &eval) ==
                        0;
        ConsumerPoint *result = evaluated ? (ConsumerPoint *)value.ptr : NULL;
        check(result && result->x == 4.5f && result->y == 7, "loaded consumer Make node evaluates with its type descriptor");
        eval_context_free(&eval);
        arena_free(&a);
        graph_free(loaded);
        free(loaded);
    }
    free(json);
    graph_free(&authored);

    return fails != 0;
}
