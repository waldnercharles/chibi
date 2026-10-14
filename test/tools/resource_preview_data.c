#include "authoring/resource_preview_data.h"

#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <string.h>
TEST_CASE(stylesheet_swatches_follow_literal_declaration_colors_only)
{
    unsigned char colors[64][4];
    int count;
    char error[128];
    REQUIRE(resource_preview_palette(
        "/* #abcdef */ #abc { content: '#fedcba'; color:#abc; border:1px "
        "#aabbcc; background:linear-gradient(#1234,#ff000080); "
        "--dynamic:var(--external); }",
        colors,
        &count,
        error,
        sizeof error
    ));
    REQUIRE(count == 3);
    REQUIRE(!memcmp(colors[0], (unsigned char[]){ 0xaa, 0xbb, 0xcc, 255 }, 4));
    REQUIRE(!memcmp(colors[1], (unsigned char[]){ 0x11, 0x22, 0x33, 0x44 }, 4));
    REQUIRE(!memcmp(colors[2], (unsigned char[]){ 255, 0, 0, 128 }, 4));
    REQUIRE(!resource_preview_palette(
        "#abc {color:var(--external);content:'#123456';}",
        colors,
        &count,
        error,
        sizeof error
    ));
    REQUIRE(count == 0 && error[0]);
    REQUIRE(!resource_preview_palette("x { color: #12;", colors, &count, error, sizeof error));
    return true;
}
TEST_CASE(graph_thumbnails_are_repeatable_read_only_and_bounded)
{
    const node_desc descriptor = { .name = "Example", .flow_kind = NODE_FLOW_NONE };
    node first = { .desc = &descriptor, .id = 1, .x = 0, .y = 0 },
         second = { .desc = &descriptor, .id = 2, .x = 300, .y = 120 };
    graph view = { 0 };
    apush(view.nodes, &first);
    apush(view.nodes, &second);
    apush(view.edges, ((edge){ .from_id = 1, .to_id = 2, .kind = GRAPH_EDGE_EXEC }));
    unsigned char before[64 * 64 * 4], after[64 * 64 * 4];
    char error[128];
    REQUIRE(resource_preview_graph(&view, 64, 64, before, error, sizeof error));
    REQUIRE(first.x == 0 && first.y == 0 && second.x == 300 && second.y == 120);
    REQUIRE(resource_preview_graph(&view, 64, 64, after, error, sizeof error));
    REQUIRE(!memcmp(before, after, sizeof before));
    first.x += 1000;
    second.x += 1000;
    first.y -= 500;
    second.y -= 500;
    REQUIRE(resource_preview_graph(&view, 64, 64, after, error, sizeof error));
    REQUIRE(!memcmp(before, after, sizeof before));
    first.x = NAN;
    REQUIRE(!resource_preview_graph(&view, 64, 64, after, error, sizeof error));
    first.x = 1000;
    for (int i = 2; i < 513; i++) {
        apush(view.nodes, &first);
    }
    REQUIRE(!resource_preview_graph(&view, 64, 64, after, error, sizeof error));
    afree(view.nodes);
    afree(view.edges);
    return true;
}
TEST_CASE(thumbnail_routes_preserve_semantic_asset_kinds)
{
    const char *graphs[] = { "behavior", "event", "library", "shader-library", "system" };
    for (size_t i = 0; i < sizeof graphs / sizeof *graphs; ++i) {
        REQUIRE(resource_preview_route(graphs[i], "content/assets/Named.kdl") == RESOURCE_PREVIEW_GRAPH);
        REQUIRE(
            resource_preview_route(graphs[i], "content/assets/Named.chibi") == RESOURCE_PREVIEW_GRAPH
        );
    }
    const char *visuals[] = { "material", "shape", "field", "model" };
    for (size_t i = 0; i < sizeof visuals / sizeof *visuals; ++i) {
        REQUIRE(resource_preview_route(visuals[i], "content/assets/Named.kdl") == RESOURCE_PREVIEW_VISUAL);
    }
    REQUIRE(resource_preview_route("prototype", "content/Prefabs/Named.kdl") == RESOURCE_PREVIEW_SCENE);
    REQUIRE(resource_preview_route("scene", "content/Scenes/Named.kdl") == RESOURCE_PREVIEW_SCENE);
    REQUIRE(resource_preview_route("ui", "content/ui/Named.rml") == RESOURCE_PREVIEW_UI);
    REQUIRE(resource_preview_route("ui", "content/ui/Named.rcss") == RESOURCE_PREVIEW_STYLESHEET);
    REQUIRE(resource_preview_route("stylesheet", "content/ui/Named.rcss") == RESOURCE_PREVIEW_STYLESHEET);
    REQUIRE(resource_preview_route("image", "content/images/Named.PNG") == RESOURCE_PREVIEW_RASTER);
    REQUIRE(resource_preview_route("image", "content/images/Named.JpEg") == RESOURCE_PREVIEW_RASTER);
    REQUIRE(resource_preview_route("image", "content/images/Named.jpeg") == RESOURCE_PREVIEW_RASTER);
    REQUIRE(resource_preview_route("image", "content/images/Named.svg") == RESOURCE_PREVIEW_NONE);
    REQUIRE(resource_preview_route("sound", "content/audio/Named.kdl") == RESOURCE_PREVIEW_NONE);
    REQUIRE(resource_preview_route("ui", "content/ui/Named.kdl") == RESOURCE_PREVIEW_NONE);
    REQUIRE(resource_preview_route(NULL, "content/ui/Named.rml") == RESOURCE_PREVIEW_NONE);
    REQUIRE(resource_preview_route("ui", NULL) == RESOURCE_PREVIEW_NONE);
    return true;
}
void test_tool_resource_preview_data()
{
    RUN_TEST_CASE(thumbnail_routes_preserve_semantic_asset_kinds);
    RUN_TEST_CASE(stylesheet_swatches_follow_literal_declaration_colors_only);
    RUN_TEST_CASE(graph_thumbnails_are_repeatable_read_only_and_bounded);
}
