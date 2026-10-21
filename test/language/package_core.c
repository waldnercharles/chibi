#include "authoring/native_bindings.h"
#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "execution/function.h"
#include "execution/module.h"
#include "execution/status.h"
#include "package_core_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/text_definition.h"
#include "runtime/text_format.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Seven cases, two backends. Each preparation has two files, one import and
 * three-element arrays at most. Each call has a fixed 10000-instruction budget;
 * no filesystem, services, workers, frames or timing loops. The
 * generated header embeds the actual installed-source candidate, not a second
 * wrapper copy. Expected values follow Odin's core:strings over bytes. */
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static authoring_packages *owners[3];
static native_capabilities *tables[2];
static execution_module *retained;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage, inputs_storage;
static pin_value output;
static const char APP[] =
    "package app\nimport strings \"core:strings\"\n"
    "Row :: struct { value: int; tag: string; }\n"
    "pub Check :: () -> int {\n"
    " numbers: int[] = [1, 2]; third: int = 3; numbers = "
    "array_add(numbers, third);\n"
    " words: string[] = [\"alpha\"]; words = array_add(words, \"beta\");\n"
    " rows: Row[] = []; row: Row = {value: 7, tag: \"seven\"};\n"
    " rows = array_add(rows, row); picked: Row = rows[0];\n"
    " if length(numbers) != 3 || numbers[2] != 3 { return -1; }\n"
    " if length(words) != 2 || words[1] != \"beta\" { return -2; }\n"
    " if length(rows) != 1 || picked.value != 7 || picked.tag != "
    "\"seven\" { return -3; }\n"
    " if !strings.IsIdentifier(strings.BaseName(\"root/valid_name\")) { "
    "return -4; }\n"
    " return strings.ByteLength(strings.Slice(words[1], 1, 3));\n"
    "}\n"
    "View :: (parts: string[]) -> string {\n"
    " return format_text(\"{}:{}\", length(parts), strings.Join(parts, "
    "\"|\"));\n"
    "}\n"
    "pub Split :: (kind: int, s: string, sep: string, n: int) -> string {\n"
    " if kind == 0 { return View(strings.Split(s, sep)); }\n"
    " if kind == 1 { return View(strings.SplitN(s, sep, n)); }\n"
    " if kind == 2 { return View(strings.SplitAfter(s, sep)); }\n"
    " if kind == 3 { return View(strings.SplitAfterN(s, sep, n)); }\n"
    " if kind == 4 { return View(strings.SplitLines(s)); }\n"
    " if kind == 5 { return View(strings.SplitLinesN(s, n)); }\n"
    " if kind == 6 { return View(strings.SplitLinesAfter(s)); }\n"
    " if kind == 7 { return View(strings.SplitLinesAfterN(s, n)); }\n"
    " if kind == 8 { return strings.Concatenate(strings.Split(s, sep)); }\n"
    " return View(strings.Fields(s));\n"
    "}\n"
    "pub Partition :: (s: string, sep: string) -> string {\n"
    " parts: strings.Parts = strings.Partition(s, sep);\n"
    " return format_text(\"{}|{}|{}\", parts.head, parts.match, parts.tail);\n"
    "}\n";

static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    output = (pin_value){ 0 };
}
static void cleanup()
{
    finish_call();
    arena_free(&inputs_storage);
    execution_module_free(retained);
    retained = NULL;
    for (int i = 0; i < 3; i++) {
        authoring_packages_free(owners[i]);
        owners[i] = NULL;
    }
    for (int i = 0; i < 2; i++) {
        native_capabilities_free(tables[i]);
        tables[i] = NULL;
    }
}
static bool table(int index)
{
    native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                       NATIVE_ARRAY_DEFINITION_PACK };
    char error[192];
    tables[index] = native_capabilities_from_definition_packs(NULL, 0, packs, 2, error, sizeof error);
    if (!tables[index]) {
        fprintf(stderr, "core native: %s\n", error);
    }
    REQUIRE(tables[index]);
    return true;
}
static bool prepare(int index, int previous, int native, behavior_backend backend)
{
    char *source = strdup(CORE_STRINGS_SOURCE), *app = strdup(APP),
         *root = strdup("/explicit/project"), *core = strdup("/explicit/sdk/core");
    REQUIRE(source && app && root && core);
    source_package_input files[] = {
        { "/explicit/sdk/core/strings/Strings.chibi", source, strlen(source) },
        { "app/Main.chibi", app, strlen(app) },
    };
    REQUIRE(files[0].size < 16384 && files[1].size < 4096);
    source_package_collection collections[] = { { "core", core } };
    source_package_snapshot_options options = {
        .project_root = root,
        .files = files,
        .file_count = 2,
        .collections = collections,
        .collection_count = 1,
    };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    memset(source, '?', files[0].size);
    memset(app, '?', files[1].size);
    free(source);
    free(app);
    free(root);
    free(core);
    REQUIRE(!source_package_snapshot_error_at(snapshot));
    script_lower_bindings bindings = authoring_native_bindings(tables[native]);
    owners[index] = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(tables[native]),
        &bindings,
        backend,
        previous >= 0 ? owners[previous] : NULL
    );
    REQUIRE(owners[index]);
    if (!authoring_packages_valid(owners[index])) {
        const char *error = authoring_packages_error(owners[index]);
        if (error) {
            fprintf(stderr, "core owner: %s\n", error);
        }
        for (int i = 0; i < authoring_packages_count(owners[index]); i++) {
            const authoring_package *item = authoring_packages_at(owners[index], i);
            for (int j = 0; j < item->diagnostic_count; j++) {
                const script_diagnostic *diagnostic = &item->diagnostics[j];
                const char *path = "unknown";
                if (diagnostic->range.document >= 0 &&
                    diagnostic->range.document < item->analysis->document_count) {
                    path = item->analysis->documents[diagnostic->range.document].key;
                }
                fprintf(
                    stderr,
                    "%s %s:%d:%d [%d,%d): %s\n",
                    item->identity,
                    path,
                    diagnostic->range.line,
                    diagnostic->range.column,
                    diagnostic->range.start,
                    diagnostic->range.end,
                    diagnostic->message
                );
            }
        }
    }
    REQUIRE(authoring_packages_valid(owners[index]));
    REQUIRE(authoring_packages_count(owners[index]) == 2);
    return true;
}
static const authoring_package *package(int index, const char *identity)
{
    for (int i = 0; i < authoring_packages_count(owners[index]); i++) {
        const authoring_package *item = authoring_packages_at(owners[index], i);
        if (!strcmp(item->identity, identity)) {
            return item;
        }
    }
    return NULL;
}
static uint64_t exported(const authoring_package *item, const char *name)
{
    for (int i = 0; item && i < item->exports->member_count; i++) {
        const script_package_export *member = &item->exports->members[i];
        if (!strcmp(member->name, name)) {
            return member->id;
        }
    }
    return 0;
}
static bool invoke(
    execution_module *module,
    uint64_t id,
    const pin_value *arguments,
    int count,
    pin_type type,
    behavior_backend backend
)
{
    finish_call();
    REQUIRE(module && id && count <= 4);
    const behavior_function *function = NULL;
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
    output = (pin_value){ .type = type };
    context = (behavior_function_context){
        .activation = behavior_function_resumable(function) ? &activation : NULL,
        .arguments = arguments,
        .argument_count = count,
        .results = &output,
        .budget = 10000,
    };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &context, backend == TEST_COMPILED_BACKEND);
    behavior_run_scope_end(&runs);
    REQUIRE(status == FLOW_OK && context.returned && output.type == type);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(
        backend == BEHAVIOR_BYTECODE ? runs.counts.bytecode_entries > 0
                                     : !runs.counts.bytecode_entries
    );
    REQUIRE(backend != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
    REQUIRE(backend != BEHAVIOR_WASM || runs.counts.wasm_entries > 0);
    return true;
}
static bool core_call(const char *name, const pin_value *arguments, int count, pin_type type, behavior_backend backend)
{
    const authoring_package *core = package(0, "core:strings");
    REQUIRE(core && core->valid && core->module);
    return invoke(core->module, exported(core, name), arguments, count, type, backend);
}
static pin_value text(const char *value)
{
    pin_value result;
    const char *error = pin_value_text(&inputs_storage, &result, value, strlen(value));
    assert(!error);
    (void)error;
    return result;
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
/* One call of an exported function. `args` names each argument in order, s for
 * the next text and i for the next integer. Integer and bool results compare
 * `number`; text results compare `result`. */
typedef struct
{
    const char *name, *args, *texts[3];
    int integers[2];
    pin_type type;
    int number;
    const char *result;
} call_case;
#define INT_SS(NAME, A, B, N) { NAME, "ss", { A, B }, { 0 }, PIN_I32, N, NULL }
#define INT_SI(NAME, A, I, N) { NAME, "si", { A }, { I }, PIN_I32, N, NULL }
#define BOOL_SS(NAME, A, B, V)                                                 \
    { NAME, "ss", { A, B }, { 0 }, PIN_BOOL, V, NULL }
#define BOOL_I(NAME, I, V) { NAME, "i", { NULL }, { I }, PIN_BOOL, V, NULL }
#define TEXT_S(NAME, A, R) { NAME, "s", { A }, { 0 }, PIN_STR, 0, R }
#define TEXT_SS(NAME, A, B, R) { NAME, "ss", { A, B }, { 0 }, PIN_STR, 0, R }
#define TEXT_SI(NAME, A, I, R) { NAME, "si", { A }, { I }, PIN_STR, 0, R }
#define TEXT_SII(NAME, A, I, J, R)                                             \
    { NAME, "sii", { A }, { I, J }, PIN_STR, 0, R }
#define TEXT_SSS(NAME, A, B, C, R)                                             \
    { NAME, "sss", { A, B, C }, { 0 }, PIN_STR, 0, R }
#define TEXT_SSI(NAME, A, B, I, R)                                             \
    { NAME, "ssi", { A, B }, { I }, PIN_STR, 0, R }
#define TEXT_SSSI(NAME, A, B, C, I, R)                                         \
    { NAME, "sssi", { A, B, C }, { I }, PIN_STR, 0, R }
/* The app's Split picks a splitter by kind and renders count:part|part. */
#define SPLIT(KIND, S, SEP, N, R)                                              \
    { "Split", "issi", { S, SEP }, { KIND, N }, PIN_STR, 0, R }
#define CASES(ARRAY) ARRAY, (int)(sizeof ARRAY / sizeof ARRAY[0])
static bool run_cases(const char *identity, const call_case *cases, int count, behavior_backend backend)
{
    const authoring_package *item = package(0, identity);
    REQUIRE(item && item->valid && item->module);
    for (int c = 0; c < count; c++) {
        const call_case *call = &cases[c];
        pin_value args[4];
        int size = (int)strlen(call->args), texts = 0, integers = 0;
        REQUIRE(size <= 4);
        for (int i = 0; i < size; i++) {
            args[i] = call->args[i] == 's' ? text(call->texts[texts++])
                                           : integer(call->integers[integers++]);
        }
        REQUIRE(invoke(item->module, exported(item, call->name), args, size, call->type, backend));
        bool same = call->type == PIN_STR
                      ? !strcmp(pin_text(output).bytes, call->result)
                  : call->type == PIN_BOOL ? output.b == (call->number != 0)
                                           : output.i == call->number;
        if (!same) {
            fprintf(stderr, "core strings case %d: %s\n", c, call->name);
        }
        REQUIRE(same);
    }
    return true;
}
TEST_CASE(package_core_strings_bytes_bounds_and_owned_slices)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        char bytes[] = { 'A', (char)0x80, (char)0xff, 0 };
        pin_value args[] = { text(bytes), integer(0), integer(0) };
        REQUIRE(core_call("ByteLength", args, 1, PIN_I32, backends[b]) && output.i == 3);
        const int indices[] = { -1, 0, 1, 2, 3 };
        const int expected[] = { -1, 65, 128, 255, -1 };
        for (int i = 0; i < 5; i++) {
            args[1] = integer(indices[i]);
            REQUIRE(
                core_call("ByteAt", args, 2, PIN_I32, backends[b]) &&
                output.i == expected[i]
            );
        }
        REQUIRE(core_call("Bytes", args, 1, PIN_I32, backends[b]));
        const pin_array *read = output.ptr;
        REQUIRE(output.container == PIN_CONTAINER_ARRAY && read && read->count == 3);
        for (int i = 0; i < 3; i++) {
            pin_value item = pin_array_get(read, i);
            REQUIRE(item.type == PIN_I32 && item.i == expected[i + 1]);
        }
        args[0] = text("");
        REQUIRE(core_call("ByteLength", args, 1, PIN_I32, backends[b]) && output.i == 0);
        REQUIRE(core_call("Bytes", args, 1, PIN_I32, backends[b]));
        read = output.ptr;
        REQUIRE(output.container == PIN_CONTAINER_ARRAY && read && read->count == 0);
        args[1] = integer(0);
        REQUIRE(core_call("ByteAt", args, 2, PIN_I32, backends[b]) && output.i == -1);
        char source[] = "abcdef";
        args[0] = text(source);
        args[1] = integer(1);
        args[2] = integer(3);
        REQUIRE(core_call("Slice", args, 3, PIN_STR, backends[b]));
        REQUIRE(
            pin_text(output).bytes && pin_text(output).bytes != source + 1 &&
            !strcmp(pin_text(output).bytes, "bcd")
        );
        memset(source, 'x', 6);
        REQUIRE(!strcmp(pin_text(output).bytes, "bcd"));
        args[0] = text("abcdef");
        const int starts[] = { -2, 99, 2, 4 };
        const int counts[] = { 2, 2, -1, 99 };
        const char *slices[] = { "ab", "", "", "ef" };
        for (int i = 0; i < 4; i++) {
            args[1] = integer(starts[i]);
            args[2] = integer(counts[i]);
            REQUIRE(core_call("Slice", args, 3, PIN_STR, backends[b]));
            REQUIRE(!strcmp(pin_text(output).bytes, slices[i]));
        }
        const char binary[] = { 'A', 0, 'B', (char)0xff };
        REQUIRE(!pin_value_text(&inputs_storage, &args[0], binary, sizeof binary));
        REQUIRE(core_call("ByteLength", args, 1, PIN_I32, backends[b]) && output.i == 4);
        args[1] = integer(1);
        REQUIRE(core_call("ByteAt", args, 2, PIN_I32, backends[b]) && output.i == 0);
        args[1] = integer(3);
        REQUIRE(core_call("ByteAt", args, 2, PIN_I32, backends[b]) && output.i == 255);
        args[1] = integer(1);
        args[2] = integer(3);
        REQUIRE(core_call("Slice", args, 3, PIN_STR, backends[b]));
        REQUIRE(
            pin_text(output).length == 3 &&
            !memcmp(pin_text(output).bytes, binary + 1, 3)
        );
        pin_value format_args[] = { text("{:>4.2s}"), args[0] }, formatted;
        REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, format_args, 2, &formatted));
        const char padded[] = { ' ', ' ', 'A', 0 };
        REQUIRE(
            pin_text(formatted).length == sizeof padded &&
            !memcmp(pin_text(formatted).bytes, padded, sizeof padded)
        );
        pin_value character_args[] = { text("{:3c}"), integer(0) };
        REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, character_args, 2, &formatted));
        const char character[] = { ' ', ' ', 0 };
        REQUIRE(
            pin_text(formatted).length == sizeof character &&
            !memcmp(pin_text(formatted).bytes, character, sizeof character)
        );
        const char invalid_mods[] = { '{', ':', 0, 'g', '}' };
        REQUIRE(
            !pin_value_text(&inputs_storage, &character_args[0], invalid_mods, sizeof invalid_mods)
        );
        character_args[1] = integer(42);
        REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, character_args, 2, &formatted));
        REQUIRE(pin_text(formatted).length == 2 && !memcmp(pin_text(formatted).bytes, "42", 2));
        const char format[] = { 'X', 0, '{', '}' };
        REQUIRE(!pin_value_text(&inputs_storage, &format_args[0], format, sizeof format));
        REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, format_args, 2, &formatted));
        const char joined[] = { 'X', 0, 'A', 0, 'B', (char)0xff };
        REQUIRE(
            pin_text(formatted).length == sizeof joined &&
            !memcmp(pin_text(formatted).bytes, joined, sizeof joined)
        );
        arena_free(&inputs_storage);
        REQUIRE(
            pin_text(output).length == 3 &&
            !memcmp(pin_text(output).bytes, binary + 1, 3)
        );
        REQUIRE(
            pin_text(formatted).length == sizeof joined &&
            !memcmp(pin_text(formatted).bytes, joined, sizeof joined)
        );
        cleanup();
    }
    return true;
}
TEST_CASE(package_core_strings_comparison_identifiers_and_basename)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        const char high[] = { (char)0xff, 0 }, low[] = { (char)0x80, 0 };
        const char *left[] = { "", "abc", "ab", "b", high, low };
        const char *right[] = { "", "abc", "abc", "a", low, high };
        const int expected[] = { 0, 0, -1, 1, 1, -1 };
        for (int i = 0; i < 6; i++) {
            pin_value args[] = { text(left[i]), text(right[i]) };
            REQUIRE(
                core_call("Compare", args, 2, PIN_I32, backends[b]) &&
                output.i == expected[i]
            );
        }
        const char *names[] = { "", "a", "_A9", "9a", "a-b", high };
        const bool identifiers[] = { false, true, true, false, false, false };
        for (int i = 0; i < 6; i++) {
            pin_value arg = text(names[i]);
            REQUIRE(
                core_call("IsIdentifier", &arg, 1, PIN_BOOL, backends[b]) &&
                output.b == identifiers[i]
            );
        }
        const char *paths[] = { "", "file", "/", "a/b/", "a/b", "a\\b" };
        const char *base[] = { "", "file", "", "", "b", "a\\b" };
        for (int i = 0; i < 6; i++) {
            pin_value arg = text(paths[i]);
            REQUIRE(core_call("BaseName", &arg, 1, PIN_STR, backends[b]));
            REQUIRE(!strcmp(pin_text(output).bytes, base[i]));
        }
        cleanup();
    }
    return true;
}
static const call_case search_cases[] = {
    INT_SS("Index", "chicken", "ken", 4),
    INT_SS("Index", "chicken", "dmr", -1),
    INT_SS("Index", "", "", 0),
    INT_SS("Index", "abc", "", 0),
    INT_SS("Index", "", "a", -1),
    INT_SS("Index", "abc", "c", 2),
    INT_SS("Index", "abc", "abc", 0),
    INT_SS("Index", "abc", "abd", -1),
    INT_SS("Index", "ab", "abc", -1),
    INT_SS("Index", "aaab", "aab", 1),
    INT_SS("Index", "abcxabc", "abc", 0),
    INT_SS("Index", "the quick brown fox", "fox", 16),
    INT_SS("LastIndex", "go gopher", "go", 3),
    INT_SS("LastIndex", "go gopher", "rodent", -1),
    INT_SS("LastIndex", "abc", "", 3),
    INT_SS("LastIndex", "", "", 0),
    INT_SS("LastIndex", "aaaa", "aa", 2),
    INT_SS("LastIndex", "abcabc", "abc", 3),
    INT_SS("LastIndex", "xabcxx", "abc", 1),
    INT_SS("LastIndex", "abc", "abc", 0),
    INT_SS("LastIndex", "abc", "b", 1),
    INT_SI("IndexByte", "golang", 'l', 2),
    INT_SI("IndexByte", "golang", 'z', -1),
    INT_SI("IndexByte", "", 'a', -1),
    INT_SI("IndexByte", "a\xff", 255, 1),
    INT_SI("LastIndexByte", "hello", 'l', 3),
    INT_SI("LastIndexByte", "hello", 'z', -1),
    INT_SS("IndexAny", "golang", "xyzn", 4),
    INT_SS("IndexAny", "golang", "", -1),
    INT_SS("IndexAny", "", "a", -1),
    INT_SS("LastIndexAny", "go gopher", "go", 4),
    INT_SS("LastIndexAny", "go gopher", "", -1),
    INT_SS("LastIndexAny", "go gopher", "xyz", -1),
    INT_SS("Count", "cheese", "e", 3),
    INT_SS("Count", "five", "", 5),
    INT_SS("Count", "", "", 1),
    INT_SS("Count", "aaaa", "aa", 2),
    INT_SS("Count", "abcabc", "bc", 2),
    INT_SS("Count", "abc", "d", 0),
    BOOL_SS("Contains", "seafood", "foo", 1),
    BOOL_SS("Contains", "seafood", "bar", 0),
    BOOL_SS("Contains", "seafood", "", 1),
    BOOL_SS("Contains", "", "", 1),
    BOOL_SS("ContainsAny", "failure", "ui", 1),
    BOOL_SS("ContainsAny", "foo", "", 0),
    BOOL_SS("ContainsAny", "", "", 0),
    BOOL_SS("HasPrefix", "testing", "test", 1),
    BOOL_SS("HasPrefix", "testing", "", 1),
    BOOL_SS("HasPrefix", "te", "test", 0),
    BOOL_SS("HasPrefix", "testing", "est", 0),
    BOOL_SS("HasSuffix", "todo.txt", ".txt", 1),
    BOOL_SS("HasSuffix", "todo.doc", ".txt", 0),
    BOOL_SS("HasSuffix", "", "", 1),
    BOOL_SS("HasSuffix", "t", "txt", 0),
};
/* Case and spaces are ASCII: UTF-8 bytes neither fold nor trim. */
static const call_case text_cases[] = {
    BOOL_SS("EqualFold", "Go", "GO", 1),
    BOOL_SS("EqualFold", "Test", "tEsT", 1),
    BOOL_SS("EqualFold", "test", "tes", 0),
    BOOL_SS("EqualFold", "", "", 1),
    BOOL_SS("EqualFold", "@[", "`{", 0),
    BOOL_SS("EqualFold", "\xc3\xa9", "\xc3\x89", 0),
    TEXT_S("ToLower", "Hello, World!", "hello, world!"),
    TEXT_S("ToLower", "@AZ[`az{", "@az[`az{"),
    TEXT_S("ToLower", "", ""),
    TEXT_S(
        "ToLower",
        "\xc3\x80"
        "B",
        "\xc3\x80"
        "b"
    ),
    TEXT_S("ToUpper", "hello wOrld", "HELLO WORLD"),
    TEXT_S("ToUpper", "z", "Z"),
    TEXT_S("ToUpper", "@AZ[`az{", "@AZ[`AZ{"),
    BOOL_I("IsAsciiSpace", '\t', 1),
    BOOL_I("IsAsciiSpace", '\r', 1),
    BOOL_I("IsAsciiSpace", ' ', 1),
    BOOL_I("IsAsciiSpace", 8, 0),
    BOOL_I("IsAsciiSpace", 14, 0),
    BOOL_I("IsAsciiSpace", 0xa0, 0),
    TEXT_SS("TrimLeft", "xxhixx", "x", "hixx"),
    TEXT_SS("TrimRight", "xxhixx", "x", "xxhi"),
    TEXT_SS("Trim", "xxhixx", "x", "hi"),
    TEXT_SS("Trim", "abcba", "ab", "c"),
    TEXT_SS("Trim", "xxxx", "x", ""),
    TEXT_SS("Trim", "hi", "", "hi"),
    TEXT_SS("Trim", "", "x", ""),
    TEXT_S("TrimSpace", " \t\n hi there \r\v\f", "hi there"),
    TEXT_S("TrimSpace", "   ", ""),
    TEXT_S(
        "TrimSpace",
        "\xa0"
        "a"
        "\xa0",
        "\xa0"
        "a"
        "\xa0"
    ),
    TEXT_S("TrimLeftSpace", "  a ", "a "),
    TEXT_S("TrimRightSpace", "  a ", "  a"),
    TEXT_SS("TrimPrefix", "testing", "test", "ing"),
    TEXT_SS("TrimPrefix", "testing", "ing", "testing"),
    TEXT_SS("TrimPrefix", "a", "", "a"),
    TEXT_SS("TrimSuffix", "todo.txt", ".txt", "todo"),
    TEXT_SS("TrimSuffix", "todo.txt", ".doc", "todo.txt"),
    TEXT_SS("TrimSuffix", "x", "x", ""),
    TEXT_SI("TruncateToByte", "a/b/c", '/', "a"),
    TEXT_SI("TruncateToByte", "abc", '/', "abc"),
    TEXT_SI("TruncateToByte", "/abc", '/', ""),
    TEXT_SII("Cut", "some example text", 0, 4, "some"),
    TEXT_SII("Cut", "some example text", 2, 2, "me"),
    TEXT_SII("Cut", "some example text", 5, 7, "example"),
    TEXT_SII("Cut", "some example text", 5, 0, "example text"),
    TEXT_SII("Cut", "abc", 1, 99, "bc"),
    TEXT_SII("Cut", "abc", 3, 1, "a"),
    TEXT_SII("Cut", "abc", -1, 0, "abc"),
    TEXT_SII("Cut", "", 0, 0, ""),
    TEXT_SI("Repeat", "na", 3, "nanana"),
    TEXT_SI("Repeat", "x", 5, "xxxxx"),
    TEXT_SI("Repeat", "abc", 1, "abc"),
    TEXT_SI("Repeat", "ab", 0, ""),
    TEXT_SI("Repeat", "ab", -1, ""),
    TEXT_SI("Repeat", "", 5, ""),
};
static const call_case replace_cases[] = {
    TEXT_SSSI("Replace", "oink oink oink", "k", "ky", 2, "oinky oinky oink"),
    TEXT_SSSI("Replace", "oink oink oink", "oink", "moo", -1, "moo moo moo"),
    TEXT_SSSI("Replace", "aaaa", "aa", "b", -1, "bb"),
    TEXT_SSSI("Replace", "abc", "", "-", -1, "-a-b-c-"),
    TEXT_SSSI("Replace", "abc", "", "-", 2, "-a-bc"),
    TEXT_SSSI("Replace", "abc", "x", "y", -1, "abc"),
    TEXT_SSSI("Replace", "abc", "b", "b", -1, "abc"),
    TEXT_SSSI("Replace", "abc", "b", "x", 0, "abc"),
    TEXT_SSSI("Replace", "abc", "abc", "", -1, ""),
    TEXT_SSSI("Replace", "50%", "%", "{}", -1, "50{}"),
    TEXT_SSS("ReplaceAll", "a.b.c", ".", "::", "a::b::c"),
    TEXT_SSI("Remove", "abcabc", "b", 1, "acabc"),
    TEXT_SS("RemoveAll", "abcabc", "b", "acac"),
};
/* Kinds: 0 Split, 1 SplitN, 2 SplitAfter, 3 SplitAfterN, 4 SplitLines,
 * 5 SplitLinesN, 6 SplitLinesAfter, 7 SplitLinesAfterN, 8 Concatenate of
 * Split, 9 Fields. */
static const call_case split_cases[] = {
    SPLIT(0, "aaa.bbb.ccc", ".", 0, "3:aaa|bbb|ccc"),
    SPLIT(0, "a,b,", ",", 0, "3:a|b|"),
    SPLIT(0, "", ",", 0, "1:"),
    SPLIT(0, "abc", "", 0, "3:a|b|c"),
    SPLIT(0, "", "", 0, "0:"),
    SPLIT(0, "abc", "x", 0, "1:abc"),
    SPLIT(0, "a--b--c", "--", 0, "3:a|b|c"),
    SPLIT(0, "aaa", "aa", 0, "2:|a"),
    SPLIT(1, "aaa.bbb.ccc.ddd", ".", 2, "2:aaa|bbb.ccc.ddd"),
    SPLIT(1, "a.b", ".", 0, "0:"),
    SPLIT(1, "a.b.c", ".", -1, "3:a|b|c"),
    SPLIT(1, "a.b", ".", 10, "2:a|b"),
    SPLIT(1, "abc", "", 2, "2:a|bc"),
    SPLIT(1, "abc", "", 0, "0:"),
    SPLIT(2, "a.b.c", ".", 0, "3:a.|b.|c"),
    SPLIT(2, "a.", ".", 0, "2:a.|"),
    SPLIT(2, "abc", "", 0, "3:a|b|c"),
    SPLIT(3, "a.b.c", ".", 2, "2:a.|b.c"),
    SPLIT(4, "a\r\nb\nc\r", "", 0, "3:a|b|c"),
    SPLIT(4, "a\n", "", 0, "2:a|"),
    SPLIT(4, "", "", 0, "1:"),
    SPLIT(5, "a\nb\nc", "", 2, "2:a|b\nc"),
    SPLIT(6, "a\r\nb", "", 0, "2:a\r\n|b"),
    SPLIT(7, "a\nb\nc", "", 2, "2:a\n|b\nc"),
    SPLIT(8, "a,b,c", ",", 0, "abc"),
    SPLIT(8, "", ",", 0, ""),
    SPLIT(9, "  foo bar  baz   ", "", 0, "3:foo|bar|baz"),
    SPLIT(9, "", "", 0, "0:"),
    SPLIT(9, " \t\n", "", 0, "0:"),
    SPLIT(9, "a\vb\fc", "", 0, "3:a|b|c"),
    SPLIT(
        9,
        "x\xa0"
        "y",
        "",
        0,
        "1:x\xa0"
        "y"
    ),
    TEXT_SS("Partition", "testing this out", " ", "testing| |this out"),
    TEXT_SS("Partition", "abc", "x", "abc||"),
    TEXT_SS("Partition", "abc", "", "||abc"),
    TEXT_SS("Partition", "a=b=c", "=", "a|=|b=c"),
};
TEST_CASE(package_core_strings_search_matches_odin)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        REQUIRE(run_cases("core:strings", CASES(search_cases), backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(package_core_strings_case_trim_cut_and_repeat_match_odin)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        REQUIRE(run_cases("core:strings", CASES(text_cases), backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(package_core_strings_split_join_and_replace_match_odin)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        REQUIRE(run_cases("core:strings", CASES(replace_cases), backends[b]));
        REQUIRE(run_cases("project:app", CASES(split_cases), backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(package_core_pure_exports_and_generic_native_arrays)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        const authoring_package *core = package(0, "core:strings"),
                                *app = package(0, "project:app");
        REQUIRE(core && app);
        int functions = 0, types = 0;
        for (int i = 0; i < core->exports->member_count; i++) {
            const script_package_export *member = &core->exports->members[i];
            if (member->kind == SCRIPT_PACKAGE_TYPE) {
                types++;
                continue;
            }
            REQUIRE(member->kind == SCRIPT_PACKAGE_FUNCTION);
            const script_catalog_capability *signature = script_catalog_capability_by_id(
                core->exports->metadata,
                member->id
            );
            REQUIRE(signature && signature->pure && !signature->effects);
            functions++;
        }
        /* Odin keeps its two splitters private. */
        REQUIRE(functions == 49 && types == 1 && core->exports->private_count == 2);
        REQUIRE(invoke(app->module, exported(app, "Check"), NULL, 0, PIN_I32, backends[b]));
        REQUIRE(output.i == 3);
        cleanup();
    }
    return true;
}
TEST_CASE(package_core_inputs_modules_and_binding_generations)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        REQUIRE(prepare(1, 0, 0, backends[b]));
        REQUIRE(authoring_packages_reused(owners[1]) == 2);
        REQUIRE(table(1) && prepare(2, 1, 1, backends[b]));
        REQUIRE(
            native_capabilities_generation(tables[0]) !=
            native_capabilities_generation(tables[1])
        );
        REQUIRE(authoring_packages_reused(owners[2]) == 0);
        REQUIRE(
            package(1, "core:strings")->generation != package(2, "core:strings")->generation
        );
        const authoring_package *app = package(0, "project:app");
        REQUIRE(app);
        uint64_t id = exported(app, "Check");
        retained = app->module;
        execution_module_retain(retained);
        for (int i = 0; i < 3; i++) {
            authoring_packages_free(owners[i]);
            owners[i] = NULL;
        }
        REQUIRE(invoke(retained, id, NULL, 0, PIN_I32, backends[b]));
        REQUIRE(output.i == 3);
        cleanup();
    }
    return true;
}
void test_language_package_core()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_core_strings_bytes_bounds_and_owned_slices);
    RUN_TEST_CASE(package_core_strings_comparison_identifiers_and_basename);
    RUN_TEST_CASE(package_core_strings_search_matches_odin);
    RUN_TEST_CASE(package_core_strings_case_trim_cut_and_repeat_match_odin);
    RUN_TEST_CASE(package_core_strings_split_join_and_replace_match_odin);
    RUN_TEST_CASE(package_core_pure_exports_and_generic_native_arrays);
    RUN_TEST_CASE(package_core_inputs_modules_and_binding_generations);
    pu_clear_setup();
}
