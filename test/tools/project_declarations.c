#include "assets/project_migrate.h"
#include "authoring/source_metadata.h"
#include "language/syntax.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>

/* Exact Studio inputs from 07dccf7b5fcfcc879b9d24e95730f4ee1d6a281f,
 * projects/studio/content/behaviors. Both sidecars are already stale: keep
 * their historical anchors and hashes, never manufacture a fresh binding.
 * The existing program migration case covers a valid bound sidecar.
 *
 * Success ceiling: six declaration migration calls, four retention/syntax
 * migration calls, three explicit current parses, four metadata checks and
 * two content transactions (four then five files).
 * Two additional in-memory call/unary controls total less than 1 KiB.
 * Including migration internals: at most 63 current/historical syntax parses
 * (twenty declaration, twelve retention/syntax, three explicit,
 * twelve successful-content and sixteen rejected-content).
 * No authored execution, backend, renderer, child process or extra corpus. */
enum
{
    SOURCE_COUNT = 2,
    FIXTURE_COUNT = 4,
    MAX_DIRECT_MIGRATIONS = 6,
    MAX_FOLLOWUP_MIGRATIONS = 4,
    MAX_CURRENT_PARSES = 3,
    MAX_METADATA_CHECKS = 4,
    MAX_TRANSACTIONS = 2,
    MAX_CONTENT_FILES = 5
};

static const char *const names[FIXTURE_COUNT] = { "OutlinerRows.chibi",
                                                  "VirtualList.chibi",
                                                  "OutlinerRows.chibi.meta",
                                                  "VirtualList.chibi.meta" };
static const char *const hashes[SOURCE_COUNT] = { "bda331caa1d8cfe6",
                                                  "c11a7c378a29d56c" };
static const int anchor_counts[SOURCE_COUNT] = { 104, 41 };
static const size_t fixture_bytes[FIXTURE_COUNT] = { 5025, 2122, 20968, 8335 };
static const char malformed[] = "behavior Broken { pure function ";
static const char
    legacy_calls[] = "library Calls {\n"
                     "    pure function Keep(value: bool) -> (Result: bool) {\n"
                     "        // not(Value: value) stays a comment.\n"
                     "        label := \"not(Value: value)\"\n"
                     "        a := not(Value: value)\n"
                     "        b := Tools.not(Value: value)\n"
                     "        c := \"not\"(Value: value)\n"
                     "        return !value\n"
                     "    }\n"
                     "}\n";
static const char
    expected_calls[] = "library Calls {\n"
                       "    Keep :: pure fn(value: bool) -> (Result: bool) {\n"
                       "        // not(Value: value) stays a comment.\n"
                       "        label := \"not(Value: value)\"\n"
                       "        a := \"not\"(Value: value)\n"
                       "        b := Tools.not(Value: value)\n"
                       "        c := \"not\"(Value: value)\n"
                       "        return !value\n"
                       "    }\n"
                       "}\n";
static const char current_unary[] = "Current :: library { Keep :: (value: "
                                    "bool) -> (Result: bool) { "
                                    "return not value } }";
_Static_assert(sizeof legacy_calls + sizeof current_unary <= 1024, "call/unary controls stay below 1 KiB");
static char directory[64];
static char *inputs[FIXTURE_COUNT], *migrated[SOURCE_COUNT], *again, *actual;
static char *retained[SOURCE_COUNT], *current[SOURCE_COUNT];
static script_syntax *syntax;
static int direct_migrations, followup_migrations, current_parses,
    metadata_checks, transactions;

static int occurrences(const char *text, const char *needle)
{
    int count = 0;
    for (const char *p = text; (p = strstr(p, needle)); p += strlen(needle)) {
        count++;
    }
    return count;
}

static TSNode first_node(TSNode node, const char *type)
{
    if (!strcmp(ts_node_type(node), type)) {
        return node;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode found = first_node(ts_node_named_child(node, i), type);
        if (!ts_node_is_null(found)) {
            return found;
        }
    }
    return (TSNode){ 0 };
}

static void path(char out[1024], const char *root, const char *name)
{
    int size = snprintf(out, 1024, "%s/%s", root, name);
    assert(size > 0 && size < 1024);
}

static void setup()
{
    strcpy(directory, "/tmp/chibi-studio-declarations-XXXXXX");
    assert(mkdtemp(directory));
    char root[1024], file[1024];
    path(root, tool_root(), "test/fixtures/studio-declarations");
    for (int i = 0; i < FIXTURE_COUNT; i++) {
        path(file, root, names[i]);
        inputs[i] = tool_read(file);
        assert(inputs[i] && strlen(inputs[i]) == fixture_bytes[i]);
    }
    direct_migrations = followup_migrations = current_parses = metadata_checks = transactions = 0;
}

static void cleanup()
{
    script_syntax_free(syntax);
    syntax = NULL;
    free(again);
    again = NULL;
    free(actual);
    actual = NULL;
    for (int i = 0; i < SOURCE_COUNT; i++) {
        free(migrated[i]);
        migrated[i] = NULL;
        free(retained[i]);
        retained[i] = NULL;
        free(current[i]);
        current[i] = NULL;
    }
    for (int i = 0; i < FIXTURE_COUNT; i++) {
        free(inputs[i]);
        inputs[i] = NULL;
    }
    tool_remove_tree(directory);
}

static bool stale_metadata(int source, const char *json, const char *text)
{
    assert(++metadata_checks <= MAX_METADATA_CHECKS);
    yyjson_doc *document = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(document);
    yyjson_val *anchors = yyjson_obj_get(root, "anchors");
    const char *hash = yyjson_get_str(yyjson_obj_get(root, "source_hash"));
    bool valid = yyjson_is_obj(root) && hash && !strcmp(hash, hashes[source]) &&
                 yyjson_is_arr(anchors) &&
                 yyjson_arr_size(anchors) == (size_t)anchor_counts[source];
    size_t i, count;
    yyjson_val *anchor;
    yyjson_arr_foreach(anchors, i, count, anchor)
    {
        valid &= yyjson_is_uint(yyjson_obj_get(anchor, "start")) &&
                 yyjson_is_uint(yyjson_obj_get(anchor, "id")) &&
                 yyjson_is_str(yyjson_obj_get(anchor, "kind"));
    }
    yyjson_doc_free(document);
    return valid && !authoring_source_metadata_matches(json, text);
}

static void write_inputs()
{
    char file[1024];
    for (int i = 0; i < FIXTURE_COUNT; i++) {
        path(file, directory, names[i]);
        tool_write(file, inputs[i]);
    }
}

static bool read_equals(const char *name, const char *expected)
{
    char file[1024];
    path(file, directory, name);
    free(actual);
    actual = tool_read(file);
    struct stat info;
    return actual && !stat(file, &info) &&
           info.st_size == (off_t)strlen(expected) && !strcmp(actual, expected);
}

/* The temporary content is flat and contains only the four exact fixtures,
 * plus the one malformed source in the rejection transaction. A staging file
 * or any other unexpected entry fails even when the originals still match. */
static bool content_files(int expected)
{
    assert(expected <= MAX_CONTENT_FILES);
    DIR *dir = opendir(directory);
    assert(dir);
    int count = 0;
    bool valid = true;
    for (struct dirent *entry; (entry = readdir(dir));) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        count++;
        bool known = expected == MAX_CONTENT_FILES &&
                     !strcmp(entry->d_name, "Broken.chibi");
        for (int i = 0; i < FIXTURE_COUNT; i++) {
            known |= !strcmp(entry->d_name, names[i]);
        }
        char file[1024];
        path(file, directory, entry->d_name);
        struct stat info;
        valid &= known && !strstr(entry->d_name, ".migrating") &&
                 !lstat(file, &info) && S_ISREG(info.st_mode);
    }
    assert(!closedir(dir));
    return valid && count == expected;
}

TEST_CASE(studio_declarations_migrate_and_reject_atomically)
{
    char error[1024] = { 0 };
    for (int i = 0; i < SOURCE_COUNT; i++) {
        fprintf(stderr, "Studio declaration migration: %s\n", names[i]);
        REQUIRE(stale_metadata(i, inputs[i + SOURCE_COUNT], inputs[i]));
        REQUIRE(++direct_migrations <= MAX_DIRECT_MIGRATIONS);
        int status = chibi_migrate_declarations(inputs[i], &migrated[i], error, sizeof error);
        if (status) {
            fprintf(stderr, "%s: %s\n", names[i], error);
        }
        REQUIRE(!status);
        REQUIRE(migrated[i] && strcmp(migrated[i], inputs[i]));
        if (i == 0) {
            REQUIRE(occurrences(inputs[i], "not(Value: state.Collapsed)") == 3);
            REQUIRE(occurrences(migrated[i], "\"not\"(Value: state.Collapsed)") == 3);
            REQUIRE(occurrences(inputs[i], "not(Value: rootclicked.Open)") == 1);
            REQUIRE(occurrences(migrated[i], "\"not\"(Value: rootclicked.Open)") == 1);
            REQUIRE(!strstr(migrated[i], "= not("));
        }
        /* Keep the v24 declaration assertions above and content transaction
         * below exact; only the current-parser check needs the later steps. */
        REQUIRE(++followup_migrations <= MAX_FOLLOWUP_MIGRATIONS);
        status = chibi_migrate_retention(migrated[i], &retained[i], error, sizeof error);
        if (status) {
            fprintf(stderr, "%s retention: %s\n", names[i], error);
        }
        REQUIRE(!status && retained[i]);
        REQUIRE(++followup_migrations <= MAX_FOLLOWUP_MIGRATIONS);
        status = chibi_migrate_syntax(retained[i], &current[i], error, sizeof error);
        if (status) {
            fprintf(stderr, "%s syntax: %s\n", names[i], error);
        }
        REQUIRE(!status && current[i]);
        REQUIRE(++current_parses <= MAX_CURRENT_PARSES);
        syntax = script_parse(current[i]);
        REQUIRE(!syntax->diagnostic_count);
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(++direct_migrations <= MAX_DIRECT_MIGRATIONS);
        REQUIRE(!chibi_migrate_declarations(migrated[i], &again, error, sizeof error));
        REQUIRE(!again);
    }

    write_inputs();
    REQUIRE(content_files(FIXTURE_COUNT));
    REQUIRE(++transactions <= MAX_TRANSACTIONS);
    int status = chibi_project_migrate_content(directory, NULL, 23, error, sizeof error);
    if (status) {
        fprintf(stderr, "%s\n", error);
    }
    REQUIRE(!status);
    for (int i = 0; i < SOURCE_COUNT; i++) {
        REQUIRE(read_equals(names[i], migrated[i]));
        REQUIRE(read_equals(names[i + SOURCE_COUNT], inputs[i + SOURCE_COUNT]));
        REQUIRE(stale_metadata(i, actual, migrated[i]));
    }
    REQUIRE(content_files(FIXTURE_COUNT));

    write_inputs();
    char broken[1024];
    path(broken, directory, "Broken.chibi");
    tool_write(broken, malformed);
    REQUIRE(content_files(MAX_CONTENT_FILES));
    REQUIRE(++transactions <= MAX_TRANSACTIONS);
    REQUIRE(chibi_project_migrate_content(directory, NULL, 23, error, sizeof error) < 0);
    REQUIRE(!strncmp(error, broken, strlen(broken)));
    REQUIRE(!strcmp(error + strlen(broken), ": Cannot migrate invalid version 23 source"));
    for (int i = 0; i < FIXTURE_COUNT; i++) {
        REQUIRE(read_equals(names[i], inputs[i]));
    }
    REQUIRE(read_equals("Broken.chibi", malformed));
    REQUIRE(content_files(MAX_CONTENT_FILES));

    REQUIRE(++direct_migrations <= MAX_DIRECT_MIGRATIONS);
    status = chibi_migrate_declarations(legacy_calls, &again, error, sizeof error);
    if (status) {
        fprintf(stderr, "Legacy not-call control: %s\n", error);
    }
    REQUIRE(!status);
    REQUIRE(again && !strcmp(again, expected_calls));
    free(again);
    again = NULL;
    REQUIRE(++direct_migrations <= MAX_DIRECT_MIGRATIONS);
    REQUIRE(!chibi_migrate_declarations(current_unary, &again, error, sizeof error));
    REQUIRE(!again);
    REQUIRE(++current_parses <= MAX_CURRENT_PARSES);
    syntax = script_parse(current_unary);
    REQUIRE(!syntax->diagnostic_count);
    TSNode root = ts_tree_root_node(syntax->tree);
    TSNode unary = first_node(root, "unary_expression");
    REQUIRE(!ts_node_is_null(unary));
    REQUIRE(!strcmp(ts_node_type(ts_node_child_by_field_name(unary, "operator", 8)), "not"));
    REQUIRE(!strcmp(ts_node_type(ts_node_child_by_field_name(unary, "operand", 7)), "identifier"));
    REQUIRE(ts_node_is_null(first_node(root, "call_expression")));
    script_syntax_free(syntax);
    syntax = NULL;
    REQUIRE(direct_migrations == MAX_DIRECT_MIGRATIONS);
    REQUIRE(followup_migrations == MAX_FOLLOWUP_MIGRATIONS);
    REQUIRE(current_parses == MAX_CURRENT_PARSES);
    REQUIRE(metadata_checks == MAX_METADATA_CHECKS);
    REQUIRE(transactions == MAX_TRANSACTIONS);
    return true;
}

void test_tool_project_declarations()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(studio_declarations_migrate_and_reject_atomically);
    pu_clear_setup();
}
