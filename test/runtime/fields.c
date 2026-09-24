#include "graph/compiler/field_program.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "runtime/material_field.h"
#include "runtime/material_stream.h"
#include "runtime/material_terrain.h"
#include "support/text_file.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static node *add(behavior_asset *asset, const char *name)
{
    node *n = graph_add_node(asset->graph, registry_node_by_name(name));
    assert(n);
    return n;
}
static void value(behavior_asset *asset, node *n, int pin, float number)
{
    assert(
        !graph_set_pin_default(asset->graph, n, 0, pin, &(pin_value){ .type = PIN_F32, .f = number })
    );
}
static void wire(behavior_asset *asset, node *from, node *to, int pin)
{
    assert(!graph_connect_data(asset->graph, from, 0, to, pin));
}
static field_program *compile(behavior_asset *asset)
{
    char error[256];
    field_program *p = field_program_compile(asset, error, sizeof error);
    if (!p) {
        fprintf(stderr, "%s\n", error);
    }
    assert(p);
    return p;
}
static float sample(field_program *p, float x, float y)
{
    float out;
    char error[256];
    assert(field_program_sample(p, (field_context){ .seed = 123 }, x, y, &out, error, sizeof error));
    return out;
}
static behavior_asset *field(const char *name)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_FIELD;
    return a;
}
static uint64_t hash(const float *values, int count)
{
    uint64_t result = UINT64_C(14695981039346656037);
    const unsigned char *bytes = (const unsigned char *)values;
    for (size_t i = 0; i < (size_t)count * sizeof(float); i++) {
        result = (result ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return result;
}
static void primitives()
{
    const char *names[] = { "Field Circle", "Field Box", "Field Capsule" };
    for (int i = 0; i < 3; i++) {
        behavior_asset *a = field("primitive");
        node *x = add(a, "Field X"), *y = add(a, "Field Y"),
             *shape = add(a, names[i]), *out = add(a, "Field Output");
        wire(a, x, shape, 0);
        wire(a, y, shape, 1);
        wire(a, shape, out, 0);
        if (i == 0) {
            value(a, shape, 2, 10);
        }
        if (i == 1) {
            value(a, shape, 2, 10);
            value(a, shape, 3, 20);
        }
        if (i == 2) {
            value(a, shape, 2, -5);
            value(a, shape, 4, 5);
            value(a, shape, 6, 10);
        }
        field_program *p = compile(a);
        assert(sample(p, 0, 0) == -10);
        assert(sample(p, 40, 0) > 0);
        assert(sample(p, 0, 40) > 0);
        field_program_free(p);
        behavior_asset_free(a);
    }
}
static void parameters_and_errors()
{
    char error[256];
    float out;
    behavior_asset *a = field("parameters");
    assert(behavior_asset_add_variable(
        a,
        "Radius",
        PIN_F32,
        NULL,
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .f = 4 },
        NULL
    ));
    node *param = add(a, "Field Parameter"), *output = add(a, "Field Output");
    assert(
        !graph_set_pin_default(a->graph, param, 0, 0, &(pin_value){ .type = PIN_STR, .str = "Radius" })
    );
    wire(a, param, output, 0);
    field_program *p = compile(a);
    assert(sample(p, 0, 0) == 4);
    field_parameter v = { "Radius", { .type = PIN_F32, .f = 8 } };
    field_context ctx = { 1, &v, 1 };
    assert(field_program_sample(p, ctx, 0, 0, &out, error, sizeof error) && out == 8);
    v.value.f = NAN;
    assert(!field_program_sample(p, ctx, 0, 0, &out, error, sizeof error));
    v.value = (pin_value){ .type = PIN_I32, .i = 8 };
    assert(!field_program_sample(p, ctx, 0, 0, &out, error, sizeof error));
    v.name = "missing";
    assert(!field_program_sample(p, ctx, 0, 0, &out, error, sizeof error));
    assert(
        !field_program_bake(p, (field_context){ 0 }, (field_grid){ 0, 1, 1 }, &out, error, sizeof error)
    );
    field_program_free(p);
    material_fields *catalog = material_fields_new();
    assert(material_field_compile(catalog, a, error, sizeof error));
    add(a, "Print String");
    assert(!field_program_compile(a, error, sizeof error));
    assert(!material_field_compile(catalog, a, error, sizeof error));
    assert(
        material_field_bake(
            catalog,
            "parameters",
            "{\"Radius\":7}",
            0,
            (field_grid){ 1, 1, 1 },
            &out,
            error,
            sizeof error
        ) &&
        out == 7
    );
    assert(material_field_revision(catalog, "parameters") == 1);
    assert(!material_field_bake(
        catalog,
        "parameters",
        "{\"Unknown\":7}",
        0,
        (field_grid){ 1, 1, 1 },
        &out,
        error,
        sizeof error
    ));
    material_fields_free(catalog);
    behavior_asset_free(a);
    a = field("recursive");
    node *sum = add(a, "Field Add");
    output = add(a, "Field Output");
    wire(a, sum, sum, 0);
    wire(a, sum, output, 0);
    assert(!field_program_compile(a, error, sizeof error));
    assert(strstr(error, "Recursive"));
    behavior_asset_free(a);
    a = field("invalid evaluation");
    node *division = add(a, "Field Divide");
    output = add(a, "Field Output");
    value(a, division, 0, 1);
    wire(a, division, output, 0);
    p = compile(a);
    assert(!field_program_sample(p, (field_context){ 0 }, 0, 0, &out, error, sizeof error));
    field_program_free(p);
    behavior_asset_free(a);
}
static void noise_and_composition()
{
    char error[256];
    float values[256], again[256];
    behavior_asset *a = field("noise");
    node *x = add(a, "Field X"), *y = add(a, "Field Y"),
         *noise = add(a, "Field Fractal"), *output = add(a, "Field Output");
    wire(a, x, noise, 0);
    wire(a, y, noise, 1);
    value(a, noise, 3, 4);
    value(a, noise, 4, 2);
    value(a, noise, 5, .5f);
    wire(a, noise, output, 0);
    field_program *p = compile(a);
    field_context ctx = { .seed = 8 };
    field_grid grid = { 16, 16, .125f };
    assert(field_program_bake(p, ctx, grid, values, error, sizeof error));
    assert(field_program_bake(p, ctx, grid, again, error, sizeof error));
    assert(hash(values, 256) == hash(again, 256));
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            float scalar_value;
            assert(
                field_program_sample(p, ctx, (x + .5f - 8) * .125f, (y + .5f - 8) * .125f, &scalar_value, error, sizeof error)
            );
            assert(scalar_value == values[y * 16 + x]);
        }
    }
    ctx.seed++;
    assert(field_program_bake(p, ctx, grid, again, error, sizeof error));
    assert(hash(values, 256) != hash(again, 256));
    field_program_free(p);
    behavior_asset_free(a);
    char *json = text_file_read("projects/arena2d/content/fields/MaterialIslands.yaml");
    assert(json);
    a = behavior_asset_load_json(json);
    free(json);
    json = text_file_read("projects/arena2d/content/libraries/FieldShapes.yaml");
    assert(json);
    behavior_asset *library = behavior_asset_load_json(json);
    free(json);
    const behavior_asset *catalog[] = { library };
    behavior_asset *linked = behavior_asset_link_libraries(a, catalog, 1, error, sizeof error);
    if (!linked) {
        fprintf(stderr, "field library: %s\n", error);
    }
    assert(linked);
    behavior_asset_free(a);
    behavior_asset_free(library);
    a = linked;
    p = compile(a);
    assert(
        sample(p, -90, 0) < -60 && sample(p, 60, 0) < -50 &&
        sample(p, 0, 0) < -15 && sample(p, 0, 150) > 50
    );
    field_program_free(p);
    behavior_asset_free(a);
}
static void functions()
{
    char error[256];
    behavior_asset *a = field("composed");
    graph *fn = behavior_asset_add_function(a, "Double", true);
    assert(fn);
    node *entry = graph_node_by_id(fn, fn->entry_id),
         *ret = graph_node_by_id(fn, fn->return_id);
    assert(graph_add_pin(fn, entry, 1, &(pin_spec){ .name = "X", .type = PIN_F32 }) == 0);
    assert(graph_add_pin(fn, ret, 0, &(pin_spec){ .name = "Value", .type = PIN_F32 }) == 0);
    node *plus = graph_add_node(fn, registry_node_by_name("Field Add"));
    assert(!graph_connect_data(fn, entry, 0, plus, 0));
    assert(!graph_connect_data(fn, entry, 0, plus, 1));
    assert(!graph_connect_data(fn, plus, 0, ret, 0));
    node *call = behavior_asset_add_function_call_node(a, fn),
         *out = add(a, "Field Output");
    assert(call);
    value(a, call, 0, 3);
    wire(a, call, out, 0);
    field_program *p = compile(a);
    assert(sample(p, 0, 0) == 6);
    field_program_free(p);
    node *recursive = behavior_asset_add_function_call_node_to_graph(a, fn, fn);
    assert(recursive);
    assert(!field_program_compile(a, error, sizeof error));
    assert(strstr(error, "Recursive"));
    assert(!graph_remove_node(fn, recursive));
    behavior_asset *library = behavior_asset_new("ScalarFields");
    library->domain = BEHAVIOR_DOMAIN_LIBRARY;
    /* Move a copied pure function into the existing library contract. */
    char *json = graph_save_json(fn);
    graph *copy = graph_load_json(json);
    free(json);
    assert(copy);
    copy->kind = GRAPH_FUNCTION;
    copy->graph_id = fn->graph_id;
    copy->entry_id = fn->entry_id;
    copy->return_id = fn->return_id;
    copy->name = graph_own_str(copy, "Double", 6);
    copy->owner = library;
    copy->shared_id_counter = &library->graph->id_counter;
    apush(library->graphs, copy);
    library->graph->id_counter = a->graph->id_counter;
    call->reference_asset = graph_own_str(a->graph, "ScalarFields", strlen("ScalarFields"));
    const behavior_asset *catalog[] = { library };
    behavior_asset *linked = behavior_asset_link_libraries(a, catalog, 1, error, sizeof error);
    if (!linked) {
        fprintf(stderr, "field library: %s\n", error);
    }
    assert(linked);
    p = compile(linked);
    assert(sample(p, 0, 0) == 6);
    field_program_free(p);
    behavior_asset_free(linked);
    behavior_asset_free(library);
    behavior_asset_free(a);
}

static void chunks()
{
    field_grid grid = { 130, 129, 2 };
    float *values = malloc((size_t)grid.width * grid.height * sizeof(float));
    assert(values);
    for (int i = 0; i < grid.width * grid.height; i++) {
        values[i] = -1;
    }
    material_terrain_chunk *chunks = material_terrain_chunks(values, grid);
    assert(acount(chunks) == 4);
    int count = 0;
    for (int i = 0; i < acount(chunks); i++) {
        count += chunks[i].mask.count;
    }
    assert(count == 130 * 129 && chunks[3].mask.width == 2 && chunks[3].mask.height == 1);
    material_terrain_chunks_free(chunks);
    free(values);
    float disconnected[] = { -1, -1, -1, -1, -1, -1, 1, -1, 1, -1, -1, -1 };
    material_terrain_connect(disconnected, (field_grid){ 6, 2, 1 }, MATERIAL_CONNECTIVITY_FIRST_AIR, 0);
    assert(disconnected[6] > 0 && disconnected[8] < 0);
}
static void library_hot_apply()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(!ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL));
    }
    game_live_world *live = game_live_world_new(world);
    const char *paths[] = {
        "projects/arena2d/content/libraries/FieldShapes.yaml",
        "projects/arena2d/content/fields/MaterialIslands.yaml"
    };
    for (int i = 0; i < 2; i++) {
        char *json = text_file_read(paths[i]);
        assert(json);
        behavior_asset *asset = behavior_asset_load_json(json);
        free(json);
        assert(asset);
        assert(!game_live_add_definition(live, game_live_definition_new(asset->name, asset)));
    }
    material_fields *fields = game_materials_fields(live->materials);
    char error[256];
    float before, after;
    field_grid grid = { 1, 1, 1, -90, 0 };
    assert(
        material_field_bake(fields, "MaterialIslands", NULL, 1, grid, &before, error, sizeof error)
    );
    game_live_definition *library = game_live_find_definition(live, "FieldShapes");
    graph *function = library->working->graphs[1];
    node *circle = NULL;
    for (int i = 0; i < acount(function->nodes); i++) {
        if (!strcmp(function->nodes[i]->desc->name, "Field Circle")) {
            circle = function->nodes[i];
            break;
        }
    }
    assert(circle);
    assert(
        !graph_set_pin_default(function, circle, 0, 2, &(pin_value){ .type = PIN_F32, .f = 110 })
    );
    assert(!game_live_mark_dirty(library));
    assert(!game_live_apply(live, library, error, sizeof error));
    assert(material_field_revision(fields, "MaterialIslands") == 2);
    assert(
        material_field_bake(fields, "MaterialIslands", NULL, 1, grid, &after, error, sizeof error)
    );
    assert(after < before - 20);
    assert(graph_add_node(function, registry_node_by_name("Print String")));
    assert(!game_live_mark_dirty(library));
    assert(game_live_apply(live, library, error, sizeof error) < 0);
    assert(material_field_revision(fields, "MaterialIslands") == 2);
    float retained;
    assert(
        material_field_bake(fields, "MaterialIslands", NULL, 1, grid, &retained, error, sizeof error)
    );
    assert(retained == after);
    game_live_world_free(live);
    ecs_world_free(world);
}

static void streaming_restore()
{
    behavior_asset *a = field("stream-fixture");
    node *x = add(a, "Field X"), *y = add(a, "Field Y");
    node *circle = add(a, "Field Circle"), *out = add(a, "Field Output");
    wire(a, x, circle, 0);
    wire(a, y, circle, 1);
    value(a, circle, 2, 5000);
    wire(a, circle, out, 0);
    material_fields *fields = material_fields_new();
    char error[256] = { 0 };
    assert(material_field_compile(fields, a, error, sizeof error));
    material_stream_desc desc = { .grid = { 4099, 513, 1 },
                                  .field = "stream-fixture",
                                  .seed = 71 };
    material_stream *stream = material_stream_new(desc);
    material_stream_view view = { -1500, 0, 80 };
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    int count;
    const material_stream_tile *tiles = material_stream_tiles(stream, &count);
    assert(count == 1);
    uint64_t key = tiles[0].key;
    material_mask *mask = tiles[0].mask;
    int length = mask->width * mask->height;
    assert(!material_mask_apply_damage(mask, 32, 20, 4, .3f, 1));
    assert(material_mask_carve(mask, 0, 0, 12) > 0);
    int remaining = mask->count;
    unsigned char *cells = malloc((size_t)length);
    float *distance = malloc((size_t)length * sizeof(float));
    float *damage = malloc((size_t)length * sizeof(float));
    assert(cells && distance && damage);
    memcpy(cells, mask->cells, (size_t)length);
    memcpy(distance, mask->distance, (size_t)length * sizeof(float));
    memcpy(damage, mask->damage, (size_t)length * sizeof(float));
    view.x = 1500;
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    assert(material_stream_inspect(stream).unloaded == 1);
    view.x = -1500;
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    assert(count == 1 && tiles[0].key == key);
    mask = tiles[0].mask;
    assert(mask->count == remaining);
    assert(!memcmp(cells, mask->cells, (size_t)length));
    assert(!memcmp(distance, mask->distance, (size_t)length * sizeof(float)));
    assert(!memcmp(damage, mask->damage, (size_t)length * sizeof(float)));
    /* A released chunk remains gone even after its mask ownership transfers. */
    material_mask_free(mask);
    *mask = (material_mask){ 0 };
    size_t stored_bytes = material_stream_inspect(stream).stored_bytes;
    view.x = 1500;
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    view.x = -1500;
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    assert(tiles[0].mask->count == 0);
    assert(material_stream_inspect(stream).stored_bytes == stored_bytes);
    /* Traversal remains local even though the world exceeds one bake's limit. */
    for (int i = 0; i < 24; i++) {
        view.x = -1800 + 150 * i;
        assert(material_stream_update(stream, fields, &view, 1, 2, error, sizeof error));
        assert(material_stream_inspect(stream).resident <= 20);
    }
    view = (material_stream_view){ 2049, 256, 0 };
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    bool edge = false;
    for (int i = 0; i < count; i++) {
        edge |= tiles[i].grid.width == 3 && tiles[i].grid.height == 1;
    }
    assert(edge);
    material_stream_free(stream);
    /* A fresh run regenerates pristine terrain. */
    stream = material_stream_new(desc);
    view = (material_stream_view){ -1500, 0, 80 };
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    assert(tiles[0].mask->count == length);
    assert(material_stream_inspect(stream).stored == 0);
    material_stream_free(stream);
    /* Signed tiles have no rectangular edge and preserve all four quadrants.
     * (-1,-1) encodes UINT64_MAX, which must not be used as a missing sentinel. */
    desc.grid = (field_grid){ 0, 0, 1 };
    stream = material_stream_new(desc);
    view = (material_stream_view){ -64, -64, 0 };
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    assert(count == 1 && tiles[0].key == UINT64_MAX);
    key = tiles[0].key;
    assert(material_mask_carve(tiles[0].mask, 0, 0, 12) > 0);
    remaining = tiles[0].mask->count;
    const material_stream_view distant[] = { { 100064, 100064, 0 },
                                             { -100064, 100064, 0 },
                                             { 100064, -100064, 0 },
                                             { -100064, -100064, 0 } };
    uint64_t keys[4];
    for (int i = 0; i < 4; i++) {
        assert(material_stream_update(stream, fields, &distant[i], 1, 1, error, sizeof error));
        tiles = material_stream_tiles(stream, &count);
        assert(count == 1);
        keys[i] = tiles[0].key;
        assert(keys[i] != key);
        for (int j = 0; j < i; j++) {
            assert(keys[i] != keys[j]);
        }
        assert(fabsf(tiles[0].grid.center_x - distant[i].x) <= 64);
        assert(fabsf(tiles[0].grid.center_y - distant[i].y) <= 64);
    }
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    assert(count == 1 && tiles[0].key == key && tiles[0].mask->count == remaining);
    material_stream_free(stream);
    material_fields_free(fields);
    behavior_asset_free(a);
    free(cells);
    free(distance);
    free(damage);
}

void test_fields()
{
    streaming_restore();
    primitives();
    parameters_and_errors();
    noise_and_composition();
    functions();
    chunks();
    library_hot_apply();
    puts("PASS: pure field compilation, parameters, deterministic noise, composition, connectivity and chunks");
}
int bench_fields()
{
    char *json = text_file_read("projects/arena2d/content/fields/CaveField.yaml");
    assert(json);
    behavior_asset *a = behavior_asset_load_json(json);
    free(json);
    field_program *p = compile(a);
    field_grid grid = { 2048, 768, 2 };
    float *values = malloc((size_t)grid.width * grid.height * sizeof(float));
    assert(values);
    char error[256];
    for (int run = 0; run < 5; run++) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        assert(
            field_program_bake(p, (field_context){ .seed = 1729 }, grid, values, error, sizeof error)
        );
        clock_gettime(CLOCK_MONOTONIC, &end);
        double bake_ms = 1000. * (end.tv_sec - start.tv_sec) +
                         (end.tv_nsec - start.tv_nsec) / 1e6;
        material_terrain_connect(values, grid, MATERIAL_CONNECTIVITY_FIRST_AIR, 8);
        clock_gettime(CLOCK_MONOTONIC, &end);
        double total_ms = 1000. * (end.tv_sec - start.tv_sec) +
                          (end.tv_nsec - start.tv_nsec) / 1e6;
        extern float *reference_cave_generate(unsigned, int, int, float, float, float);
        clock_gettime(CLOCK_MONOTONIC, &start);
        float *reference = reference_cave_generate(1729, grid.width, grid.height, grid.cell_size, 225, 100);
        clock_gettime(CLOCK_MONOTONIC, &end);
        float maximum = 0;
        int differences = 0;
        for (int i = 0; i < grid.width * grid.height; i++) {
            maximum = fmaxf(maximum, fabsf(values[i] - reference[i]));
            differences += (values[i] < 0) != (reference[i] < 0);
        }
        free(reference);
        printf(
            "field bake %d: IR %.3f ms, with connectivity %.3f ms, reference "
            "%.3f ms, %d instructions, hash %016llx, max error %.9g, occupancy "
            "errors %d\n",
            run,
            bake_ms,
            total_ms,
            1000. * (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e6,
            field_program_instruction_count(p),
            (unsigned long long)hash(values, grid.width * grid.height),
            maximum,
            differences
        );
    }
    free(values);
    field_program_free(p);
    behavior_asset_free(a);
    return 0;
}
