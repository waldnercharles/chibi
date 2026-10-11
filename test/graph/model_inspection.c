#include "runtime/models/model_inspection.h"

#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/modeling/model_authoring.h"
#include "support/array.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <string.h>

TEST_CASE(model_references_and_part_reports)
{
    behavior_asset *source = behavior_asset_new("ReferenceStudy");
    source->domain = BEHAVIOR_DOMAIN_MODEL;
    model_reference reference = { .image = "reference/camera.png",
                                  .center = { 1.5f, -2.5f },
                                  .span = 7,
                                  .angle = .2f,
                                  .locked = true };
    REQUIRE(behavior_asset_set_model_reference(source, &reference));
    behavior_asset *loaded = behavior_asset_clone(source);
    REQUIRE(loaded && !strcmp(loaded->reference.image, reference.image));
    REQUIRE(loaded->reference.image != source->reference.image);
    REQUIRE(loaded->reference.span == 7 && loaded->reference.locked);
    REQUIRE(loaded->reference.center[1] == -2.5f && loaded->reference.angle == .2f);
    reference.image = "../outside.png";
    REQUIRE(!behavior_asset_set_model_reference(source, &reference));
    reference.image = "reference/camera.png";
    reference.span = NAN;
    REQUIRE(!behavior_asset_set_model_reference(source, &reference));
    REQUIRE(source->reference.span == 7);
    model_reference clear = { 0 };
    REQUIRE(behavior_asset_set_model_reference(loaded, &clear));
    REQUIRE(!loaded->reference.image && source->reference.image);
    behavior_asset_free(loaded);

    graph *g = source->graph;
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    node *a = graph_add_node(g, &NODE_ModelPart);
    node *b = graph_add_node(g, &NODE_ModelPart);
    node *join = graph_add_node(g, &NODE_ModelUnion);
    node *output = model_graph_add_output(g);
    graph_set_node_reference(g, a, 0, "Left");
    graph_set_node_reference(g, b, 0, "Right");
    REQUIRE(!graph_connect_data(g, sphere, 0, a, 0));
    REQUIRE(!graph_connect_data(g, sphere, 0, b, 0));
    REQUIRE(!graph_connect_data(g, a, 0, join, 0));
    REQUIRE(!graph_connect_data(g, b, 0, join, 1));
    REQUIRE(!graph_connect_data(g, join, 0, output, 0));
    model_program program;
    model_program_init(&program);
    shader_error error = { 0 };
    REQUIRE(model_compile(source, &program, &error));
    model_inspection report = { 0 };
    model_program_inspect(&program, &report);
    REQUIRE(acount(report.parts) == 2);
    REQUIRE(report.parts[0].distance_nodes > 0);
    REQUIRE(report.parts[0].distance_nodes == report.parts[1].distance_nodes);
    REQUIRE(report.distance_nodes > report.parts[0].distance_nodes);
    REQUIRE(report.distance_limit == SHADER_NODE_MAX);
    model_program_free(&program);
    behavior_asset_free(source);
    REQUIRE(!strcmp(report.parts[0].asset, "ReferenceStudy"));
    REQUIRE(!strcmp(report.parts[0].name, "Left"));
    model_inspection_free(&report);

    return true;
}

TEST_CASE(instance_anchors_keep_declarations)
{
    shader_error error = { 0 };
    behavior_asset *loaded;
    behavior_asset *module = behavior_asset_new("AnchoredModule");
    module->domain = BEHAVIOR_DOMAIN_MODEL;
    node *anchor = graph_add_node(module->graph, registry_node_by_name("Model Anchor"));
    REQUIRE(!graph_set_node_reference(module->graph, anchor, 0, "Lens mount"));
    behavior_asset *assembly = behavior_asset_new("Assembly");
    assembly->domain = BEHAVIOR_DOMAIN_MODEL;
    node *instance = graph_add_node(assembly->graph, &NODE_ModelInstance);
    REQUIRE(!graph_set_node_reference_asset(assembly->graph, instance, module->name));
    int pin = model_instance_anchor(assembly->graph, instance, module, "Lens mount", &error);
    REQUIRE(pin > 0 && instance->output_pins[pin].declaration_id == anchor->id);
    REQUIRE(
        model_instance_anchor(assembly->graph, instance, module, "Lens mount", &error) == pin
    );
    loaded = behavior_asset_clone(assembly);
    REQUIRE(
        loaded &&
        loaded->graph->nodes[0]->output_pins[pin].declaration_id == anchor->id
    );
    REQUIRE(!strcmp(loaded->graph->nodes[0]->output_pins[pin].type_name, "Anchor3D"));
    behavior_asset_free(loaded);
    behavior_asset_free(assembly);
    behavior_asset_free(module);
    return true;
}

void test_model_inspection()
{
    test_graph_packs_install();
    RUN_TEST_CASE(model_references_and_part_reports);
    RUN_TEST_CASE(instance_anchors_keep_declarations);
    test_graph_packs_restore();
}
