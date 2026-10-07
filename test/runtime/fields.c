#include "graph/compiler/field_program.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "runtime/material_field.h"
#include "runtime/material_stream.h"
#include "runtime/material_terrain.h"
#include "runtime/task_pool.h"
#include "runtime/tasks.h"
#include "support/cos_sin.h"
#include "support/exponential.h"
#include "support/text_file.h"
#include "test/support/code_fixture.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
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
typedef struct
{
    int enqueues, finishes;
} inline_executor;
static void *inline_enqueue(game_task_range *run, int count, int minimum, void *context, void *user)
{
    inline_executor *executor = user;
    assert(count > 0 && minimum > 0);
    executor->enqueues++;
    run(0, count, 0, context);
    return NULL;
}
static void inline_finish(void *task, void *user)
{
    inline_executor *executor = user;
    assert(!task);
    executor->finishes++;
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
        field_grid grid = { 9, 7, 4, .25f, -.5f };
        float baked[9 * 7];
        char error[256];
        field_context ctx = { .seed = 16 };
        assert(field_program_bake(p, ctx, grid, baked, NULL, error, sizeof error));
        for (int y = 0; y < grid.height; y++) {
            for (int x = 0; x < grid.width; x++) {
                float scalar_value;
                assert(field_program_sample(
                    p,
                    ctx,
                    grid.center_x + (x + .5f - grid.width * .5f) * grid.cell_size,
                    grid.center_y + (y + .5f - grid.height * .5f) * grid.cell_size,
                    &scalar_value,
                    error,
                    sizeof error
                ));
                assert(scalar_value == baked[y * grid.width + x]);
            }
        }
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
        !field_program_bake(p, (field_context){ 0 }, (field_grid){ 0, 1, 1 }, &out, NULL, error, sizeof error)
    );
    material_fields *catalog = material_fields_new();
    assert(material_field_compile(catalog, a, error, sizeof error));
    add(a, "Print String");
    assert(!field_program_compile(a, error, sizeof error));
    assert(!material_field_compile(catalog, a, error, sizeof error));
    assert(
        material_field_bake(catalog, "parameters", "Radius 7\n", 0, (field_grid){ 1, 1, 1 }, &out, NULL, error, sizeof error) &&
        out == 7
    );
    {
        field_grid grid = { 7, 41, .25f, -1.5f, 2.5f };
        float repeated[7 * 41];
        v = (field_parameter){ "Radius", { .type = PIN_F32, .f = 7 } };
        field_program_baker *baker;
        baker = field_program_baker_new(p, (field_context){ 19, &v, 1 }, 1, error, sizeof error);
        assert(baker);
        v.value.f = 15;
        for (int first = 0; first < grid.height; first += 16) {
            int count = grid.height - first < 16 ? grid.height - first : 16;
            assert(
                field_program_baker_bake_rows(baker, grid, first, count, repeated, NULL, error, sizeof error)
            );
        }
        for (int i = 0; i < 7 * 41; i++) {
            assert(repeated[i] == 7);
        }
        field_program_baker_free(baker);
    }
    field_program_free(p);
    assert(material_field_revision(catalog, "parameters") == 1);
    assert(!material_field_bake(
        catalog,
        "parameters",
        "Unknown 7\n",
        0,
        (field_grid){ 1, 1, 1 },
        &out,
        NULL,
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
    assert(field_program_bake(p, ctx, grid, values, NULL, error, sizeof error));
    assert(field_program_bake(p, ctx, grid, again, NULL, error, sizeof error));
    assert(hash(values, 256) == hash(again, 256));
    float partial[16 * 16];
    for (int i = 0; i < 16 * 16; i++) {
        partial[i] = -1;
    }
    assert(field_program_bake_rows(p, ctx, grid, 4, 5, partial, NULL, error, sizeof error));
    assert(!memcmp(partial + 4 * 16, values + 4 * 16, 5 * 16 * sizeof(float)));
    for (int y = 0; y < 16; y++) {
        if (y >= 4 && y < 9) {
            continue;
        }
        for (int x = 0; x < 16; x++) {
            assert(partial[y * 16 + x] == -1);
        }
    }
    field_grid wider = { 16, 32, .125f };
    float serial[16 * 32], submitted[16 * 32];
    inline_executor executor = { 0 };
    game_task_system tasks = { 2, &executor, inline_enqueue, inline_finish };
    assert(
        field_program_bake_rows(p, ctx, wider, 0, wider.height, serial, NULL, error, sizeof error)
    );
    assert(
        field_program_bake_rows(p, ctx, wider, 0, wider.height, submitted, &tasks, error, sizeof error)
    );
    assert(executor.enqueues == 1 && executor.finishes == 0);
    assert(!memcmp(serial, submitted, sizeof serial));
    {
        field_grid tiled = { 16, 41, .125f, -2, 3 };
        float expected[16 * 41], repeated[16 * 41];
        assert(field_program_bake(p, ctx, tiled, expected, NULL, error, sizeof error));
        game_task_pool *pool = game_task_pool_acquire_workers(4);
        const game_task_system *pool_tasks = game_task_pool_tasks(pool);
        field_program_baker *baker;
        baker = field_program_baker_new(p, ctx, pool_tasks->workers, error, sizeof error);
        assert(baker);
        game_task_pool_begin(pool, true);
        assert(
            field_program_baker_bake_rows(baker, tiled, 0, tiled.height, repeated, pool_tasks, error, sizeof error)
        );
        game_task_pool_end(pool);
        assert(!memcmp(expected, repeated, sizeof expected));
        game_task_pool_begin(pool, true);
        for (int first = 0; first < tiled.height; first += 16) {
            int count = tiled.height - first < 16 ? tiled.height - first : 16;
            assert(
                field_program_baker_bake_rows(baker, tiled, first, count, repeated, pool_tasks, error, sizeof error)
            );
        }
        game_task_pool_end(pool);
        assert(!memcmp(expected, repeated, sizeof expected));
        field_program_baker_free(baker);
        game_task_pool_release(pool);
    }
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
    assert(field_program_bake(p, ctx, grid, again, NULL, error, sizeof error));
    assert(hash(values, 256) != hash(again, 256));
    field_program_free(p);
    behavior_asset_free(a);
    char *json = text_file_read("projects/arena2d/content/fields/MaterialIslands.kdl");
    assert(json);
    a = behavior_asset_load(json);
    free(json);
    json = text_file_read("projects/arena2d/content/libraries/FieldShapes.code");
    assert(json);
    behavior_asset *library = test_project_asset("projects/arena2d/content/libraries/FieldShapes.code", json);
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
static void compare_field_samples(field_program *program, field_context context, field_grid grid, float *baked)
{
    char error[256];
    for (int y = 0; y < grid.height; y++) {
        float py = grid.center_y + (y + .5f - grid.height * .5f) * grid.cell_size;
        for (int x = 0; x < grid.width; x++) {
            float px = grid.center_x + (x + .5f - grid.width * .5f) * grid.cell_size;
            float scalar;
            assert(field_program_sample(program, context, px, py, &scalar, error, sizeof error));
            assert(!memcmp(&baked[y * grid.width + x], &scalar, sizeof scalar));
        }
    }
}

static void row_simd_parity()
{
    char error[256];
    behavior_asset *asset = field("row-arithmetic");
    assert(behavior_asset_add_variable(
        asset,
        "Offset",
        PIN_F32,
        NULL,
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .f = .25f },
        NULL
    ));
    node *x = add(asset, "Field X"), *parameter = add(asset, "Field Parameter");
    assert(
        !graph_set_pin_default(asset->graph, parameter, 0, 0, &(pin_value){ .type = PIN_STR, .str = "Offset" })
    );
    node *sum = add(asset, "Field Add"), *subtract = add(asset, "Field Subtract");
    node *output = add(asset, "Field Output");
    wire(asset, x, sum, 0);
    wire(asset, x, sum, 1);
    wire(asset, sum, subtract, 0);
    wire(asset, parameter, subtract, 1);
    wire(asset, subtract, output, 0);
    field_program *program = compile(asset);
    field_parameter offset = { "Offset", { .type = PIN_F32, .f = .25f } };
    field_parameter original_offset = offset;
    field_context context = { 37, &offset, 1 };
    field_context original_context = { 37, &original_offset, 1 };
    field_program_baker *baker = field_program_baker_new(program, context, 1, error, sizeof error);
    assert(baker);
    const int widths[] = { 1, 2, 3, 4, 5, 6, 7, 3, 9 };
    for (int i = 0; i < (int)(sizeof widths / sizeof widths[0]); i++) {
        field_grid grid = { widths[i], 5, .5f, -.125f, .25f };
        float baked[9 * 5];
        assert(field_program_baker_bake_rows(baker, grid, 0, 2, baked, NULL, error, sizeof error));
        assert(field_program_baker_bake_rows(baker, grid, 2, 3, baked, NULL, error, sizeof error));
        compare_field_samples(program, original_context, grid, baked);
    }
    offset.value.f = -2;
    field_grid resized = { 9, 5, .5f, -.125f, .25f };
    float snapshotted[9 * 5], rebound[9 * 5];
    assert(
        field_program_baker_bake_rows(baker, resized, 0, resized.height, snapshotted, NULL, error, sizeof error)
    );
    compare_field_samples(program, original_context, resized, snapshotted);
    field_program_baker_free(baker);
    baker = field_program_baker_new(program, context, 1, error, sizeof error);
    assert(baker);
    assert(
        field_program_baker_bake_rows(baker, resized, 0, resized.height, rebound, NULL, error, sizeof error)
    );
    compare_field_samples(program, context, resized, rebound);
    assert(memcmp(snapshotted, rebound, sizeof snapshotted) != 0);
    field_program_baker_free(baker);
    field_program_free(program);
    behavior_asset_free(asset);

    asset = field("row-signed-zero");
    x = add(asset, "Field X");
    node *multiply = add(asset, "Field Multiply");
    output = add(asset, "Field Output");
    wire(asset, x, multiply, 0);
    value(asset, multiply, 1, -0.0f);
    wire(asset, multiply, output, 0);
    program = compile(asset);
    for (int width = 1; width <= 9; width++) {
        field_grid grid = { width, 3, 1, 0, 0 };
        float baked[9 * 3];
        assert(
            field_program_bake(program, (field_context){ 0 }, grid, baked, NULL, error, sizeof error)
        );
        compare_field_samples(program, (field_context){ 0 }, grid, baked);
        bool negative_zero = false;
        for (int i = 0; i < width * grid.height; i++) {
            uint32_t bits;
            memcpy(&bits, &baked[i], sizeof bits);
            negative_zero |= bits == UINT32_C(0x80000000);
        }
        assert(negative_zero);
    }
    field_program_free(program);
    behavior_asset_free(asset);

    asset = field("row-overflow");
    x = add(asset, "Field X");
    multiply = add(asset, "Field Multiply");
    output = add(asset, "Field Output");
    wire(asset, x, multiply, 0);
    value(asset, multiply, 1, FLT_MAX);
    wire(asset, multiply, output, 0);
    program = compile(asset);
    field_grid grid = { 5, 1, 1, 2, 0 };
    float baked[5];
    assert(
        !field_program_bake(program, (field_context){ 0 }, grid, baked, NULL, error, sizeof error)
    );
    float scalar;
    assert(!field_program_sample(program, (field_context){ 0 }, 2, 0, &scalar, error, sizeof error));
    field_program_free(program);
    behavior_asset_free(asset);
}

static void repeated_inputs()
{
    behavior_asset *a = field("repeated inputs");
    node *x = add(a, "Field X"), *sum = add(a, "Field Add"),
         *output = add(a, "Field Output");
    wire(a, x, sum, 0);
    wire(a, x, sum, 1);
    wire(a, sum, output, 0);
    field_program *p = compile(a);
    field_grid grid = { 17, 9, .25f, .125f, -.25f };
    float baked[17 * 9];
    char error[256];
    field_context ctx = { .seed = 16 };
    assert(field_program_bake(p, ctx, grid, baked, NULL, error, sizeof error));
    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            float sampled;
            assert(field_program_sample(
                p,
                ctx,
                grid.center_x + (x + .5f - grid.width * .5f) * grid.cell_size,
                grid.center_y + (y + .5f - grid.height * .5f) * grid.cell_size,
                &sampled,
                error,
                sizeof error
            ));
            assert(sampled == baked[y * grid.width + x]);
        }
    }
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
    char *json = graph_save(fn);
    graph *copy = graph_load(json);
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

/* A function returns several values, and a stage both results share
   compiles once. */
static void multiple_results()
{
    behavior_asset *a = field("pair");
    graph *fn = behavior_asset_add_function(a, "Pair", true);
    node *entry = graph_node_by_id(fn, fn->entry_id),
         *ret = graph_node_by_id(fn, fn->return_id);
    assert(graph_add_pin(fn, entry, 1, &(pin_spec){ .name = "V", .type = PIN_F32 }) == 0);
    assert(graph_add_pin(fn, ret, 0, &(pin_spec){ .name = "Sum", .type = PIN_F32 }) == 0);
    assert(graph_add_pin(fn, ret, 0, &(pin_spec){ .name = "Product", .type = PIN_F32 }) == 1);
    node *plus = graph_add_node(fn, registry_node_by_name("Field Add")),
         *times = graph_add_node(fn, registry_node_by_name("Field Multiply"));
    assert(!graph_connect_data(fn, entry, 0, plus, 0));
    assert(!graph_connect_data(fn, entry, 0, plus, 1));
    assert(!graph_connect_data(fn, plus, 0, times, 0));
    assert(!graph_connect_data(fn, plus, 0, times, 1));
    assert(!graph_connect_data(fn, plus, 0, ret, 0));
    assert(!graph_connect_data(fn, times, 0, ret, 1));
    node *call = behavior_asset_add_function_call_node(a, fn),
         *minus = add(a, "Field Subtract"), *out = add(a, "Field Output");
    value(a, call, 0, 3);
    assert(!graph_connect_data(a->graph, call, 1, minus, 0));
    assert(!graph_connect_data(a->graph, call, 0, minus, 1));
    wire(a, minus, out, 0);
    field_program *p = compile(a);
    assert(sample(p, 0, 0) == 36 - 6);
    assert(field_program_instruction_count(p) <= 6);
    field_program_free(p);
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
        "projects/arena2d/content/libraries/FieldShapes.code",
        "projects/arena2d/content/fields/MaterialIslands.kdl"
    };
    for (int i = 0; i < 2; i++) {
        char *json = text_file_read(paths[i]);
        assert(json);
        behavior_asset *asset = test_project_asset(paths[i], json);
        free(json);
        assert(asset);
        assert(!game_live_add_definition(live, game_live_definition_new(asset->name, asset)));
    }
    material_fields *fields = game_materials_fields(live->materials);
    char error[256];
    float before, after;
    field_grid grid = { 1, 1, 1, -90, 0 };
    assert(
        material_field_bake(fields, "MaterialIslands", NULL, 1, grid, &before, NULL, error, sizeof error)
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
        material_field_bake(fields, "MaterialIslands", NULL, 1, grid, &after, NULL, error, sizeof error)
    );
    assert(after < before - 20);
    assert(graph_add_node(function, registry_node_by_name("Print String")));
    assert(!game_live_mark_dirty(library));
    assert(game_live_apply(live, library, error, sizeof error) < 0);
    assert(material_field_revision(fields, "MaterialIslands") == 2);
    float retained;
    assert(
        material_field_bake(fields, "MaterialIslands", NULL, 1, grid, &retained, NULL, error, sizeof error)
    );
    assert(retained == after);
    game_live_world_free(live);
    ecs_world_free(world);
}

/* Prefetch budgets count 16-row slices; one 128-row tile takes eight. */
enum
{
    TILE_SLICES = 8
};

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
    material_stream_view view = { -1500, 0, 80 };
    material_fields *hot_fields = material_fields_new();
    assert(material_field_compile(hot_fields, a, error, sizeof error));
    material_stream *stale_stream = material_stream_new(desc);
    assert(material_stream_update(stale_stream, hot_fields, &view, 1, 1, error, sizeof error));
    assert(material_stream_inspect(stale_stream).resident == 0);
    behavior_asset *replacement = field("stream-fixture");
    node *rx = add(replacement, "Field X"), *ry = add(replacement, "Field Y");
    node *rcircle = add(replacement, "Field Circle"),
         *rout = add(replacement, "Field Output");
    wire(replacement, rx, rcircle, 0);
    wire(replacement, ry, rcircle, 1);
    value(replacement, rcircle, 2, 4000);
    wire(replacement, rcircle, rout, 0);
    assert(material_field_compile(hot_fields, replacement, error, sizeof error));
    assert(!material_stream_update(stale_stream, hot_fields, &view, 1, 1, error, sizeof error));
    assert(
        strstr(error, "field changed") &&
        material_stream_inspect(stale_stream).resident == 0
    );
    material_stream_free(stale_stream);
    material_fields_free(hot_fields);
    behavior_asset_free(replacement);

    material_stream *stream = material_stream_new(desc);
    /* Prefetch bakes a tile a slice per update, publishing it once complete. */
    int count;
    const material_stream_tile *tiles;
    for (int i = 0; i < TILE_SLICES; i++) {
        material_stream_tiles(stream, &count);
        assert(count == 0);
        assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    }
    tiles = material_stream_tiles(stream, &count);
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
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    assert(material_stream_inspect(stream).unloaded == 1);
    view.x = -1500;
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
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
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    view.x = -1500;
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    assert(tiles[0].mask->count == 0);
    assert(material_stream_inspect(stream).stored_bytes == stored_bytes);
    /* Traversal remains local even though the world exceeds one bake's limit. */
    for (int i = 0; i < 24; i++) {
        view.x = -1800 + 150 * i;
        assert(
            material_stream_update(stream, fields, &view, 1, 2 * TILE_SLICES, error, sizeof error)
        );
        assert(material_stream_inspect(stream).resident <= 20);
    }
    view = (material_stream_view){ 2049, 256, 0 };
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
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
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    assert(tiles[0].mask->count == length);
    assert(material_stream_inspect(stream).stored == 0);
    material_stream_free(stream);
    /* Signed tiles have no rectangular edge and preserve all four quadrants.
     * (-1,-1) encodes UINT64_MAX, which must not be used as a missing sentinel. */
    desc.grid = (field_grid){ 0, 0, 1 };
    stream = material_stream_new(desc);
    view = (material_stream_view){ -64, -64, 0 };
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
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
        assert(
            material_stream_update(stream, fields, &distant[i], 1, TILE_SLICES, error, sizeof error)
        );
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
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    assert(count == 1 && tiles[0].key == key && tiles[0].mask->count == remaining);
    material_stream_free(stream);
    /* Tidying removes an enclosed island and fills an enclosed pocket, but
     * keeps components that reach the grid edge. */
    {
        field_grid small = { 12, 12, 1 };
        float cells[144];
        for (int i = 0; i < 144; i++) {
            cells[i] = 1; /* open */
        }
        cells[5 * 12 + 5] = cells[5 * 12 + 6] = -1; /* island */
        cells[0 * 12 + 2] = cells[1 * 12 + 2] = -1; /* touches the top edge */
        for (int y = 8; y < 12; y++) {
            for (int x = 0; x < 4; x++) {
                cells[y * 12 + x] = -1; /* rock with a pocket */
            }
        }
        cells[9 * 12 + 1] = 1;
        material_terrain_tidy(cells, small, 8);
        assert(cells[5 * 12 + 5] > 0 && cells[5 * 12 + 6] > 0);
        assert(cells[0 * 12 + 2] < 0 && cells[1 * 12 + 2] < 0);
        assert(cells[9 * 12 + 1] < 0);
        material_terrain_tidy(cells, small, 0);
    }
    /* Every tile inside a required extent loads at once; the budget only
     * limits prefetching beyond it. */
    stream = material_stream_new(desc);
    view = (material_stream_view){ 0, 0, 1000, 200 };
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    assert(material_stream_inspect(stream).resident == 16 + 1);
    assert(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    assert(material_stream_inspect(stream).resident == 16 + 2);
    material_stream_free(stream);
    material_fields_free(fields);
    behavior_asset_free(a);
    free(cells);
    free(distance);
    free(damage);
}

static void streaming_failed_slice_retries()
{
    behavior_asset *asset = field("stream-failing-row");
    node *y = add(asset, "Field Y"), *subtract = add(asset, "Field Subtract");
    node *divide = add(asset, "Field Divide"), *output = add(asset, "Field Output");
    wire(asset, y, subtract, 0);
    value(asset, subtract, 1, 20.5f);
    value(asset, divide, 0, 1);
    wire(asset, subtract, divide, 1);
    wire(asset, divide, output, 0);
    material_fields *fields = material_fields_new();
    char error[256] = { 0 };
    assert(material_field_compile(fields, asset, error, sizeof error));
    material_stream *stream = material_stream_new((material_stream_desc){
        .grid = { 0, 0, 1 },
        .field = "stream-failing-row",
    });
    material_stream_view view = { 64, 64, 0 };
    assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    assert(!material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    assert(!material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    assert(strstr(error, "invalid scalar"));
    assert(material_stream_inspect(stream).resident == 0);
    material_stream_free(stream);
    material_fields_free(fields);
    behavior_asset_free(asset);
}

/* Fields use Box2D's cosine and sine, identical on every platform: within
 * 0.002 of libm across many turns, and exact at zero. */
static void cos_sin_accuracy()
{
    static const struct
    {
        float angle;
        uint32_t cosine, sine;
    } golden[] = { { 0.0f, 0x3f800000, 0x00000000 },
                   { 0.1f, 0x3f7eb030, 0x3dcf0f38 },
                   { 0.5f, 0x3f60943c, 0x3ef5c425 },
                   { 1.57079632679f, 0x00000000, 0x3f800000 },
                   { -2.5f, 0xbf4d0e59, 0xbf19420f },
                   { 9.0f, 0xbf692558, 0x3ed3762b } };
    for (int i = 0; i < (int)(sizeof golden / sizeof golden[0]); i++) {
        cos_sin pair = cos_sin_compute(golden[i].angle);
        uint32_t cosine, sine;
        memcpy(&cosine, &pair.c, sizeof cosine);
        memcpy(&sine, &pair.s, sizeof sine);
        assert(cosine == golden[i].cosine && sine == golden[i].sine);
    }
    float worst = 0;
    for (float angle = -1000; angle < 1000; angle += 0.37f) {
        cos_sin r = cos_sin_compute(angle);
        worst = fmaxf(worst, fmaxf(fabsf(r.c - cosf(angle)), fabsf(r.s - sinf(angle))));
    }
    cos_sin zero = cos_sin_compute(0);
    assert(worst < 0.002f && zero.c == 1 && zero.s == 0);
}

static void atan2_accuracy()
{
    static const struct
    {
        float y, x;
        uint32_t angle;
    } golden[] = {
        { 0, 0, 0x00000000 },           { 0, 1, 0x00000000 },
        { 1, 0, 0x3fc90fdb },           { 1, 1, 0x3f4911aa },
        { 1, -1, 0x4016cb70 },          { -1, -1, 0xc016cb70 },
        { -1, 1, 0xbf4911aa },          { -0.123f, -0.994f, 0xc0412eae },
        { -0.123f, 0.994f, 0xbdfc2590 }
    };
    for (int i = 0; i < (int)(sizeof golden / sizeof golden[0]); i++) {
        float angle = cos_sin_atan2(golden[i].y, golden[i].x);
        uint32_t bits;
        memcpy(&bits, &angle, sizeof bits);
        assert(bits == golden[i].angle);
    }
    float worst = 0;
    for (float y = -10; y <= 10; y += 0.137f) {
        for (float x = -10; x <= 10; x += 0.119f) {
            if (x == 0 && y == 0) {
                continue;
            }
            worst = fmaxf(worst, fabsf(cos_sin_atan2(y, x) - atan2f(y, x)));
        }
    }
    assert(worst < 0.0001f);
}

static void exponential_accuracy()
{
    static const struct
    {
        float input;
        uint32_t result;
    } golden[] = { { 0, 0x3f800000 },
                   { -1, 0x3ebc5ab2 },
                   { 1, 0x402df854 },
                   { -0.5f, 0x3f1b4598 },
                   { 0.5f, 0x3fd3094c },
                   { -2, 0x3e0a9555 },
                   { 2, 0x40ec7326 },
                   { -0.08219975233078003f, 0x3f6bcc6c },
                   { -10, 0x383e6bce },
                   { 10, 0x46ac14ee },
                   { -0x1.9fe36ap+6f, 0x00000000 },
                   { -0x1.9fe368p+6f, 0x00000001 },
                   { -0x1.9fe366p+6f, 0x00000001 },
                   { 0x1.62e42cp+6f, 0x7f7fff04 },
                   { 0x1.62e42ep+6f, 0x7f7fff84 },
                   { 0x1.62e430p+6f, 0x7f800000 } };
    for (int i = 0; i < (int)(sizeof golden / sizeof golden[0]); i++) {
        float result = deterministic_expf(golden[i].input);
        uint32_t bits;
        memcpy(&bits, &result, sizeof bits);
        assert(bits == golden[i].result);
    }
    assert(deterministic_expf(-INFINITY) == 0);
    assert(isinf(deterministic_expf(INFINITY)));
    assert(isnan(deterministic_expf(NAN)));
    assert(deterministic_expf(-104) == 0);
    assert(isinf(deterministic_expf(89)));
    for (float x = -80; x <= 88; x += 0.125f) {
        double expected = exp((double)x);
        double actual = deterministic_expf(x);
        assert(fabs(actual - expected) <= expected * 1e-6);
    }
}

void test_fields()
{
    cos_sin_accuracy();
    atan2_accuracy();
    exponential_accuracy();
    streaming_restore();
    streaming_failed_slice_retries();
    primitives();
    parameters_and_errors();
    noise_and_composition();
    row_simd_parity();
    repeated_inputs();
    functions();
    multiple_results();
    chunks();
    library_hot_apply();
    puts("PASS: pure field compilation, deterministic trigonometry, parameters, noise, composition, connectivity and chunks");
}
int bench_fields()
{
    char *json = text_file_read("projects/arena2d/content/fields/CaveField.kdl");
    assert(json);
    behavior_asset *a = behavior_asset_load(json);
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
            field_program_bake(p, (field_context){ .seed = 1729 }, grid, values, NULL, error, sizeof error)
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

int bench_field_slices()
{
    const char *parameters = "Radius 225.0\n"
                             "Roughness 100.0\n";
    char *json = text_file_read("projects/arena2d/content/fields/CaveField.kdl");
    assert(json);
    behavior_asset *asset = behavior_asset_load(json);
    free(json);
    material_fields *fields = material_fields_new();
    char error[256] = { 0 };
    assert(material_field_compile(fields, asset, error, sizeof error));
    material_stream *stream = material_stream_new((material_stream_desc){
        .grid = { 0, 0, 2 },
        .field = asset->name,
        .parameters = parameters,
        .seed = 1729,
    });
    enum
    {
        TILE_SPAN = 128 * 2,
        CENTER_COUNT = 256,
        SLICE_COUNT = TILE_SLICES - 1,
        SAMPLE_COUNT = 5,
    };
    field_grid expected_grid = { 128, 128, 2, TILE_SPAN * .5f, TILE_SPAN * .5f };
    float *expected = malloc(
        (size_t)expected_grid.width * expected_grid.height * sizeof(float)
    );
    assert(expected);
    assert(
        material_field_bake(fields, asset->name, parameters, 1729, expected_grid, expected, NULL, error, sizeof error)
    );
    uint64_t expected_hash = hash(expected, expected_grid.width * expected_grid.height);
    free(expected);
    for (int run = -1; run < SAMPLE_COUNT; run++) {
        struct timespec start, end;
        if (run >= 0) {
            clock_gettime(CLOCK_MONOTONIC, &start);
        }
        for (int center = 0; center < CENTER_COUNT; center++) {
            material_stream_view view = { center * TILE_SPAN * 4.0f, 0, 0, 0 };
            for (int slice = 0; slice < SLICE_COUNT; slice++) {
                assert(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
            }
        }
        assert(material_stream_inspect(stream).resident == 0);
        if (run >= 0) {
            clock_gettime(CLOCK_MONOTONIC, &end);
            double elapsed_ms = 1000. * (end.tv_sec - start.tv_sec) +
                                (end.tv_nsec - start.tv_nsec) / 1e6;
            printf(
                "field slices %d: %.3f ms, %d centers, %d slices per center, "
                "hash %016llx\n",
                run,
                elapsed_ms,
                CENTER_COUNT,
                SLICE_COUNT,
                (unsigned long long)expected_hash
            );
        }
    }
    material_stream_free(stream);
    material_fields_free(fields);
    behavior_asset_free(asset);
    return 0;
}
