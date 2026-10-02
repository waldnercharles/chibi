#include "render/mesh_asset.h"

#include "render/mesh_surface.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char
    *triangle = "{\"version\":1,\"vertices\":[[-.5,0,-.5,0,2,0,.2,.8],"
                "[.5,0,-.5,0,2,0,.8,.8],[0,0,.5,0,2,0,.5,.1]]}";

static void near_value(float actual, float expected)
{
    if (fabsf(actual - expected) >= .001f) {
        fprintf(stderr, "mesh asset: got %g expected %g\n", actual, expected);
    }
    assert(fabsf(actual - expected) < .001f);
}

void test_mesh_asset()
{
    /* JSON requires leading zeroes. Keep the valid fixture independent of the
       exporter: parser and transformed picking are used before any GPU call. */
    const char
        *valid = "{\"version\":1,\"vertices\":[[-0.5,0,-0.5,0,2,0,0.2,0.8],"
                 "[0.5,0,-0.5,0,2,0,0.8,0.8],[0,0,0.5,0,2,0,0.5,0.1]]}";
    render_mesh_asset asset = { 0 };
    char error[256];
    assert(render_mesh_asset_parse(valid, &asset, error, sizeof error));
    assert(acount(asset.vertices) == 3);
    near_value(asset.min.x, -.5f);
    near_value(asset.max.z, .5f);
    near_value(asset.vertices[0].normal.y, 1);
    near_value(asset.vertices[0].uv.y, .8f);
    render_mesh_surface_vertex *original = asset.vertices;
    const char *invalid[] = {
        NULL,
        "",
        "[]",
        "{\"version\":2,\"vertices\":[]}",
        triangle,
        "{\"version\":1,\"vertices\":[[0,0,0,0,1,0,0,0]]}",
        "{\"version\":1,\"vertices\":[[0,0,0,0,1,0,0],"
        "[1,0,0,0,1,0,0,0],[0,0,1,0,1,0,0,0]]}",
        "{\"version\":1,\"vertices\":[[0,0,0,0,0,0,0,0],"
        "[1,0,0,0,1,0,0,0],[0,0,1,0,1,0,0,0]]}",
        "{\"version\":1,\"vertices\":[[1e100,0,0,0,1,0,0,0],"
        "[1,0,0,0,1,0,0,0],[0,0,1,0,1,0,0,0]]}",
        "{\"version\":1,\"vertices\":[[0,0,0,0,1,0,0,1e100],"
        "[1,0,0,0,1,0,0,0],[0,0,1,0,1,0,0,0]]}",
        "{\"version\":1,\"vertices\":[[0,0,0,0,1,0,0,0],"
        "[1,0,0,0,1,0,0,0],[2,0,0,0,1,0,0,0]]}",
        "{\"version\":1,\"vertices\":[[0,0,0,0,1,0,0,0],"
        "[1,0,0,0,1,0,0,0],[0,0,1,0,1,0,NaN,0]]}"
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        assert(!render_mesh_asset_parse(invalid[i], &asset, error, sizeof error));
        assert(error[0] && asset.vertices == original && acount(asset.vertices) == 3);
    }
    render_mesh_surface surface = { 0 };
    render_mesh_surface_part part = { .vertices = asset.vertices };
    part.visual.part.dimensions = (CF_V3){ 8, 4, 6 };
    part.visual.frame = cf_m4_from_trs(
        (CF_V3){ 10, 7, 20 },
        game_visual_rotation(0, (CF_V3){ 0 }),
        (CF_V3){ 1, 1, 1 }
    );
    apush(surface.parts, part);
    float height;
    assert(render_mesh_surface_pick(&surface, (CF_V2){ 10, 20 }, &height));
    near_value(height, 7);
    assert(!render_mesh_surface_pick(&surface, (CF_V2){ 14, 23 }, &height));
    surface.parts[0].visual.frame = cf_m4_from_trs(
        (CF_V3){ 10, 7, 20 },
        game_visual_rotation(0, (CF_V3){ 0, 0, .5235987756f }),
        (CF_V3){ 1, 1, 1 }
    );
    assert(render_mesh_surface_pick(&surface, (CF_V2){ 10, 20 }, &height));
    near_value(height, 7);
    CF_V3 normal = render_mesh_surface_normal(
        render_arena_visual_model(&surface.parts[0].visual),
        asset.vertices[0].normal
    );
    near_value(normal.x, 0);
    /* Cute quaternion normalization uses a fast approximation. */
    assert(fabsf(normal.y - cosf(.5235987756f)) < .005f);
    assert(fabsf(normal.z - sinf(.5235987756f)) < .005f);
    /* Free the borrowed array exactly once. */
    surface.parts[0].vertices = NULL;
    render_mesh_surface_free(&surface);
    render_mesh_asset_free(&asset);
    assert(!asset.vertices);
    puts("mesh asset PASS: invalid/finite data, atomic failure, UV, transformed picking and normals");
}
