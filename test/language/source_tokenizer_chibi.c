#include "compiler_fixture.h"
#include "source_tokenizer_chibi_source.h"
#include "support/array.h"
#include "support/file_io.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const TSLanguage *tree_sitter_chibi();
/* The authored tokenizer against Tree-sitter, the parser behind
 * compiler_syntax_parse. Inputs are every .chibi file below packages/, test/
 * and tools/, read at run time, plus fixed sources for what that corpus lacks.
 * Leaves come from the cursor walk compiler_syntax_parse also makes. The
 * external scanner's terminators are hidden there, so each is read from a node
 * that ends after its last child, where the zero-width token sits. One that
 * directly follows a token, as in `x}`, leaves no gap: hand-written cases
 * cover it, and in a corpus file it would fail the comparison as an extra
 * tokenizer terminator rather than pass unseen. Files Tree-sitter rejects
 * compare their leaves only, since its recovery places their terminators.
 * Workload: about 150 files, under 1 MiB, each at most 256 KiB, one
 * compiled-backend call per file; the fixed sources and cases run on both
 * backends. Calls are capped at 64 steps per byte (they take under 20). No
 * children, services or GPU. */
enum
{
    MAX_SOURCE = 256 * 1024,
    MAX_DEPTH = 1024,
    MAX_KINDS = 256,
    STEPS_PER_BYTE = 64,
    TOKEN_COLUMNS = 5,
    ERROR_COLUMNS = 4,
    BACKENDS = 2,
    COMPILED = 1
};
static const char IDENTITY[] = "project:source_tokenizer";
static const char *const ROOTS[] = { "packages", "test", "tools" };

typedef struct
{
    int kind, start, end, row, column;
} token;
typedef struct
{
    const char *message;
    int offset;
} lex_error;
/* A read position: its offset, row and the offset its line starts at. */
typedef struct
{
    int offset, row, line;
} position;

static compiler_fixture fixture;
static TSParser *parser;
static arena input, detached, names_storage;
static const char *names[BACKENDS][MAX_KINDS];
static int name_count[BACKENDS], semicolon;
static int files, rejected, tokens, terminators, splits, bytes;

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
}
static int kind_named(const char *name)
{
    for (int i = 0; i < name_count[0]; i++) {
        if (!strcmp(names[0][i], name)) {
            return i;
        }
    }
    fprintf(stderr, "source tokenizer: no token kind '%s'\n", name);
    return -1;
}
static bool prepare()
{
    if (parser) {
        return true;
    }
    source_package_input sources[] = {
        { "source_tokenizer/Tokenizer.chibi",
          SOURCE_TOKENIZER_CHIBI_SOURCE,
          sizeof SOURCE_TOKENIZER_CHIBI_SOURCE - 1 },
    };
    REQUIRE(compiler_fixture_prepare(&fixture, sources, 1));
    parser = ts_parser_new();
    REQUIRE(parser && ts_parser_set_language(parser, tree_sitter_chibi()));
    for (int b = 0; b < BACKENDS; b++) {
        pin_value out = { 0 };
        REQUIRE(
            compiler_fixture_call(&fixture, b, IDENTITY, "KindNames", NULL, 0, 100000, &names_storage, &out)
        );
        const pin_array *array = out.ptr;
        REQUIRE(out.type == PIN_STR && out.container == PIN_CONTAINER_ARRAY && array);
        REQUIRE(array->count > 0 && array->count <= MAX_KINDS);
        name_count[b] = array->count;
        for (int i = 0; i < array->count; i++) {
            names[b][i] = pin_text(pin_array_get(array, i)).bytes;
            REQUIRE(names[b][i] && names[b][i][0]);
        }
    }
    REQUIRE(name_count[0] == name_count[1]);
    for (int i = 0; i < name_count[0]; i++) {
        REQUIRE(!strcmp(names[0][i], names[1][i]));
    }
    semicolon = kind_named(";");
    REQUIRE(semicolon >= 0);
    return true;
}
static bool is_word(const char *text)
{
    return *text == '_' || (*text >= 'a' && *text <= 'z') ||
           (*text >= 'A' && *text <= 'Z');
}
/* A word Tree-sitter reads as an identifier, a keyword or a keyword that fits
 * as a name: the tokenizer names it a keyword exactly when it is one, and the
 * keywords are the table's lowercase words. */
static int word_kind(const char *text, int size)
{
    for (int i = 0; i < name_count[0]; i++) {
        const char *name = names[0][i];
        if (name[0] >= 'a' && name[0] <= 'z' && (int)strlen(name) == size &&
            !strncmp(name, text, (size_t)size)) {
            return i;
        }
    }
    return kind_named("Ident");
}
static int number_kind(const char *text, int size)
{
    if (size > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        return kind_named("Integer");
    }
    for (int i = 0; i < size; i++) {
        if (text[i] == '.' || text[i] == 'e' || text[i] == 'E') {
            return kind_named("Float");
        }
    }
    return kind_named("Integer");
}
static int leaf_kind(const char *type, const char *text, int size)
{
    if (!strcmp(type, "comment")) {
        return kind_named("Comment");
    }
    if (!strcmp(type, "string")) {
        return kind_named("String");
    }
    if (!strcmp(type, "number")) {
        return number_kind(text, size);
    }
    if (is_word(type)) {
        return word_kind(text, size);
    }
    return kind_named(type);
}
/* Moves `at` forward to `offset`, counting rows as Tree-sitter does. */
static void locate(const char *text, int offset, position *at)
{
    for (; at->offset < offset; at->offset++) {
        if (text[at->offset] == '\n') {
            at->row++;
            at->line = at->offset + 1;
        }
    }
}
static int compare_offsets(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}
/* Tree-sitter's tokens for `text`, ending with EOF; false when it rejects the
 * text, in which case the expected tokens hold no terminators. */
static bool expect(const char *text, token **out, bool *accepted)
{
    TSTree *tree = ts_parser_parse_string(parser, NULL, text, (uint32_t)strlen(text));
    REQUIRE(tree);
    TSNode root = ts_tree_root_node(tree);
    *accepted = !ts_node_has_error(root);
    uint32_t capacity = ts_node_descendant_count(root);
    compiler_fixture_node *nodes = xmalloc(capacity * sizeof *nodes);
    int count = 0;
    bool walked = compiler_fixture_nodes(root, nodes, (int)capacity, MAX_DEPTH, &count);
    int *gaps = NULL;
    /* The root also spans the whitespace before the end of the text. */
    for (int i = 1; walked && *accepted && i < count; i++) {
        int last = nodes[i].first_child;
        if (last < 0) {
            continue;
        }
        while (nodes[last].next_sibling >= 0) {
            last = nodes[last].next_sibling;
        }
        uint32_t end = ts_node_end_byte(nodes[i].node);
        if (end > ts_node_end_byte(nodes[last].node)) {
            apush(gaps, (int)end);
        }
        /* A prototype's field ends with `,`, `;` or a terminator after the
         * field node, before the next sibling, so no node spans it. */
        for (int child = nodes[i].first_child; child >= 0;
             child = nodes[child].next_sibling) {
            if (strcmp(ts_node_type(nodes[i].node), "prototype_body") ||
                strcmp(ts_node_type(nodes[child].node), "struct_field")) {
                continue;
            }
            int next = nodes[child].next_sibling, after = next;
            while (after >= 0 && !strcmp(ts_node_type(nodes[after].node), "comment")) {
                after = nodes[after].next_sibling;
            }
            if (after >= 0 && strcmp(ts_node_type(nodes[after].node), ",") &&
                strcmp(ts_node_type(nodes[after].node), ";")) {
                apush(gaps, (int)ts_node_start_byte(nodes[next].node));
            }
        }
    }
    if (gaps) {
        qsort(gaps, (size_t)alen(gaps), sizeof *gaps, compare_offsets);
    }
    position at = { 0 };
    int gap = 0, gt = kind_named(">"), shr = kind_named(">>"),
        eq = kind_named("="), gt_eq = kind_named(">=");
    for (int i = 0; walked && i <= count; i++) {
        bool leaf = i < count && nodes[i].first_child < 0 &&
                    !ts_node_is_missing(nodes[i].node);
        if (i < count && !leaf) {
            continue;
        }
        int start = i < count ? (int)ts_node_start_byte(nodes[i].node)
                              : (int)strlen(text);
        for (; gap < alen(gaps) && gaps[gap] <= start; gap++) {
            if (gap > 0 && gaps[gap] == gaps[gap - 1]) {
                continue;
            }
            locate(text, gaps[gap], &at);
            apush(*out, (token){ semicolon, gaps[gap], gaps[gap], at.row, gaps[gap] - at.line });
        }
        if (i == count) {
            break;
        }
        int end = (int)ts_node_end_byte(nodes[i].node);
        TSPoint point = ts_node_start_point(nodes[i].node);
        int kind = leaf_kind(ts_node_type(nodes[i].node), text + start, end - start);
        token *previous = alen(*out) ? &alast(*out) : NULL;
        /* Tree-sitter reads `>>` and `>=` as `>` and the next token where
         * the `>` closes type arguments; the tokenizer leaves splitting them
         * to the parser. */
        if ((kind == gt || kind == eq) && previous && previous->kind == gt &&
            previous->end == start && end == start + 1) {
            previous->kind = kind == gt ? shr : gt_eq;
            previous->end = end;
            splits++;
            continue;
        }
        apush(*out, (token){ kind, start, end, (int)point.row, (int)point.column });
    }
    int size = (int)strlen(text);
    locate(text, size, &at);
    apush(*out, (token){ kind_named("EOF"), size, size, at.row, size - at.line });
    afree(gaps);
    free(nodes);
    ts_tree_delete(tree);
    return walked;
}
static bool column_at(const pin_union *payload, int index, pin_type type, const pin_array **out)
{
    const pin_value *value = &payload->values[index];
    REQUIRE(value->type == type && value->container == PIN_CONTAINER_ARRAY);
    *out = value->ptr;
    return true;
}
static int count_of(const pin_array *array)
{
    return array ? array->count : 0;
}
/* The authored tokens and errors for `text`, read from one call. */
static bool tokenize(int backend, const char *text, token **out, lex_error **errors)
{
    arena_free(&input);
    arena_free(&detached);
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(&input, size);
    memcpy(copy, text, size);
    pin_value argument = { .type = PIN_STR,
                           .text = &(
                               const pin_string
                           ){ .bytes = copy, .length = (uint32_t)strlen(copy) } },
              output = { 0 };
    int budget = STEPS_PER_BYTE * (int)size + 100000;
    REQUIRE(
        compiler_fixture_call(&fixture, backend, IDENTITY, "Tokenize", &argument, 1, budget, &detached, &output)
    );
    arena_free(&input);
    const pin_union *payload = output.ptr;
    REQUIRE(output.type == PIN_UNION && payload);
    REQUIRE(payload->count == TOKEN_COLUMNS + ERROR_COLUMNS);
    const pin_array *columns[TOKEN_COLUMNS + ERROR_COLUMNS];
    for (int c = 0; c < TOKEN_COLUMNS + ERROR_COLUMNS; c++) {
        REQUIRE(column_at(payload, c, c == TOKEN_COLUMNS ? PIN_STR : PIN_I32, &columns[c]));
    }
    int count = count_of(columns[0]), failures = count_of(columns[TOKEN_COLUMNS]);
    for (int c = 1; c < TOKEN_COLUMNS + ERROR_COLUMNS; c++) {
        REQUIRE(count_of(columns[c]) == (c < TOKEN_COLUMNS ? count : failures));
    }
    for (int i = 0; i < count; i++) {
        int values[TOKEN_COLUMNS];
        for (int c = 0; c < TOKEN_COLUMNS; c++) {
            values[c] = pin_array_get(columns[c], i).i;
        }
        apush(*out, (token){ values[0], values[1], values[2], values[3], values[4] });
    }
    for (int i = 0; i < failures; i++) {
        /* An unterminated string reports its start after its escapes. */
        int offset = pin_array_get(columns[TOKEN_COLUMNS + 1], i).i;
        REQUIRE(offset >= 0 && offset <= (int)strlen(text));
        position at = { 0 };
        locate(text, offset, &at);
        REQUIRE(pin_array_get(columns[TOKEN_COLUMNS + 2], i).i == at.row);
        REQUIRE(pin_array_get(columns[TOKEN_COLUMNS + 3], i).i == offset - at.line);
        const char *message = pin_text(pin_array_get(columns[TOKEN_COLUMNS], i)).bytes;
        REQUIRE(message && message[0]);
        char *owned = xmalloc(strlen(message) + 1);
        strcpy(owned, message);
        apush(*errors, (lex_error){ owned, offset });
    }
    arena_free(&detached);
    return true;
}
static void free_errors(lex_error *errors)
{
    for (int i = 0; i < alen(errors); i++) {
        free((char *)errors[i].message);
    }
    afree(errors);
}
static bool is_terminator(const token *item)
{
    return item->kind == semicolon && item->start == item->end;
}
static bool same_tokens(const char *label, const token *got, const token *want, bool terminators_too)
{
    int g = 0, w = 0;
    for (;;) {
        while (!terminators_too && g < alen(got) && is_terminator(&got[g])) {
            g++;
        }
        while (!terminators_too && w < alen(want) && is_terminator(&want[w])) {
            w++;
        }
        if (g == alen(got) || w == alen(want)) {
            break;
        }
        const token *a = &got[g], *b = &want[w];
        if (a->kind != b->kind || a->start != b->start || a->end != b->end ||
            a->row != b->row || a->column != b->column) {
            fprintf(
                stderr,
                "%s: token %d is %s %d..%d at %d:%d, Tree-sitter's %s %d..%d "
                "at %d:%d\n",
                label,
                g,
                a->kind >= 0 && a->kind < name_count[0] ? names[0][a->kind] : "?",
                a->start,
                a->end,
                a->row,
                a->column,
                b->kind >= 0 && b->kind < name_count[0] ? names[0][b->kind] : "?",
                b->start,
                b->end,
                b->row,
                b->column
            );
            return false;
        }
        g++;
        w++;
    }
    REQUIRE(g == alen(got) && w == alen(want));
    return true;
}
static bool differential(const char *label, const char *text, int backend)
{
    token *want = NULL, *got = NULL;
    lex_error *errors = NULL;
    bool accepted = false;
    bool valid = expect(text, &want, &accepted) &&
                 tokenize(backend, text, &got, &errors) &&
                 same_tokens(label, got, want, accepted);
    if (valid && alen(errors)) {
        fprintf(stderr, "%s: %s at %d\n", label, errors[0].message, errors[0].offset);
        valid = false;
    }
    if (valid) {
        tokens += alen(got);
        for (int i = 0; i < alen(want); i++) {
            terminators += is_terminator(&want[i]);
        }
        rejected += !accepted;
    }
    afree(want);
    afree(got);
    free_errors(errors);
    return valid;
}
static int compare_paths(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static void collect(const char *directory, char ***paths)
{
    DIR *dir = opendir(directory);
    if (!dir) {
        return;
    }
    for (struct dirent *entry; (entry = readdir(dir));) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        size_t size = strlen(directory) + strlen(entry->d_name) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s/%s", directory, entry->d_name);
        struct stat info;
        size_t length = strlen(entry->d_name);
        if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
            collect(path, paths);
            free(path);
        } else if (length > 6 && !strcmp(entry->d_name + length - 6, ".chibi")) {
            apush(*paths, path);
        } else {
            free(path);
        }
    }
    closedir(dir);
}
/* A corpus file that cannot be read or is over the cap fails by name, so the
 * files after it are not skipped silently. */
static bool corpus_file(const char *path)
{
    char *text = file_read_text(path);
    if (!text) {
        fprintf(stderr, "source tokenizer: cannot read %s\n", path);
        return false;
    }
    size_t size = strlen(text);
    bool valid = size <= MAX_SOURCE;
    if (!valid) {
        fprintf(stderr, "source tokenizer: %s is %zu bytes, over %d\n", path, size, MAX_SOURCE);
    }
    valid = valid && differential(path, text, COMPILED);
    bytes += (int)size;
    free(text);
    return valid;
}
TEST_CASE(source_tokenizer_corpus)
{
    REQUIRE(prepare());
    char **paths = NULL;
    for (size_t r = 0; r < sizeof ROOTS / sizeof *ROOTS; r++) {
        char root[512];
        snprintf(root, sizeof root, "%s/%s", CHIBI_SOURCE_ROOT, ROOTS[r]);
        collect(root, &paths);
    }
    qsort(paths, (size_t)alen(paths), sizeof *paths, compare_paths);
    /* Finding the corpus at all. */
    bool valid = alen(paths) > 100;
    for (int i = 0; valid && i < alen(paths); i++) {
        valid = corpus_file(paths[i]);
        files++;
    }
    for (int i = 0; i < alen(paths); i++) {
        free(paths[i]);
    }
    afree(paths);
    REQUIRE(valid);
    return true;
}
/* Valid sources for rules the corpus does not reach: number forms, a label,
 * types ending a line, comments holding the line break, escapes, `>>` in type
 * arguments and as a shift, CRLF, union variants and multi-line literals, jump
 * keywords, an attribute line, keywords and contextual words as names, bodies
 * opened on the next line, a `return .Name` value, a label opening a case,
 * prototype fields and components, a relationship, `$` and `=>`, struct
 * constants, and `>=` or `>=>` after type arguments. */
static const struct
{
    const char *name, *text;
} SOURCES[] = {
    { "managed pointers",
      "package p\n"
      "F :: (values: ^int[], optional: ^int?, all: ^(int[])) {\n"
      "value := values[0]^\n"
      "pointer := &value\n"
      "copy := pointer^^\n"
      "xor := value ^ +value\n"
      "F(pointer)\n"
      "}\n" },
    { "half-open range", "package p\nRange :: () { for i in 0..<9 {} }\n" },
    { "numbers",
      "package p\n"
      "\n"
      "X: int : 0x1F\n"
      "Y :: 1.5e-3\n"
      "Z :: 2E+8\n"
      "W :: 0XaB\n"
      "Range :: () {\n"
      "    for i in 0..9 {\n"
      "    }\n"
      "    for i in 0..<9 {\n"
      "    }\n"
      "}\n" },
    { "label",
      "B :: behavior {\n"
      "    on Begin :: () {\n"
      "        top:\n"
      "        goto top\n"
      "    }\n"
      "}\n" },
    { "types",
      "package p\n"
      "Pair :: struct {\n"
      "    item: Array<int>\n"
      "    nested: Array<Array<int>>\n"
      "    next: int?\n"
      "}\n" },
    { "comments",
      "B :: behavior {\n"
      "    on Begin :: () {\n"
      "        a := 1 /* first\n"
      "        */ b := 2\n"
      "        c := a // note\n"
      "            + b\n"
      "        d := a\n"
      "            / b\n"
      "    }\n"
      "}\n" },
    { "escapes",
      "B :: behavior {\n"
      "    label := \"\\u00e9\\n\\\"\\\\\\/\\t\"\n"
      "}\n" },
    { "generics",
      "B :: behavior {\n"
      "    on Begin :: () {\n"
      "        x := f<A, g<B>>(1 >> 2, 3 << 4)\n"
      "    }\n"
      "}\n" },
    { "crlf",
      "B :: behavior {\r\n"
      "    x := 1\r\n"
      "    y := 2\r\n"
      "}\r\n" },
    { "literals",
      "package p\n"
      "Shape :: union {\n"
      "    Circle(radius: int)\n"
      "    | Square\n"
      "}\n"
      "Origin :: () -> Point {\n"
      "    p: Point = {\n"
      "        x: 0,\n"
      "        y: 0\n"
      "    }\n"
      "    return {\n"
      "        x: p.x,\n"
      "        y: [1,\n"
      "            2][0]\n"
      "    }\n"
      "}\n" },
    { "jumps",
      "B :: behavior {\n"
      "    on Begin :: () {\n"
      "        if x {\n"
      "            return\n"
      "        }\n"
      "        for item in items {\n"
      "            continue\n"
      "        }\n"
      "        break\n"
      "    }\n"
      "}\n" },
    { "names",
      "package p\n"
      "\n"
      "@test\n"
      "Probe :: () {\n"
      "    x := a.exit\n"
      "    y := from\n"
      "    z := b.state\n"
      "    w := split\n"
      "    v := c.if\n"
      "    u := join\n"
      "    t := d.prototype\n"
      "    if t {\n"
      "        if u {\n"
      "            s := 1\n"
      "        }\n"
      "    }\n"
      "}\n" },
    { "allman",
      "package p\n"
      "Foo :: () -> int\n"
      "{\n"
      "    if x\n"
      "    {\n"
      "        return 1\n"
      "    }\n"
      "    while y\n"
      "    {\n"
      "    }\n"
      "    return 2\n"
      "}\n" },
    { "outcomes",
      "B :: behavior {\n"
      "    macro M() -> () {\n"
      "        exit Done;\n"
      "        switch x {\n"
      "            case 1:\n"
      "                top:\n"
      "                goto top\n"
      "        }\n"
      "        return .Done {a: 1}\n"
      "        y := 1\n"
      "    }\n"
      "}\n" },
    { "prototypes",
      "Squad :: prototype(Base) {\n"
      "    Health {\n"
      "        current: 10,\n"
      "        maximum: 10\n"
      "    }\n"
      "    behaviors: [Probe]\n"
      "    children: [{name: \"enemy\", prototype: Target}]\n"
      "    links: {Targets: child.enemy}\n"
      "}\n" },
    { "relationships",
      "Targets :: relationship {\n"
      "    exclusive: true,\n"
      "    acyclic: true\n"
      "}\n" },
    { "templates",
      "package p\n"
      "Identity :: (value: $T) -> T => {\n"
      "    return value\n"
      "}\n"
      "Scale :: (value: $T, $N: int) -> T {\n"
      "    total: retain int = 0\n"
      "    return value * N\n"
      "}\n" },
    { "fields",
      "P :: prototype {\n"
      "    x: 1;\n"
      "    y: 2 /* c */, z: 3\n"
      "    w: 4 // note\n"
      "}\n" },
    { "constants",
      "package p\n"
      "X :: {\n"
      "    a: 1,\n"
      "    b: 2\n"
      "}\n"
      "Y: T : {\n"
      "    a: 1\n"
      "}\n"
      "G :: () -> Array<int>=> {\n"
      "    x: Array<int>= f()\n"
      "    return x\n"
      "}\n" },
};
TEST_CASE(source_tokenizer_sources)
{
    REQUIRE(prepare());
    for (size_t i = 0; i < sizeof SOURCES / sizeof *SOURCES; i++) {
        for (int b = 0; b < BACKENDS; b++) {
            REQUIRE(differential(SOURCES[i].name, SOURCES[i].text, b));
        }
        REQUIRE(rejected == 0);
    }
    return true;
}
/* Hand-written expectations: terminators with no gap before them, which the
 * differential cannot see, and Odin's errors. Tokens are kind, start, end;
 * errors are message and offset. */
typedef struct
{
    const char *kind;
    int start, end;
} expected_token;
static const struct
{
    const char *text;
    expected_token tokens[12];
    lex_error errors[3];
} CASES[] = {
    { "0..<9",
      { { "Integer", 0, 1 }, { "..<", 1, 4 }, { "Integer", 4, 5 }, { ";", 5, 5 }, { "EOF", 5, 5 } },
      { { 0 } } },
    { "0..9",
      { { "Integer", 0, 1 }, { "..", 1, 3 }, { "Integer", 3, 4 }, { ";", 4, 4 }, { "EOF", 4, 4 } },
      { { 0 } } },
    { "0.. <9",
      { { "Integer", 0, 1 },
        { "..", 1, 3 },
        { "<", 4, 5 },
        { "Integer", 5, 6 },
        { ";", 6, 6 },
        { "EOF", 6, 6 } },
      { { 0 } } },
    { "on X { y := 1}",
      { { "on", 0, 2 },
        { "Ident", 3, 4 },
        { "{", 5, 6 },
        { "Ident", 7, 8 },
        { ":=", 9, 11 },
        { "Integer", 12, 13 },
        { ";", 13, 13 },
        { "}", 13, 14 },
        { "EOF", 14, 14 } },
      { { 0 } } },
    { "package p",
      { { "package", 0, 7 }, { "Ident", 8, 9 }, { ";", 9, 9 }, { "EOF", 9, 9 } },
      { { 0 } } },
    { "x #\ny",
      { { "Ident", 0, 1 },
        { "Invalid", 2, 3 },
        { ";", 4, 4 },
        { "Ident", 4, 5 },
        { ";", 5, 5 },
        { "EOF", 5, 5 } },
      { { "illegal character '#': 35", 2 } } },
    { "s := 1\nt := \"abc\ny",
      { { "Ident", 0, 1 },
        { ":=", 2, 4 },
        { "Integer", 5, 6 },
        { ";", 7, 7 },
        { "Ident", 7, 8 },
        { ":=", 9, 11 },
        { "String", 12, 16 },
        { ";", 17, 17 },
        { "Ident", 17, 18 },
        { ";", 18, 18 },
        { "EOF", 18, 18 } },
      { { "String literal not terminated", 12 } } },
    { "a /* open",
      { { "Ident", 0, 1 }, { ";", 2, 2 }, { "Comment", 2, 9 }, { "EOF", 9, 9 } },
      { { "comment not terminated", 2 } } },
    { "\"\\q\\u12\"",
      { { "String", 0, 8 }, { ";", 8, 8 }, { "EOF", 8, 8 } },
      { { "unknown escape sequence", 2 },
        { "illegal character 34 in escape sequence", 7 } } },
    { "\"\\",
      { { "String", 0, 2 }, { ";", 2, 2 }, { "EOF", 2, 2 } },
      { { "escape sequence was not terminated", 2 },
        { "String literal not terminated", 0 } } },
    { "0x 1e+ 2",
      { { "Invalid", 0, 2 }, { "Float", 3, 6 }, { "Integer", 7, 8 }, { ";", 8, 8 }, { "EOF", 8, 8 } },
      { { "illegal hexadecimal integer", 2 }, { "illegal floating-point exponent", 6 } } },
    { "\xc3\xa9",
      { { "Invalid", 0, 2 }, { "EOF", 2, 2 } },
      { { "illegal character '\xc3\xa9': 233", 0 } } },
    { "\xff\xe2\x82",
      { { "Invalid", 0, 1 }, { "Invalid", 1, 2 }, { "Invalid", 2, 3 }, { "EOF", 3, 3 } },
      { { "illegal UTF-8 encoding", 0 },
        { "illegal UTF-8 encoding", 1 },
        { "illegal UTF-8 encoding", 2 } } },
};
TEST_CASE(source_tokenizer_cases)
{
    REQUIRE(prepare());
    for (size_t i = 0; i < sizeof CASES / sizeof *CASES; i++) {
        token *want = NULL;
        position at = { 0 };
        bool valid = true;
        for (const expected_token *item = CASES[i].tokens; valid && item->kind; item++) {
            int kind = kind_named(item->kind);
            locate(CASES[i].text, item->start, &at);
            apush(want, (token){ kind, item->start, item->end, at.row, item->start - at.line });
            valid = kind >= 0;
        }
        int failures = 0;
        while (failures < 3 && CASES[i].errors[failures].message) {
            failures++;
        }
        for (int b = 0; valid && b < BACKENDS; b++) {
            token *got = NULL;
            lex_error *errors = NULL;
            valid = tokenize(b, CASES[i].text, &got, &errors) &&
                    same_tokens(CASES[i].text, got, want, true) &&
                    alen(errors) == failures;
            for (int e = 0; valid && e < failures; e++) {
                valid = !strcmp(errors[e].message, CASES[i].errors[e].message) &&
                        errors[e].offset == CASES[i].errors[e].offset;
                if (!valid) {
                    fprintf(
                        stderr,
                        "error %d: %s at %d\n",
                        e,
                        errors[e].message,
                        errors[e].offset
                    );
                }
            }
            afree(got);
            free_errors(errors);
        }
        afree(want);
        REQUIRE(valid);
    }
    return true;
}
void test_language_source_tokenizer_chibi()
{
    files = rejected = tokens = terminators = splits = bytes = 0;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_tokenizer_corpus);
    printf(
        "Source tokenizer corpus: %d files (%d rejected by Tree-sitter), %d "
        "bytes, "
        "%d tokens, %d terminators, %d split >> or >=\n",
        files,
        rejected,
        bytes,
        tokens,
        terminators,
        splits
    );
    rejected = 0;
    RUN_TEST_CASE(source_tokenizer_sources);
    RUN_TEST_CASE(source_tokenizer_cases);
    pu_clear_setup();
    cleanup();
    arena_free(&names_storage);
    compiler_fixture_free(&fixture);
    ts_parser_delete(parser);
    parser = NULL;
}
