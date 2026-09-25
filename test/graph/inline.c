#include "graph/authoring/layout.h"
#include "graph/model/local.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "support/asset_text.h"
#include "test/suite_internal.h"

static pin_spec setting_inputs[] = {
    { .name = "Mode", .type = PIN_I32, .type_name = "int", .kind = PIN_DATA, .flags = PIN_FLAG_SETTING },
};
static pin_spec setting_outputs[] = {
    { .name = "Value", .type = PIN_F64, .type_name = "double", .kind = PIN_DATA },
};
static node_desc setting_desc = { .name = "Inline Setting Source",
                                  .input_count = 1,
                                  .inputs = setting_inputs,
                                  .output_count = 1,
                                  .outputs = setting_outputs,
                                  .pure_kind = NODE_PURE_CONST };

static node *add(graph *g, const char *type)
{
    node *n = graph_add_node(g, registry_node_by_name(type));
    assert(n);
    return n;
}

static node *real(graph *g, double value)
{
    node *n = add(g, "Real");
    pin_value v = { .type = PIN_F64, .type_name = "double", .d = value };
    assert(graph_set_pin_default(g, n, false, 0, &v) == 0);
    return n;
}

static void wire(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    int connected = graph_connect(g, from, from_pin, to, to_pin);
    assert(connected == 0);
    (void)connected;
}

static node *binary(graph *g, const char *type, node *left, node *right)
{
    node *n = add(g, type);
    wire(g, left, 0, n, 0);
    wire(g, right, 0, n, 1);
    return n;
}

static void eligibility()
{
    graph g = { 0 };
    graph_init(&g);
    node *source = real(&g, 1);
    node *target = add(&g, "Add");
    wire(&g, source, 0, target, 0);
    test_check(
        graph_node_is_inline(&g, source) && !graph_node_is_inline(&g, target) &&
            source->presentation == NODE_PRESENTATION_AUTO &&
            graph_inline_owner(&g, source) == target,
        "a single-use constant folds into its consumer by default"
    );

    assert(graph_set_node_presentation(&g, source, NODE_PRESENTATION_EXPANDED) == 0);
    test_check(!graph_node_is_inline(&g, source) && graph_node_can_inline(&g, source), "expanded sources keep their box");
    test_check(
        graph_inline_node(&g, source) == 0 && graph_node_is_inline(&g, source) &&
            source->presentation == NODE_PRESENTATION_AUTO,
        "folding persists nothing when auto suffices"
    );

    assert(graph_set_node_disabled(&g, source, 1) == 0);
    test_check(!graph_node_can_inline(&g, source), "disabled sources keep their box");
    assert(graph_set_node_disabled(&g, source, 0) == 0);

    node *other = add(&g, "Add");
    wire(&g, source, 0, other, 0);
    test_check(!graph_node_can_inline(&g, source), "shared sources keep their box");
    assert(graph_remove_node(&g, other) == 0);

    assert(graph_set_pin_watch(&g, source, 1, 0, 1) == 0);
    test_check(!graph_node_is_inline(&g, source), "watched sources keep their box by default");
    test_check(
        graph_inline_node(&g, source) == 0 && graph_node_is_inline(&g, source) &&
            source->presentation == NODE_PRESENTATION_INLINE,
        "explicit inline folds a watched source"
    );
    graph_free(&g);

    graph_init(&g);
    node *leaf = real(&g, 2);
    node *first = add(&g, "Negate");
    node *second = add(&g, "Negate");
    node *third = add(&g, "Negate");
    target = add(&g, "Add");
    wire(&g, leaf, 0, first, 0);
    wire(&g, first, 0, second, 0);
    wire(&g, second, 0, third, 0);
    wire(&g, third, 0, target, 0);
    test_check(
        graph_node_is_inline(&g, second) && !graph_node_is_inline(&g, third) &&
            graph_inline_owner(&g, leaf) == third,
        "auto folds trees of at most three nodes"
    );
    test_check(
        graph_inline_node(&g, third) == 0 &&
            third->presentation == NODE_PRESENTATION_INLINE &&
            graph_inline_owner(&g, leaf) == target,
        "explicit inline folds a larger tree"
    );
    assert(graph_set_node_presentation(&g, second, NODE_PRESENTATION_EXPANDED) == 0);
    test_check(
        !graph_node_is_inline(&g, third) && graph_node_is_inline(&g, first) &&
            graph_inline_owner(&g, leaf) == second,
        "an expanded input keeps its consumer expanded"
    );
    g.math_expression = "-(-(-2))";
    test_check(
        !graph_node_is_inline(&g, leaf) && graph_node_can_inline(&g, leaf),
        "generated expression graphs keep their boxes by default"
    );
    graph_free(&g);

    graph_init(&g);
    node *setting = graph_add_node(&g, &setting_desc);
    node *reroute = add(&g, "Reroute Data");
    target = add(&g, "Add");
    source = real(&g, 3);
    wire(&g, setting, 0, target, 0);
    wire(&g, source, 0, reroute, 0);
    wire(&g, reroute, 0, target, 1);
    test_check(!graph_node_can_inline(&g, setting), "sources with settings keep their box");
    test_check(
        !graph_node_can_inline(&g, reroute) && graph_inline_owner(&g, source) == reroute,
        "reroutes keep their box"
    );
    graph_free(&g);

    graph_init(&g);
    node *named = real(&g, 4);
    node *uses[3];
    for (int i = 0; i < 3; i++) {
        uses[i] = binary(&g, "Add", named, real(&g, i));
    }
    target = add(&g, "Add");
    wire(&g, uses[0], 0, target, 0);
    test_check(
        graph_create_local(&g, named, 0, "base") &&
            !graph_node_can_inline(&g, named) && graph_node_is_inline(&g, uses[0]),
        "a local input is a value an inline node can fold"
    );
    graph_free(&g);
}

static void removal()
{
    graph g = { 0 };
    graph_init(&g);
    node *sum = binary(&g, "Add", real(&g, 1), real(&g, 2));
    node *target = add(&g, "Add");
    wire(&g, sum, 0, target, 0);
    test_check(graph_remove_node(&g, target) == 0 && acount(g.nodes) == 3, "removing a node leaves the sources it drew");
    target = add(&g, "Add");
    wire(&g, sum, 0, target, 0);
    test_check(graph_remove_node_tree(&g, target) == 0 && acount(g.nodes) == 0, "removing a node tree removes its inline sources");
    graph_free(&g);
}

/* Double(X) = X * 2: a pure function's entry has no Exec pin. */
static void signature()
{
    behavior_asset *asset = behavior_asset_new("Signature");
    graph *function = behavior_asset_add_function(asset, "Double", 1);
    node *entry = graph_node_by_id(function, function->entry_id);
    node *ret = graph_node_by_id(function, function->return_id);
    pin_spec value = { .name = "X", .type = PIN_F64, .type_name = "double", .kind = PIN_DATA };
    assert(graph_add_pin(function, entry, 1, &value) == 0);
    assert(graph_add_pin(function, ret, 0, &value) == 0);
    node *doubled = binary(function, "Multiply", entry, real(function, 2));
    wire(function, doubled, 0, ret, 0);
    test_check(
        !graph_node_can_inline(function, entry) &&
            !graph_node_is_inline(function, doubled),
        "a function entry and the nodes it feeds keep their boxes"
    );
    test_check(
        graph_remove_node_tree(function, doubled) == 0 &&
            graph_node_by_id(function, function->entry_id) == entry &&
            acount(function->nodes) == 2,
        "removing a node tree keeps the function entry it reads"
    );
    behavior_asset_free(asset);
}

static behavior_asset *presentation_asset(node **modes)
{
    behavior_asset *asset = behavior_asset_new("Presentation");
    graph *g = asset->graph;
    node *target = add(g, "Add");
    for (int i = 0; i < 2; i++) {
        modes[i] = real(g, i);
        wire(g, modes[i], 0, target, i);
    }
    assert(graph_set_node_presentation(g, modes[1], NODE_PRESENTATION_EXPANDED) == 0);
    node *tree = binary(g, "Add", real(g, 5), binary(g, "Add", real(g, 6), real(g, 7)));
    modes[2] = tree;
    wire(g, tree, 0, add(g, "Add"), 0);
    assert(graph_set_node_presentation(g, tree, NODE_PRESENTATION_INLINE) == 0);
    return asset;
}

static bool yaml_has(const char *yaml, const char *text)
{
    return yaml && strstr(yaml, text);
}

static char *yaml_replace(const char *yaml, const char *from, const char *to)
{
    const char *at = strstr(yaml, from);
    assert(at);
    size_t head = (size_t)(at - yaml);
    char *out = xmalloc(strlen(yaml) - strlen(from) + strlen(to) + 1);
    memcpy(out, yaml, head);
    strcpy(out + head, to);
    strcat(out, at + strlen(from));
    return out;
}

static void serialization()
{
    node *modes[3];
    behavior_asset *asset = presentation_asset(modes);
    uint64_t ids[3] = { modes[0]->id, modes[1]->id, modes[2]->id };
    char *json = behavior_asset_save_json(asset);
    char error[256] = { 0 };
    char *yaml = json ? asset_text_to_disk(json, error, sizeof error) : NULL;
    test_check(
        yaml_has(yaml, "presentation: inline") &&
            yaml_has(yaml, "presentation: expanded") &&
            !yaml_has(yaml, "presentation: auto") && !yaml_has(yaml, "inline_source"),
        "YAML spells explicit presentations and omits auto"
    );
    behavior_asset *copy = yaml ? behavior_asset_load_json(yaml) : NULL;
    bool preserved = copy != NULL;
    for (int i = 0; preserved && i < 3; i++) {
        node *n = graph_node_by_id(copy->graph, ids[i]);
        preserved = n && n->presentation == modes[i]->presentation &&
                    graph_node_is_inline(copy->graph, n) ==
                        graph_node_is_inline(asset->graph, modes[i]);
    }
    test_check(preserved, "presentations round-trip through YAML");
    test_check(
        copy && !graph_node_by_id(copy->graph, ids[0])->placed &&
            graph_node_by_id(copy->graph, ids[1])->placed,
        "inline nodes are saved without a position"
    );
    behavior_asset_free(copy);

    char *invalid = yaml_replace(yaml, "presentation: inline", "presentation: sideways");
    test_check(
        !behavior_asset_load_json(invalid) && strstr(graph_last_error(), "presentation"),
        "unknown presentations are rejected"
    );
    free(invalid);
    free(yaml);
    free(json);
    behavior_asset_free(asset);
}

static bool at(const node *n, float x, float y)
{
    return n->x == x && n->y == y;
}

static void arrangement()
{
    graph plain = { 0 };
    graph folded = { 0 };
    graph_init(&plain);
    graph_init(&folded);
    graph *graphs[] = { &plain, &folded };
    node *event[2], *branch[2];
    for (int i = 0; i < 2; i++) {
        event[i] = add(graphs[i], "Event");
        branch[i] = add(graphs[i], "Branch");
        assert(graph_connect_exec(graphs[i], event[i], 0, branch[i], 0, 0) == 0);
    }
    node *less = binary(&folded, "Less", real(&folded, 1), real(&folded, 2));
    wire(&folded, less, 0, branch[1], 1);
    for (int i = 0; i < acount(folded.nodes); i++) {
        folded.nodes[i]->x = 1234;
        folded.nodes[i]->y = 5678;
    }
    graph_layout(&plain);
    graph_layout(&folded);
    bool untouched = true;
    for (int i = 0; i < acount(folded.nodes); i++) {
        node *n = folded.nodes[i];
        untouched &= !graph_node_is_inline(&folded, n) || at(n, 1234, 5678);
    }
    test_check(
        graph_node_is_inline(&folded, less) && untouched &&
            at(event[1], event[0]->x, event[0]->y) &&
            at(branch[1], branch[0]->x, branch[0]->y) && !at(branch[1], 1234, 5678),
        "arrangement ignores inline nodes and their wires"
    );

    event[1]->placed = branch[1]->placed = true;
    event[1]->x = branch[1]->x = 99;
    node *spare = add(&folded, "Branch");
    spare->placed = false;
    spare->x = spare->y = -1;
    graph_place_unplaced(&folded);
    untouched = true;
    for (int i = 0; i < acount(folded.nodes); i++) {
        node *n = folded.nodes[i];
        untouched &= !graph_node_is_inline(&folded, n) || at(n, 1234, 5678);
    }
    test_check(
        untouched && event[1]->x == 99 && branch[1]->x == 99 && !at(spare, -1, -1),
        "placing automatic nodes skips inline nodes"
    );
    graph_free(&plain);
    graph_free(&folded);
}

/* Formats what a consumer of n's first output receives. */
static bool formats(graph *g, node *n, const char *expected)
{
    node *consumer = add(g, "Reroute Data");
    wire(g, n, 0, consumer, 0);
    char text[256];
    behavior_expression_format(g, consumer, 0, text, sizeof text, NULL);
    if (strcmp(text, expected)) {
        printf("formatted `%s`, expected `%s`\n", text, expected);
        return false;
    }
    return true;
}

static void formatting()
{
    behavior_asset *asset = behavior_asset_new("Format");
    graph *g = asset->graph;
    behavior_variable *speed = behavior_asset_add_variable(
        asset,
        "speed",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        NULL,
        NULL
    );
    node *get = behavior_asset_add_get_node(asset, speed->id);
    node *scaled = binary(g, "Multiply", get, real(g, 2));
    test_check(formats(g, scaled, "speed × 2"), "variables and constants format as an expression");
    node *integer = add(g, "Integer");
    pin_value seven = { .type = PIN_I32, .type_name = "int", .i = 7 };
    assert(graph_set_pin_default(g, integer, false, 0, &seven) == 0);
    node *converted = add(g, "Multiply");
    assert(graph_connect_data_conversion(g, integer, 0, converted, 0, &NODE_To_Real) == 0);
    wire(g, real(g, 2), 0, converted, 1);
    test_check(formats(g, converted, "to_real(7) × 2"), "edge conversions format as calls");

    test_check(
        formats(g, binary(g, "Subtract", real(g, 1), binary(g, "Subtract", real(g, 2), real(g, 3))), "1 − (2 − 3)") &&
            formats(g, binary(g, "Multiply", binary(g, "Add", real(g, 1), real(g, 2)), real(g, 3)), "(1 + 2) × 3") &&
            formats(g, binary(g, "Add", real(g, 1), binary(g, "Multiply", real(g, 2), real(g, 3))), "1 + 2 × 3"),
        "expression text groups operands by precedence"
    );
    test_check(
        formats(g, binary(g, "Power", real(g, 2), binary(g, "Power", real(g, 3), real(g, 2))), "2 ^ 3 ^ 2") &&
            formats(g, binary(g, "Power", binary(g, "Power", real(g, 2), real(g, 3)), real(g, 2)), "(2 ^ 3) ^ 2") &&
            formats(g, binary(g, "Power", real(g, -2), real(g, 2)), "(-2) ^ 2"),
        "power associates right and groups negative bases"
    );
    node *negated = add(g, "Negate");
    wire(g, binary(g, "Add", real(g, 1), real(g, 2)), 0, negated, 0);
    test_check(
        formats(g, negated, "−(1 + 2)") &&
            formats(g, binary(g, "Min", real(g, 1), real(g, 2)), "min(1, 2)") &&
            formats(g, binary(g, "Less", real(g, 1), real(g, 2)), "1 < 2"),
        "prefix operators, functions and comparisons use expression syntax"
    );

    node *self = add(g, "Self");
    assert(graph_set_pin_type(g, self, 1, 0, PIN_HANDLE, "ecs.entity") == 0);
    node *self_has = add(g, "Has Component");
    assert(component_node_retarget(g, self_has, GAME_COMPONENT_GameMovement.key) == 0);
    wire(g, self, 0, self_has, component_node_setting(COMPONENT_NODE_HAS) + 1);
    test_check(
        graph_node_is_inline(g, self) && formats(g, self_has, "has(Self.GameMovement)"),
        "a wired Self formats as Self"
    );
    node *getter = add(g, "Get_GameMovement");
    assert(graph_expose_output_fields(g, getter, 0) == 0);
    int field = -1;
    for (int p = 0; p < getter->output_count; p++) {
        const char *name = getter->output_pins[p].name;
        field = name && !strcmp(name, "speed") ? p : field;
    }
    assert(field >= 0);
    node *sum = add(g, "Add");
    wire(g, getter, field, sum, 0);
    wire(g, real(g, 2), 0, sum, 1);
    test_check(
        graph_node_is_inline(g, getter) && formats(g, sum, "Self.GameMovement.speed + 2"),
        "component field reads format like their bindings"
    );

    node *read = add(g, "Get Component");
    assert(component_node_retarget(g, read, GAME_COMPONENT_GameMovement.key) == 0);
    assert(graph_expose_output_fields(g, read, 0) == 0);
    field = -1;
    for (int p = 0; p < read->output_count; p++) {
        const char *name = read->output_pins[p].name;
        field = name && !strcmp(name, "speed") ? p : field;
    }
    assert(field >= 0);
    node *scaled_read = add(g, "Multiply");
    wire(g, read, field, scaled_read, 0);
    wire(g, real(g, 2), 0, scaled_read, 1);
    node *has = add(g, "Has Component");
    assert(component_node_retarget(g, has, GAME_COMPONENT_GameMovement.key) == 0);
    node *missing = add(g, "Not");
    wire(g, has, 0, missing, 0);
    test_check(
        graph_node_is_inline(g, read) && graph_node_is_inline(g, has) &&
            formats(g, scaled_read, "Self.GameMovement.speed × 2") &&
            formats(g, missing, "!has(Self.GameMovement)"),
        "configured component reads fold despite their Component setting"
    );

    /* The values a chip reads, so an editor can name them in its own style. */
    node *reader = add(g, "Reroute Data");
    wire(g, scaled_read, 0, reader, 0);
    char text[128];
    behavior_expression_read *reads = NULL;
    behavior_expression_format(g, reader, 0, text, sizeof text, &reads);
    test_check(
        acount(reads) == 1 && reads[0].start == 0 &&
            reads[0].length == (int)strlen("Self.GameMovement.speed") &&
            reads[0].read.kind == PIN_BINDING_COMPONENT_FIELD &&
            !strcmp(reads[0].read.component_key, GAME_COMPONENT_GameMovement.key) &&
            !strcmp(reads[0].read.field_name, "speed"),
        "formatting lists the component fields a chip reads"
    );
    asetlen(reads, 0);
    behavior_expression_format(g, reader, 0, text, 8, &reads);
    test_check(
        acount(reads) == 1 && reads[0].length == 7 && !strcmp(text, "Self.Ga"),
        "truncated text cuts its last read short"
    );
    afree(reads);
    behavior_asset_free(asset);
}

void run_graph_inline_checks()
{
    eligibility();
    removal();
    signature();
    serialization();
    arrangement();
    formatting();
}
