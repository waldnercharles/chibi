#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/authoring/layout.h"
#include "graph/authoring/local_edit.h"
#include "graph/document/serialize.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pin_spec setting_inputs[] = {
    { .name = "Mode", .type = PIN_I32, .type_name = "int", .kind = PIN_DATA, .flags = PIN_FLAG_SETTING },
};
static pin_spec setting_outputs[] = {
    { .name = "Value", .type = PIN_F64, .type_name = "f64", .kind = PIN_DATA },
};
static node_desc setting_desc = { .name = "Inline Setting Source",
                                  .input_count = 1,
                                  .inputs = setting_inputs,
                                  .output_count = 1,
                                  .outputs = setting_outputs,
                                  .pure_kind = NODE_PURE_CONST };

static graph g;
static behavior_asset *asset;

static void setup()
{
    graph_init(&g);
}

static void cleanup()
{
    graph_free(&g);
    behavior_asset_free(asset);
    asset = NULL;
}

static node *add(graph *to, const char *type)
{
    node *n = graph_add_node(to, graph_palette_node(type));
    assert(n);
    return n;
}

static node *real(graph *to, double value)
{
    node *n = add(to, "Real");
    pin_value v = { .type = PIN_F64, .type_name = "f64", .d = value };
    int set = graph_set_pin_default(to, n, false, 0, &v);
    assert(set == 0);
    (void)set;
    return n;
}

static void wire(graph *to, node *from, int from_pin, node *target, int target_pin)
{
    int connected = graph_connect(to, from, from_pin, target, target_pin);
    assert(connected == 0);
    (void)connected;
}

static node *binary(graph *to, const char *type, node *left, node *right)
{
    node *n = add(to, type);
    wire(to, left, 0, n, 0);
    wire(to, right, 0, n, 1);
    return n;
}

TEST_CASE(single_use_constants_fold_unless_kept)
{
    node *source = real(&g, 1);
    node *target = add(&g, "Add");
    wire(&g, source, 0, target, 0);
    REQUIRE(graph_node_is_inline(&g, source) && !graph_node_is_inline(&g, target));
    REQUIRE(source->presentation == NODE_PRESENTATION_AUTO);
    REQUIRE(graph_inline_owner(&g, source) == target);

    REQUIRE(graph_set_node_presentation(&g, source, NODE_PRESENTATION_EXPANDED) == 0);
    REQUIRE(!graph_node_is_inline(&g, source) && graph_node_can_inline(&g, source));
    /* Folding persists nothing when auto suffices. */
    REQUIRE(graph_inline_node(&g, source) == 0 && graph_node_is_inline(&g, source));
    REQUIRE(source->presentation == NODE_PRESENTATION_AUTO);

    REQUIRE(graph_set_node_disabled(&g, source, 1) == 0);
    REQUIRE(!graph_node_can_inline(&g, source));
    REQUIRE(graph_set_node_disabled(&g, source, 0) == 0);

    node *other = add(&g, "Add");
    wire(&g, source, 0, other, 0);
    REQUIRE(!graph_node_can_inline(&g, source));
    REQUIRE(graph_remove_node(&g, other) == 0);

    /* Watched sources keep their box by default; explicit inline folds them. */
    REQUIRE(graph_set_pin_watch(&g, source, 1, 0, 1) == 0);
    REQUIRE(!graph_node_is_inline(&g, source));
    REQUIRE(graph_inline_node(&g, source) == 0 && graph_node_is_inline(&g, source));
    REQUIRE(source->presentation == NODE_PRESENTATION_INLINE);
    return true;
}

TEST_CASE(auto_folds_small_trees_and_explicit_inline_folds_larger)
{
    node *leaf = real(&g, 2);
    node *first = add(&g, "Negate");
    node *second = add(&g, "Negate");
    node *third = add(&g, "Negate");
    node *target = add(&g, "Add");
    wire(&g, leaf, 0, first, 0);
    wire(&g, first, 0, second, 0);
    wire(&g, second, 0, third, 0);
    wire(&g, third, 0, target, 0);
    REQUIRE(graph_node_is_inline(&g, second) && !graph_node_is_inline(&g, third));
    REQUIRE(graph_inline_owner(&g, leaf) == third);
    REQUIRE(graph_inline_node(&g, third) == 0);
    REQUIRE(third->presentation == NODE_PRESENTATION_INLINE);
    REQUIRE(graph_inline_owner(&g, leaf) == target);
    /* An expanded input keeps its consumer expanded. */
    REQUIRE(graph_set_node_presentation(&g, second, NODE_PRESENTATION_EXPANDED) == 0);
    REQUIRE(!graph_node_is_inline(&g, third) && graph_node_is_inline(&g, first));
    REQUIRE(graph_inline_owner(&g, leaf) == second);
    /* Generated expression graphs keep their boxes by default. */
    g.math_expression = "-(-(-2))";
    REQUIRE(!graph_node_is_inline(&g, leaf) && graph_node_can_inline(&g, leaf));
    g.math_expression = NULL;
    return true;
}

TEST_CASE(settings_and_reroutes_keep_boxes_and_locals_fold)
{
    node *setting = graph_add_node(&g, &setting_desc);
    node *reroute = add(&g, "Reroute Data");
    node *target = add(&g, "Add");
    node *source = real(&g, 3);
    wire(&g, setting, 0, target, 0);
    wire(&g, source, 0, reroute, 0);
    wire(&g, reroute, 0, target, 1);
    REQUIRE(!graph_node_can_inline(&g, setting));
    REQUIRE(!graph_node_can_inline(&g, reroute));
    REQUIRE(graph_inline_owner(&g, source) == reroute);

    /* A local input is a value an inline node can fold. */
    node *named = real(&g, 4);
    node *uses[3];
    for (int i = 0; i < 3; i++) {
        uses[i] = binary(&g, "Add", named, real(&g, i));
    }
    wire(&g, uses[0], 0, add(&g, "Add"), 0);
    REQUIRE(graph_create_local(&g, named, 0, "base"));
    REQUIRE(!graph_node_can_inline(&g, named) && graph_node_is_inline(&g, uses[0]));
    return true;
}

TEST_CASE(removing_a_tree_removes_its_inline_sources)
{
    node *sum = binary(&g, "Add", real(&g, 1), real(&g, 2));
    node *target = add(&g, "Add");
    wire(&g, sum, 0, target, 0);
    REQUIRE(graph_remove_node(&g, target) == 0 && acount(g.nodes) == 3);
    target = add(&g, "Add");
    wire(&g, sum, 0, target, 0);
    REQUIRE(graph_remove_node_tree(&g, target) == 0 && acount(g.nodes) == 0);
    return true;
}

/* Double(X) = X * 2: a pure function's entry has no Exec pin. */
TEST_CASE(function_entries_keep_their_boxes)
{
    asset = behavior_asset_new("Signature");
    graph *function = behavior_asset_add_function(asset, "Double", 1);
    node *entry = graph_node_by_id(function, function->entry_id);
    node *ret = graph_node_by_id(function, function->return_id);
    pin_spec value = { .name = "X", .type = PIN_F64, .type_name = "f64", .kind = PIN_DATA };
    REQUIRE(graph_add_pin(function, entry, 1, &value) == 0);
    REQUIRE(graph_add_pin(function, ret, 0, &value) == 0);
    node *doubled = binary(function, "Multiply", entry, real(function, 2));
    wire(function, doubled, 0, ret, 0);
    REQUIRE(!graph_node_can_inline(function, entry) && !graph_node_is_inline(function, doubled));
    REQUIRE(graph_remove_node_tree(function, doubled) == 0);
    REQUIRE(graph_node_by_id(function, function->entry_id) == entry);
    REQUIRE(acount(function->nodes) == 2);
    return true;
}

/* An auto source, an expanded source and an explicitly inline tree. */
static void presentation_asset(node **modes)
{
    asset = behavior_asset_new("Presentation");
    graph *to = asset->graph;
    node *target = add(to, "Add");
    for (int i = 0; i < 2; i++) {
        modes[i] = real(to, i);
        wire(to, modes[i], 0, target, i);
    }
    int set = graph_set_node_presentation(to, modes[1], NODE_PRESENTATION_EXPANDED);
    node *tree = binary(to, "Add", real(to, 5), binary(to, "Add", real(to, 6), real(to, 7)));
    modes[2] = tree;
    wire(to, tree, 0, add(to, "Add"), 0);
    set |= graph_set_node_presentation(to, tree, NODE_PRESENTATION_INLINE);
    assert(set == 0);
    (void)set;
}

static char *replace(const char *text, const char *from, const char *to)
{
    const char *at = strstr(text, from);
    assert(at);
    size_t head = (size_t)(at - text);
    char *out = xmalloc(strlen(text) - strlen(from) + strlen(to) + 1);
    memcpy(out, text, head);
    strcpy(out + head, to);
    strcat(out, at + strlen(from));
    return out;
}

TEST_CASE(presentations_and_placement_round_trip)
{
    node *modes[3];
    presentation_asset(modes);
    for (int i = 0; i < 3; i++) {
        modes[i]->placed = i != 2;
        modes[i]->x = (float)(i + 1) * 72;
        modes[i]->y = (float)(i + 1) * 96;
    }
    char *document = graph_asset_text(asset);
    REQUIRE(document);
    bool written = strstr(document, "presentation \"inline\"") &&
                   strstr(document, "presentation \"expanded\"") &&
                   !strstr(document, "presentation \"auto\"") &&
                   !strstr(document, "inline_source");
    behavior_asset *copy = graph_asset_read(document);
    behavior_asset *snapshot = behavior_asset_clone(asset);
    char *invalid = replace(document, "presentation \"inline\"", "presentation \"sideways\"");
    behavior_asset *rejected = graph_asset_read(invalid);
    bool named_error = !rejected && strstr(graph_last_error(), "presentation");
    free(invalid);
    free(document);
    REQUIRE(written);
    REQUIRE(copy && snapshot);
    for (int i = 0; i < 3; i++) {
        node *saved = graph_node_by_id(copy->graph, modes[i]->id);
        node *cloned = graph_node_by_id(snapshot->graph, modes[i]->id);
        REQUIRE(saved && cloned && saved->presentation == modes[i]->presentation);
        REQUIRE(
            graph_node_is_inline(copy->graph, saved) ==
            graph_node_is_inline(asset->graph, modes[i])
        );
        REQUIRE(saved->placed == modes[i]->placed && cloned->placed == modes[i]->placed);
        REQUIRE(saved->x == modes[i]->x && saved->y == modes[i]->y);
        REQUIRE(cloned->x == modes[i]->x && cloned->y == modes[i]->y);
    }
    behavior_asset_free(snapshot);
    behavior_asset_free(copy);
    behavior_asset_free(rejected);
    REQUIRE(named_error);
    return true;
}

static bool at(const node *n, float x, float y)
{
    return n->x == x && n->y == y;
}

static bool inline_nodes_untouched(const graph *folded)
{
    for (int i = 0; i < acount(folded->nodes); i++) {
        node *n = folded->nodes[i];
        if (graph_node_is_inline(folded, n) && !at(n, 1234, 5678)) {
            return false;
        }
    }
    return true;
}

TEST_CASE(arrangement_ignores_inline_nodes)
{
    graph folded;
    graph_init(&folded);
    graph *graphs[] = { &g, &folded };
    node *event[2], *branch[2];
    for (int i = 0; i < 2; i++) {
        event[i] = add(graphs[i], "Event");
        branch[i] = add(graphs[i], "Branch");
        REQUIRE(graph_connect_exec(graphs[i], event[i], 0, branch[i], 0, 0) == 0);
    }
    node *less = binary(&folded, "Less", real(&folded, 1), real(&folded, 2));
    wire(&folded, less, 0, branch[1], 1);
    for (int i = 0; i < acount(folded.nodes); i++) {
        folded.nodes[i]->x = 1234;
        folded.nodes[i]->y = 5678;
    }
    graph_layout(&g);
    graph_layout(&folded);
    bool arranged = graph_node_is_inline(&folded, less) &&
                    inline_nodes_untouched(&folded) &&
                    at(event[1], event[0]->x, event[0]->y) &&
                    at(branch[1], branch[0]->x, branch[0]->y) &&
                    !at(branch[1], 1234, 5678);

    /* Placing automatic nodes skips inline nodes. */
    event[1]->placed = branch[1]->placed = true;
    event[1]->x = branch[1]->x = 99;
    node *spare = add(&folded, "Branch");
    spare->placed = false;
    spare->x = spare->y = -1;
    graph_place_unplaced(&folded);
    bool placed = inline_nodes_untouched(&folded) && event[1]->x == 99 &&
                  branch[1]->x == 99 && !at(spare, -1, -1);
    graph_free(&folded);
    REQUIRE(arranged);
    REQUIRE(placed);
    return true;
}

/* Formats what a consumer of n's first output receives. */
static bool formats(graph *to, node *n, const char *expected)
{
    node *consumer = add(to, "Reroute Data");
    wire(to, n, 0, consumer, 0);
    char text[256];
    behavior_expression_format(to, consumer, 0, text, sizeof text, NULL);
    if (strcmp(text, expected)) {
        printf("formatted `%s`, expected `%s`\n", text, expected);
        return false;
    }
    return true;
}

TEST_CASE(expressions_format_with_precedence)
{
    asset = behavior_asset_new("Format");
    graph *to = asset->graph;
    behavior_variable *speed = behavior_asset_add_variable(
        asset,
        "speed",
        PIN_F64,
        "f64",
        PIN_CONTAINER_SINGLE,
        NULL,
        NULL
    );
    node *get = behavior_asset_add_get_node(asset, speed->id);
    REQUIRE(formats(to, binary(to, "Multiply", get, real(to, 2)), "speed × 2"));
    node *integer = add(to, "Integer");
    pin_value seven = { .type = PIN_I32, .type_name = "int", .i = 7 };
    REQUIRE(graph_set_pin_default(to, integer, false, 0, &seven) == 0);
    node *converted = add(to, "Multiply");
    REQUIRE(graph_connect_data_conversion(to, integer, 0, converted, 0, &NODE_To_Real) == 0);
    wire(to, real(to, 2), 0, converted, 1);
    REQUIRE(formats(to, converted, "to_real(7) × 2"));

    REQUIRE(formats(
        to,
        binary(to, "Subtract", real(to, 1), binary(to, "Subtract", real(to, 2), real(to, 3))),
        "1 − (2 − 3)"
    ));
    REQUIRE(formats(
        to,
        binary(to, "Multiply", binary(to, "Add", real(to, 1), real(to, 2)), real(to, 3)),
        "(1 + 2) × 3"
    ));
    REQUIRE(formats(
        to,
        binary(to, "Add", real(to, 1), binary(to, "Multiply", real(to, 2), real(to, 3))),
        "1 + 2 × 3"
    ));
    /* Power associates right and groups negative bases. */
    REQUIRE(formats(
        to,
        binary(to, "Power", real(to, 2), binary(to, "Power", real(to, 3), real(to, 2))),
        "2 ^ 3 ^ 2"
    ));
    REQUIRE(formats(
        to,
        binary(to, "Power", binary(to, "Power", real(to, 2), real(to, 3)), real(to, 2)),
        "(2 ^ 3) ^ 2"
    ));
    REQUIRE(formats(to, binary(to, "Power", real(to, -2), real(to, 2)), "(-2) ^ 2"));
    node *negated = add(to, "Negate");
    wire(to, binary(to, "Add", real(to, 1), real(to, 2)), 0, negated, 0);
    REQUIRE(formats(to, negated, "−(1 + 2)"));
    REQUIRE(formats(to, binary(to, "Min", real(to, 1), real(to, 2)), "min(1, 2)"));
    REQUIRE(formats(to, binary(to, "Less", real(to, 1), real(to, 2)), "1 < 2"));
    return true;
}

/* The index of n's output named name. */
static int output_named(const node *n, const char *name)
{
    for (int p = 0; p < n->output_count; p++) {
        if (n->output_pins[p].name && !strcmp(n->output_pins[p].name, name)) {
            return p;
        }
    }
    return -1;
}

TEST_CASE(component_reads_format_like_their_bindings)
{
    asset = behavior_asset_new("Components");
    graph *to = asset->graph;
    const char *movement = COMPONENT_Movement.key;
    node *self = add(to, "Self");
    REQUIRE(graph_set_pin_type(to, self, 1, 0, PIN_HANDLE, "ecs.entity") == 0);
    node *self_has = add(to, "Has Component");
    REQUIRE(component_node_retarget(to, self_has, movement) == 0);
    wire(to, self, 0, self_has, component_node_setting(self_has) + 1);
    REQUIRE(graph_node_is_inline(to, self));
    REQUIRE(formats(to, self_has, "has(Self.Movement)"));

    /* Make Struct reads like the generated Make it lowers to. */
    const char *structs[] = { "Health", "Transform" };
    for (int i = 0; i < 2; i++) {
        char generated_name[64], generated_text[128], authored_text[128];
        snprintf(generated_name, sizeof generated_name, "Make_%s", structs[i]);
        node *generated = add(to, generated_name);
        node *authored = add(to, "Make Struct");
        REQUIRE(component_node_retarget(to, authored, structs[i]) == 0);
        node *first = add(to, "Reroute Data"), *second = add(to, "Reroute Data");
        wire(to, generated, 0, first, 0);
        wire(to, authored, 0, second, 0);
        behavior_expression_format(to, first, 0, generated_text, sizeof generated_text, NULL);
        behavior_expression_format(to, second, 0, authored_text, sizeof authored_text, NULL);
        REQUIRE(!strcmp(generated_text, authored_text));
    }

    node *getter = add(to, "Get_Movement");
    REQUIRE(graph_expose_output_fields(to, getter, 0) == 0);
    int field = output_named(getter, "speed");
    REQUIRE(field >= 0);
    node *sum = add(to, "Add");
    wire(to, getter, field, sum, 0);
    wire(to, real(to, 2), 0, sum, 1);
    REQUIRE(graph_node_is_inline(to, getter));
    REQUIRE(formats(to, sum, "Self.Movement.speed + 2"));

    /* Configured component reads fold despite their Component setting. */
    node *read = add(to, "Get Component");
    REQUIRE(component_node_retarget(to, read, movement) == 0);
    REQUIRE(graph_expose_output_fields(to, read, 0) == 0);
    field = output_named(read, "speed");
    REQUIRE(field >= 0);
    node *scaled_read = add(to, "Multiply");
    wire(to, read, field, scaled_read, 0);
    wire(to, real(to, 2), 0, scaled_read, 1);
    node *has = add(to, "Has Component");
    REQUIRE(component_node_retarget(to, has, movement) == 0);
    node *missing = add(to, "Not");
    wire(to, has, 0, missing, 0);
    REQUIRE(graph_node_is_inline(to, read) && graph_node_is_inline(to, has));
    REQUIRE(formats(to, scaled_read, "Self.Movement.speed × 2"));
    REQUIRE(formats(to, missing, "!has(Self.Movement)"));

    /* The values a chip reads, so an editor can name them in its own style. */
    node *reader = add(to, "Reroute Data");
    wire(to, scaled_read, 0, reader, 0);
    char text[128];
    behavior_expression_read *reads = NULL;
    behavior_expression_format(to, reader, 0, text, sizeof text, &reads);
    bool listed = acount(reads) == 1 && reads[0].start == 0 &&
                  reads[0].length == (int)strlen("Self.Movement.speed") &&
                  reads[0].read.kind == PIN_BINDING_COMPONENT_FIELD &&
                  !strcmp(reads[0].read.component_key, movement) &&
                  !strcmp(reads[0].read.field_name, "speed");
    /* Truncated text cuts its last read short. */
    asetlen(reads, 0);
    behavior_expression_format(to, reader, 0, text, 8, &reads);
    bool truncated = acount(reads) == 1 && reads[0].length == 7 &&
                     !strcmp(text, "Self.Mo");
    afree(reads);
    REQUIRE(listed);
    REQUIRE(truncated);
    return true;
}

static node *named(const char *type, const char *name)
{
    node *n = add(&g, type);
    int set = graph_set_node_reference(&g, n, 0, name);
    assert(set == 0);
    (void)set;
    return n;
}

/* Shader and field nodes leave pure_kind at its default, as constants do. */
TEST_CASE(shader_and_field_operations_format_as_expressions)
{
    node *gain = named("Shader Parameter", "Reaction Gain");
    pin_value value = { .type = PIN_F32, .f = 0.84f };
    REQUIRE(graph_set_pin_default(&g, gain, true, 0, &value) == 0);
    REQUIRE(formats(&g, gain, "Reaction Gain"));
    node *glow = named("Shader Parameter", "Glow");
    node *vector = add(&g, "Shader Vector3");
    for (int axis = 0; axis < 3; axis++) {
        value.f = (float)axis + 1;
        REQUIRE(graph_set_pin_default(&g, vector, false, axis, &value) == 0);
    }
    REQUIRE(
        formats(&g, binary(&g, "Shader Multiply", add(&g, "Shader Time"), glow), "time \xc3\x97 Glow")
    );
    REQUIRE(formats(&g, vector, "vec3(1, 2, 3)"));
    node *origin = add(&g, "Field Parameter");
    pin_value name = { .type = PIN_STR,
                       .text = &(const pin_string){ .bytes = "OriginX",
                                                    .length = sizeof("OriginX") - 1 } };
    REQUIRE(graph_set_pin_default(&g, origin, false, 0, &name) == 0);
    REQUIRE(formats(&g, binary(&g, "Field Add", add(&g, "Field X"), origin), "x + OriginX"));
    return true;
}

void test_graph_inline()
{
    graph_palette_open();
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(single_use_constants_fold_unless_kept);
    RUN_TEST_CASE(auto_folds_small_trees_and_explicit_inline_folds_larger);
    RUN_TEST_CASE(settings_and_reroutes_keep_boxes_and_locals_fold);
    RUN_TEST_CASE(removing_a_tree_removes_its_inline_sources);
    RUN_TEST_CASE(function_entries_keep_their_boxes);
    RUN_TEST_CASE(presentations_and_placement_round_trip);
    RUN_TEST_CASE(arrangement_ignores_inline_nodes);
    RUN_TEST_CASE(expressions_format_with_precedence);
    RUN_TEST_CASE(component_reads_format_like_their_bindings);
    RUN_TEST_CASE(shader_and_field_operations_format_as_expressions);
    pu_clear_setup();
    graph_palette_close();
}
