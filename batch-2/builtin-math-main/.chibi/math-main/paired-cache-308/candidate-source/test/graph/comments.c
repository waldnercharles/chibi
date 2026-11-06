#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

/* Node comments and comment frames: authored, round-tripped, and omitted from
   documents while unused. */
TEST_CASE(node_comments_fold_save_and_reload)
{
    graph g;
    graph_init(&g);
    node *real = graph_add_node(&g, graph_palette_node("Real"));
    node *add = graph_add_node(&g, graph_palette_node("Add"));
    REQUIRE(graph_connect(&g, real, 0, add, 0) == 0);
    REQUIRE(graph_node_is_inline(&g, real));
    REQUIRE(graph_set_node_comment(&g, real, "Tuned by hand", true) == 0);
    REQUIRE(!graph_node_is_inline(&g, real));
    REQUIRE(graph_set_node_comment(&g, add, "", true) == 0);
    REQUIRE(!add->comment && !add->comment_hidden);

    char *document = graph_save(&g);
    REQUIRE(document);
    REQUIRE(strstr(document, "comment=\"Tuned by hand\""));
    REQUIRE(strstr(document, "comment_hidden=#true"));
    graph *loaded = graph_load(document);
    REQUIRE(loaded);
    node *loaded_real = graph_node_by_id(loaded, real->id);
    node *loaded_add = graph_node_by_id(loaded, add->id);
    REQUIRE(loaded_real && loaded_real->comment);
    REQUIRE(!strcmp(loaded_real->comment, "Tuned by hand") && loaded_real->comment_hidden);
    REQUIRE(loaded_add && !loaded_add->comment);
    char *again = graph_save(loaded);
    REQUIRE(again && !strcmp(document, again));
    free(again);
    free(document);
    graph_free(loaded);
    free(loaded);
    graph_free(&g);
    return true;
}

TEST_CASE(comment_lines_round_trip_through_asset_documents)
{
    behavior_asset *asset = behavior_asset_new("Lines");
    node *noted = graph_add_node(asset->graph, graph_palette_node("Add"));
    REQUIRE(graph_set_node_comment(asset->graph, noted, "First line\nSecond: line", false) == 0);
    char *document = graph_asset_text(asset);
    REQUIRE(document);
    behavior_asset *reread = graph_asset_read(document);
    REQUIRE(reread);
    node *renoted = graph_node_by_id(reread->graph, noted->id);
    REQUIRE(renoted && !strcmp(renoted->comment, "First line\nSecond: line"));
    char *resaved = graph_asset_text(reread);
    REQUIRE(resaved && !strcmp(document, resaved));
    free(resaved);
    free(document);
    behavior_asset_free(reread);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(non_string_comments_are_rejected)
{
    graph *bad = graph_load(
        "version 1\n"
        "id_counter 3\n"
        "(array)nodes {\n"
        "    - id=1 type=\"Real\" comment=7\n"
        "}\n"
    );
    REQUIRE(!bad);
    return true;
}

TEST_CASE(comment_frames_keep_title_color_and_bounds)
{
    graph g;
    graph_init(&g);
    graph_comment_box *box = graph_add_comment_box(&g, "Movement", 10, 20, 300, 200);
    REQUIRE(box && box->color == 0);
    char *plain = graph_save(&g);
    REQUIRE(plain && !strstr(plain, "color "));
    REQUIRE(graph_set_comment_box(&g, box, "Dash", 0x4a8fe0ffu) == 0);
    char *colored = graph_save(&g);
    REQUIRE(colored && strstr(colored, "0x4a8fe0ff"));
    graph *loaded = graph_load(colored);
    REQUIRE(loaded && acount(loaded->comment_boxes) == 1);
    const graph_comment_box *copy = &loaded->comment_boxes[0];
    REQUIRE(copy->color == 0x4a8fe0ffu && !strcmp(copy->title, "Dash"));
    REQUIRE(copy->width == 300 && copy->height == 200);
    free(plain);
    free(colored);
    graph_free(loaded);
    free(loaded);
    graph_free(&g);
    return true;
}

void test_graph_comments()
{
    graph_palette_open();
    RUN_TEST_CASE(node_comments_fold_save_and_reload);
    RUN_TEST_CASE(comment_lines_round_trip_through_asset_documents);
    RUN_TEST_CASE(non_string_comments_are_rejected);
    RUN_TEST_CASE(comment_frames_keep_title_color_and_bounds);
    graph_palette_close();
}
