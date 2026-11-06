#include "package_syntax_chibi.h"

#include "compiler_fixture.h"
#include "package_core_source.h"
#include "package_syntax_chibi_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const TSLanguage *tree_sitter_chibi();
/* Existing eleven cases supply 31 parsed snapshots and one edited snapshot.
 * Each of the 32 independent raw external parses transports every child and
 * its incoming field name. Two owners prepare three authored sources/backend;
 * 64 calls have a one-million-instruction ceiling each. Inputs stay below
 * 256 bytes, 1024 nodes and depth128. No runtime I/O or generated test inputs.
 * C extraction remains the oracle, including its first syntax diagnostic.
 * Result values survive their producer activation and input arena retirement. */
enum
{
    MAX_SOURCE = 256,
    MAX_NODES = 1024,
    MAX_DEPTH = 128
};
static const char IDENTITY[] = "project:package_syntax";
static compiler_fixture fixture;
static bool prepared, attempted;
static TSParser *parser;
static TSTree *tree;
static compiler_fixture_node nodes[MAX_NODES];
static int attempts, calls, matches, total_nodes, maximum_nodes;
static arena input, detached;

static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value text(const char *value)
{
    value = value ? value : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, value, strlen(value));
    if (error) {
        abort();
    }
    return result;
}
static bool prepare()
{
    source_package_input files[] = {
        { "package_syntax/Extract.chibi",
          PACKAGE_SYNTAX_CHIBI_SOURCE,
          sizeof PACKAGE_SYNTAX_CHIBI_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
    };
    REQUIRE(compiler_fixture_prepare(&fixture, files, 3));
    parser = ts_parser_new();
    REQUIRE(parser && ts_parser_set_language(parser, tree_sitter_chibi()));
    prepared = true;
    return true;
}
static bool read(const pin_value *row, const char *name, pin_value *out)
{
    REQUIRE(row->type == PIN_STRUCT && row->container == PIN_CONTAINER_SINGLE && row->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, out));
    return true;
}
static bool same_int(const pin_value *row, const char *name, int expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value));
    REQUIRE(value.type == PIN_I32 && value.i == expected);
    return true;
}
static bool same_text(const pin_value *row, const char *name, const char *expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value));
    REQUIRE(
        value.type == PIN_STR && pin_text(value).bytes &&
        !strcmp(pin_text(value).bytes, expected)
    );
    REQUIRE(arena_contains(&detached, value.text));
    return true;
}
static bool same_range(const pin_value *row, const char *prefix, script_range range)
{
    const char *names[] = { "start", "end", "line", "column", "document" };
    const int values[] = { range.start,
                           range.end,
                           range.line,
                           range.column,
                           range.document };
    for (int i = 0; i < 5; i++) {
        char field[32];
        int size = snprintf(field, sizeof field, "%s%s%s", prefix, *prefix ? "_" : "", names[i]);
        REQUIRE(size > 0 && size < (int)sizeof field);
        REQUIRE(same_int(row, field, values[i]));
    }
    return true;
}
static bool shape(const pin_value *row, int fields)
{
    REQUIRE(row->type == PIN_STRUCT && row->container == PIN_CONTAINER_SINGLE && row->ptr);
    const type_desc *type = pin_value_type(row);
    REQUIRE(type && type->field_count == fields && row->record && pin_record_valid(row->record));
    REQUIRE(arena_contains(&detached, row->ptr) && arena_contains(&detached, row->record));
    return true;
}
static const script_catalog_variant *variant(int backend, const pin_value *value, const char *name)
{
    const authoring_package *program = compiler_fixture_package(&fixture, backend, IDENTITY);
    const script_catalog_type *type = script_catalog_type_by_id(
        program->exports->metadata,
        compiler_fixture_export(&fixture, backend, IDENTITY, "Result")
    );
    if (!type || value->type != PIN_UNION || !value->ptr) {
        return NULL;
    }
    const pin_union *payload = value->ptr;
    for (int i = 0; i < type->variant_count; i++) {
        if (type->variants[i].tag == payload->tag &&
            !strcmp(type->variants[i].name, name)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool compare_result(
    int backend,
    const pin_value *value,
    const source_package_syntax *oracle,
    const script_diagnostic *error,
    const char *path,
    const char *directory,
    int file
)
{
    REQUIRE(variant(backend, value, oracle ? "Accepted" : "Rejected"));
    const pin_union *payload = value->ptr;
    REQUIRE(arena_contains(&detached, payload));
    if (!oracle) {
        REQUIRE(payload->count == 1 && shape(&payload->values[0], 6));
        REQUIRE(same_range(&payload->values[0], "", error->range));
        REQUIRE(same_text(&payload->values[0], "message", error->message));
        return true;
    }
    REQUIRE(payload->count == 2 && !error->message[0]);
    const pin_value *header = &payload->values[0], *imports = &payload->values[1];
    REQUIRE(shape(header, 8));
    REQUIRE(same_text(header, "path", path) && same_text(header, "directory", directory));
    REQUIRE(same_text(header, "name", oracle->file.name));
    REQUIRE(same_range(header, "header", oracle->file.header));
    REQUIRE(imports->type == PIN_STRUCT && imports->container == PIN_CONTAINER_ARRAY);
    REQUIRE(
        imports->record && pin_record_valid(imports->record) &&
        imports->record->type.field_count == 18
    );
    REQUIRE(imports->record->id == compiler_fixture_export(&fixture, backend, IDENTITY, "Import"));
    const pin_array *array = imports->ptr;
    REQUIRE((array ? array->count : 0) == oracle->import_count);
    if (array) {
        REQUIRE(arena_contains(&detached, array) && array->record == imports->record);
    }
    for (int i = 0; i < oracle->import_count; i++) {
        const pin_value *row = &array->items[i];
        const source_package_import *item = &oracle->imports[i];
        REQUIRE(shape(row, 18));
        REQUIRE(same_int(row, "file", item->file) && item->file == file);
        REQUIRE(same_text(row, "alias", item->alias ? item->alias : ""));
        pin_value explicit_alias;
        REQUIRE(read(row, "explicit_alias", &explicit_alias));
        REQUIRE(explicit_alias.type == PIN_BOOL && explicit_alias.b == (item->alias != NULL));
        REQUIRE(!item->directory);
        REQUIRE(same_range(row, "range", item->range));
        REQUIRE(same_range(row, "path", oracle->locations[i].path));
        REQUIRE(same_range(row, "alias", oracle->locations[i].alias));
    }
    return true;
}
bool package_syntax_chibi_compare(
    const script_syntax *syntax,
    const source_package_syntax *oracle,
    const script_diagnostic *error,
    const char *path,
    const char *directory,
    int file
)
{
    if (!attempted) {
        attempted = true;
        REQUIRE(prepare());
    }
    REQUIRE(prepared);
    ts_tree_delete(tree);
    tree = NULL;
    size_t size = strlen(syntax->text);
    REQUIRE(size < MAX_SOURCE);
    tree = ts_parser_parse_string(parser, NULL, syntax->text, (uint32_t)size);
    REQUIRE(tree);
    int count = 0;
    REQUIRE(compiler_fixture_nodes(ts_tree_root_node(tree), nodes, MAX_NODES, MAX_DEPTH, &count));
    attempts++;
    total_nodes += count;
    maximum_nodes = count > maximum_nodes ? count : maximum_nodes;
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        pin_value arguments[7];
        REQUIRE(
            compiler_fixture_pack_nodes(&fixture, b, nodes, count, &input, &arguments[0], &arguments[1])
        );
        arguments[2] = text(syntax->text);
        arguments[3] = integer(0);
        arguments[4] = integer(file);
        arguments[5] = text(path);
        arguments[6] = text(directory);
        pin_value output = { 0 };
        calls++;
        REQUIRE(
            compiler_fixture_call(&fixture, b, IDENTITY, "Extract", arguments, 7, 1000000, &detached, &output)
        );
        arena_free(&input);
        REQUIRE(compare_result(b, &output, oracle, error, path, directory, file));
        matches++;
    }
    arena_free(&detached);
    ts_tree_delete(tree);
    tree = NULL;
    return true;
}
bool package_syntax_chibi_complete()
{
    REQUIRE(attempts == 32 && calls == 64 && matches == 64);
    return true;
}
void package_syntax_chibi_finish()
{
    ts_tree_delete(tree);
    tree = NULL;
    ts_parser_delete(parser);
    parser = NULL;
    arena_free(&input);
    arena_free(&detached);
    compiler_fixture_free(&fixture);
    fprintf(
        stderr,
        "package extraction: %d external trees, %d raw nodes, max %d "
        "nodes/tree, %d authored call attempts, %d matching results\n",
        attempts,
        total_nodes,
        maximum_nodes,
        calls,
        matches
    );
    attempts = calls = matches = total_nodes = maximum_nodes = 0;
    prepared = attempted = false;
}
