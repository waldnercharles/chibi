#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "graph/shader/shader_graph.h"
#include "render/shader_catalog.h"
#include "support/array.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static behavior_asset *model(const char *name)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_MODEL;
    return a;
}
static void scalar(graph *g, node *n, int pin, float number)
{
    pin_value value = { .type = PIN_F32, .f = number };
    assert(!graph_set_pin_default(g, n, false, pin, &value));
}
static void vector(graph *g, node *n, int pin, float x, float y, float z)
{
    float xyz[3] = { x, y, z };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = xyz };
    assert(!graph_set_pin_default(g, n, false, pin, &value));
}
static void connect(graph *g, node *from, node *to, int pin)
{
    assert(!graph_connect_data(g, from, 0, to, pin));
}
static void publish(graph *g, node *from)
{
    connect(g, from, model_graph_add_output(g), 0);
}
static node *part(graph *g, const char *name, float radius)
{
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    scalar(g, sphere, 0, radius);
    node *part = graph_add_node(g, &NODE_ModelPart);
    assert(!graph_set_node_reference(g, part, 0, name));
    connect(g, sphere, part, 0);
    return part;
}
static node *translated(graph *g, node *source, float x, float y, float z)
{
    node *n = graph_add_node(g, &NODE_ModelTranslate);
    vector(g, n, 1, x, y, z);
    connect(g, source, n, 0);
    return n;
}
static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < .0001f;
}
static behavior_asset *material(const char *name, float red, float green, float blue);
static node *slot(graph *g, const char *name, const char *material);

TEST_CASE(material_coordinate_inputs)
{
    const char *names[] = { "Shader ObjectPosition", "Shader PartPosition" };
    shader_sample_context context = { .object_position = { 11, -7, 5 },
                                      .part_position = { 1, 2, -3 } };
    const float *expected[] = { context.object_position, context.part_position };
    for (int i = 0; i < 2; i++) {
        behavior_asset *a = behavior_asset_new(names[i]);
        a->domain = BEHAVIOR_DOMAIN_MATERIAL;
        const node_desc *descriptor = registry_node_by_name(names[i]);
        REQUIRE(descriptor);
        node *position = graph_add_node(a->graph, descriptor);
        connect(a->graph, position, shader_graph_add_output(a->graph, false), 0);
        shader_graph ir;
        shader_graph_init(&ir);
        shader_error error = { 0 };
        REQUIRE(shader_graph_from_graph(a->graph, &ir, &error));
        float result[SHADER_OUTPUT_MAX][4];
        REQUIRE(shader_graph_eval_outputs(&ir, &context, NULL, result, &error));
        for (int axis = 0; axis < 3; axis++) {
            REQUIRE(near(result[0][axis], expected[i][axis]));
        }
        shader_graph_free(&ir);
        behavior_asset_free(a);
    }
    return true;
}

static model_hit hit(const model_program *p, float x, float y, float z)
{
    float prepared[32][4];
    model_bounds bounds;
    model_hit result;
    shader_error error = { 0 };
    assert(model_program_prepare(p, NULL, prepared, &bounds, &error));
    assert(model_program_hit(p, prepared, (float[3]){ x, y, z }, &result, &error));
    return result;
}

/* Whether the part `name` and its paint own the surface at x. */
static bool owner(const model_program *p, float x, const char *name)
{
    model_hit result = hit(p, x, 0, 0);
    const char *paint = !strcmp(name, "Left") ? "LeftPaint" : "RightPaint";
    return result.part >= 0 && result.part < acount(p->parts) &&
           !strcmp(p->parts[result.part].name, name) && result.material >= 0 &&
           result.material < acount(p->material_slots) &&
           !strcmp(p->material_slots[result.material].material, paint);
}

TEST_CASE(csg_ownership)
{
    const node_desc *operations[] = { &NODE_ModelUnion,
                                      &NODE_ModelIntersection,
                                      &NODE_ModelDifference,
                                      &NODE_ModelSmoothUnion };
    for (int i = 0; i < 4; i++) {
        behavior_asset *a = model("Ownership");
        graph *g = a->graph;
        node *left = part(g, "Left", 2), *right = part(g, "Right", i == 2 ? 1 : 2);
        behavior_asset *left_paint = material("LeftPaint", 1, 0, 0);
        behavior_asset *right_paint = material("RightPaint", 0, 0, 1);
        connect(g, slot(g, "LeftColor", left_paint->name), left, 1);
        connect(g, slot(g, "RightColor", right_paint->name), right, 1);
        left = translated(g, left, i == 2 ? 0 : -1, 0, 0);
        right = translated(g, right, 1, 0, 0);
        node *operation = graph_add_node(g, operations[i]);
        connect(g, left, operation, 0);
        connect(g, right, operation, 1);
        if (i == 3) {
            scalar(g, operation, 2, .5f);
        }
        publish(g, operation);
        model_program p;
        model_program_init(&p);
        shader_error error = { 0 };
        const behavior_asset *catalog[] = { a, left_paint, right_paint };
        REQUIRE(model_compile_with_catalog(a, catalog, 3, &p, &error));
        if (i == 0) {
            REQUIRE(owner(&p, -3, "Left"));
            REQUIRE(owner(&p, 3, "Right"));
        }
        if (i == 1) {
            REQUIRE(owner(&p, -1, "Right"));
            REQUIRE(owner(&p, 1, "Left"));
        }
        if (i == 2) {
            /* The cutter supplies the active zero, but its material never owns the cut. */
            REQUIRE(owner(&p, 0, "Left"));
            REQUIRE(owner(&p, -2, "Left"));
        }
        if (i == 3) {
            REQUIRE(owner(&p, -1, "Left"));
            REQUIRE(owner(&p, 1, "Right"));
        }
        REQUIRE(owner(&p, 0, "Left"));
        model_program_free(&p);
        behavior_asset_free(a);
        behavior_asset_free(left_paint);
        behavior_asset_free(right_paint);
    }
    return true;
}

TEST_CASE(transformed_part_coordinates)
{
    behavior_asset *a = model("OffCenterPart");
    graph *g = a->graph;
    node *source = part(g, "Local", 2);
    node *scale = graph_add_node(g, &NODE_ModelScale);
    vector(g, scale, 1, 2, 1, .5f);
    connect(g, source, scale, 0);
    node *rotate = graph_add_node(g, &NODE_ModelRotate);
    vector(g, rotate, 1, 0, 0, 1.5707963267948966f);
    connect(g, scale, rotate, 0);
    publish(g, translated(g, rotate, 11, -7, 5));
    model_program p;
    model_program_init(&p);
    shader_error error = { 0 };
    REQUIRE(model_compile(a, &p, &error));
    model_hit result = hit(&p, 11, -3, 5);
    REQUIRE(result.part >= 0 && !strcmp(p.parts[result.part].name, "Local"));
    REQUIRE(near(result.position[0], 2));
    REQUIRE(near(result.position[1], 0));
    REQUIRE(near(result.position[2], 0));
    for (int axis = 0; axis < 3; axis++) {
        REQUIRE(
            near((p.bounds.min[axis] + p.bounds.max[axis]) * .5f, (float[3]){ 11, -7, 5 }[axis])
        );
    }
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}

TEST_CASE(transformed_part_frame)
{
    behavior_asset *a = model("FramePlane");
    graph *g = a->graph;
    node *source = part(g, "Plane", 2);
    node *scale = graph_add_node(g, &NODE_ModelScale);
    vector(g, scale, 1, 2, 1, .5f);
    connect(g, source, scale, 0);
    node *reflect = graph_add_node(g, &NODE_ModelReflect);
    pin_value axis = { .type = PIN_ENUM, .type_name = "ModelAxis", .e = 0 };
    REQUIRE(!graph_set_pin_default(g, reflect, false, 1, &axis));
    connect(g, scale, reflect, 0);
    node *rotate = graph_add_node(g, &NODE_ModelRotate);
    vector(g, rotate, 1, 0, 0, 1.5707963267948966f);
    connect(g, reflect, rotate, 0);
    publish(g, translated(g, rotate, 1000000, -700000, 500000));
    model_program p;
    model_program_init(&p);
    shader_error error = { 0 };
    REQUIRE(model_compile(a, &p, &error));
    model_hit result = hit(&p, 1000000, -700000, 500000);
    const float expected[3][3] = { { 0, -.5f, 0 }, { -1, 0, 0 }, { 0, 0, 2 } };
    for (int row = 0; row < 3; row++) {
        for (int j = 0; j < 3; j++) {
            REQUIRE(near(result.part_from_object[row][j], expected[row][j]));
        }
    }
    // Independent forward-transformed tangent plane: reflection reverses winding.
    const float tangent[2][3] = { { 1, -4, 0 }, { 0, -6, -.5f } },
                local_normal[3] = { 1, 2, 3 };
    float normal[3] = { 0 };
    for (int j = 0; j < 3; j++) {
        for (int row = 0; row < 3; row++) {
            normal[j] += local_normal[row] * result.part_from_object[row][j];
        }
    }
    for (int i = 0; i < 2; i++) {
        REQUIRE(near(
            normal[0] * tangent[i][0] + normal[1] * tangent[i][1] +
                normal[2] * tangent[i][2],
            0
        ));
    }
    REQUIRE(near(normal[0], -2));
    REQUIRE(near(normal[1], -.5f));
    REQUIRE(near(normal[2], 6));
    float point[3] = { 1000001, -700004, 500000 }, local[3], rows[3][3];
    REQUIRE(model_program_part_point(&p, NULL, result.part, point, local, rows, &error));
    REQUIRE(near(local[0], 2));
    REQUIRE(near(local[1], -1));
    REQUIRE(near(local[2], 0));
    char *glsl = model_program_frame_glsl(&p, "test_frame", &error);
    REQUIRE(glsl && strstr(glsl, "test_frame_part_0") && strstr(glsl, "sg_out[4]"));
    free(glsl);
    model_program_free(&p);
    behavior_asset_free(a);

    a = model("FixedOwner");
    g = a->graph;
    node *left = translated(g, part(g, "Left", 2), -10, 0, 0),
         *right = translated(g, part(g, "Right", 2), 10, 0, 0);
    node *unite = graph_add_node(g, &NODE_ModelUnion);
    connect(g, left, unite, 0);
    connect(g, right, unite, 1);
    publish(g, unite);
    model_program_init(&p);
    REQUIRE(model_compile(a, &p, &error));
    int left_owner = hit(&p, -10, 0, 0).part;
    REQUIRE(hit(&p, 10, 0, 0).part != left_owner);
    REQUIRE(
        model_program_part_point(&p, NULL, left_owner, (float[3]){ 10, 0, 0 }, local, rows, &error)
    );
    REQUIRE(near(local[0], 20));
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            REQUIRE(near(rows[i][j], i == j ? 1 : 0));
        }
    }
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}

TEST_CASE(extreme_part_frames)
{
    const float factors[] = { 1e20f, 1e-30f };
    for (int i = 0; i < 2; i++) {
        behavior_asset *a = model("ExtremeFrame");
        graph *g = a->graph;
        node *shape = part(g, "Local", i == 0 ? 1e-20f : 1);
        node *scale = graph_add_node(g, &NODE_ModelScale);
        vector(g, scale, 1, factors[i], i == 0 ? factors[i] : 1, i == 0 ? factors[i] : 1);
        connect(g, shape, scale, 0);
        publish(g, scale);
        model_program p;
        model_program_init(&p);
        shader_error error = { 0 };
        REQUIRE(model_compile(a, &p, &error));
        float rows[3][3];
        REQUIRE(model_program_frame(&p, 0, NULL, rows, &error));
        REQUIRE(isfinite(rows[0][0]) && rows[0][0] > 0);
        REQUIRE(fabsf(rows[0][0] * factors[i] - 1) < 1e-5f);
        model_program_free(&p);
        behavior_asset_free(a);
    }
    return true;
}

TEST_CASE(isolated_part_shaders)
{
    behavior_asset *a = model("HiddenPart");
    graph *g = a->graph;
    node *outer = part(g, "Outer", 4), *inner = part(g, "Inner", 1),
         *combine = graph_add_node(g, &NODE_ModelUnion);
    connect(g, outer, combine, 0);
    connect(g, translated(g, inner, 1, 0, 0), combine, 1);
    node *group = graph_add_node(g, &NODE_ModelPart);
    REQUIRE(!graph_set_node_reference(g, group, 0, "Group"));
    connect(g, combine, group, 0);
    node *scale = graph_add_node(g, &NODE_ModelScale);
    vector(g, scale, 1, 2, 1, .5f);
    connect(g, group, scale, 0);
    publish(g, translated(g, scale, 10, 0, 0));
    model_program p;
    model_program_init(&p);
    shader_error error = { 0 };
    REQUIRE(model_compile(a, &p, &error));
    int inner_index = -1, group_index = -1;
    for (int i = 0; i < acount(p.parts); i++) {
        if (!strcmp(p.parts[i].name, "Inner")) {
            inner_index = i;
        }
        if (!strcmp(p.parts[i].name, "Group")) {
            group_index = i;
        }
    }
    REQUIRE(inner_index >= 0 && group_index >= 0);
    model_hit ordinary = hit(&p, 12, 0, 0);
    REQUIRE(ordinary.part != inner_index);
    shader_graph distance, resolver;
    shader_graph_init(&distance);
    shader_graph_init(&resolver);
    REQUIRE(model_program_part_shaders(&p, inner_index, &distance, &resolver, &error));
    float prepared[32][4];
    model_bounds bounds;
    REQUIRE(model_program_prepare(&p, NULL, prepared, &bounds, &error));
    shader_sample_context sample = { .object_position = { 12, 0, 0 } };
    float result[SHADER_OUTPUT_MAX][4];
    REQUIRE(shader_graph_eval_outputs(&distance, &sample, prepared, result, &error));
    REQUIRE(near(result[0][0], -1));
    REQUIRE(shader_graph_eval_outputs(&resolver, &sample, prepared, result, &error));
    REQUIRE((int)result[0][0] == inner_index);
    REQUIRE(near(result[1][0], 0));
    REQUIRE(model_program_part_shaders(&p, group_index, &distance, &resolver, &error));
    REQUIRE(shader_graph_eval_outputs(&resolver, &sample, prepared, result, &error));
    REQUIRE((int)result[0][0] == ordinary.part);
    shader_graph_free(&distance);
    shader_graph_free(&resolver);
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}

static behavior_asset *material(const char *name, float red, float green, float blue)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *output = shader_graph_add_output(a->graph, false);
    vector(a->graph, output, 0, red, green, blue);
    return a;
}

static node *slot(graph *g, const char *name, const char *material)
{
    node *n = graph_add_node(g, &NODE_ModelMaterialSlot);
    assert(!graph_set_node_reference(g, n, 0, name));
    n->reference_asset = graph_own_str(g, material, strlen(material));
    return n;
}

static behavior_asset *parameter_material(const char *name, int count)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = a->graph;
    node *sum = NULL;
    for (int i = 0; i < count; i++) {
        node *parameter = graph_add_node(g, registry_node_by_name("Shader Parameter"));
        char name[32];
        snprintf(name, sizeof name, "Color%d", i);
        assert(!graph_set_node_reference(g, parameter, i, name));
        assert(!graph_set_pin_type(g, parameter, true, 0, PIN_STRUCT, "ShaderVec3"));
        float white[3] = { 1, 1, 1 };
        pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = white };
        assert(!graph_set_pin_default(g, parameter, true, 0, &value));
        if (!sum) {
            sum = parameter;
            continue;
        }
        node *add = graph_add_node(g, registry_node_by_name("Shader Add"));
        connect(g, sum, add, 0);
        connect(g, parameter, add, 1);
        sum = add;
    }
    assert(sum);
    connect(g, sum, shader_graph_add_output(g, false), 0);
    return a;
}

static behavior_asset *ramp_material(const char *name, int count)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = a->graph;
    node *sum = NULL;
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    for (int i = 0; i < SHADER_RAMP_MAX; i++) {
        stops[i] = (shader_ramp_stop){ .position = (float)i / (SHADER_RAMP_MAX - 1),
                                       .color = { (float)i / SHADER_RAMP_MAX, 0, 0, 1 } };
    }
    for (int i = 0; i < count; i++) {
        node *ramp = graph_add_node(g, registry_node_by_name("Shader Color Ramp"));
        assert(shader_node_ramp_set(g, ramp, stops, SHADER_RAMP_MAX));
        if (!sum) {
            sum = ramp;
            continue;
        }
        node *add = graph_add_node(g, registry_node_by_name("Shader Add"));
        connect(g, sum, add, 0);
        connect(g, ramp, add, 1);
        sum = add;
    }
    connect(g, sum, shader_graph_add_output(g, false), 0);
    return a;
}

static behavior_asset *painted_pair(const char *name, const char *left, const char *right)
{
    behavior_asset *a = model(name);
    node *first = part(a->graph, "Left", 1), *second = part(a->graph, "Right", 1);
    connect(a->graph, slot(a->graph, "Left", left), first, 1);
    connect(a->graph, slot(a->graph, "Right", right), second, 1);
    node *combine = graph_add_node(a->graph, &NODE_ModelUnion);
    connect(a->graph, translated(a->graph, first, -2, 0, 0), combine, 0);
    connect(a->graph, translated(a->graph, second, 2, 0, 0), combine, 1);
    publish(a->graph, combine);
    return a;
}

TEST_CASE(material_assignment_lifecycle)
{
    behavior_asset *red = material("PaintRed", 1, 0, 0);
    behavior_asset *blue = material("PaintBlue", 0, 0, 1);
    behavior_asset *a = model("PaintedSphere");
    node *surface = part(a->graph, "Body", 2);
    node *paint = slot(a->graph, "Paint", red->name);
    connect(a->graph, paint, surface, 1);
    publish(a->graph, translated(a->graph, surface, 11, -7, 5));
    const behavior_asset *catalog[] = { a, red, blue };
    model_program p;
    model_program_init(&p);
    shader_error error = { 0 };
    REQUIRE(model_compile_with_catalog(a, catalog, 3, &p, &error));
    model_hit result = hit(&p, 13, -7, 5);
    REQUIRE(result.material >= 0 && result.material < acount(p.material_slots));
    REQUIRE(!strcmp(p.material_slots[result.material].material, red->name));
    REQUIRE(near(result.position[0], 2));
    char *path = strdup(p.parts[result.part].path);
    REQUIRE(path);
    char *text = behavior_asset_save(a);
    behavior_asset *reload = behavior_asset_load(text);
    REQUIRE(reload);
    const behavior_asset *reloaded[] = { reload, red, blue };
    REQUIRE(model_compile_with_catalog(reload, reloaded, 3, &p, &error));
    result = hit(&p, 13, -7, 5);
    REQUIRE(p.parts[result.part].node == surface->id);
    REQUIRE(!strcmp(p.parts[result.part].path, path));
    REQUIRE(p.material_slots[result.material].node == paint->id);
    /* Missing material source cannot replace the previous hit generation. */
    REQUIRE(!model_compile_with_catalog(a, catalog, 1, &p, &error));
    REQUIRE(error.node == paint->id);
    result = hit(&p, 13, -7, 5);
    REQUIRE(!strcmp(p.material_slots[result.material].material, red->name));
    model_material_bindings bindings;
    model_material_bindings_init(&bindings);
    paint->reference_asset = graph_own_str(a->graph, blue->name, strlen(blue->name));
    REQUIRE(model_program_compile_bindings(&p, a, catalog, 3, &bindings, &error) == MODEL_BINDINGS_READY);
    REQUIRE(acount(bindings.slots) == 1);
    REQUIRE(!strcmp(bindings.slots[0].material, blue->name));
    node *alternate = slot(a->graph, "AlternatePaint", red->name);
    connect(a->graph, alternate, surface, 1);
    REQUIRE(model_program_compile_bindings(&p, a, catalog, 3, &bindings, &error) == MODEL_BINDINGS_READY);
    REQUIRE(acount(bindings.slots) == 1);
    REQUIRE(bindings.slots[0].node == alternate->id);
    REQUIRE(!strcmp(bindings.slots[0].material, red->name));
    /* Binding refresh must leave retained geometry and its old assignment immutable. */
    result = hit(&p, 13, -7, 5);
    REQUIRE(!strcmp(p.material_slots[result.material].material, red->name));
    model_material_bindings_free(&bindings);
    model_program_free(&p);
    behavior_asset_free(reload);
    behavior_asset_free(a);
    behavior_asset_free(red);
    behavior_asset_free(blue);
    free(path);
    free(text);
    return true;
}

TEST_CASE(reused_nested_materials)
{
    behavior_asset *red = material("ReuseRed", 1, 0, 0);
    behavior_asset *blue = material("ReuseBlue", 0, 0, 1);
    behavior_asset *base = model("PaintedComponent"), *a = model("PaintedInstances");
    node *inner = part(base->graph, "Inner", 1);
    node *paint = slot(base->graph, "Paint", red->name);
    connect(base->graph, paint, inner, 1);
    node *group = graph_add_node(base->graph, &NODE_ModelPart);
    REQUIRE(!graph_set_node_reference(base->graph, group, 0, "Group"));
    connect(base->graph, translated(base->graph, inner, 3, 0, 0), group, 0);
    publish(base->graph, group);
    node *left = graph_add_node(a->graph, &NODE_ModelInstance);
    node *right = graph_add_node(a->graph, &NODE_ModelInstance);
    left->reference_asset = graph_own_str(a->graph, base->name, strlen(base->name));
    right->reference_asset = graph_own_str(a->graph, base->name, strlen(base->name));
    pin_spec override = { .name = "Paint",
                          .type = PIN_STR,
                          .type_name = "MaterialAsset",
                          .declaration_id = paint->id,
                          .default_value = {
                              .type = PIN_STR,
                              .type_name = "MaterialAsset",
                              .text = &(const pin_string){
                                  .bytes = blue->name,
                                  .length = (uint32_t)strlen(blue->name) } } };
    REQUIRE(graph_add_pin(a->graph, right, false, &override) == 0);
    node *combine = graph_add_node(a->graph, &NODE_ModelUnion);
    connect(a->graph, translated(a->graph, left, -10, 0, 0), combine, 0);
    connect(a->graph, translated(a->graph, right, 10, 0, 0), combine, 1);
    publish(a->graph, combine);
    const behavior_asset *catalog[] = { a, base, red, blue };
    model_program p;
    model_program_init(&p);
    shader_error error = { 0 };
    REQUIRE(model_compile_with_catalog(a, catalog, 4, &p, &error));
    model_hit l = hit(&p, -6, 0, 0), r = hit(&p, 14, 0, 0);
    REQUIRE(l.part != r.part);
    REQUIRE(p.parts[l.part].node == inner->id && p.parts[r.part].node == inner->id);
    REQUIRE(!strcmp(p.parts[l.part].name, "Inner") && !strcmp(p.parts[r.part].name, "Inner"));
    REQUIRE(strcmp(p.parts[l.part].path, p.parts[r.part].path));
    REQUIRE(near(l.position[0], 1));
    REQUIRE(near(r.position[0], 1));
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            REQUIRE(near(l.part_from_object[i][j], i == j ? 1 : 0));
            REQUIRE(near(r.part_from_object[i][j], i == j ? 1 : 0));
        }
    }
    REQUIRE(!strcmp(p.material_slots[l.material].material, red->name));
    REQUIRE(!strcmp(p.material_slots[r.material].material, blue->name));
    REQUIRE(l.material != r.material);
    char *source = behavior_asset_save(a), *base_source = behavior_asset_save(base);
    behavior_asset *reload = behavior_asset_load(source),
                   *base_reload = behavior_asset_load(base_source);
    REQUIRE(reload && base_reload);
    const behavior_asset *reloaded[] = { reload, base_reload, red, blue };
    REQUIRE(model_compile_with_catalog(reload, reloaded, 4, &p, &error));
    l = hit(&p, -6, 0, 0);
    r = hit(&p, 14, 0, 0);
    REQUIRE(!strcmp(p.material_slots[l.material].material, red->name));
    REQUIRE(!strcmp(p.material_slots[r.material].material, blue->name));
    REQUIRE(p.material_slots[r.material].node == paint->id);
    model_program_free(&p);
    behavior_asset_free(reload);
    behavior_asset_free(base_reload);
    behavior_asset_free(a);
    behavior_asset_free(base);
    behavior_asset_free(red);
    behavior_asset_free(blue);
    free(source);
    free(base_source);
    return true;
}

TEST_CASE(catalog_material_edits)
{
    authoring_shader_catalog_clear();
    behavior_asset *red = material("CatalogRed", 1, 0, 0);
    behavior_asset *blue = material("CatalogBlue", 0, 0, 1);
    behavior_asset *a = model("CatalogPainted");
    node *body = part(a->graph, "Body", 2);
    node *paint = slot(a->graph, "BodyPaint", red->name);
    connect(a->graph, paint, body, 1);
    slot(a->graph, "UnusedPaint", blue->name);
    publish(a->graph, body);
    shader_error error = { 0 };
    REQUIRE(render_shader_upsert(red, &error));
    REQUIRE(render_shader_upsert(blue, &error));
    const behavior_asset *models[] = { a };
    REQUIRE(render_shader_models_upsert(models, 1, &error));
    int index = render_shader_find(a->name);
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4], parameters[32][4];
    render_shader_set resources = { 0 };
    REQUIRE(render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        NULL,
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    int material_id = (int)bindings[1][0];
    REQUIRE(material_id == render_shader_find(red->name));
    shader_sample_context context = { .object_position = { 2, 0, 0 },
                                      .part_position = { 2, 0, 0 } };
    float result[SHADER_OUTPUT_MAX][4];
    REQUIRE(render_shader_sample(material_id, &context, parameters, result, &error));
    REQUIRE(near(result[0][0], 1));
    REQUIRE(near(result[0][1], 0));
    render_shader_model_stats_reset();
    node *output = red->graph->nodes[0];
    vector(red->graph, output, 0, 0, 1, 0);
    REQUIRE(render_shader_upsert(red, &error));
    REQUIRE(render_shader_model_stats().compilations == 0);
    REQUIRE(render_shader_sample(material_id, &context, parameters, result, &error));
    REQUIRE(near(result[0][0], 0));
    REQUIRE(near(result[0][1], 1));
    REQUIRE(!render_shader_material_remove(red->name, &error));
    REQUIRE(render_shader_find(red->name) == material_id);
    REQUIRE(!render_shader_material_remove(blue->name, &error));
    REQUIRE(render_shader_find(blue->name));
    behavior_asset *invalid = behavior_asset_new(red->name);
    invalid->domain = BEHAVIOR_DOMAIN_MATERIAL;
    REQUIRE(!render_shader_upsert(invalid, &error));
    REQUIRE(render_shader_sample(material_id, &context, parameters, result, &error));
    REQUIRE(near(result[0][1], 1));
    REQUIRE(render_shader_material_rename(red->name, "CatalogGreen", &error));
    model_material_slot applied;
    REQUIRE(render_shader_model_material_slot(index, 0, &applied));
    REQUIRE(!strcmp(applied.material, "CatalogGreen"));
    REQUIRE(render_shader_find("CatalogGreen") == material_id);
    REQUIRE(render_shader_model_stats().compilations == 0);
    REQUIRE(!strcmp(red->name, "CatalogRed"));
    behavior_asset *renamed = behavior_asset_clone(red);
    REQUIRE(renamed && !behavior_asset_set_name(renamed, "CatalogGreen"));
    unsigned ramp_revision = render_shader_ramp_revision();
    REQUIRE(render_shader_upsert(renamed, &error));
    REQUIRE(render_shader_ramp_revision() == ramp_revision);
    behavior_asset_free(renamed);
    const char *renamed_model = "CatalogPaintedRenamed";
    REQUIRE(render_shader_model_rename(a->name, renamed_model, &error));
    REQUIRE(render_shader_find(renamed_model) == index);
    behavior_asset *parent = model("CatalogOverride");
    node *instance = graph_add_node(parent->graph, &NODE_ModelInstance);
    instance->reference_asset = graph_own_str(parent->graph, renamed_model, strlen(renamed_model));
    pin_spec override = { .name = "BodyPaint",
                          .type = PIN_STR,
                          .type_name = "MaterialAsset",
                          .declaration_id = paint->id,
                          .default_value = {
                              .type = PIN_STR,
                              .type_name = "MaterialAsset",
                              .text = &(const pin_string){
                                  .bytes = blue->name,
                                  .length = (uint32_t)strlen(blue->name) } } };
    REQUIRE(graph_add_pin(parent->graph, instance, false, &override) == 0);
    publish(parent->graph, instance);
    REQUIRE(render_shader_upsert(parent, &error));
    int parent_id = render_shader_find(parent->name),
        blue_id = render_shader_find(blue->name);
    REQUIRE(render_shader_material_rename(blue->name, "CatalogBlueRenamed", &error));
    REQUIRE(render_shader_find("CatalogBlueRenamed") == blue_id);
    REQUIRE(render_shader_model_rename(parent->name, "CatalogOverrideRenamed", &error));
    REQUIRE(render_shader_find("CatalogOverrideRenamed") == parent_id);
    REQUIRE(render_shader_model_material_slot(parent_id, 0, &applied));
    REQUIRE(!strcmp(applied.material, "CatalogBlueRenamed"));
    REQUIRE(!strcmp(pin_text(instance->input_pins[0].default_value).bytes, "CatalogBlue"));
    behavior_asset_free(parent);
    behavior_asset_free(invalid);
    behavior_asset_free(red);
    behavior_asset_free(blue);
    behavior_asset_free(a);
    authoring_shader_catalog_clear();
    return true;
}

TEST_CASE(flattened_parameter_limit)
{
    authoring_shader_catalog_clear();
    behavior_asset *left = parameter_material("ParametersLeft", 17);
    behavior_asset *right = parameter_material("ParametersRight", 15);
    behavior_asset *a = painted_pair("ParameterPaintPair", left->name, right->name);
    shader_error error = { 0 };
    REQUIRE(render_shader_upsert(left, &error));
    REQUIRE(render_shader_upsert(right, &error));
    const behavior_asset *models[] = { a };
    REQUIRE(render_shader_models_upsert(models, 1, &error));
    int index = render_shader_find(a->name);
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4], parameters[32][4];
    render_shader_set resources = { 0 };
    REQUIRE(render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        NULL,
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    REQUIRE((int)bindings[1][2] + (int)bindings[2][2] == 32);
    model_material_slot applied;
    REQUIRE(render_shader_model_material_slot(index, 0, &applied));
    char text[1024];
    snprintf(
        text,
        sizeof text,
        "(array)\"material.%s.Color0\" 0 0.5 0.25\n",
        applied.name
    );
    REQUIRE(render_shader_parameters_validate(index, text, &error));
    REQUIRE(render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        text,
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    int first_offset = (int)bindings[1][1], second_offset = (int)bindings[2][1];
    REQUIRE(near(parameters[first_offset][0], 0));
    REQUIRE(near(parameters[first_offset][1], .5f));
    REQUIRE(near(parameters[first_offset][2], .25f));
    REQUIRE(near(parameters[second_offset][0], 1));
    float valid_bindings[MODEL_MATERIAL_SLOT_MAX + 1][4], valid_parameters[32][4];
    render_shader_set valid_resources = resources;
    memcpy(valid_bindings, bindings, sizeof bindings);
    memcpy(valid_parameters, parameters, sizeof parameters);
    /* Color0 appears in both materials and cannot be overridden as a bare name. */
    REQUIRE(!render_shader_parameters_validate(index, "(array)Color0 0 0 0\n", &error));
    REQUIRE(!render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        "(array)Color0 0 0 0\n",
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    REQUIRE(!memcmp(valid_bindings, bindings, sizeof bindings));
    REQUIRE(!memcmp(valid_parameters, parameters, sizeof parameters));
    REQUIRE(!memcmp(&valid_resources, &resources, sizeof resources));
    REQUIRE(render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        NULL,
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    float before_bindings[MODEL_MATERIAL_SLOT_MAX + 1][4], before_parameters[32][4];
    memcpy(before_bindings, bindings, sizeof bindings);
    memcpy(before_parameters, parameters, sizeof parameters);
    behavior_asset *too_many = parameter_material(right->name, 16);
    render_shader_model_stats_reset();
    REQUIRE(!render_shader_upsert(too_many, &error));
    REQUIRE(strstr(error.message, "32") || strstr(error.message, "parameter"));
    REQUIRE(render_shader_model_stats().compilations == 0);
    REQUIRE(render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        NULL,
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    REQUIRE(!memcmp(before_bindings, bindings, sizeof bindings));
    REQUIRE(!memcmp(before_parameters, parameters, sizeof parameters));
    behavior_asset_free(too_many);
    behavior_asset_free(left);
    behavior_asset_free(right);
    behavior_asset_free(a);
    authoring_shader_catalog_clear();
    return true;
}

TEST_CASE(combined_ramp_limit)
{
    authoring_shader_catalog_clear();
    behavior_asset *left = ramp_material("RampsLeft", 8),
                   *right = ramp_material("RampsRight", 8);
    behavior_asset *a = painted_pair("RampPaintPair", left->name, right->name);
    shader_error error = { 0 };
    REQUIRE(render_shader_upsert(left, &error));
    REQUIRE(render_shader_upsert(right, &error));
    const behavior_asset *models[] = { a };
    REQUIRE(render_shader_models_upsert(models, 1, &error));
    int index = render_shader_find(a->name);
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4], parameters[32][4];
    render_shader_set resources = { 0 };
    REQUIRE(render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        NULL,
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    REQUIRE(render_shader_ramp_vec4_count(&resources) == 512);
    behavior_asset *too_many = ramp_material(right->name, 9);
    render_shader_model_stats_reset();
    REQUIRE(!render_shader_upsert(too_many, &error));
    REQUIRE(strstr(error.message, "512") || strstr(error.message, "ramp"));
    REQUIRE(render_shader_model_stats().compilations == 0);
    REQUIRE(render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        NULL,
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    REQUIRE(render_shader_ramp_vec4_count(&resources) == 512);
    behavior_asset_free(too_many);
    behavior_asset_free(left);
    behavior_asset_free(right);
    behavior_asset_free(a);
    authoring_shader_catalog_clear();
    return true;
}

TEST_CASE(scoped_namespace_collision)
{
    authoring_shader_catalog_clear();
    behavior_asset *paint = parameter_material("NamespacePaint", 1);
    behavior_asset *other = parameter_material("NamespaceOther", 1);
    REQUIRE(!graph_set_node_reference(other->graph, other->graph->nodes[0], 0, "Normal"));
    behavior_asset *a = painted_pair("NamespaceModel", paint->name, other->name);
    for (int i = 0; i < acount(a->graph->nodes); i++) {
        node *n = a->graph->nodes[i];
        if (n->desc != &NODE_ModelMaterialSlot) {
            continue;
        }
        REQUIRE(
            !graph_set_node_reference(a->graph, n, 0, !strcmp(n->reference_name, "Left") ? "Paint" : "Other")
        );
    }
    shader_error error = { 0 };
    REQUIRE(render_shader_upsert(paint, &error));
    REQUIRE(render_shader_upsert(other, &error));
    const behavior_asset *models[] = { a };
    REQUIRE(render_shader_models_upsert(models, 1, &error));
    int index = render_shader_find(a->name);
    model_material_slot applied;
    REQUIRE(render_shader_model_material_slot(index, 0, &applied));
    char lookup[1024], overrides[1200];
    snprintf(lookup, sizeof lookup, "%s.Color0", applied.name);
    snprintf(overrides, sizeof overrides, "(array)\"material.%s\" 0 0.5 0.25\n", lookup);
    REQUIRE(render_shader_parameters_validate(index, overrides, &error));
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4], parameters[32][4];
    render_shader_set resources = { 0 };
    REQUIRE(render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        overrides,
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    int offset = (int)bindings[1][1];
    REQUIRE(near(parameters[offset][0], 0));
    REQUIRE(near(parameters[offset][1], .5f));
    REQUIRE(near(parameters[offset][2], .25f));
    float before_bindings[MODEL_MATERIAL_SLOT_MAX + 1][4], before_parameters[32][4];
    memcpy(before_bindings, bindings, sizeof bindings);
    memcpy(before_parameters, parameters, sizeof parameters);
    render_shader_set before_resources = resources;
    unsigned revision = render_shader_revision();
    behavior_asset *collision = parameter_material(other->name, 1);
    REQUIRE(!graph_set_node_reference(collision->graph, collision->graph->nodes[0], 0, lookup));
    render_shader_model_stats_reset();
    REQUIRE(!render_shader_upsert(collision, &error));
    REQUIRE(strstr(error.message, "namespace collision"));
    REQUIRE(render_shader_revision() == revision);
    REQUIRE(render_shader_model_stats().compilations == 0);
    REQUIRE(render_shader_model_material_snapshot(
        index,
        NULL,
        (ecs_entity){ 0 },
        overrides,
        0,
        bindings,
        parameters,
        &resources,
        &error
    ));
    REQUIRE(!memcmp(before_bindings, bindings, sizeof bindings));
    REQUIRE(!memcmp(before_parameters, parameters, sizeof parameters));
    REQUIRE(!memcmp(&before_resources, &resources, sizeof resources));
    behavior_asset_free(collision);
    behavior_asset_free(paint);
    behavior_asset_free(other);
    behavior_asset_free(a);
    authoring_shader_catalog_clear();
    return true;
}

static void teardown()
{
    authoring_shader_catalog_clear();
}

void test_model_materials()
{
    test_graph_packs_install();
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(material_coordinate_inputs);
    RUN_TEST_CASE(csg_ownership);
    RUN_TEST_CASE(transformed_part_coordinates);
    RUN_TEST_CASE(transformed_part_frame);
    RUN_TEST_CASE(extreme_part_frames);
    RUN_TEST_CASE(isolated_part_shaders);
    RUN_TEST_CASE(material_assignment_lifecycle);
    RUN_TEST_CASE(reused_nested_materials);
    RUN_TEST_CASE(catalog_material_edits);
    RUN_TEST_CASE(flattened_parameter_limit);
    RUN_TEST_CASE(combined_ramp_limit);
    RUN_TEST_CASE(scoped_namespace_collision);
    pu_clear_setup();
    test_graph_packs_restore();
}
