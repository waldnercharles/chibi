#include "authoring/source_package_io.h"
#include "compiler_cache.h"
#include "execution/function.h"
#include "execution/instruction.h"
#include "execution/prepared.h"
#include "package_compiler_chibi_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/native_definitions.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_bootstrap.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "toolchain/compiler_target.h"
#include "types/array.h"
#include "types/map.h"
#include "types/text.h"

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>
#include <yyjson.h>

const TSLanguage *tree_sitter_chibi(void);

/* W21's census: the first feature that keeps each function of the
 * self-hosted compiler from the native path. Every function of the bootstrap
 * closure becomes one single-package source, the scalar producer
 * (CollectScalar) runs over it on SLJIT, and its first rejection is grouped
 * by the work package that removes it.
 *
 * The producer takes one package without imports or constants, so the
 * census flattens both: a function's source holds a first entry function,
 * the function, and the records and unions it names, from any package, with
 * names from other packages qualified as `<package>__<Name>` and each
 * constant written as its value. A function it calls joins the source when
 * the producer reaches a call to it, so a later rejection inside that
 * function blocks the census function through it. */

enum
{
    CENSUS_BUDGET = 20000000,
    CENSUS_EXCERPT = 60
};
enum
{
    DECL_FUNCTION,
    DECL_TYPE,
    DECL_CONSTANT
};
static const char
    COLLECT_SCALAR[] = "project:package_compiler:1:CollectScalar:callable";
/* The producer's own rejections and the ordinary compiler's rejection of a
 * call it cannot resolve. */
static const char PRODUCER[] = "scalar proof ";
static const char UNKNOWN[] = "Unknown function '";
enum
{
    REJECTION_ROW = 1 /* scalar_facts.RejectionRow */
};
static const char ENTRY[] = "NativeCensusEntry";
static const source_package_input SOURCES[] = { PACKAGE_COMPILER_SOURCE_INPUTS };
enum
{
    SOURCE_COUNT = (int)(sizeof SOURCES / sizeof *SOURCES)
};
const TSLanguage *tree_sitter_chibi();

typedef struct
{
    char *alias;
    int package;
} census_alias;
typedef struct
{
    TSTree *tree;
    int package;
    census_alias *aliases;
} census_source;
typedef struct
{
    int source, kind, package;
    TSNode node;
    char *name;
} census_decl;
/* A reference to declaration `decl` that replaces source bytes [start, end). */
typedef struct
{
    uint32_t start, end;
    int decl;
} census_edit;
/* Where a function landed in a census source: its text and its parameter
 * and result lists. */
typedef struct
{
    int decl;
    uint32_t start, end, parameters, results;
    bool result;
} census_place;
/* One census source and where its functions and references landed. */
typedef struct
{
    char *text;
    int *types;              /* Records and unions to add. */
    census_place *functions; /* The census function, then those it calls. */
    census_edit *calls;      /* References to functions, in text bytes. */
    bool imports, constants;
} census_build;
/* What keeps a function from the native path, and the work package that
 * removes it. */
enum
{
    CENSUS_NATIVE,
    CENSUS_ARRAYS,
    CENSUS_STRINGS,
    CENSUS_MAPS,
    CENSUS_POINTERS,
    CENSUS_RESULTS,
    CENSUS_NO_RESULT,
    CENSUS_MANAGED_FIELDS,
    CENSUS_TEXT_NATIVES,
    CENSUS_FORMAT,
    CENSUS_NATIVES,
    CENSUS_OTHER,
    CENSUS_CATEGORIES
};
static const struct
{
    const char *package, *feature;
} CATEGORIES[CENSUS_CATEGORIES] = {
    [CENSUS_NATIVE] = { "none", "passes the producer" },
    [CENSUS_ARRAYS] = { "W7", "array parameters, results and natives" },
    [CENSUS_STRINGS] = { "W3", "string parameters and results" },
    [CENSUS_MAPS] = { "W8", "map parameters and results" },
    [CENSUS_POINTERS] = { "W10", "pointer and reference parameters and results" },
    [CENSUS_RESULTS] = { "W2", "multiple and named results" },
    [CENSUS_NO_RESULT] = { "W2", "functions without a result" },
    [CENSUS_MANAGED_FIELDS] = { "W9", "strings and arrays in records and unions" },
    [CENSUS_TEXT_NATIVES] = { "W4", "text natives" },
    [CENSUS_FORMAT] = { "W6", "`format_text`" },
    [CENSUS_NATIVES] = { "W14", "other native catalog calls" },
    [CENSUS_OTHER] = { "none yet", "other forms; see each message" },
};
/* The array natives of the compiler host's catalog. */
static const char *const ARRAY_NATIVES[] = { "make_array",     "get",
                                             "length",         "last_index",
                                             "is_empty",       "is_not_empty",
                                             "is_valid_index", "contains",
                                             "find",           "identical",
                                             "append",         "add_unique",
                                             "extend",         "set_array_elem",
                                             "insert",         "remove_index",
                                             "remove_item",    "clear",
                                             "reverse",        "swap",
                                             "resize",         "slice" };
/* Where a rejection lies in its function. */
enum
{
    SPAN_TYPE, /* A record or union the function names. */
    SPAN_PARAMETER,
    SPAN_RESULTS,
    SPAN_FUNCTION,
    SPAN_BODY
};
typedef struct
{
    const char *function, *path, *callee;
    char *message, *excerpt;
    int category;
    bool imports, constants;
} census_row;

static compiler_host *compiler;
static native_capabilities *native; /* What the census sources may call. */
static char *target_json;           /* The JSON array of its target parts. */
static size_t target_length;
static TSParser *parser;
static const source_package_input *files; /* The sources the census reads. */
static census_source *sources;
static char **packages; /* The directory that names each package. */
static census_decl *decls;
static census_row *rows;

/* Forgets the sources the census read. */
static void unload()
{
    for (int i = 0; i < acount(sources); i++) {
        ts_tree_delete(sources[i].tree);
        for (int a = 0; a < acount(sources[i].aliases); a++) {
            free(sources[i].aliases[a].alias);
        }
        afree(sources[i].aliases);
    }
    afree(sources);
    for (int i = 0; i < acount(packages); i++) {
        free(packages[i]);
    }
    afree(packages);
    for (int i = 0; i < acount(decls); i++) {
        free(decls[i].name);
    }
    afree(decls);
    files = NULL;
}
static void forget_rows()
{
    for (int i = 0; i < acount(rows); i++) {
        free(rows[i].message);
        free(rows[i].excerpt);
    }
    afree(rows);
}
static void cleanup()
{
    unload();
    compiler_host_free(compiler);
    compiler = NULL;
    native_capabilities_free(native);
    native = NULL;
    free(target_json);
    target_json = NULL;
    forget_rows();
    ts_parser_delete(parser);
    parser = NULL;
}
static char *node_text(int source, TSNode node)
{
    uint32_t start = ts_node_start_byte(node), end = ts_node_end_byte(node);
    char *text = xmalloc(end - start + 1);
    memcpy(text, files[source].text + start, end - start);
    text[end - start] = 0;
    return text;
}
static bool node_is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static bool text_is(int source, TSNode node, const char *text)
{
    uint32_t start = ts_node_start_byte(node), length = ts_node_end_byte(node) - start;
    return strlen(text) == length && !memcmp(files[source].text + start, text, length);
}
/* The directory of a source path, which identifies its package. */
static char *directory(const char *path)
{
    const char *slash = strrchr(path, '/');
    assert(slash);
    char *text = xmalloc((size_t)(slash - path) + 2);
    memcpy(text, path, (size_t)(slash - path) + 1);
    text[slash - path + 1] = 0;
    return text;
}
static int package_of(const char *dir)
{
    for (int i = 0; i < acount(packages); i++) {
        if (!strcmp(packages[i], dir)) {
            return i;
        }
    }
    apush(packages, xstrdup(dir));
    return acount(packages) - 1;
}
static bool starts(const char *text, const char *prefix)
{
    return !strncmp(text, prefix, strlen(prefix));
}
/* The package an import path names: `project:x` is the folder x and
 * `core:x` the core collection's; -1 for another collection. */
static int imported_package(const char *path)
{
    char dir[256];
    if (starts(path, "project:")) {
        snprintf(dir, sizeof dir, "%s/", path + 8);
        return package_of(dir);
    }
    if (starts(path, "core:")) {
        snprintf(dir, sizeof dir, "/sdk/core/%s/", path + 5);
        return package_of(dir);
    }
    return -1;
}
/* A package's name in qualified census names: its folder's last part. */
static const char *package_name(int package)
{
    const char *dir = packages[package];
    const char *end = dir + strlen(dir) - 1;
    const char *start = end;
    while (start > dir && start[-1] != '/') {
        start--;
    }
    static char name[128];
    snprintf(name, sizeof name, "%.*s", (int)(end - start), start);
    return name;
}
static int find_decl(int package, int source, TSNode name)
{
    for (int i = 0; i < acount(decls); i++) {
        if (decls[i].package == package && text_is(source, name, decls[i].name)) {
            return i;
        }
    }
    return -1;
}
static int find_alias(int source, TSNode name)
{
    for (int a = 0; a < acount(sources[source].aliases); a++) {
        if (text_is(source, name, sources[source].aliases[a].alias)) {
            return sources[source].aliases[a].package;
        }
    }
    return -1;
}
static bool read_source(int source)
{
    const source_package_input *file = &files[source];
    TSTree
        *tree = ts_parser_parse_string(parser, NULL, file->text, (uint32_t)file->size);
    apush(sources, (census_source){ .tree = tree });
    TSNode root = ts_tree_root_node(tree);
    REQUIRE(!ts_node_has_error(root));
    char *dir = directory(file->path);
    sources[source].package = package_of(dir);
    free(dir);
    for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
        TSNode member = ts_node_named_child(root, i);
        const char *type = ts_node_type(member);
        if (!strcmp(type, "import_declaration")) {
            TSNode alias = ts_node_child_by_field_name(member, "alias", 5);
            TSNode path = ts_node_child_by_field_name(member, "path", 4);
            REQUIRE(!ts_node_is_null(alias));
            char *quoted = node_text(source, path);
            quoted[strlen(quoted) - 1] = 0;
            int package = imported_package(quoted + 1);
            free(quoted);
            REQUIRE(package >= 0);
            apush(sources[source].aliases, (census_alias){ node_text(source, alias), package });
            continue;
        }
        int kind = !strcmp(type, "function_declaration") ? DECL_FUNCTION
                 : !strcmp(type, "constant_declaration") ? DECL_CONSTANT
                 : !strcmp(type, "record_declaration") ||
                           !strcmp(type, "union_declaration")
                     ? DECL_TYPE
                     : -1;
        if (kind < 0) {
            REQUIRE(!strcmp(type, "package_header") || !strcmp(type, "comment"));
            continue;
        }
        TSNode name = ts_node_child_by_field_name(member, "name", 4);
        apush(
            decls,
            (census_decl){ .source = source,
                           .kind = kind,
                           .package = sources[source].package,
                           .node = member,
                           .name = node_text(source, name) }
        );
    }
    return true;
}
/* The names in scope that shadow package names: parameters, locals, loop
 * variables and pattern bindings. Each takes effect after its declaration, so
 * a local's initializer still names the package's declaration, and ends with
 * the block, loop or switch arm that holds it. A condition's pattern binds
 * through its `if` or `while` body, but not an `else`, as the analyzer's
 * statement scopes do. */
static char **bound;

static bool is_bound(int source, TSNode name)
{
    for (int i = 0; i < acount(bound); i++) {
        if (text_is(source, name, bound[i])) {
            return true;
        }
    }
    return false;
}
static void bind(int source, TSNode node, const char *field)
{
    TSNode name = ts_node_child_by_field_name(node, field, (uint32_t)strlen(field));
    if (!ts_node_is_null(name)) {
        apush(bound, node_text(source, name));
    }
}
static void unbind(int depth)
{
    while (acount(bound) > depth) {
        free(apop(bound));
    }
}
static bool scopes(TSNode node)
{
    return node_is(node, "block") || node_is(node, "for_statement") ||
           node_is(node, "if_statement") || node_is(node, "while_statement") ||
           node_is(node, "switch_arm");
}
/* The declaration `node` names from its place in `source`, or -1. */
static int reference(int source, TSNode node, const char *field)
{
    const char *type = ts_node_type(node);
    if (!strcmp(type, "member_expression")) {
        TSNode object = ts_node_child_by_field_name(node, "object", 6);
        int package = node_is(object, "identifier") && !is_bound(source, object)
                        ? find_alias(source, object)
                        : -1;
        return package < 0
                 ? -1
                 : find_decl(package, source, ts_node_child_by_field_name(node, "member", 6));
    }
    if (!strcmp(type, "type_name")) {
        uint32_t parts = ts_node_named_child_count(node);
        if (parts == 2) {
            int package = find_alias(source, ts_node_named_child(node, 0));
            return package < 0
                     ? -1
                     : find_decl(package, source, ts_node_named_child(node, 1));
        }
        return parts == 1
                 ? find_decl(sources[source].package, source, ts_node_named_child(node, 0))
                 : -1;
    }
    TSNode parent = ts_node_parent(node);
    if (strcmp(type, "identifier") || node_is(parent, "pattern_bindings") ||
        node_is(parent, "argument_name") ||
        (field && (!strcmp(field, "name") || !strcmp(field, "member") ||
                   !strcmp(field, "key") || !strcmp(field, "binding")))) {
        return -1;
    }
    return is_bound(source, node)
             ? -1
             : find_decl(sources[source].package, source, node);
}
/* The references under `node`, in source order, with the names in scope at
 * each. A qualified name is one reference; its parts are not visited. */
static void references(int source, TSNode node, const char *field, census_edit **edits)
{
    int decl = reference(source, node, field);
    if (decl >= 0) {
        apush(*edits, (census_edit){ ts_node_start_byte(node), ts_node_end_byte(node), decl });
        return;
    }
    if (node_is(node, "type_name")) {
        return;
    }
    if (node_is(node, "identifier") && (node_is(ts_node_parent(node), "pattern_bindings") ||
                                        (field && !strcmp(field, "binding")))) {
        apush(bound, node_text(source, node));
        return;
    }
    int depth = acount(bound);
    TSTreeCursor cursor = ts_tree_cursor_new(node);
    if (ts_tree_cursor_goto_first_child(&cursor)) {
        do {
            const char *name = ts_tree_cursor_current_field_name(&cursor);
            if (node_is(node, "for_statement") && name && !strcmp(name, "body")) {
                bind(source, node, "key");
                bind(source, node, "name");
            }
            if (node_is(node, "if_statement") && name && !strcmp(name, "alternative")) {
                unbind(depth);
            }
            references(source, ts_tree_cursor_current_node(&cursor), name, edits);
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }
    ts_tree_cursor_delete(&cursor);
    if (scopes(node)) {
        unbind(depth);
    }
    if (node_is(node, "parameter") || node_is(node, "local_declaration")) {
        bind(source, node, "name");
    }
}
static void append(char **text, const char *bytes, size_t length)
{
    int at = acount(*text);
    asetlen(*text, at + (int)length);
    memcpy(*text + at, bytes, length);
}
static void append_name(char **text, int package, int decl)
{
    if (decls[decl].package != package) {
        const char *name = package_name(decls[decl].package);
        append(text, name, strlen(name));
        append(text, "__", 2);
    }
    append(text, decls[decl].name, strlen(decls[decl].name));
}
static bool listed(const int *items, int item)
{
    for (int i = 0; i < acount(items); i++) {
        if (items[i] == item) {
            return true;
        }
    }
    return false;
}
static TSNode field_of(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static void emit_node(census_build *build, int package, int source, TSNode node);
/* Writes a reference to `decl` as the census source names it: a function,
 * record or union by its census name, a constant as its value. */
static void emit_reference(census_build *build, int package, int decl)
{
    build->imports |= decls[decl].package != package;
    if (decls[decl].kind == DECL_CONSTANT) {
        build->constants = true;
        TSNode type = field_of(decls[decl].node, "type");
        if (!ts_node_is_null(type)) {
            emit_node(build, package, decls[decl].source, type);
        }
        append(&build->text, "(", 1);
        emit_node(build, package, decls[decl].source, field_of(decls[decl].node, "value"));
        append(&build->text, ")", 1);
        return;
    }
    uint32_t start = (uint32_t)acount(build->text);
    append_name(&build->text, package, decl);
    if (decls[decl].kind == DECL_FUNCTION) {
        apush(build->calls, (census_edit){ start, (uint32_t)acount(build->text), decl });
    } else if (!listed(build->types, decl)) {
        apush(build->types, decl);
    }
}
/* Appends bytes [from, to) of `source`, writing the references of `edits`
 * from `*next` on that start before `to`. */
static void emit_range(
    census_build *build,
    int package,
    int source,
    uint32_t from,
    uint32_t to,
    const census_edit *edits,
    int *next
)
{
    const char *text = files[source].text;
    for (; *next < acount(edits) && edits[*next].start < to; (*next)++) {
        append(&build->text, text + from, edits[*next].start - from);
        emit_reference(build, package, edits[*next].decl);
        from = edits[*next].end;
    }
    append(&build->text, text + from, to - from);
}
static void emit_node(census_build *build, int package, int source, TSNode node)
{
    census_edit *edits = NULL;
    references(source, node, NULL, &edits);
    int next = 0;
    emit_range(build, package, source, ts_node_start_byte(node), ts_node_end_byte(node), edits, &next);
    afree(edits);
}
/* Appends declaration `decl` under its census name. For a function, records
 * where it, its parameters and its results land; without results, `results`
 * is where its body starts. */
static void emit_decl(census_build *build, int package, int decl, census_place *place)
{
    const census_decl *d = &decls[decl];
    TSNode name = field_of(d->node, "name");
    census_edit *edits = NULL;
    references(d->source, d->node, NULL, &edits);
    unbind(0);
    afree(bound);
    uint32_t start = (uint32_t)acount(build->text);
    int next = 0;
    emit_range(build, package, d->source, ts_node_start_byte(d->node), ts_node_start_byte(name), edits, &next);
    append_name(&build->text, package, decl);
    uint32_t from = ts_node_end_byte(name);
    if (place) {
        TSNode results = field_of(d->node, "results");
        TSNode after = ts_node_is_null(results) ? field_of(d->node, "body") : results;
        uint32_t cuts[] = { ts_node_start_byte(field_of(d->node, "parameters")),
                            ts_node_start_byte(after) };
        uint32_t *marks[] = { &place->parameters, &place->results };
        *place = (census_place){ .decl = decl,
                                 .start = start,
                                 .result = !ts_node_is_null(results) };
        for (int i = 0; i < 2; i++) {
            emit_range(build, package, d->source, from, cuts[i], edits, &next);
            *marks[i] = (uint32_t)acount(build->text);
            from = cuts[i];
        }
    }
    emit_range(build, package, d->source, from, ts_node_end_byte(d->node), edits, &next);
    if (place) {
        place->end = (uint32_t)acount(build->text);
    }
    append(&build->text, "\n", 1);
    afree(edits);
}
/* The census source of `functions`, the census function first: an entry
 * function, the functions, and the records and unions they name. */
static void build_source(census_build *build, const int *functions)
{
    int package = decls[functions[0]].package;
    char header[256];
    int length = snprintf(
        header,
        sizeof header,
        "package %s\n%s :: () -> int { return 0; }\n",
        package_name(package),
        ENTRY
    );
    append(&build->text, header, (size_t)length);
    for (int i = 0; i < acount(functions); i++) {
        census_place place = { 0 };
        emit_decl(build, package, functions[i], &place);
        apush(build->functions, place);
    }
    for (int i = 0; i < acount(build->types); i++) {
        emit_decl(build, package, build->types[i], NULL);
    }
    apush(build->text, 0);
}
static void build_free(census_build *build)
{
    afree(build->text);
    afree(build->types);
    afree(build->functions);
    afree(build->calls);
    *build = (census_build){ 0 };
}
/* The producer's verdict on `text`: true when it accepts it; otherwise its
 * message and the bytes [start, end) it covers. CollectScalar takes the
 * project, the path, the text, then the target, instruction names, natives,
 * ports and identities, which are parts 0 and 2 to 5 of the target JSON. */
static bool collect_scalar(const char *text, char **message, int *start, int *end, bool *accepted)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *arguments = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, arguments);
    yyjson_mut_arr_add_str(doc, arguments, "/project");
    yyjson_mut_arr_add_str(doc, arguments, "Census.chibi");
    yyjson_mut_arr_add_str(doc, arguments, text);
    yyjson_doc *target = yyjson_read(target_json, target_length, 0);
    REQUIRE(target);
    for (size_t part = 0; part < 6; part++) {
        if (part != 1) {
            yyjson_mut_arr_append(
                arguments,
                yyjson_val_mut_copy(doc, yyjson_arr_get(yyjson_doc_get_root(target), part))
            );
        }
    }
    yyjson_doc_free(target);
    size_t length;
    char *json = yyjson_mut_write(doc, 0, &length);
    yyjson_mut_doc_free(doc);
    REQUIRE(json);
    compiler_host_result result = { 0 };
    bool called = compiler_host_invoke(
        compiler,
        compiler_host_entry(COLLECT_SCALAR),
        json,
        length,
        CENSUS_BUDGET,
        &result
    );
    free(json);
    if (!called) {
        fprintf(stderr, "native census: %s\n", result.diagnostic);
    }
    REQUIRE(called);
    yyjson_doc *produced = yyjson_read(result.json, result.length, COMPILER_HOST_JSON_READ_FLAGS);
    free(result.json);
    REQUIRE(produced);
    yyjson_val *rows_json = yyjson_arr_get(yyjson_doc_get_root(produced), 0);
    yyjson_val *verdict = yyjson_arr_get(rows_json, 1);
    REQUIRE(yyjson_arr_size(rows_json) >= 2);
    *accepted = yyjson_get_int(yyjson_obj_get(verdict, "kind")) != REJECTION_ROW;
    if (!*accepted) {
        const char *said = yyjson_get_str(yyjson_obj_get(verdict, "text"));
        size_t said_length = yyjson_get_len(yyjson_obj_get(verdict, "text"));
        *message = xmalloc(said_length + 1);
        memcpy(*message, said, said_length);
        (*message)[said_length] = 0;
        *start = yyjson_get_int(yyjson_obj_get(verdict, "start"));
        *end = yyjson_get_int(yyjson_obj_get(verdict, "end"));
    }
    yyjson_doc_free(produced);
    return true;
}
/* A function `build` names in bytes [start, end) and leaves out, or -1. */
static int omitted_call(const census_build *build, const int *functions, int start, int end)
{
    for (int i = 0; i < acount(build->calls); i++) {
        const census_edit *call = &build->calls[i];
        if ((int)call->start < end && (int)call->end > start &&
            !listed(functions, call->decl)) {
            return call->decl;
        }
    }
    return -1;
}
/* The excerpt [start, end) of `text`, on one line, at most CENSUS_EXCERPT
 * bytes and whole UTF-8 characters, without backticks or table bars. */
static char *excerpt(const char *text, int start, int end)
{
    int length = end - start < CENSUS_EXCERPT ? end - start : CENSUS_EXCERPT;
    while (length < end - start && length > 0 && (text[start + length] & 0xC0) == 0x80) {
        length--;
    }
    char *out = xmalloc((size_t)length + 1);
    for (int i = 0; i < length; i++) {
        char c = text[start + i];
        out[i] = c == '\n' || c == '\t' || c == '|' ? ' ' : c == '`' ? '\'' : c;
    }
    out[length] = 0;
    return out;
}
/* Where in `build` byte `at` lies: the function that holds it, or -1. */
static int owner(const census_build *build, int at)
{
    for (int i = 0; i < acount(build->functions); i++) {
        if ((int)build->functions[i].start <= at &&
            at < (int)build->functions[i].end) {
            return i;
        }
    }
    return -1;
}
/* The category of a value of type `type`, spelled in bytes [start, end). */
static int type_category(const char *text, int start, int end)
{
    while (end > start && text[end - 1] == ' ') {
        end--;
    }
    if (text[start] == '[') {
        return CENSUS_MAPS;
    }
    if (end - start > 2 && !memcmp(text + end - 2, "[]", 2)) {
        return CENSUS_ARRAYS;
    }
    if (text[start] == '^' || !strncmp(text + start, "ref ", 4)) {
        return CENSUS_POINTERS;
    }
    if (end - start == 6 && !memcmp(text + start, "string", 6)) {
        return CENSUS_STRINGS;
    }
    return CENSUS_OTHER;
}
/* The category of a call to the unknown function `name`: a native of the
 * catalog, which the producer does not call. */
static int native_category(const char *name)
{
    for (size_t i = 0; i < sizeof ARRAY_NATIVES / sizeof *ARRAY_NATIVES; i++) {
        if (!strcmp(name, ARRAY_NATIVES[i])) {
            return CENSUS_ARRAYS;
        }
    }
    if (!strcmp(name, "format_text")) {
        return CENSUS_FORMAT;
    }
    return !strncmp(name, "text_", 5) ? CENSUS_TEXT_NATIVES : CENSUS_NATIVES;
}
/* The category of the producer's rejection `message` of bytes [start, end)
 * of `text`, which lie at `span` of their function. */
static int category(const char *message, const char *text, int start, int end, int span)
{
    if (starts(message, UNKNOWN)) {
        char name[128];
        snprintf(name, sizeof name, "%.*s", end - start, text + start);
        return native_category(name);
    }
    if (strstr(message, "record fields") || strstr(message, "union payloads")) {
        return CENSUS_MANAGED_FIELDS;
    }
    if (!strstr(message, "parameters and one")) {
        return CENSUS_OTHER;
    }
    switch (span) {
        case SPAN_FUNCTION: return CENSUS_NO_RESULT;
        case SPAN_RESULTS:
            return text[start] == '(' ? CENSUS_RESULTS
                                      : type_category(text, start, end);
        case SPAN_PARAMETER: {
            const char *colon = memchr(text + start, ':', (size_t)(end - start));
            assert(colon);
            return type_category(text, (int)(colon - text) + 2, end);
        }
    }
    return CENSUS_OTHER;
}
/* Where byte `at` lies in `place`'s function. */
static int span_of(const census_place *place, int at)
{
    if (!place) {
        return SPAN_TYPE;
    }
    if (at > (int)place->parameters && at < (int)place->results) {
        return SPAN_PARAMETER;
    }
    if (place->result && at == (int)place->results) {
        return SPAN_RESULTS;
    }
    return at == (int)place->start ? SPAN_FUNCTION : SPAN_BODY;
}
/* Whether bytes [start, end) of `text` can name a native: a plain
 * lower-case name that no declaration has. A qualified or declared name
 * there is one the census failed to flatten. */
static bool native_name(const char *text, int start, int end)
{
    for (int i = start; i < end; i++) {
        if (!islower((unsigned char)text[i]) &&
            !isdigit((unsigned char)text[i]) && text[i] != '_') {
            return false;
        }
    }
    for (int i = 0; i < acount(decls); i++) {
        if ((int)strlen(decls[i].name) == end - start &&
            !memcmp(decls[i].name, text + start, (size_t)(end - start))) {
            return false;
        }
    }
    return end > start;
}
/* The census of function `function`: its own text first, then with each
 * function it reaches before its first rejection. */
static bool census_function(int function)
{
    int *functions = NULL;
    apush(functions, function);
    census_build build = { 0 };
    char *message = NULL;
    int start = 0, end = 0;
    bool accepted;
    for (;;) {
        build_source(&build, functions);
        bool collected = collect_scalar(build.text, &message, &start, &end, &accepted);
        if (!collected) {
            fprintf(
                stderr,
                "census: the producer could not run over %s\n%s\n",
                decls[function].name,
                build.text
            );
        }
        REQUIRE(collected);
        /* An omitted callee is unknown, or its name is a native's and the
         * call's arguments differ from the native's. */
        int missing = accepted || starts(message, PRODUCER)
                        ? -1
                        : omitted_call(&build, functions, start, end);
        if (missing < 0) {
            break;
        }
        apush(functions, missing);
        free(message);
        message = NULL;
        build_free(&build);
    }
    census_row row = { .function = decls[function].name,
                       .path = files[decls[function].source].path,
                       .category = CENSUS_NATIVE,
                       .imports = build.imports,
                       .constants = build.constants };
    if (!accepted) {
        int in = owner(&build, start);
        const census_place *place = in >= 0 ? &build.functions[in] : NULL;
        /* A flattening mistake leaves a name unresolved. Every other rejection
         * is the producer's, or a limit of the checks it runs over the body,
         * and so are calls of natives. */
        bool ordinary = starts(message, "Unknown ") &&
                        (!starts(message, UNKNOWN) ||
                         !native_name(build.text, start, end));
        if (ordinary) {
            fprintf(
                stderr,
                "census source of %s rejected: %s\n%s\n",
                row.function,
                message,
                build.text
            );
        }
        REQUIRE(!ordinary);
        row.message = message;
        row.excerpt = excerpt(build.text, start, end);
        row.callee = in > 0 ? decls[place->decl].name : NULL;
        int span = span_of(place, start);
        row.category = category(message, build.text, start, end, span);
    }
    apush(rows, row);
    build_free(&build);
    afree(functions);
    return true;
}

/* Loads the compiler from the sources on disk and describes the natives the
 * census sources may call. */
static bool prepare()
{
    if (compiler) {
        return true;
    }
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    char error[512];
    native = native_capabilities_from_definition_packs(NULL, 0, packs, 4, error, sizeof error);
    REQUIRE(native);
    target_json = compiler_target_json(native_capabilities_catalog(native), &target_length);
    REQUIRE(target_json);
    const compiler_bootstrap options = {
        .folder = CHIBI_SOURCE_ROOT "/tools/compiler/bootstrap",
        .compiler = CHIBI_SOURCE_ROOT "/tools/compiler",
        .core = CHIBI_SOURCE_ROOT "/packages/core",
        .cache = test_compiler_cache()
    };
    compiler = compiler_bootstrap_load(&options, error, sizeof error);
    if (!compiler) {
        fprintf(stderr, "native census bootstrap: %s\n", error);
    }
    REQUIRE(compiler);
    return true;
}
/* Reads the `count` sources of `in`, which outlive the census of them. */
static bool load(const source_package_input *in, int count)
{
    unload();
    if (!parser) {
        parser = ts_parser_new();
        REQUIRE(parser && ts_parser_set_language(parser, tree_sitter_chibi()));
    }
    files = in;
    for (int i = 0; i < count; i++) {
        REQUIRE(read_source(i));
    }
    return true;
}
static const char *report_path;

static int count_of(int category)
{
    int count = 0;
    for (int i = 0; i < acount(rows); i++) {
        count += rows[i].category == category;
    }
    return count;
}
/* The categories, most functions first. */
static void ranked(int order[CENSUS_CATEGORIES])
{
    for (int i = 0; i < CENSUS_CATEGORIES; i++) {
        order[i] = i;
    }
    for (int i = 1; i < CENSUS_CATEGORIES; i++) {
        for (int j = i; j > 0 && count_of(order[j]) > count_of(order[j - 1]); j--) {
            int swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }
    }
}
static void write_summary(FILE *out)
{
    int order[CENSUS_CATEGORIES], imports = 0, constants = 0, plain = 0;
    ranked(order);
    for (int i = 0; i < acount(rows); i++) {
        imports += rows[i].imports;
        constants += rows[i].constants;
        plain += rows[i].category == CENSUS_NATIVE && !rows[i].imports &&
                 !rows[i].constants;
    }
    fprintf(
        out,
        "Of %d functions, %d pass the producer, %d of them without flattened "
        "imports or constants.\n\n",
        acount(rows),
        count_of(CENSUS_NATIVE),
        plain
    );
    fprintf(out, "| Package | First blocker | Functions |\n| --- | --- | ---: |\n");
    for (int i = 0; i < CENSUS_CATEGORIES; i++) {
        if (count_of(order[i])) {
            fprintf(
                out,
                "| %s | %s | %d |\n",
                CATEGORIES[order[i]].package,
                CATEGORIES[order[i]].feature,
                count_of(order[i])
            );
        }
    }
    fprintf(
        out,
        "\nEach first blocker by the producer's message:\n\n| Package | "
        "Rejection | Functions |\n"
        "| --- | --- | ---: |\n"
    );
    for (int c = 0; c < CENSUS_CATEGORIES; c++) {
        for (int i = 0; i < acount(rows); i++) {
            const census_row *r = &rows[i];
            bool first = r->category == order[c] && r->message;
            for (int j = 0; first && j < i; j++) {
                first = rows[j].category != r->category ||
                        strcmp(rows[j].message, r->message);
            }
            int count = 0;
            for (int j = i; first && j < acount(rows); j++) {
                count += rows[j].category == r->category &&
                         !strcmp(rows[j].message, r->message);
            }
            if (first) {
                fprintf(out, "| %s | %s | %d |\n", CATEGORIES[r->category].package, r->message, count);
            }
        }
    }
    fprintf(
        out,
        "\nThe census flattens two features, so neither is ever a first "
        "blocker: %d functions name "
        "another package (W13), and %d name a package constant (W12).\n",
        imports,
        constants
    );
}
static void write_report(FILE *out)
{
    fprintf(
        out,
        "# Native compiler census\n\n"
        "<!-- Generated by `chibi build native-census`; do not edit. -->\n\n"
        "W21 of the [native LLVM ledger](native-llvm-ledger.md#work-packages): "
        "for each function of the\n"
        "self-hosted compiler's bootstrap closure, the first rejection the "
        "scalar producer\n"
        "(`CollectScalar`) gives it, and the work package that removes that "
        "rejection. Each function\n"
        "becomes one source of a single package: a first entry function, the "
        "function, and the records\n"
        "and unions it names, from any package, under `<package>__<Name>`, "
        "with each package constant\n"
        "written as its value. The functions it calls join one at a time, when "
        "the producer reaches a\n"
        "call to one, so a function can be blocked by a function it calls "
        "(*Through*). A function passes\n"
        "when the producer accepts it with everything it calls; it is native "
        "once `scalar_llvm` and\n"
        "`Driver.Object` also take it. The producer checks every signature "
        "before any body, so most\n"
        "functions stop at a parameter or result, and the next blocker shows\n"
        "once that package lands.\n\n"
        "## Summary\n\n"
    );
    write_summary(out);
    fprintf(
        out,
        "\n## Functions\n\n*Rejected* is the text the producer's rejection "
        "covers; *Message* is what the producer said.\n\n"
        "| Source | Function | Package | Through | Also | Rejected | Message "
        "|\n"
        "| --- | --- | --- | --- | --- | --- | --- |\n"
    );
    for (int i = 0; i < acount(rows); i++) {
        const census_row *r = &rows[i];
        const char *also = r->imports && r->constants ? "W12, W13"
                         : r->imports                 ? "W13"
                         : r->constants               ? "W12"
                                                      : "";
        fprintf(
            out,
            "| `%s` | `%s` | %s | %s%s%s | %s | %s%s%s | %s |\n",
            r->path,
            r->function,
            CATEGORIES[r->category].package,
            r->callee ? "`" : "",
            r->callee ? r->callee : "",
            r->callee ? "`" : "",
            also,
            r->excerpt ? "`" : "",
            r->excerpt ? r->excerpt : "",
            r->excerpt ? "`" : "",
            r->message ? r->message : ""
        );
    }
}

static int decl_named(const char *name)
{
    for (int i = 0; i < acount(decls); i++) {
        if (!strcmp(decls[i].name, name)) {
            return i;
        }
    }
    return -1;
}
/* Two packages: app imports lib, whose functions name its record, union and
 * constant. Locals, loop variables and pattern bindings shadow the constant
 * and the import alias in nested and sibling scopes, and a function calls one
 * of more parameters than the producer admits. */
static const char
    LIB_SOURCE[] = "package lib\n"
                   "pub Limit: int : 3;\n"
                   "pub Pair :: struct { a: int; b: int; }\n"
                   "pub Sum :: (p: Pair) -> int {\n"
                   "    return p.a + p.b + Limit;\n"
                   "}\n"
                   "pub Shadow :: (Limit: int) -> int { return Limit; }\n"
                   "pub Scoped :: (x: int) -> int {\n"
                   "    v := Limit;\n"
                   "    if x > 0 { Limit := 9; v = Limit; }\n"
                   "    if x < 0 { v = Limit; }\n"
                   "    for Limit in 0..<x { v += Limit; }\n"
                   "    Limit := Limit + v;\n"
                   "    return Limit;\n"
                   "}\n"
                   "pub Value :: union { Yes(value: int) | No }\n"
                   "pub Picked :: (v: Value) -> int {\n"
                   "    if v is .Yes(Limit) && Limit > 0 { return Limit; } "
                   "else { return Limit; }\n"
                   "    while v is .Yes(Limit) { return Limit; }\n"
                   "    switch v { case .Yes(Limit): return Limit; case: "
                   "return Limit; }\n"
                   "    return Limit;\n"
                   "}\n";
static const char APP_SOURCE
    [] = "package app\n"
         "import lib \"project:lib\"\n"
         "Twice :: (x: int) -> int { return lib.Sum({a: x, b: x}); }\n"
         "Wrap :: (x: int) -> int {\n"
         "    return Many(x, x, x, x, x, x, x, x, [1: x]);\n"
         "}\n"
         "Alias :: (v: lib.Value) -> int {\n"
         "    if v is .Yes(lib) { return lib; }\n"
         "    return lib.Sum({a: 1, b: 2});\n"
         "}\n"
         "Many :: (a: int, b: int, c: int, d: int, e: int,\n"
         "         f: int, g: int, h: int, i: [int: int]) -> int {\n"
         "    return a;\n"
         "}\n";
static const source_package_input SAMPLE[] = {
    { "lib/Lib.chibi", LIB_SOURCE, sizeof LIB_SOURCE - 1 },
    { "app/App.chibi", APP_SOURCE, sizeof APP_SOURCE - 1 }
};
static census_build sample;

/* The sample's census source of `first`, with `second` when it is given. */
static const census_build *census_of(const char *first, const char *second)
{
    int *functions = NULL;
    apush(functions, decl_named(first));
    if (second) {
        apush(functions, decl_named(second));
    }
    build_free(&sample);
    build_source(&sample, functions);
    afree(functions);
    return &sample;
}

TEST_CASE(native_census_flattens_imports_and_constants)
{
    REQUIRE(load(SAMPLE, 2));
    const census_build *build = census_of("Twice", "Sum");
    REQUIRE(!strcmp(
        build->text,
        "package app\n"
        "NativeCensusEntry :: () -> int { return 0; }\n"
        "Twice :: (x: int) -> int { return lib__Sum({a: x, b: x}); }\n"
        "pub lib__Sum :: (p: lib__Pair) -> int {\n"
        "    return p.a + p.b + int(3);\n"
        "}\n"
        "pub lib__Pair :: struct { a: int; b: int; }\n"
    ));
    REQUIRE(build->imports && build->constants && acount(build->calls) == 1);
    build = census_of("Shadow", NULL);
    REQUIRE(!strcmp(
        build->text,
        "package lib\n"
        "NativeCensusEntry :: () -> int { return 0; }\n"
        "pub Shadow :: (Limit: int) -> int { return Limit; }\n"
    ));
    REQUIRE(!build->imports && !build->constants);
    build = census_of("Scoped", NULL);
    REQUIRE(!strcmp(
        build->text,
        "package lib\n"
        "NativeCensusEntry :: () -> int { return 0; }\n"
        "pub Scoped :: (x: int) -> int {\n"
        "    v := int(3);\n"
        "    if x > 0 { Limit := 9; v = Limit; }\n"
        "    if x < 0 { v = int(3); }\n"
        "    for Limit in 0..<x { v += Limit; }\n"
        "    Limit := int(3) + v;\n"
        "    return Limit;\n"
        "}\n"
    ));
    REQUIRE(build->constants);
    build = census_of("Picked", NULL);
    REQUIRE(!strcmp(
        build->text,
        "package lib\n"
        "NativeCensusEntry :: () -> int { return 0; }\n"
        "pub Picked :: (v: Value) -> int {\n"
        "    if v is .Yes(Limit) && Limit > 0 { return Limit; } else { return "
        "int(3); }\n"
        "    while v is .Yes(Limit) { return Limit; }\n"
        "    switch v { case .Yes(Limit): return Limit; case: return int(3); "
        "}\n"
        "    return int(3);\n"
        "}\n"
        "pub Value :: union { Yes(value: int) | No }\n"
    ));
    build = census_of("Alias", NULL);
    REQUIRE(!strcmp(
        build->text,
        "package app\n"
        "NativeCensusEntry :: () -> int { return 0; }\n"
        "Alias :: (v: lib__Value) -> int {\n"
        "    if v is .Yes(lib) { return lib; }\n"
        "    return lib__Sum({a: 1, b: 2});\n"
        "}\n"
        "pub lib__Value :: union { Yes(value: int) | No }\n"
    ));
    build_free(&sample);
    return true;
}
TEST_CASE(native_census_follows_calls_into_their_blockers)
{
    REQUIRE(prepare() && load(SAMPLE, 2));
    const char *names[] = { "Twice", "Wrap", "Many" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(census_function(decl_named(names[i])));
    }
    REQUIRE(rows[0].category == CENSUS_NATIVE && rows[0].imports && rows[0].constants);
    REQUIRE(
        rows[1].category == CENSUS_MAPS && rows[1].callee &&
        !strcmp(rows[1].callee, "Many") && !strcmp(rows[1].excerpt, "i: [int: int]")
    );
    REQUIRE(rows[2].category == CENSUS_MAPS && !rows[2].callee && !rows[2].imports);
    forget_rows();
    return true;
}
TEST_CASE(native_census_covers_every_compiler_function)
{
    REQUIRE(prepare() && load(SOURCES, SOURCE_COUNT));
    int functions = 0;
    for (int i = 0; i < acount(decls); i++) {
        if (decls[i].kind == DECL_FUNCTION) {
            REQUIRE(census_function(i));
            functions++;
        }
    }
    if (acount(rows) != functions || count_of(CENSUS_NATIVE) == 0) {
        fprintf(
            stderr,
            "census: %d rows for %d functions, %d pass\n",
            acount(rows),
            functions,
            count_of(CENSUS_NATIVE)
        );
    }
    REQUIRE(acount(rows) == functions && count_of(CENSUS_NATIVE) > 0);
    write_summary(stdout);
    if (report_path) {
        FILE *out = fopen(report_path, "w");
        REQUIRE(out);
        write_report(out);
        REQUIRE(!fclose(out));
    }
    forget_rows();
    return true;
}

void test_language_native_census_flattening()
{
    RUN_TEST_CASE(native_census_flattens_imports_and_constants);
    cleanup();
}
void test_language_native_census(const char *report)
{
    report_path = report;
    RUN_TEST_CASE(native_census_flattens_imports_and_constants);
    RUN_TEST_CASE(native_census_follows_calls_into_their_blockers);
    RUN_TEST_CASE(native_census_covers_every_compiler_function);
    cleanup();
}
