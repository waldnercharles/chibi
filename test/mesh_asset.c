#include "runtime/geometry/mesh_asset.h"

#include "authoring/mesh_prepare.h"
#include "render/mesh_surface.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

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

static void large_streams()
{
    enum
    {
        count = 12288
    };
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    yyjson_mut_val *vertices = yyjson_mut_obj_add_arr(doc, root, "vertices");
    yyjson_mut_val *positions = yyjson_mut_obj_add_arr(doc, root, "part_positions");
    yyjson_mut_val *frames = yyjson_mut_obj_add_arr(doc, root, "part_frames");
    for (int i = 0; i < count; i++) {
        float x = i / 3 + (i % 3 == 1), z = i % 3 == 2;
        double vertex[] = { x, 0, z, 0, 1, 0, 0, 0 };
        double position[] = { i, -i, i + .5 };
        double frame[] = { i + 1, 0, 0, 0, 1, 0, 0, 0, -1 };
        yyjson_mut_arr_add_val(vertices, yyjson_mut_arr_with_real(doc, vertex, 8));
        yyjson_mut_arr_add_val(positions, yyjson_mut_arr_with_real(doc, position, 3));
        yyjson_mut_arr_add_val(frames, yyjson_mut_arr_with_real(doc, frame, 9));
    }
    char *text = yyjson_mut_write(doc, 0, NULL);
    render_mesh_asset asset = { 0 };
    char error[256];
    assert(render_mesh_asset_parse(text, &asset, error, sizeof error));
    free(text);
    assert(acount(asset.vertices) == count);
    for (int i = 0; i < count; i++) {
        near_value(asset.vertices[i].part_position.x, i);
        near_value(asset.vertices[i].part_position.y, -i);
        near_value(asset.vertices[i].part_position.z, i + .5f);
        near_value(asset.vertices[i].part_from_object[0][0], i + 1);
        near_value(asset.vertices[i].part_from_object[2][2], -1);
    }
    render_mesh_surface_vertex *retained = asset.vertices;
    yyjson_mut_arr_replace(frames, count - 1, yyjson_mut_str(doc, "bad final frame"));
    text = yyjson_mut_write(doc, 0, NULL);
    assert(!render_mesh_asset_parse(text, &asset, error, sizeof error));
    assert(asset.vertices == retained && strstr(error, "12287"));
    free(text);
    render_mesh_asset_free(&asset);
    yyjson_mut_doc_free(doc);
}

void test_mesh_asset()
{
    large_streams();
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
        render_scene3d_visual_model(&surface.parts[0].visual),
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
    const char *grouped = "{\"version\":1,\"vertices\":[[0,0,0,0,1,0,0,0],[1,0,"
                          "0,0,1,0,1,0],[0,0,1,0,1,0,0,1]],"
                          "\"part_positions\":[[10,0,0],[11,0,0],[10,0,1]],"
                          "\"part_frames\":[[1,0,0,0,1,0,0,0,-1],[1,0,0,0,1,0,"
                          "0,0,-1],[1,0,0,0,1,0,0,0,-1]],"
                          "\"uv_bounds\":[-1,-1,-1,1,1,1],"
                          "\"groups\":[{\"first\":0,\"count\":3,\"part\":0,"
                          "\"node\":17,\"asset\":\"Model\",\"name\":\"Wing\","
                          "\"path\":\"Model/p17\",\"material\":\"Paint\"}]}";
    assert(render_mesh_asset_parse(grouped, &asset, error, sizeof error));
    assert(asset.has_part_coordinates && asset.has_uv_bounds && acount(asset.groups) == 1);
    assert(asset.groups[0].count == 3 && asset.groups[0].node == 17);
    assert(!strcmp(asset.groups[0].material, "Paint"));
    near_value(asset.vertices[0].part_position.x, 10);
    near_value(asset.vertices[0].part_from_object[2][2], -1);
    render_mesh_asset_free(&asset);
    const char
        *missing_frames = "{\"version\":1,\"vertices\":[[0,0,0,0,1,0,0,0],[1,0,"
                          "0,0,1,0,0,0],[0,0,1,0,1,0,0,0]],\"part_positions\":["
                          "[0,0,0],[1,0,0],[0,0,1]]}";
    assert(!render_mesh_asset_parse(missing_frames, &asset, error, sizeof error));
    assert(strstr(error, "frames") && !asset.vertices);
    puts("mesh asset PASS: invalid/finite data, atomic failure, UV, transformed picking and normals");
}
