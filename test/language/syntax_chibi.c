#include "syntax_chibi.h"

#include "authoring/native_bindings.h"
#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "execution/module.h"
#include "execution/status.h"
#include "package_core_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/text_definition.h"
#include "syntax_chibi_source.h"
#include "syntax_corpus.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"

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
typedef struct
{
    TSNode node;
    int parent, first_child, next_sibling;
} raw_node;
static raw_node nodes[MAX_NODES];
static int node_count, total_nodes, maximum_nodes, tree_count;
static int missing_nodes, error_descendants, hidden_diagnostics, utf8_byte_columns;
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static native_capabilities *native;
static authoring_packages *owners[2];
static const authoring_package *programs[2];
static arena input, producer, detached;
static behavior_function_context context;
static behavior_function_activation activation;
static pin_value output;
static TSParser *parser;
static TSTree *trees[3];
static script_syntax *expected[3];

static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&producer);
    arena_free(&input);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    output = (pin_value){ 0 };
}
static void cleanup()
{
    finish_call();
    arena_free(&detached);
    for (int i = 0; i < 3; i++) {
        ts_tree_delete(trees[i]);
        trees[i] = NULL;
        script_syntax_free(expected[i]);
        expected[i] = NULL;
    }
}
static uint64_t exported(int backend, const char *name)
{
    const script_package_exports *exports = programs[backend]->exports;
    for (int i = 0; i < exports->member_count; i++) {
        if (!strcmp(exports->members[i].name, name)) {
            return exports->members[i].id;
        }
    }
    return 0;
}
static bool prepare()
{
    native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                       NATIVE_ARRAY_DEFINITION_PACK };
    char message[192];
    native = native_capabilities_from_definition_packs(NULL, 0, packs, 2, message, sizeof message);
    REQUIRE(native);
    parser = ts_parser_new();
    REQUIRE(parser && ts_parser_set_language(parser, tree_sitter_chibi()));
    for (int b = 0; b < 2; b++) {
        source_package_input files[] = {
            { "syntax_diagnostics/Diagnostics.chibi",
              SYNTAX_CHIBI_SOURCE,
              sizeof SYNTAX_CHIBI_SOURCE - 1 },
            { "/sdk/core/strings/Strings.chibi",
              CORE_STRINGS_SOURCE,
              sizeof CORE_STRINGS_SOURCE - 1 },
        };
        REQUIRE(files[0].size < 16384 && files[1].size < 16384);
        source_package_collection collections[] = { { "core", "/sdk/core" } };
        source_package_snapshot_options options = { .project_root = "/compiler",
                                                    .files = files,
                                                    .file_count = 2,
                                                    .collections = collections,
                                                    .collection_count = 1 };
        source_package_snapshot *snapshot = source_package_snapshot_new(&options);
        REQUIRE(snapshot);
        const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
        if (error) {
            fprintf(
                stderr,
                "authored syntax: %d:%d: %s\n",
                error->diagnostic.range.line,
                error->diagnostic.range.column,
                error->diagnostic.message
            );
            source_package_snapshot_free(snapshot);
            REQUIRE(false);
        }
        script_lower_bindings bindings = authoring_native_bindings(native);
        owners[b] = authoring_packages_prepare(
            snapshot,
            native_capabilities_catalog(native),
            &bindings,
            backends[b],
            NULL
        );
        REQUIRE(owners[b]);
        for (int p = 0; p < authoring_packages_count(owners[b]); p++) {
            const authoring_package *package = authoring_packages_at(owners[b], p);
            for (int d = 0; d < package->diagnostic_count; d++) {
                fprintf(
                    stderr,
                    "%s:%d:%d: %s\n",
                    package->identity,
                    package->diagnostics[d].range.line,
                    package->diagnostics[d].range.column,
                    package->diagnostics[d].message
                );
            }
            if (!strcmp(package->identity, "project:syntax_diagnostics")) {
                programs[b] = package;
            }
        }
        REQUIRE(
            authoring_packages_valid(owners[b]) && programs[b] && programs[b]->module
        );
    }
    return true;
}
/* This traversal transports every public Tree-sitter child, including tokens.
 * No error selection, hidden-error calculation or diagnostic text occurs here. */
static bool collect(TSNode root)
{
    REQUIRE(ts_node_descendant_count(root) <= MAX_NODES);
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    int parent = -1, previous = -1, depth = 0;
    for (;;) {
        if (depth > MAX_DEPTH || node_count >= MAX_NODES) {
            ts_tree_cursor_delete(&cursor);
            REQUIRE(false);
        }
        int current = node_count++;
        TSNode node = ts_tree_cursor_current_node(&cursor);
        nodes[current] = (raw_node){ node, parent, -1, -1 };
        if (parent >= 0) {
            if (previous < 0) {
                nodes[parent].first_child = current;
            } else {
                nodes[previous].next_sibling = current;
            }
        }
        missing_nodes += ts_node_is_missing(node);
        error_descendants += ts_node_is_error(node) && ts_node_child_count(node) > 0;
        if (ts_tree_cursor_goto_first_child(&cursor)) {
            parent = current;
            previous = -1;
            depth++;
            continue;
        }
        for (;;) {
            if (ts_tree_cursor_goto_next_sibling(&cursor)) {
                previous = current;
                break;
            }
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                ts_tree_cursor_delete(&cursor);
                return true;
            }
            current = parent;
            parent = nodes[current].parent;
            depth--;
        }
    }
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static pin_value text(const char *value)
{
    return (pin_value){ .type = PIN_STR, .str = value };
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    pin_write_field(row->ptr, field, value);
    return true;
}
static bool pack(int backend, pin_value *argument)
{
    const script_catalog_type *type = script_catalog_type_by_id(
        programs[backend]->exports->metadata,
        exported(backend, "Node")
    );
    REQUIRE(type && type->authored_record && type->field_count == 11);
    type_field fields[11] = { 0 };
    for (int i = 0; i < 11; i++) {
        fields[i] = (type_field){ .name = type->fields[i].name,
                                  .type = type->fields[i].type.storage };
    }
    const pin_record *record = pin_record_create(&input, type->name, type->id, fields, 11);
    REQUIRE(record);
    pin_array
        *array = pin_array_snapshot_allocate(&input, PIN_STRUCT, record->type.name, node_count);
    REQUIRE(array);
    array->record = record;
    for (int i = 0; i < node_count; i++) {
        TSNode node = nodes[i].node;
        TSPoint point = ts_node_start_point(node);
        pin_value row = { .type = PIN_STRUCT,
                          .ptr = arena_alloc(&input, record->type.size),
                          .record = record,
                          .type_name = record->type.name };
        memset(row.ptr, 0, record->type.size);
        REQUIRE(put(&row, "kind", text(ts_node_type(node))));
        REQUIRE(
            put(&row, "start", integer((int)ts_node_start_byte(node))) &&
            put(&row, "end", integer((int)ts_node_end_byte(node)))
        );
        REQUIRE(
            put(&row, "row", integer((int)point.row)) &&
            put(&row, "column", integer((int)point.column))
        );
        REQUIRE(
            put(&row, "parent", integer(nodes[i].parent)) &&
            put(&row, "first_child", integer(nodes[i].first_child)) &&
            put(&row, "next_sibling", integer(nodes[i].next_sibling))
        );
        REQUIRE(
            put(&row, "error", boolean(ts_node_is_error(node))) &&
            put(&row, "missing", boolean(ts_node_is_missing(node))) &&
            put(&row, "has_error", boolean(ts_node_has_error(node)))
        );
        array->items[i] = row;
    }
    *argument = (pin_value){ .type = PIN_STRUCT,
                             .container = PIN_CONTAINER_ARRAY,
                             .ptr = array,
                             .record = record,
                             .type_name = record->type.name };
    return true;
}
static bool read(const pin_value *row, const char *name, pin_value *value)
{
    REQUIRE(row->type == PIN_STRUCT && row->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    pin_read_field(row->ptr, field, value);
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
    finish_call();
    arena_free(&detached);
    pin_value arguments[2];
    REQUIRE(pack(backend, &arguments[0]));
    arguments[1] = integer(0);
    execution_module *module = programs[backend]->module;
    const behavior_function *function = NULL;
    uint64_t id = exported(backend, "Diagnose");
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info info = execution_module_function_at(module, i);
        if (info.id == id) {
            function = info.body;
        }
    }
    REQUIRE(function);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    output = (pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_ARRAY };
    context = (behavior_function_context){
        .activation = behavior_function_resumable(function) ? &activation : NULL,
        .arguments = arguments,
        .argument_count = 2,
        .results = &output,
        .budget = 1000000
    };
    behavior_function_bind_storage(&context, &producer);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &context, backend != 0);
    behavior_run_scope_end(&runs);
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(backend == 0 ? runs.counts.bytecode_entries > 0 : !runs.counts.bytecode_entries);
    REQUIRE(backends[backend] != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
    REQUIRE(backends[backend] != BEHAVIOR_WASM || runs.counts.wasm_entries > 0);
    pin_value owned;
    REQUIRE(!pin_value_snapshot_to_arena(&detached, &owned, &output));
    finish_call();
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
            message.str && !strcmp(message.str, diagnostic->message)
        );
    }
    arena_free(&detached);
    return true;
}
static bool diagnose(TSTree *tree, const script_syntax *oracle)
{
    node_count = 0;
    REQUIRE(tree && oracle && oracle->diagnostic_count >= 0 && oracle->diagnostic_count <= MAX_NODES);
    REQUIRE(collect(ts_tree_root_node(tree)));
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
        programs[0] && programs[1] && programs[0]->module &&
        programs[1]->module && index >= 0 && index < SYNTAX_CORPUS_EDIT_COUNT
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
    for (int b = 0; b < 2; b++) {
        authoring_packages_free(owners[b]);
        owners[b] = NULL;
        programs[b] = NULL;
    }
    native_capabilities_free(native);
    native = NULL;
    ts_parser_delete(parser);
    parser = NULL;
}
