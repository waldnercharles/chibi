#define PICO_UNIT_IMPLEMENTATION
#include "pico_unit.h"
#include "sources.h"
#include <tree_sitter/api.h>

#include <stdio.h>
#include <string.h>

const TSLanguage *tree_sitter_chibi();

#define MAX_SOURCE_BYTES 32768
#define MAX_TREE_NODES 50000
#define MAX_DIAGNOSTICS 128

static bool inspect(int file, const char *source, size_t size)
{
    REQUIRE(size > 0 && size < MAX_SOURCE_BYTES);
    TSParser *parser = ts_parser_new();
    REQUIRE(parser);
    bool valid = ts_parser_set_language(parser, tree_sitter_chibi());
    if (!valid) {
        ts_parser_delete(parser);
        REQUIRE(valid);
    }
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)size);
    if (!tree) {
        ts_parser_delete(parser);
        REQUIRE(tree);
    }
    TSNode root = ts_tree_root_node(tree);
    uint32_t nodes = ts_node_descendant_count(root);
    bool bounded = nodes <= MAX_TREE_NODES;
    int errors = 0;
    if (bounded) {
        TSTreeCursor cursor = ts_tree_cursor_new(root);
        for (;;) {
            TSNode node = ts_tree_cursor_current_node(&cursor);
            bool error = ts_node_is_error(node), missing = ts_node_is_missing(node);
            bool hidden = ts_node_has_error(node);
            for (uint32_t i = 0; hidden && i < ts_node_child_count(node); i++) {
                hidden = !ts_node_has_error(ts_node_child(node, i));
            }
            if (error || missing || hidden) {
                TSPoint start = ts_node_start_point(node);
                if (errors < MAX_DIAGNOSTICS) {
                    fprintf(stderr, "source=%d error=%d missing=%d hidden=%d row=%u column=%u start=%u end=%u\n",
                            file, error, missing, hidden, start.row + 1, start.column + 1,
                            ts_node_start_byte(node), ts_node_end_byte(node));
                }
                errors++;
            }
            if (!error && ts_tree_cursor_goto_first_child(&cursor)) {
                continue;
            }
            while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
                if (!ts_tree_cursor_goto_parent(&cursor)) {
                    goto finished;
                }
            }
        }
    finished:
        ts_tree_cursor_delete(&cursor);
    }
    valid = bounded && !errors && !ts_node_has_error(root);
    printf("source=%d bytes=%zu nodes=%u errors=%d valid=%d\n", file, size, nodes, errors, valid);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    REQUIRE(valid);
    return true;
}

TEST_CASE(compiler_complete_source_00_syntax)
{
    return inspect(0, source_0, sizeof source_0 - 1);
}
TEST_CASE(compiler_complete_source_01_syntax)
{
    return inspect(1, source_1, sizeof source_1 - 1);
}
TEST_CASE(compiler_complete_source_02_syntax)
{
    return inspect(2, source_2, sizeof source_2 - 1);
}
TEST_CASE(compiler_complete_source_03_syntax)
{
    return inspect(3, source_3, sizeof source_3 - 1);
}
TEST_CASE(compiler_complete_source_04_syntax)
{
    return inspect(4, source_4, sizeof source_4 - 1);
}
TEST_CASE(compiler_complete_source_05_syntax)
{
    return inspect(5, source_5, sizeof source_5 - 1);
}
TEST_CASE(compiler_complete_source_06_syntax)
{
    return inspect(6, source_6, sizeof source_6 - 1);
}
TEST_CASE(compiler_complete_source_07_syntax)
{
    return inspect(7, source_7, sizeof source_7 - 1);
}
TEST_CASE(compiler_complete_source_08_syntax)
{
    return inspect(8, source_8, sizeof source_8 - 1);
}
TEST_CASE(compiler_complete_source_09_syntax)
{
    return inspect(9, source_9, sizeof source_9 - 1);
}
TEST_CASE(compiler_complete_source_10_syntax)
{
    return inspect(10, source_10, sizeof source_10 - 1);
}
TEST_CASE(compiler_complete_source_11_syntax)
{
    return inspect(11, source_11, sizeof source_11 - 1);
}
TEST_CASE(compiler_complete_source_12_syntax)
{
    return inspect(12, source_12, sizeof source_12 - 1);
}
TEST_CASE(compiler_complete_source_13_syntax)
{
    return inspect(13, source_13, sizeof source_13 - 1);
}
TEST_CASE(compiler_complete_source_14_syntax)
{
    return inspect(14, source_14, sizeof source_14 - 1);
}
TEST_CASE(compiler_complete_source_15_syntax)
{
    return inspect(15, source_15, sizeof source_15 - 1);
}
TEST_CASE(compiler_complete_source_16_syntax)
{
    return inspect(16, source_16, sizeof source_16 - 1);
}
TEST_CASE(compiler_complete_source_17_syntax)
{
    return inspect(17, source_17, sizeof source_17 - 1);
}
int main(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "--bounded-compiler-source-syntax")) {
        return 2;
    }
    RUN_TEST_CASE(compiler_complete_source_00_syntax);
    RUN_TEST_CASE(compiler_complete_source_01_syntax);
    RUN_TEST_CASE(compiler_complete_source_02_syntax);
    RUN_TEST_CASE(compiler_complete_source_03_syntax);
    RUN_TEST_CASE(compiler_complete_source_04_syntax);
    RUN_TEST_CASE(compiler_complete_source_05_syntax);
    RUN_TEST_CASE(compiler_complete_source_06_syntax);
    RUN_TEST_CASE(compiler_complete_source_07_syntax);
    RUN_TEST_CASE(compiler_complete_source_08_syntax);
    RUN_TEST_CASE(compiler_complete_source_09_syntax);
    RUN_TEST_CASE(compiler_complete_source_10_syntax);
    RUN_TEST_CASE(compiler_complete_source_11_syntax);
    RUN_TEST_CASE(compiler_complete_source_12_syntax);
    RUN_TEST_CASE(compiler_complete_source_13_syntax);
    RUN_TEST_CASE(compiler_complete_source_14_syntax);
    RUN_TEST_CASE(compiler_complete_source_15_syntax);
    RUN_TEST_CASE(compiler_complete_source_16_syntax);
    RUN_TEST_CASE(compiler_complete_source_17_syntax);
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
