#include "support/asset_text.h"
#include "test/suite_internal.h"

/* Node comments and comment frames: authored, round-tripped, and omitted from
   documents while unused. */
static void check_node_comments()
{
    graph g;
    graph_init(&g);
    node *real = graph_add_node(&g, registry_node_by_name("Real"));
    node *add = graph_add_node(&g, registry_node_by_name("Add"));
    graph_connect(&g, real, 0, add, 0);
    test_check(graph_node_is_inline(&g, real), "comments: an uncommented literal folds into its consumer");
    test_check(graph_set_node_comment(&g, real, "Tuned by hand", true) == 0, "comments: a node takes a comment");
    test_check(!graph_node_is_inline(&g, real), "comments: a commented node keeps its own box");
    test_check(
        graph_set_node_comment(&g, add, "", true) == 0 && !add->comment &&
            !add->comment_hidden,
        "comments: an empty comment removes it and its hidden flag"
    );

    char *json = graph_save(&g);
    test_check(
        json && strstr(json, "comment=\"Tuned by hand\"") &&
            strstr(json, "comment_hidden=#true"),
        "comments: documents keep a comment and its hidden flag"
    );
    graph *loaded = json ? graph_load(json) : NULL;
    node *loaded_real = loaded ? graph_node_by_id(loaded, real->id) : NULL;
    node *loaded_add = loaded ? graph_node_by_id(loaded, add->id) : NULL;
    test_check(
        loaded_real && loaded_real->comment &&
            !strcmp(loaded_real->comment, "Tuned by hand") &&
            loaded_real->comment_hidden && loaded_add && !loaded_add->comment,
        "comments: loading restores node comments"
    );
    char *again = loaded ? graph_save(loaded) : NULL;
    test_check(json && again && !strcmp(json, again), "comments: node comments round-trip byte for byte");
    free(again);
    free(json);
    if (loaded) {
        graph_free(loaded);
        free(loaded);
    }
    graph_free(&g);

    /* A comment keeps its lines through the asset file. */
    behavior_asset *asset = behavior_asset_new("Lines");
    node *noted = graph_add_node(asset->graph, registry_node_by_name("Add"));
    graph_set_node_comment(asset->graph, noted, "First line\nSecond: line", false);
    char *text = behavior_asset_save(asset);
    char *code = text ? strdup(text) : NULL;
    behavior_asset *reread = code ? behavior_asset_load(code) : NULL;
    char *resaved = reread ? behavior_asset_save(reread) : NULL;
    char *reyaml = resaved ? strdup(resaved) : NULL;
    node *renoted = reread ? graph_node_by_id(reread->graph, noted->id) : NULL;
    test_check(
        renoted && !strcmp(renoted->comment, "First line\nSecond: line") &&
            reyaml && !strcmp(code, reyaml),
        "comments: comment lines round-trip through the asset file"
    );
    free(text);
    free(code);
    free(resaved);
    free(reyaml);
    behavior_asset_free(reread);
    behavior_asset_free(asset);

    graph *bad = graph_load(
        "version 1\n"
        "id_counter 3\n"
        "(array)nodes {\n"
        "    - id=1 type=\"Real\" comment=7\n"
        "}\n"
    );
    test_check(!bad, "comments: a comment that is not a string is rejected");
}

static void check_comment_frames()
{
    graph g;
    graph_init(&g);
    graph_comment_box *box = graph_add_comment_box(&g, "Movement", 10, 20, 300, 200);
    test_check(box && box->color == 0, "frames: a new frame takes the canvas's comment color");
    char *plain = graph_save(&g);
    test_check(plain && !strstr(plain, "color "), "frames: documents omit the default color");
    test_check(graph_set_comment_box(&g, box, "Dash", 0x4a8fe0ffu) == 0, "frames: a frame takes a title and color");
    char *colored = graph_save(&g);
    test_check(colored && strstr(colored, "0x4a8fe0ff"), "frames: packed colors use readable RGBA hex");
    graph *loaded = colored ? graph_load(colored) : NULL;
    const graph_comment_box *copy = loaded && acount(loaded->comment_boxes) == 1
                                      ? &loaded->comment_boxes[0]
                                      : NULL;
    test_check(
        copy && copy->color == 0x4a8fe0ffu && !strcmp(copy->title, "Dash") &&
            copy->width == 300 && copy->height == 200,
        "frames: title, color and bounds round-trip"
    );
    free(plain);
    free(colored);
    if (loaded) {
        graph_free(loaded);
        free(loaded);
    }
    graph_free(&g);
}

void run_graph_comment_checks()
{
    check_node_comments();
    check_comment_frames();
}
