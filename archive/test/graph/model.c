#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/pin_conv.h"
#include "graph/nodes/conv_nodes.h"
#include "support/array.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Hand-authored descriptors: a float literal, an executable sink and a
   two-input float sum, none of them from the registry. */
static pin_spec const_out[] = { { "value", PIN_F32, "number" } };
static node_desc const_desc = { .name = "const_f32",
                                .output_count = 1,
                                .outputs = const_out,
                                .pure_kind = NODE_PURE_CONST,
                                .display_class = NODE_DISPLAY_LITERAL };
static pin_spec sink_inputs[] = { { "Exec", PIN_FLOW, NULL, PIN_EXEC },
                                  { "Index", PIN_I32, "int", PIN_DATA } };
static pin_spec sink_outputs[] = { { "Then", PIN_FLOW, NULL, PIN_EXEC } };
static node_desc sink_desc = { .name = "Sink",
                               .input_count = 2,
                               .output_count = 1,
                               .inputs = sink_inputs,
                               .outputs = sink_outputs,
                               .flow_kind = NODE_FLOW_CUSTOM };
static pin_spec sum_inputs[] = { { "a", PIN_F32, "number" },
                                 { "b", PIN_F32, "number" } };
static pin_spec sum_outputs[] = { { "result", PIN_F32, "number" } };
static node_desc sum_desc = { .name = "test_add",
                              .input_count = 2,
                              .output_count = 1,
                              .inputs = sum_inputs,
                              .outputs = sum_outputs,
                              .pure_kind = NODE_PURE_CALL };

static graph g;

static void setup()
{
    graph_init(&g);
}

static void cleanup()
{
    graph_free(&g);
}

static node *make_const(graph *to, float v)
{
    node *n = graph_add_node(to, &const_desc);
    pin_value authored = { .type = PIN_F32, .type_name = "number", .f = v };
    graph_set_pin_default(to, n, 1, 0, &authored);
    return n;
}

static node *add(graph *to, const char *type)
{
    return graph_add_node(to, graph_palette_node(type));
}

static bool input_matches(
    const graph *from,
    const node *target,
    int pin,
    const node *expected,
    int expected_pin,
    const node_desc *conversion
)
{
    node *source = NULL;
    int source_pin = -1;
    int connected = graph_input_source(from, target, pin, &source, &source_pin);
    return (expected ? connected && source == expected && source_pin == expected_pin
                     : !connected) &&
           graph_input_conversion(from, target, pin) == conversion;
}

static bool exec_matches(
    const graph *from,
    const node *source,
    int pin,
    const node *expected,
    int expected_pin,
    int order
)
{
    const edge *first = NULL;
    int count = graph_exec_output_edges(from, source, pin, &first);
    if (!expected) {
        return count == 0 && !first;
    }
    node *target = NULL;
    int target_pin = -1;
    return count == 1 && first && first->to_id == expected->id &&
           first->exec_order == order &&
           graph_exec_output_target(from, source, pin, order, &target, &target_pin) &&
           target == expected && target_pin == expected_pin;
}

/* Imports remap stable IDs directly, then rebuild the graph's indexes. */
static void remap_ids(graph *to, uint64_t offset)
{
    for (int i = 0; i < acount(to->nodes); i++) {
        node *n = to->nodes[i];
        n->id += offset;
        for (int p = 0; p < n->input_count; p++) {
            n->input_pins[p].id += offset;
        }
        for (int p = 0; p < n->output_count; p++) {
            n->output_pins[p].id += offset;
        }
    }
    for (int i = 0; i < acount(to->edges); i++) {
        to->edges[i].from_id += offset;
        to->edges[i].from_pin_id += offset;
        to->edges[i].to_id += offset;
        to->edges[i].to_pin_id += offset;
    }
    to->id_counter += offset;
    graph_reindex(to);
}

TEST_CASE(input_lookup_follows_edits)
{
    graph z = { 0 };
    node *first = add(&z, "Real");
    node *second = add(&z, "Real");
    node *integer = add(&z, "Integer");
    node *target = add(&z, "Add");
    bool kept = input_matches(&z, target, 0, NULL, 0, NULL);
    /* Independent plain and converted drivers. */
    kept &= graph_connect(&z, first, 0, target, 0) == 0 &&
            graph_connect_data_conversion(&z, integer, 0, target, 1, &NODE_To_Real) == 0 &&
            input_matches(&z, target, 0, first, 0, NULL) &&
            input_matches(&z, target, 1, integer, 0, &NODE_To_Real);
    /* Replacing a driver invalidates a warmed lookup. */
    kept &= graph_connect(&z, second, 0, target, 0) == 0 &&
            input_matches(&z, target, 0, second, 0, NULL);
    /* Disconnecting preserves the edge swapped into the removed slot. */
    kept &= graph_remove_edge(&z, second, 0, target, 0) == 0 &&
            input_matches(&z, target, 0, NULL, 0, NULL) &&
            input_matches(&z, target, 1, integer, 0, &NODE_To_Real);

    /* A deserialized graph owns an independent input lookup. */
    char *document = graph_save(&z);
    graph *copy = document ? graph_load(document) : NULL;
    free(document);
    node *copy_target = copy ? graph_node_by_id(copy, target->id) : NULL;
    node *copy_integer = copy ? graph_node_by_id(copy, integer->id) : NULL;
    bool independent = copy_target && copy_integer &&
                       input_matches(copy, copy_target, 1, copy_integer, 0, &NODE_To_Real) &&
                       graph_remove_edge(copy, copy_integer, 0, copy_target, 1) == 0 &&
                       input_matches(copy, copy_target, 1, NULL, 0, NULL) &&
                       input_matches(&z, target, 1, integer, 0, &NODE_To_Real);
    if (copy) {
        graph_free(copy);
        free(copy);
    }

    /* The lookup follows stable IDs when both endpoint pin arrays move, and
       removing dynamic pins keeps the remaining connection. */
    pin_spec extra = { "Extra", PIN_F64, "f64", PIN_DATA };
    bool moved = graph_connect(&z, first, 0, target, 0) == 0 &&
                 input_matches(&z, target, 0, first, 0, NULL) &&
                 graph_add_pin(&z, first, 1, &extra) == 1 &&
                 graph_move_pin(&z, first, 1, 0, 1) == 1 &&
                 graph_move_pin(&z, target, 0, 0, 1) == 1 &&
                 input_matches(&z, target, 1, first, 1, NULL) &&
                 input_matches(&z, target, 0, integer, 0, &NODE_To_Real) &&
                 graph_remove_pin(&z, first, 1, 0) == 0 &&
                 input_matches(&z, target, 1, first, 0, NULL) &&
                 graph_remove_pin(&z, target, 0, 0) == 0 &&
                 input_matches(&z, target, 0, first, 0, NULL);

    remap_ids(&z, 1000);
    bool remapped = input_matches(&z, target, 0, first, 0, NULL);
    /* The lookup survives deleting the source and reconnecting after the
       graph became empty. */
    remapped &= graph_remove_node(&z, first) == 0 &&
                input_matches(&z, target, 0, NULL, 0, NULL) &&
                graph_connect(&z, second, 0, target, 0) == 0 &&
                input_matches(&z, target, 0, second, 0, NULL);
    graph_free(&z);
    REQUIRE(kept);
    REQUIRE(independent);
    REQUIRE(moved);
    REQUIRE(remapped);
    return true;
}

typedef struct
{
    graph *g;
    node *source, *event, *target;
    int inputs;
    atomic_bool start;
} input_readers;

static void *read_shared_inputs(void *user)
{
    input_readers *readers = user;
    while (!atomic_load_explicit(&readers->start, memory_order_acquire)) {
    }
    for (int repeat = 0; repeat < 64; repeat++) {
        const edge *first = NULL;
        if (graph_exec_output_edges(readers->g, readers->event, 0, &first) != 1 ||
            !first || first->to_id != readers->target->id) {
            return readers;
        }
        for (int i = 1; i <= readers->inputs; i++) {
            if (!input_matches(readers->g, readers->g->nodes[i], 0, readers->source, 0, &NODE_To_Real)) {
                return readers;
            }
        }
    }
    return NULL;
}

/* Concurrent first readers share one complete input index. */
TEST_CASE(concurrent_first_readers_share_the_index)
{
    enum
    {
        INPUTS = 128,
        READERS = 4
    };
    node *source = add(&g, "Integer");
    for (int i = 0; i < INPUTS; i++) {
        REQUIRE(
            graph_connect_data_conversion(&g, source, 0, add(&g, "Real"), 0, &NODE_To_Real) == 0
        );
    }
    node *event = add(&g, "Event");
    node *target = add(&g, "Branch");
    REQUIRE(graph_connect_exec(&g, event, 0, target, 0, 3) == 0);
    input_readers readers = { .g = &g,
                              .source = source,
                              .event = event,
                              .target = target,
                              .inputs = INPUTS };
    atomic_init(&readers.start, false);
    pthread_t threads[READERS];
    int started = 0;
    for (; started < READERS; started++) {
        if (pthread_create(&threads[started], NULL, read_shared_inputs, &readers)) {
            break;
        }
    }
    /* Even a failed creation must release and join every started reader
       before its stack-owned inputs and graph can be released. */
    atomic_store_explicit(&readers.start, true, memory_order_release);
    int failed = 0;
    for (int i = 0; i < started; i++) {
        void *result = NULL;
        int joined = pthread_join(threads[i], &result);
        assert(!joined);
        failed += result != NULL;
    }
    REQUIRE(started == READERS && failed == 0);
    return true;
}

/* Pasted data and EXEC lookups follow remapped IDs and keep earlier copies. */
TEST_CASE(pasted_edges_index_each_copy)
{
    graph source;
    graph_init(&source);
    node *value = add(&source, "Integer");
    node *sum = add(&source, "Add");
    node *event = add(&source, "Event");
    node *branch = add(&source, "Branch");
    bool authored = graph_set_event_name(&source, event, "Pasted") == 0 &&
                    graph_connect_data(&source, value, 0, sum, 0) == 0 &&
                    graph_connect_exec(&source, event, 0, branch, 0, 0) == 0;
    char *document = graph_save(&source);
    graph_free(&source);
    REQUIRE(authored && document);
    bool pasted = true;
    for (int pass = 0; pass < 3; pass++) {
        pasted &= graph_append(&g, document, pass * 100.0f, 0) == 0;
        /* Read before any mutation helper can warm or invalidate the index
           on behalf of paste. Recheck earlier copies after each append. */
        for (int copy = 0; pasted && copy <= pass; copy++) {
            node **nodes = g.nodes + copy * 4;
            pasted &= input_matches(&g, nodes[1], 0, nodes[0], 0, NULL) &&
                      exec_matches(&g, nodes[2], 0, nodes[3], 0, 0);
        }
    }
    free(document);
    REQUIRE(pasted);
    return true;
}

TEST_CASE(exec_lookup_follows_edits)
{
    node *event = add(&g, "Event");
    node *sequence = add(&g, "Sequence");
    node *a = graph_add_node(&g, &sink_desc);
    node *b = graph_add_node(&g, &sink_desc);
    graph_set_event_name(&g, event, "Indexed Exec");
    /* Indexed EXEC outputs keep independent targets and explicit orders. */
    REQUIRE(graph_connect_exec(&g, event, 0, sequence, 0, 7) == 0);
    REQUIRE(graph_connect_exec(&g, sequence, 0, a, 0, 9) == 0);
    REQUIRE(graph_connect_exec(&g, sequence, 1, b, 0, 4) == 0);
    REQUIRE(exec_matches(&g, sequence, 0, a, 0, 9) && exec_matches(&g, sequence, 1, b, 0, 4));
    /* Changing EXEC order invalidates the warmed output lookup. */
    node *unused = NULL;
    int unused_pin = -1;
    REQUIRE(graph_connect_exec(&g, sequence, 0, a, 0, 12) == 0);
    REQUIRE(exec_matches(&g, sequence, 0, a, 0, 12));
    REQUIRE(!graph_exec_output_target(&g, sequence, 0, 9, &unused, &unused_pin));
    /* Disconnecting preserves an edge swapped into the removed slot. */
    REQUIRE(graph_remove_edge(&g, sequence, 0, a, 0) == 0);
    REQUIRE(
        exec_matches(&g, sequence, 0, NULL, 0, 0) &&
        exec_matches(&g, sequence, 1, b, 0, 4)
    );
    /* The lookup follows source and target pins after dynamic reordering. */
    pin_spec extra = { "Extra", PIN_FLOW, NULL, PIN_EXEC };
    REQUIRE(graph_connect_exec(&g, sequence, 0, a, 0, 12) == 0);
    REQUIRE(graph_move_pin(&g, sequence, 1, 0, 1) == 1);
    REQUIRE(graph_add_pin(&g, a, 0, &extra) == 2);
    REQUIRE(graph_move_pin(&g, a, 0, 0, 1) == 1);
    REQUIRE(exec_matches(&g, sequence, 0, b, 0, 4) && exec_matches(&g, sequence, 1, a, 1, 12));
    /* Reindexing picks up a remapped output ID. */
    uint64_t old = sequence->output_pins[1].id;
    sequence->output_pins[1].id = graph_alloc_id(&g);
    for (int i = 0; i < acount(g.edges); i++) {
        if (g.edges[i].from_pin_id == old) {
            g.edges[i].from_pin_id = sequence->output_pins[1].id;
        }
    }
    graph_reindex(&g);
    REQUIRE(exec_matches(&g, sequence, 1, a, 1, 12));
    /* Pin and target deletion leave no stale edges. */
    REQUIRE(graph_remove_pin(&g, a, 0, 0) == 0 && exec_matches(&g, sequence, 1, a, 0, 12));
    REQUIRE(graph_remove_node(&g, a) == 0 && exec_matches(&g, sequence, 1, NULL, 0, 0));
    REQUIRE(
        graph_remove_pin(&g, sequence, 1, 1) == 0 &&
        exec_matches(&g, sequence, 0, b, 0, 4)
    );
    return true;
}

TEST_CASE(invalid_exec_fan_out_stays_inspectable)
{
    node *event = add(&g, "Event");
    node *a = add(&g, "Branch");
    node *b = add(&g, "Branch");
    graph_set_event_name(&g, event, "Invalid Exec");
    REQUIRE(graph_connect_exec(&g, event, 0, a, 0, 7) == 0);
    REQUIRE(exec_matches(&g, event, 0, a, 0, 7));
    edge duplicate = g.edges[0];
    duplicate.to_id = b->id;
    duplicate.to_pin_id = b->input_pins[0].id;
    apush(g.edges, duplicate);
    graph_reindex(&g);
    /* Invalid fan-out counts every edge and keeps the first authored match. */
    const edge *first = NULL;
    node *target = NULL;
    int target_pin = -1;
    REQUIRE(graph_exec_output_edges(&g, event, 0, &first) == 2 && first == &g.edges[0]);
    REQUIRE(graph_exec_output_target(&g, event, 0, 7, &target, &target_pin) && target == a);
    g.edges[1].exec_order = 3;
    graph_reindex(&g);
    REQUIRE(graph_exec_output_target(&g, event, 0, 3, &target, &target_pin) && target == b);
    /* Repairing malformed edges refreshes the same shared index. */
    g.edges[0].to_id = g.id_counter + 100;
    graph_reindex(&g);
    asetlen(g.edges, 1);
    g.edges[0].to_id = a->id;
    g.edges[0].to_pin_id = a->output_pins[0].id;
    graph_reindex(&g);
    g.edges[0].to_pin_id = a->input_pins[0].id;
    graph_reindex(&g);
    REQUIRE(exec_matches(&g, event, 0, a, 0, 7));
    return true;
}

TEST_CASE(descriptors_carry_reflected_types_and_policy)
{
    const node_desc *make = graph_palette_node("Make_vec3");
    const node_desc *brk = graph_palette_node("Break_vec3");
    const node_desc *sum = graph_palette_node("Add");
    REQUIRE(make->type && make->type->name && !strcmp(make->type->name, "vec3"));
    REQUIRE(make->type->field_count == 3 && make->type->fields);
    REQUIRE(make->type->fields[0].size == sizeof(float) && make->type->fields[0].offset == 0);
    REQUIRE(make->type->size >= sizeof(float));
    REQUIRE(make->inputs && make->outputs);
    REQUIRE(make->pure_kind == NODE_PURE_MAKE && brk->pure_kind == NODE_PURE_BREAK);
    REQUIRE(sum->fn);
    pin_spec i32_pin = { "i", PIN_I32, "int", PIN_DATA };
    pin_spec i64_pin = { "i", PIN_I64, "long", PIN_DATA };
    REQUIRE(!pin_types_compatible(&i32_pin, &i64_pin));
    REQUIRE(node_desc_is_pure(&const_desc) && !node_desc_is_pure(&sink_desc));
    REQUIRE(const_desc.display_class == NODE_DISPLAY_LITERAL);

    /* The graph owns struct defaults independently of caller storage. */
    node *reader = graph_add_node(&g, brk);
    float source[] = { 2, 3, 5 };
    pin_value value = { .type = PIN_STRUCT, .type_name = "vec3", .ptr = source };
    REQUIRE(graph_set_pin_default(&g, reader, 0, 0, &value) == 0);
    source[0] = source[1] = source[2] = -100;
    const float *stored = reader->input_pins[0].default_value.ptr;
    REQUIRE(stored != source && stored[0] == 2 && stored[1] == 3 && stored[2] == 5);
    return true;
}

TEST_CASE(ids_and_instance_pins_stay_stable)
{
    const node_desc *sum = graph_palette_node("Add");
    node *retiring = graph_add_node(&g, sum);
    uint64_t retired = retiring->id;
    uint64_t retired_pin = retiring->output_pins[0].id;
    REQUIRE(graph_remove_node(&g, retiring) == 0);
    REQUIRE(graph_remove_node(&g, retiring) == -1);
    node *fresh = graph_add_node(&g, sum);
    REQUIRE(fresh->id != retired && fresh->output_pins[0].id != retired_pin);
    REQUIRE(!graph_node_by_id(&g, retired));
    REQUIRE(fresh->id && fresh->input_pins[0].id && fresh->output_pins[0].id);
    REQUIRE(!graph_node_by_id(&g, 0) && !graph_pin_by_id(fresh, 0, NULL, NULL));
    REQUIRE(fresh->desc == sum);
    REQUIRE(fresh->input_pins[0].name && fresh->input_pins[0].type_name);
    REQUIRE(fresh->input_pins[0].kind == PIN_DATA);

    /* Instance pin edits copy from an untouched descriptor template. */
    node *p1 = graph_add_node(&g, sum);
    node *p2 = graph_add_node(&g, sum);
    REQUIRE(graph_set_pin_name(&g, p1, 0, 0, "left") == 0);
    REQUIRE(!strcmp(p1->input_pins[0].name, "left") && !strcmp(p2->input_pins[0].name, "A"));
    REQUIRE(!strcmp(sum->inputs[0].name, "A"));
    pin_value authored = { .type = PIN_F32, .type_name = "number", .f = 7.0f };
    REQUIRE(graph_set_pin_default(&g, p1, 0, 0, &authored) == 0);
    REQUIRE(p1->input_pins[0].default_value.f == 7.0f);

    /* User-extensible groups add and remove instance pins. */
    pin_spec extra = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
    node *seq = add(&g, "Sequence");
    int old_outputs = seq->output_count;
    REQUIRE(graph_add_pin(&g, seq, 1, &extra) == old_outputs);
    REQUIRE(seq->output_count == old_outputs + 1);
    REQUIRE(graph_remove_pin(&g, seq, 1, old_outputs) == 0 && seq->output_count == old_outputs);

    char *document = graph_save(&g);
    graph *loaded = document ? graph_load(document) : NULL;
    free(document);
    node *reloaded = loaded ? graph_node_by_id(loaded, fresh->id) : NULL;
    bool kept = reloaded &&
                graph_pin_by_id(reloaded, fresh->input_pins[0].id, NULL, NULL);
    if (loaded) {
        graph_free(loaded);
        free(loaded);
    }
    REQUIRE(kept);
    return true;
}

/* The first input default of n's copy in a reloaded graph. */
static const pin_value *loaded_default(const graph *loaded, const node *n)
{
    const node *copy = graph_node_by_id(loaded, n->id);
    assert(copy);
    return &copy->input_pins[0].default_value;
}

TEST_CASE(authored_defaults_and_literals_save)
{
    node *literal = add(&g, "Real");
    pin_value changed = { .type = PIN_F64, .type_name = "f64", .d = 9.0 };
    REQUIRE(graph_set_pin_default(&g, literal, 0, 0, &changed) == 0);
    REQUIRE(literal->input_pins[0].default_value.d == 9.0);

    const char *names[] = { "Bool",      "Byte", "Integer",
                            "Integer64", "Name", "String" };
    node *literals[6];
    for (int i = 0; i < 6; i++) {
        literals[i] = add(&g, names[i]);
    }
    node *flag = literals[0];
    REQUIRE(flag->input_count == 1 && flag->output_count == 1);
    REQUIRE(flag->input_pins[0].kind == PIN_DATA && flag->output_pins[0].kind == PIN_DATA);
    pin_value values[] = {
        { .type = PIN_BOOL, .b = true },
        { .type = PIN_U8, .u8 = 7 },
        { .type = PIN_I32, .i = 8 },
        { .type = PIN_I64, .i64 = 9 },
        { .type = PIN_NAME, .str = "ExactName" },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "literal",
                                       .length = sizeof("literal") - 1 } },
    };
    for (int i = 0; i < 6; i++) {
        REQUIRE(graph_set_pin_default(&g, literals[i], 0, 0, &values[i]) == 0);
    }
    char *document = graph_save(&g);
    graph *loaded = document ? graph_load(document) : NULL;
    free(document);
    REQUIRE(loaded);
    const pin_value *real = loaded_default(loaded, literal);
    const pin_value *name = loaded_default(loaded, literals[4]);
    const pin_value *text = loaded_default(loaded, literals[5]);
    bool kept = acount(loaded->nodes) == 7 && real->d == 9.0 &&
                pin_text(*name).bytes &&
                !strcmp(pin_text(*name).bytes, "ExactName") &&
                pin_text(*text).bytes && !strcmp(pin_text(*text).bytes, "literal");
    graph_free(loaded);
    free(loaded);
    REQUIRE(kept);
    return true;
}

TEST_CASE(real_inputs_convert_between_widths)
{
    node *delay = add(&g, "Delay");
    REQUIRE(graph_connect_data(&g, make_const(&g, 2.5f), 0, delay, 1) == 0);
    REQUIRE(delay->input_pins[1].type == PIN_F64);
    static pin_spec int_output[] = { { "value", PIN_I32, "int" } };
    static pin_spec int64_input[] = { { "value", PIN_I64, "i64" } };
    static node_desc int_source = { .name = "test_int_source",
                                    .output_count = 1,
                                    .outputs = int_output,
                                    .pure_kind = NODE_PURE_CONST };
    static node_desc int_target = { .name = "test_int_target",
                                    .input_count = 1,
                                    .inputs = int64_input,
                                    .pure_kind = NODE_PURE_CONST };
    node *integer = graph_add_node(&g, &int_source);
    node *integer64 = graph_add_node(&g, &int_target);
    REQUIRE(graph_connect(&g, integer, 0, integer64, 0) != 0);
    pin_spec f32 = { .type = PIN_F32, .type_name = "number" };
    pin_spec f64 = { .type = PIN_F64, .type_name = "f64" };
    pin_value converted = { .type = PIN_F64, .type_name = "f64", .d = 7.25 };
    REQUIRE(pin_compatible(&f32, &f64) && pin_coerce(&converted, &f32));
    REQUIRE(converted.type == PIN_F32 && converted.f == 7.25f);
    return true;
}

TEST_CASE(data_edges_replace_remove_and_fan_out)
{
    /* An edge resolves to its node after an earlier entry is deleted. */
    node *early = make_const(&g, 1.0f);
    node *late = make_const(&g, 2.0f);
    node *ordered = graph_add_node(&g, &sum_desc);
    REQUIRE(graph_connect(&g, late, 0, ordered, 0) == 0);
    REQUIRE(graph_remove_node(&g, early) == 0);
    REQUIRE(input_matches(&g, ordered, 0, late, 0, NULL));

    /* Connecting an input replaces its driver; removal takes one edge. */
    node *first = make_const(&g, 4.0f);
    node *second = make_const(&g, 5.0f);
    node *sum = graph_add_node(&g, &sum_desc);
    int edges = acount(g.edges);
    REQUIRE(graph_connect(&g, first, 0, sum, 0) == 0);
    REQUIRE(graph_connect(&g, second, 0, sum, 0) == 0);
    REQUIRE(acount(g.edges) == edges + 1 && input_matches(&g, sum, 0, second, 0, NULL));
    REQUIRE(graph_connect(&g, second, 0, sum, 1) == 0);
    REQUIRE(graph_remove_edge(&g, first, 0, sum, 0) == -1);
    REQUIRE(graph_remove_edge(&g, second, 0, sum, 0) == 0);
    REQUIRE(acount(g.edges) == edges + 1 && input_matches(&g, sum, 0, NULL, 0, NULL));

    /* One output drives three inputs; removing one edge leaves the others. */
    node *driver = make_const(&g, 8.0f);
    node *fan[3];
    for (int i = 0; i < 3; i++) {
        fan[i] = graph_add_node(&g, &sum_desc);
        REQUIRE(graph_connect(&g, driver, 0, fan[i], 0) == 0);
    }
    edges = acount(g.edges);
    REQUIRE(graph_remove_edge(&g, driver, 0, fan[1], 0) == 0 && acount(g.edges) == edges - 1);
    REQUIRE(
        input_matches(&g, fan[0], 0, driver, 0, NULL) &&
        input_matches(&g, fan[2], 0, driver, 0, NULL)
    );

    /* Removing a node drops its edges. */
    node *x = graph_add_node(&g, &sum_desc);
    node *y = graph_add_node(&g, &sum_desc);
    REQUIRE(graph_connect(&g, x, 0, y, 0) == 0 && graph_connect(&g, y, 0, x, 0) == 0);
    int nodes = acount(g.nodes);
    edges = acount(g.edges);
    REQUIRE(graph_remove_node(&g, x) == 0);
    REQUIRE(acount(g.nodes) == nodes - 1 && acount(g.edges) == edges - 2);
    return true;
}

void test_graph_indexes()
{
    graph_palette_open();
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(input_lookup_follows_edits);
    RUN_TEST_CASE(concurrent_first_readers_share_the_index);
    RUN_TEST_CASE(pasted_edges_index_each_copy);
    RUN_TEST_CASE(exec_lookup_follows_edits);
    RUN_TEST_CASE(invalid_exec_fan_out_stays_inspectable);
    RUN_TEST_CASE(descriptors_carry_reflected_types_and_policy);
    RUN_TEST_CASE(ids_and_instance_pins_stay_stable);
    RUN_TEST_CASE(authored_defaults_and_literals_save);
    RUN_TEST_CASE(real_inputs_convert_between_widths);
    RUN_TEST_CASE(data_edges_replace_remove_and_fan_out);
    pu_clear_setup();
    graph_palette_close();
}
