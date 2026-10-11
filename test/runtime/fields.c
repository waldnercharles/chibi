#include "assets/field_parameters.h"
#include "authoring/behavior_loader.h"
#include "authoring/preview_source.h"
#include "authoring/source_graph_library.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/field_program.h"
#include "graph/compiler/material_field.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "runtime/component_catalog.h"
#include "runtime/material_field.h"
#include "runtime/material_stream.h"
#include "runtime/material_terrain.h"
#include "runtime/task_pool.h"
#include "runtime/tasks.h"
#include "support/cos_sin.h"
#include "support/exponential.h"
#include "support/text_file.h"
#include "test/support/code_fixture.h"
#include "test/support/game_graph_packs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Field graphs compiled to field programs: primitives, parameters, noise,
 * composition, row and scalar parity, library functions, source libraries
 * and their hot apply, terrain chunks, streaming tiles and the deterministic
 * trigonometry they use. */

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
TEST_CASE(field_primitives_bake_as_they_sample)
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
        REQUIRE(sample(p, 0, 0) == -10);
        REQUIRE(sample(p, 40, 0) > 0);
        REQUIRE(sample(p, 0, 40) > 0);
        field_grid grid = { 9, 7, 4, .25f, -.5f };
        float baked[9 * 7];
        char error[256];
        field_context ctx = { .seed = 16 };
        REQUIRE(field_program_bake(p, ctx, grid, baked, NULL, error, sizeof error));
        for (int y = 0; y < grid.height; y++) {
            for (int x = 0; x < grid.width; x++) {
                float scalar_value;
                REQUIRE(field_program_sample(
                    p,
                    ctx,
                    grid.center_x + (x + .5f - grid.width * .5f) * grid.cell_size,
                    grid.center_y + (y + .5f - grid.height * .5f) * grid.cell_size,
                    &scalar_value,
                    error,
                    sizeof error
                ));
                REQUIRE(scalar_value == baked[y * grid.width + x]);
            }
        }
        field_program_free(p);
        behavior_asset_free(a);
    }
    return true;
}
TEST_CASE(field_parameters_and_errors)
{
    char error[256];
    float out;
    behavior_asset *a = field("parameters");
    REQUIRE(behavior_asset_add_variable(
        a,
        "Radius",
        PIN_F32,
        NULL,
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .f = 4 },
        NULL
    ));
    node *param = add(a, "Field Parameter"), *output = add(a, "Field Output");
    REQUIRE(
        !graph_set_pin_default(a->graph, param, 0, 0, &(pin_value){ .type = PIN_STR, .str = "Radius" })
    );
    wire(a, param, output, 0);
    field_program *p = compile(a);
    REQUIRE(sample(p, 0, 0) == 4);
    field_parameter v = { "Radius", { .type = PIN_F32, .f = 8 } };
    field_context ctx = { 1, &v, 1 };
    REQUIRE(field_program_sample(p, ctx, 0, 0, &out, error, sizeof error) && out == 8);
    v.value.f = NAN;
    REQUIRE(!field_program_sample(p, ctx, 0, 0, &out, error, sizeof error));
    v.value = (pin_value){ .type = PIN_I32, .i = 8 };
    REQUIRE(!field_program_sample(p, ctx, 0, 0, &out, error, sizeof error));
    v.name = "missing";
    REQUIRE(!field_program_sample(p, ctx, 0, 0, &out, error, sizeof error));
    REQUIRE(
        !field_program_bake(p, (field_context){ 0 }, (field_grid){ 0, 1, 1 }, &out, NULL, error, sizeof error)
    );
    material_fields *catalog = material_fields_new();
    REQUIRE(material_field_compile(catalog, a, error, sizeof error));
    add(a, "Print String");
    REQUIRE(!field_program_compile(a, error, sizeof error));
    REQUIRE(!material_field_compile(catalog, a, error, sizeof error));
    REQUIRE(
        material_field_bake_text(
            catalog,
            "parameters",
            "Radius 7\n",
            0,
            (field_grid){ 1, 1, 1 },
            &out,
            NULL,
            error,
            sizeof error
        ) &&
        out == 7
    );
    {
        field_grid grid = { 7, 41, .25f, -1.5f, 2.5f };
        float repeated[7 * 41];
        v = (field_parameter){ "Radius", { .type = PIN_F32, .f = 7 } };
        field_program_baker *baker;
        baker = field_program_baker_new(p, (field_context){ 19, &v, 1 }, 1, error, sizeof error);
        REQUIRE(baker);
        v.value.f = 15;
        for (int first = 0; first < grid.height; first += 16) {
            int count = grid.height - first < 16 ? grid.height - first : 16;
            REQUIRE(
                field_program_baker_bake_rows(baker, grid, first, count, repeated, NULL, error, sizeof error)
            );
        }
        for (int i = 0; i < 7 * 41; i++) {
            REQUIRE(repeated[i] == 7);
        }
        field_program_baker_free(baker);
    }
    field_program_free(p);
    REQUIRE(material_field_revision(catalog, "parameters") == 1);
    REQUIRE(!material_field_bake_text(
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
    REQUIRE(!field_program_compile(a, error, sizeof error));
    REQUIRE(strstr(error, "Recursive"));
    behavior_asset_free(a);
    a = field("invalid evaluation");
    node *division = add(a, "Field Divide");
    output = add(a, "Field Output");
    value(a, division, 0, 1);
    wire(a, division, output, 0);
    p = compile(a);
    REQUIRE(!field_program_sample(p, (field_context){ 0 }, 0, 0, &out, error, sizeof error));
    field_program_free(p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(field_noise_and_composition)
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
    REQUIRE(field_program_bake(p, ctx, grid, values, NULL, error, sizeof error));
    REQUIRE(field_program_bake(p, ctx, grid, again, NULL, error, sizeof error));
    REQUIRE(hash(values, 256) == hash(again, 256));
    float partial[16 * 16];
    for (int i = 0; i < 16 * 16; i++) {
        partial[i] = -1;
    }
    REQUIRE(field_program_bake_rows(p, ctx, grid, 4, 5, partial, NULL, error, sizeof error));
    REQUIRE(!memcmp(partial + 4 * 16, values + 4 * 16, 5 * 16 * sizeof(float)));
    for (int y = 0; y < 16; y++) {
        if (y >= 4 && y < 9) {
            continue;
        }
        for (int x = 0; x < 16; x++) {
            REQUIRE(partial[y * 16 + x] == -1);
        }
    }
    field_grid wider = { 16, 32, .125f };
    float serial[16 * 32], submitted[16 * 32];
    inline_executor executor = { 0 };
    game_task_system tasks = { 2, &executor, inline_enqueue, inline_finish };
    REQUIRE(
        field_program_bake_rows(p, ctx, wider, 0, wider.height, serial, NULL, error, sizeof error)
    );
    REQUIRE(
        field_program_bake_rows(p, ctx, wider, 0, wider.height, submitted, &tasks, error, sizeof error)
    );
    REQUIRE(executor.enqueues == 1 && executor.finishes == 0);
    REQUIRE(!memcmp(serial, submitted, sizeof serial));
    {
        field_grid tiled = { 16, 41, .125f, -2, 3 };
        float expected[16 * 41], repeated[16 * 41];
        REQUIRE(field_program_bake(p, ctx, tiled, expected, NULL, error, sizeof error));
        game_task_pool *pool = game_task_pool_acquire_workers(4);
        const game_task_system *pool_tasks = game_task_pool_tasks(pool);
        field_program_baker *baker;
        baker = field_program_baker_new(p, ctx, pool_tasks->workers, error, sizeof error);
        REQUIRE(baker);
        game_task_pool_begin(pool, true);
        REQUIRE(
            field_program_baker_bake_rows(baker, tiled, 0, tiled.height, repeated, pool_tasks, error, sizeof error)
        );
        game_task_pool_end(pool);
        REQUIRE(!memcmp(expected, repeated, sizeof expected));
        game_task_pool_begin(pool, true);
        for (int first = 0; first < tiled.height; first += 16) {
            int count = tiled.height - first < 16 ? tiled.height - first : 16;
            REQUIRE(
                field_program_baker_bake_rows(baker, tiled, first, count, repeated, pool_tasks, error, sizeof error)
            );
        }
        game_task_pool_end(pool);
        REQUIRE(!memcmp(expected, repeated, sizeof expected));
        field_program_baker_free(baker);
        game_task_pool_release(pool);
    }
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            float scalar_value;
            REQUIRE(
                field_program_sample(p, ctx, (x + .5f - 8) * .125f, (y + .5f - 8) * .125f, &scalar_value, error, sizeof error)
            );
            REQUIRE(scalar_value == values[y * 16 + x]);
        }
    }
    ctx.seed++;
    REQUIRE(field_program_bake(p, ctx, grid, again, NULL, error, sizeof error));
    REQUIRE(hash(values, 256) != hash(again, 256));
    field_program_free(p);
    behavior_asset_free(a);
    return true;
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

TEST_CASE(field_rows_match_scalar_samples)
{
    char error[256];
    behavior_asset *asset = field("row-arithmetic");
    REQUIRE(behavior_asset_add_variable(
        asset,
        "Offset",
        PIN_F32,
        NULL,
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .f = .25f },
        NULL
    ));
    node *x = add(asset, "Field X"), *parameter = add(asset, "Field Parameter");
    REQUIRE(
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
    REQUIRE(baker);
    const int widths[] = { 1, 2, 3, 4, 5, 6, 7, 3, 9 };
    for (int i = 0; i < (int)(sizeof widths / sizeof widths[0]); i++) {
        field_grid grid = { widths[i], 5, .5f, -.125f, .25f };
        float baked[9 * 5];
        REQUIRE(field_program_baker_bake_rows(baker, grid, 0, 2, baked, NULL, error, sizeof error));
        REQUIRE(field_program_baker_bake_rows(baker, grid, 2, 3, baked, NULL, error, sizeof error));
        compare_field_samples(program, original_context, grid, baked);
    }
    offset.value.f = -2;
    field_grid resized = { 9, 5, .5f, -.125f, .25f };
    float snapshotted[9 * 5], rebound[9 * 5];
    REQUIRE(
        field_program_baker_bake_rows(baker, resized, 0, resized.height, snapshotted, NULL, error, sizeof error)
    );
    compare_field_samples(program, original_context, resized, snapshotted);
    field_program_baker_free(baker);
    baker = field_program_baker_new(program, context, 1, error, sizeof error);
    REQUIRE(baker);
    REQUIRE(
        field_program_baker_bake_rows(baker, resized, 0, resized.height, rebound, NULL, error, sizeof error)
    );
    compare_field_samples(program, context, resized, rebound);
    REQUIRE(memcmp(snapshotted, rebound, sizeof snapshotted) != 0);
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
        REQUIRE(
            field_program_bake(program, (field_context){ 0 }, grid, baked, NULL, error, sizeof error)
        );
        compare_field_samples(program, (field_context){ 0 }, grid, baked);
        bool negative_zero = false;
        for (int i = 0; i < width * grid.height; i++) {
            uint32_t bits;
            memcpy(&bits, &baked[i], sizeof bits);
            negative_zero |= bits == UINT32_C(0x80000000);
        }
        REQUIRE(negative_zero);
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
    REQUIRE(
        !field_program_bake(program, (field_context){ 0 }, grid, baked, NULL, error, sizeof error)
    );
    float scalar;
    REQUIRE(
        !field_program_sample(program, (field_context){ 0 }, 2, 0, &scalar, error, sizeof error)
    );
    field_program_free(program);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(field_repeated_inputs)
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
    REQUIRE(field_program_bake(p, ctx, grid, baked, NULL, error, sizeof error));
    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            float sampled;
            REQUIRE(field_program_sample(
                p,
                ctx,
                grid.center_x + (x + .5f - grid.width * .5f) * grid.cell_size,
                grid.center_y + (y + .5f - grid.height * .5f) * grid.cell_size,
                &sampled,
                error,
                sizeof error
            ));
            REQUIRE(sampled == baked[y * grid.width + x]);
        }
    }
    field_program_free(p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(field_functions)
{
    char error[256];
    behavior_asset *a = field("composed");
    graph *fn = behavior_asset_add_function(a, "Double", true);
    REQUIRE(fn);
    node *entry = graph_node_by_id(fn, fn->entry_id),
         *ret = graph_node_by_id(fn, fn->return_id);
    REQUIRE(graph_add_pin(fn, entry, 1, &(pin_spec){ .name = "X", .type = PIN_F32 }) == 0);
    REQUIRE(graph_add_pin(fn, ret, 0, &(pin_spec){ .name = "Value", .type = PIN_F32 }) == 0);
    node *plus = graph_add_node(fn, registry_node_by_name("Field Add"));
    REQUIRE(!graph_connect_data(fn, entry, 0, plus, 0));
    REQUIRE(!graph_connect_data(fn, entry, 0, plus, 1));
    REQUIRE(!graph_connect_data(fn, plus, 0, ret, 0));
    node *call = behavior_asset_add_function_call_node(a, fn),
         *out = add(a, "Field Output");
    REQUIRE(call);
    value(a, call, 0, 3);
    wire(a, call, out, 0);
    field_program *p = compile(a);
    REQUIRE(sample(p, 0, 0) == 6);
    field_program_free(p);
    node *recursive = behavior_asset_add_function_call_node_to_graph(a, fn, fn);
    REQUIRE(recursive);
    REQUIRE(!field_program_compile(a, error, sizeof error));
    REQUIRE(strstr(error, "Recursive"));
    REQUIRE(!graph_remove_node(fn, recursive));
    behavior_asset *library = behavior_asset_new("ScalarFields");
    library->domain = BEHAVIOR_DOMAIN_LIBRARY;
    /* Move a copied pure function into the existing library contract. */
    char *json = graph_save(fn);
    graph *copy = graph_load(json);
    free(json);
    REQUIRE(copy);
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
    REQUIRE(linked);
    p = compile(linked);
    REQUIRE(sample(p, 0, 0) == 6);
    field_program_free(p);
    behavior_asset_free(linked);
    behavior_asset_free(library);
    behavior_asset_free(a);
    return true;
}

/* A function returns several values, and a stage both results share
   compiles once. */
TEST_CASE(field_multiple_results)
{
    behavior_asset *a = field("pair");
    graph *fn = behavior_asset_add_function(a, "Pair", true);
    node *entry = graph_node_by_id(fn, fn->entry_id),
         *ret = graph_node_by_id(fn, fn->return_id);
    REQUIRE(graph_add_pin(fn, entry, 1, &(pin_spec){ .name = "V", .type = PIN_F32 }) == 0);
    REQUIRE(graph_add_pin(fn, ret, 0, &(pin_spec){ .name = "Sum", .type = PIN_F32 }) == 0);
    REQUIRE(graph_add_pin(fn, ret, 0, &(pin_spec){ .name = "Product", .type = PIN_F32 }) == 1);
    node *plus = graph_add_node(fn, registry_node_by_name("Field Add")),
         *times = graph_add_node(fn, registry_node_by_name("Field Multiply"));
    REQUIRE(!graph_connect_data(fn, entry, 0, plus, 0));
    REQUIRE(!graph_connect_data(fn, entry, 0, plus, 1));
    REQUIRE(!graph_connect_data(fn, plus, 0, times, 0));
    REQUIRE(!graph_connect_data(fn, plus, 0, times, 1));
    REQUIRE(!graph_connect_data(fn, plus, 0, ret, 0));
    REQUIRE(!graph_connect_data(fn, times, 0, ret, 1));
    node *call = behavior_asset_add_function_call_node(a, fn),
         *minus = add(a, "Field Subtract"), *out = add(a, "Field Output");
    value(a, call, 0, 3);
    REQUIRE(!graph_connect_data(a->graph, call, 1, minus, 0));
    REQUIRE(!graph_connect_data(a->graph, call, 0, minus, 1));
    wire(a, minus, out, 0);
    field_program *p = compile(a);
    REQUIRE(sample(p, 0, 0) == 36 - 6);
    REQUIRE(field_program_instruction_count(p) <= 6);
    field_program_free(p);
    behavior_asset_free(a);
    return true;
}

TEST_CASE(field_terrain_chunks_and_connectivity)
{
    field_grid grid = { 130, 129, 2 };
    float *values = malloc((size_t)grid.width * grid.height * sizeof(float));
    REQUIRE(values);
    for (int i = 0; i < grid.width * grid.height; i++) {
        values[i] = -1;
    }
    material_terrain_chunk *chunks = material_terrain_chunks(values, grid);
    REQUIRE(acount(chunks) == 4);
    int count = 0;
    for (int i = 0; i < acount(chunks); i++) {
        count += chunks[i].mask.count;
    }
    REQUIRE(count == 130 * 129 && chunks[3].mask.width == 2 && chunks[3].mask.height == 1);
    material_terrain_chunks_free(chunks);
    free(values);
    float disconnected[] = { -1, -1, -1, -1, -1, -1, 1, -1, 1, -1, -1, -1 };
    material_terrain_connect(disconnected, (field_grid){ 6, 2, 1 }, MATERIAL_CONNECTIVITY_FIRST_AIR, 0);
    REQUIRE(disconnected[6] > 0 && disconnected[8] < 0);
    return true;
}
/* Prefetch budgets count 16-row slices; one 128-row tile takes eight. */
enum
{
    TILE_SLICES = 8
};

TEST_CASE(field_streams_restore_tiles)
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
    REQUIRE(material_field_compile(fields, a, error, sizeof error));
    material_stream_desc desc = { .grid = { 4099, 513, 1 },
                                  .field = "stream-fixture",
                                  .context = { .seed = 71 } };
    material_stream_view view = { -1500, 0, 80 };
    material_fields *hot_fields = material_fields_new();
    REQUIRE(material_field_compile(hot_fields, a, error, sizeof error));
    material_stream *stale_stream = material_stream_new(desc);
    REQUIRE(material_stream_update(stale_stream, hot_fields, &view, 1, 1, error, sizeof error));
    REQUIRE(material_stream_inspect(stale_stream).resident == 0);
    behavior_asset *replacement = field("stream-fixture");
    node *rx = add(replacement, "Field X"), *ry = add(replacement, "Field Y");
    node *rcircle = add(replacement, "Field Circle"),
         *rout = add(replacement, "Field Output");
    wire(replacement, rx, rcircle, 0);
    wire(replacement, ry, rcircle, 1);
    value(replacement, rcircle, 2, 4000);
    wire(replacement, rcircle, rout, 0);
    REQUIRE(material_field_compile(hot_fields, replacement, error, sizeof error));
    REQUIRE(!material_stream_update(stale_stream, hot_fields, &view, 1, 1, error, sizeof error));
    REQUIRE(
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
        REQUIRE(count == 0);
        REQUIRE(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    }
    tiles = material_stream_tiles(stream, &count);
    REQUIRE(count == 1);
    uint64_t key = tiles[0].key;
    material_mask *mask = tiles[0].mask;
    int length = mask->width * mask->height;
    REQUIRE(!material_mask_apply_damage(mask, 32, 20, 4, .3f, 1));
    REQUIRE(material_mask_carve(mask, 0, 0, 12) > 0);
    int remaining = mask->count;
    unsigned char *cells = malloc((size_t)length);
    float *distance = malloc((size_t)length * sizeof(float));
    float *damage = malloc((size_t)length * sizeof(float));
    REQUIRE(cells && distance && damage);
    memcpy(cells, mask->cells, (size_t)length);
    memcpy(distance, mask->distance, (size_t)length * sizeof(float));
    memcpy(damage, mask->damage, (size_t)length * sizeof(float));
    view.x = 1500;
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    REQUIRE(material_stream_inspect(stream).unloaded == 1);
    view.x = -1500;
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    REQUIRE(count == 1 && tiles[0].key == key);
    mask = tiles[0].mask;
    REQUIRE(mask->count == remaining);
    REQUIRE(!memcmp(cells, mask->cells, (size_t)length));
    REQUIRE(!memcmp(distance, mask->distance, (size_t)length * sizeof(float)));
    REQUIRE(!memcmp(damage, mask->damage, (size_t)length * sizeof(float)));
    /* A released chunk remains gone even after its mask ownership transfers. */
    material_mask_free(mask);
    *mask = (material_mask){ 0 };
    size_t stored_bytes = material_stream_inspect(stream).stored_bytes;
    view.x = 1500;
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    view.x = -1500;
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    REQUIRE(tiles[0].mask->count == 0);
    REQUIRE(material_stream_inspect(stream).stored_bytes == stored_bytes);
    /* Traversal remains local even though the world exceeds one bake's limit. */
    for (int i = 0; i < 24; i++) {
        view.x = -1800 + 150 * i;
        REQUIRE(
            material_stream_update(stream, fields, &view, 1, 2 * TILE_SLICES, error, sizeof error)
        );
        REQUIRE(material_stream_inspect(stream).resident <= 20);
    }
    view = (material_stream_view){ 2049, 256, 0 };
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    bool edge = false;
    for (int i = 0; i < count; i++) {
        edge |= tiles[i].grid.width == 3 && tiles[i].grid.height == 1;
    }
    REQUIRE(edge);
    material_stream_free(stream);
    /* A fresh run regenerates pristine terrain. */
    stream = material_stream_new(desc);
    view = (material_stream_view){ -1500, 0, 80 };
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    REQUIRE(tiles[0].mask->count == length);
    REQUIRE(material_stream_inspect(stream).stored == 0);
    material_stream_free(stream);
    /* Signed tiles have no rectangular edge and preserve all four quadrants.
     * (-1,-1) encodes UINT64_MAX, which must not be used as a missing sentinel. */
    desc.grid = (field_grid){ 0, 0, 1 };
    stream = material_stream_new(desc);
    view = (material_stream_view){ -64, -64, 0 };
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    REQUIRE(count == 1 && tiles[0].key == UINT64_MAX);
    key = tiles[0].key;
    REQUIRE(material_mask_carve(tiles[0].mask, 0, 0, 12) > 0);
    remaining = tiles[0].mask->count;
    const material_stream_view distant[] = { { 100064, 100064, 0 },
                                             { -100064, 100064, 0 },
                                             { 100064, -100064, 0 },
                                             { -100064, -100064, 0 } };
    uint64_t keys[4];
    for (int i = 0; i < 4; i++) {
        REQUIRE(
            material_stream_update(stream, fields, &distant[i], 1, TILE_SLICES, error, sizeof error)
        );
        tiles = material_stream_tiles(stream, &count);
        REQUIRE(count == 1);
        keys[i] = tiles[0].key;
        REQUIRE(keys[i] != key);
        for (int j = 0; j < i; j++) {
            REQUIRE(keys[i] != keys[j]);
        }
        REQUIRE(fabsf(tiles[0].grid.center_x - distant[i].x) <= 64);
        REQUIRE(fabsf(tiles[0].grid.center_y - distant[i].y) <= 64);
    }
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    tiles = material_stream_tiles(stream, &count);
    REQUIRE(count == 1 && tiles[0].key == key && tiles[0].mask->count == remaining);
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
        REQUIRE(cells[5 * 12 + 5] > 0 && cells[5 * 12 + 6] > 0);
        REQUIRE(cells[0 * 12 + 2] < 0 && cells[1 * 12 + 2] < 0);
        REQUIRE(cells[9 * 12 + 1] < 0);
        material_terrain_tidy(cells, small, 0);
    }
    /* Every tile inside a required extent loads at once; the budget only
     * limits prefetching beyond it. */
    stream = material_stream_new(desc);
    view = (material_stream_view){ 0, 0, 1000, 200 };
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    REQUIRE(material_stream_inspect(stream).resident == 16 + 1);
    REQUIRE(material_stream_update(stream, fields, &view, 1, TILE_SLICES, error, sizeof error));
    REQUIRE(material_stream_inspect(stream).resident == 16 + 2);
    material_stream_free(stream);
    material_fields_free(fields);
    behavior_asset_free(a);
    free(cells);
    free(distance);
    free(damage);
    return true;
}

TEST_CASE(field_streams_retry_failed_slices)
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
    REQUIRE(material_field_compile(fields, asset, error, sizeof error));
    material_stream *stream = material_stream_new((material_stream_desc){
        .grid = { 0, 0, 1 },
        .field = "stream-failing-row",
    });
    material_stream_view view = { 64, 64, 0 };
    REQUIRE(material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    REQUIRE(!material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    REQUIRE(!material_stream_update(stream, fields, &view, 1, 1, error, sizeof error));
    REQUIRE(strstr(error, "invalid scalar"));
    REQUIRE(material_stream_inspect(stream).resident == 0);
    material_stream_free(stream);
    material_fields_free(fields);
    behavior_asset_free(asset);
    return true;
}

/* Fields use Box2D's cosine and sine, identical on every platform: within
 * 0.002 of libm across many turns, and exact at zero. */
TEST_CASE(deterministic_cos_sin_stay_accurate)
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
        REQUIRE(cosine == golden[i].cosine && sine == golden[i].sine);
    }
    float worst = 0;
    for (float angle = -1000; angle < 1000; angle += 0.37f) {
        cos_sin r = cos_sin_compute(angle);
        worst = fmaxf(worst, fmaxf(fabsf(r.c - cosf(angle)), fabsf(r.s - sinf(angle))));
    }
    cos_sin zero = cos_sin_compute(0);
    REQUIRE(worst < 0.002f && zero.c == 1 && zero.s == 0);
    return true;
}

TEST_CASE(deterministic_atan2_stays_accurate)
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
        REQUIRE(bits == golden[i].angle);
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
    REQUIRE(worst < 0.0001f);
    return true;
}

TEST_CASE(deterministic_exponentials_stay_accurate)
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
        REQUIRE(bits == golden[i].result);
    }
    REQUIRE(deterministic_expf(-INFINITY) == 0);
    REQUIRE(isinf(deterministic_expf(INFINITY)));
    REQUIRE(isnan(deterministic_expf(NAN)));
    REQUIRE(deterministic_expf(-104) == 0);
    REQUIRE(isinf(deterministic_expf(89)));
    for (float x = -80; x <= 88; x += 0.125f) {
        double expected = exp((double)x);
        double actual = deterministic_expf(x);
        REQUIRE(fabs(actual - expected) <= expected * 1e-6);
    }
    return true;
}

#define CONTRACTS "test/fixtures/material-contracts/content"

/* A field calls a pure function of a `.chibi` library, lowered to the field
 * nodes its native calls name. */
TEST_CASE(field_links_source_library)
{
    char *text = text_file_read(CONTRACTS "/libraries/FieldShapes.chibi");
    REQUIRE(text);
    test_source_fixture fixture = test_source_prepare(CONTRACTS "/libraries/FieldShapes.chibi", text);
    free(text);
    behavior_asset **libraries = authoring_source_graph_libraries(fixture.sources);
    const behavior_asset *shapes = NULL;
    for (int i = 0; i < acount(libraries); i++) {
        shapes = strcmp(libraries[i]->name, "FieldShapes") ? shapes : libraries[i];
    }
    REQUIRE(shapes && behavior_asset_find_graph_by_name(shapes, "Islands"));
    text = text_file_read(CONTRACTS "/fields/MaterialIslands.kdl");
    behavior_asset *islands = behavior_asset_load(text);
    free(text);
    REQUIRE(islands);
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(islands, &shapes, 1, error, sizeof error);
    REQUIRE(linked);
    field_program *p = compile(linked);
    REQUIRE(
        sample(p, -90, 0) < -60 && sample(p, 60, 0) < -50 &&
        sample(p, 0, 0) < -15 && sample(p, 0, 150) > 50
    );
    field_program_free(p);
    behavior_asset_free(linked);

    /* A library without the function the field names refuses the link. */
    behavior_asset *empty = behavior_asset_new("FieldShapes");
    empty->domain = BEHAVIOR_DOMAIN_LIBRARY;
    const behavior_asset *missing = empty;
    REQUIRE(!behavior_asset_link_libraries(islands, &missing, 1, error, sizeof error));
    REQUIRE(strstr(error, "missing library function"));
    behavior_asset_free(empty);
    behavior_asset_free(islands);
    authoring_source_graph_libraries_free(libraries);
    test_source_free(&fixture);
    return true;
}

/* A call added for a lowered function takes its parameters' defaults. */
TEST_CASE(field_source_library_keeps_parameter_defaults)
{
    const char
        *text = "library \"Discs\" {\n"
                "    pure function \"Disc\"(X: float = 0.0, Y: float = 0.0, "
                "Radius: float = 40.0) -> (Value: float = 0.0) {\n"
                "        return field_circle(entry.X, entry.Y, entry.Radius)\n"
                "    }\n"
                "}\n";
    test_source_fixture fixture = test_source_prepare("content/libraries/Discs.chibi", text);
    behavior_asset **libraries = authoring_source_graph_libraries(fixture.sources);
    REQUIRE(acount(libraries) == 1);
    graph *disc = behavior_asset_find_graph_by_name(libraries[0], "Disc");
    REQUIRE(disc);
    behavior_asset *field = behavior_asset_new("DiscField");
    field->domain = BEHAVIOR_DOMAIN_FIELD;
    node *call = behavior_asset_add_function_call_node(field, disc);
    node *output = add(field, "Field Output");
    REQUIRE(call && output && !graph_connect_data(field->graph, call, 0, output, 0));
    char error[256];
    const behavior_asset *catalog = libraries[0];
    behavior_asset *linked = behavior_asset_link_libraries(field, &catalog, 1, error, sizeof error);
    REQUIRE(linked);
    field_program *p = compile(linked);
    REQUIRE(p && sample(p, 0, 0) == -40);
    field_program_free(p);
    behavior_asset_free(linked);
    behavior_asset_free(field);
    authoring_source_graph_libraries_free(libraries);
    test_source_free(&fixture);
    return true;
}

/* Source functions and their parameters keep their names when generated IDs
 * move. Calls authored through the public API must survive that regeneration. */
TEST_CASE(field_source_library_calls_follow_names)
{
    static const char *sources[] = {
        "library \"Values\" {"
        "pure function \"Other\"() -> (Value: float) { return 111.0 }"
        "pure function \"Difference\"(X: float = 10.0, Y: float = 3.0) "
        "-> (Value: float) { return field_subtract(entry.X, entry.Y) } }",
        "library \"Values\" {"
        "pure function \"Inserted\"() -> (Value: float) { return 999.0 }"
        "pure function \"Other\"() -> (Value: float) { return 111.0 }"
        "pure function \"Difference\"(Y: float = 30.0, X: float = 100.0) "
        "-> (Value: float) { return field_subtract(entry.X, entry.Y) } }",
        "library \"Values\" {"
        "pure function \"Inserted\"() -> (Value: float) { return 999.0 }"
        "pure function \"Other\"() -> (Value: float) { return 111.0 } }",
    };
    test_source_fixture fixture = test_source_prepare("content/libraries/Values.chibi", sources[0]);
    behavior_asset **libraries = authoring_source_graph_libraries(fixture.sources);
    REQUIRE(acount(libraries) == 1);
    /* Calls also retain name identity when their generated library is cloned. */
    behavior_asset *library = behavior_asset_clone(libraries[0]);
    REQUIRE(library);
    graph *function = behavior_asset_find_graph_by_name(library, "Difference");
    REQUIRE(function);
    uint64_t generated_id = function->graph_id;
    behavior_asset *field = behavior_asset_new("DifferenceField");
    field->domain = BEHAVIOR_DOMAIN_FIELD;
    node *call = behavior_asset_add_function_call_node(field, function);
    node *x = add(field, "Field X"), *output = add(field, "Field Output");
    REQUIRE(call && x && output);
    REQUIRE(!graph_connect_data(field->graph, x, 0, call, 0));
    REQUIRE(!graph_connect_data(field->graph, call, 0, output, 0));
    uint64_t call_id = call->id;
    char *saved = behavior_asset_save(field);
    REQUIRE(saved);
    behavior_asset_free(field);
    field = behavior_asset_load(saved);
    free(saved);
    REQUIRE(field);
    call = graph_node_by_id(field->graph, call_id);
    REQUIRE(call);
    bool named_call = call->reference_id == 0 &&
                      call->input_pins[0].declaration_id == 0;
    behavior_asset_free(library);
    authoring_source_graph_libraries_free(libraries);
    test_source_free(&fixture);

    for (int i = 0; i < 3; i++) {
        fixture = test_source_prepare("content/libraries/Values.chibi", sources[i]);
        libraries = authoring_source_graph_libraries(fixture.sources);
        REQUIRE(acount(libraries) == 1);
        const behavior_asset *catalog = libraries[0];
        char error[256];
        behavior_asset *linked = behavior_asset_link_libraries(field, &catalog, 1, error, sizeof error);
        if (i == 2) {
            REQUIRE(!linked && strstr(error, "missing library function"));
        } else {
            REQUIRE(linked);
            field_program *program = compile(linked);
            REQUIRE(program && sample(program, 10, 0) == 7);
            field_program_free(program);
            behavior_asset_free(linked);
        }
        if (i == 0) {
            /* Existing authored calls may retain an earlier generated ID. */
            call->reference_id = generated_id;
        }
        authoring_source_graph_libraries_free(libraries);
        test_source_free(&fixture);
    }
    REQUIRE(named_call);
    behavior_asset_free(field);
    return true;
}

static char root[] = "/tmp/chibi-fields-XXXXXX";

static void copy_fixture(const char *folder, const char *file)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, folder);
    mkdir(path, 0700);
    snprintf(path, sizeof path, CONTRACTS "/%s/%s", folder, file);
    char *text = text_file_read(path);
    assert(text);
    snprintf(path, sizeof path, "%s/%s/%s", root, folder, file);
    FILE *out = fopen(path, "wb");
    assert(out && fputs(text, out) >= 0 && !fclose(out));
    free(text);
}

static void remove_fixture(const char *folder, const char *file)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s/%s", root, folder, file);
    unlink(path);
    snprintf(path, sizeof path, "%s/%s", root, folder);
    rmdir(path);
}

static float islands_at(material_fields *fields, float x)
{
    char error[256];
    field_context context;
    float out;
    bool ready = material_field_context(fields, "MaterialIslands", NULL, 1, &context, error, sizeof error) &&
                 material_field_bake(
                     fields,
                     "MaterialIslands",
                     context,
                     (field_grid){ 1, 1, 1, x, 0 },
                     &out,
                     NULL,
                     error,
                     sizeof error
                 );
    assert(ready);
    (void)ready;
    return out;
}

/* Publishing a changed source library relinks and recompiles the fields that
 * call it; a library the field can no longer call keeps the applied field. */
TEST_CASE(field_source_library_hot_apply)
{
    strcpy(root, "/tmp/chibi-fields-XXXXXX");
    REQUIRE(mkdtemp(root));
    copy_fixture("libraries", "FieldShapes.chibi");
    copy_fixture("fields", "MaterialIslands.kdl");
    ecs_world *ecs = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(!ecs_world_register_component(ecs, GAME_REGISTRY_COMPONENTS[i], NULL));
    }
    game_preview_world *live = game_preview_world_new(ecs);
    REQUIRE(app_behavior_fixtures_load(live, NULL, root) == 2);
    material_fields *fields = game_materials_fields(live->runtime->materials);
    REQUIRE(material_field_revision(fields, "MaterialIslands") == 1);
    float before = islands_at(fields, -90);

    /* A valid field draft changes the result, but is not applied with its library. */
    game_preview_definition *definition = game_preview_find_definition(live, "MaterialIslands");
    REQUIRE(definition);
    graph *draft = definition->working->graph;
    node *sum = graph_add_node(draft, registry_node_by_name("Field Add"));
    REQUIRE(sum);
    REQUIRE(
        !graph_set_pin_default(draft, sum, false, 1, &(pin_value){ .type = PIN_F32, .f = 1000 })
    );
    REQUIRE(!graph_connect_data(draft, graph_node_by_id(draft, 49), 0, sum, 0));
    REQUIRE(!graph_connect_data(draft, sum, 0, graph_node_by_id(draft, 53), 0));
    REQUIRE(!game_preview_mark_dirty(definition));
    behavior_asset *saved_draft = behavior_asset_clone(definition->working);
    REQUIRE(saved_draft);
    behavior_asset *linked_draft = game_preview_link(live, definition->working);
    REQUIRE(linked_draft);
    field_program *draft_program = compile(linked_draft);
    REQUIRE(fabsf(sample(draft_program, -90, 0) - before - 1000) < .001f);
    field_program_free(draft_program);
    behavior_asset_free(linked_draft);

    char path[512], error[256];
    snprintf(path, sizeof path, "%s/libraries/FieldShapes.chibi", root);
    char *text = text_file_read(path);
    char *grown = strstr(text, "65.0");
    REQUIRE(grown);
    memcpy(grown, "99.0", 4);
    REQUIRE(!game_preview_source_edit(live, path, text, error, sizeof error));
    REQUIRE(!game_preview_sources_publish(live, NULL, error, sizeof error));
    REQUIRE(material_field_revision(fields, "MaterialIslands") == 2);
    float after = islands_at(fields, -90);
    REQUIRE(after < before - 30);
    REQUIRE(definition->dirty && definition->unsaved);
    REQUIRE(behavior_asset_equal(definition->working, saved_draft));
    behavior_asset_free(saved_draft);

    /* An invalid field draft must neither block publication nor become applied. */
    REQUIRE(graph_add_node(draft, registry_node_by_name("Print String")));
    saved_draft = behavior_asset_clone(definition->working);
    REQUIRE(saved_draft);
    linked_draft = game_preview_link(live, definition->working);
    REQUIRE(linked_draft);
    REQUIRE(!field_program_compile(linked_draft, error, sizeof error));
    behavior_asset_free(linked_draft);
    memcpy(grown, "88.0", 4);
    REQUIRE(!game_preview_source_edit(live, path, text, error, sizeof error));
    REQUIRE(!game_preview_sources_publish(live, NULL, error, sizeof error));
    REQUIRE(material_field_revision(fields, "MaterialIslands") == 3);
    REQUIRE(fabsf(islands_at(fields, -90) - after - 11) < .001f);
    after = islands_at(fields, -90);
    REQUIRE(definition->dirty && definition->unsaved);
    REQUIRE(behavior_asset_equal(definition->working, saved_draft));

    char *renamed = strstr(text, "\"Islands\"");
    REQUIRE(renamed);
    memcpy(renamed, "\"Isles\"  ", 9);
    REQUIRE(!game_preview_source_edit(live, path, text, error, sizeof error));
    REQUIRE(game_preview_sources_publish(live, NULL, error, sizeof error));
    REQUIRE(strstr(error, "missing library function"));
    REQUIRE(material_field_revision(fields, "MaterialIslands") == 3);
    REQUIRE(islands_at(fields, -90) == after);
    REQUIRE(definition->dirty && definition->unsaved);
    REQUIRE(behavior_asset_equal(definition->working, saved_draft));
    behavior_asset_free(saved_draft);
    free(text);

    game_preview_world_free(live);
    ecs_world_free(ecs);
    remove_fixture("libraries", "FieldShapes.chibi");
    remove_fixture("fields", "MaterialIslands.kdl");
    rmdir(root);
    return true;
}

void test_fields()
{
    /* Every case reads field nodes from the same packs. */
    bool installed = test_game_graph_packs_set();
    assert(installed);
    RUN_TEST_CASE(deterministic_cos_sin_stay_accurate);
    RUN_TEST_CASE(deterministic_atan2_stays_accurate);
    RUN_TEST_CASE(deterministic_exponentials_stay_accurate);
    RUN_TEST_CASE(field_streams_restore_tiles);
    RUN_TEST_CASE(field_streams_retry_failed_slices);
    RUN_TEST_CASE(field_primitives_bake_as_they_sample);
    RUN_TEST_CASE(field_parameters_and_errors);
    RUN_TEST_CASE(field_noise_and_composition);
    RUN_TEST_CASE(field_rows_match_scalar_samples);
    RUN_TEST_CASE(field_repeated_inputs);
    RUN_TEST_CASE(field_functions);
    RUN_TEST_CASE(field_multiple_results);
    RUN_TEST_CASE(field_links_source_library);
    RUN_TEST_CASE(field_source_library_keeps_parameter_defaults);
    RUN_TEST_CASE(field_source_library_calls_follow_names);
    RUN_TEST_CASE(field_source_library_hot_apply);
    RUN_TEST_CASE(field_terrain_chunks_and_connectivity);
    test_game_graph_packs_clear();
}
