#include "syntax_chibi.h"

#include "compiler_fixture.h"
#include "package_core_source.h"
#include "syntax_chibi_source.h"
#include "syntax_corpus.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"

#include <stdio.h>
#include <string.h>

const TSLanguage *tree_sitter_chibi();
/* Three cases share fourteen fixed source inputs and two independent edits.
 * Twenty external trees and twenty C oracle snapshots produce 40 authored
 * calls, each capped at one million instructions. Two retained package owners
 * prepare the actual diagnostic source and core strings once per backend.
 * Each diagnostic is compared after its producer activation and arenas retire.
 * No runtime filesystem, workers, services or generated inputs are involved. */
enum
{
    MAX_SOURCE = 2048,
    MAX_NODES = 1024,
    MAX_DEPTH = 128
};
static compiler_fixture_node nodes[MAX_NODES];
static int node_count, total_nodes, maximum_nodes, tree_count;
static int missing_nodes, error_descendants, hidden_diagnostics, utf8_byte_columns;
static compiler_fixture fixture;
static arena input, detached;
static TSParser *parser;
static TSTree *trees[3];
static script_syntax *expected[3];

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
    for (int i = 0; i < 3; i++) {
        ts_tree_delete(trees[i]);
        trees[i] = NULL;
        script_syntax_free(expected[i]);
        expected[i] = NULL;
    }
}
static bool prepare()
{
    parser = ts_parser_new();
    REQUIRE(parser && ts_parser_set_language(parser, tree_sitter_chibi()));
    source_package_input files[] = {
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
    };
    REQUIRE(files[0].size < 16384);
    return compiler_fixture_prepare(&fixture, files, 2);
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static bool read(const pin_value *row, const char *name, pin_value *value)
{
    REQUIRE(row->type == PIN_STRUCT && row->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, value));
    return true;
}
static bool same_int(const pin_value *row, const char *name, int expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_I32 && value.i == expected);
    return true;
}
static bool compare(int backend, const script_syntax *oracle)
{
    arena_free(&input);
    arena_free(&detached);
    pin_value arguments[2], owned;
    REQUIRE(
        compiler_fixture_pack_nodes(&fixture, backend, nodes, node_count, &input, &arguments[0], NULL)
    );
    arguments[1] = integer(0);
    REQUIRE(compiler_fixture_call(
        &fixture,
        backend,
        "project:syntax_diagnostics",
        "Diagnose",
        arguments,
        2,
        1000000,
        &detached,
        &owned
    ));
    arena_free(&input);
    REQUIRE(owned.type == PIN_STRUCT && owned.container == PIN_CONTAINER_ARRAY);
    const pin_array *array = owned.ptr;
    REQUIRE((array ? array->count : 0) == oracle->diagnostic_count);
    for (int i = 0; i < oracle->diagnostic_count; i++) {
        const pin_value *row = &array->items[i];
        const script_diagnostic *diagnostic = &oracle->diagnostics[i];
        REQUIRE(
            same_int(row, "start", diagnostic->range.start) &&
            same_int(row, "end", diagnostic->range.end)
        );
        REQUIRE(
            same_int(row, "line", diagnostic->range.line) &&
            same_int(row, "column", diagnostic->range.column) &&
            same_int(row, "document", diagnostic->range.document)
        );
        pin_value message;
        REQUIRE(
            read(row, "message", &message) && message.type == PIN_STR &&
            pin_text(message).bytes &&
            !strcmp(pin_text(message).bytes, diagnostic->message)
        );
    }
    arena_free(&detached);
    return true;
}
static bool diagnose(TSTree *tree, const script_syntax *oracle)
{
    node_count = 0;
    REQUIRE(tree && oracle && oracle->diagnostic_count >= 0 && oracle->diagnostic_count <= MAX_NODES);
    REQUIRE(
        compiler_fixture_nodes(ts_tree_root_node(tree), nodes, MAX_NODES, MAX_DEPTH, &node_count)
    );
    for (int i = 0; i < node_count; i++) {
        missing_nodes += ts_node_is_missing(nodes[i].node);
        error_descendants += ts_node_is_error(nodes[i].node) &&
                             nodes[i].first_child >= 0;
    }
    for (int i = 0; i < oracle->diagnostic_count; i++) {
        const script_diagnostic *diagnostic = &oracle->diagnostics[i];
        hidden_diagnostics += !strncmp(diagnostic->message, "Missing syntax inside ", 22);
        int start = diagnostic->range.start, line_start = start, continuation = 0;
        REQUIRE(start >= 0 && start <= (int)strlen(oracle->text));
        while (line_start > 0 && oracle->text[line_start - 1] != '\n') {
            line_start--;
        }
        for (int b = line_start; b < start; b++) {
            continuation += ((unsigned char)oracle->text[b] & 0xc0) == 0x80;
        }
        if (continuation) {
            REQUIRE(diagnostic->range.column == start - line_start + 1);
            REQUIRE(diagnostic->range.column > start - line_start + 1 - continuation);
            utf8_byte_columns++;
        }
    }
    total_nodes += node_count;
    maximum_nodes = node_count > maximum_nodes ? node_count : maximum_nodes;
    tree_count++;
    for (int b = 0; b < 2; b++) {
        REQUIRE(compare(b, oracle));
    }
    return true;
}
static TSTree *parse_raw(const char *text, const TSTree *previous)
{
    size_t size = strlen(text);
    if (size > MAX_SOURCE) {
        return NULL;
    }
    return ts_parser_parse_string(parser, previous, text, (uint32_t)size);
}
TEST_CASE(syntax_chibi_shared_corpus)
{
    REQUIRE(prepare());
    REQUIRE(SYNTAX_CORPUS_CASE_COUNT == 14 && SYNTAX_CORPUS_EDIT_COUNT == 2);
    for (int i = 0; i < SYNTAX_CORPUS_CASE_COUNT; i++) {
        cleanup();
        const syntax_corpus_case *item = &syntax_corpus_cases[i];
        REQUIRE(strlen(item->text) <= MAX_SOURCE);
        expected[0] = script_parse(item->text);
        trees[0] = parse_raw(item->text, NULL);
        REQUIRE(expected[0] && trees[0]);
        REQUIRE((expected[0]->diagnostic_count == 0) == item->valid);
        REQUIRE(diagnose(trees[0], expected[0]));
    }
    REQUIRE(missing_nodes > 0 && error_descendants > 0 && utf8_byte_columns > 0);
    return true;
}
static TSPoint point(TSPoint start, const char *bytes, int count)
{
    for (int i = 0; i < count; i++) {
        if (bytes[i] == '\n') {
            start.row++;
            start.column = 0;
        } else {
            start.column++;
        }
    }
    return start;
}
static bool incremental(int index)
{
    REQUIRE(
        fixture.owners[0] && fixture.owners[1] &&
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1]) && index >= 0 && index < SYNTAX_CORPUS_EDIT_COUNT
    );
    const syntax_corpus_edit *item = &syntax_corpus_edits[index];
    const char *before = syntax_corpus_cases[item->source].text;
    int added = (int)strlen(item->replacement);
    REQUIRE(strlen(before) <= MAX_SOURCE && strlen(item->after) <= MAX_SOURCE);
    REQUIRE(item->start >= 0 && item->end >= item->start && item->end <= (int)strlen(before));
    expected[0] = script_parse(before);
    REQUIRE(expected[0]);
    expected[1] = script_edit(expected[0], item->start, item->end, item->replacement);
    expected[2] = script_parse(item->after);
    REQUIRE(expected[0] && expected[1] && expected[2]);
    REQUIRE(!strcmp(expected[1]->text, item->after));
    trees[0] = parse_raw(before, NULL);
    REQUIRE(trees[0]);
    TSTree *previous = ts_tree_copy(trees[0]);
    TSPoint start = point((TSPoint){ 0 }, before, item->start);
    TSInputEdit edit = {
        .start_byte = (uint32_t)item->start,
        .old_end_byte = (uint32_t)item->end,
        .new_end_byte = (uint32_t)(item->start + added),
        .start_point = start,
        .old_end_point = point(start, before + item->start, item->end - item->start),
        .new_end_point = point(start, item->replacement, added),
    };
    ts_tree_edit(previous, &edit);
    trees[1] = parse_raw(item->after, previous);
    ts_tree_delete(previous);
    trees[2] = parse_raw(item->after, NULL);
    REQUIRE(trees[1] && trees[2]);
    /* The original generation remains usable after both independent edits. */
    REQUIRE(!strcmp(expected[0]->text, before));
    for (int i = 0; i < 3; i++) {
        REQUIRE(diagnose(trees[i], expected[i]));
    }
    return true;
}
TEST_CASE(syntax_chibi_incremental_utf8)
{
    return incremental(0);
}
TEST_CASE(syntax_chibi_incremental_recovery)
{
    return incremental(1);
}
void test_language_syntax_chibi()
{
    total_nodes = maximum_nodes = tree_count = 0;
    missing_nodes = error_descendants = hidden_diagnostics = utf8_byte_columns = 0;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(syntax_chibi_shared_corpus);
    RUN_TEST_CASE(syntax_chibi_incremental_utf8);
    RUN_TEST_CASE(syntax_chibi_incremental_recovery);
    pu_clear_setup();
    printf("Authored syntax: %d trees, %d raw nodes, maximum %d nodes/tree\n", tree_count, total_nodes, maximum_nodes);
    printf(
        "Raw branches: %d missing, %d ERROR with descendants; oracle "
        "witnesses: %d hidden recovery, %d UTF8 byte columns\n",
        missing_nodes,
        error_descendants,
        hidden_diagnostics,
        utf8_byte_columns
    );
    cleanup();
    compiler_fixture_free(&fixture);
    ts_parser_delete(parser);
    parser = NULL;
}
