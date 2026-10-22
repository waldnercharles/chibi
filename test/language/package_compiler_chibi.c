#include "package_compiler_chibi.h"

#include "backend.h"
#include "compiler_fixture.h"
#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/module_internal.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "language/lower.h"
#include "package_compiler_chibi_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/native_capabilities.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/copy.h"
#include "types/map.h"
#include "types/text.h"
#include "types/union.h"
#include "types/value_document.h"

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The authored package compiler, prepared by stage zero, compiles each sample
 * package, with the packages it imports, to a closure against the natives the
 * compiler host links. A fresh runtime host loads it and calls every function
 * on bytecode and SLJIT, and stage zero's own closure of the same packages
 * must give the same JSON result. Rejections report their exact message and
 * position. */
static const char PACKAGE[] = "project:package_compiler";
enum
{
    CALLS = 12,
    SAMPLE_INPUTS = 4,
    ARGUMENTS = 12,
    /* Compile's debug flag, then the six that describe the target. */
    DEBUG_ARGUMENT = 5,
    TARGET_ARGUMENT = 6,
    COMPILE_BUDGET = 20000000,
    SELF_BUDGET = 2000000000,
    TARGET_BUDGET = 100000
};
/* A NULL expected result means the call must fail. */
typedef struct
{
    const char *function, *arguments, *expected;
    const char *diagnostic;
} sample_call;
/* Each group of samples runs under its own selector, so each selector stays
 * within its time ceiling: unions, constants, patterns and integer types
 * (TYPE_SAMPLES), and the imports sample, which compiles all of core:strings,
 * with the samples that link the core pack, whose oracle costs seconds more
 * to prepare, and the moves, reads in place and array methods
 * (IMPORT_SAMPLES, run on the unsanitized Release build); and maps,
 * references, arrays and ranges (COLLECTION_SAMPLES). */
typedef enum
{
    BASE_SAMPLES,
    TYPE_SAMPLES,
    IMPORT_SAMPLES,
    COLLECTION_SAMPLES
} sample_group;
/* `source` is package facts, and `more` another file of it when present;
 * `library` is package lib when present, and `core` adds core:strings. A
 * `game` sample's target links the core pack a game links, which declares
 * assert; preparing every sample with it would cost each selector seconds. An
 * `updates` sample's `Build(count)` updates a local array in place: doubling
 * its count at most doubles the values it copies, where copying the array on
 * each update would quadruple them. A `boxed` sample's Boxed functions read
 * only arrays that are never packed, and its Packed functions arrays that may
 * be. A `moves` sample calls no native: its Moved functions take fresh values
 * into locals and returns without copying them, and its Copied function copies
 * an array's element. An `aliases` sample's Aliased functions read records and
 * arrays nothing changes in place, and its CopiedWalk copies the array its loop
 * changes. A `variants` sample calls no native: its Kept functions return
 * variants that keep their payload values, and its Copied function builds one
 * it keeps after a change. A `built` sample calls no native: its Built
 * functions build the arrays they return in their caller's storage, and its
 * Copied function, which assigns its array, does not. */
typedef struct
{
    const char *name, *source, *more, *library;
    sample_group group;
    bool core, updates, boxed, game, moves, aliases, variants, built,
        same_rejection, c_string_probe;
    sample_call calls[CALLS];
    const char *message;
    const char *oracle_message;
    int line, column;
} sample;
static const char *cstring_probe(arena *storage, const pin_value *arguments, int, pin_value *result)
{
    const char *value = NULL;
    size_t nul = 0;
    const char *error = pin_text_cstring(storage, arguments[0], &value, &nul);
    if (error) {
        return error;
    }
    result->i = (int)strlen(value);
    return NULL;
}
static const native_parameter_definition CSTRING_INPUT[] = {
    { .name = "value",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .c_string = true },
};
static const native_parameter_definition CSTRING_OUTPUT[] = {
    { .name = "length", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
};
static const native_capability_definition CSTRING_DEFINITIONS[] = {
    { .canonical_name = "CString Probe",
      .source_name = "cstring_probe",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = CSTRING_INPUT,
      .input_count = 1,
      .outputs = CSTRING_OUTPUT,
      .output_count = 1,
      .completion_output = -1,
      .pure = true },
};
static const node_host_call CSTRING_CALL = { .value = cstring_probe };
static const node_access CSTRING_ACCESS = { .flags = NODE_EFFECT_PURE };
static const native_definition_binding CSTRING_BINDINGS[] = {
    { .canonical_name = "CString Probe",
      .implementation = { .call = &CSTRING_CALL, .access = &CSTRING_ACCESS } },
};
static const native_definition_pack CSTRING_PACK = {
    .definitions = CSTRING_DEFINITIONS,
    .definition_count = 1,
    .bindings = CSTRING_BINDINGS,
    .binding_count = 1,
};
static const sample samples[] = {
    { .name = "typed constant shifts discard bits",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Sign: int : 1 << 31;\n"
                "Zero: byte : 128 << 1;\n"
                "Small: int8 : 64 << 1;\n"
                "Unsigned: uint : 4294967295 << 1;\n"
                "Floor: int : -3 >> 1;\n"
                "Wide: int64 : 1 << 63;\n"
                "ReadSign :: () -> int { return Sign; }\n"
                "ReadZero :: () -> byte { return Zero; }\n"
                "ReadSmall :: () -> int8 { return Small; }\n"
                "ReadUnsigned :: () -> uint { return Unsigned; }\n"
                "ReadFloor :: () -> int { return Floor; }\n"
                "ReadWide :: () -> int64 { return Wide; }",
      .calls = { { .function = "ReadSign", .arguments = "[]", .expected = "[-2147483648]" },
                 { .function = "ReadZero", .arguments = "[]", .expected = "[0]" },
                 { .function = "ReadSmall", .arguments = "[]", .expected = "[-128]" },
                 { .function = "ReadUnsigned", .arguments = "[]", .expected = "[4294967294]" },
                 { .function = "ReadFloor", .arguments = "[]", .expected = "[-2]" },
                 { .function = "ReadWide",
                   .arguments = "[]",
                   .expected = "[-9223372036854775808]" } } },
    { .name = "constant negative shift count",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: int : 1 << -1;",
      .message = "Constant shift count is outside the type",
      .line = 2,
      .column = 12 },
    { .name = "constant shift count at width",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: int : 1 >> 32;",
      .message = "Constant shift count is outside the type",
      .line = 2,
      .column = 12 },
    { .name = "private imported function",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { return lib.Hidden(); }",
      .library = "package lib\nHidden :: () -> int { return 7; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported constant",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { return lib.Hidden; }",
      .library = "package lib\nHidden :: 7;",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported constant expression",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\nValue :: "
                "lib.Hidden + 1;",
      .library = "package lib\nHidden :: 7;",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported record local",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { value: lib.Hidden = lib.Make(); return "
                "value.x; }",
      .library = "package lib\nHidden :: struct { x: int; }\n"
                 "pub Make :: () -> Hidden { return {x: 7}; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported record parameter",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: (value: lib.Hidden) -> int { return value.x; }",
      .library = "package lib\nHidden :: struct { x: int; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported record result",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> lib.Hidden { return lib.Make(); }",
      .library = "package lib\nHidden :: struct { x: int; }\n"
                 "pub Make :: () -> Hidden { return {x: 7}; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported union payload type",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Wrapper :: union { Some(value: lib.Hidden) }",
      .library = "package lib\nHidden :: struct { x: int; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported map value type",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { values: [int: lib.Hidden] = [:]; return 0; "
                "}",
      .library = "package lib\nHidden :: struct { x: int; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported union constructor",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { value := lib.Hidden.Some(7); return 0; }",
      .library = "package lib\nHidden :: union { Some(value: int) | None }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .oracle_message = "Unknown callable 'lib.Hidden.Some'",
      .same_rejection = true },
    { .name = "private imported union value",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { value := lib.Hidden.None; return 0; }",
      .library = "package lib\nHidden :: union { Some(value: int) | None }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private package implementation",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { return Helper(3) + lib.Value(3) + "
                "lib.Make(3).x; }\n"
                "Shadow :: (lib: Local) -> int { return lib.Hidden; }\n"
                "Local :: struct { Hidden: int; }",
      .more = "package facts\nHelper :: (value: int) -> int { return value + "
              "1; }",
      .library = "package lib\nHidden :: 7;\n"
                 "Secret :: struct { x: int; }\n"
                 "Twice :: (value: int) -> int { return value * 2; }\n"
                 "pub Value :: (value: int) -> int { return Twice(value) + "
                 "Hidden; }\n"
                 "pub Make :: (value: int) -> Secret { return {x: value}; }",
      .calls = { { .function = "One", .arguments = "[]", .expected = "[20]" },
                 { .function = "Shadow", .arguments = "[{\"Hidden\":11}]", .expected = "[11]" } } },
    { .name = "constant",
      .source = "package facts\nAnswer :: () -> int { return 42; }",
      .calls = { { .function = "Answer", .arguments = "[]", .expected = "[42]" } } },
    { .name = "named results",
      .source = "package facts\nPair :: (x: int) -> (kind: int, end: int) {\n  "
                "  end = x + 1;\n"
                "    return (x, end);\n}\nRest :: (x: int) -> (low: int, high: "
                "int) {\n    low = x - 1;\n    return;\n}\n"
                "Use :: (x: int) -> int {\n    p := Pair(x);\n    q := "
                "Rest(x);\n"
                "    return p.kind * 10 + p.end + Pair(x).end * 100 + q.low * "
                "1000 + q.high;\n}",
      .calls = { { .function = "Use", .arguments = "[3]", .expected = "[2434]" } } },
    { .name = "parameters",
      .source = "package facts\nAdd :: (a: int, b: int) -> int { return "
                "a + b * 2 - (a - b) % 3; }",
      .calls = { { .function = "Add", .arguments = "[3,4]", .expected = "[12]" },
                 { .function = "Add", .arguments = "[-5,2]", .expected = "[0]" } } },
    { .name = "loops",
      .source = "package facts\nSum :: (n: int) -> int {\n"
                "    total: int = 0;\n    i: int = 0;\n"
                "    while (i < n) { total += i; i += 1; }\n    return "
                "total;\n}",
      .calls = { { .function = "Sum", .arguments = "[10]", .expected = "[45]" },
                 { .function = "Sum", .arguments = "[0]", .expected = "[0]" } } },
    { .name = "recursion",
      .source = "package facts\nFib :: (n: int) -> int {\n"
                "    if (n < 2) { return n; }\n    return Fib(n - 1) + Fib(n - "
                "2);\n}",
      .calls = { { .function = "Fib", .arguments = "[15]", .expected = "[610]" } } },
    { .name = "control",
      .source = "package facts\nCount :: (n: int) -> int {\n"
                "    found: int = 0;\n    i: int = 0;\n"
                "    while (true) {\n        i += 1;\n        if (i > n) { "
                "break; }\n"
                "        if (i % 3 == 0 || i % 5 == 0) { continue; } else { "
                "found += 1; }\n    }\n"
                "    return found;\n}",
      .calls = { { .function = "Count", .arguments = "[15]", .expected = "[8]" } } },
    { .name = "logic",
      .source = "package facts\nEven :: (n: int) -> bool { return !(n % "
                "2 != 0) && n >= 0; }\n"
                "Both :: (a: int, b: int) -> bool { return Even(a) && "
                "Even(b) || a == b; }",
      .calls = { { .function = "Even", .arguments = "[4]", .expected = "[true]" },
                 { .function = "Even", .arguments = "[-4]", .expected = "[false]" },
                 { .function = "Both", .arguments = "[2,4]", .expected = "[true]" },
                 { .function = "Both", .arguments = "[3,3]", .expected = "[true]" } } },
    { .name = "records",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; title: string; open: bool; "
                "}\n"
                "Make :: (x: int, y: int) -> Point {\n"
                "    p: Point = {x: x, y: y, title: \"p\", open: x < y};\n"
                "    return p;\n}\n"
                "Sum :: (p: Point) -> int { return p.x + p.y; }\n"
                "Moved :: (p: Point, dx: int) -> Point {\n"
                "    return {title: p.title, open: p.open, y: p.y, x: p.x + "
                "dx};\n}\n"
                "Total :: (n: int) -> int {\n"
                "    acc: Point = {x: 0, y: 0, title: \"\", open: false};\n"
                "    i: int = 0;\n"
                "    while (i < n) { acc = Moved(acc, i); i += 1; }\n"
                "    first := acc;\n    acc = Moved(acc, 100);\n"
                "    return Sum(acc) + Sum(Make(1, 2)) + first.x;\n}",
      .calls = { { .function = "Make", .arguments = "[1,2]", .expected = "[{\"x\":1,\"y\":2,\"title\":\"p\",\"open\":true}]" },
                 { .function = "Moved", .arguments = "[{\"x\":1,\"y\":2,\"title\":\"q\",\"open\":false},5]", .expected = "[{\"x\":6,\"y\":2,\"title\":\"q\",\"open\":false}]" },
                 { .function = "Total", .arguments = "[5]", .expected = "[123]" } } },
    { .name = "moved values",
      .group = IMPORT_SAMPLES,
      .moves = true,
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; }\n"
                "MovedNote :: (n: int) -> Note { return {text: \"a\", count: "
                "n}; }\n"
                "MovedResults :: () -> int {\n"
                "    a := MovedNote(1);\n    b := MovedNote(1);\n"
                "    a.count = 10;\n    b = MovedNote(2);\n"
                "    return a.count + b.count;\n}\n"
                "MovedList :: (n: int) -> int[] { return [n, n + 1]; }\n"
                "MovedSecond :: (n: int) -> int { xs := MovedList(n); return "
                "xs[1]; }\n"
                "Copied :: (notes: Note[]) -> int {\n"
                "    note := notes[0];\n    note.count += 5;\n"
                "    return note.count + notes[0].count;\n}",
      .calls = { { .function = "MovedNote", .arguments = "[3]", .expected = "[{\"text\":\"a\",\"count\":3}]" },
                 { .function = "MovedResults", .arguments = "[]", .expected = "[12]" },
                 { .function = "MovedSecond", .arguments = "[4]", .expected = "[5]" },
                 { .function = "Copied",
                   .arguments = "[[{\"text\":\"x\",\"count\":1}]]",
                   .expected = "[7]" } } },
    { .name = "returned variants",
      .group = IMPORT_SAMPLES,
      .variants = true,
      .source =
          "package facts\n"
          "Holder :: union { Some(items: int[], name: string) }\n"
          "KeptMake :: (n: int) -> Holder {\n"
          "    xs: int[] = [];\n    i: int = 0;\n"
          "    while (i < n) { xs.append(i); i += 1; }\n"
          "    return (Holder.Some(xs, \"made\"));\n}\n"
          "KeptAgain :: (h: Holder) -> Holder {\n"
          "    switch h { case .Some(items, name): return Holder.Some(items, "
          "name); }\n}\n"
          "KeptParam :: (xs: int[]) -> Holder { return Holder.Some(xs, \"p\"); "
          "}\n"
          "Total :: () -> int {\n"
          "    a := KeptMake(3);\n    b := KeptMake(5);\n    c := "
          "KeptAgain(a);\n"
          "    total: int = 0;\n"
          "    switch a { case .Some(items, name): total += items[2] * 100; }\n"
          "    switch b { case .Some(items, name): total += items[4] * 10; }\n"
          "    switch c { case .Some(items, name): total += items[1]; }\n"
          "    return total;\n}\n"
          "Changed :: () -> int {\n"
          "    ys: int[] = [1, 2];\n    h := KeptParam(ys);\n    ys[0] = 7;\n"
          "    switch h { case .Some(items, name): return items[0] * 10 + "
          "ys[0]; }\n}\n"
          "CopiedHold :: (xs: int[]) -> int {\n"
          "    h := Holder.Some(xs, \"x\");\n    xs[0] = 9;\n"
          "    switch h { case .Some(items, name): return items[0]; }\n}",
      .calls = { { .function = "Total", .arguments = "[]", .expected = "[241]" },
                 { .function = "Changed", .arguments = "[]", .expected = "[17]" },
                 { .function = "CopiedHold", .arguments = "[[5,6]]", .expected = "[5]" } } },
    { .name = "built results",
      .group = IMPORT_SAMPLES,
      .built = true,
      .source = "package facts\n"
                "Holder :: union { Some(items: int[], name: string) }\n"
                "BuiltCount :: (n: int) -> int[] {\n"
                "    xs: int[] = [];\n    i: int = 0;\n"
                "    while (i < n) { xs.append(i); i += 1; }\n"
                "    return xs;\n}\n"
                "BuiltPair :: (n: int, ys: int[]) -> Holder {\n"
                "    xs: int[] = [7];\n    xs.extend(BuiltCount(n));\n    "
                "xs[0] = 9;\n"
                "    if (n == 0) { return Holder.Some(ys, \"none\"); }\n"
                "    return Holder.Some(xs, \"pair\");\n}\n"
                "CopiedCount :: (n: int) -> int[] {\n"
                "    xs: int[] = [];\n    xs = BuiltCount(n);\n    return "
                "xs;\n}\n"
                "Total :: () -> int {\n"
                "    a := BuiltCount(3);\n    ys: int[] = [4, 5];\n"
                "    b := BuiltPair(2, ys);\n    c := BuiltPair(0, ys);\n    "
                "ys[0] = 6;\n"
                "    total: int = a[2] * 1000 + CopiedCount(4)[3] * 100;\n"
                "    switch b { case .Some(items, name): total += items[0] * "
                "10 + items[2]; }\n"
                "    switch c { case .Some(items, name): total += items[0] * "
                "10000; }\n"
                "    return total;\n}",
      .calls = { { .function = "Total", .arguments = "[]", .expected = "[42391]" } } },
    { .name = "read in place",
      .group = IMPORT_SAMPLES,
      .aliases = true,
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; }\n"
                "AliasedSum :: (notes: Note[]) -> int {\n"
                "    total: int = 0;\n"
                "    for (note in notes) { total += note.count; }\n"
                "    first := notes[0];\n    return total + first.count * "
                "100;\n}\n"
                "CopiedWalk :: (notes: Note[]) -> int {\n"
                "    xs := notes;\n    total: int = 0;\n"
                "    for (note in xs) { xs[1] = {text: \"z\", count: 100}; "
                "total += note.count; }\n"
                "    return total;\n}",
      .calls = { { .function = "AliasedSum",
                   .arguments = "[[{\"text\":\"a\",\"count\":1},{\"text\":"
                                "\"b\",\"count\":2}]]",
                   .expected = "[103]" },
                 { .function = "CopiedWalk",
                   .arguments = "[[{\"text\":\"x\",\"count\":1},{\"text\":"
                                "\"y\",\"count\":2}]]",
                   .expected = "[3]" } } },
    { .name = "array methods",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Build :: (n: int) -> int[] {\n"
                "    xs: int[] = [];\n    i: int = 0;\n"
                "    while (i < n) { xs.append(i); i += 1; }\n"
                "    ys: int[] = [100, 200];\n"
                "    xs.extend(ys);\n    seven: int = 7;\n    xs.insert(0, "
                "seven);\n    xs.reverse();\n"
                "    return xs;\n}\n"
                "Count :: (xs: int[]) -> int { return xs.length() * 10 + "
                "Build(2).length(); }\n"
                "Has :: (xs: int[], x: int) -> bool { return xs.contains(x); "
                "}\n"
                "Sliced :: (xs: int[]) -> int[] { part := xs.slice(1, 2); "
                "return part; }\n"
                "Separate :: () -> int { xs: int[] = [1, 2]; ys := xs; "
                "ys.append(3); return xs.length(); }\n"
                "Grow :: (xs: int[]) -> int[] { xs.append(1); return xs; }",
      .calls = { { .function = "Build", .arguments = "[3]", .expected = "[[200,100,2,1,0,7]]" },
                 { .function = "Count", .arguments = "[[1,2,3]]", .expected = "[35]" },
                 { .function = "Has", .arguments = "[[1,2],2]", .expected = "[true]" },
                 { .function = "Sliced", .arguments = "[[5,6,7,8]]", .expected = "[[6,7]]" },
                 { .function = "Separate", .arguments = "[]", .expected = "[2]" },
                 { .function = "Grow", .arguments = "[[5]]", .expected = "[[5,1]]" } } },
    { .name = "array method in an expression",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nBuild :: () -> int[] { xs: int[] = []; xs = "
                "xs.append(1); return xs; }",
      .message = "append changes the array; call it as a statement on a name",
      .line = 2,
      .column = 45 },
    { .name = "strings",
      .source = "package facts\n"
                "Name :: (name: string) -> string {\n"
                "    if (name == \"\") { return \"nobody\"; }\n    return "
                "name;\n}\n"
                "Same :: (a: string, b: string) -> bool { return a == b "
                "&& !(a != b); }\n"
                "Differ :: (a: string, b: string) -> bool { return a != "
                "b; }\n"
                "Quoted :: () -> string { text: string = "
                "\"a\\\"b\\u0041\"; "
                "return text; }",
      .calls = { { .function = "Name", .arguments = "[\"\"]", .expected = "[\"nobody\"]" },
                 { .function = "Name", .arguments = "[\"ada\"]", .expected = "[\"ada\"]" },
                 { .function = "Same", .arguments = "[\"x\",\"x\"]", .expected = "[true]" },
                 { .function = "Same", .arguments = "[\"x\",\"y\"]", .expected = "[false]" },
                 { .function = "Differ", .arguments = "[\"x\",\"y\"]", .expected = "[true]" },
                 { .function = "Quoted", .arguments = "[]", .expected = "[\"a\\\"bA\"]" } } },
    /* Records and union payloads hold float and double values; an integer
     * stored there converts as stage zero converts it. */
    { .name = "floating fields",
      .source = "package facts\n"
                "V :: struct { x: float; y: double; n: int; }\n"
                "Shape :: union { Circle(radius: double) | Dot }\n"
                "Make :: (x: float, n: int) -> double {\n"
                "    v: V = {x: x, y: 2.5, n: n};\n    v.y += v.x;\n    v.x = "
                "n;\n"
                "    return v.y + v.x;\n}\n"
                "Area :: (r: double) -> double {\n"
                "    switch Shape.Circle(r) { case .Circle(radius): return "
                "radius * radius; case .Dot: return 0.0; }\n"
                "}\n",
      .calls = { { .function = "Make", .arguments = "[1.5,3]", .expected = "[7.0]" },
                 { .function = "Area", .arguments = "[1.5]", .expected = "[2.25]" } } },
    /* int8, int16, uint16 and uint as stage zero computes them: mixed
     * operands widen to the type that holds both, and records hold them. */
    { .name = "narrow integers",
      .source = "package facts\n"
                "P :: struct { a: int8; b: uint16; }\n"
                "Narrow :: (a: int8, b: uint16, c: uint, d: int16) -> int64 {\n"
                "    x: int8 = a - 1;\n    y: uint = c / 2;\n"
                "    return int64(x) + int64(b) * 2 + int64(y) - int64(d);\n}\n"
                "Mixed :: (a: int8, b: byte) -> int { return a + b; }\n"
                "Big :: (a: uint, b: int) -> int64 { return a + b; }\n"
                "Store :: () -> uint16 { v: uint16 = 65535; return v; }\n"
                "Make :: (a: int8) -> int { p: P = {a: a, b: 7}; return "
                "int(p.a) + int(p.b); }\n",
      .calls = { { .function = "Narrow", .arguments = "[-5,300,9,-7]", .expected = "[605]" },
                 { .function = "Mixed", .arguments = "[-3,200]", .expected = "[197]" },
                 { .function = "Big", .arguments = "[4000000000,-1]", .expected = "[3999999999]" },
                 { .function = "Store", .arguments = "[]", .expected = "[65535]" },
                 { .function = "Make", .arguments = "[-2]", .expected = "[5]" } } },
    /* uint64 and byte have every operator, unsigned ordering and division
     * included, as stage zero computes them. */
    { .name = "unsigned arithmetic",
      .source =
          "package facts\n"
          "Diff :: (a: uint64, b: uint64) -> uint64 { return a - b; }\n"
          "Less :: (a: uint64, b: uint64) -> bool { return a < b; }\n"
          "Div :: (a: uint64, b: uint64) -> uint64 { return a / b + a % b; }\n"
          "Bytes :: (a: byte, b: byte) -> byte { return a + b * 2 - 1; }\n"
          "Order :: (a: byte, b: byte) -> bool { return a > b && a >= 3; }\n"
          "Shift :: (a: byte) -> byte { return (a << 1) >> 2; }\n",
      .calls = { { .function = "Diff", .arguments = "[10,3]", .expected = "[7]" },
                 { .function = "Less", .arguments = "[9223372036854775808,1]", .expected = "[false]" },
                 { .function = "Less", .arguments = "[1,9223372036854775808]", .expected = "[true]" },
                 { .function = "Div",
                   .arguments = "[18446744073709551615,10]",
                   .expected = "[1844674407370955166]" },
                 { .function = "Bytes", .arguments = "[20,30]", .expected = "[79]" },
                 { .function = "Order", .arguments = "[5,3]", .expected = "[true]" },
                 { .function = "Shift", .arguments = "[12]", .expected = "[6]" } } },
    /* Constants fold integer expressions of literals and other constants,
     * declared in any order and in an imported package. */
    { .name = "constant expressions",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "import lib \"project:lib\"\n"
                "Area :: Width * Height;\n"
                "Width :: (Unit << 2) + 1;\n"
                "Height: int : -(Width - 7) / 2 % 3;\n"
                "Mask: int64 : ~lib.Bits & 0xFFFF;\n"
                "Small: byte : 0x10 | 2;\n"
                "Low: byte : ~0x0F;\n"
                "Unit :: 2;\n"
                "Folded :: () -> int64 { return int64(Area) * 1000 + Mask + "
                "int64(Small) + int64(Low); }\n",
      .library = "package lib\npub Bits: int64 : 1 << 40 | 0xF0;\n",
      .calls = { { .function = "Folded", .arguments = "[]", .expected = "[56553]" } } },
    /* An omitted argument takes its parameter's literal default. */
    { .name = "defaults",
      .source = "package facts\n"
                "Add :: (a: int, b: int = -10, c: int64 = 0x10) -> int64 { "
                "return int64(a + b) + c; }\n"
                "Greet :: (name: string = \"you\", loud: bool = true) -> "
                "string {\n"
                "    if (loud) { return format_text(\"HI {}\", name); }\n"
                "    return name;\n}\n"
                "Use :: () -> int64 { return Add(1) + Add(1, 2) + Add(1, 2, "
                "3); }\n"
                "Hello :: () -> string { return Greet(); }\n",
      .calls = { { .function = "Add", .arguments = "[5,6,7]", .expected = "[18]" },
                 { .function = "Use", .arguments = "[]", .expected = "[32]" },
                 { .function = "Hello", .arguments = "[]", .expected = "[\"HI you\"]" },
                 { .function = "Greet", .arguments = "[\"ada\",false]", .expected = "[\"ada\"]" } } },
    /* An integer switch tests its arms in order and runs the default arm
     * after them all, wherever it is written. */
    { .name = "integer switch",
      .source = "package facts\n"
                "Kind :: (c: int) -> int {\n"
                "    switch c {\n"
                "        case 61: return 1;\n"
                "        case: return 0;\n"
                "        case -62: return 2;\n"
                "    }\n"
                "}\n"
                "Spaces :: (xs: int[]) -> int {\n"
                "    total: int = 0;\n"
                "    for x in xs {\n"
                "        b := byte(x);\n"
                "        switch b { case 32: total += 1; case 10: total += "
                "100; }\n"
                "    }\n"
                "    return total;\n}\n",
      .calls = { { .function = "Kind", .arguments = "[61]", .expected = "[1]" },
                 { .function = "Kind", .arguments = "[-62]", .expected = "[2]" },
                 { .function = "Kind", .arguments = "[7]", .expected = "[0]" },
                 { .function = "Spaces", .arguments = "[[32,10,32,5]]", .expected = "[102]" } } },
    /* Range loops count as stage zero's do: inclusive and half-open,
     * forward and reversed, ending at a bound without wrapping; `for x, i`
     * names an array's item and its index, and `for <` walks it backwards. */
    { .name = "ranges",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Sum :: (first: int, last: int) -> int {\n"
                "    total: int = 0;\n    for i in first..last { total += i; "
                "}\n"
                "    return total;\n}\n"
                "Below :: (n: int) -> int {\n"
                "    total: int = 0;\n"
                "    for i in 0..<n { if (i == 2) { continue; } total += i; }\n"
                "    return total;\n}\n"
                "Down :: (n: int) -> int {\n"
                "    total: int = 0;\n"
                "    for < i in 0..n { total = total * 10 + i; if (i == 1) { "
                "break; } }\n"
                "    return total;\n}\n"
                "DownBelow :: (n: int) -> int {\n"
                "    total: int = 0;\n    for < i in 0..<n { total = total * "
                "10 + i; }\n"
                "    return total;\n}\n"
                "Count :: (first: int, last: int) -> int {\n"
                "    count: int = 0;\n    for i in first..last { count += 1; "
                "}\n"
                "    for < i in first..last { count += 1; }\n"
                "    return count;\n}\n"
                "Small :: (b: byte) -> int {\n"
                "    total: int = 0;\n    for i in 0..b { total += 1; }\n"
                "    return total;\n}\n"
                "Keys :: (xs: int[]) -> int {\n"
                "    total: int = 0;\n    for x, i in xs { total += i * x; }\n"
                "    return total;\n}\n"
                "Back :: (xs: int[]) -> int {\n"
                "    total: int = 0;\n"
                "    for < x, i in xs { total = total * 100 + x * 10 + i; }\n"
                "    return total;\n}\n",
      .calls = { { .function = "Sum", .arguments = "[1,4]", .expected = "[10]" },
                 { .function = "Below", .arguments = "[5]", .expected = "[8]" },
                 { .function = "Down", .arguments = "[3]", .expected = "[321]" },
                 { .function = "DownBelow", .arguments = "[3]", .expected = "[210]" },
                 { .function = "DownBelow", .arguments = "[0]", .expected = "[0]" },
                 { .function = "Count", .arguments = "[2147483646,2147483647]", .expected = "[4]" },
                 { .function = "Count", .arguments = "[-2147483648,-2147483647]", .expected = "[4]" },
                 { .function = "Small", .arguments = "[3]", .expected = "[4]" },
                 { .function = "Keys", .arguments = "[[5,6,7]]", .expected = "[20]" },
                 { .function = "Back", .arguments = "[[1,2,3]]", .expected = "[322110]" } } },
    { .name = "arrays",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Squares :: (n: int) -> int[] {\n"
                "    out: int[] = [];\n    i: int = 0;\n"
                "    while (i < n) { out = append(out, i * i); i += 1; }\n"
                "    return out;\n}\n"
                "Total :: (xs: int[]) -> int {\n"
                "    sum: int = 0;\n"
                "    for (x in xs) { if (x == 4) { continue; } sum += x; "
                "}\n"
                "    return sum;\n}\n"
                "Edit :: () -> int {\n"
                "    xs: int[] = [1, 2, 3];\n    xs[1] = 20;\n    xs[2] += 5;\n"
                "    copy := xs;\n    more: int[] = [7, 8];\n    xs = "
                "extend(xs, more);\n"
                "    return xs[1] + xs[2] + length(xs) * 100 + length(copy) * "
                "1000;\n}\n"
                "Points :: (n: int) -> Point[] {\n"
                "    ps: Point[] = [];\n    i: int = 0;\n"
                "    while (i < n) { p: Point = {x: i, y: i * 2}; ps = "
                "append(ps, p); i += 1; }\n"
                "    return ps;\n}\n"
                "SumY :: (ps: Point[]) -> int {\n"
                "    total: int = 0;\n"
                "    for (p in ps) { total += p.y; if (total > 100) { "
                "break; } }\n"
                "    return total + ps[0].x;\n}\n"
                "Words :: () -> string[] {\n"
                "    ws: string[] = [\"a\", \"b\", \"c\"];\n    return "
                "slice(ws, 1, 2);\n}\n"
                "Counts :: (n: int) -> int { return length(Squares(n)) "
                "+ Total(Squares(n)); }\n"
                "Pick :: (xs: int[], i: int) -> int { return xs[i]; }\n"
                "Index :: (i: int) -> int { return i; }\n"
                "Nested :: (ps: Point[]) -> int {\n"
                "    total: int = 0;\n    ws: string[] = [\"a\", \"bb\"];\n"
                "    for (p in ps) {\n        if (p.x == 0) { continue; }\n"
                "        for (w in ws) { if (w == \"a\") { continue; } "
                "total += p.x * 10; }\n    }\n"
                "    counts: int[] = [0, 0, 0];\n    counts[Index(2)] += 7;\n"
                "    return total + counts[2];\n}",
      .calls = { { .function = "Squares", .arguments = "[4]", .expected = "[[0,1,4,9]]" },
                 { .function = "Total", .arguments = "[[0,1,4,9]]", .expected = "[10]" },
                 { .function = "Edit", .arguments = "[]", .expected = "[3528]" },
                 { .function = "Points", .arguments = "[3]", .expected = "[[{\"x\":0,\"y\":0},{\"x\":1,\"y\":2},{\"x\":2,\"y\":4}]]" },
                 { .function = "SumY",
                   .arguments = "[[{\"x\":5,\"y\":1},{\"x\":0,\"y\":2}]]",
                   .expected = "[8]" },
                 { .function = "Words", .arguments = "[]", .expected = "[[\"b\",\"c\"]]" },
                 { .function = "Counts", .arguments = "[3]", .expected = "[4]" },
                 { .function = "Pick", .arguments = "[[1,2],2]", .expected = NULL },
                 { .function = "Nested",
                   .arguments = "[[{\"x\":0,\"y\":1},{\"x\":2,\"y\":0},{\"x\":"
                                "3,\"y\":0}]]",
                   .expected = "[57]" } } },
    { .name = "maps",
      .group = COLLECTION_SAMPLES,
      .source =
          "package facts\n"
          "Point :: struct { x: int; y: int; }\n"
          "Ages :: () -> int {\n"
          "    ages: [string: int] = [\"Ada\": 36, \"Bea\": 41];\n"
          "    return ages[\"Ada\"] + ages[\"Cid\"] * 1000 + ages[\"Bea\"] * "
          "100;\n}\n"
          "Name :: (key: int) -> string {\n"
          "    names: [int: string] = [1: \"one\", 2: \"two\"];\n    return "
          "names[key];\n}\n"
          "Y :: (key: string) -> int {\n"
          "    ps: [string: Point] = [\"a\": {x: 1, y: 2}];\n    return "
          "ps[key].y;\n}\n"
          "Flag :: () -> bool { flags: [int: bool] = [:]; return flags[3]; }\n"
          "Count :: (n: int) -> int {\n"
          "    squares: [int: int] = [:];\n    i: int = 0;\n"
          "    while (i < n) { squares[i] = i * i; i += 1; }\n"
          "    delete squares[1];\n    squares.delete(2);\n    "
          "squares.delete(99);\n"
          "    found: int = 0;\n"
          "    if (0 in squares) { found += 1; }\n"
          "    if (1 not in squares) { found += 10; }\n"
          "    if (squares.has(3)) { found += 100; }\n"
          "    if (!squares.has(2)) { found += 1000; }\n"
          "    return found * 100000 + squares[3] * 1000 + squares[1];\n}\n"
          "Points :: () -> int {\n"
          "    ps: [string: Point] = [:];\n    ps[\"a\"] = {x: 3, y: 4};\n"
          "    p: Point = {x: 5, y: 6};\n    ps[\"b\"] = p;\n    p.x = 7;\n"
          "    return ps[\"a\"].y * 100 + ps[\"b\"].x;\n}\n"
          "Walk :: () -> string {\n"
          "    ages: [string: int] = [\"Ada\": 3, \"Bea\": 1, \"Cid\": 2];\n"
          "    ages[\"Ada\"] = 4;\n    delete ages[\"Bea\"];\n    "
          "ages[\"Bea\"] = 5;\n"
          "    out: string = \"\";\n    total: int = 0;\n"
          "    for (name, age in ages) { out = format_text(\"{}{}{}\", out, "
          "name, age); total += age; }\n"
          "    for (age in ages) { if (age == 2) { continue; } total += age * "
          "100; }\n"
          "    ps: [int: Point] = [1: {x: 2, y: 3}, 4: {x: 5, y: 6}];\n"
          "    for (key, p in ps) { total += key * 1000 + p.y * 10000; }\n"
          "    return format_text(\"{} {}\", out, total);\n}\n"
          "Make :: (n: int) -> [int: string] {\n"
          "    made: [int: string] = [:];\n    i: int = 0;\n"
          "    while (i < n) { made[i] = format_text(\"v{}\", i); i += 1; }\n  "
          "  return made;\n}\n"
          "Join :: (m: [int: string]) -> string {\n"
          "    out: string = \"\";\n    for (k, v in m) { out = "
          "format_text(\"{}{}={};\", out, k, v); }\n"
          "    return out;\n}\n"
          "Copies :: () -> string {\n"
          "    a := Make(3);\n    b := a;\n    b[0] = \"changed\";\n    delete "
          "b[2];\n"
          "    return format_text(\"{}|{}\", Join(a), Join(b));\n}",
      .calls = { { .function = "Ages", .arguments = "[]", .expected = "[4136]" },
                 { .function = "Name", .arguments = "[2]", .expected = "[\"two\"]" },
                 { .function = "Name", .arguments = "[3]", .expected = "[\"\"]" },
                 { .function = "Y", .arguments = "[\"a\"]", .expected = "[2]" },
                 { .function = "Y", .arguments = "[\"b\"]", .expected = NULL },
                 { .function = "Flag", .arguments = "[]", .expected = "[false]" },
                 { .function = "Count", .arguments = "[5]", .expected = "[111109000]" },
                 { .function = "Points", .arguments = "[]", .expected = "[405]" },
                 { .function = "Walk", .arguments = "[]", .expected = "[\"Ada4Cid2Bea5 95911\"]" },
                 { .function = "Copies",
                   .arguments = "[]",
                   .expected = "[\"0=v0;1=v1;2=v2;|0=changed;1=v1;\"]" } } },
    { .name = "references",
      .group = COLLECTION_SAMPLES,
      .source =
          "package facts\n"
          "Point :: struct { x: int; y: int; }\n"
          "Bump :: (v: ref int) { v += 1; }\n"
          "Set :: (v: ref int, to: int) { v = to; }\n"
          "Twice :: (v: ref int) { Bump(ref v); Bump(ref v); }\n"
          "Rename :: (s: ref string) { s = format_text(\"{}!\", s); }\n"
          "Swap :: (p: ref Point) { p = {x: p.y, y: p.x}; }\n"
          "Grow :: (xs: ref int[]) { xs = append(xs, int(7)); }\n"
          "Inc :: (k: int) -> int { Bump(ref k); return k; }\n"
          "Nudge :: (p: ref Point) { p.x = 9; p.y += 5; Bump(ref p.x); }\n"
          "Count :: () -> int { c: int = 2; c += 3; Bump(ref c); c = c * 2; "
          "return c; }\n"
          "Run :: () -> int {\n"
          "    n: int = 1;\n    Bump(ref n);\n    Twice(ref n);\n    Set(ref "
          "n, n * "
          "10);\n"
          "    name: string = \"a\";\n    Rename(ref name);\n"
          "    p: Point = {x: 1, y: 2};\n    Swap(ref p);\n    Bump(ref p.x);\n"
          "    xs: int[] = [1];\n    Grow(ref xs);\n"
          "    total := n + p.x * 100 + p.y * 1000 + length(xs) * 10000 "
          "+ Inc(5) * 1000000;\n"
          "    if (name == \"a!\") { total += 100000; }\n"
          "    return total;\n}\n"
          "Moved :: () -> int { p: Point = {x: 1, y: 2}; Nudge(ref p); "
          "return p.x * 100 + p.y; }",
      .calls = { { .function = "Run", .arguments = "[]", .expected = "[6121340]" },
                 { .function = "Inc", .arguments = "[1]", .expected = "[2]" },
                 { .function = "Count", .arguments = "[]", .expected = "[12]" },
                 { .function = "Moved", .arguments = "[]", .expected = "[1007]" } } },
    /* Fixed scalar-result calls exercise aliases, escaping storage,
     * grouped types, widening, aggregate replacement, and one two-node cycle. */
    { .name = "managed pointers",
      .group = COLLECTION_SAMPLES,
      .source =
          "package facts\n"
          "Point :: struct { x: int; y: int; }\n"
          "Holder :: struct { value: int; alias: ^int; }\n"
          "Link :: struct { value: int; next: ^Link?; }\n"
          "Bump :: (value: ref int) { value += 1; }\n"
          "Make :: () -> ^Point { value: Point = {x: 3, y: 4}; return &value; "
          "}\n"
          "Scalar :: () -> ^int { value: int = 2; return &value; }\n"
          "EscapeCopy :: (n: int) -> ^int { p := &n; p^ += 1; return p; }\n"
          "Parameter :: () -> int { value: int = 3; p := EscapeCopy(value); "
          "return value * 10 + p^; }\n"
          "Alias :: () -> int { value: int = 1; p: ^int = &value; q := p; "
          "q^ = 7; Bump(ref p^); return value * 100 + p^ * 10 + q^; }\n"
          "Escaped :: () -> int { p := Make(); p.x = 9; return p^.x * 10 + "
          "p.y; }\n"
          "Field :: () -> int { value: Point = {x: 2, y: 4}; p := &value; "
          "x := &value.x; x^ = 5; return p.x * 10 + p^.y; }\n"
          "Copies :: () -> int { value: int = 2; original: Holder = "
          "{value: 3, alias: &value}; copied := original; copied.value = 7; "
          "copied.alias^ = 9; return original.value * 100 + copied.value * 10 "
          "+ value; }\n"
          "Distinct :: () -> int { a := Scalar(); b := Scalar(); a^ = 11; "
          "return a^ * 100 + b^; }\n"
          "Nullable :: () -> int { p: ^int? = nil; value: int = 3; p = &value; "
          "if p != nil { return p^; } return 0; }\n"
          "ArrayCopies :: () -> int { x: int = 1; y: int = 2; "
          "original: ^int[] = [&x, &y]; copied := original; copied[0]^ = 5; "
          "return original[0]^ * 100 + original[1]^ * 10 + "
          "copied[1]^; }\n"
          "ReadGrouped :: (values: (^int)[]) -> int { return values[0]^; }\n"
          "Grouped :: () -> int { value: int = 4; return "
          "ReadGrouped([&value]); }\n"
          "Widened :: () -> int64 { small: int = 3; wide: int64 = small; "
          "p := &wide; p^ += 2; return p^; }\n"
          "WholeArray :: () -> int { values: int[] = [1, 2]; "
          "p: ^(int[]) = &values; p^ = [8, 9, 10]; "
          "return length(values) * 100 + values[0] * 10 + values[2]; }\n"
          "Cycle :: () -> int { a: Link = {value: 1, next: nil}; "
          "b: Link = {value: 2, next: nil}; p := &a; q := &b; "
          "p.next = q; q.next = p; return p.value * 10 + q.value; }",
      .calls = { { .function = "Alias", .arguments = "[]", .expected = "[888]" },
                 { .function = "Escaped", .arguments = "[]", .expected = "[94]" },
                 { .function = "Field", .arguments = "[]", .expected = "[54]" },
                 { .function = "Copies", .arguments = "[]", .expected = "[379]" },
                 { .function = "Distinct", .arguments = "[]", .expected = "[1102]" },
                 { .function = "Nullable", .arguments = "[]", .expected = "[3]" },
                 { .function = "ArrayCopies", .arguments = "[]", .expected = "[522]" },
                 { .function = "Parameter", .arguments = "[]", .expected = "[34]" },
                 { .function = "Cycle", .arguments = "[]", .expected = "[12]" },
                 { .function = "Grouped", .arguments = "[]", .expected = "[4]" },
                 { .function = "Widened", .arguments = "[]", .expected = "[5]" },
                 { .function = "WholeArray", .arguments = "[]", .expected = "[390]" } } },
    { .name = "imported references",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Forward :: (value: ref int) { lib.Bump(ref value); }\n"
                "Run :: () -> int { value: int = 4; Forward(ref value); "
                "lib.Bump(ref value); return value; }",
      .library = "package lib\npub Bump :: (value: ref int) { value += 3; }",
      .calls = { { .function = "Run", .arguments = "[]", .expected = "[10]" } } },
    { .name = "reference marker missing",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBump :: (v: ref int) { v += 1; }\nRun :: () "
                "{\n    n: int = 1;\n    Bump(n);\n}",
      .message = "Reference parameter requires an explicit ref argument",
      .line = 5,
      .column = 10 },
    { .name = "reference forwarded marker missing",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBump :: (v: ref int) { v += 1; }\nForward :: "
                "(v: ref int) {\n    Bump(v);\n}",
      .message = "Reference parameter requires an explicit ref argument",
      .line = 4,
      .column = 10 },
    { .name = "reference marker on value parameter",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nRead :: (v: int) -> int { return v; }\nRun :: "
                "() -> int {\n    n: int = 1;\n    return Read(ref n);\n}",
      .message = "ref argument requires a reference parameter",
      .line = 5,
      .column = 17 },
    { .name = "reference marker on conversion",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nRun :: () -> int64 {\n    n: int = 1;\n    "
                "return int64(ref n);\n}",
      .message = "ref argument requires a reference parameter",
      .line = 4,
      .column = 18 },
    { .name = "reference marker on native",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nRun :: () -> string {\n    n: int = 1;\n    "
                "return format_text(ref n);\n}",
      .message = "ref argument requires a reference parameter",
      .line = 4,
      .column = 24 },
    { .name = "reference marker on union constructor",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nU :: union { A(value: int) }\nRun :: () -> U "
                "{\n    n: int = 1;\n    return U.A(ref n);\n}",
      .message = "ref argument requires a reference parameter",
      .line = 5,
      .column = 16 },
    { .name = "reference imported marker missing",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\nRun :: () {\n    "
                "n: int = 1;\n    lib.Bump(n);\n}",
      .library = "package lib\npub Bump :: (v: ref int) { v += 1; }",
      .message = "Reference parameter requires an explicit ref argument",
      .line = 5,
      .column = 14 },
    { .name = "reference literal",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBump :: (v: ref int) { v += 1; }\nRun :: () { "
                "Bump(ref 3); }",
      .message = "Reference argument requires a writable value",
      .line = 3,
      .column = 22 },
    { .name = "reference default",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBump :: (v: ref int = 1) { v += 1; }",
      .message = "a reference parameter takes no default",
      .line = 2,
      .column = 10 },
    { .name = "reference type",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBump :: (v: ref int) { v += 1; }\nRun :: () { "
                "n: int64 = 1; Bump(ref n); }",
      .message = "reference argument differs from the parameter's type",
      .line = 3,
      .column = 36 },
    { .name = "unions",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Shape :: union { Circle(radius: int) | Box(width: int, "
                "height: int) | Spot(at: Point)\n"
                "    | Named(name: string, sides: int[]) | Empty }\n"
                "Make :: (kind: int) -> Shape {\n"
                "    if (kind == 0) { return Shape.Circle(3); }\n"
                "    if (kind == 1) { return Shape.Box(2, 5); }\n"
                "    if (kind == 2) { p: Point = {x: 4, y: 6}; return "
                "Shape.Spot(p); }\n"
                "    if (kind == 3) { sides: int[] = [1, 2, 3]; return "
                "Shape.Named(\"tri\", sides); }\n"
                "    return Shape.Empty;\n}\n"
                "Sum :: (p: Point) -> int { return p.x + p.y; }\n"
                "Area :: (shape: Shape) -> int {\n"
                "    switch shape {\n"
                "        case .Circle(radius): return radius * radius * 3;\n"
                "        case .Box(width, height): return width * height;\n"
                "        case .Spot(at): total := Sum(at); return total + "
                "at.x;\n"
                "        case .Named(name, sides):\n"
                "            total: int = 0;\n"
                "            for (side in sides) { total += side; }\n"
                "            if (name == \"tri\") { total += 100; }\n"
                "            return total;\n"
                "        case .Empty: return -1;\n"
                "    }\n    return -2;\n}\n"
                "Score :: (n: int) -> int {\n"
                "    total: int = 0;\n    i: int = 0;\n"
                "    while (i < n) { total += Area(Make(i)); i += 1; }\n"
                "    return total;\n}\n"
                "Kinds :: () -> int {\n"
                "    shapes: Shape[] = [Shape.Circle(1), Shape.Empty, "
                "Shape.Box(1, 1)];\n"
                "    count: int = 0;\n"
                "    for (shape in shapes) {\n"
                "        switch shape {\n            case .Empty: count += "
                "10;\n            case .Box: count += 100;\n"
                "            case: count += 1;\n        }\n"
                "    }\n    return count;\n}\n"
                "Pair :: (a: Shape, b: Shape) -> int {\n"
                "    switch a {\n"
                "        case .Empty:\n"
                "            switch b {\n                case .Empty: return "
                "2;\n                case: return 1;\n            }\n"
                "        case .Circle(radius): return radius;\n"
                "        case .Box(width, height): return width;\n"
                "        case .Spot(at): return at.x;\n"
                "        case .Named(name, sides): return length(sides);\n"
                "    }\n    return -1;\n}\n"
                "Nested :: () -> int {\n"
                "    return Pair(Shape.Empty, Shape.Empty) * 10 + "
                "Pair(Shape.Empty, Shape.Circle(1)) * 100 + Pair(Shape.Box(3, "
                "4), Shape.Empty);\n}",
      .calls = { { .function = "Score", .arguments = "[5]", .expected = "[156]" },
                 { .function = "Kinds", .arguments = "[]", .expected = "[111]" },
                 { .function = "Nested", .arguments = "[]", .expected = "[123]" },
                 { .function = "Make", .arguments = "[1]", .expected = "[{\"tag\":6300533083688230396,\"values\":[2,5]}]" },
                 { .function = "Make", .arguments = "[2]", .expected = "[{\"tag\":10337135207335990755,\"values\":[{\"x\":4,\"y\":6}]}]" },
                 { .function = "Make", .arguments = "[3]", .expected = "[{\"tag\":16893218631787042844,\"values\":[\"tri\",[1,2,3]]}]" },
                 { .function = "Make", .arguments = "[4]", .expected = "[{\"tag\":16895435817925979816,\"values\":[]}]" } } },
    { .name = "imports",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "import lib \"project:lib\"\n"
                "import strings \"core:strings\"\n"
                "Total :: (n: int) -> int { return lib.Twice(n) + "
                "lib.Sum(lib.Make(n, 3)); }\n"
                "Local :: () -> int { p: lib.Point = {x: 2, y: 5}; "
                "return lib.Sum(p) + p.x; }\n"
                "Shapes :: () -> int {\n"
                "    return lib.Size(lib.Shape.Dot(lib.Make(1, 2))) + "
                "lib.Size(lib.Shape.Line(7)) * 10 +\n"
                "        lib.Size(lib.Shape.Nothing);\n}\n"
                "Match :: (n: int) -> int {\n"
                "    shape := lib.Shape.Line(n);\n"
                "    switch shape { case .Line(size): return size; case: "
                "return 0; }\n"
                "    return -1;\n}\n"
                "Measure :: (s: string) -> int {\n"
                "    return strings.ByteLength(s) * 10 + strings.Compare(s, "
                "\"m\") + lib.Width(s) * 100;\n}\n"
                "Echo :: (p: lib.Point) -> lib.Point { return p; }\n"
                "Points :: (n: int) -> lib.Point[] { points: "
                "lib.Point[] = [lib.Make(n, n)]; return points; }",
      .library = "package lib\n"
                 "import text \"core:strings\"\n"
                 "pub Point :: struct { x: int; y: int; }\n"
                 "pub Shape :: union { Dot(at: Point) | Line(size: int) | "
                 "Nothing "
                 "}\n"
                 "pub Make :: (x: int, y: int) -> Point { p: Point = {x: x, "
                 "y: y}; return p; }\n"
                 "pub Sum :: (p: Point) -> int { return p.x + p.y; }\n"
                 "pub Twice :: (n: int) -> int { return Helper(n) * 2; }\n"
                 "Helper :: (n: int) -> int { return n + 1; }\n"
                 "pub Width :: (s: string) -> int { return "
                 "text.ByteLength(s); }\n"
                 "pub Size :: (shape: Shape) -> int {\n"
                 "    switch shape {\n"
                 "        case .Dot(at): return Sum(at);\n"
                 "        case .Line(size): return size;\n"
                 "        case .Nothing: return 0;\n"
                 "    }\n    return -1;\n}",
      .core = true,
      .calls = { { .function = "Total", .arguments = "[4]", .expected = "[17]" },
                 { .function = "Local", .arguments = "[]", .expected = "[9]" },
                 { .function = "Shapes", .arguments = "[]", .expected = "[73]" },
                 { .function = "Match", .arguments = "[6]", .expected = "[6]" },
                 { .function = "Measure", .arguments = "[\"zebra\"]", .expected = "[551]" },
                 { .function = "Echo", .arguments = "[{\"x\":1,\"y\":2}]", .expected = "[{\"x\":1,\"y\":2}]" },
                 { .function = "Points", .arguments = "[3]", .expected = "[[{\"x\":3,\"y\":3}]]" } } },
    /* An assert fails the call when its condition is false, with its
     * message or its condition's source; it names its arguments as the core
     * catalog does. */
    { .name = "assertions",
      .group = IMPORT_SAMPLES,
      .game = true,
      .source = "package facts\n"
                "Double :: (n: int) -> int {\n"
                "    assert(n > 0, \"n is positive\");\n"
                "    assert(n < 100);\n"
                "    assert(condition: n != 7, message: \"\");\n"
                "    assert((n != 8), (\"not eight\"));\n"
                "    assert(n != 9, \"a\\u0000b\");\n"
                "    return n * 2;\n}",
      .calls = { { .function = "Double", .arguments = "[3]", .expected = "[6]" },
                 { .function = "Double", .arguments = "[-1]", .expected = NULL },
                 { .function = "Double", .arguments = "[200]", .expected = NULL },
                 { .function = "Double", .arguments = "[7]", .expected = NULL },
                 { .function = "Double", .arguments = "[8]", .expected = NULL },
                 { .function = "Double",
                   .arguments = "[9]",
                   .expected = NULL,
                   .diagnostic = "Assertion failed: a\\0b" } } },
    /* Package and imported constants. `lib.Scaled(2)` takes a default that
     * stage zero reads from lib's catalog, at the call; `Triple(2)` one that
     * the package's other file declares, whose occurrence is that file's. */
    { .name = "constants",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "import lib \"project:lib\"\n"
                "Base: int : 40;\n"
                "Down: int : -2;\n"
                "Plain: int : 7;\n"
                "Sum :: (n: int) -> int { return n + Base + Down + "
                "Plain + lib.Step; }\n"
                "Hidden :: () -> int { Base: int = 1; return Base; }\n"
                "Limits :: () -> int {\n"
                "    if (lib.Low < 0 && lib.High > 0) { return lib.High + "
                "lib.Low; }\n"
                "    return 0;\n}\n"
                "Scale :: () -> int { return lib.Scaled(2); }\n"
                "Tripled :: () -> int { return Triple(2); }",
      .more = "package facts\nTriple :: (n: int, by: int = 3) -> int { return "
              "n * by; }\n",
      .library = "package lib\n"
                 "pub Step: int : 100;\n"
                 "pub Low: int : -2147483648;\n"
                 "pub High: int : 2147483647;\n"
                 "pub Scaled :: (n: int, by: int = 1) -> int { return n * "
                 "Step * by; }",
      .calls = { { .function = "Sum", .arguments = "[3]", .expected = "[148]" },
                 { .function = "Hidden", .arguments = "[]", .expected = "[1]" },
                 { .function = "Limits", .arguments = "[]", .expected = "[-1]" },
                 { .function = "Scale", .arguments = "[]", .expected = "[200]" },
                 { .function = "Tripled", .arguments = "[]", .expected = "[6]" } } },
    { .name = "patterns",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Value :: union { Int(n: int) | Text(s: string) | Pair(a: int, "
                "b: int) | Spot(at: Point) | Nothing }\n"
                "Make :: (k: int) -> Value {\n"
                "    if (k == 0) { return Value.Int(5); }\n"
                "    if (k == 1) { return Value.Text(\"hey\"); }\n"
                "    if (k == 2) { return Value.Pair(3, 4); }\n"
                "    if (k == 3) { return Value.Pair(4, 3); }\n"
                "    return Value.Nothing;\n}\n"
                "Score :: (k: int) -> int {\n"
                "    v := Make(k);\n"
                "    if (v is .Int(n)) { return n; }\n"
                "    if (v is .Pair(a, b) && a < b) { return a * 10 + b; }\n"
                "    if (v is not .Nothing && !(v is .Text)) { return -1; }\n"
                "    if ((v is .Text(s)) && s == \"hey\") { return 100; } else "
                "{ return 0; }\n"
                "    return -2;\n}\n"
                "Kind :: (k: int) -> int {\n"
                "    v := Make(k);\n"
                "    if (v is .Int(n)) { return n; } else if (v is .Pair(a, "
                "b)) { return a + b; } else { return -5; }\n"
                "    return 0;\n}\n"
                "Count :: () -> int {\n"
                "    v := Make(2);\n"
                "    total: int = 0;\n"
                "    while (v is .Pair(a, b) && a < 10) { total += a; v = "
                "Value.Pair(a + b, b); }\n"
                "    return total;\n}\n"
                "Walk :: () -> int {\n"
                "    start: Point = {x: 0, y: 1};\n"
                "    v := Value.Spot(start);\n"
                "    total: int = 0;\n"
                "    while (v is .Spot(at) and at.x < 3) {\n"
                "        total += at.x * 10 + at.y;\n"
                "        next: Point = {x: at.x + 1, y: at.y};\n"
                "        v = Value.Spot(next);\n"
                "    }\n"
                "    return total;\n}\n"
                "Empty :: (k: int) -> bool { return Make(k) is "
                ".Nothing; }",
      .calls = { { .function = "Score", .arguments = "[0]", .expected = "[5]" },
                 { .function = "Score", .arguments = "[1]", .expected = "[100]" },
                 { .function = "Score", .arguments = "[2]", .expected = "[34]" },
                 { .function = "Score", .arguments = "[3]", .expected = "[-1]" },
                 { .function = "Score", .arguments = "[4]", .expected = "[0]" },
                 { .function = "Kind", .arguments = "[2]", .expected = "[7]" },
                 { .function = "Kind", .arguments = "[4]", .expected = "[-5]" },
                 { .function = "Count", .arguments = "[]", .expected = "[10]" },
                 { .function = "Walk", .arguments = "[]", .expected = "[33]" },
                 { .function = "Empty", .arguments = "[4]", .expected = "[true]" } } },
    { .name = "shadowing",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Shadow :: (n: int) -> int {\n"
                "    m := n + 1;\n"
                "    total: int = m;\n"
                "    if (m > 0) { total: int = 100; total += 1; }\n"
                "    if (m > 1) { m: int = 5; total += m; }\n"
                "    return total + m;\n}\n"
                "U :: union { A(x: int) | B }\n"
                "Arm :: () -> int { u := U.A(7); switch u { case .A(n): "
                "n: int = 2; return n; case .B: return 0; } return -1; }",
      .calls = { { .function = "Shadow", .arguments = "[3]", .expected = "[13]" },
                 { .function = "Arm", .arguments = "[]", .expected = "[2]" } } },
    { .name = "integers",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Mask: uint64 : 0xFF00000000000000;\n"
                "Small: byte : 200;\n"
                "Hash :: (bytes: byte[]) -> uint64 {\n"
                "    hash: uint64 = 1469598103934665603;\n"
                "    for (item in bytes) { hash = (hash ^ uint64(item)) * "
                "1099511628211; }\n"
                "    return hash;\n}\n"
                "Bytes :: () -> byte[] {\n"
                "    out: byte[] = [1, 2, 255];\n"
                "    out = append(out, byte(wrap: 300));\n"
                "    out = append(out, Small);\n"
                "    return out;\n}\n"
                "High :: (n: int) -> int { return int(wrap: (Mask >> "
                "56) + uint64(n)); }\n"
                "Mixed :: (a: int, b: int64) -> int64 { return a + b; "
                "}\n"
                "Store :: (a: int) -> int64 { x: int64 = a; x += 10; x "
                "*= 3; return x; }\n"
                "Narrow :: (v: int64) -> int { return int(v); }\n"
                "Clamp :: (v: int) -> byte { return byte(clamp: v); }",
      .calls = { { .function = "Hash", .arguments = "[[1,2,3]]", .expected = "[11570874782335668893]" },
                 { .function = "Bytes", .arguments = "[]", .expected = "[[1,2,255,44,200]]" },
                 { .function = "High", .arguments = "[1]", .expected = "[256]" },
                 { .function = "Mixed", .arguments = "[2,5000000000]", .expected = "[5000000002]" },
                 { .function = "Store", .arguments = "[4]", .expected = "[42]" },
                 { .function = "Narrow", .arguments = "[7]", .expected = "[7]" },
                 { .function = "Narrow", .arguments = "[5000000000]", .expected = NULL },
                 { .function = "Clamp", .arguments = "[300]", .expected = "[255]" },
                 { .function = "Clamp", .arguments = "[-5]", .expected = "[0]" } } },
    { .name = "wide values",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Wide: int64 : -5000000000;\n"
                "Tag :: union { Bits(value: uint64) | None }\n"
                "Shifts :: (n: int) -> uint64 { v := uint64(n); return "
                "(v << 4) | (v >> 60) & 0xF; }\n"
                "Equal :: (a: int, b: int) -> bool { return uint64(a) "
                "== uint64(b) && uint64(a) != 0; }\n"
                "Twice :: () -> int64 { return Wide * 2; }\n"
                "Negative :: (n: int64) -> bool { return n < 0; }\n"
                "Mk :: (v: uint64) -> Tag { return Tag.Bits(v); }\n"
                "Pick :: (n: int) -> uint64 {\n"
                "    t := Mk(uint64(n) * 1000000007);\n"
                "    switch t { case .Bits(value): return value; case .None: "
                "return 0; }\n"
                "    return 0;\n}",
      .calls = { { .function = "Shifts", .arguments = "[1]", .expected = "[16]" },
                 { .function = "Equal", .arguments = "[3,3]", .expected = "[true]" },
                 { .function = "Twice", .arguments = "[]", .expected = "[-10000000000]" },
                 { .function = "Negative", .arguments = "[-1]", .expected = "[true]" },
                 { .function = "Pick", .arguments = "[5]", .expected = "[5000000035]" } } },
    { .name = "wide records",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Entry :: struct { flag: bool; id: uint64; small: byte; "
                "count: int; delta: int64; name: string; }\n"
                "Make :: (n: int) -> Entry {\n"
                "    e: Entry = {flag: n > 0, id: uint64(n) * 1000000007, "
                "small: byte(wrap: n), count: n, delta: int64(n) - "
                "5000000000, name: \"e\"};\n"
                "    return e;\n}\n"
                "Mix :: (e: Entry) -> uint64 { return (e.id ^ "
                "uint64(e.small)) + uint64(e.count); }\n"
                "Bump :: (e: Entry) -> Entry {\n"
                "    out := e;\n    out.id += 1;\n    out.small = 7;\n"
                "    out.delta *= 2;\n    return out;\n}\n"
                "Total :: (n: int) -> int64 {\n"
                "    es: Entry[] = [];\n    i: int = 0;\n"
                "    while (i < n) { es = append(es, Make(i)); i += 1; }\n"
                "    total: int64 = 0;\n"
                "    for (e in es) { total += e.delta + int64(e.small); }\n"
                "    return total;\n}",
      .calls = { { .function = "Make",
                   .arguments = "[3]",
                   .expected = "[{\"flag\":true,\"id\":3000000021,\"small\":3,"
                               "\"count\":3,"
                               "\"delta\":-4999999997,\"name\":\"e\"}]" },
                 { .function = "Mix",
                   .arguments = "[{\"flag\":true,\"id\":3000000021,\"small\":3,"
                                "\"count\":3,"
                                "\"delta\":-4999999997,\"name\":\"e\"}]",
                   .expected = "[3000000025]" },
                 { .function = "Bump",
                   .arguments = "[{\"flag\":false,\"id\":18446744073709551614,"
                                "\"small\":200,"
                                "\"count\":-1,\"delta\":-4999999997,\"name\":"
                                "\"x\"}]",
                   .expected = "[{\"flag\":false,\"id\":18446744073709551615,"
                               "\"small\":7,"
                               "\"count\":-1,\"delta\":-9999999994,\"name\":"
                               "\"x\"}]" },
                 { .function = "Total", .arguments = "[3]", .expected = "[-14999999994]" } } },
    { .name = "inferred arrays",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Sizes :: (n: int) -> int {\n"
                "    flags := resize([false], n);\n"
                "    counts := resize([int(0)], n);\n"
                "    counts[0] = 7;\n"
                "    total: int = 0;\n"
                "    for (count in counts) { total += count; }\n"
                "    if (flags[1]) { return -1; }\n"
                "    return total * 10 + length(flags);\n}\n"
                "Words :: () -> int { first := \"x\"; words := [first, "
                "\"yy\", \"zzz\"]; return length(words); }\n"
                "Points :: () -> int { p: Point = {x: 1, y: 2}; points "
                ":= [p, p]; return points[1].y + length(points); }\n"
                "Wide :: () -> uint64 { bits := [uint64(1), 2, 0xFF]; "
                "return bits[1] + bits[2]; }",
      .calls = { { .function = "Sizes", .arguments = "[3]", .expected = "[73]" },
                 { .function = "Words", .arguments = "[]", .expected = "[3]" },
                 { .function = "Points", .arguments = "[]", .expected = "[4]" },
                 { .function = "Wide", .arguments = "[]", .expected = "[257]" } } },
    { .name = "boxed elements",
      .group = TYPE_SAMPLES,
      .boxed = true,
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; }\n"
                "Point :: struct { x: int; y: int; }\n"
                "Shape :: union { Circle(radius: int) | Empty }\n"
                "BoxedWord :: (words: string[], i: int) -> string { return "
                "words[i]; }\n"
                "BoxedWords :: (words: string[]) -> int {\n"
                "    total: int = 0;\n"
                "    for (word in words) { if (word == \"b\") { total += 1; } "
                "}\n"
                "    return total;\n}\n"
                "BoxedNotes :: (notes: Note[], i: int) -> int {\n"
                "    total: int = 0;\n"
                "    for (note in notes) { total += note.count; }\n"
                "    return total + notes[i].count * 100;\n}\n"
                "BoxedShapes :: (i: int) -> int {\n"
                "    shapes: Shape[] = [Shape.Circle(2), Shape.Empty, "
                "Shape.Circle(5)];\n"
                "    total: int = 0;\n"
                "    for (shape in shapes) {\n"
                "        switch shape { case .Circle(radius): total += radius; "
                "case .Empty: total += 10; }\n"
                "    }\n"
                "    picked := shapes[i];\n"
                "    switch picked { case .Circle(radius): return total + "
                "radius * 100; case .Empty: return total; }\n"
                "    return -1;\n}\n"
                "PackedPoints :: (points: Point[], i: int) -> int {\n"
                "    total: int = 0;\n"
                "    for (point in points) { total += point.y; }\n"
                "    return total + points[i].x * 100;\n}",
      .calls = { { .function = "BoxedWord",
                   .arguments = "[[\"a\",\"b\",\"c\"],1]",
                   .expected = "[\"b\"]" },
                 { .function = "BoxedWord", .arguments = "[[\"a\"],1]", .expected = NULL },
                 { .function = "BoxedWord", .arguments = "[[\"a\"],-1]", .expected = NULL },
                 { .function = "BoxedWords", .arguments = "[[\"a\",\"b\",\"b\"]]", .expected = "[2]" },
                 { .function = "BoxedNotes",
                   .arguments = "[[{\"text\":\"x\",\"count\":1},{\"text\":"
                                "\"y\",\"count\":2}],1]",
                   .expected = "[203]" },
                 { .function = "BoxedNotes",
                   .arguments = "[[{\"text\":\"x\",\"count\":1}],1]",
                   .expected = NULL },
                 { .function = "BoxedShapes", .arguments = "[2]", .expected = "[517]" },
                 { .function = "BoxedShapes", .arguments = "[1]", .expected = "[17]" },
                 { .function = "BoxedShapes", .arguments = "[3]", .expected = NULL },
                 { .function = "PackedPoints",
                   .arguments = "[[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}],1]",
                   .expected = "[306]" } } },
    { .name = "formatting",
      .source = "package facts\n"
                "Label :: (n: int, name: string) -> string { return "
                "format_text(\"{}:{}\", n, name); }\n"
                "Plain :: () -> string { return format_text(\"plain\"); "
                "}\n"
                "Mixed :: (flag: bool) -> string { big: uint64 = "
                "0xFFFFFFFFFFFFFFFF; return format_text(\"{} {} {}\", flag, "
                "big, int64(-3)); }",
      .calls = { { .function = "Label", .arguments = "[3,\"x\"]", .expected = "[\"3:x\"]" },
                 { .function = "Plain", .arguments = "[]", .expected = "[\"plain\"]" },
                 { .function = "Mixed",
                   .arguments = "[true]",
                   .expected = "[\"true 18446744073709551615 -3\"]" } } },
    { .name = "native results",
      .source = "package facts\n"
                "Decode :: (literal: string) -> string {\n"
                "    decoded := compiler_json_string(literal);\n"
                "    if (!decoded.valid) { return \"?\"; }\n"
                "    return format_text(\"{}/{}\", decoded.value, "
                "decoded.byte_length);\n}\n"
                "Valid :: (literal: string) -> bool { return "
                "compiler_json_string(literal).Valid; }\n"
                "Length :: (literal: string) -> int { return "
                "compiler_json_string(literal).ByteLength; }",
      .calls = { { .function = "Decode",
                   .arguments = "[\"\\\"a\\\\u00e9\\\"\"]",
                   .expected = "[\"a\303\251/3\"]" },
                 { .function = "Decode", .arguments = "[\"bad\"]", .expected = "[\"?\"]" },
                 { .function = "Valid", .arguments = "[\"\\\"x\\\"\"]", .expected = "[true]" },
                 { .function = "Length", .arguments = "[\"\\\"abc\\\"\"]", .expected = "[3]" } } },
    { .name = "written parameters",
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Grow :: (xs: int[], n: int) -> int[] { xs = "
                "append(xs, n); xs[0] = 9; return xs; }\n"
                "Swap :: (p: Point, q: Point) -> int { p = q; return "
                "p.x * 10 + q.y; }\n"
                "Caller :: () -> int {\n"
                "    xs: int[] = [1, 2];\n"
                "    ys := Grow(xs, 5);\n"
                "    a: Point = {x: 1, y: 2};\n"
                "    b: Point = {x: 3, y: 4};\n"
                "    return xs[0] * 1000 + length(ys) * 100 + ys[0] * 10 + "
                "Swap(a, b) - a.x * 34;\n}",
      .calls = { { .function = "Caller", .arguments = "[]", .expected = "[1390]" } } },
    { .name = "field assignment",
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; open: bool; }\n"
                "Stamp :: (note: Note, count: int) -> Note { note.count "
                "= count; note.text = format_text(\"#{}\", count); return "
                "note; }\n"
                "Fields :: (n: int) -> string {\n"
                "    note: Note = {text: \"a\", count: 1, open: false};\n"
                "    copy := note;\n"
                "    note.count += n;\n"
                "    note.count *= 2;\n"
                "    note.open = true;\n"
                "    stamped := Stamp(note, 7);\n"
                "    b: byte = 3;\n"
                "    copy.count = b;\n"
                "    copy.count += b;\n"
                "    return format_text(\"{} {} {} {} {} {}\", note.text, "
                "note.count, note.open, copy.count, stamped.text, "
                "stamped.count);\n}",
      .calls = { { .function = "Fields", .arguments = "[4]", .expected = "[\"a 10 true 6 #7 7\"]" } } },
    { .name = "record push",
      .source = "package facts\nNode :: struct { kind: string; start: int; }\n"
                "Build :: (count: int) -> int {\n"
                "    nodes: Node[] = [];\n"
                "    index: int = 0;\n"
                "    while (index < count) {\n"
                "        node: Node = {kind: \"a\", start: index};\n"
                "        nodes = append(nodes, node);\n"
                "        index += 1;\n"
                "    }\n"
                "    return length(nodes);\n}",
      .updates = true,
      .calls = { { .function = "Build", .arguments = "[3]", .expected = "[3]" } } },
    { .name = "extension",
      .source = "package facts\nBuild :: (count: int) -> int {\n"
                "    xs: int[] = [];\n"
                "    index: int = 0;\n"
                "    while (index < count) {\n"
                "        xs = extend(xs, [index, index]);\n"
                "        index += 1;\n"
                "    }\n"
                "    return length(xs);\n}",
      .updates = true,
      .calls = { { .function = "Build", .arguments = "[3]", .expected = "[6]" } } },
    { .name = "map updates",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBuild :: (count: int) -> int {\n"
                "    squares: [int: int] = [:];\n"
                "    index: int = 0;\n"
                "    while (index < count) { squares[index] = index; index += "
                "1; }\n"
                "    index = 0;\n"
                "    while (index < count) { delete squares[index]; index += "
                "2; }\n"
                "    total: int = 0;\n"
                "    for (value in squares) { total += 1; }\n"
                "    return total;\n}",
      .updates = true,
      .calls = { { .function = "Build", .arguments = "[3]", .expected = "[1]" } } },
    { .name = "map loop names",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nAnswer :: () -> int {\n"
                "    m: [int: int] = [1: 2];\n    total: int = 0;\n"
                "    for (k, v in m) { total += k * 10 + v; }\n"
                "    v: int = 3;\n    k: int = 4;\n"
                "    return total * 100 + v + k;\n}",
      .calls = { { .function = "Answer", .arguments = "[]", .expected = "[1207]" } } },
    { .name = "duplicate map loop names",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nAnswer :: () { m: [int: int] = [:]; for (x, x "
                "in m) {} }",
      .message = "Duplicate declaration 'x'",
      .line = 2,
      .column = 45 },
    { .name = "inferred map literal",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nAnswer :: () -> int { m := [\"a\": 1]; return "
                "0; }",
      .message = "map literal needs a [K: V] type",
      .line = 2,
      .column = 28 },
    { .name = "compound map assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nAdd :: () -> int { m: [int: int] = [:]; m[1] "
                "+= 2; return m[1]; }",
      .message = "compound map assignment is not supported yet",
      .line = 2,
      .column = 41 },
    { .name = "map parameter removal",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nDrop :: (m: [int: int]) -> int { delete m[1]; "
                "return 0; }",
      .message = "deleting a key needs a local map",
      .line = 2,
      .column = 41 },
    { .name = "bounds",
      .source = "package facts\nPick :: (xs: int[], i: int) -> int { "
                "return xs[i]; }",
      .calls = { { .function = "Pick", .arguments = "[[1,2],1]", .expected = "[2]" },
                 { .function = "Pick", .arguments = "[[1,2],-1]", .expected = NULL },
                 { .function = "Pick", .arguments = "[[],0]", .expected = NULL } } },
    { .name = "unknown name",
      .source = "package facts\nAnswer :: () -> int { return missing; }",
      .message = "Unknown name 'missing'",
      .line = 2,
      .column = 30 },
    { .name = "arity",
      .source = "package facts\nOne :: (a: int) -> int { return a; "
                "}\nTwo :: () -> int { return One(); }",
      .message = "call has the wrong number of arguments",
      .line = 3,
      .column = 27 },
    { .name = "types",
      .source = "package facts\nAnswer :: () -> int { return true; }",
      .message = "returned value differs from the result type",
      .line = 2,
      .column = 23 },
    { .name = "untyped literal",
      .source = "package facts\nPair :: struct { a: int; }\n"
                "One :: () -> int { p := {a: 1}; return p.a; }",
      .message = "Struct literal needs an explicit record type",
      .line = 3,
      .column = 25 },
    { .name = "unknown field",
      .source = "package facts\nPair :: struct { a: int; }\n"
                "One :: (p: Pair) -> int { return p.b; }",
      .message = "Unknown field 'b'",
      .line = 3,
      .column = 34 },
    { .name = "missing field",
      .source = "package facts\nPair :: struct { a: int; b: int; }\n"
                "One :: () -> Pair { return {a: 1}; }",
      .message = "Struct literal needs every field",
      .line = 3,
      .column = 28 },
    { .name = "duplicate member",
      .source = "package facts\nPair :: struct { a: int; b: int; }\n"
                "One :: () -> Pair { return {a: 1, a: 2}; }",
      .message = "Duplicate literal member 'a'",
      .line = 3,
      .column = 35 },
    { .name = "shared scope",
      .source = "package facts\nPoint :: struct { x: int; }\n"
                "Point :: () -> int { return 1; }",
      .message = "Duplicate declaration 'Point'",
      .line = 3,
      .column = 1 },
    { .name = "no value",
      .source = "package facts\nNothing :: () { }\n"
                "One :: () -> int { x := Nothing(); return 0; }",
      .message = "call has no value",
      .line = 3,
      .column = 20 },
    { .name = "untyped array",
      .source = "package facts\nOne :: () -> int { xs := [1]; return 0; "
                "}",
      .message = "an untyped number is a double; give the array an integer "
                 "type",
      .line = 2,
      .column = 27 },
    { .name = "unknown function",
      .source = "package facts\nOne :: () -> int { return Missing(1); }",
      .message = "Unknown function 'Missing'",
      .line = 2,
      .column = 27 },
    { .name = "index type",
      .source = "package facts\nOne :: (xs: int[]) -> int { return "
                "xs[true]; }",
      .message = "index needs an int",
      .line = 2,
      .column = 39 },
    { .name = "reference for",
      .source = "package facts\nOne :: (xs: int[]) -> int { for x: ref in xs { "
                "} "
                "return 0; }",
      .message = "for loops do not support `ref` yet",
      .line = 2,
      .column = 29 },
    { .name = "descending range",
      .source = "package facts\nTop :: 3;\nOne :: () -> int { for i in Top..1 "
                "{ } "
                "return 0; }",
      .message = "Range bounds descend; count down with for <",
      .line = 3,
      .column = 20 },
    { .name = "descending reverse range",
      .source = "package facts\nOne :: () -> int { for < i in 10..0 { } "
                "return 0; }",
      .message = "Range bounds descend; for < takes the low bound first, as in "
                 "for < i in 0..10",
      .line = 2,
      .column = 20 },
    { .name = "duplicate keyed binding",
      .source = "package facts\nOne :: (xs: int[]) -> int { for x, x in xs { } "
                "return 0; }",
      .message = "Duplicate declaration 'x'",
      .line = 2,
      .column = 36 },
    { .name = "missing default",
      .source = "package facts\nF :: (a: int = 1, b: int) -> int { return a + "
                "b; "
                "}\nOne :: () -> int { return F(); }",
      .message = "call has the wrong number of arguments",
      .line = 3,
      .column = 27 },
    { .name = "multiplication overflow",
      .source = "package facts\nA: int64 : -1 * -9223372036854775808;",
      .message = "Constant value is outside the range of int64",
      .line = 2,
      .column = 12 },
    { .name = "range assignment",
      .source = "package facts\nOne :: () -> int { for i in 0..3 { i = 1; } "
                "return 0; }",
      .message = "Assignment requires a writable value",
      .line = 2,
      .column = 36 },
    { .name = "for scalar",
      .source = "package facts\nOne :: (n: int) -> int { for (x in "
                "n) { } return 0; }",
      .message = "for needs an array",
      .line = 2,
      .column = 36 },
    { .name = "untyped wildcard argument",
      .source = "package facts\nOne :: () -> string { return "
                "format_text(\"{}\", 1); }",
      .message = "a number needs an integer type here",
      .line = 2,
      .column = 48 },
    { .name = "inferred mismatch",
      .source = "package facts\nOne :: (xs: int[]) -> int { ys := "
                "append(xs, true); return length(ys); }",
      .message = "argument differs from its parameter type",
      .line = 2,
      .column = 35 },
    { .name = "switch scalar pattern",
      .source = "package facts\nU :: union { A | B }\nOne :: (n: int) "
                "-> int { switch n { case .A: return 1; } return 0; }",
      .message = "an integer switch's arms need values",
      .line = 3,
      .column = 42 },
    { .name = "switch string",
      .source = "package facts\nOne :: (s: string) -> int { switch s { "
                "case \"a\": return 1; } return 0; }",
      .message = "switch needs a union or integer value",
      .line = 2,
      .column = 36 },
    { .name = "unknown variant",
      .source = "package facts\nU :: union { A | B }\nOne :: () -> U { "
                "return U.C; }",
      .message = "Unknown variant 'C'",
      .line = 3,
      .column = 25 },
    { .name = "partial bindings",
      .source = "package facts\nU :: union { A(x: int, y: int) }\n"
                "One :: (u: U) -> int { switch u { case .A(x): return "
                "x; } return 0; }",
      .message = "A union payload pattern must bind every field",
      .line = 3,
      .column = 40 },
    { .name = "assigned binding",
      .source = "package facts\nU :: union { A(x: int) }\n"
                "One :: (u: U) -> int { switch u { case .A(x): x = 2; "
                "return x; } return 0; }",
      .message = "Assignment requires a writable value",
      .line = 3,
      .column = 47 },
    { .name = "missing payload",
      .source = "package facts\nU :: union { A(x: int) }\nOne :: () -> "
                "U { return U.A; }",
      .message = "variant 'A' needs its payload",
      .line = 3,
      .column = 25 },
    { .name = "assert message",
      .group = IMPORT_SAMPLES,
      .game = true,
      .source = "package facts\nCheck :: (n: int, m: string) -> int {\n"
                "    assert(n > 0, m);\n    return n;\n}",
      .message = "assert's message is a string constant",
      .line = 3,
      .column = 19 },
    { .name = "unknown imported function",
      .source = "package facts\nimport lib \"project:lib\"\nOne :: () "
                "-> int { return lib.Missing(1); }",
      .library = "package lib\npub Two :: () -> int { return 2; }",
      .message = "Unknown function 'Missing'",
      .line = 3,
      .column = 31 },
    { .name = "alias conflict",
      .source = "package facts\nimport lib \"project:lib\"\nlib :: () "
                "-> int { return 1; }",
      .library = "package lib\npub Two :: () -> int { return 2; }",
      .message = "Package import alias conflicts with a package declaration",
      .line = 2,
      .column = 1 },
    { .name = "alias value",
      .source = "package facts\nimport lib \"project:lib\"\nOne :: () "
                "-> int { return lib; }",
      .library = "package lib\npub Two :: () -> int { return 2; }",
      .message = "Unknown name 'lib'",
      .line = 3,
      .column = 27 },
    { .name = "constant type",
      .source = "package facts\nOn: bool : true;",
      .message = "constant type is not supported yet",
      .line = 2,
      .column = 5 },
    { .name = "constant cycle",
      .source = "package facts\nA: int : B + 1;\nB: int : A;",
      .message = "Constant 'A' depends on itself",
      .line = 2,
      .column = 10 },
    { .name = "constant overflow",
      .source = "package facts\nA: int : 65536 * 65536 / 65536;",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 10 },
    { .name = "constant clash",
      .source = "package facts\nA: int : 1;\nA :: () -> int { return 1; "
                "}",
      .message = "Duplicate declaration 'A'",
      .line = 3,
      .column = 1 },
    { .name = "constant assignment",
      .source = "package facts\nA: int : 1;\nOne :: () -> int { A = 2; "
                "return A; }",
      .message = "assignment needs a local name",
      .line = 3,
      .column = 20 },
    { .name = "binding outside condition",
      .source = "package facts\nU :: union { A(x: int) | B }\nOne :: "
                "(u: U) -> bool { return u is .A(x); }",
      .message = "Pattern bindings require a matching if, while or switch arm",
      .line = 3,
      .column = 40 },
    { .name = "negated binding",
      .source = "package facts\nU :: union { A(x: int) | B }\n"
                "One :: (u: U) -> int { if (u is not .A(x)) { return 1; "
                "} return 0; }",
      .message = "Pattern bindings require a matching if, while or switch arm",
      .line = 3,
      .column = 40 },
    { .name = "alternative binding",
      .source = "package facts\nU :: union { A(x: int) | B }\n"
                "One :: (u: U) -> int { if (u is .A(x) || false) { "
                "return 1; } return 0; }",
      .message = "Pattern bindings require a matching if, while or switch arm",
      .line = 3,
      .column = 36 },
    { .name = "binding in else",
      .source = "package facts\nU :: union { A(x: int) | B }\n"
                "One :: (u: U) -> int { if (u is .A(x)) { return x; } "
                "else { return x; } }",
      .message = "Unknown name 'x'",
      .line = 3,
      .column = 68 },
    { .name = "is scalar",
      .source = "package facts\nU :: union { A | B }\nOne :: (n: int) "
                "-> bool { return n is .A; }",
      .message = "is needs a union value",
      .line = 3,
      .column = 34 },
    { .name = "is unknown variant",
      .source = "package facts\nU :: union { A | B }\nOne :: (u: U) -> "
                "bool { return u is .C; }",
      .message = "Unknown union variant 'C'",
      .line = 3,
      .column = 37 },
    { .name = "duplicate local",
      .source = "package facts\nOne :: () -> int { x: int = 1; x: int = "
                "2; "
                "return x; }",
      .message = "Duplicate declaration 'x'",
      .line = 2,
      .column = 32 },
    { .name = "duplicate binding",
      .source = "package facts\nU :: union { A(x: int, y: int) | B }\n"
                "One :: (u: U) -> int { if (u is .A(n, n)) { return n; "
                "} return 0; }",
      .message = "Duplicate declaration 'n'",
      .line = 3,
      .column = 39 },
    { .name = "duplicate condition binding",
      .source = "package facts\nU :: union { A(x: int) | B }\n"
                "One :: (u: U, v: U) -> int { if (u is .A(n) && v is "
                ".A(n)) { return n; } return 0; }",
      .message = "Duplicate declaration 'n'",
      .line = 3,
      .column = 56 },
    { .name = "signed with uint64",
      .source = "package facts\nOne :: (a: int, b: uint64) -> uint64 { "
                "return a + b; }",
      .message = "int and uint64 operands need an explicit conversion, such as "
                 "int(value)",
      .line = 2,
      .column = 47 },
    { .name = "unsigned negation",
      .source = "package facts\nOne :: (a: uint64) -> uint64 { return -a; }",
      .message = "unsupported arithmetic type",
      .line = 2,
      .column = 39 },
    { .name = "byte literal range",
      .source = "package facts\nOne :: () -> byte { return 256; }",
      .message = "Numeric literal is outside the range of byte",
      .line = 2,
      .column = 28 },
    { .name = "negative uint64",
      .source = "package facts\nOne :: () -> uint64 { x: uint64 = -1; "
                "return x; }",
      .message = "Numeric literal is outside the range of uint64",
      .line = 2,
      .column = 35 },
    { .name = "untyped local",
      .source = "package facts\nOne :: () -> int { x := 5; return 1; }",
      .message = "an untyped number is a double; give the local an integer "
                 "type",
      .line = 2,
      .column = 25 },
    { .name = "conversion arity",
      .source = "package facts\nOne :: () -> byte { return byte(1, 2); "
                "}",
      .message = "Conversion takes one value",
      .line = 2,
      .column = 28 },
    { .name = "conversion name",
      .source = "package facts\nOne :: () -> byte { return byte(round: "
                "1); }",
      .message = "A conversion argument may only be named wrap or clamp",
      .line = 2,
      .column = 33 },
    { .name = "floating constant",
      .source = "package facts\nHalf :: 0.5;",
      .message = "only integer constants are supported yet",
      .line = 2,
      .column = 9 },
    { .name = "negative zero byte",
      .source = "package facts\nOne :: () -> byte { x: byte = -0; "
                "return x; }",
      .message = "Numeric literal is outside the range of byte",
      .line = 2,
      .column = 31 },
    { .name = "wrapped literal in parentheses",
      .source = "package facts\nOne :: () -> int { return int(wrap: "
                "(0xFFFFFFFFFFFFFFFF)); }",
      .message = "Numeric literal is outside the range of int64",
      .line = 2,
      .column = 38 },
    { .name = "untyped empty array",
      .source = "package facts\nOne :: () -> int { xs := []; return "
                "length(xs); }",
      .message = "Empty arrays need an explicit element type",
      .line = 2,
      .column = 26 },
    { .name = "results in an array",
      .source = "package facts\nOne :: (s: string) -> bool { xs := "
                "[compiler_json_string(s)]; return true; }",
      .message = "array item has no value type",
      .line = 2,
      .column = 37 },
    { .name = "results assignment",
      .source = "package facts\nOne :: (s: string) -> bool { d := "
                "compiler_json_string(s); d = "
                "compiler_json_string(s); return d.valid; }",
      .message = "assigning a native's several results is not supported yet",
      .line = 2,
      .column = 60 },
    { .name = "binding field assignment",
      .source = "package facts\nP :: struct { x: int; }\nU :: union { A(p: P) "
                "| B }\n"
                "One :: (u: U) -> int { switch u { case .A(p): p.x = 1; "
                "return p.x; case .B: return 0; } return 0; }",
      .message = "Assignment requires a writable value",
      .line = 4,
      .column = 47 },
    { .name = "for field assignment",
      .source = "package facts\nP :: struct { x: int; }\nOne :: (ps: "
                "P[]) -> int { for (p in ps) { p.x = 1; } return 0; }",
      .message = "Assignment requires a writable value",
      .line = 3,
      .column = 43 },
    { .name = "for assignment",
      .source = "package facts\nOne :: (xs: int[]) -> "
                "int { for (x in xs) { x = 1; } return 0; }",
      .message = "Assignment requires a writable value",
      .line = 2,
      .column = 45 },
    { .name = "element field assignment",
      .source = "package facts\nP :: struct { x: int; }\nOne :: () -> "
                "int { ps: P[] = []; ps[0].x = 1; return 0; }",
      .message = "field assignment needs a local record",
      .line = 3,
      .column = 34 },
    { .name = "non-exhaustive switch",
      .source = "package facts\nU :: union { A | B }\nOne :: (u: U) -> "
                "int { switch u { case .A: return 2; } return 1; }",
      .message = "Non-exhaustive switch: a union alternative is missing",
      .line = 3,
      .column = 24 },
    { .name = "duplicate arm",
      .source = "package facts\nU :: union { A | B }\n"
                "One :: (u: U) -> int { switch u { case .A: return 2; "
                "case .A: return 3; case .B: return 4; } return 1; }",
      .message = "Unreachable duplicate union pattern",
      .line = 3,
      .column = 59 },
    { .name = "duplicate default",
      .source = "package facts\nU :: union { A | B }\n"
                "One :: (u: U) -> int { switch u { case: return 2; "
                "case: return 3; } return 1; }",
      .message = "Duplicate default arm",
      .line = 3,
      .column = 51 },
    { .name = "duplicate field",
      .source = "package facts\nPair :: struct { a: int; a: bool; }",
      .message = "Duplicate field 'a'",
      .line = 2,
      .column = 26 },
    { .name = "byte-preserving strings",
      .source = "package facts\nBinary :: () -> string { return \"a\\u0000b\"; "
                "}\nDefault :: (value: string = \"a\\u0000b\") -> string { "
                "return value; }\nReadDefault :: () -> string { return "
                "Default(); }\nEqual :: () -> bool { return \"a\\u0000b\" == "
                "\"a\\u0000c\"; }",
      .calls = { { .function = "Binary", .arguments = "[]", .expected = "[\"a\\u0000b\"]" },
                 { .function = "ReadDefault", .arguments = "[]", .expected = "[\"a\\u0000b\"]" },
                 { .function = "Equal", .arguments = "[]", .expected = "[false]" } } },
    { .name = "C-string dynamic boundary compiles", .c_string_probe = true, .source = "package facts\nDynamic :: (value: string) -> int { return cstring_probe(value); }" },
    { .name = "C-string literal boundary",
      .c_string_probe = true,
      .source = "package facts\nOne :: () -> int { return "
                "cstring_probe(\"a\\u0000b\"); }",
      .message = "C-string boundary 'value' cannot accept NUL at byte 1",
      .same_rejection = true },
    { .name = "C-string parenthesized boundary",
      .c_string_probe = true,
      .source = "package facts\nOne :: () -> int { return "
                "cstring_probe((\"\\u0000b\")); }",
      .message = "C-string boundary 'value' cannot accept NUL at byte 0",
      .same_rejection = true },
};
enum
{
    SAMPLES = sizeof samples / sizeof *samples
};
static compiler_fixture fixture, oracle;
/* The compiler host's natives, and those with the core pack a game links,
 * which declares assert. */
static native_capabilities *linked[2][2];
/* Compile's native tables for the host and for a game, per compiler backend,
 * built once from `linked`. */
static pin_value linked_tables[2][2][2][3];
static arena input, detached;
static compiler_host *host;
static compiler_host_result result;

static void cleanup()
{
    compiler_fixture_free(&fixture);
    compiler_fixture_free(&oracle);
    for (int game = 0; game < 2; game++) {
        for (int probe = 0; probe < 2; probe++) {
            native_capabilities_free(linked[game][probe]);
        }
    }
    memset(linked, 0, sizeof linked);
    memset(linked_tables, 0, sizeof linked_tables);
    arena_free(&input);
    arena_free(&detached);
    compiler_host_free(host);
    host = NULL;
    free(result.json);
    result = (compiler_host_result){ 0 };
}
static pin_value text(const char *s)
{
    pin_value value = { 0 };
    assert(!pin_value_text(&input, &value, s, strlen(s)));
    return value;
}
static pin_value strings(const char *const *items, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = text(items[i]);
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
static bool put(pin_value *row, const char *name, int value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == PIN_I32);
    REQUIRE(
        !pin_write_field(&input, row->ptr, field, (pin_value){ .type = PIN_I32, .i = value })
    );
    return true;
}
static bool put_value(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static pin_value row(int b, const char *name)
{
    const pin_record *shape = compiler_fixture_record(&fixture, b, PACKAGE, name, &input);
    assert(shape);
    pin_value v = { .type = PIN_STRUCT,
                    .ptr = arena_alloc(&input, shape->type.size),
                    .record = shape,
                    .type_name = shape->type.name };
    memset(v.ptr, 0, shape->type.size);
    return v;
}
static pin_value rows(const pin_value *items, int count, pin_type type, const pin_record *shape)
{
    pin_array *
        array = pin_array_snapshot_allocate(&input, type, shape ? shape->type.name : NULL, count);
    array->record = shape;
    for (int i = 0; i < count; i++) {
        array->items[i] = items[i];
    }
    return (pin_value){ .type = type,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array,
                        .record = shape,
                        .type_name = shape ? shape->type.name : NULL };
}
/* A port's storage as Compile takes it: a generic port's type is the one its
 * call infers (0), a wildcard takes each argument's own type (-2), and -1 is
 * a type the compiler cannot pass. */
static int port_storage(script_type type)
{
    unsigned inferred = SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_GENERIC;
    if (type.flags & SCRIPT_TYPE_UNTYPED) {
        if (type.flags & ~inferred) {
            return -1;
        }
        return type.flags & SCRIPT_TYPE_GENERIC ? 0 : -2;
    }
    return type.flags || type.nominal || type.storage == PIN_FLOW ? -1
                                                                  : (int)type.storage;
}
/* The data functions the compiler host links: no flow, outcomes or
 * suspension. */
static bool natives(int b, bool game, bool probe, pin_value *out)
{
    if (linked_tables[game][probe][b][0].ptr) {
        memcpy(out, linked_tables[game][probe][b], sizeof linked_tables[game][probe][b]);
        return true;
    }
    if (!linked[game][probe]) {
        const native_definition_pack packs[] = {
            NATIVE_TEXT_DEFINITION_PACK,   NATIVE_ARRAY_DEFINITION_PACK,
            COMPILER_JSON_DEFINITION_PACK, COMPILER_SYNTAX_DEFINITION_PACK,
            CORE_DEFINITION_PACK,          CSTRING_PACK
        };
        char error[256];
        native_definition_pack selected[6];
        int count = 4;
        memcpy(selected, packs, (size_t)count * sizeof *selected);
        if (game) {
            selected[count++] = CORE_DEFINITION_PACK;
        }
        if (probe) {
            selected[count++] = CSTRING_PACK;
        }
        linked[game][probe] = native_capabilities_from_definition_packs(
            NULL,
            0,
            selected,
            count,
            error,
            sizeof error
        );
        REQUIRE(linked[game][probe]);
    }
    const script_catalog *catalog = native_capabilities_catalog(linked[game][probe]);
    pin_value *functions = NULL, *ports = NULL, *identities = NULL;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_FUNCTION ||
            c->flow.kind != SCRIPT_FLOW_ACCESS_NONE || c->outcome_count ||
            (c->effects & SCRIPT_EFFECT_SUSPEND)) {
            continue;
        }
        pin_value function = row(b, "Native");
        REQUIRE(put_value(&function, "name", text(c->name)));
        REQUIRE(put(&function, "first", acount(ports)));
        REQUIRE(put(&function, "parameters", c->parameter_count));
        REQUIRE(put(&function, "results", c->result_count));
        REQUIRE(put_value(
            &function,
            "variadic",
            (pin_value){ .type = PIN_BOOL, .b = c->variadic || c->variadic_optional }
        ));
        apush(functions, function);
        for (int j = 0; j < c->parameter_count + c->result_count; j++) {
            const script_catalog_parameter *declared = j < c->parameter_count
                                                         ? &c->parameters[j]
                                                         : &c->results[j - c->parameter_count];
            script_type type = declared->type;
            pin_value port = row(b, "Port");
            REQUIRE(put_value(&port, "name", text(declared->name ? declared->name : "")));
            REQUIRE(put(&port, "storage", port_storage(type)));
            REQUIRE(put(&port, "container", (int)type.container));
            REQUIRE(put_value(
                &port,
                "c_string",
                (pin_value){ .type = PIN_BOOL, .b = declared->c_string }
            ));
            apush(ports, port);
        }
        apush(identities, ((pin_value){ .type = PIN_U64, .u64 = c->id }));
        apush(
            identities,
            ((pin_value){ .type = PIN_U64,
                          .u64 = script_catalog_capability_abi(catalog, c) })
        );
    }
    out[0] = rows(
        functions,
        acount(functions),
        PIN_STRUCT,
        functions ? functions[0].record : NULL
    );
    out[1] = rows(ports, acount(ports), PIN_STRUCT, ports ? ports[0].record : NULL);
    out[2] = rows(identities, acount(identities), PIN_U64, NULL);
    memcpy(linked_tables[game][probe][b], out, sizeof linked_tables[game][probe][b]);
    afree(functions);
    afree(ports);
    afree(identities);
    return true;
}
static bool get(const pin_value *row, const char *name, pin_value *out)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, out));
    return true;
}
static uint64_t variant_tag(int b, const char *union_name, const char *variant)
{
    const authoring_package *owner = compiler_fixture_package(&fixture, b, PACKAGE);
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(&fixture, b, PACKAGE, union_name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, variant)) {
            return type->variants[i].tag;
        }
    }
    return 0;
}
static uint64_t tag(int b, const char *variant)
{
    return variant_tag(b, "Result", variant);
}
static bool prepare()
{
    const source_package_input sources[] = { PACKAGE_COMPILER_SOURCE_INPUTS };
    native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                       COMPILER_SYNTAX_DEFINITION_PACK,
                                       CSTRING_PACK };
    REQUIRE(
        compiler_fixture_prepare_packs(&fixture, sources, (int)(sizeof sources / sizeof *sources), packs, 3)
    );
    return true;
}
/* The sample's files: package facts, then lib and core:strings when it
 * uses them. */
static int sample_inputs(const sample *s, source_package_input inputs[SAMPLE_INPUTS])
{
    int count = 0;
    inputs[count++] = (source_package_input){ "facts/Main.chibi",
                                              s->source,
                                              strlen(s->source) };
    if (s->more) {
        inputs[count++] = (source_package_input){ "facts/More.chibi",
                                                  s->more,
                                                  strlen(s->more) };
    }
    if (s->library) {
        inputs[count++] = (source_package_input){ "lib/Lib.chibi",
                                                  s->library,
                                                  strlen(s->library) };
    }
    if (s->core) {
        inputs[count++] = (source_package_input){
            "/sdk/core/strings/Strings.chibi",
            CORE_STRINGS_SOURCE,
            sizeof CORE_STRINGS_SOURCE - 1
        };
    }
    return count;
}
static pin_value files(const source_package_input *inputs, int count, bool sources)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = text(sources ? inputs[i].text : inputs[i].path);
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
/* Compile's arguments for `inputs`: the project, the core collection, the
 * files, debugger points and watches, this host's target and the natives it
 * links: with `game`, also the core pack a game links. */
static bool arguments_for(
    int b,
    bool game,
    bool probe,
    const source_package_input *inputs,
    int count,
    pin_value args[ARGUMENTS]
)
{
    const char *collection_names[] = { "core" };
    const char *collection_paths[] = { "/sdk/core" };
    args[0] = text("/project");
    args[1] = strings(collection_names, 1);
    args[2] = strings(collection_paths, 1);
    args[3] = files(inputs, count, false);
    args[4] = files(inputs, count, true);
    args[DEBUG_ARGUMENT] = (pin_value){ .type = PIN_BOOL, .b = true };
    const pin_record *record = compiler_fixture_record(&fixture, b, PACKAGE, "Target", &input);
    REQUIRE(record);
    args[6] = (pin_value){ .type = PIN_STRUCT,
                           .ptr = arena_alloc(&input, record->type.size),
                           .record = record,
                           .type_name = record->type.name };
    memset(args[6].ptr, 0, record->type.size);
    REQUIRE(put(&args[6], "pointer_storage", BF_POINTER_TYPE));
    REQUIRE(put(&args[6], "pointer_size", (int)sizeof(void *)));
    REQUIRE(put(&args[6], "value_size", (int)sizeof(pin_value)));
    REQUIRE(put(&args[6], "payload_offset", (int)offsetof(pin_value, d)));
    REQUIRE(put(&args[6], "results_offset", (int)offsetof(behavior_function_context, results)));
    REQUIRE(put(&args[6], "type_offset", (int)offsetof(pin_value, type)));
    REQUIRE(put(&args[6], "container_offset", (int)offsetof(pin_value, container)));
    REQUIRE(put(&args[6], "name_offset", (int)offsetof(pin_value, type_name)));
    REQUIRE(put(&args[6], "record_offset", (int)offsetof(pin_value, record)));
    REQUIRE(put(&args[6], "count_offset", (int)offsetof(pin_array, count)));
    REQUIRE(put(&args[6], "items_offset", (int)offsetof(pin_array, items)));
    REQUIRE(put(&args[6], "data_offset", (int)offsetof(pin_array, data)));
    REQUIRE(put(&args[6], "stride_offset", (int)offsetof(pin_array, stride)));
    REQUIRE(put(&args[6], "map_count_offset", (int)offsetof(pin_map, count)));
    REQUIRE(put(&args[6], "data_failure", BEHAVIOR_FUNCTION_HOST_DATA));
    REQUIRE(put(&args[6], "argument_failure", BEHAVIOR_FUNCTION_ARGUMENT));
    REQUIRE(put(&args[6], "assert_failure", BEHAVIOR_FUNCTION_DATA));
    args[7] = (pin_value){ .type = PIN_U64, .u64 = execution_prepared_abi() };
    int instructions = execution_prepared_instruction_count();
    pin_array *names = pin_array_snapshot_allocate(&input, PIN_STR, NULL, instructions);
    for (int i = 0; i < instructions; i++) {
        names->items[i] = text(execution_prepared_instruction_name(i));
    }
    args[8] = (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = names };
    REQUIRE(natives(b, game, probe, &args[9]));
    return true;
}
/* Ordinary compilation and bootstrap use only production host natives. */
static bool arguments(int b, bool game, const source_package_input *inputs, int count, pin_value args[ARGUMENTS])
{
    return arguments_for(b, game, false, inputs, count, args);
}
static bool compile(
    int b,
    bool game,
    bool probe,
    const source_package_input *inputs,
    int count,
    uintptr_t budget,
    pin_value *out
)
{
    pin_value args[ARGUMENTS];
    REQUIRE(arguments_for(b, game, probe, inputs, count, args));
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, "Compile", args, ARGUMENTS, budget, &detached, out)
    );
    return true;
}
static uint64_t function_id(const char *name)
{
    char text[256];
    snprintf(text, sizeof text, "project:facts:1:%s:callable", name);
    return script_catalog_id("package-symbol", text);
}
static bool run(const unsigned char *bytes, size_t length, const sample_call *call, int backend)
{
    char error[256];
    host = compiler_host_load(
        bytes,
        length,
        backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    if (!host) {
        fprintf(stderr, "package compiler load: %s\n", error);
    }
    REQUIRE(host);
    if (getenv("CHIBI_TRACE_PACKAGE_CALLS")) {
        fprintf(
            stderr,
            "package compiler call: backend=%d function=%s arguments=%s\n",
            backend,
            call->function,
            call->arguments
        );
    }
    bool valid = compiler_host_invoke(
        host,
        function_id(call->function),
        call->arguments,
        strlen(call->arguments),
        TARGET_BUDGET,
        &result
    );
    bool passed = valid && result.flow_status == FLOW_OK;
    bool expected = call->expected ? passed && !strcmp(result.json, call->expected)
                                   : !passed;
    if (call->diagnostic) {
        expected = expected && strstr(result.diagnostic, call->diagnostic);
    }
    if (!expected) {
        fprintf(
            stderr,
            "package compiler run %s%s: %d %s %s\n",
            call->function,
            call->arguments,
            result.flow_status,
            valid && result.json ? result.json : "",
            result.diagnostic
        );
    }
    REQUIRE(expected);
    compiler_host_free(host);
    host = NULL;
    free(result.json);
    result = (compiler_host_result){ 0 };
    return true;
}
/* Whether a point is one this compiler places as stage zero does: a
 * function's entry and completions, each statement's, each evaluated
 * expression's and each symbol's it reads or stores. */
static bool placed(const behavior_source_point *point)
{
    return point->kind == BEHAVIOR_SOURCE_FUNCTION ||
           point->kind == BEHAVIOR_SOURCE_STATEMENT ||
           point->kind == BEHAVIOR_SOURCE_EXPRESSION ||
           point->kind == BEHAVIOR_SOURCE_SYMBOL;
}
static void print_points(const char *compiler, const behavior_function *f)
{
    for (int k = 0; k < acount(f->source_points); k++) {
        const behavior_source_point *p = &f->source_points[k];
        fprintf(
            stderr,
            "  %s point kind %d at %d..%d occurrence %016llx completion %d\n",
            compiler,
            p->kind,
            p->start,
            p->end,
            (unsigned long long)p->occurrence,
            p->completion
        );
    }
}
static bool same_point(const behavior_source_point *x, const behavior_source_point *y)
{
    return x->kind == y->kind && x->occurrence == y->occurrence &&
           x->start == y->start && x->end == y->end && x->line == y->line &&
           x->column == y->column && x->completion == y->completion &&
           (x->kind != BEHAVIOR_SOURCE_FUNCTION || x->meaning == y->meaning);
}
/* Each function of stage one's module has stage zero's function and
 * statement points, as many of each, in any order. */
static bool same_points(const execution_module *expected, const execution_module *actual)
{
    REQUIRE(expected->plan.function_count == actual->plan.function_count);
    bool all_alike = true;
    for (int i = 0; i < expected->plan.function_count; i++) {
        const behavior_function *x = expected->plan.functions[i].body,
                                *y = actual->plan.functions[i].body;
        int count = acount(y->source_points);
        bool *used = xcalloc((size_t)count + 1, sizeof *used);
        bool alike = true;
        int placed_count = 0;
        for (int j = 0; alike && j < acount(x->source_points); j++) {
            if (!placed(&x->source_points[j])) {
                continue;
            }
            placed_count++;
            int found = -1;
            for (int k = 0; found < 0 && k < count; k++) {
                found = !used[k] && same_point(&x->source_points[j], &y->source_points[k])
                          ? k
                          : -1;
            }
            alike = found >= 0;
            if (alike) {
                used[found] = true;
            } else {
                fprintf(
                    stderr,
                    "function %d lacks stage zero's point kind %d at %d..%d\n",
                    i,
                    x->source_points[j].kind,
                    x->source_points[j].start,
                    x->source_points[j].end
                );
            }
        }
        for (int k = 0; alike && k < count; k++) {
            alike = !placed(&y->source_points[k]) || used[k];
            placed_count -= placed(&y->source_points[k]);
            if (!alike) {
                fprintf(
                    stderr,
                    "function %d has an extra point at %d..%d\n",
                    i,
                    y->source_points[k].start,
                    y->source_points[k].end
                );
            }
        }
        free(used);
        if (!alike || placed_count != 0) {
            print_points("stage zero", x);
            print_points("stage one", y);
            all_alike = false;
        }
    }
    return all_alike;
}
static bool same_watch(const behavior_source_watch *x, const behavior_source_watch *y)
{
    const char *a = x->initial.type_name ? x->initial.type_name : "",
               *b = y->initial.type_name ? y->initial.type_name : "";
    return x->origin.kind == y->origin.kind &&
           x->origin.occurrence == y->origin.occurrence &&
           x->origin.start == y->origin.start && x->origin.end == y->origin.end &&
           x->origin.line == y->origin.line && x->origin.column == y->origin.column &&
           x->initial.type == y->initial.type &&
           x->initial.container == y->initial.container && !strcmp(a, b) &&
           x->ordinal == y->ordinal && x->output == y->output &&
           x->reference == y->reference;
}
static void print_watches(const char *compiler, const behavior_function *f)
{
    for (int k = 0; k < acount(f->source_watches); k++) {
        const behavior_source_watch *w = &f->source_watches[k];
        fprintf(
            stderr,
            "  %s watch at %d..%d occurrence %016llx type %d container %d name "
            "%s ordinal %d output %d "
            "reference %d\n",
            compiler,
            w->origin.start,
            w->origin.end,
            (unsigned long long)w->origin.occurrence,
            w->initial.type,
            w->initial.container,
            w->initial.type_name ? w->initial.type_name : "-",
            w->ordinal,
            w->output,
            w->reference
        );
    }
}
/* Each function of stage one's module has stage zero's watches, as many of
 * each, in any order. */
static bool same_watches(const execution_module *expected, const execution_module *actual)
{
    REQUIRE(expected->plan.function_count == actual->plan.function_count);
    bool all_alike = true;
    for (int i = 0; i < expected->plan.function_count; i++) {
        const behavior_function *x = expected->plan.functions[i].body,
                                *y = actual->plan.functions[i].body;
        int count = acount(y->source_watches);
        bool *used = xcalloc((size_t)count + 1, sizeof *used);
        bool alike = count == acount(x->source_watches);
        for (int j = 0; alike && j < acount(x->source_watches); j++) {
            int found = -1;
            for (int k = 0; found < 0 && k < count; k++) {
                found = !used[k] && same_watch(&x->source_watches[j], &y->source_watches[k])
                          ? k
                          : -1;
            }
            alike = found >= 0;
            if (alike) {
                used[found] = true;
            }
        }
        free(used);
        if (!alike) {
            fprintf(stderr, "WATCHES %d\n", i);
            print_watches("stage zero", x);
            print_watches("stage one", y);
            all_alike = false;
        }
    }
    return all_alike;
}
/* Stage zero's closure of the same packages, whose calls must give the same
 * results. */
static bool oracle_closure(const sample *s, unsigned char **closure, size_t *length)
{
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(s, inputs);
    compiler_fixture_free(&oracle);
    /* The natives the stage-one target links. */
    native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                       COMPILER_SYNTAX_DEFINITION_PACK,
                                       CSTRING_PACK,
                                       CORE_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&oracle, inputs, count, packs, s->game ? 4 : 3));
    const authoring_packages *owner = oracle.owners[0];
    package_module rows[3];
    REQUIRE(authoring_packages_count(owner) <= 3);
    int written = 0;
    char error[256];
    bool valid = true;
    for (int p = 0; valid && p < authoring_packages_count(owner); p++) {
        const authoring_package *package = authoring_packages_at(owner, p);
        unsigned char *prepared = NULL;
        size_t size = 0;
        valid = package->module &&
                execution_prepared_write(package->module, &prepared, &size, error, sizeof error);
        rows[written++] = (package_module){ package->identity,
                                            valid ? execution_module_id(package->module)
                                                  : 0,
                                            prepared,
                                            size };
    }
    const authoring_package *root = compiler_fixture_package(&oracle, 0, "project:facts");
    valid = valid && root &&
            compiler_artifact_write(
                rows,
                written,
                execution_module_id(root->module),
                closure,
                length,
                error,
                sizeof error
            );
    for (int i = 0; i < written; i++) {
        free((void *)rows[i].data);
    }
    REQUIRE(valid);
    return true;
}
/* The closure of a Compiled result. */
static unsigned char *closure_bytes(const pin_union *value, size_t *length)
{
    const pin_array *array = value->values[1].ptr;
    *length = (size_t)array->count;
    unsigned char *bytes = xmalloc(*length);
    for (size_t i = 0; i < *length; i++) {
        bytes[i] = array->items[i].u8;
    }
    return bytes;
}
/* The values `Build(count)` copies in `host`. */
static uint64_t copies(int count)
{
    char arguments[32];
    snprintf(arguments, sizeof arguments, "[%d]", count);
    pin_copy_scope scope = { 0 };
    pin_copy_scope_begin(&scope);
    bool called = compiler_host_invoke(
        host,
        function_id("Build"),
        arguments,
        strlen(arguments),
        TARGET_BUDGET,
        &result
    );
    pin_copy_scope_end(&scope);
    free(result.json);
    result = (compiler_host_result){ 0 };
    return called ? scope.counts.value_header_moves : UINT64_MAX;
}
static bool updates_in_place(const unsigned char *bytes, size_t length, const char *name)
{
    char error[256];
    host = compiler_host_load(bytes, length, TEST_COMPILED_BACKEND, error, sizeof error);
    if (!host) {
        fprintf(stderr, "package compiler %s load: %s\n", name, error);
    }
    REQUIRE(host);
    uint64_t fewer = copies(100), more = copies(200);
    if (!fewer || fewer == UINT64_MAX || more > 2 * fewer) {
        fprintf(
            stderr,
            "package compiler %s copies %llu then %llu\n",
            name,
            (unsigned long long)fewer,
            (unsigned long long)more
        );
    }
    REQUIRE(fewer && fewer != UINT64_MAX && more <= 2 * fewer);
    compiler_host_free(host);
    host = NULL;
    return true;
}
/* How many of `f`'s instructions load an array's stride. */
static int stride_loads(const behavior_function *f)
{
    int loads = 0;
    for (int i = 0; i < acount(f->instructions); i++) {
        loads += f->instructions[i].op == BF_LOAD &&
                 f->instructions[i].offset == offsetof(pin_array, stride);
    }
    return loads;
}
/* An array of strings, unions or records with a string field is never packed,
 * so stage one reads its elements from `items` alone; any other array may be,
 * so its reads still choose the representation by its stride. In the root
 * module, only the Packed functions load a stride. */
static bool reads_boxed(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler boxed load: %s\n", error);
    }
    REQUIRE(packages);
    int boxed = 0, packed = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool box = !strncmp(f.entry, "Boxed", 5),
                 pack = !strncmp(f.entry, "Packed", 6);
            int loads = stride_loads(f.body);
            boxed += box;
            packed += pack;
            if ((box && loads) || (pack && !loads)) {
                fprintf(stderr, "package compiler %s loads a stride %d times\n", f.entry, loads);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(boxed == 4 && packed == 1 && !wrong);
    return true;
}
/* An intrinsic's capability ID is "CHII" and its number. */
static const uint64_t SNAPSHOT_CAPABILITY = UINT64_C(0x4348494900000000) |
                                            EXECUTION_INTRINSIC_SNAPSHOT;
static int snapshots(const behavior_function *f)
{
    int count = 0;
    for (int i = 0; i < acount(f->host_calls); i++) {
        count += f->host_calls[i].capability_id == SNAPSHOT_CAPABILITY;
    }
    return count;
}
/* A literal or an authored function's result is new in the function's
 * storage, so stage one moves it into a local or a return without a snapshot;
 * an element read from an array is still copied. */
static bool moves_fresh_values(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler moves load: %s\n", error);
    }
    REQUIRE(packages);
    int moved = 0, copied = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool move = !strncmp(f.entry, "Moved", 5),
                 copy = !strcmp(f.entry, "Copied");
            int count = snapshots(f.body);
            moved += move;
            copied += copy;
            if ((move && count) || (copy && !count)) {
                fprintf(stderr, "package compiler %s snapshots %d values\n", f.entry, count);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(moved == 4 && copied == 1 && !wrong);
    return true;
}
static int union_calls(const behavior_function *f, const node_host_call *call)
{
    int count = 0;
    for (int i = 0; i < acount(f->host_calls); i++) {
        count += f->host_calls[i].capability_id == execution_intrinsic_id(call);
    }
    return count;
}
/* Variants built in the caller's storage, keeping the locals built there. */
static int kept_unions(const behavior_function *f)
{
    int count = 0;
    for (int i = 0; i < acount(f->host_calls); i++) {
        count += f->host_calls[i].capability_id ==
                 (execution_intrinsic_id(&EXECUTION_union_keep_call) |
                  EXECUTION_INTRINSIC_IN_RESULT);
    }
    return count;
}
/* A returned variant keeps its payload values, which the return copies to the
 * caller; one kept in a local copies them, since the local's array changes. */
static bool moves_returned_variants(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler variants load: %s\n", error);
    }
    REQUIRE(packages);
    int kept = 0, copied = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool keep = !strncmp(f.entry, "Kept", 4),
                 copy = !strncmp(f.entry, "Copied", 6);
            int moves = kept_unions(f.body),
                makes = union_calls(f.body, &EXECUTION_union_make_call);
            kept += keep;
            copied += copy;
            if ((keep && (moves != 1 || makes || snapshots(f.body))) ||
                (copy && (moves || makes != 1))) {
                fprintf(stderr, "package compiler %s moves %d and makes %d variants\n", f.entry, moves, makes);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(kept == 3 && copied == 1 && !wrong);
    return true;
}
/* A function builds a local array it returns in its caller's storage; one
 * whose local it assigns does not. */
static bool builds_results(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler built load: %s\n", error);
    }
    REQUIRE(packages);
    int built = 0, copied = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool build = !strncmp(f.entry, "Built", 5),
                 copy = !strncmp(f.entry, "Copied", 6);
            built += build;
            copied += copy;
            if ((build && !f.body->builds_results) || (copy && f.body->builds_results)) {
                fprintf(
                    stderr,
                    "package compiler %s builds results: %d\n",
                    f.entry,
                    f.body->builds_results
                );
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(built == 2 && copied == 1 && !wrong);
    return true;
}
/* A loop over an array nothing changes, and a local that reads an element of
 * it, read in place; a loop whose body changes its array walks a copy. */
static bool reads_in_place(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler aliases load: %s\n", error);
    }
    REQUIRE(packages);
    int aliased = 0, copied = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool alias = !strncmp(f.entry, "Aliased", 7),
                 copy = !strcmp(f.entry, "CopiedWalk");
            int count = snapshots(f.body);
            aliased += alias;
            copied += copy;
            if ((alias && count) || (copy && !count)) {
                fprintf(stderr, "package compiler %s snapshots %d values\n", f.entry, count);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(aliased == 1 && copied == 1 && !wrong);
    return true;
}
/* Literals whose binary64 and binary32 readings LiteralBits must agree on:
 * exact and halfway values, the ends of both ranges, long and hexadecimal
 * spellings. */
static const char *const literal_spellings[] = {
    "0",
    "0.0",
    "000",
    "1",
    "007.5",
    "0.5",
    "0.1",
    "0.2",
    "0.3",
    "0.001",
    "384.0",
    "1e10",
    "1e22",
    "1e23",
    "3.14159265358979323846264338327950288",
    "123456789012345678901234567890",
    "9007199254740992",
    "9007199254740993",
    "9007199254740995",
    "18446744073709551615",
    "1.00000000000000011102230246251565404236316680908203125",
    "1.00000000000000011102230246251565404236316680908203124",
    "1.00000000000000011102230246251565404236316680908203126",
    "1.000000059604644775390625",
    "1.000000059604644775390624",
    "1.7976931348623157e308",
    "1.7976931348623158e308",
    "1.7976931348623159e308",
    "1e308",
    "1e309",
    "2.2250738585072014e-308",
    "2.2250738585072011e-308",
    "2.2250738585072012e-308",
    "2.2250738585072013e-308",
    "1.000000178813934326171875",
    "4.9e-324",
    "1e-400",
    "3.4028234663852886e38",
    "3.4028235e38",
    "3.4028235677973366e38",
    "3.4028236e38",
    "1.17549435e-38",
    "1.4e-45",
    "1e-46",
    "1e-30",
    "0x0",
    "0x1F",
    "0x10000000000001",
    "0x1FFFFFFFFFFFFF",
    "0x20000000000001",
    "0x8000000000000000",
    "0xFFFFFFFFFFFFFFFF",
    "1e0",
    "1E+2",
    "25e-1",
    "0.000000000000000000001",
    "100000000000000000000000000000000000000000000000000",
    "1.5e300",
    "6.02214076e23",
};
/* Whether a literal's significant digits are nonzero. */
static bool digits_nonzero(const char *text)
{
    text += *text == '-';
    bool hexadecimal = text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    for (const char *at = hexadecimal ? text + 2 : text; *at; at++) {
        if (!hexadecimal && (*at == 'e' || *at == 'E')) {
            return false;
        }
        if (*at != '0' && *at != '.') {
            return true;
        }
    }
    return false;
}
/* The reading LiteralBits implements, which is stage zero's where long double
 * is double: the binary64 nearest the literal and the binary32 nearest that;
 * a nonzero literal whose binary64 is infinite, zero or subnormal is out of
 * range. */
static void literal_reading(const char *text, bool *finite, uint64_t *wide, bool *fits, uint64_t *narrow)
{
    char *end;
    double value = strtod(text, &end);
    *finite = !*end && isfinite(value) && !(value != 0 && fabs(value) < DBL_MIN) &&
              !(value == 0 && digits_nonzero(text));
    memcpy(wide, &value, sizeof *wide);
    *fits = fabs(value) <= FLT_MAX;
    float single = (float)value;
    uint32_t bits;
    memcpy(&bits, &single, sizeof bits);
    *narrow = bits;
}
static bool literal_agrees(int b, const char *spelling, bool negative)
{
    char spelling_text[2100];
    snprintf(spelling_text, sizeof spelling_text, "%s%s", negative ? "-" : "", spelling);
    bool finite, fits;
    uint64_t wide, narrow;
    literal_reading(spelling_text, &finite, &wide, &fits, &narrow);
    pin_value args[] = { text(spelling),
                         (pin_value){ .type = PIN_BOOL, .b = negative } };
    pin_value out;
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, "LiteralBits", args, 2, COMPILE_BUDGET, &detached, &out)
    );
    const pin_union *value = out.ptr;
    bool read = value->tag == variant_tag(b, "FloatBits", "Finite");
    bool same = read == finite;
    if (same && read) {
        same = value->values[0].u64 == wide && value->values[1].b == fits &&
               (!fits || value->values[2].u64 == narrow);
    }
    if (!same) {
        fprintf(
            stderr,
            "package compiler literal %s: expected %d %016llx %d %08llx, read "
            "%d %016llx %d %08llx\n",
            spelling_text,
            finite,
            (unsigned long long)wide,
            fits,
            (unsigned long long)narrow,
            read,
            read ? (unsigned long long)value->values[0].u64 : 0ull,
            read && value->values[1].b,
            read ? (unsigned long long)value->values[2].u64 : 0ull
        );
    }
    arena_free(&detached);
    return same;
}
/* Spellings longer than the listed ones: the exact decimals of binary64
 * values that are binary32 ties, and 800 digits followed by one that decides
 * the rounding, or by an exponent that moves the point back. */
static int long_spellings(char spellings[][2048], int capacity)
{
    static const double ties[] = { 0x3p-150, 0x5p-150, 0x1.000001p0, 0x1.000003p0, 0x1.fffffe8p127 };
    int count = 0;
    for (int i = 0; i < (int)(sizeof ties / sizeof *ties) && count < capacity; i++) {
        snprintf(spellings[count++], sizeof spellings[0], "%.200e", ties[i]);
    }
    char *sticky = spellings[count++];
    int length = snprintf(sticky, sizeof spellings[0], "%s", "1.00000000000000011102230246251565404236316680908203125");
    for (int i = 0; i < 800; i++) {
        sticky[length++] = '0';
    }
    sticky[length++] = '1';
    sticky[length] = 0;
    char *shifted = spellings[count++];
    shifted[0] = '1';
    for (int i = 1; i <= 800; i++) {
        shifted[i] = '0';
    }
    snprintf(shifted + 801, sizeof spellings[0] - 801, "%s", "e-800");
    return count;
}
/* LiteralBits reads number literals of floating types by that reading:
 * the listed spellings on both compiler backends, with both signs on the
 * compiled one, and there also the long spellings and generated ones across
 * the binary64 range. */
static bool literals_agree()
{
    static char spellings[8][2048];
    int long_count = long_spellings(spellings, 8);
    for (int i = 0; i < (int)(sizeof literal_spellings / sizeof *literal_spellings);
         i++) {
        REQUIRE(literal_agrees(0, literal_spellings[i], false));
        REQUIRE(literal_agrees(1, literal_spellings[i], false));
        REQUIRE(literal_agrees(1, literal_spellings[i], true));
    }
    for (int i = 0; i < long_count; i++) {
        REQUIRE(literal_agrees(1, spellings[i], false));
    }
    uint32_t state = 12345;
    for (int i = 0; i < 120; i++) {
        char spelling[96];
        int length = 0;
        state = state * 1103515245u + 12345u;
        int digits = 1 + (int)(state >> 16) % 19;
        for (int d = 0; d < digits; d++) {
            state = state * 1103515245u + 12345u;
            spelling[length++] = (char)('0' + (state >> 16) % 10);
            if (d == digits / 2 && d + 1 < digits && (state & 1)) {
                spelling[length++] = '.';
            }
        }
        state = state * 1103515245u + 12345u;
        int exponent = (int)((state >> 8) % 640) - 330;
        length += snprintf(spelling + length, sizeof spelling - (size_t)length, "e%d", exponent);
        REQUIRE(literal_agrees(1, spelling, false));
    }
    return true;
}
/* The packages of a closure, linked against the compiler host's natives,
 * which `native` holds; NULL when it does not load. */
static package_collection *read_packages(const unsigned char *bytes, size_t length, native_capabilities **native)
{
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK,
                                             CSTRING_PACK };
    char error[256];
    *native = native_capabilities_from_definition_packs(NULL, 0, packs, 5, error, sizeof error);
    assert(*native);
    native_prepared_context context = { .native = *native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    linker.decoded_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES;
    linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        &linker,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler load: %s\n", error);
    }
    return packages;
}
/* Each module of stage one's closure has the function and statement points of
 * the stage-zero module with its ID that oracle_closure prepared. */
static bool points_like_oracle(const unsigned char *bytes, size_t length)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native);
    REQUIRE(packages);
    const authoring_packages *owner = oracle.owners[0];
    bool alike = package_collection_count(packages) == authoring_packages_count(owner);
    for (int p = 0; alike && p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        const execution_module *expected = NULL;
        for (int q = 0; !expected && q < authoring_packages_count(owner); q++) {
            const execution_module *candidate = authoring_packages_at(owner, q)->module;
            expected = execution_module_id(candidate) == execution_module_id(module)
                         ? candidate
                         : NULL;
        }
        alike = expected && same_points(expected, module) &&
                same_watches(expected, module);
    }
    package_collection_free(packages);
    native_capabilities_free(native);
    return alike;
}
/* The samples of `group` compiled by stage one on compiler backend `b`.
 * Stage one compiles slowest on the bytecode backend, so each backend has a
 * selector of its own; only it compares with stage zero's closure. */
static bool rejects_like_oracle(const sample *s, const char *message, int line, int column)
{
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(s, inputs);
    source_package_snapshot_options options = { .project_root = "/compiler",
                                                .files = inputs,
                                                .file_count = count };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
    script_lower_bindings bindings = authoring_native_bindings(fixture.native);
    authoring_packages *packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(fixture.native),
        &bindings,
        BEHAVIOR_BYTECODE,
        NULL
    );
    REQUIRE(packages && !authoring_packages_valid(packages));
    const authoring_package *root = NULL;
    for (int p = 0; p < authoring_packages_count(packages); p++) {
        const authoring_package *candidate = authoring_packages_at(packages, p);
        if (!strcmp(candidate->identity, "project:facts")) {
            root = candidate;
        }
    }
    REQUIRE(root && root->diagnostic_count);
    const script_diagnostic *expected = &root->diagnostics[0];
    bool same = !strcmp(expected->message, s->oracle_message ? s->oracle_message : message) &&
                expected->range.line == line && expected->range.column == column;
    if (!same) {
        fprintf(
            stderr,
            "stage zero %s: %s at %d:%d; stage one at %d:%d\n",
            s->name,
            expected->message,
            expected->range.line,
            expected->range.column,
            line,
            column
        );
    }
    authoring_packages_free(packages);
    REQUIRE(same);
    return true;
}
static bool run_samples(sample_group group, int b)
{
    REQUIRE(prepare());
    for (int i = 0; i < (int)SAMPLES; i++) {
        const sample *s = &samples[i];
        if (s->group != group) {
            continue;
        }
        source_package_input inputs[SAMPLE_INPUTS];
        int count = sample_inputs(s, inputs);
        pin_value out;
        bool compiled = compile(b, s->game, s->c_string_probe, inputs, count, COMPILE_BUDGET, &out);
        if (!compiled) {
            fprintf(
                stderr,
                "package compiler sample failed: name=%s index=%d group=%d "
                "backend=%s inputs=%d source_bytes=%zu budget=%d\n",
                s->name,
                i,
                group,
                b ? "compiled" : "bytecode",
                count,
                strlen(s->source),
                COMPILE_BUDGET
            );
        }
        REQUIRE(compiled);
        const pin_union *value = out.ptr;
        REQUIRE(out.type == PIN_UNION && value);
        if (s->message) {
            REQUIRE(value->tag == tag(b, "Rejected"));
            pin_value message, line, column;
            REQUIRE(get(&value->values[0], "message", &message));
            REQUIRE(
                get(&value->values[0], "line", &line) &&
                get(&value->values[0], "column", &column)
            );
            bool position = s->same_rejection
                              ? rejects_like_oracle(
                                    s,
                                    pin_text(message).bytes,
                                    line.i,
                                    column.i
                                )
                              : line.i == s->line && column.i == s->column;
            if (strcmp(pin_text(message).bytes, s->message) || !position) {
                fprintf(
                    stderr,
                    "package compiler %s: %s at %d:%d\n",
                    s->name,
                    pin_text(message).bytes,
                    line.i,
                    column.i
                );
            }
            REQUIRE(!strcmp(pin_text(message).bytes, s->message) && position);
            arena_free(&detached);
            continue;
        }
        if (value->tag != tag(b, "Compiled")) {
            pin_value message, line, column;
            get(&value->values[0], "message", &message);
            get(&value->values[0], "line", &line);
            get(&value->values[0], "column", &column);
            fprintf(
                stderr,
                "package compiler %s rejected: %s at %d:%d\n",
                s->name,
                pin_text(message).bytes,
                line.i,
                column.i
            );
        }
        REQUIRE(value->tag == tag(b, "Compiled") && value->count == 2);
        size_t length;
        unsigned char *bytes = closure_bytes(value, &length);
        arena_free(&detached);
        unsigned char *expected = NULL;
        size_t expected_length = 0;
        bool ran = (!s->updates || updates_in_place(bytes, length, s->name)) &&
                   (!s->boxed || reads_boxed(bytes, length)) &&
                   (!s->moves || moves_fresh_values(bytes, length)) &&
                   (!s->aliases || reads_in_place(bytes, length)) &&
                   (!s->variants || moves_returned_variants(bytes, length)) &&
                   (!s->built || builds_results(bytes, length)) &&
                   (b || (oracle_closure(s, &expected, &expected_length) &&
                          points_like_oracle(bytes, length)));
        for (int c = 0; ran && c < CALLS && s->calls[c].function; c++) {
            for (int backend = 0; ran && backend < 2; backend++) {
                ran = run(bytes, length, &s->calls[c], backend);
            }
            ran = ran && (b || run(expected, expected_length, &s->calls[c], 0));
        }
        free(bytes);
        free(expected);
        REQUIRE(ran);
    }
    return true;
}
/* Without debugger points and watches, a closure's functions have none, and
 * its loops and branches still run as they do with them. */
TEST_CASE(package_compiler_omits_debug_info)
{
    REQUIRE(prepare());
    const char *const names[] = { "loops", "control" };
    for (size_t n = 0; n < sizeof names / sizeof *names; n++) {
        const sample *s = samples;
        while (strcmp(s->name, names[n])) {
            s++;
        }
        source_package_input inputs[SAMPLE_INPUTS];
        int count = sample_inputs(s, inputs);
        pin_value args[ARGUMENTS], out;
        REQUIRE(arguments(1, false, inputs, count, args));
        args[DEBUG_ARGUMENT].b = false;
        REQUIRE(
            compiler_fixture_call(&fixture, 1, PACKAGE, "Compile", args, ARGUMENTS, COMPILE_BUDGET, &detached, &out)
        );
        const pin_union *value = out.ptr;
        REQUIRE(out.type == PIN_UNION && value && value->tag == tag(1, "Compiled"));
        size_t length;
        unsigned char *bytes = closure_bytes(value, &length);
        arena_free(&detached);
        native_capabilities *native;
        package_collection *packages = read_packages(bytes, length, &native);
        REQUIRE(packages);
        int debug = 0;
        for (int p = 0; p < package_collection_count(packages); p++) {
            const execution_module_plan *plan = &package_collection_module(packages, p)
                                                     ->plan;
            for (int i = 0; i < plan->function_count; i++) {
                debug += acount(plan->functions[i].body->source_points) +
                         acount(plan->functions[i].body->source_watches);
            }
        }
        package_collection_free(packages);
        native_capabilities_free(native);
        bool ran = !debug;
        for (int c = 0; ran && c < CALLS && s->calls[c].function; c++) {
            for (int backend = 0; ran && backend < 2; backend++) {
                ran = run(bytes, length, &s->calls[c], backend);
            }
        }
        free(bytes);
        REQUIRE(ran);
    }
    return true;
}
TEST_CASE(package_compiler_builds_runnable_packages)
{
    return run_samples(BASE_SAMPLES, 0);
}
TEST_CASE(package_compiler_builds_runnable_packages_compiled)
{
    return run_samples(BASE_SAMPLES, 1);
}
TEST_CASE(package_compiler_builds_runnable_typed_packages)
{
    return run_samples(TYPE_SAMPLES, 0);
}
TEST_CASE(package_compiler_builds_runnable_typed_packages_compiled)
{
    return run_samples(TYPE_SAMPLES, 1);
}
TEST_CASE(package_compiler_builds_runnable_collection_packages)
{
    return run_samples(COLLECTION_SAMPLES, 0);
}
TEST_CASE(package_compiler_builds_runnable_collection_packages_compiled)
{
    return run_samples(COLLECTION_SAMPLES, 1);
}
TEST_CASE(package_compiler_builds_runnable_importing_packages)
{
    return run_samples(IMPORT_SAMPLES, 0);
}
TEST_CASE(package_compiler_builds_runnable_importing_packages_compiled)
{
    return run_samples(IMPORT_SAMPLES, 1);
}
/* Values as the compiler host takes arguments and projects results: a JSON
 * array; malloc-owned. */
static char *document_json(const pin_value *values, int count, size_t *length)
{
    av_doc *doc = av_doc_new();
    av_value *array = av_arr(doc);
    for (int i = 0; i < count; i++) {
        av_value *value = pin_value_to_document(doc, &values[i]);
        assert(value);
        av_arr_append(array, value);
    }
    yyjson_mut_doc *encoded = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(encoded, av_to_json(encoded, array));
    char *json = yyjson_mut_write(encoded, YYJSON_WRITE_ALLOW_INVALID_UNICODE, length);
    yyjson_mut_doc_free(encoded);
    av_doc_free(doc);
    return json;
}
/* Stage two's rejection of the compiler's sources, from Compile's result. */
static void stage_two_rejection(const char *json, size_t length)
{
    bool called = compiler_host_invoke(
        host,
        script_catalog_id("package-symbol", "project:package_compiler:1:Compile:callable"),
        json,
        length,
        SELF_BUDGET,
        &result
    );
    fprintf(
        stderr,
        "package compiler stage two rejected: %s\n",
        called ? result.json : result.diagnostic
    );
}
/* Writes bytes to folder/name. */
static bool write_seed_file(const char *folder, const char *name, const void *bytes, size_t length)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", folder, name);
    FILE *file = fopen(path, "wb");
    bool written = file && fwrite(bytes, 1, length, file) == length;
    if (file) {
        written = !fclose(file) && written;
    }
    return written;
}
/* With CHIBI_BOOTSTRAP_SEED naming a folder, leaves stage two and Compile's
 * arguments there, the seed make bootstrap-artifact writes the bootstrap
 * artifact from. */
static bool write_seed(const unsigned char *stage_two, size_t length, const char *json, size_t json_length)
{
    const char *seed = getenv("CHIBI_BOOTSTRAP_SEED");
    return !seed || (write_seed_file(seed, "compiler.chibip", stage_two, length) &&
                     write_seed_file(seed, "compile.json", json, json_length));
}
/* Stage two reads the listed floating literals as stage one does: each
 * LiteralBits result projects to the same JSON. */
static bool stage_two_literals()
{
    uint64_t entry = script_catalog_id("package-symbol", "project:package_compiler:1:LiteralBits:callable");
    for (int i = 0; i < (int)(sizeof literal_spellings / sizeof *literal_spellings);
         i++) {
        for (int sign = 0; sign < 2; sign++) {
            pin_value args[] = { text(literal_spellings[i]),
                                 (pin_value){ .type = PIN_BOOL, .b = sign } },
                      out;
            REQUIRE(
                compiler_fixture_call(&fixture, 1, PACKAGE, "LiteralBits", args, 2, COMPILE_BUDGET, &detached, &out)
            );
            size_t length, expected_length;
            char *json = document_json(args, 2, &length);
            char *expected = document_json(&out, 1, &expected_length);
            arena_free(&detached);
            bool called = compiler_host_invoke(host, entry, json, length, COMPILE_BUDGET, &result);
            bool same = called && result.flow_status == FLOW_OK &&
                        result.length == expected_length &&
                        !memcmp(result.json, expected, expected_length);
            if (!same) {
                fprintf(
                    stderr,
                    "package compiler stage two literal %s%s: %s\n",
                    sign ? "-" : "",
                    literal_spellings[i],
                    called ? result.json : result.diagnostic
                );
            }
            free(json);
            free(expected);
            free(result.json);
            result = (compiler_host_result){ 0 };
            REQUIRE(same);
        }
    }
    return true;
}
/* Whether stage two, loaded in `host`, compiles sample `s` as stage one does:
 * to the same closure, or to the same rejection. A closure is compared as
 * bytes, since its Result as JSON outgrows the host's projection limits. */
static bool stage_two_sample(const sample *s)
{
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(s, inputs);
    pin_value args[ARGUMENTS], out;
    REQUIRE(arguments_for(1, s->game, s->c_string_probe, inputs, count, args));
    REQUIRE(
        compiler_fixture_call(&fixture, 1, PACKAGE, "Compile", args, ARGUMENTS, COMPILE_BUDGET, &detached, &out)
    );
    const pin_union *value = out.ptr;
    size_t length, expected_length;
    char *json = document_json(args, ARGUMENTS, &length);
    bool compiled = value->tag == tag(1, "Compiled");
    unsigned char *expected = compiled
                                ? closure_bytes(value, &expected_length)
                                : (unsigned char *)document_json(&out, 1, &expected_length);
    arena_free(&detached);
    bool same;
    if (compiled) {
        compiler_host_bytes again = { 0 };
        same = compiler_host_invoke_bytes(
                   host,
                   script_catalog_id("package-symbol", "project:package_compiler:1:CompileClosure:callable"),
                   json,
                   length,
                   SELF_BUDGET,
                   COMPILER_HOST_MAX_OUTPUT_BYTES,
                   &again
               ) &&
               again.flow_status == FLOW_OK && again.length == expected_length &&
               !memcmp(again.bytes, expected, expected_length);
        if (!same) {
            fprintf(
                stderr,
                "package compiler stage two %s: %s\n",
                s->name,
                again.diagnostic
            );
        }
        free(again.bytes);
    } else {
        uint64_t entry = script_catalog_id("package-symbol", "project:package_compiler:1:Compile:callable");
        bool called = compiler_host_invoke(host, entry, json, length, SELF_BUDGET, &result);
        same = called && result.flow_status == FLOW_OK &&
               result.length == expected_length &&
               !memcmp(result.json, expected, expected_length);
        if (!same) {
            fprintf(
                stderr,
                "package compiler stage two %s: %s\n",
                s->name,
                called ? result.json : result.diagnostic
            );
        }
        free(result.json);
        result = (compiler_host_result){ 0 };
    }
    free(json);
    free(expected);
    return same;
}
static bool stage_two_samples()
{
    for (int i = 0; i < (int)SAMPLES; i++) {
        REQUIRE(stage_two_sample(&samples[i]));
    }
    return true;
}
/* Stage zero prepares stage one, which compiles its own packages, without
 * debugger points and watches, to a closure: stage two. Stage two, loaded by
 * the compiler host, compiles the same packages to the same bytes, every
 * sample as stage one does, and reads the listed floating literals as stage
 * one does. */
TEST_CASE(package_compiler_compiles_itself)
{
    REQUIRE(prepare());
    const source_package_input files[] = { PACKAGE_COMPILER_SOURCE_INPUTS };
    enum
    {
        FILES = sizeof files / sizeof *files
    };
    /* Compile's root package is its first file's: the package compiler's. */
    source_package_input inputs[FILES];
    int count = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < FILES; i++) {
            bool own = !strncmp(files[i].path, "package_compiler/", 17);
            if (own == !pass) {
                inputs[count++] = files[i];
            }
        }
    }
    pin_value args[ARGUMENTS], out;
    REQUIRE(arguments(1, false, inputs, count, args));
    /* The compiler is compiled without debugger points and watches, which
     * no one debugging a game steps through. */
    args[DEBUG_ARGUMENT].b = false;
    REQUIRE(
        compiler_fixture_call(&fixture, 1, PACKAGE, "Compile", args, ARGUMENTS, SELF_BUDGET, &detached, &out)
    );
    const pin_union *value = out.ptr;
    REQUIRE(out.type == PIN_UNION && value);
    if (value->tag != tag(1, "Compiled")) {
        pin_value message, line, column;
        get(&value->values[0], "message", &message);
        get(&value->values[0], "line", &line);
        get(&value->values[0], "column", &column);
        fprintf(
            stderr,
            "package compiler stage one rejected: %s at %d:%d\n",
            pin_text(message).bytes,
            line.i,
            column.i
        );
    }
    REQUIRE(value->tag == tag(1, "Compiled") && value->count == 2);
    size_t length, json_length;
    unsigned char *stage_two = closure_bytes(value, &length);
    char *json = document_json(args, ARGUMENTS, &json_length);
    arena_free(&detached);
    char error[256];
    host = compiler_host_load(stage_two, length, TEST_COMPILED_BACKEND, error, sizeof error);
    if (!host) {
        fprintf(stderr, "package compiler stage two could not load: %s\n", error);
    }
    REQUIRE(host);
    size_t target_length;
    char *target_json = compiler_host_target_json(host, &target_length);
    yyjson_doc *target = yyjson_read(target_json, target_length, 0);
    yyjson_doc *compiled_arguments = yyjson_read(json, json_length, 0);
    REQUIRE(target && compiled_arguments);
    bool target_equal = true;
    for (size_t i = 0; i < 6; i++) {
        target_equal &= yyjson_equals(
            yyjson_arr_get(yyjson_doc_get_root(target), i),
            yyjson_arr_get(yyjson_doc_get_root(compiled_arguments), TARGET_ARGUMENT + i)
        );
    }
    yyjson_doc_free(compiled_arguments);
    yyjson_doc_free(target);
    free(target_json);
    REQUIRE(target_equal);
    compiler_host_bytes again = { 0 };
    bool invoked = host &&
                   compiler_host_invoke_bytes(
                       host,
                       script_catalog_id("package-symbol", "project:package_compiler:1:CompileClosure:callable"),
                       json,
                       json_length,
                       SELF_BUDGET,
                       COMPILER_HOST_MAX_OUTPUT_BYTES,
                       &again
                   );
    bool same = invoked && again.flow_status == FLOW_OK &&
                again.length == length && !memcmp(again.bytes, stage_two, length);
    if (!invoked) {
        fprintf(
            stderr,
            "package compiler stage two: %s%s\n",
            host ? "" : error,
            again.diagnostic
        );
    } else if (!again.length) {
        stage_two_rejection(json, json_length);
    } else if (!same) {
        size_t at = 0;
        while (at < length && at < again.length && again.bytes[at] == stage_two[at]) {
            at++;
        }
        fprintf(
            stderr,
            "package compiler stage two: %zu bytes, stage one %zu; first "
            "difference at %zu\n",
            again.length,
            length,
            at
        );
    }
    bool seeded = same && write_seed(stage_two, length, json, json_length);
    free(again.bytes);
    free(stage_two);
    free(json);
    REQUIRE(same);
    REQUIRE(seeded);
    return stage_two_samples() && stage_two_literals();
}
TEST_CASE(package_compiler_reads_float_literals)
{
    REQUIRE(prepare());
    return literals_agree();
}
/* Behavior assets. Stage one compiles each against a game this test declares,
 * on both compiler backends; its module declares stage zero's identities and
 * plays as stage zero's does. Counter is the Counter story's behavior: a
 * retained state, and a Tick handler that increments it and writes it,
 * converted to a float, to a field of the owner's component. */
typedef struct
{
    const char *name, *source;
} behavior_sample;
static const char COUNTER[] = "// Behavior state that survives between steps, "
                              "written to a component.\n"
                              "Counter :: behavior {\n"
                              "    steps: int = 0\n"
                              "\n"
                              "    on Tick :: () {\n"
                              "        steps += 1\n"
                              "        self.Spot.x = steps\n"
                              "    }\n"
                              "}\n";
static const behavior_sample behavior_samples[] = {
    { "Counter", COUNTER },
    { "Store", "Store :: behavior {\n    steps: int = 4\n    on Tick :: () {\n        steps = 7\n    }\n}\n" },
    { "Glide", "Glide :: behavior {\n    on Tick :: (DeltaSeconds: float) {\n        self.Spot.y = DeltaSeconds\n    }\n}\n" },
    { "Mark", "Mark :: behavior {\n    on tick :: () {\n        self.Spot.x = 3\n    }\n}\n" },
    { "Sink", "Sink :: behavior {\n    on Tick :: () {\n        self.Spot.y = -2.5\n    }\n}\n" },
    { "Copy", "Copy :: behavior {\n    on Tick :: () {\n        self.Spot.x = self.Spot.w\n    }\n}\n" },
    { "Grow", "Grow :: behavior {\n    on Tick :: () {\n        self.Spot.x += 1\n    }\n}\n" },
    { "Swap", "Swap :: behavior {\n    on Tick :: () {\n        self.Spot = self.Spot\n    }\n}\n" },
    { "Turn",
      "Turn :: behavior {\n    angle: float = 0.5\n    on Tick :: "
      "(DeltaSeconds: float) {\n        "
      "angle += DeltaSeconds * 2\n"
      "        self.Spot.x = angle\n    }\n}\n" },
    { "Fold",
      "Fold :: behavior {\n    Wrap :: (value: float, limit: float) -> float "
      "{\n"
      "        if value > limit {\n            return value - limit * 2\n      "
      "  }\n        return value\n    }\n"
      "    Split :: (value: float) -> (whole: int, rest: float) {\n        "
      "whole = int(value)\n"
      "        rest = value - whole\n        return (whole, rest)\n    }\n    "
      "on Tick :: () {\n"
      "        self.Spot.x = Wrap(self.Spot.x + 7, 20)\n        parts := "
      "Split(self.Spot.x)\n"
      "        self.Spot.y = parts.whole * 2 + parts.rest\n    }\n}\n" },
    { "Halve",
      "Halve :: behavior {\n    on Tick :: (DeltaSeconds: float) {\n        "
      "self.Spot.y = "
      "self.Spot.w / 2 - DeltaSeconds\n"
      "        if self.Spot.w > 3 {\n            self.Spot.x *= 2.5\n        "
      "}\n    }\n}\n" },
    { "Pair",
      "Pair :: behavior {\n    a: int = 2\n    b: int = 5\n    on Tick :: () "
      "{\n     "
      "   a *= 2\n"
      "        b = a - 1\n        self.Spot.x = b\n        self.Spot.y = a\n   "
      " }\n}\n" },
    { "Steer",
      "Steer :: behavior {\n    on Move :: (y: float, x: float) {\n        "
      "self.Spot.x = x - y\n        self.Spot.y = y\n    }\n}\n" },
    { "Lift", "Lift :: behavior {\n    on Move :: (y: float) {\n        self.Spot.y += y\n    }\n}\n" },
};
/* Sources stage one rejects, with its message. Where stage zero rejects one
 * too, stage one gives its message at its position. */
typedef struct
{
    const char *source, *message;
    bool stage_zero;
} behavior_rejection;
static const behavior_rejection behavior_rejections[] = {
    { "Late :: behavior {\n    after Tick {\n    }\n}\n",
      "only `on Event` handlers are supported yet",
      false },
    { "Jump :: behavior {\n    on Jump :: () {\n    }\n}\n", "Unknown event 'Jump'", false },
    { "Fixed :: behavior {\n    on Tick :: () {\n        self.Spot.w = 1\n    "
      "}\n}\n",
      "Assignment requires a writable value",
      false },
    { "Named :: behavior {\n    label: string = \"\"\n    on Tick :: () {\n    "
      "}\n}\n",
      "state needs a number type",
      false },
    { "Extra :: behavior {\n    on Tick :: (DeltaSeconds: float, more: float) "
      "{\n    }\n}\n",
      "Tick has no payload field 'more'",
      true },
    { "Drift :: behavior {\n    on Move :: (z: float, x: float) {\n    }\n}\n",
      "Move has no payload field 'z'",
      true },
    { "Twice :: behavior {\n    on Move :: (x: float, X: float) {\n    }\n}\n",
      "Payload field 'x' is listed twice",
      true },
    { "Scale :: behavior {\n    on Tick :: () {\n        self.Spot *= 2\n    "
      "}\n}\n",
      "operator does not apply to these operands",
      false },
    { "Pace :: behavior {\n    on Tick :: () {\n        self.Spot.x = pace\n   "
      " "
      "}\n}\n",
      "reading the global 'pace' is not supported yet",
      false },
};
enum
{
    SPOT = 101,
    SPOT_COMPONENT = 901,
    TICK = 11,
    MOVE = 12,
    OWNER = 30,
    ENTITY = 700,
    BEHAVIOR_ARGUMENTS = 17,
    TICKS = 5
};
typedef struct
{
    float x, y, w;
} spot;
static const type_field spot_fields[] = {
    { .name = "x",
      .type = PIN_F32,
      .type_name = "float",
      .offset = offsetof(spot, x),
      .size = sizeof(float) },
    { .name = "y",
      .type = PIN_F32,
      .type_name = "float",
      .offset = offsetof(spot, y),
      .size = sizeof(float) },
    { .name = "w",
      .type = PIN_F32,
      .type_name = "float",
      .offset = offsetof(spot, w),
      .size = sizeof(float) },
};
static const type_desc spot_type = { .name = "Spot",
                                     .size = sizeof(spot),
                                     .fields = spot_fields,
                                     .field_count = 3 };
static const script_catalog_field spot_members[] = {
    { .id = 811,
      .name = "x",
      .type = { .storage = PIN_F32 },
      .offset = offsetof(spot, x),
      .writable = true },
    { .id = 812,
      .name = "y",
      .type = { .storage = PIN_F32 },
      .offset = offsetof(spot, y),
      .writable = true },
    { .id = 813, .name = "w", .type = { .storage = PIN_F32 }, .offset = offsetof(spot, w) },
};
static const script_catalog_parameter delta_seconds = { .name = "DeltaSeconds",
                                                        .type = { .storage = PIN_F32 } };
static const script_catalog_parameter move_payload[] = {
    { .name = "x", .type = { .storage = PIN_F32 } },
    { .name = "y", .type = { .storage = PIN_F32 } },
};
static const script_catalog_capability game_events[] = {
    { .id = TICK,
      .name = "Tick",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &delta_seconds,
      .parameter_count = 1,
      .completion_outcome = -1 },
    { .id = MOVE,
      .name = "Move",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = move_payload,
      .parameter_count = 2,
      .completion_outcome = -1 },
};
static const script_catalog_global game_globals[] = {
    { .id = OWNER, .name = "self", .type = { .storage = PIN_HANDLE, .nominal = ENTITY } },
    { .id = OWNER + 1, .name = "pace", .type = { .storage = PIN_F32 } },
};
/* The game: the Spot component and the entity type, Tick, Move with two
 * fields, `self` and `pace`, a global stage one cannot read yet. */
static script_catalog game_catalog()
{
    static script_catalog_type types[2];
    types[0] = (script_catalog_type){ .id = SPOT,
                                      .name = spot_type.name,
                                      .type = { .storage = PIN_STRUCT, .nominal = SPOT },
                                      .fields = spot_members,
                                      .field_count = 3,
                                      .size = spot_type.size,
                                      .layout = type_layout_hash(&spot_type),
                                      .component_id = SPOT_COMPONENT,
                                      .component = true };
    types[1] = (script_catalog_type){ .id = ENTITY,
                                      .name = "ecs.entity",
                                      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
                                      .size = sizeof(uint64_t) };
    return (script_catalog){ .types = types,
                             .type_count = 2,
                             .capabilities = game_events,
                             .capability_count = 2,
                             .globals = game_globals,
                             .global_count = 2,
                             .entity_type = ENTITY,
                             .tick_event = TICK };
}
static bool game_global(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id != OWNER) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}
/* Stage zero's module of `source`. */
static execution_module *stage_zero_behavior(const char *source)
{
    script_catalog catalog = game_catalog();
    script_syntax *syntax = script_parse(source);
    script_analysis *analysis = script_analyze(syntax, &catalog);
    const script_lower_bindings bindings = { .global = game_global };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    char error[256] = { 0 };
    execution_module *module = analysis->diagnostic_count || plan->diagnostic_count
                                 ? NULL
                                 : execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!module) {
        fprintf(stderr, "stage zero behavior: %s\n", error);
    }
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return module;
}
static pin_value u64(uint64_t value)
{
    return (pin_value){ .type = PIN_U64, .u64 = value };
}
static pin_value flag(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
/* The index of the type `id` among the catalog's, -1 for none. */
static int type_index(const script_catalog *catalog, uint64_t id)
{
    for (int i = 0; id && i < catalog->type_count; i++) {
        if (catalog->types[i].id == id) {
            return i;
        }
    }
    return -1;
}
/* CompileBehavior's game arguments, as the game target exports them: each
 * event and its payload fields, each type and its fields, the entity type,
 * no operators that combine records, the globals, and no record makes. */
static bool game_arguments(int b, pin_value *out)
{
    script_catalog catalog = game_catalog();
    pin_value *declared = NULL, *payloads = NULL, *types = NULL, *fields = NULL;
    for (int i = 0; i < catalog.capability_count; i++) {
        const script_catalog_capability *c = &catalog.capabilities[i];
        pin_value event = row(b, "GameEvent");
        REQUIRE(put_value(&event, "name", text(c->name)));
        REQUIRE(put_value(&event, "id", u64(c->id)));
        REQUIRE(put_value(&event, "abi", u64(script_catalog_capability_abi(&catalog, c))));
        REQUIRE(put(&event, "first", acount(payloads)));
        REQUIRE(put(&event, "count", c->parameter_count));
        apush(declared, event);
        for (int j = 0; j < c->parameter_count; j++) {
            pin_value payload = row(b, "GameEventField");
            REQUIRE(put_value(&payload, "name", text(c->parameters[j].name)));
            REQUIRE(put(&payload, "storage", (int)c->parameters[j].type.storage));
            REQUIRE(put_value(&payload, "nominal", u64(c->parameters[j].type.nominal)));
            REQUIRE(put_value(&payload, "has_default", flag(c->parameters[j].has_default)));
            REQUIRE(put_value(&payload, "default_owner", flag(c->parameters[j].default_owner)));
            apush(payloads, payload);
        }
    }
    for (int i = 0; i < catalog.type_count; i++) {
        const script_catalog_type *t = &catalog.types[i];
        pin_value type = row(b, "GameType");
        REQUIRE(put_value(&type, "required", flag(true)));
        REQUIRE(put(&type, "underlying", type_index(&catalog, t->underlying)));
        REQUIRE(put_value(&type, "name", text(t->name)));
        REQUIRE(put_value(&type, "id", u64(t->id)));
        REQUIRE(put_value(&type, "layout", u64(t->layout)));
        REQUIRE(put(&type, "storage", (int)t->type.storage));
        REQUIRE(put(&type, "size", (int)t->size));
        REQUIRE(put_value(&type, "component", u64(t->component ? t->component_id : 0)));
        REQUIRE(put(&type, "first", acount(fields)));
        REQUIRE(put(&type, "count", t->field_count));
        apush(types, type);
        for (int j = 0; j < t->field_count; j++) {
            pin_value field = row(b, "GameField");
            REQUIRE(put_value(&field, "name", text(t->fields[j].name)));
            REQUIRE(put(&field, "storage", (int)t->fields[j].type.storage));
            REQUIRE(put(&field, "offset", (int)t->fields[j].offset));
            REQUIRE(put_value(&field, "writable", flag(t->fields[j].writable)));
            REQUIRE(put_value(&field, "c_string", flag(t->fields[j].c_string)));
            REQUIRE(put(&field, "record", type_index(&catalog, t->fields[j].type.nominal)));
            REQUIRE(put_value(&field, "constant", flag(t->fields[j].constant)));
            REQUIRE(put(&field, "read", -1));
            REQUIRE(put_value(&field, "read_key", text("")));
            apush(fields, field);
        }
    }
    out[0] = rows(declared, acount(declared), PIN_STRUCT, declared[0].record);
    out[1] = rows(payloads, acount(payloads), PIN_STRUCT, payloads[0].record);
    out[2] = rows(types, acount(types), PIN_STRUCT, types[0].record);
    out[3] = rows(fields, acount(fields), PIN_STRUCT, fields[0].record);
    out[4] = u64(catalog.entity_type);
    out[5] = rows(
        NULL,
        0,
        PIN_STRUCT,
        compiler_fixture_record(&fixture, b, PACKAGE, "NumericOperator", &input)
    );
    pin_value *globals = NULL;
    for (int i = 0; i < catalog.global_count; i++) {
        const script_catalog_global *g = &catalog.globals[i];
        pin_value global = row(b, "GameGlobal");
        REQUIRE(put_value(&global, "name", text(g->name)));
        REQUIRE(put(&global, "storage", (int)g->type.storage));
        REQUIRE(put(&global, "record", type_index(&catalog, g->type.nominal)));
        apush(globals, global);
    }
    out[6] = rows(globals, acount(globals), PIN_STRUCT, globals[0].record);
    out[7] = rows(NULL, 0, PIN_STRUCT, compiler_fixture_record(&fixture, b, PACKAGE, "NativeMake", &input));
    out[8] = rows(NULL, 0, PIN_I32, NULL);
    afree(declared);
    afree(payloads);
    afree(types);
    afree(fields);
    afree(globals);
    return true;
}
/* Stage one's Result for `source` on backend `b`, in `detached`. */
static bool compile_behavior(int b, const char *identity, const char *source, pin_value *out)
{
    pin_value compile[ARGUMENTS], args[BEHAVIOR_ARGUMENTS];
    REQUIRE(arguments(b, false, NULL, 0, compile));
    args[0] = text(identity);
    args[1] = text(source);
    memcpy(&args[2], &compile[TARGET_ARGUMENT], 6 * sizeof *args);
    REQUIRE(game_arguments(b, &args[8]));
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, "CompileBehavior", args, BEHAVIOR_ARGUMENTS, COMPILE_BUDGET, &detached, out)
    );
    REQUIRE(out->type == PIN_UNION && out->ptr);
    return true;
}
/* The game's types and events, as the player links them. */
static bool link_game_type(void *user, uint64_t id, execution_prepared_type *out)
{
    const script_catalog *catalog = user;
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *t = &catalog->types[i];
        if (t->id == id) {
            *out = (execution_prepared_type){ .name = t->name,
                                              .type = t->id == SPOT ? &spot_type : NULL,
                                              .storage = t->type.storage,
                                              .size = t->size,
                                              .layout = t->layout };
            return true;
        }
    }
    return false;
}
static bool link_game_event(void *user, uint64_t id, uint64_t abi)
{
    const script_catalog *catalog = user;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind == SCRIPT_CAPABILITY_EVENT && c->id == id) {
            return script_catalog_capability_abi(catalog, c) == abi;
        }
    }
    return false;
}
/* The module stage one compiles `source` to on backend `b`, linked against
 * the game. */
static bool stage_one_behavior(int b, const char *identity, const char *source, execution_module **module)
{
    *module = NULL;
    pin_value out;
    REQUIRE(compile_behavior(b, identity, source, &out));
    const pin_union *value = out.ptr;
    if (value->tag != tag(b, "Compiled")) {
        pin_value message, line, column;
        get(&value->values[0], "message", &message);
        get(&value->values[0], "line", &line);
        get(&value->values[0], "column", &column);
        fprintf(
            stderr,
            "stage one behavior %s rejected: %s at %d:%d\n",
            identity,
            pin_text(message).bytes,
            line.i,
            column.i
        );
        arena_free(&detached);
        return false;
    }
    size_t length;
    unsigned char *bytes = closure_bytes(value, &length);
    arena_free(&detached);
    script_catalog catalog = game_catalog();
    execution_prepared_linker linker = { .user = &catalog,
                                         .type = link_game_type,
                                         .event = link_game_event };
    char error[256] = { 0 };
    *module = execution_prepared_read(bytes, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!*module) {
        fprintf(stderr, "stage one behavior %s load: %s\n", identity, error);
    }
    free(bytes);
    return *module != NULL;
}
/* A behavior module playing in its own world, on one owner holding a Spot. */
typedef struct
{
    ecs_world *world;
    execution_program *program;
    execution_instance *instance;
    ecs_entity owner;
} player;
static bool play(execution_module *module, player *out)
{
    out->world = ecs_world_new();
    const ecs_component_desc component = { .id = SPOT_COMPONENT,
                                           .key = "test.spot",
                                           .name = spot_type.name,
                                           .element_size = sizeof(spot),
                                           .type = &spot_type };
    REQUIRE(ecs_world_register_component(out->world, &component, NULL) == ECS_OK);
    out->program = execution_program_new(module, out->world, NULL);
    REQUIRE(out->program);
    out->owner = ecs_world_create_entity(out->world);
    spot initial = { .w = 9 };
    REQUIRE(ecs_world_record_add(out->world, out->owner, SPOT_COMPONENT, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(out->world) == ECS_OK);
    out->instance = execution_program_attach(out->program, ecs_entity_to_u64(out->owner));
    REQUIRE(out->instance);
    return true;
}
static void stop(player *p)
{
    execution_program_free(p->program);
    ecs_world_free(p->world);
    *p = (player){ 0 };
}
/* Stage one's module declares what stage zero's does: the module, its owner,
 * types, services, states, events and handlers, with the same identities,
 * ABIs, kinds and effects. */
static bool same_declarations(const execution_module *expected, const execution_module *actual)
{
    const execution_module_plan *e = &expected->plan, *a = &actual->plan;
    REQUIRE(a->id == e->id && a->owner_type == e->owner_type);
    REQUIRE(a->type_count == e->type_count);
    for (int i = 0; i < e->type_count; i++) {
        REQUIRE(
            a->types[i].id == e->types[i].id &&
            a->types[i].layout == e->types[i].layout
        );
        REQUIRE(
            a->types[i].storage == e->types[i].storage &&
            a->types[i].size == e->types[i].size
        );
        REQUIRE(
            !strcmp(a->types[i].name, e->types[i].name) &&
            !a->types[i].record == !e->types[i].record
        );
    }
    REQUIRE(a->service_count == e->service_count);
    for (int i = 0; i < e->service_count; i++) {
        REQUIRE(!strcmp(a->services[i], e->services[i]));
    }
    REQUIRE(a->state_count == e->state_count);
    for (int i = 0; i < e->state_count; i++) {
        REQUIRE(a->states[i].id == e->states[i].id);
        REQUIRE(a->states[i].initial.type == e->states[i].initial.type);
        REQUIRE(a->states[i].initial.i == e->states[i].initial.i);
    }
    REQUIRE(a->event_count == e->event_count);
    for (int i = 0; i < e->event_count; i++) {
        const execution_module_event_declaration *x = &e->events[i],
                                                 *y = &a->events[i];
        REQUIRE(y->id == x->id && y->abi == x->abi && !strcmp(y->name, x->name));
        REQUIRE(y->field_count == x->field_count);
        for (int j = 0; j < x->field_count; j++) {
            REQUIRE(
                !strcmp(y->fields[j].name, x->fields[j].name) &&
                y->fields[j].nominal == x->fields[j].nominal
            );
            REQUIRE(y->fields[j].has_default == x->fields[j].has_default);
            REQUIRE(y->fields[j].default_owner == x->fields[j].default_owner);
            REQUIRE(y->fields[j].initial.type == x->fields[j].initial.type);
        }
    }
    REQUIRE(a->function_count == e->function_count);
    for (int i = 0; i < e->function_count; i++) {
        const execution_module_function *x = &e->functions[i], *y = &a->functions[i];
        REQUIRE(y->id == x->id && y->event == x->event && y->event_abi == x->event_abi);
        REQUIRE(y->kind == x->kind && y->effects == x->effects);
        REQUIRE(!y->event_name == !x->event_name && !y->entry == !x->entry);
        REQUIRE(!x->event_name || !strcmp(y->event_name, x->event_name));
        REQUIRE(y->body->argument_count == x->body->argument_count);
        REQUIRE(y->body->result_count == x->body->result_count);
        REQUIRE(y->body->variable_count == x->body->variable_count);
        REQUIRE(y->body->component_access == x->body->component_access);
        REQUIRE(acount(y->body->components) == acount(x->body->components));
        for (int j = 0; j < acount(x->body->components); j++) {
            REQUIRE(y->body->components[j].id == x->body->components[j].id);
        }
    }
    return true;
}
/* A dispatch that ran, or found no handler in a module that handles only
 * other events. */
static bool handled(int status)
{
    return status == FLOW_OK || status == FLOW_ERR_UNKNOWN_EVENT;
}
/* Each Tick and Move, both modules' owners hold the same Spot and states. */
static bool plays_alike(execution_module *expected, execution_module *actual)
{
    player e = { 0 }, a = { 0 };
    bool alike = play(expected, &e) && play(actual, &a);
    pin_value delta = { .type = PIN_F32, .f = 1.0f / 60.0f };
    const pin_value move[] = { { .type = PIN_F32, .f = 1.5f },
                               { .type = PIN_F32, .f = -4.0f } };
    for (int tick = 0; alike && tick < TICKS; tick++) {
        int left = execution_instance_dispatch(e.instance, TICK, &delta, 1, 0, 0, 1000);
        int right = execution_instance_dispatch(a.instance, TICK, &delta, 1, 0, 0, 1000);
        int moved = execution_instance_dispatch(e.instance, MOVE, move, 2, 0, 0, 1000);
        int moving = execution_instance_dispatch(a.instance, MOVE, move, 2, 0, 0, 1000);
        const spot *x = ecs_component_data_const(e.world, e.owner, SPOT_COMPONENT);
        const spot *y = ecs_component_data_const(a.world, a.owner, SPOT_COMPONENT);
        alike = left == right && moved == moving && handled(left) &&
                handled(moved) && x && y && !memcmp(x, y, sizeof *x);
        for (int i = 0; alike && i < expected->plan.state_count; i++) {
            const pin_value *kept = execution_instance_state(
                e.instance,
                expected->plan.states[i].id
            );
            const pin_value *held = execution_instance_state(
                a.instance,
                expected->plan.states[i].id
            );
            alike = kept && held && kept->type == held->type && kept->i == held->i;
        }
        if (!alike) {
            fprintf(
                stderr,
                "behavior tick %d: flow %d/%d x %g/%g y %g/%g\n",
                tick,
                left,
                right,
                x ? x->x : -1.0,
                y ? y->x : -1.0,
                x ? x->y : -1.0,
                y ? y->y : -1.0
            );
        }
    }
    stop(&e);
    stop(&a);
    return alike;
}
/* Stage one's module of `sample` against stage zero's, on backend `b`. */
static bool behaves_alike(int b, const behavior_sample *sample)
{
    execution_module *expected = stage_zero_behavior(sample->source);
    execution_module *actual = NULL;
    bool same = expected &&
                stage_one_behavior(b, sample->name, sample->source, &actual) &&
                same_declarations(expected, actual) &&
                same_points(expected, actual) &&
                same_watches(expected, actual) && plays_alike(expected, actual);
    if (!same) {
        fprintf(stderr, "behavior %s differs on backend %d\n", sample->name, b);
    }
    execution_module_free(actual);
    execution_module_free(expected);
    return same;
}
/* Stage zero's first diagnostic of `source`. */
static script_diagnostic stage_zero_diagnostic(const char *source)
{
    script_catalog catalog = game_catalog();
    script_syntax *syntax = script_parse(source);
    script_analysis *analysis = script_analyze(syntax, &catalog);
    script_diagnostic first = analysis->diagnostic_count
                                ? analysis->diagnostics[0]
                                : (script_diagnostic){ 0 };
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return first;
}
/* Stage one rejects the source with exactly the message, and where stage zero
 * rejects it too, with stage zero's message at its position. */
static bool rejects(int b, const behavior_rejection *rejection)
{
    pin_value out, message, line, column;
    REQUIRE(compile_behavior(b, "Rejected", rejection->source, &out));
    const pin_union *value = out.ptr;
    bool rejected = value->tag == tag(b, "Rejected") &&
                    get(&value->values[0], "message", &message) &&
                    get(&value->values[0], "line", &line) &&
                    get(&value->values[0], "column", &column) &&
                    !strcmp(pin_text(message).bytes, rejection->message);
    if (rejected && rejection->stage_zero) {
        script_diagnostic expected = stage_zero_diagnostic(rejection->source);
        rejected = !strcmp(expected.message, rejection->message) &&
                   expected.range.line == line.i &&
                   expected.range.column == column.i;
    }
    if (!rejected) {
        fprintf(stderr, "behavior rejection: wanted '%s'\n", rejection->message);
    }
    arena_free(&detached);
    return rejected;
}
/* A project names a behavior by its path, and the IDs follow it. */
static bool takes_project_identities(int b)
{
    const char *identity = "behaviors/Counter.chibi";
    execution_module *module = NULL;
    REQUIRE(stage_one_behavior(b, identity, COUNTER, &module));
    char handler[128];
    snprintf(handler, sizeof handler, "%s:%d:1", identity, (int)(strstr(COUNTER, "on Tick") - COUNTER));
    const execution_module_plan *p = &module->plan;
    bool named = p->id == script_catalog_id("module", identity) &&
                 p->state_count == 1 &&
                 p->states[0].id ==
                     script_catalog_id("symbol", "behaviors/Counter.chibi:1:steps") &&
                 p->function_count == 1 &&
                 p->functions[0].id == script_catalog_id("function", handler);
    execution_module_free(module);
    REQUIRE(named);
    return true;
}
/* Stage zero's build of stage one passes arguments without copying them to
 * every function but those that run once per compile. One that pushes onto an
 * array argument, or stores a uint64 into a field of a local record, copies
 * every argument on each call, and so does every function that calls it;
 * each took stage one compiling itself past its 60-second ceiling. */
TEST_CASE(package_compiler_shares_its_arguments)
{
    REQUIRE(prepare());
    const char *const once[] = { "Compile",           "CompileClosure",
                                 "CompileBehavior",   "CompileBehaviors",
                                 "BehaviorRejection", "Declare",
                                 "DeclarePackage" };
    const execution_module_plan *p = &compiler_fixture_package(&fixture, 1, PACKAGE)
                                          ->module->plan;
    int copying = 0;
    for (int i = 0; i < p->function_count; i++) {
        const behavior_function *body = p->functions[i].body;
        bool allowed = false;
        for (size_t j = 0; j < sizeof once / sizeof *once; j++) {
            char name[128];
            snprintf(name, sizeof name, "%s:1:%s:callable", PACKAGE, once[j]);
            allowed |= p->functions[i].id == script_catalog_id("package-symbol", name);
        }
        if (body && body->argument_sharing != BEHAVIOR_SHARING_SAFE && !allowed) {
            fprintf(
                stderr,
                "package compiler function %016llx copies its arguments\n",
                (unsigned long long)p->functions[i].id
            );
            copying++;
        }
    }
    REQUIRE(copying == 0);
    return true;
}
/* The behavior samples from `first` before `last` compile alike on both
 * backends. Stage one compiles slowest on the bytecode backend, so each
 * selector takes half of them. */
static bool samples_alike(size_t first, size_t last)
{
    const type_desc *const reflected[] = { &spot_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    type_catalog_set_project(&pack);
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        for (size_t i = first; passed && i < last; i++) {
            passed = behaves_alike(b, &behavior_samples[i]);
        }
    }
    type_catalog_set_project(NULL);
    return passed;
}
enum
{
    SAMPLE_COUNT = sizeof behavior_samples / sizeof *behavior_samples
};
TEST_CASE(package_compiler_compiles_behaviors)
{
    REQUIRE(prepare());
    REQUIRE(samples_alike(0, SAMPLE_COUNT / 2));
    return true;
}
/* The other half of the samples, the rejections and the identities a
 * project's path gives. */
TEST_CASE(package_compiler_compiles_the_other_behaviors)
{
    REQUIRE(prepare());
    const type_desc *const reflected[] = { &spot_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    bool passed = samples_alike(SAMPLE_COUNT / 2, SAMPLE_COUNT);
    type_catalog_set_project(&pack);
    for (size_t i = 0;
         passed && i < sizeof behavior_rejections / sizeof *behavior_rejections;
         i++) {
        passed = rejects(1, &behavior_rejections[i]);
    }
    passed = passed && takes_project_identities(1);
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
void test_language_package_compiler_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_packages);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_types()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_typed_packages);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_imports()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_importing_packages);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_compiled()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_packages_compiled);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_types_compiled()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_typed_packages_compiled);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_collections()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_collection_packages);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_collections_compiled()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_collection_packages_compiled);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_imports_compiled()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_importing_packages_compiled);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_bootstrap()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_compiles_itself);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_literals()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_reads_float_literals);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_behaviors()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_compiles_behaviors);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_behaviors_rest()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_compiles_the_other_behaviors);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_sharing()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_shares_its_arguments);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_debug_free()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_omits_debug_info);
    pu_clear_setup();
}
