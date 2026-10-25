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
#include "float_conversion_cases.h"
#include "language/lower.h"
#include "package_compiler_chibi_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/native_capabilities.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/file_io.h"
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
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) && defined(__x86_64__)
#include "host/process.h"

#include <dlfcn.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
extern char **environ;
#endif

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
/* Focused selectors group unions, constants, patterns and integer types
 * (TYPE_SAMPLES); imports, core-pack calls, moves, reads in place and array
 * methods (IMPORT_SAMPLES, run in Release); and maps, references, arrays and
 * ranges (COLLECTION_SAMPLES). */
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
 * Copied function, which assigns its array, does not. A `debug_metadata`
 * sample compares source points and watches with stage zero on both compiler
 * backends. */
typedef struct
{
    const char *name, *source, *more, *library;
    sample_group group;
    bool core, updates, boxed, game, moves, aliases, variants, built,
        same_rejection, c_string_probe, host_effects, update_order, debug_metadata;
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
static const char *integer_probe(arena *, const pin_value *arguments, int, pin_value *result)
{
    result->i = arguments[0].i;
    return NULL;
}
static const native_parameter_definition INTEGER_INPUT[] = {
    { .name = "value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
};
static const native_parameter_definition CSTRING_INPUT[] = {
    { .name = "value",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .c_string = true },
};
/* The second parameter deliberately carries the C-string boundary. */
static const native_parameter_definition CSTRING_PAIR_INPUT[] = {
    { .name = "prefix", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
    { .name = "value",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .c_string = true },
};
static const char *cstring_pair(arena *storage, const pin_value *arguments, int count, pin_value *result)
{
    return cstring_probe(storage, arguments + 1, count - 1, result);
}
static const native_parameter_definition CSTRING_OUTPUT[] = {
    { .name = "length", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
};
#define PROBE_DEFINITION(CANONICAL, SOURCE, INPUT, PURE)                       \
    { .canonical_name = CANONICAL,                                             \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = INPUT,                                                         \
      .input_count = 1,                                                        \
      .outputs = CSTRING_OUTPUT,                                               \
      .output_count = 1,                                                       \
      .completion_output = -1,                                                 \
      .pure = PURE }
static const native_capability_definition CSTRING_DEFINITIONS[] = {
    PROBE_DEFINITION("CString Probe", "cstring_probe", CSTRING_INPUT, true),
    { .canonical_name = "CString Pair",
      .source_name = "cstring_pair",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = CSTRING_PAIR_INPUT,
      .input_count = 2,
      .outputs = CSTRING_OUTPUT,
      .output_count = 1,
      .completion_output = -1,
      .pure = true },
    PROBE_DEFINITION("Qualified Probe", "native.probe", CSTRING_INPUT, false),
    PROBE_DEFINITION("Qualified Length", "native.length", CSTRING_INPUT, true),
    PROBE_DEFINITION("Qualified Text Overload", "native.overload", CSTRING_INPUT, true),
    PROBE_DEFINITION("Qualified Integer Overload", "native.overload", INTEGER_INPUT, true),
    PROBE_DEFINITION("Qualified Ambiguous One", "native.ProbeOne", CSTRING_INPUT, true),
    PROBE_DEFINITION("Qualified Ambiguous Two", "native.probe_one", CSTRING_INPUT, true),
};
#undef PROBE_DEFINITION
static const node_host_call CSTRING_CALL = { .value = cstring_probe };
static const node_host_call CSTRING_PAIR_CALL = { .value = cstring_pair };
static const node_host_call INTEGER_CALL = { .value = integer_probe };
static const node_access CSTRING_ACCESS = { .flags = NODE_EFFECT_PURE };
static const node_access HOST_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define PROBE_BINDING(CANONICAL, CALL, ACCESS)                                 \
    {                                                                          \
        .canonical_name = CANONICAL, .implementation = {                       \
            .call = &CALL,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding CSTRING_BINDINGS[] = {
    PROBE_BINDING("CString Probe", CSTRING_CALL, CSTRING_ACCESS),
    PROBE_BINDING("CString Pair", CSTRING_PAIR_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Probe", CSTRING_CALL, HOST_ACCESS),
    PROBE_BINDING("Qualified Length", CSTRING_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Text Overload", CSTRING_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Integer Overload", INTEGER_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Ambiguous One", CSTRING_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Ambiguous Two", CSTRING_CALL, CSTRING_ACCESS),
};
#undef PROBE_BINDING
static const native_definition_pack CSTRING_PACK = {
    .definitions = CSTRING_DEFINITIONS,
    .definition_count = sizeof CSTRING_DEFINITIONS / sizeof *CSTRING_DEFINITIONS,
    .bindings = CSTRING_BINDINGS,
    .binding_count = sizeof CSTRING_BINDINGS / sizeof *CSTRING_BINDINGS,
};
static const sample samples[] = {
    { .name = "array update debug order",
      .group = COLLECTION_SAMPLES,
      .update_order = true,
      .source = "package facts\n"
                "Item :: () -> int { return 7; }\n"
                "Items :: () -> int[] { return [7, 8]; }\n"
                "AppendLocal :: () -> int[] { xs: int[] = [1]; xs = append(xs, "
                "Item()); return xs; }\n"
                "AppendParameter :: (xs: int[]) -> int[] { xs = append(xs, "
                "Item()); return xs; }\n"
                "AppendMethod :: () -> int[] { xs: int[] = [1]; "
                "xs.append(Item()); return xs; }\n"
                "AppendParameterMethod :: (xs: int[]) -> int[] { "
                "xs.append(Item()); return xs; }\n"
                "ExtendLocal :: () -> int[] { xs: int[] = [1]; xs = extend(xs, "
                "Items()); return xs; }\n"
                "ExtendParameter :: (xs: int[]) -> int[] { xs = extend(xs, "
                "Items()); return xs; }\n"
                "ExtendMethod :: () -> int[] { xs: int[] = [1]; "
                "xs.extend(Items()); return xs; }\n"
                "ExtendParameterMethod :: (xs: int[]) -> int[] { "
                "xs.extend(Items()); return xs; }",
      .calls = { { .function = "AppendLocal", .arguments = "[]", .expected = "[[1,7]]" },
                 { .function = "AppendParameter", .arguments = "[[1]]", .expected = "[[1,7]]" },
                 { .function = "AppendMethod", .arguments = "[]", .expected = "[[1,7]]" },
                 { .function = "AppendParameterMethod", .arguments = "[[1]]", .expected = "[[1,7]]" },
                 { .function = "ExtendLocal", .arguments = "[]", .expected = "[[1,7,8]]" },
                 { .function = "ExtendParameter", .arguments = "[[1]]", .expected = "[[1,7,8]]" },
                 { .function = "ExtendMethod", .arguments = "[]", .expected = "[[1,7,8]]" },
                 { .function = "ExtendParameterMethod",
                   .arguments = "[[1]]",
                   .expected = "[[1,7,8]]" } } },
    { .name = "typed constants fold exactly",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Sign: int : -1 << 31;\n"
                "Small: int8 : (64 << 1) - 1;\n"
                "Unsigned: uint : 4294967295 >> 1;\n"
                "Floor: int : -3 >> 1;\n"
                "Wide: int64 : -1 << 63;\n"
                "Mask: uint64 : ~0;\n"
                "Low: byte : ~byte(15);\n"
                "Big: int : (1 << 100) >> 98;\n"
                "Half: int8 : (127 + 1) / 2;\n"
                "Wrapped: byte : byte(wrap: 300);\n"
                "ReadSign :: () -> int { return Sign; }\n"
                "ReadSmall :: () -> int8 { return Small; }\n"
                "ReadUnsigned :: () -> uint { return Unsigned; }\n"
                "ReadFloor :: () -> int { return Floor; }\n"
                "ReadWide :: () -> int64 { return Wide; }\n"
                "ReadMask :: () -> uint64 { return Mask; }\n"
                "ReadLow :: () -> byte { return Low; }\n"
                "ReadBig :: () -> int { return Big; }\n"
                "ReadHalf :: () -> int8 { return Half; }\n"
                "ReadWrapped :: () -> byte { return Wrapped; }",
      .calls = { { .function = "ReadSign", .arguments = "[]", .expected = "[-2147483648]" },
                 { .function = "ReadSmall", .arguments = "[]", .expected = "[127]" },
                 { .function = "ReadUnsigned", .arguments = "[]", .expected = "[2147483647]" },
                 { .function = "ReadFloor", .arguments = "[]", .expected = "[-2]" },
                 { .function = "ReadWide", .arguments = "[]", .expected = "[-9223372036854775808]" },
                 { .function = "ReadMask", .arguments = "[]", .expected = "[18446744073709551615]" },
                 { .function = "ReadLow", .arguments = "[]", .expected = "[240]" },
                 { .function = "ReadBig", .arguments = "[]", .expected = "[4]" },
                 { .function = "ReadHalf", .arguments = "[]", .expected = "[64]" },
                 { .function = "ReadWrapped", .arguments = "[]", .expected = "[44]" } } },
    { .name = "constant outside its type",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: int : 1 << 31;",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 12 },
    { .name = "constant negative shift count",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: int : 1 << -1;",
      .message = "Shift count must not be negative",
      .line = 2,
      .column = 17 },
    { .name = "constant shift count too large",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: int : 1 >> 4097;",
      .message = "Constant shift count is too large",
      .line = 2,
      .column = 17 },
    { .name = "parenthesized negative shift count",
      .group = TYPE_SAMPLES,
      .source = "package facts\nShift :: (a: int) -> int { return a << (2 - "
                "3); }\n",
      .message = "Shift count must not be negative",
      .line = 2,
      .column = 41 },
    { .name = "constant signed shift count",
      .group = TYPE_SAMPLES,
      .source = "package facts\nCount: int : 3;\nBad: int : 1 << Count;",
      .message = "Shift count must be unsigned; convert it with uint(value)",
      .line = 3,
      .column = 17 },
    { .name = "comparisons of untyped literals",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Guard :: () -> int {\n"
                "    if (7 < 3) { return 1; }\n"
                "    return 2;\n"
                "}\n"
                "Wide :: () -> bool { return 3000000000 > 2; }\n"
                "Mixed :: () -> bool { return 2.5 > 2; }\n"
                "Negative :: () -> bool { return -1 < 0; }\n"
                "Same :: () -> bool { return 7 == 7; }",
      .calls = { { .function = "Guard", .arguments = "[]", .expected = "[2]" },
                 { .function = "Wide", .arguments = "[]", .expected = "[true]" },
                 { .function = "Mixed", .arguments = "[]", .expected = "[true]" },
                 { .function = "Negative", .arguments = "[]", .expected = "[true]" },
                 { .function = "Same", .arguments = "[]", .expected = "[true]" } } },
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
    { .name = "single named result",
      .source = "package facts\n"
                "One :: (n: int) -> (value: int) { return n + 1; }\n"
                "Flag :: () -> (value: bool = false) { value = true; return; "
                "}\n"
                "Sequential :: () -> (a: int, b: int) { a = 1; b = 2; return "
                "(b, a); }\n"
                "Use :: (n: int) -> int {\n"
                "    if Flag() { return One(n) * 10 + One(n).value; }\n"
                "    return 0;\n}\n",
      .calls = { { .function = "One", .arguments = "[3]", .expected = "[4]" },
                 { .function = "Sequential", .arguments = "[]", .expected = "[2,2]" },
                 { .function = "Use", .arguments = "[3]", .expected = "[44]" } } },
    { .name = "widened named projection",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Tiny :: () -> (value: int8) { return 7; }\n"
                "Narrow :: (n: int8) -> int { return int(n); }\n"
                "Use :: () -> int { n: int = Tiny(); return Narrow(n.value); "
                "}\n"
                "Alias :: () -> int { n: int = Tiny(); q := n; "
                "return Narrow(q.value); }\n"
                "Assigned :: (value: int) -> int { n: int = Tiny(); "
                "n = value; return Narrow(n.value); }\n"
                "Converted :: (value: int) -> int { n: int = Tiny(); "
                "n = value; return int(n.value); }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[7]" },
                 { .function = "Alias", .arguments = "[]", .expected = "[7]" },
                 { .function = "Assigned", .arguments = "[127]", .expected = "[127]" },
                 { .function = "Assigned", .arguments = "[-128]", .expected = "[-128]" },
                 { .function = "Assigned", .arguments = "[300]", .expected = "[127]" },
                 { .function = "Assigned", .arguments = "[-129]", .expected = "[-128]" },
                 { .function = "Converted", .arguments = "[300]", .expected = "[127]" },
                 { .function = "Converted", .arguments = "[-129]", .expected = "[-128]" } } },
    { .name = "named struct results",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Collision :: struct { point: int; }\n"
                "MakeCollision :: () -> (point: Collision) { return {point: "
                "7}; }\n"
                "ReadCollision :: () -> int { return "
                "MakeCollision().point.point; }\n"
                "Direct :: (n: int) -> (point: Point) { return {x: n, y: 2}; "
                "}\n"
                "Empty :: () -> (point: Point) { return; }\n"
                "Mutate :: (n: int) -> (point: Point) { point.x = n; point.y = "
                "2; return; }\n"
                "Project :: () -> int { p := Direct(7); q := p; return "
                "q.point.x; }\n"
                "Other :: () -> (other: Point) { return {x: 9, y: 3}; }\n"
                "Reassigned :: () -> int { p := Direct(7); p = Other(); "
                "q := p; return q.point.x; }\n"
                "LaterShadow :: () -> int { p := Direct(7); q := p; "
                "if true { p: Point = {x: 99, y: 1}; return q.point.x + "
                "p.x; } return 0; }\n"
                "Bump :: (p: ref Point) { p.x += 1; }\n"
                "ByRef :: () -> int { p := Direct(7); Bump(ref p); return "
                "p.point.x; }\n"
                "Counter :: () -> (value: int) { return 7; }\n"
                "Increment :: (n: ref int) { n += 1; }\n"
                "Boxed :: () -> int { n := Counter(); Increment(ref n); "
                "return n.value; }\n"
                "Use :: (n: int) -> int {\n"
                "    p := Direct(n);\n"
                "    q := p;\n"
                "    q.x += 5;\n"
                "    return p.x * 100 + q.x * 10 + q.y + Empty().x;\n}\n",
      .calls = { { .function = "Direct", .arguments = "[7]", .expected = "[{\"x\":7,\"y\":2}]" },
                 { .function = "Empty", .arguments = "[]", .expected = "[{\"x\":0,\"y\":0}]" },
                 { .function = "Mutate", .arguments = "[7]", .expected = "[{\"x\":7,\"y\":2}]" },
                 { .function = "Project", .arguments = "[]", .expected = "[7]" },
                 { .function = "Reassigned", .arguments = "[]", .expected = "[9]" },
                 { .function = "LaterShadow", .arguments = "[]", .expected = "[106]" },
                 { .function = "ByRef", .arguments = "[]", .expected = "[8]" },
                 { .function = "Boxed", .arguments = "[]", .expected = "[8]" },
                 { .function = "ReadCollision", .arguments = "[]", .expected = "[7]" },
                 { .function = "Use", .arguments = "[7]", .expected = "[822]" } } },
    { .name = "named result ownership",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; }\n"
                "Keep :: (value: string) -> (note: Note) {\n"
                "    note.text = value; note.count = 2; return;\n}\n"
                "Split :: (value: string) -> (note: Note, count: int) {\n"
                "    note = Keep(value); return (note, note.count);\n}\n"
                "Use :: () -> string {\n"
                "    first := Keep(\"a\\u0000b\");\n"
                "    second := Keep(\"changed\");\n"
                "    second.text = \"other\";\n"
                "    return first.text;\n}\n"
                "Projected :: () -> string {\n"
                "    pair := Split(\"a\\u0000b\");\n"
                "    return pair.note.text;\n}\n",
      .calls = { { .function = "Keep",
                   .arguments = "[\"a\\u0000b\"]",
                   .expected = "[{\"text\":\"a\\u0000b\",\"count\":2}]" },
                 { .function = "Split",
                   .arguments = "[\"kept\"]",
                   .expected = "[{\"text\":\"kept\",\"count\":2},2]" },
                 { .function = "Use", .arguments = "[]", .expected = "[\"a\\u0000b\"]" },
                 { .function = "Projected", .arguments = "[]", .expected = "[\"a\\u0000b\"]" } } },
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
                "Mixed :: (a: int8, b: byte) -> int { return int(a) + int(b); "
                "}\n"
                "Big :: (a: uint, b: int) -> int64 { return int64(a) + "
                "int64(b); }\n"
                "Store :: () -> uint16 { v: uint16 = 65535; return v; }\n"
                "Make :: (a: int8) -> int { p: P = {a: a, b: 7}; return "
                "int(p.a) + int(p.b); }\n",
      .calls = { { .function = "Narrow", .arguments = "[-5,300,9,-7]", .expected = "[605]" },
                 { .function = "Mixed", .arguments = "[-3,200]", .expected = "[197]" },
                 { .function = "Big", .arguments = "[4000000000,-1]", .expected = "[3999999999]" },
                 { .function = "Store", .arguments = "[]", .expected = "[65535]" },
                 { .function = "Make", .arguments = "[-2]", .expected = "[5]" } } },
    { .name = "clamp label",
      .group = TYPE_SAMPLES,
      .source = "package facts\nClamp :: (v: int) -> byte { return byte(clamp: "
                "v); }\n",
      .message = "A conversion argument may only be named wrap",
      .line = 2,
      .column = 41 },
    { .name = "mixed integer types",
      .source = "package facts\nMixed :: (a: int8, b: byte) -> int { return a "
                "+ b; }\n",
      .message = "Mismatched integer types int8 and byte; convert one operand",
      .line = 2,
      .column = 45 },
    /* Compound arithmetic rejects differing typed integers before assignment
     * widening, both for a local and for a stored field. */
    { .name = "mixed compound assignment",
      .source = "package facts\nGrow :: (b: byte) -> int { a: int = 1; a += b; "
                "return a; }\n",
      .message = "Mismatched integer types int and byte; convert one operand",
      .line = 2,
      .column = 40 },
    { .name = "mixed compound field assignment",
      .source = "package facts\nNote :: struct { count: int; }\n"
                "Grow :: (b: byte) -> int { n: Note = {count: 1}; n.count += "
                "b; return n.count; }\n",
      .message = "Mismatched integer types int and byte; convert one operand",
      .line = 3,
      .column = 50 },
    { .name = "compound assignment preserves contextual and explicit operands",
      .debug_metadata = true,
      .source = "package facts\nNote :: struct { count: int; }\n"
                "Plain :: (b: byte) -> int { a: int = 1; a = b; return a; }\n"
                "Local :: (b: byte) -> int { a: int = 1; a += int(b); return "
                "a; }\n"
                "Literal :: () -> int { a: int = 1; a += 2; return a; }\n"
                "Field :: (b: byte) -> int { n: Note = {count: 1}; n.count += "
                "int(b); return n.count; }\n"
                "FieldLiteral :: () -> int { n: Note = {count: 1}; n.count += "
                "2; return n.count; }\n"
                "Shift :: (a: int, count: byte) -> int { return a << count; "
                "}\n",
      .calls = { { .function = "Plain", .arguments = "[2]", .expected = "[2]" },
                 { .function = "Local", .arguments = "[2]", .expected = "[3]" },
                 { .function = "Literal", .arguments = "[]", .expected = "[3]" },
                 { .function = "Field", .arguments = "[2]", .expected = "[3]" },
                 { .function = "FieldLiteral", .arguments = "[]", .expected = "[3]" },
                 { .function = "Shift", .arguments = "[3,1]", .expected = "[6]" } } },
    { .name = "mixed compound reference assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Grow :: (a: ref int, b: byte) { a += b; }\n",
      .message = "Mismatched integer types int and byte; convert one operand",
      .line = 2,
      .column = 33 },
    { .name = "mixed compound reference field assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Note :: struct { count: int; }\n"
                "Grow :: (n: ref Note, b: byte) { n.count += b; }\n",
      .message = "Mismatched integer types int and byte; convert one operand",
      .line = 3,
      .column = 34 },
    { .name = "mixed compound boxed local assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Touch :: (v: ref int) { v += 0; }\n"
                "Grow :: (b: byte) -> int { a: int = 1; Touch(ref a); a += b; "
                "return a; }\n",
      .message = "Mismatched integer types int and byte; convert one operand",
      .line = 3,
      .column = 54 },
    { .name = "mixed compound boxed parameter assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Touch :: (v: ref int) { v += 0; }\n"
                "Grow :: (a: int, b: byte) -> int { Touch(ref a); a += b; "
                "return "
                "a; }\n",
      .message = "Mismatched integer types int and byte; convert one operand",
      .line = 3,
      .column = 50 },
    { .name = "compound reference and boxed operands preserve effects",
      .debug_metadata = true,
      .group = COLLECTION_SAMPLES,
      .source =
          "package facts\n"
          "Note :: struct { count: int; }\n"
          "Touch :: (v: ref int) { v += 0; }\n"
          "Bump :: (v: ref int, b: byte) { v += int(b); }\n"
          "BumpField :: (n: ref Note, b: byte) { n.count += int(b); }\n"
          "Source :: (calls: ref int) -> int { calls += 1; return 2; }\n"
          "Ref :: () -> int { n: int = 1; Bump(ref n, byte(2)); return n; }\n"
          "RefField :: () -> int { n: Note = {count: 1}; BumpField(ref n, "
          "byte(2)); return n.count; }\n"
          "Box :: () -> int { n: int = 1; Touch(ref n); n += int(byte(2)); "
          "return n; }\n"
          "BoxParam :: (n: int) -> int { Touch(ref n); n += 2; return n; }\n"
          "Once :: () -> int { n: Note = {count: 1}; calls: int = 0; "
          "n.count += Source(ref calls); return n.count * 10 + calls; }\n"
          "Bits :: (v: int) -> int { n: int = 6; n &= v; n |= 8; n ^= 1; "
          "return n; }\n"
          "Clamp :: (v: int) -> byte { return byte(v); }\n",
      .calls = { { .function = "Ref", .arguments = "[]", .expected = "[3]" },
                 { .function = "RefField", .arguments = "[]", .expected = "[3]" },
                 { .function = "Box", .arguments = "[]", .expected = "[3]" },
                 { .function = "BoxParam", .arguments = "[1]", .expected = "[3]" },
                 { .function = "Once", .arguments = "[]", .expected = "[31]" },
                 { .function = "Bits", .arguments = "[3]", .expected = "[11]" },
                 { .function = "Clamp", .arguments = "[300]", .expected = "[255]" } } },
    { .name = "mixed compound bitwise assignment",
      .source = "package facts\nGrow :: (b: byte) -> int { a: int = 1; a &= b; "
                "return a; }\n",
      .message = "Mismatched integer types int and byte; convert one operand",
      .line = 2,
      .column = 40 },
    { .name = "constant division by zero",
      .group = TYPE_SAMPLES,
      .source = "package facts\nRest :: (a: int) -> int { return a % (2 - 2); "
                "}\n",
      .message = "Constant division by zero",
      .line = 2,
      .column = 34 },
    { .name = "compound division by zero",
      .group = TYPE_SAMPLES,
      .source = "package facts\nHalve :: (a: int) -> int { b := a; b /= 0; "
                "return b; }\n",
      .message = "Constant division by zero",
      .line = 2,
      .column = 36 },
    { .name = "folded constant outside its type",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBig :: () -> int { return 1 << 32; }\n",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 27 },
    { .name = "conversion outside its type",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: byte : byte(300);\n",
      .message = "Constant does not fit in byte; use byte(wrap: value)",
      .line = 2,
      .column = 13 },
    { .name = "negated unsigned constant",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: byte : -byte(1);\n",
      .message = "Cannot negate an unsigned constant",
      .line = 2,
      .column = 13 },
    { .name = "signed shift count",
      .group = TYPE_SAMPLES,
      .source = "package facts\nShift :: (a: int, n: int) -> int { return a << "
                "n; }\n",
      .message = "Shift count must be unsigned; convert it with uint(value)",
      .line = 2,
      .column = 48 },
    /* Integer operations never fail at run time: unsigned negation wraps, a
     * count at or past the width gives 0 and a zero divisor gives 0. */
    { .name = "total integer operations",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Negate :: (a: byte) -> byte { return -a; }\n"
                "Wide :: (a: uint64) -> uint64 { return -a; }\n"
                "Far :: (a: int) -> int { return a << 40; }\n"
                "Fill :: (a: int) -> int { return a >> 40; }\n"
                "Divide :: (a: int, b: int) -> int { return a / b + a % b; }\n",
      .calls = { { .function = "Negate", .arguments = "[1]", .expected = "[255]" },
                 { .function = "Wide", .arguments = "[1]", .expected = "[18446744073709551615]" },
                 { .function = "Far", .arguments = "[3]", .expected = "[0]" },
                 { .function = "Fill", .arguments = "[-5]", .expected = "[-1]" },
                 { .function = "Fill", .arguments = "[5]", .expected = "[0]" },
                 { .function = "Divide", .arguments = "[7,0]", .expected = "[7]" } } },
    /* An untyped constant expression that names a constant takes its type
     * from its use, and only integer division by zero is an error. */
    { .name = "untyped constant expressions",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Count: uint : 3;\n"
                "Eight :: (x: int64) -> bool { return x == (1 << Count); }\n"
                "Huge :: (f: double) -> bool { return f / 0 > 1000000.0; }\n",
      .calls = { { .function = "Eight", .arguments = "[8]", .expected = "[true]" },
                 { .function = "Huge", .arguments = "[1]", .expected = "[true]" } } },
    /* A shift has its left operand's type, and a count past that type's
     * maximum clamps to it. Exact constant shifts retain the written count. */
    { .name = "shift counts clamp",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Literal :: (a: byte) -> byte { return a << 300; }\n"
                "Counted :: (a: byte, n: uint) -> byte { return a << n; }\n"
                "Signed :: (a: int8, n: uint64) -> int8 { return a >> n; }\n",
      .calls = { { .function = "Literal", .arguments = "[1]", .expected = "[0]" },
                 { .function = "Counted", .arguments = "[1,300]", .expected = "[0]" },
                 { .function = "Counted", .arguments = "[1,7]", .expected = "[128]" },
                 { .function = "Signed", .arguments = "[-5,4096]", .expected = "[-1]" } } },
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
          "Grow :: (xs: ref int[]) { item: int = 7; xs = append(xs, item); }\n"
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
    /* Engine array kernels take arrays of managed pointers without retaining
     * them, so the native argument boundary does not reject or walk them. */
    { .name = "managed pointer arrays",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Count :: () -> int { x: int = 1; y: int = 2; "
                "ps: ^int[] = [&x]; ps.append(&y); ps[1]^ = 7; "
                "return length(ps) * 10 + y; }\n",
      .calls = { { .function = "Count", .arguments = "[]", .expected = "[27]" } } },
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
    /* A compound write reads its pointer target once, before its value, as
     * stage zero caches the target. Each call adds to `calls`, so receivers
     * and values evaluate exactly once. */
    { .name = "managed pointer compound order",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Set :: (q: ^int, calls: ^int) -> int { calls^ += 10; "
                "q^ = 10; return 2; }\n"
                "SetX :: (q: ^Point, calls: ^int) -> int { calls^ += 10; "
                "q.x = 10; return 2; }\n"
                "Pick :: (p: ^int, calls: ^int) -> ^int { calls^ += 1; "
                "return p; }\n"
                "PickPoint :: (p: ^Point, calls: ^int) -> ^Point { "
                "calls^ += 1; return p; }\n"
                "Through :: () -> int { value: int = 1; calls: int = 0; "
                "p := &value; Pick(p, &calls)^ += Set(p, &calls); "
                "return value * 100 + calls; }\n"
                "Local :: () -> int { value: int = 1; calls: int = 0; "
                "p := &value; value += Set(p, &calls); "
                "return value * 100 + calls; }\n"
                "Member :: () -> int { value: Point = {x: 1, y: 0}; "
                "calls: int = 0; p := &value; "
                "PickPoint(p, &calls).x += SetX(p, &calls); "
                "return value.x * 100 + calls; }\n"
                "Owned :: () -> int { value: Point = {x: 1, y: 0}; "
                "calls: int = 0; p := &value; value.x += SetX(p, &calls); "
                "return value.x * 100 + calls; }\n",
      .calls = { { .function = "Through", .arguments = "[]", .expected = "[311]" },
                 { .function = "Local", .arguments = "[]", .expected = "[310]" },
                 { .function = "Member", .arguments = "[]", .expected = "[311]" },
                 { .function = "Owned", .arguments = "[]", .expected = "[310]" } } },
    /* A narrower integer widens when it is stored through a pointer or into
     * an addressed local, as ordinary assignment widens it. */
    { .name = "managed pointer widening",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Widened :: () -> int64 { x: int64 = 0; y: int = 1; p := &x; "
                "x = y; p^ = y + 1; return x; }\n"
                "Floated :: () -> double { x: double = 0; f: float = 1.5; p := "
                "&x; "
                "p^ = f; return x; }\n",
      .calls = { { .function = "Widened", .arguments = "[]", .expected = "[2]" },
                 { .function = "Floated", .arguments = "[]", .expected = "[1.5]" } } },
    /* A float operand widens in a compound through a pointer, an addressed
     * local and a pointer's field, as it does for an ordinary local. */
    { .name = "managed pointer compound float widening",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "R :: struct { v: double; }\n"
                "Local :: () -> double { x: double = 1.0; y: float = 2.0; "
                "x += y; return x; }\n"
                "Through :: () -> double { x: double = 1.0; y: float = 2.0; "
                "p := &x; p^ += y; return x; }\n"
                "Addressed :: () -> double { x: double = 1.0; y: float = 2.0; "
                "p := &x; x += y; return p^; }\n"
                "Member :: () -> double { r: R = {v: 1.0}; y: float = 2.0; "
                "p := &r; p.v += y; return r.v; }\n",
      .calls = { { .function = "Local", .arguments = "[]", .expected = "[3.0]" },
                 { .function = "Through", .arguments = "[]", .expected = "[3.0]" },
                 { .function = "Addressed", .arguments = "[]", .expected = "[3.0]" },
                 { .function = "Member", .arguments = "[]", .expected = "[3.0]" } } },
    { .name = "managed pointer compound narrowing",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nRun :: () -> float {\n    x: float = 1.0;\n"
                "    d: double = 2.0;\n    p := &x;\n    p^ += d;\n    return "
                "x;\n}",
      .message = "compound assignment needs number operands of its type",
      .line = 6,
      .column = 5 },
    /* Mixed integer compounds reject through a pointer, as they do for an
     * ordinary local. */
    { .name = "managed pointer compound mixed integers",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nRun :: () -> int64 {\n    x: int64 = 0;\n"
                "    y: int = 1;\n    p := &x;\n    p^ += y;\n    return x;\n}",
      .message = "Mismatched integer types int64 and int; convert one operand",
      .line = 6,
      .column = 5 },
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
                "    high := lib.High;\n"
                "    if (lib.Low < 0 && high > 0) { return high + lib.Low; }\n"
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
                "Bytes :: (n: int) -> byte[] {\n"
                "    out: byte[] = [1, 2, 255];\n"
                "    out = append(out, byte(wrap: n));\n"
                "    out = append(out, Small);\n"
                "    return out;\n}\n"
                "High :: (n: int) -> int { mask := Mask; return int(wrap: "
                "(mask >> 56) + uint64(n)); }\n"
                "Mixed :: (a: int, b: int64) -> int64 { return int64(a) + b; "
                "}\n"
                "Store :: (a: int) -> int64 { x: int64 = a; x += 10; x "
                "*= 3; return x; }\n"
                "Narrow :: (v: int64) -> int { return int(v); }\n"
                "Clamp :: (v: int) -> byte { return byte(v); }",
      .calls = { { .function = "Hash", .arguments = "[[1,2,3]]", .expected = "[11570874782335668893]" },
                 { .function = "Bytes", .arguments = "[300]", .expected = "[[1,2,255,44,200]]" },
                 { .function = "High", .arguments = "[1]", .expected = "[256]" },
                 { .function = "Mixed", .arguments = "[2,5000000000]", .expected = "[5000000002]" },
                 { .function = "Store", .arguments = "[4]", .expected = "[42]" },
                 { .function = "Narrow", .arguments = "[7]", .expected = "[7]" },
                 { .function = "Narrow", .arguments = "[5000000000]", .expected = "[2147483647]" },
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
                "Twice :: () -> int64 { wide := Wide; return wide * 2; }\n"
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
                "    zero: int = 0;\n"
                "    counts := resize([zero], n);\n"
                "    counts[0] = 7;\n"
                "    total: int = 0;\n"
                "    for (count in counts) { total += count; }\n"
                "    if (flags[1]) { return -1; }\n"
                "    return total * 10 + length(flags);\n}\n"
                "Words :: () -> int { first := \"x\"; words := [first, "
                "\"yy\", \"zzz\"]; return length(words); }\n"
                "Points :: () -> int { p: Point = {x: 1, y: 2}; points "
                ":= [p, p]; return points[1].y + length(points); }\n"
                "Wide :: () -> uint64 { one: uint64 = 1; bits := [one, 2, "
                "0xFF]; "
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
                "0xFFFFFFFFFFFFFFFF; small: int64 = -3; return "
                "format_text(\"{} {} {}\", flag, big, small); }",
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
                "    copy.count += int(b);\n"
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
      .source = "package facts\nA: int : 65536 * 65536;",
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
      .message = "Mismatched integer types int and uint64; convert one operand",
      .line = 2,
      .column = 47 },
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
    { .name = "inferred numeric locals",
      .source = "package facts\n"
                "Bump :: (x: ref int) { x += 1; }\n"
                "ByReference :: () -> int { x := 0; Bump(ref x); return x; }\n"
                "One :: () -> int { x := 5; return x; }\n"
                "Index :: () -> int { x := 0; xs: int[] = [4, 7]; x += 1; "
                "xs[x] = 9; return xs[x] + x; }\n"
                "Negative :: () -> int { x := -1; x += 2; return x; }\n"
                "Wide :: () -> int64 { x := 2147483648; x += 1; return x; }\n"
                "Maximum :: () -> uint64 { x := 18446744073709551615; return "
                "x; }\n"
                "Decimal :: () -> double { x := 0.5; x += 0.25; return x; }\n"
                "Exponent :: () -> double { x := 1e3; return x; }",
      .calls = { { .function = "ByReference", .arguments = "[]", .expected = "[1]" },
                 { .function = "One", .arguments = "[]", .expected = "[5]" },
                 { .function = "Index", .arguments = "[]", .expected = "[10]" },
                 { .function = "Negative", .arguments = "[]", .expected = "[1]" },
                 { .function = "Wide", .arguments = "[]", .expected = "[2147483649]" },
                 { .function = "Maximum", .arguments = "[]", .expected = "[18446744073709551615]" },
                 { .function = "Decimal", .arguments = "[]", .expected = "[0.75]" },
                 { .function = "Exponent", .arguments = "[]", .expected = "[1000.0]" } } },
    { .name = "inferred integer refuses float assignment",
      .source = "package facts\nOne :: () -> int { x := 0; fraction := 0.5; x "
                "= fraction; return x; }",
      .message = "assigned value differs from its local",
      .line = 2,
      .column = 45 },
    { .name = "conversion arity",
      .source = "package facts\nOne :: () -> byte { return byte(1, 2); "
                "}",
      .message = "Conversion takes one value",
      .line = 2,
      .column = 28 },
    { .name = "conversion name",
      .source = "package facts\nOne :: () -> byte { return byte(round: "
                "1); }",
      .message = "A conversion argument may only be named wrap",
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
    /* Unary complements are classified as literal-shaped for debug metadata.
     * Their folded value still needs one watch, including through grouping. */
    { .name = "folded complement retains its watch",
      .source = "package facts\nOne :: () -> int { return ~0; }\n"
                "Grouped :: () -> int { return (~0); }",
      .calls = { { .function = "One", .arguments = "[]", .expected = "[-1]" },
                 { .function = "Grouped", .arguments = "[]", .expected = "[-1]" } } },
    /* An untyped operand of a conversion is int64, or uint64 beyond it,
     * in parentheses too. */
    { .name = "wrapped literal in parentheses",
      .source = "package facts\nOne :: () -> int { return int(wrap: "
                "(0xFFFFFFFFFFFFFFFF)); }",
      .calls = { { .function = "One", .arguments = "[]", .expected = "[-1]" } } },
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
    { .name = "qualified native calls",
      .c_string_probe = true,
      .host_effects = true,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Probe :: () -> int { return native.probe(\"seven..\"); }\n"
                "Caller :: () -> int { return Probe(); }\n"
                "Recursive :: (n: int) -> int { if (n <= 0) { return Probe(); "
                "} return Recursive(n - 1); }\n"
                "ImportedHost :: () -> int { return lib.Through(); }\n"
                "Length :: () -> int { return native.length(\"four\"); }\n"
                "Text :: () -> int { return native . overload(\"three\"); }\n"
                "Integer :: (value: int) -> int { return native . "
                "overload(value); }",
      .library = "package lib\npub Through :: () -> int { return "
                 "native.probe(\"lib\"); }" },
    { .name = "qualified native import and local precedence",
      .c_string_probe = true,
      .source = "package facts\nimport native \"project:lib\"\n"
                "Imported :: () -> int { return native.probe(\"seven..\"); }\n"
                "Local :: () -> int { native: int[] = [1, 2]; "
                "native.append(3); return length(native); }",
      .library = "package lib\npub probe :: (value: string) -> int { return "
                 "99; }",
      .calls = { { .function = "Imported", .arguments = "[]", .expected = "[99]" },
                 { .function = "Local", .arguments = "[]", .expected = "[3]" } } },
    { .name = "qualified native missing",
      .c_string_probe = true,
      .source = "package facts\nCall :: () -> int { return native.absent(); }",
      .message = "Unknown function 'native.absent'",
      .line = 2,
      .column = 35 },
    { .name = "qualified native ambiguous",
      .c_string_probe = true,
      .source = "package facts\nCall :: () -> int { return "
                "native.PROBE_ONE(\"x\"); }",
      .message = "Ambiguous function 'native.PROBE_ONE'",
      .line = 2,
      .column = 35 },
    { .name = "qualified native import missing",
      .c_string_probe = true,
      .source = "package facts\nimport native \"project:lib\"\nCall :: () -> "
                "int { return native.probe(\"x\"); }",
      .library = "package lib\npub Other :: () -> int { return 0; }",
      .message = "Unknown function 'probe'",
      .line = 3,
      .column = 35 },
    { .name = "qualified native local shadow",
      .c_string_probe = true,
      .source = "package facts\nCall :: () -> int { native: int = 1; return "
                "native.probe(\"x\"); }",
      .message = "call needs a function name",
      .line = 2,
      .column = 45 },
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
    /* Authored constant support boundaries. Stage zero accepts some of these
     * forms; require the authored diagnostic at the operand, not parity with
     * stage zero or an earlier unsupported-declaration failure. */
    { .name = "floating constant cannot become integer bits",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nWide: double : 1.5;\nBad: uint64 : "
                "uint64(Wide);\n",
      .message = "floating-to-integer constant conversion is not supported yet",
      .line = 3,
      .column = 22 },
    { .name = "floating constant cannot wrap into integer bits",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nWide: double : 1.5;\nBad: int : int(wrap: "
                "Wide);\n",
      .message = "floating-to-integer constant conversion is not supported yet",
      .line = 3,
      .column = 22 },
    { .name = "floating conversion cannot become integer bits",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nBad: uint64 : uint64(double(1.5));\n",
      .message = "floating-to-integer constant conversion is not supported yet",
      .line = 2,
      .column = 22 },
    { .name = "floating shift count cannot become integer bits",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nWide: double : 1.5;\nBad: int : 1 << Wide;\n",
      .message = "constant value must be an integer expression",
      .line = 3,
      .column = 17 },
    { .name = "floating binary constant initializer remains unsupported",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nBad: double : 1.0 + 2.0;\n",
      .message = "floating constant initializer is not supported yet",
      .line = 2,
      .column = 15 },
};
enum
{
    SAMPLES = sizeof samples / sizeof *samples
};
static compiler_fixture fixture, oracle, consumer, corpus;
/* The compiler host's natives, and those with the core pack a game links,
 * which declares assert. */
static native_capabilities *linked[2][2];
/* Compile's native tables for the host and for a game, per compiler backend,
 * built once from `linked`. */
static pin_value linked_tables[2][2][2][3];
static pin_value linked_effects[2][2][2];
static arena input, detached;
static compiler_host *host;
static compiler_host_result result;

/* Whether a test published its project's types, which cleanup withdraws
 * after a test that returned early. */
static bool project_published;
static void publish_project(const type_catalog_pack *pack)
{
    type_catalog_set_project(pack);
    project_published = pack;
}
static void cleanup()
{
    if (project_published) {
        publish_project(NULL);
    }
    compiler_fixture_free(&fixture);
    compiler_fixture_free(&oracle);
    compiler_fixture_free(&consumer);
    compiler_fixture_free(&corpus);
    for (int game = 0; game < 2; game++) {
        for (int probe = 0; probe < 2; probe++) {
            native_capabilities_free(linked[game][probe]);
        }
    }
    memset(linked, 0, sizeof linked);
    memset(linked_tables, 0, sizeof linked_tables);
    memset(linked_effects, 0, sizeof linked_effects);
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
static pin_value record_row(const compiler_fixture *f, int b, const char *identity, const char *name)
{
    const pin_record *shape = compiler_fixture_record(f, b, identity, name, &input);
    assert(shape);
    pin_value v = { .type = PIN_STRUCT,
                    .ptr = arena_alloc(&input, shape->type.size),
                    .record = shape,
                    .type_name = shape->type.name };
    memset(v.ptr, 0, shape->type.size);
    return v;
}
static pin_value row(int b, const char *name)
{
    return record_row(&fixture, b, PACKAGE, name);
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
    pin_value *functions = NULL, *ports = NULL, *identities = NULL, *effects = NULL;
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
        apush(effects, ((pin_value){ .type = PIN_I32, .i = (int)c->effects }));
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
    linked_effects[game][probe][b] = rows(effects, acount(effects), PIN_I32, NULL);
    memcpy(linked_tables[game][probe][b], out, sizeof linked_tables[game][probe][b]);
    afree(functions);
    afree(ports);
    afree(identities);
    afree(effects);
    return true;
}
static bool get(const pin_value *row, const char *name, pin_value *out)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, out));
    return true;
}
static uint64_t fixture_variant_tag(
    const compiler_fixture *f,
    int b,
    const char *identity,
    const char *union_name,
    const char *variant
)
{
    const authoring_package *owner = compiler_fixture_package(f, b, identity);
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(f, b, identity, union_name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, variant)) {
            return type->variants[i].tag;
        }
    }
    return 0;
}
static uint64_t variant_tag(int b, const char *union_name, const char *variant)
{
    return fixture_variant_tag(&fixture, b, PACKAGE, union_name, variant);
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
/* This host's target, as Compile and CollectScalar take it. */
static bool target(int b, pin_value *out)
{
    const pin_record *record = compiler_fixture_record(&fixture, b, PACKAGE, "Target", &input);
    REQUIRE(record);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .ptr = arena_alloc(&input, record->type.size),
                        .record = record,
                        .type_name = record->type.name };
    memset(out->ptr, 0, record->type.size);
    REQUIRE(put(out, "pointer_storage", BF_POINTER_TYPE));
    REQUIRE(put(out, "pointer_size", (int)sizeof(void *)));
    REQUIRE(put(out, "value_size", (int)sizeof(pin_value)));
    REQUIRE(put(out, "payload_offset", (int)offsetof(pin_value, d)));
    REQUIRE(put(out, "results_offset", (int)offsetof(behavior_function_context, results)));
    REQUIRE(put(out, "type_offset", (int)offsetof(pin_value, type)));
    REQUIRE(put(out, "container_offset", (int)offsetof(pin_value, container)));
    REQUIRE(put(out, "name_offset", (int)offsetof(pin_value, type_name)));
    REQUIRE(put(out, "record_offset", (int)offsetof(pin_value, record)));
    REQUIRE(put(out, "count_offset", (int)offsetof(pin_array, count)));
    REQUIRE(put(out, "items_offset", (int)offsetof(pin_array, items)));
    REQUIRE(put(out, "data_offset", (int)offsetof(pin_array, data)));
    REQUIRE(put(out, "stride_offset", (int)offsetof(pin_array, stride)));
    REQUIRE(put(out, "map_count_offset", (int)offsetof(pin_map, count)));
    REQUIRE(put(out, "data_failure", BEHAVIOR_FUNCTION_HOST_DATA));
    REQUIRE(put(out, "argument_failure", BEHAVIOR_FUNCTION_ARGUMENT));
    REQUIRE(put(out, "assert_failure", BEHAVIOR_FUNCTION_DATA));
    return true;
}
/* The names of this host's prepared instructions, in their order. */
static pin_value instruction_names()
{
    int instructions = execution_prepared_instruction_count();
    pin_array *names = pin_array_snapshot_allocate(&input, PIN_STR, NULL, instructions);
    for (int i = 0; i < instructions; i++) {
        names->items[i] = text(execution_prepared_instruction_name(i));
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = names };
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
    REQUIRE(target(b, &args[6]));
    args[7] = (pin_value){ .type = PIN_U64, .u64 = execution_prepared_abi() };
    args[8] = instruction_names();
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
    bool effects,
    const source_package_input *inputs,
    int count,
    uintptr_t budget,
    pin_value *out
)
{
    pin_value args[ARGUMENTS + 1];
    REQUIRE(arguments_for(b, game, probe, inputs, count, args));
    args[ARGUMENTS] = linked_effects[game][probe][b];
    REQUIRE(compiler_fixture_call(
        &fixture,
        b,
        PACKAGE,
        effects ? "CompileWithEffects" : "Compile",
        args,
        ARGUMENTS + effects,
        budget,
        &detached,
        out
    ));
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
static package_collection *read_packages(
    const unsigned char *bytes,
    size_t length,
    native_capabilities **native,
    behavior_backend backend
)
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
    linker.decoded_records = COMPILER_HOST_MAX_CLOSURE_RECORDS;
    linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        &linker,
        backend,
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
/* The receiver input must be captured before the authored item call. Comparing
 * watch sets alone cannot detect a watch moved across that call. Each fixture
 * function has exactly one update and one authored invocation. */
static bool array_update_order(const unsigned char *bytes, size_t length, const sample *s)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    for (int c = 0; c < CALLS && s->calls[c].function; c++) {
        const behavior_function *body = NULL;
        for (int p = 0; p < package_collection_count(packages); p++) {
            const execution_module *module = package_collection_module(packages, p);
            for (int f = 0; f < module->plan.function_count; f++) {
                if (module->plan.functions[f].id == function_id(s->calls[c].function)) {
                    body = module->plan.functions[f].body;
                }
            }
        }
        REQUIRE(body);
        int receiver = -1, item = -1, invocation = -1;
        for (int pc = 0; pc < acount(body->instructions); pc++) {
            const behavior_function_instruction *op = &body->instructions[pc];
            if (op->op == BF_INVOKE) {
                REQUIRE(invocation < 0);
                invocation = pc;
            }
            if (op->op != BF_SOURCE_WATCH) {
                continue;
            }
            const behavior_source_watch *watch = &body->source_watches[op->target];
            if (watch->output) {
                continue;
            }
            if (watch->ordinal == 0 && watch->initial.container == PIN_CONTAINER_ARRAY) {
                REQUIRE(receiver < 0);
                receiver = pc;
            }
            if (watch->ordinal == 1) {
                REQUIRE(item < 0);
                item = pc;
            }
        }
        if (!(receiver >= 0 && receiver < invocation && invocation < item)) {
            fprintf(
                stderr,
                "%s debug order: receiver %d, invocation %d, item %d\n",
                s->calls[c].function,
                receiver,
                invocation,
                item
            );
        }
        REQUIRE(receiver >= 0 && receiver < invocation && invocation < item);
    }
    package_collection_free(packages);
    native_capabilities_free(native);
    return true;
}
typedef struct
{
    const behavior_function *item;
    int start, end, step;
    bool valid;
} array_update_trace;
static int array_update_pause(void *user, behavior_function_context *context, const behavior_source_point *point)
{
    array_update_trace *trace = user;
    if (context->source_function == trace->item &&
        point->kind == BEHAVIOR_SOURCE_FUNCTION && !point->completion) {
        trace->valid &= trace->step == 1;
        trace->step++;
    }
    return 0;
}
static void array_update_capture(
    void *user,
    behavior_function_context *,
    const behavior_source_watch *watch,
    const pin_value *value
)
{
    array_update_trace *trace = user;
    if (watch->origin.start != trace->start || watch->origin.end != trace->end) {
        return;
    }
    if (!value) {
        trace->valid = false;
        return;
    }
    if (!watch->output && watch->ordinal == 1) {
        trace->valid &= trace->step == 2 && value->type == PIN_I32 &&
                        value->container == PIN_CONTAINER_SINGLE && value->i == 7;
        trace->step++;
        return;
    }
    const pin_array *array = value->ptr;
    bool valid = value->type == PIN_I32 && value->container == PIN_CONTAINER_ARRAY &&
                 array && array->count == (watch->output ? 2 : 1);
    if (valid) {
        valid = pin_array_get(array, 0).i == 1 &&
                (!watch->output || pin_array_get(array, 1).i == 7);
    }
    trace->valid &= valid && watch->ordinal == 0 &&
                    trace->step == (watch->output ? 3 : 0);
    trace->step++;
}
/* Observe actual delivery as well as emitted order. Payload pointers belong
 * to the invocation; the callbacks retain only their checked literal values. */
static bool observes_array_update(const unsigned char *bytes, size_t length, const sample *s)
{
    const char *call = "append(xs, Item())";
    const char *start = strstr(s->source, call);
    REQUIRE(start);
    for (int backend = 0; backend < 2; backend++) {
        native_capabilities *native;
        package_collection *packages = read_packages(
            bytes,
            length,
            &native,
            backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE
        );
        REQUIRE(packages);
        const behavior_function *body = NULL, *item = NULL;
        for (int p = 0; p < package_collection_count(packages); p++) {
            const execution_module *module = package_collection_module(packages, p);
            for (int f = 0; f < execution_module_function_count(module); f++) {
                execution_module_function_info info = execution_module_function_at(module, f);
                if (info.id == function_id("AppendLocal")) {
                    body = info.body;
                }
                if (info.id == function_id("Item")) {
                    item = info.body;
                }
            }
        }
        REQUIRE(body && item);
        array_update_trace trace = { .item = item,
                                     .start = (int)(start - s->source),
                                     .end = (int)(start - s->source + strlen(call)),
                                     .valid = true };
        arena storage = { 0 };
        pin_value result = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY };
        behavior_function_activation activation = { 0 };
        bool resumable = behavior_function_resumable(body);
        if (resumable) {
            behavior_function_activation_init(body, &activation);
        }
        behavior_function_prepare_results(body, &result, &storage);
        behavior_function_context context = { .activation = resumable ? &activation : NULL,
                                              .results = &result,
                                              .budget = TARGET_BUDGET,
                                              .source_debug_user = &trace,
                                              .source_pause = array_update_pause,
                                              .source_capture = array_update_capture };
        behavior_function_bind_storage(&context, &storage);
        bool bound = behavior_function_bind(body, &context, NULL, 0);
        behavior_run_scope runs = { 0 };
        behavior_run_scope_begin(&runs);
        int status = bound ? behavior_function_run(body, &context, backend != 0)
                           : FLOW_ERR_ARGUMENT;
        behavior_run_scope_end(&runs);
        const pin_array *array = result.ptr;
        bool valid = status == FLOW_OK && context.returned && trace.valid &&
                     trace.step == 4 && array && array->count == 2 &&
                     pin_array_get(array, 0).i == 1 &&
                     pin_array_get(array, 1).i == 7;
        behavior_function_release_host(&context);
        behavior_function_activation_free(&activation);
        arena_free(&storage);
        package_collection_free(packages);
        native_capabilities_free(native);
        REQUIRE(valid);
        REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
        REQUIRE(backend != 0 || runs.counts.bytecode_entries > 0);
        if (backend && TEST_COMPILED_BACKEND == BEHAVIOR_SLJIT) {
            REQUIRE(runs.counts.inspected_sljit_entries > 0 && !runs.counts.bytecode_entries);
        }
        if (backend && TEST_COMPILED_BACKEND == BEHAVIOR_WASM) {
            REQUIRE(runs.counts.inspected_wasm_entries > 0);
        }
    }
    return true;
}
/* Compare serialized function flags, independently of the runtime's derived
 * effect summary. Direct, recursive and imported HOST callers must retain it. */
static bool same_host_effects(const execution_module *expected, const execution_module *actual)
{
    bool same = true;
    int observed = 0;
    for (int i = 0; i < expected->plan.function_count; i++) {
        const execution_module_function *x = &expected->plan.functions[i];
        const execution_module_function *y = NULL;
        for (int j = 0; !y && j < actual->plan.function_count; j++) {
            if (actual->plan.functions[j].id == x->id) {
                y = &actual->plan.functions[j];
            }
        }
        bool host_call = x->id == function_id("Probe") ||
                         x->id == function_id("Caller") ||
                         x->id == function_id("Recursive") ||
                         x->id == function_id("ImportedHost") ||
                         x->id == script_catalog_id("package-symbol", "project:lib:1:Through:callable");
        REQUIRE(!host_call || (x->effects & SCRIPT_EFFECT_HOST));
        observed += host_call;
        REQUIRE(y);
        if (x->effects != y->effects) {
            fprintf(
                stderr,
                "stored function effects %016llx: stage zero=%u stage one=%u\n",
                (unsigned long long)x->id,
                x->effects,
                y->effects
            );
        }
        same &= x->effects == y->effects;
    }
    REQUIRE(observed > 0);
    return same;
}
/* Each module retains stage zero's source points, watches and, when supplied,
 * effects for the target's natives. */
static bool points_like_oracle(const unsigned char *bytes, size_t length, bool host_effects)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
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
                same_watches(expected, module) &&
                (!host_effects || same_host_effects(expected, module));
    }
    package_collection_free(packages);
    native_capabilities_free(native);
    return alike;
}
/* Each compiler backend has a focused selector. Bytecode compares source
 * points with stage zero; both compare explicit target effects. */
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
static bool rejects_malformed_effects()
{
    static const source_package_input bad_source = { "facts/Main.chibi", "not source", 10 };
    pin_value args[ARGUMENTS + 1];
    REQUIRE(arguments_for(0, false, false, &bad_source, 1, args));
    const pin_array *declared = linked_effects[0][0][0].ptr;
    REQUIRE(declared && declared->count > 0);
    for (int i = 0; i < 3; i++) {
        int count = i ? declared->count : 0;
        pin_array *masks = pin_array_snapshot_allocate(&input, PIN_I32, NULL, count);
        for (int j = 0; j < count; j++) {
            masks->items[j] = (pin_value){ .type = PIN_I32,
                                           .i = j      ? 0
                                              : i == 1 ? -1
                                                       : 128 };
        }
        args[ARGUMENTS] = (pin_value){ .type = PIN_I32,
                                       .container = PIN_CONTAINER_ARRAY,
                                       .ptr = masks };
        pin_value out, message, document;
        REQUIRE(compiler_fixture_call(
            &fixture,
            0,
            PACKAGE,
            "CompileWithEffects",
            args,
            ARGUMENTS + 1,
            COMPILE_BUDGET,
            &detached,
            &out
        ));
        const pin_union *value = out.ptr;
        REQUIRE(out.type == PIN_UNION && value && value->tag == tag(0, "Rejected"));
        REQUIRE(get(&value->values[0], "message", &message));
        REQUIRE(get(&value->values[0], "document", &document));
        REQUIRE(
            !strcmp(pin_text(message).bytes, "unsupported compiler input or target") &&
            document.i == -1
        );
        arena_free(&detached);
    }
    return true;
}
static bool run_samples(sample_group group, int b)
{
    REQUIRE(prepare());
    if (group == BASE_SAMPLES && b == 0) {
        REQUIRE(rejects_malformed_effects());
    }
    for (int i = 0; i < (int)SAMPLES; i++) {
        const sample *s = &samples[i];
        if (s->group != group) {
            continue;
        }
        source_package_input inputs[SAMPLE_INPUTS];
        int count = sample_inputs(s, inputs);
        pin_value out;
        bool compiled = compile(b, s->game, s->c_string_probe, s->host_effects, inputs, count, COMPILE_BUDGET, &out);
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
            if (value->tag != tag(b, "Rejected")) {
                fprintf(stderr, "package compiler %s: accepted\n", s->name);
            }
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
                   ((b && !s->host_effects && !s->debug_metadata) ||
                    (oracle_closure(s, &expected, &expected_length) &&
                     points_like_oracle(bytes, length, s->host_effects)));
        for (int c = 0; ran && c < CALLS && s->calls[c].function; c++) {
            for (int backend = 0; ran && backend < 2; backend++) {
                ran = run(bytes, length, &s->calls[c], backend);
            }
            ran = ran && (b || run(expected, expected_length, &s->calls[c], 0));
        }
        if (ran && s->update_order) {
            ran = (b || array_update_order(expected, expected_length, s)) &&
                  array_update_order(bytes, length, s) &&
                  observes_array_update(bytes, length, s);
            pin_value args[ARGUMENTS], closure;
            REQUIRE(arguments(b, false, inputs, count, args));
            REQUIRE(compiler_fixture_call(
                &fixture,
                b,
                PACKAGE,
                "CompileClosure",
                args,
                ARGUMENTS,
                COMPILE_BUDGET,
                &detached,
                &closure
            ));
            const pin_array *array = closure.ptr;
            REQUIRE(closure.type == PIN_U8 && closure.container == PIN_CONTAINER_ARRAY && array);
            REQUIRE(length > 0 && (size_t)array->count == length);
            bool same = true;
            for (size_t i = 0; i < length; i++) {
                same &= array->items[i].u8 == bytes[i];
            }
            arena_free(&detached);
            REQUIRE(same);
        }
        free(bytes);
        free(expected);
        if (!ran) {
            fprintf(stderr, "package compiler %s: differs\n", s->name);
        }
        REQUIRE(ran);
    }
    return true;
}
/* Without debugger points and watches, a closure's functions have none, and
 * its loops and branches still run as they do with them. */
TEST_CASE(package_compiler_omits_debug_info)
{
    REQUIRE(prepare());
    const char *const names[] = { "loops", "control", "array update debug order" };
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
        package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
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
/* Stage zero currently resolves assert through its native catalog, so its
 * ordinary pure-target oracle cannot model this authored intrinsic contract.
 * Compile directly with no game/native assertion pack and inspect the emitted
 * checked instruction before running both execution backends. Existing oracle
 * samples remain unchanged. */
TEST_CASE(package_compiler_asserts_without_native_capability)
{
    REQUIRE(prepare());
    const sample cases[] = {
        { .source = "package facts\n"
                    "Double :: (n: int) -> int {\n"
                    "    assert(condition: n > 0, message: \"n is "
                    "positive\");\n"
                    "    return n * 2;\n}\n",
          .calls = { { .function = "Double", .arguments = "[3]", .expected = "[6]" },
                     { .function = "Double",
                       .arguments = "[-1]",
                       .expected = NULL,
                       .diagnostic = "Assertion failed: n is positive" } } },
        { .source = "package facts\n"
                    "assert :: (n: int) -> int { return n + 7; }\n"
                    "Main :: () -> int { return assert(3); }\n",
          .calls = { { .function = "Main", .arguments = "[]", .expected = "[10]" } } },
        { .source = "package facts\n"
                    "import lib \"project:lib\"\n"
                    "Main :: () -> int { return lib.assert(3); }\n",
          .library = "package lib\npub assert :: (n: int) -> int { return n + "
                     "9; }\n",
          .calls = { { .function = "Main", .arguments = "[]", .expected = "[12]" } } }
    };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        source_package_input inputs[SAMPLE_INPUTS];
        int count = sample_inputs(&cases[i], inputs);
        for (int b = 0; b < 2; b++) {
            pin_value out;
            REQUIRE(compile(b, false, false, false, inputs, count, COMPILE_BUDGET, &out));
            const pin_union *value = out.ptr;
            REQUIRE(out.type == PIN_UNION && value);
            REQUIRE(value->tag == tag(b, "Compiled") && value->count == 2);
            size_t length;
            unsigned char *bytes = closure_bytes(value, &length);
            arena_free(&detached);
            native_capabilities *native;
            package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
            REQUIRE(packages);
            int checked = 0;
            for (int p = 0; p < package_collection_count(packages); p++) {
                const execution_module_plan
                    *plan = &package_collection_module(packages, p)->plan;
                for (int f = 0; f < plan->function_count; f++) {
                    const behavior_function *body = plan->functions[f].body;
                    for (int op = 0; op < acount(body->instructions); op++) {
                        if (body->instructions[op].op == BF_ASSERT) {
                            REQUIRE(body->instructions[op].target == BEHAVIOR_FUNCTION_DATA);
                            REQUIRE(!acount(body->host_calls));
                            checked++;
                        }
                    }
                }
            }
            package_collection_free(packages);
            native_capabilities_free(native);
            REQUIRE(checked == (i == 0));
            bool ran = true;
            for (int c = 0; ran && c < CALLS && cases[i].calls[c].function; c++) {
                for (int backend = 0; ran && backend < 2; backend++) {
                    ran = run(bytes, length, &cases[i].calls[c], backend);
                }
            }
            free(bytes);
            REQUIRE(ran);
        }
    }
    return true;
}
static bool approved_float_conversion_admission(int compiler, bool constants)
{
    bool agreed = true;
    for (size_t i = 0; i < sizeof FLOAT_CONVERSION_CASES / sizeof *FLOAT_CONVERSION_CASES;
         i++) {
        const float_conversion_case *item = &FLOAT_CONVERSION_CASES[i];
        if (item->constant != constants) {
            continue;
        }
        source_package_input source = { "facts/Main.chibi",
                                        item->source,
                                        strlen(item->source) };
        pin_value out;
        REQUIRE(compile(compiler, false, false, false, &source, 1, COMPILE_BUDGET, &out));
        const pin_union *value = out.ptr;
        REQUIRE(out.type == PIN_UNION && value);
        REQUIRE(value->tag == tag(compiler, "Compiled") || value->tag == tag(compiler, "Rejected"));
        bool accepted = value->tag == tag(compiler, "Compiled");
        if (!accepted) {
            pin_value message, line, column;
            REQUIRE(get(&value->values[0], "message", &message));
            REQUIRE(get(&value->values[0], "line", &line));
            REQUIRE(get(&value->values[0], "column", &column));
            REQUIRE(line.type == PIN_I32 && column.type == PIN_I32);
            fprintf(
                stderr,
                "FLOAT_REJECTION stage1 %s %d:%d %s\n",
                item->name,
                line.i,
                column.i,
                pin_text(message).bytes
            );
            if (!item->accepted) {
                if (!item->stage_one.message) {
                    REQUIRE(item->constant);
                    fprintf(
                        stderr,
                        "FLOAT_DEPENDENCY_BLOCKED %s: typed-constant narrowing "
                        "category is pending; rejection is not policy proof\n",
                        item->name
                    );
                    agreed = false;
                } else if (
                    strcmp(pin_text(message).bytes, item->stage_one.message) ||
                    line.i != item->stage_one.line ||
                    column.i != item->stage_one.column
                ) {
                    fprintf(
                        stderr,
                        "FLOAT_REJECTION_MISMATCH stage1 %s expected %d:%d "
                        "%s\n",
                        item->name,
                        item->stage_one.line,
                        item->stage_one.column,
                        item->stage_one.message
                    );
                    agreed = false;
                }
            }
        }
        if (accepted != item->accepted) {
            fprintf(
                stderr,
                "stage-one floating admission %s: expected %s, got %s\n",
                item->name,
                item->accepted ? "accepted" : "rejected",
                accepted ? "accepted" : "rejected"
            );
            if (!accepted) {
                pin_value message;
                REQUIRE(get(&value->values[0], "message", &message));
                fprintf(stderr, "  %s\n", pin_text(message).bytes);
            }
            agreed = false;
        }
        if (accepted && item->accepted) {
            size_t length;
            unsigned char *bytes = closure_bytes(value, &length);
            arena_free(&detached);
            sample_call call = { .function = "Fold",
                                 .arguments = item->arguments,
                                 .expected = item->expected };
            bool ran = true;
            for (int backend = 0; backend < 2; backend++) {
                ran = run(bytes, length, &call, backend) && ran;
                compiler_host_free(host);
                host = NULL;
                free(result.json);
                result = (compiler_host_result){ 0 };
            }
            free(bytes);
            agreed = ran && agreed;
        }
        arena_free(&detached);
    }
    REQUIRE(agreed);
    return true;
}

/* The literal diagnostics also hold in the independent stage-zero frontend,
 * including reference and boxed targets and record fields. */
TEST_CASE(package_compiler_compound_diagnostics_match_stage_zero)
{
    REQUIRE(prepare());
    int checked = 0;
    for (int i = 0; i < (int)SAMPLES; i++) {
        const sample *s = &samples[i];
        if (!strncmp(s->name, "mixed compound ", strlen("mixed compound "))) {
            REQUIRE(s->message);
            REQUIRE(rejects_like_oracle(s, s->message, s->line, s->column));
            checked++;
        }
    }
    REQUIRE(checked == 7);
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
    REQUIRE(run_samples(IMPORT_SAMPLES, 0));
    return approved_float_conversion_admission(0, false);
}
TEST_CASE(package_compiler_builds_runnable_importing_packages_compiled)
{
    REQUIRE(run_samples(IMPORT_SAMPLES, 1));
    return approved_float_conversion_admission(1, false);
}
TEST_CASE(package_compiler_float_constant_dependency)
{
    REQUIRE(prepare());
    return approved_float_conversion_admission(0, true);
}
TEST_CASE(package_compiler_float_constant_dependency_compiled)
{
    REQUIRE(prepare());
    return approved_float_conversion_admission(1, true);
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
    pin_value args[ARGUMENTS + 1], out;
    REQUIRE(arguments_for(1, s->game, s->c_string_probe, inputs, count, args));
    args[ARGUMENTS] = linked_effects[s->game][s->c_string_probe][1];
    int argument_count = ARGUMENTS + s->host_effects;
    const char *entry_name = s->host_effects ? "CompileWithEffects" : "Compile";
    REQUIRE(
        compiler_fixture_call(&fixture, 1, PACKAGE, entry_name, args, argument_count, COMPILE_BUDGET, &detached, &out)
    );
    const pin_union *value = out.ptr;
    size_t length, expected_length;
    char *json = document_json(args, argument_count, &length);
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
                   script_catalog_id("package-symbol", s->host_effects ? "project:package_compiler:1:CompileClosureWithEffects:callable" : "project:package_compiler:1:CompileClosure:callable"),
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
        uint64_t entry = script_catalog_id(
            "package-symbol",
            s->host_effects
                ? "project:package_compiler:1:CompileWithEffects:callable"
                : "project:package_compiler:1:Compile:callable"
        );
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
/* The functions of the package compiler's module `p` that copy their
 * arguments, other than those that run once per compile, each reported. */
static int copying_functions(const execution_module_plan *p)
{
    const char *const once[] = { "Compile",
                                 "CompileClosure",
                                 "CompileWithEffects",
                                 "CompileClosureWithEffects",
                                 "CompileBehavior",
                                 "CompileBehaviors",
                                 "BehaviorRejection",
                                 "CollectScalar",
                                 "Declare",
                                 "DeclarePackage" };
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
    return copying;
}
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
        fprintf(stderr, "self-hosted compiler load: %s\n", error);
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
    /* Stage one's build of the compiler shares arguments as stage zero's
     * does: a function that copies them made stage two compile itself 20
     * times slower. */
    int copying = copying_functions(&compiler_host_root(host)->plan);
    bool seeded = same && write_seed(stage_two, length, json, json_length);
    free(again.bytes);
    free(stage_two);
    free(json);
    REQUIRE(same);
    REQUIRE(copying == 0);
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
static const char
    TEXT_STATE[] = "TextState :: behavior {\n"
                   "    label: string = \"seed\"\n"
                   "    Copy :: (value: string) -> string { return value; }\n"
                   "    on Tick :: () {\n"
                   "        if label == \"seed\" {\n"
                   "            label = Copy(\"a\\u0000b\")\n"
                   "            self.Spot.x = 1\n"
                   "        } else {\n"
                   "            label = Copy(\"seed\")\n"
                   "            self.Spot.x = 2\n"
                   "        }\n"
                   "    }\n}\n";
static const behavior_sample behavior_samples[] = {
    { "Counter", COUNTER },
    { "BoolState",
      "BoolState :: behavior {\n"
      "    active: bool = true\n"
      "    on Tick :: () {\n"
      "        if active { self.Spot.x = 1; } else { self.Spot.x = 2; }\n"
      "        active = !active\n"
      "    }\n}\n" },
    { "InferredState",
      "InferredState :: behavior {\n"
      "    active := false\n"
      "    count := 1\n"
      "    angle := 0.5\n"
      "    hex := 0x1e\n"
      "    wide := 4294967297\n"
      "    on Tick :: () {\n"
      "        active = !active\n"
      "        count += 1\n"
      "        angle += 0.5\n"
      "        if active { self.Spot.x = count; } else { self.Spot.x = "
      "float(angle); "
      "}\n"
      "    }\n}\n" },
    { "EntityState",
      "EntityState :: behavior {\n"
      "    target: entity\n"
      "    on Tick :: () {\n"
      "        target = self\n"
      "    }\n}\n" },
    { "EntityNullState",
      "EntityNullState :: behavior {\n"
      "    target: entity = null\n"
      "    on Tick :: () { target = self; }\n"
      "}\n" },
    { "EntityZeroState",
      "EntityZeroState :: behavior {\n"
      "    target: entity = 0\n"
      "    on Tick :: () { target = self; }\n"
      "}\n" },
    { "EntitySignedZeroState",
      "EntitySignedZeroState :: behavior {\n"
      "    negative: entity = -0\n"
      "    positive: entity = +0.0\n"
      "    on Tick :: () {\n"
      "        negative = self\n"
      "        positive = self\n"
      "        negative = -0.0\n"
      "        positive = +0\n"
      "    }\n}\n" },
    { "EntityResetState",
      "EntityResetState :: behavior {\n"
      "    target: entity\n"
      "    ticks: int = 0\n"
      "    on Tick :: () {\n"
      "        if ticks == 0 { target = self; }\n"
      "        if ticks == 1 { target = null; }\n"
      "        if ticks == 2 { target = self; }\n"
      "        if ticks == 3 { target = 0; }\n"
      "        ticks += 1\n"
      "    }\n}\n" },
    { "EntityEqualState",
      "EntityEqualState :: behavior {\n"
      "    target: entity\n"
      "    on Tick :: () {\n"
      "        target = self\n"
      "        if target == self { self.Spot.x += 1; }\n"
      "        if target != self { self.Spot.y += 1; }\n"
      "    }\n}\n" },
    { "NamedSpot",
      "NamedSpot :: behavior {\n"
      "    Ready :: () -> (Result: bool = false) { return self.Spot.w > 0; }\n"
      "    Read :: () -> (Point: Spot) { Point.x = 3; Point.y = 4; return; }\n"
      "    Bare :: () -> (Point: Spot) { return; }\n"
      "    Field :: (value: Spot) -> float { return value.x; }\n"
      "    Has :: (value: Spot) -> bool { return value.resolved == self; }\n"
      "    on Tick :: () { if Ready() { self.Spot = Read().Point; } }\n"
      "}\n" },
    { "TextState", TEXT_STATE },
    { "InferredTextState",
      "InferredTextState :: behavior {\n"
      "    label := \"a\\u0000b\"\n"
      "    on Tick :: () {\n"
      "        if label == \"a\\u0000b\" { self.Spot.x += 1; }\n"
      "    }\n}\n" },
    { "ScalarNullState",
      "ScalarNullState :: behavior {\n"
      "    active: bool = null\n"
      "    count: int = null\n"
      "    label: string = null\n"
      "    on Tick :: () {\n"
      "        if !active && label == \"\" { self.Spot.x = count; }\n"
      "    }\n}\n" },
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
    { "KeyedEntityState",
      "KeyedEntityState :: behavior {\n"
      "    target: entity\n"
      "    ticks: int = 0\n"
      "    on Tick :: () {\n"
      "        if ticks == 0 { target = globals.Terrain; }\n"
      "        if ticks == 1 { target = null; }\n"
      "        if ticks == 2 { target = globals.Terrain; }\n"
      "        if ticks == 3 { target = 0; }\n"
      "        if target == self { self.Spot.x += 1; }\n"
      "        if target != self { self.Spot.y += 1; }\n"
      "        ticks += 1\n"
      "    }\n}\n" },
    { "FieldEntityState",
      "FieldEntityState :: behavior {\n"
      "    target: entity\n"
      "    ticks: int = 0\n"
      "    on Tick :: () {\n"
      "        if ticks == 0 { target = self.Spot.owner; }\n"
      "        if ticks == 1 { target = self.Spot.resolved; }\n"
      "        if ticks == 2 { target = null; }\n"
      "        if ticks == 3 { target = 0; }\n"
      "        if target == self { self.Spot.x += 1; }\n"
      "        if target != self { self.Spot.y += 1; }\n"
      "        ticks += 1\n"
      "    }\n}\n" },
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
    { "Missing :: behavior { flag: bool; }", "'flag' requires a default value", true },
    { "Missing :: behavior { count: int; }", "'count' requires a default value", true },
    { "Missing :: behavior { label: string; }", "'label' requires a default value", true },
    { "Nonzero :: behavior { target: entity = 1; }",
      "entity default requires null or zero",
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
    OTHER_HANDLE = 701,
    SCENE_GLOBALS = 702,
    GET_TERRAIN = 800,
    GET_OTHER = 801,
    GET_RESOLVED = 802,
    BEHAVIOR_ARGUMENTS = 17,
    TICKS = 5
};
typedef struct
{
    float x, y, w;
    uint64_t owner;
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
    { .name = "owner",
      .type = PIN_HANDLE,
      .type_name = "ecs.entity",
      .offset = offsetof(spot, owner),
      .size = sizeof(uint64_t) },
};
static const type_desc spot_type = { .name = "Spot",
                                     .size = sizeof(spot),
                                     .fields = spot_fields,
                                     .field_count = sizeof spot_fields /
                                                    sizeof *spot_fields };
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
    { .id = 814,
      .name = "owner",
      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
      .offset = offsetof(spot, owner) },
    { .id = 815,
      .name = "resolved",
      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
      .read = GET_RESOLVED },
};
static const script_catalog_parameter delta_seconds = { .name = "DeltaSeconds",
                                                        .type = { .storage = PIN_F32 } };
static const script_catalog_parameter move_payload[] = {
    { .name = "x", .type = { .storage = PIN_F32 } },
    { .name = "y", .type = { .storage = PIN_F32 } },
};
static const script_catalog_parameter global_key = { .name = "key",
                                                     .type = { .storage = PIN_STR } };
static const script_catalog_parameter spot_argument = {
    .name = "spot",
    .type = { .storage = PIN_STRUCT, .nominal = SPOT }
};
static const script_catalog_parameter terrain_result = {
    .name = "Value",
    .type = { .storage = PIN_HANDLE, .nominal = ENTITY }
};
static const script_catalog_parameter other_result = {
    .name = "Value",
    .type = { .storage = PIN_HANDLE, .nominal = OTHER_HANDLE }
};
static const script_catalog_capability game_capabilities[] = {
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
    { .id = GET_TERRAIN,
      .name = "Fixture Terrain",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &global_key,
      .parameter_count = 1,
      .results = &terrain_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_READ,
      .completion_outcome = -1 },
    { .id = GET_OTHER,
      .name = "Fixture Other",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &global_key,
      .parameter_count = 1,
      .results = &other_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_READ,
      .completion_outcome = -1 },
    { .id = GET_RESOLVED,
      .name = "Fixture Resolved",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &spot_argument,
      .parameter_count = 1,
      .results = &terrain_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_READ,
      .completion_outcome = -1 },
};
static const script_catalog_field scene_globals[] = {
    { .id = 820,
      .name = "Terrain",
      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
      .read = GET_TERRAIN,
      .read_key = "Terrain" },
    { .id = 821,
      .name = "Other",
      .type = { .storage = PIN_HANDLE, .nominal = OTHER_HANDLE },
      .read = GET_OTHER,
      .read_key = "Other" },
};
static const script_catalog_global game_globals[] = {
    { .id = OWNER, .name = "self", .type = { .storage = PIN_HANDLE, .nominal = ENTITY } },
    { .id = OWNER + 1, .name = "pace", .type = { .storage = PIN_F32 } },
    { .id = OWNER + 2,
      .name = "globals",
      .type = { .storage = PIN_FLOW, .nominal = SCENE_GLOBALS, .flags = SCRIPT_TYPE_NAMESPACE } },
};
/* The game: Spot, two nominal handle types, and keyed scene globals, with
 * Tick, Move, `self`, and the unsupported scalar global `pace`. */
static script_catalog game_catalog()
{
    static script_catalog_type types[4];
    types[0] = (script_catalog_type){
        .id = SPOT,
        .name = spot_type.name,
        .type = { .storage = PIN_STRUCT, .nominal = SPOT },
        .fields = spot_members,
        .field_count = sizeof spot_members / sizeof *spot_members,
        .size = spot_type.size,
        .layout = type_layout_hash(&spot_type),
        .component_id = SPOT_COMPONENT,
        .component = true
    };
    types[1] = (script_catalog_type){ .id = ENTITY,
                                      .name = "ecs.entity",
                                      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
                                      .size = sizeof(uint64_t) };
    types[2] = (script_catalog_type){ .id = OTHER_HANDLE,
                                      .name = "test.other",
                                      .type = { .storage = PIN_HANDLE,
                                                .nominal = OTHER_HANDLE },
                                      .size = sizeof(uint64_t) };
    types[3] = (script_catalog_type){ .id = SCENE_GLOBALS,
                                      .name = "SceneGlobals",
                                      .type = { .storage = PIN_FLOW,
                                                .nominal = SCENE_GLOBALS,
                                                .flags = SCRIPT_TYPE_NAMESPACE },
                                      .fields = scene_globals,
                                      .field_count = 2 };
    return (script_catalog){ .types = types,
                             .type_count = 4,
                             .capabilities = game_capabilities,
                             .capability_count = sizeof game_capabilities /
                                                 sizeof *game_capabilities,
                             .globals = game_globals,
                             .global_count = sizeof game_globals / sizeof *game_globals,
                             .entity_type = ENTITY,
                             .tick_event = TICK };
}
/* The getters preserve the caller's typed result header. Equal payload bits
 * for the two handle types must never make them assignment-compatible. */
static const char *keyed_owner(
    const behavior_function_context *context,
    const pin_value *arguments,
    pin_value *out,
    const char *name,
    const char *type_name
)
{
    pin_string key = pin_text(arguments[0]);
    if (key.length != strlen(name) || memcmp(key.bytes, name, key.length)) {
        return "Unexpected keyed global name";
    }
    if (out->type != PIN_HANDLE || out->container != PIN_CONTAINER_SINGLE ||
        !out->type_name || strcmp(out->type_name, type_name)) {
        return "Keyed global result lost its nominal type";
    }
    out->u64 = context->owner;
    return NULL;
}
static const char *terrain_global(
    const behavior_function_context *context,
    void *,
    const pin_value *arguments,
    pin_value *out
)
{
    return keyed_owner(context, arguments, out, "Terrain", "ecs.entity");
}
static const char *other_global(
    const behavior_function_context *context,
    void *,
    const pin_value *arguments,
    pin_value *out
)
{
    return keyed_owner(context, arguments, out, "Other", "test.other");
}
static const char *resolved_owner(
    const behavior_function_context *,
    void *,
    const pin_value *arguments,
    pin_value *out
)
{
    const pin_value *receiver = &arguments[0];
    if (receiver->type != PIN_STRUCT ||
        receiver->container != PIN_CONTAINER_SINGLE || !receiver->ptr ||
        !receiver->type_name || strcmp(receiver->type_name, "Spot")) {
        return "Resolved getter requires a Spot";
    }
    if (out->type != PIN_HANDLE || out->container != PIN_CONTAINER_SINGLE ||
        !out->type_name || strcmp(out->type_name, "ecs.entity")) {
        return "Resolved getter result lost its nominal type";
    }
    out->u64 = ((const spot *)receiver->ptr)->owner;
    return NULL;
}
static const node_host_call terrain_call = { .context = terrain_global };
static const node_host_call other_call = { .context = other_global };
static const node_host_call resolved_call = { .context = resolved_owner };
static const node_access global_access = { .flags = NODE_EFFECT_PURE };
static const node_host_call *global_call(uint64_t id)
{
    return id == GET_TERRAIN  ? &terrain_call
         : id == GET_OTHER    ? &other_call
         : id == GET_RESOLVED ? &resolved_call
                              : NULL;
}
static bool game_host(void *, uint64_t id, script_lower_host *out)
{
    const node_host_call *call = global_call(id);
    if (!call) {
        return false;
    }
    *out = (script_lower_host){ .call = call, .access = &global_access };
    return true;
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
    const script_lower_bindings bindings = { .global = game_global, .host = game_host };
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
/* Behavior targets append these fixture getters after the production host
 * natives. Package compilation and bootstrap keep their original tables. */
static bool behavior_natives(int b, pin_value tables[3])
{
    script_catalog catalog = game_catalog();
    pin_value *functions = NULL, *ports = NULL, *identities = NULL;
    pin_array *before[] = { tables[0].ptr, tables[1].ptr, tables[2].ptr };
    for (int i = 0; i < before[0]->count; i++) {
        apush(functions, pin_array_get(before[0], i));
    }
    for (int i = 0; i < before[1]->count; i++) {
        apush(ports, pin_array_get(before[1], i));
    }
    for (int i = 0; i < before[2]->count; i++) {
        apush(identities, pin_array_get(before[2], i));
    }
    for (int i = 0; i < catalog.capability_count; i++) {
        const script_catalog_capability *c = &catalog.capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_FUNCTION) {
            continue;
        }
        pin_value function = row(b, "Native");
        REQUIRE(put_value(&function, "name", text(c->name)));
        REQUIRE(put(&function, "first", acount(ports)));
        REQUIRE(put(&function, "parameters", c->parameter_count));
        REQUIRE(put(&function, "results", c->result_count));
        REQUIRE(put_value(&function, "variadic", flag(false)));
        apush(functions, function);
        for (int j = 0; j < c->parameter_count + c->result_count; j++) {
            const script_catalog_parameter *declared = j < c->parameter_count
                                                         ? &c->parameters[j]
                                                         : &c->results[j - c->parameter_count];
            pin_value port = row(b, "Port");
            REQUIRE(put_value(&port, "name", text(declared->name)));
            REQUIRE(put(&port, "storage", port_storage(declared->type)));
            REQUIRE(put(&port, "container", (int)declared->type.container));
            REQUIRE(put_value(&port, "c_string", flag(false)));
            apush(ports, port);
        }
        apush(identities, u64(c->id));
        apush(identities, u64(script_catalog_capability_abi(&catalog, c)));
    }
    tables[0] = rows(functions, acount(functions), PIN_STRUCT, tables[0].record);
    tables[1] = rows(ports, acount(ports), PIN_STRUCT, tables[1].record);
    tables[2] = rows(identities, acount(identities), PIN_U64, NULL);
    afree(functions);
    afree(ports);
    afree(identities);
    return true;
}
static int game_native_index(const script_catalog *catalog, uint64_t id, int first)
{
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_FUNCTION) {
            continue;
        }
        if (c->id == id) {
            return first;
        }
        first++;
    }
    return -1;
}
/* CompileBehavior's game arguments, as the game target exports them: each
 * event and its payload fields, each type and its fields, the entity type,
 * no operators that combine records, the globals, and no record makes. */
static bool game_arguments(int b, int first_native, pin_value *out)
{
    script_catalog catalog = game_catalog();
    pin_value *declared = NULL, *payloads = NULL, *types = NULL, *fields = NULL;
    for (int i = 0; i < catalog.capability_count; i++) {
        const script_catalog_capability *c = &catalog.capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_EVENT) {
            continue;
        }
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
        REQUIRE(put_value(&type, "required", flag(!(t->type.flags & SCRIPT_TYPE_NAMESPACE))));
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
            REQUIRE(
                put(&field, "read", game_native_index(&catalog, t->fields[j].read, first_native))
            );
            REQUIRE(put_value(
                &field,
                "read_key",
                text(t->fields[j].read_key ? t->fields[j].read_key : "")
            ));
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
    int first_native = ((pin_array *)args[5].ptr)->count;
    REQUIRE(behavior_natives(b, &args[5]));
    REQUIRE(game_arguments(b, first_native, &args[8]));
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
static bool link_game_host(void *user, uint64_t id, execution_prepared_host *out)
{
    const node_host_call *call = global_call(id);
    const script_catalog *catalog = user;
    const script_catalog_capability *capability = script_catalog_capability_by_id(catalog, id);
    if (!call || !capability) {
        return false;
    }
    *out = (execution_prepared_host){
        .call = call,
        .access = &global_access,
        .abi = script_catalog_capability_abi(catalog, capability)
    };
    return true;
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
                                         .host = link_game_host,
                                         .type = link_game_type,
                                         .event = link_game_event };
    char error[256] = { 0 };
    *module = execution_prepared_read(
        bytes,
        length,
        &linker,
        b ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
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
    spot initial;
    memset(&initial, 0, sizeof initial);
    initial.w = 9;
    initial.owner = ecs_entity_to_u64(out->owner);
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
        REQUIRE(pin_value_equal(&a->states[i].initial, &e->states[i].initial));
        REQUIRE(!strcmp(
            a->states[i].initial.type_name ? a->states[i].initial.type_name : "",
            e->states[i].initial.type_name ? e->states[i].initial.type_name : ""
        ));
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
        if (y->kind != x->kind || y->effects != x->effects) {
            fprintf(
                stderr,
                "function %d (%s): native kind/effects %d/%u, authored %d/%u\n",
                i,
                x->entry ? x->entry : "handler",
                x->kind,
                (unsigned)x->effects,
                y->kind,
                (unsigned)y->effects
            );
        }
        REQUIRE(y->kind == x->kind && y->effects == x->effects);
        REQUIRE(!y->event_name == !x->event_name && !y->entry == !x->entry);
        REQUIRE(!x->event_name || !strcmp(y->event_name, x->event_name));
        REQUIRE(y->body->argument_count == x->body->argument_count);
        REQUIRE(y->body->result_count == x->body->result_count);
        for (int j = 0; j < x->body->result_count; j++) {
            const pin_value *wanted = &x->body->result_defaults[j];
            const pin_value *got = &y->body->result_defaults[j];
            REQUIRE(got->type == wanted->type && got->container == wanted->container);
            REQUIRE(!strcmp(
                got->type_name ? got->type_name : "",
                wanted->type_name ? wanted->type_name : ""
            ));
            REQUIRE(!got->record == !wanted->record);
            REQUIRE(!got->record || pin_record_same(got->record, wanted->record));
        }
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
            alike = kept && held && pin_value_equal(kept, held);
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
 * every argument on each call, and so does every function that calls it,
 * slowing stage one compiling itself. */
TEST_CASE(package_compiler_shares_its_arguments)
{
    REQUIRE(prepare());
    REQUIRE(
        copying_functions(
            &compiler_fixture_package(&fixture, 1, PACKAGE)->module->plan
        ) == 0
    );
    return true;
}
/* The behavior samples from `first` before `last` compile alike on both
 * backends. Stage one compiles slowest on the bytecode backend, so each
 * selector takes half of them. */
static bool samples_alike(size_t first, size_t last)
{
    const type_desc *const reflected[] = { &spot_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    publish_project(&pack);
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        for (size_t i = first; passed && i < last; i++) {
            passed = behaves_alike(b, &behavior_samples[i]);
        }
    }
    publish_project(NULL);
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
    publish_project(&pack);
    for (size_t i = 0;
         passed && i < sizeof behavior_rejections / sizeof *behavior_rejections;
         i++) {
        passed = rejects(1, &behavior_rejections[i]);
    }
    const char *const invalid_entity_defaults[] = {
        "Invalid :: behavior { target: entity = ~0; }",
        "Invalid :: behavior { target: entity = !0; }",
    };
    for (size_t i = 0; passed && i < sizeof invalid_entity_defaults /
                                         sizeof *invalid_entity_defaults;
         i++) {
        REQUIRE(stage_zero_diagnostic(invalid_entity_defaults[i]).message[0]);
        for (int b = 0; b < 2; b++) {
            pin_value out;
            REQUIRE(compile_behavior(b, "Rejected", invalid_entity_defaults[i], &out));
            REQUIRE(((pin_union *)out.ptr)->tag == tag(b, "Rejected"));
            arena_free(&detached);
        }
    }
    const behavior_rejection invalid_keyed_handles[] = {
        { "Invalid :: behavior { target: entity; on Tick :: () { "
          "target = globals.Other; } }",
          "assigned value differs from its local",
          false },
        { "Invalid :: behavior { on Tick :: () { "
          "if self == globals.Other { self.Spot.x = 1; } } }",
          "operator does not apply to these operands",
          false },
    };
    for (size_t i = 0;
         passed && i < sizeof invalid_keyed_handles / sizeof *invalid_keyed_handles;
         i++) {
        REQUIRE(stage_zero_diagnostic(invalid_keyed_handles[i].source).message[0]);
        for (int b = 0; b < 2; b++) {
            REQUIRE(rejects(b, &invalid_keyed_handles[i]));
        }
    }
    passed = passed && takes_project_identities(1);
    publish_project(NULL);
    REQUIRE(passed);
    return true;
}
/* A temporary string returned by a member call becomes instance-owned state.
 * Updating one owner must leave another owner's state and bytes independent. */
TEST_CASE(package_compiler_keeps_text_state_per_owner)
{
    REQUIRE(prepare());
    const type_desc *const reflected[] = { &spot_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    publish_project(&pack);
    execution_module *module = NULL;
    REQUIRE(stage_one_behavior(1, "TextState", TEXT_STATE, &module));
    player p = { 0 };
    REQUIRE(play(module, &p));
    ecs_entity other = ecs_world_create_entity(p.world);
    spot initial = { .w = 9 };
    REQUIRE(ecs_world_record_add(p.world, other, SPOT_COMPONENT, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(p.world) == ECS_OK);
    execution_instance *second = execution_program_attach(p.program, ecs_entity_to_u64(other));
    REQUIRE(second);
    uint64_t state = script_catalog_id("symbol", "TextState:1:label");
    pin_value delta = { .type = PIN_F32, .f = 1.0f / 60.0f };
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int first = execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, 0, 1000);
    behavior_run_scope_end(&runs);
    REQUIRE(first == FLOW_OK);
    const pin_value *first_value = execution_instance_state(p.instance, state);
    const pin_value *second_value = execution_instance_state(second, state);
    REQUIRE(first_value && second_value);
    REQUIRE(pin_text_equal(pin_text(*first_value), (pin_string){ "a\0b", 3 }));
    REQUIRE(pin_text_equal(pin_text(*second_value), (pin_string){ "seed", 4 }));
    behavior_run_scope_begin(&runs);
    int repeated = execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, 0, 1000);
    int other_result = execution_instance_dispatch(second, TICK, &delta, 1, 0, 0, 1000);
    behavior_run_scope_end(&runs);
    REQUIRE(repeated == FLOW_OK && other_result == FLOW_OK);
    first_value = execution_instance_state(p.instance, state);
    second_value = execution_instance_state(second, state);
    REQUIRE(first_value && second_value);
    REQUIRE(pin_text_equal(pin_text(*first_value), (pin_string){ "seed", 4 }));
    REQUIRE(pin_text_equal(pin_text(*second_value), (pin_string){ "a\0b", 3 }));
    REQUIRE(
        TEST_COMPILED_BACKEND == BEHAVIOR_WASM ? runs.counts.wasm_entries > 0
                                               : runs.counts.sljit_entries > 0
    );
    REQUIRE(!runs.counts.overflow && !runs.counts.bytecode_entries);
    stop(&p);
    execution_module_free(module);
    publish_project(NULL);
    return true;
}
/* A row field of CollectScalar's retained result, read through the record
 * shape the result owns. */
static pin_value scalar_field(const pin_value *row, const char *name)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    assert(field);
    pin_value value;
    assert(!pin_read_field(&detached, row->ptr, field, &value));
    return value;
}
static bool retained_text(const pin_value *row, const char *name, const char *expected)
{
    pin_value value = scalar_field(row, name);
    REQUIRE(value.type == PIN_STR && arena_contains(&detached, pin_text(value).bytes));
    REQUIRE(pin_text_equal(pin_text(value), (pin_string){ expected, strlen(expected) }));
    return true;
}
static const char
    SCALAR_SOURCE[] = "package scalar\n"
                      "Main :: () -> int { return Pick(7, true); }\n"
                      "Pick :: (x: int, b: bool) -> int { return x; }\n";
/* A guarded module as CollectScalar gives it: Pick's guard compares its
 * parameters and its arm calls Id. */
static const char GUARD_SOURCE[] = "package scalar\n"
                                   "Main :: () -> int { return Pick(1, 2); }\n"
                                   "Pick :: (a: int, b: int) -> int {\n"
                                   "    if (a < b) { return Id(a); }\n"
                                   "    return b;\n"
                                   "}\n"
                                   "Id :: (x: int) -> int { return x; }\n";
/* Calc(3, 4) is -(3 + 4) * ~4, which is 35. */
static const char ARITHMETIC_SOURCE
    [] = "package scalar\n"
         "Main :: () -> int { return Calc(3, 4); }\n"
         "Calc :: (a: int, b: int) -> int { return -(a + b) * ~b; }\n";
/* scalar_facts row kinds and types. */
enum
{
    SCALAR_SOURCE_ROW = 0,
    SCALAR_REJECTION_ROW = 1,
    SCALAR_DOCUMENT_ROW = 2,
    SCALAR_FUNCTION_ROW = 3,
    SCALAR_PARAMETER_ROW = 4,
    SCALAR_RETURN_ROW = 5,
    SCALAR_LITERAL_ROW = 6,
    SCALAR_READ_ROW = 7,
    SCALAR_CALL_ROW = 8,
    SCALAR_GUARD_ROW = 9,
    SCALAR_COMPARE_ROW = 10,
    SCALAR_ARITHMETIC_ROW = 11,
    SCALAR_UNARY_ROW = 12,
    SCALAR_INT = 1,
    SCALAR_BOOL = 2
};
/* The kind of each of GUARD_SOURCE's rows, and the rows edited. */
static const int GUARD_KINDS[] = {
    SCALAR_SOURCE_ROW,    SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,  SCALAR_LITERAL_ROW,
    SCALAR_CALL_ROW,      SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW,
    SCALAR_PARAMETER_ROW, SCALAR_GUARD_ROW,    SCALAR_READ_ROW,
    SCALAR_READ_ROW,      SCALAR_COMPARE_ROW,  SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,      SCALAR_CALL_ROW,     SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,      SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW,
    SCALAR_RETURN_ROW,    SCALAR_READ_ROW,
};
enum
{
    GUARD_ROW = 10,
    GUARD_CONDITION = 11,
    GUARD_COMPARE = 13,
    GUARD_ARM = 14,
    GUARD_ARM_READ = 15,
    GUARD_FINAL = 17,
    GUARD_ID_READ = 22,
    GUARD_ROWS = (int)(sizeof GUARD_KINDS / sizeof *GUARD_KINDS),
    GUARD_EDITS = 5
};
/* One field of one row set to `value`, to `text` when it is set, or to the
 * same field of row `from` when it is set. */
typedef struct row_edit
{
    int row;
    const char *field;
    int value;
    const char *text;
    int from;
} row_edit;
typedef struct malformed_guard
{
    row_edit edits[GUARD_EDITS];
    const char *message;
} malformed_guard;
#define EDIT(r, f, v) { .row = (r), .field = (f), .value = (v), .from = -1 }
#define COPY(r, f, source) { .row = (r), .field = (f), .from = (source) }
static const char LAYOUT[] = "invalid scalar module layout", COMPARISON[] = "scalar comparison differs from its int operands",
                  SEGMENT[] = "invalid scalar return row";
/* The kind of each of ARITHMETIC_SOURCE's rows, and the rows edited. */
static const int ARITHMETIC_KINDS[] = {
    SCALAR_SOURCE_ROW,    SCALAR_DOCUMENT_ROW,   SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,    SCALAR_LITERAL_ROW,
    SCALAR_CALL_ROW,      SCALAR_FUNCTION_ROW,   SCALAR_PARAMETER_ROW,
    SCALAR_PARAMETER_ROW, SCALAR_RETURN_ROW,     SCALAR_READ_ROW,
    SCALAR_READ_ROW,      SCALAR_ARITHMETIC_ROW, SCALAR_UNARY_ROW,
    SCALAR_READ_ROW,      SCALAR_UNARY_ROW,      SCALAR_ARITHMETIC_ROW,
};
enum
{
    ARITHMETIC_SECOND_READ = 12,
    ARITHMETIC_ADD = 13,
    ARITHMETIC_NEGATE = 14,
    ARITHMETIC_LAST_READ = 15,
    ARITHMETIC_ROWS = (int)(sizeof ARITHMETIC_KINDS / sizeof *ARITHMETIC_KINDS)
};
static const malformed_guard MALFORMED_GUARDS[] = {
    /* A guard with no condition, and a second guard past the bound. */
    { { EDIT(GUARD_ROW, "count", 0) }, LAYOUT },
    { { EDIT(GUARD_FINAL, "kind", SCALAR_GUARD_ROW) }, LAYOUT },
    /* A comparison's operator, arity and result. */
    { { EDIT(GUARD_COMPARE, "value", 0) }, COMPARISON },
    { { EDIT(GUARD_COMPARE, "value", 7) }, COMPARISON },
    { { EDIT(GUARD_COMPARE, "count", 1) }, COMPARISON },
    { { EDIT(GUARD_COMPARE, "type", SCALAR_INT) }, COMPARISON },
    /* A comparison of a bool. */
    { { EDIT(GUARD_CONDITION + 1, "kind", SCALAR_LITERAL_ROW),
        EDIT(GUARD_CONDITION + 1, "type", SCALAR_BOOL),
        EDIT(GUARD_CONDITION + 1, "value", 1) },
      COMPARISON },
    /* A condition that leaves three ints rather than one bool: the
     * comparison becomes a read of b. */
    { { EDIT(GUARD_COMPARE, "kind", SCALAR_READ_ROW),
        EDIT(GUARD_COMPARE, "type", SCALAR_INT),
        EDIT(GUARD_COMPARE, "count", 0) },
      "scalar condition is not one bool" },
    /* A guard row that carries a type, an arm that ends past its guard, a
     * condition operand inside the arm and a final return that starts inside
     * the guard. */
    { { EDIT(GUARD_ROW, "type", SCALAR_INT) }, SEGMENT },
    { { COPY(GUARD_ARM, "end", GUARD_FINAL) }, SEGMENT },
    { { COPY(GUARD_CONDITION, "start", GUARD_ARM_READ),
        COPY(GUARD_CONDITION, "end", GUARD_ARM_READ),
        COPY(GUARD_CONDITION, "line", GUARD_ARM_READ),
        COPY(GUARD_CONDITION, "column", GUARD_ARM_READ) },
      "invalid scalar operand row" },
    { { COPY(GUARD_FINAL, "start", GUARD_ROW),
        COPY(GUARD_FINAL, "line", GUARD_ROW),
        COPY(GUARD_FINAL, "column", GUARD_ROW) },
      SEGMENT },
    /* Id calls Main, so the arm's call closes a cycle. */
    { { EDIT(GUARD_ID_READ, "kind", SCALAR_CALL_ROW),
        EDIT(GUARD_ID_READ, "value", 0),
        { .row = GUARD_ID_READ, .field = "name", .text = "Main", .from = -1 } },
      "scalar module calls are recursive" },
};
/* Arithmetic's operator, arity and result, and arithmetic on a bool. */
static const char
    ARITHMETIC[] = "scalar arithmetic differs from its int operands";
static const malformed_guard MALFORMED_ARITHMETIC[] = {
    { { EDIT(ARITHMETIC_ADD, "value", 0) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_ADD, "value", 9) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_ADD, "count", 1) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_ADD, "type", SCALAR_BOOL) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_NEGATE, "value", 0) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_NEGATE, "value", 3) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_NEGATE, "count", 2) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_SECOND_READ, "kind", SCALAR_LITERAL_ROW),
        EDIT(ARITHMETIC_SECOND_READ, "type", SCALAR_BOOL),
        EDIT(ARITHMETIC_SECOND_READ, "value", 1) },
      ARITHMETIC },
    { { EDIT(ARITHMETIC_LAST_READ, "kind", SCALAR_LITERAL_ROW),
        EDIT(ARITHMETIC_LAST_READ, "type", SCALAR_BOOL),
        EDIT(ARITHMETIC_LAST_READ, "value", 0) },
      ARITHMETIC },
};
#undef EDIT
#undef COPY
static pin_value *guard_row(const pin_value *rows, int index)
{
    return &((const pin_array *)rows->ptr)->items[index];
}
static void write_row(const pin_value *rows, int index, const char *name, pin_value value)
{
    pin_value *row = guard_row(rows, index);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    assert(field);
    assert(!pin_write_field(&detached, row->ptr, field, value));
}
static bool validated(int b, pin_value *rows, const char *message)
{
    pin_value error, emitted;
    REQUIRE(
        compiler_fixture_call(&consumer, b, "project:scalar_llvm", "Validate", rows, 1, COMPILE_BUDGET, &input, &error)
    );
    REQUIRE(
        compiler_fixture_call(&consumer, b, "project:scalar_llvm", "EmitBytes", rows, 1, COMPILE_BUDGET, &input, &emitted)
    );
    bool valid = pin_text_equal(pin_text(error), (pin_string){ message, strlen(message) });
    if (!valid) {
        fprintf(
            stderr,
            "scalar_llvm Validate gave \"%.*s\", not \"%s\"\n",
            (int)pin_text(error).length,
            pin_text(error).bytes,
            message
        );
    }
    REQUIRE(valid && !((const pin_array *)emitted.ptr)->count == !!*message);
    return true;
}
/* Each edit applied to the producer's rows, the message Validate gives, and
 * the rows restored. */
static bool rejects_guard(int b, pin_value *rows, const malformed_guard *malformed)
{
    pin_value saved[GUARD_EDITS];
    int count = 0;
    while (count < GUARD_EDITS && malformed->edits[count].field) {
        const row_edit *edit = &malformed->edits[count];
        saved[count++] = scalar_field(guard_row(rows, edit->row), edit->field);
    }
    for (int i = 0; i < count; i++) {
        const row_edit *edit = &malformed->edits[i];
        pin_value value = { .type = PIN_I32, .i = edit->value };
        if (edit->text) {
            value = text(edit->text);
        }
        if (edit->from >= 0) {
            value = scalar_field(guard_row(rows, edit->from), edit->field);
        }
        write_row(rows, edit->row, edit->field, value);
    }
    REQUIRE(validated(b, rows, malformed->message));
    for (int i = count - 1; i >= 0; i--) {
        write_row(rows, malformed->edits[i].row, malformed->edits[i].field, saved[i]);
    }
    return true;
}
/* scalar_llvm accepts the producer's rows, of the `kinds` it gives, rejects
 * each malformed variant of them when its layout, operators, operand types,
 * ranges or calls are not what a producer gives, and emits nothing for it. */
static bool rejects_malformed(
    int b,
    pin_value *rows,
    const int *kinds,
    int count,
    const malformed_guard *malformed,
    int variants
)
{
    REQUIRE(((const pin_array *)rows->ptr)->count == count);
    for (int i = 0; i < count; i++) {
        REQUIRE(scalar_field(guard_row(rows, i), "kind").i == kinds[i]);
    }
    REQUIRE(validated(b, rows, ""));
    for (int i = 0; i < variants; i++) {
        REQUIRE(rejects_guard(b, rows, &malformed[i]));
    }
    REQUIRE(validated(b, rows, ""));
    return true;
}
/* CollectScalar's rows own their shape and strings: they outlive the
 * producer's packages and inputs, and a consumer prepared afterwards emits
 * them as LLVM IR that keeps the call, and validates guarded and arithmetic
 * rows. */
TEST_CASE(package_compiler_scalar_rows_outlive_the_producer)
{
    REQUIRE(prepare());
    pin_value produced[2], guarded[2], computed[2];
    for (int b = 0; b < 2; b++) {
        pin_value args[] = { text("/project"),
                             text("Scalar.chibi"),
                             text(SCALAR_SOURCE),
                             { 0 },
                             instruction_names() };
        REQUIRE(target(b, &args[3]));
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "CollectScalar", args, 5, COMPILE_BUDGET, &detached, &produced[b])
        );
        args[2] = text(GUARD_SOURCE);
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "CollectScalar", args, 5, COMPILE_BUDGET, &detached, &guarded[b])
        );
        args[2] = text(ARITHMETIC_SOURCE);
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "CollectScalar", args, 5, COMPILE_BUDGET, &detached, &computed[b])
        );
    }
    compiler_fixture_free(&fixture);
    arena_free(&input);
    const source_package_input sources[] = { SCALAR_CONSUMER_SOURCE_INPUTS };
    REQUIRE(compiler_fixture_prepare(&consumer, sources, (int)(sizeof sources / sizeof *sources)));
    for (int b = 0; b < 2; b++) {
        const pin_array *rows = produced[b].ptr;
        REQUIRE(produced[b].container == PIN_CONTAINER_ARRAY && arena_contains(&detached, rows));
        REQUIRE(rows->count == 12 && arena_contains(&detached, rows->record));
        REQUIRE(retained_text(&rows->items[0], "name", "Scalar.chibi"));
        REQUIRE(retained_text(&rows->items[0], "text", SCALAR_SOURCE));
        REQUIRE(retained_text(&rows->items[1], "name", "project:Scalar.chibi"));
        REQUIRE(retained_text(&rows->items[2], "name", "Main"));
        REQUIRE(retained_text(&rows->items[6], "name", "Pick"));
        REQUIRE(scalar_field(&rows->items[6], "value").i == 1);
        REQUIRE(scalar_field(&rows->items[6], "count").i == 2);
        REQUIRE(retained_text(&rows->items[7], "name", "Pick"));
        pin_value emitted;
        REQUIRE(compiler_fixture_call(
            &consumer,
            b,
            "project:scalar_llvm",
            "EmitBytes",
            &produced[b],
            1,
            COMPILE_BUDGET,
            &input,
            &emitted
        ));
        const pin_array *bytes = emitted.ptr;
        REQUIRE(emitted.container == PIN_CONTAINER_ARRAY && bytes->count > 0);
        char *ir = xmalloc((size_t)bytes->count + 1);
        for (int i = 0; i < bytes->count; i++) {
            ir[i] = (char)bytes->items[i].u8;
        }
        ir[bytes->count] = 0;
        bool called = strstr(ir, "define internal i32 @chibi_scalar.1(i32 %arg0, i1 %arg1)") &&
                      strstr(ir, "call i32 @chibi_scalar.1(i32 %c3_0, i1 %c3_1)");
        free(ir);
        REQUIRE(called);
        REQUIRE(rejects_malformed(
            b,
            &guarded[b],
            GUARD_KINDS,
            GUARD_ROWS,
            MALFORMED_GUARDS,
            (int)(sizeof MALFORMED_GUARDS / sizeof *MALFORMED_GUARDS)
        ));
        REQUIRE(rejects_malformed(
            b,
            &computed[b],
            ARITHMETIC_KINDS,
            ARITHMETIC_ROWS,
            MALFORMED_ARITHMETIC,
            (int)(sizeof MALFORMED_ARITHMETIC / sizeof *MALFORMED_ARITHMETIC)
        ));
    }
    return true;
}
/* llvm_native's op and type ordinals, and its tables as Emit takes them.
 * Functions own no locals. */
enum
{
    NATIVE_BOOL = 1,
    NATIVE_INT = 6,
    NATIVE_CONSTANT = 0,
    NATIVE_ARGUMENT = 1,
    NATIVE_PUBLISH = 4,
    NATIVE_RETURN = 5,
    NATIVE_CHECK = 27,
    NATIVE_CALL = 28
};
typedef struct native_code
{
    int op, type, output, left, right, target, literal;
} native_code;
typedef struct native_function
{
    int result, first_parameter, parameters, first_slot, slots, first_code,
        code, first_argument, arguments;
} native_function;
typedef struct native_module
{
    const native_function *functions;
    int function_count;
    const int *parameters;
    int parameter_count;
    const int *slots;
    int slot_count;
    const native_code *code;
    int code_count;
    const int *arguments;
    int argument_count;
} native_module;
static const char NATIVE_PACKAGE[] = "project:llvm_native";
static pin_value native_ints(const int *values, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_I32, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = (pin_value){ .type = PIN_I32, .i = values[i] };
    }
    return (pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
static const char *const CODE_FIELDS[] = { "op",    "type",   "output", "left",
                                           "right", "target", "literal" };
static const char *const FUNCTION_FIELDS[] = { "result",     "first_parameter",
                                               "parameters", "first_slot",
                                               "slots",      "first_code",
                                               "code",       "first_argument",
                                               "arguments" };
enum
{
    CODE_WIDTH = (int)(sizeof CODE_FIELDS / sizeof *CODE_FIELDS),
    FUNCTION_WIDTH = (int)(sizeof FUNCTION_FIELDS / sizeof *FUNCTION_FIELDS)
};
/* count Instruction or Function records whose fields, in declaration order,
 * take consecutive values. */
static bool native_records(int b, const char *name, const int *values, int count, pin_value *out)
{
    bool code = !strcmp(name, "Instruction");
    const char *const *fields = code ? CODE_FIELDS : FUNCTION_FIELDS;
    int width = code ? CODE_WIDTH : FUNCTION_WIDTH;
    assert(count > 0);
    pin_value *items = xmalloc(sizeof *items * (size_t)count);
    for (int i = 0; i < count; i++) {
        items[i] = record_row(&consumer, b, NATIVE_PACKAGE, name);
        for (int j = 0; j < width; j++) {
            REQUIRE(put(&items[i], fields[j], values[i * width + j]));
        }
    }
    *out = rows(items, count, PIN_STRUCT, items[0].record);
    free(items);
    return true;
}
static bool native_codes(int b, const native_code *code, int count, pin_value *out)
{
    int *values = xmalloc(sizeof *values * CODE_WIDTH * (size_t)count);
    for (int i = 0; i < count; i++) {
        const native_code *c = &code[i];
        const int fields[CODE_WIDTH] = { c->op,     c->type,  c->output,
                                         c->left,   c->right, c->target,
                                         c->literal };
        memcpy(&values[i * CODE_WIDTH], fields, sizeof fields);
    }
    bool built = native_records(b, "Instruction", values, count, out);
    free(values);
    return built;
}
static bool native_functions(int b, const native_function *functions, int count, pin_value *out)
{
    int *values = xmalloc(sizeof *values * FUNCTION_WIDTH * (size_t)count);
    for (int i = 0; i < count; i++) {
        const native_function *f = &functions[i];
        const int fields[FUNCTION_WIDTH] = { f->result,     f->first_parameter,
                                             f->parameters, f->first_slot,
                                             f->slots,      f->first_code,
                                             f->code,       f->first_argument,
                                             f->arguments };
        memcpy(&values[i * FUNCTION_WIDTH], fields, sizeof fields);
    }
    bool built = native_records(b, "Function", values, count, out);
    free(values);
    return built;
}
/* Emits m as a module of internal functions under budget 1. */
static bool native_emit(int b, const native_module *m, const pin_union **out)
{
    pin_value args[9];
    REQUIRE(compiler_fixture_call(
        &consumer,
        b,
        "project:native_abi",
        "LinuxX64",
        NULL,
        0,
        COMPILE_BUDGET,
        &input,
        &args[0]
    ));
    REQUIRE(native_functions(b, m->functions, m->function_count, &args[1]));
    args[2] = native_ints(m->parameters, m->parameter_count);
    args[3] = native_ints(m->slots, m->slot_count);
    args[4] = native_ints(NULL, 0);
    REQUIRE(native_codes(b, m->code, m->code_count, &args[5]));
    args[6] = native_ints(m->arguments, m->argument_count);
    args[7] = (pin_value){ .type = PIN_BOOL, .b = false };
    args[8] = (pin_value){ .type = PIN_I32, .i = 1 };
    pin_value result;
    REQUIRE(
        compiler_fixture_call(&consumer, b, NATIVE_PACKAGE, "Emit", args, 9, COMPILE_BUDGET, &input, &result)
    );
    REQUIRE(result.type == PIN_UNION && result.ptr);
    *out = result.ptr;
    return true;
}
static bool native_rejects(int b, const native_module *m, const char *message)
{
    const pin_union *value;
    REQUIRE(native_emit(b, m, &value));
    REQUIRE(value->tag == fixture_variant_tag(&consumer, b, NATIVE_PACKAGE, "Result", "Rejected"));
    pin_string text = pin_text(value->values[1]);
    REQUIRE(pin_text_equal(text, (pin_string){ message, strlen(message) }));
    return true;
}
/* main() returns Pick(7, true, 9), and Pick :: (int, bool, int) -> int
 * returns its third argument. Slots s0..s2 hold the arguments in call order. */
static const native_code CALLER_AND_CALLEE[] = {
    { .op = NATIVE_CHECK },
    { .op = NATIVE_CONSTANT, .type = NATIVE_INT, .literal = 7 },
    { .op = NATIVE_CONSTANT, .type = NATIVE_BOOL, .output = 1, .literal = 1 },
    { .op = NATIVE_CONSTANT, .type = NATIVE_INT, .output = 2, .literal = 9 },
    { .op = NATIVE_CALL, .type = NATIVE_INT, .output = 3, .left = 1, .target = 3 },
    { .op = NATIVE_PUBLISH, .type = NATIVE_INT, .left = 3 },
    { .op = NATIVE_RETURN },
    { .op = NATIVE_CHECK },
    { .op = NATIVE_ARGUMENT, .type = NATIVE_INT, .left = 2 },
    { .op = NATIVE_PUBLISH, .type = NATIVE_INT },
    { .op = NATIVE_RETURN },
};
enum
{
    CALL_CODE = (int)(sizeof CALLER_AND_CALLEE / sizeof *CALLER_AND_CALLEE)
};
static const native_function CALL_FUNCTIONS[] = {
    { NATIVE_INT, 0, 0, 0, 4, 0, 7, 0, 3 },
    { NATIVE_INT, 0, 3, 4, 1, 7, 4, 3, 0 },
};
static const int CALL_PARAMETERS[] = { NATIVE_INT, NATIVE_BOOL, NATIVE_INT };
static const int CALL_SLOTS[] = { NATIVE_INT, NATIVE_BOOL, NATIVE_INT, NATIVE_INT, NATIVE_INT };
static const int CALL_ARGUMENTS[] = { 0, 1, 2 };
static native_module call_module(const native_code *code, const int *arguments, int argument_count)
{
    return (native_module){
        CALL_FUNCTIONS, 2,         CALL_PARAMETERS, 3, CALL_SLOTS, 5, code,
        CALL_CODE,      arguments, argument_count
    };
}
static bool contains(pin_string haystack, const char *needle)
{
    char *text = xmalloc(haystack.length + 1);
    memcpy(text, haystack.bytes, haystack.length);
    text[haystack.length] = 0;
    bool found = strstr(text, needle);
    free(text);
    return found;
}
/* A call loads its argument slots in order and calls the callee's internal
 * definition. */
static bool native_calls_emit_definitions(int b)
{
    native_module m = call_module(CALLER_AND_CALLEE, CALL_ARGUMENTS, 3);
    const pin_union *value;
    REQUIRE(native_emit(b, &m, &value));
    REQUIRE(value->tag == fixture_variant_tag(&consumer, b, NATIVE_PACKAGE, "Result", "Emitted"));
    pin_string text = pin_text(value->values[0]);
    REQUIRE(contains(text, "define i32 @chibi_scalar() {"));
    REQUIRE(contains(text, "define internal i32 @chibi_scalar.1(i32 %arg0, i1 %arg1, i32 %arg2) {"));
    REQUIRE(contains(text, "%v4 = call i32 @chibi_scalar.1(i32 %c4_0, i1 %c4_1, i32 %c4_2)"));
    REQUIRE(
        contains(text, "%c4_0 = load i32, ptr %s0\n  %c4_1 = load i1, ptr %s1\n  %c4_2 = load i32, ptr %s2")
    );
    REQUIRE(contains(text, "store i32 %arg2, ptr %s0"));
    return true;
}
typedef struct native_change
{
    int index;
    size_t field;
    int value;
} native_change;
/* The call module with one instruction field changed. */
static bool native_rejects_change(int b, native_change change, const char *message)
{
    native_code code[CALL_CODE];
    memcpy(code, CALLER_AND_CALLEE, sizeof code);
    *(int *)((char *)&code[change.index] + change.field) = change.value;
    native_module m = call_module(code, CALL_ARGUMENTS, 3);
    return native_rejects(b, &m, message);
}
static const char
    SIGNATURE[] = "native call differs from its callee's signature";
static const char
    TABLES[] = "unsupported native scalar signature or table size";
static const char OUTPUT[] = "native scalar output has an invalid type or "
                             "multiple definitions";
static bool native_rejects_malformed_calls(int b)
{
    /* Argument slots swapped, so a bool slot sits where an int parameter is;
     * a missing argument row; an argument slot past the caller's slots. */
    static const int swapped[] = { 1, 0, 2 }, outside[] = { 0, 1, 4 };
    native_module m = call_module(CALLER_AND_CALLEE, swapped, 3);
    REQUIRE(native_rejects(b, &m, SIGNATURE));
    m = call_module(CALLER_AND_CALLEE, CALL_ARGUMENTS, 2);
    REQUIRE(native_rejects(b, &m, TABLES));
    m = call_module(CALLER_AND_CALLEE, outside, 3);
    REQUIRE(native_rejects(b, &m, SIGNATURE));
    static const struct
    {
        native_change change;
        const char *message;
    } changes[] = {
        /* The callee is past the module's functions or negative. */
        { { 4, offsetof(native_code, left), 2 }, SIGNATURE },
        { { 4, offsetof(native_code, left), -1 }, SIGNATURE },
        /* Arity, argument range and result type disagree with the callee. */
        { { 4, offsetof(native_code, target), 2 }, SIGNATURE },
        { { 4, offsetof(native_code, right), 1 }, SIGNATURE },
        { { 4, offsetof(native_code, type), NATIVE_BOOL }, OUTPUT },
        /* The call's output is an argument slot defined before it. */
        { { 4, offsetof(native_code, output), 2 }, OUTPUT },
        { { 2, offsetof(native_code, literal), 2 }, "native bool constant is not canonical" },
    };
    for (size_t i = 0; i < sizeof changes / sizeof *changes; i++) {
        REQUIRE(native_rejects_change(b, changes[i].change, changes[i].message));
    }
    /* An argument defined after the call that reads it. */
    native_code late[CALL_CODE];
    memcpy(late, CALLER_AND_CALLEE, sizeof late);
    late[3] = CALLER_AND_CALLEE[4];
    late[4] = CALLER_AND_CALLEE[3];
    m = call_module(late, CALL_ARGUMENTS, 3);
    REQUIRE(native_rejects(b, &m, "native call argument is not defined on every path"));
    return true;
}
/* Direct and mutual recursion are rejected, as is a function range that
 * leaves a code row unowned. */
static bool native_rejects_cycles(int b)
{
    static const native_code recursive[] = {
        { .op = NATIVE_CHECK },
        { .op = NATIVE_CALL, .type = NATIVE_INT },
        { .op = NATIVE_PUBLISH, .type = NATIVE_INT },
        { .op = NATIVE_RETURN },
        { .op = NATIVE_CHECK },
        { .op = NATIVE_CALL, .type = NATIVE_INT },
        { .op = NATIVE_PUBLISH, .type = NATIVE_INT },
        { .op = NATIVE_RETURN },
    };
    static const native_function one[] = { { NATIVE_INT, 0, 0, 0, 1, 0, 4, 0, 0 } };
    static const native_function two[] = { { NATIVE_INT, 0, 0, 0, 1, 0, 4, 0, 0 },
                                           { NATIVE_INT, 0, 0, 1, 1, 4, 4, 0, 0 } };
    static const int slots[] = { NATIVE_INT, NATIVE_INT };
    native_module m = { one, 1, NULL, 0, slots, 1, recursive, 4, NULL, 0 };
    REQUIRE(native_rejects(b, &m, "native call graph is cyclic"));
    native_code mutual[8];
    memcpy(mutual, recursive, sizeof mutual);
    mutual[1].left = 1;
    m = (native_module){ two, 2, NULL, 0, slots, 2, mutual, 8, NULL, 0 };
    REQUIRE(native_rejects(b, &m, "native call graph is cyclic"));
    static const native_function gap[] = { { NATIVE_INT, 0, 0, 0, 4, 0, 7, 0, 3 },
                                           { NATIVE_INT, 0, 3, 4, 1, 8, 3, 3, 0 } };
    m = call_module(CALLER_AND_CALLEE, CALL_ARGUMENTS, 3);
    m.functions = gap;
    REQUIRE(native_rejects(b, &m, TABLES));
    return true;
}
/* llvm_native's call contract: definitions, argument order, and the
 * malformed or cyclic plans it rejects before writing IR. */
TEST_CASE(package_compiler_native_emitter_checks_calls)
{
    const source_package_input sources[] = { SCALAR_CONSUMER_SOURCE_INPUTS };
    REQUIRE(compiler_fixture_prepare(&consumer, sources, (int)(sizeof sources / sizeof *sources)));
    for (int b = 0; b < 2; b++) {
        REQUIRE(native_calls_emit_definitions(b));
        REQUIRE(native_rejects_malformed_calls(b));
        REQUIRE(native_rejects_cycles(b));
    }
    return true;
}
static const char CASES_PACKAGE[] = "project:scalar_cases";
/* A shared wrong result is not an oracle. Each executed value must match
 * the fixture's literal, including Main's value. */
static bool scalar_results_match(const int32_t values[4], int32_t expected)
{
    return values[0] == expected && values[1] == expected &&
           values[2] == expected && values[3] == expected;
}
TEST_CASE(package_compiler_scalar_results_require_the_fixture_value)
{
    const source_package_input cases[] = { SCALAR_CASES_SOURCE_INPUTS };
    REQUIRE(compiler_fixture_prepare(&corpus, cases, (int)(sizeof cases / sizeof *cases)));
    static const int32_t expected[] = { 42, 0, INT32_MAX };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 3; i++) {
            pin_value ordinal = { .type = PIN_I32, .i = i }, literal;
            REQUIRE(compiler_fixture_call(
                &corpus,
                b,
                CASES_PACKAGE,
                "ExpectedValue",
                &ordinal,
                1,
                COMPILE_BUDGET,
                &detached,
                &literal
            ));
            REQUIRE(literal.type == PIN_I32 && literal.i == expected[i]);
            int32_t values[4] = { expected[i], expected[i], expected[i], expected[i] };
            REQUIRE(scalar_results_match(values, literal.i));
            int32_t wrong = expected[i] == 42 ? 41 : 1;
            const int32_t consensus[4] = { wrong, wrong, wrong, wrong };
            REQUIRE(!scalar_results_match(consensus, literal.i));
            for (int column = 0; column < 4; column++) {
                values[column] = wrong;
                REQUIRE(!scalar_results_match(values, literal.i));
                values[column] = expected[i];
            }
            arena_free(&detached);
        }
    }
    return true;
}
#if defined(__linux__) && defined(__x86_64__)
/* Native execution of the authored scalar corpus. Each case's source goes
 * through CollectScalar on one producer VM, alternating by case, and must be
 * the result Cases.Check expects. An accepted module's IR becomes O0 and O2
 * objects through the native driver's fixed opt and llc argument vectors
 * (test/native_aot/driver/README.md), with no undefined symbol. Each object,
 * linked as a validation-only shared library, runs in a child process, so a
 * trap is a status rather than the end of this test. Its entry's value on
 * both VMs and both native levels must match the fixture's literal expected
 * value, and so must each probe's. A probe reaches an internal function
 * through a validation-only IR adapter that passes it int32 arguments, which
 * is how negative values and bool parameters reach it; the adapter is not a
 * foreign ABI. Linux x86-64 is the one target the emitter admits.
 * CHIBI_LLVM_BIN names the directory of opt, llc, ld.lld and llvm-nm;
 * nothing is found through PATH. */
/* The corpus's accepted cases. */
enum
{
    SCALAR_ACCEPTED_CASES = 34
};
/* Cases whose 64-deep operand nesting takes CollectScalar past the stage-zero
 * fixture's call depth limit (SCRIPT_CALL_DEPTH_LIMIT). Run.ps1 runs them on
 * the recovered compiler, which has no such limit. */
static const char *const DEEP_CASES[] = { "most_operands", "too_many_operands" };
enum
{
    DEEP_CASE_COUNT = (int)(sizeof DEEP_CASES / sizeof *DEEP_CASES)
};
static bool deep_case(pin_string name)
{
    for (int i = 0; i < DEEP_CASE_COUNT; i++) {
        if (pin_text_equal(name, (pin_string){ DEEP_CASES[i], strlen(DEEP_CASES[i]) })) {
            return true;
        }
    }
    return false;
}
enum
{
    MAX_PROBE_ARGUMENTS = 3
};
typedef struct scalar_probe
{
    const char *source_case, *function;
    int count;
    int32_t arguments[MAX_PROBE_ARGUMENTS];
    int32_t expected;
} scalar_probe;
static const scalar_probe SCALAR_PROBES[] = {
    { "min", "Min", 2, { INT32_MIN, INT32_MAX }, INT32_MIN },
    { "min", "Min", 2, { INT32_MAX, INT32_MIN }, INT32_MIN },
    { "min", "Min", 2, { -1, -1 }, -1 },
    { "max", "Max", 2, { INT32_MIN, INT32_MAX }, INT32_MAX },
    { "max", "Max", 2, { INT32_MAX, INT32_MIN }, INT32_MAX },
    { "max", "Max", 2, { 0, -1 }, 0 },
    { "clamp", "Clamp", 3, { -5, 0, 10 }, 0 },
    { "clamp", "Clamp", 3, { 5, 0, 10 }, 5 },
    { "clamp", "Clamp", 3, { 15, 0, 10 }, 10 },
    { "clamp", "Clamp", 3, { INT32_MIN, INT32_MIN + 1, INT32_MAX }, INT32_MIN + 1 },
    { "operators", "If", 3, { 1, 4, 9 }, 4 },
    { "operators", "If", 3, { 0, 4, 9 }, 9 },
    { "call_condition", "Small", 1, { 9 }, 1 },
    { "call_condition", "Small", 1, { 10 }, 0 },
    { "lazy_arm", "Choose", 1, { 1 }, 1 },
    { "lazy_arm", "Choose", 1, { 0 }, 2 },
    { "bare_condition", "Positive", 1, { INT32_MIN }, 0 },
    { "bare_condition", "Positive", 1, { 0 }, 0 },
    { "bare_condition", "Positive", 1, { INT32_MAX }, INT32_MAX },
    { "int_operators", "Mix", 3, { 3, 4, 5 }, 37 },
    { "int_operators", "Mix", 3, { 4, 3, 5 }, 4 },
    /* (a + b) * c wraps to -6, and -c % 3 is -2. */
    { "int_operators", "Mix", 3, { INT32_MAX - 1, INT32_MAX, 2 }, -4 },
    /* The folded constant is 2^30; only the sum with x wraps. */
    { "arithmetic", "Half", 1, { 0 }, 1073741824 },
    { "arithmetic", "Half", 1, { INT32_MAX }, -1073741825 },
    { "folded_operand", "Add", 1, { INT32_MAX }, -1073741825 },
    /* The guard compares with 2^29, not the -2^29 wrapping rows give. */
    { "folded_guard", "Pick", 1, { 0 }, 0 },
    { "folded_guard", "Pick", 1, { 536870912 }, 0 },
    { "folded_guard", "Pick", 1, { 536870913 }, INT32_MAX },
    { "wrapped_operand", "Half", 1, { 1 }, 1 },
    { "wrapped_operand", "Half", 1, { -3 }, -1 },
};
/* The six comparisons of the operators case, in the order of their source
 * functions, each probed with every pair. */
static const char *const COMPARISONS[] = { "Lt", "Le", "Gt", "Ge", "Eq", "Ne" };
static const int32_t COMPARED[][2] = { { INT32_MIN, INT32_MAX },
                                       { INT32_MAX, INT32_MIN },
                                       { 0, 0 },
                                       { -1, 0 },
                                       { INT32_MAX, INT32_MAX } };
enum
{
    PROBES = (int)(sizeof SCALAR_PROBES / sizeof *SCALAR_PROBES),
    COMPARISON_COUNT = (int)(sizeof COMPARISONS / sizeof *COMPARISONS),
    PAIRS = (int)(sizeof COMPARED / sizeof *COMPARED),
    MAX_CASE_PROBES = 160
};
static int32_t compared(int comparison, int32_t a, int32_t b)
{
    switch (comparison) {
        case 0: return a < b;
        case 1: return a <= b;
        case 2: return a > b;
        case 3: return a >= b;
        case 4: return a == b;
        default: return a != b;
    }
}
/* llvm_native's integer operations, by the name the corpus's int_operators
 * case gives each one's function. */
static const struct
{
    const char *name;
    int op, parameters;
} TOTAL_OPERATIONS[] = {
    { "Add", 7, 2 },     { "Sub", 8, 2 },    { "Mul", 9, 2 },
    { "Div", 10, 2 },    { "Mod", 11, 2 },   { "Neg", 12, 1 },
    { "BitAnd", 13, 2 }, { "BitOr", 14, 2 }, { "BitXor", 15, 2 },
    { "BitNot", 16, 1 }, { "Shl", 17, 2 },   { "Shr", 18, 2 },
};
static const int32_t TOTAL_VALUES[] = { INT32_MIN, -7, -1, 0,
                                        1,         31, 32, INT32_MAX };
enum
{
    TOTAL_OPERATION_COUNT = (int)(sizeof TOTAL_OPERATIONS / sizeof *TOTAL_OPERATIONS),
    TOTAL_VALUE_COUNT = (int)(sizeof TOTAL_VALUES / sizeof *TOTAL_VALUES),
    /* INT32_MIN, -7, -1 and 0. */
    INT_OPERANDS = 4
};
/* The language's integer contract for operation `op`: nothing fails. */
static int32_t total(int op, int32_t a, int32_t b)
{
    uint32_t x = (uint32_t)a, y = (uint32_t)b;
    switch (op) {
        case 7: return (int32_t)(x + y);
        case 8: return (int32_t)(x - y);
        case 9: return (int32_t)(x * y);
        case 10: return !b ? 0 : b == -1 ? (int32_t)(0u - x) : a / b;
        case 11: return !b ? a : b == -1 ? 0 : a % b;
        case 12: return (int32_t)(0u - x);
        case 13: return a & b;
        case 14: return a | b;
        case 15: return a ^ b;
        case 16: return ~a;
        case 17: return y >= 32 ? 0 : (int32_t)(x << y);
        default: return y >= 32 ? (a < 0 ? -1 : 0) : a >> y;
    }
}
/* The probes of one corpus case. */
static int case_probes(const char *name, scalar_probe probes[MAX_CASE_PROBES])
{
    int count = 0;
    for (int i = 0; i < PROBES; i++) {
        if (!strcmp(SCALAR_PROBES[i].source_case, name)) {
            probes[count++] = SCALAR_PROBES[i];
        }
    }
    /* Each int operator but the shifts, which the corpus does not admit, on
     * the first values of TOTAL_VALUES. */
    for (int k = 0; !strcmp(name, "int_operators") && k < TOTAL_OPERATION_COUNT - 2;
         k++) {
        int n = TOTAL_OPERATIONS[k].parameters, rights = n == 2 ? INT_OPERANDS : 1;
        for (int i = 0; i < INT_OPERANDS; i++) {
            for (int j = 0; j < rights; j++) {
                int32_t a = TOTAL_VALUES[i], b = n == 2 ? TOTAL_VALUES[j] : 0;
                probes[count++] = (scalar_probe){ "int_operators",
                                                  TOTAL_OPERATIONS[k].name,
                                                  n,
                                                  { a, b },
                                                  total(TOTAL_OPERATIONS[k].op, a, b) };
            }
        }
    }
    for (int c = 0; !strcmp(name, "operators") && c < COMPARISON_COUNT; c++) {
        for (int p = 0; p < PAIRS; p++) {
            probes[count++] = (scalar_probe){
                "operators",
                COMPARISONS[c],
                2,
                { COMPARED[p][0], COMPARED[p][1] },
                compared(c, COMPARED[p][0], COMPARED[p][1])
            };
        }
    }
    assert(count <= MAX_CASE_PROBES);
    return count;
}
static bool llvm_tool(const char *name, char path[PATH_MAX])
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "CHIBI_LLVM_BIN must name the directory of opt, llc, ld.lld and llvm-nm\n");
    }
    REQUIRE(bin && *bin);
    REQUIRE(snprintf(path, PATH_MAX, "%s/%s", bin, name) < PATH_MAX);
    REQUIRE(!access(path, X_OK));
    return true;
}
/* Set by SIGINT or SIGTERM while the native tests run: the running child's
 * process group is killed, and the run reports cancellation, not a timeout. */
static volatile sig_atomic_t native_cancelled;
static void cancel_native(int)
{
    native_cancelled = 1;
}
/* The deadlines of an LLVM tool and of a library's calls, and the output
 * kept of each. */
static const double TOOL_SECONDS = 60, CALLS_SECONDS = 20;
enum
{
    NATIVE_OUTPUT = 1 << 16
};
static const char *native_outcome(chibi_process_capture_result result)
{
    switch (result.error) {
        case CHIBI_PROCESS_OK: return result.status ? "failed" : "passed";
        case CHIBI_PROCESS_TIMEOUT: return "timed out";
        case CHIBI_PROCESS_CANCELLED: return "was cancelled";
        case CHIBI_PROCESS_SPAWN_FAILED: return "could not start";
        default: return "could not be supervised";
    }
}
/* Runs argv, an absolute executable with a fixed argument vector, in
 * `directory` under chibi_process_capture: its process group is killed at
 * the deadline, `seconds` from now, or on cancellation. `out` receives its
 * standard output; a run that does not pass reports its error output. */
static chibi_process_capture_result run_native(
    const char *const argv[],
    const char *directory,
    double seconds,
    char out[NATIVE_OUTPUT]
)
{
    char error[NATIVE_OUTPUT];
    chibi_process_output output = { out, NATIVE_OUTPUT, 0, false },
                         errors = { error, sizeof error, 0, false };
    chibi_process_capture_result result = chibi_process_capture(
        argv[0],
        argv,
        directory,
        (const char *const *)environ,
        chibi_process_now() + seconds,
        &native_cancelled,
        &output,
        &errors
    );
    if (result.error != CHIBI_PROCESS_OK || result.status) {
        fprintf(
            stderr,
            "%s %s (status %d): %s%s\n",
            argv[0],
            native_outcome(result),
            result.status,
            out,
            error
        );
    }
    return result;
}
static bool native_passed(chibi_process_capture_result result)
{
    return result.error == CHIBI_PROCESS_OK && !result.status;
}
/* An LLVM tool's run, which must pass. */
static bool run_tool(const char *const argv[], const char *directory, char out[NATIVE_OUTPUT])
{
    REQUIRE(native_passed(run_native(argv, directory, TOOL_SECONDS, out)));
    return true;
}
static bool write_text(const char *path, const char *first, const char *second)
{
    FILE *file = fopen(path, "w");
    REQUIRE(file);
    bool written = fputs(first, file) >= 0 && fputs(second, file) >= 0;
    REQUIRE(!fclose(file) && written);
    return true;
}
/* The files the native builds wrote, which the test removes. */
static char **native_files;
static void native_file(const char *path)
{
    for (int i = 0; i < acount(native_files); i++) {
        if (!strcmp(native_files[i], path)) {
            return;
        }
    }
    char *copy = xmalloc(strlen(path) + 1);
    strcpy(copy, path);
    apush(native_files, copy);
}
/* `ir` and then `adapter`, built at `level` as the driver builds an object,
 * then linked as `directory`/`name`.so, which `library` names. Only observed
 * modules request PIC for their added counter globals; ordinary modules keep
 * the driver's exact object arguments. */
static bool native_library(
    const char *directory,
    const char *name,
    const char *ir,
    const char *adapter,
    int level,
    bool pic,
    char library[PATH_MAX]
)
{
    char opt[PATH_MAX], llc[PATH_MAX], nm[PATH_MAX], lld[PATH_MAX];
    REQUIRE(llvm_tool("opt", opt) && llvm_tool("llc", llc));
    REQUIRE(llvm_tool("llvm-nm", nm) && llvm_tool("ld.lld", lld));
    char source[PATH_MAX], module[PATH_MAX], object[PATH_MAX];
    snprintf(source, sizeof source, "%s/%s-%d.ll", directory, name, level);
    snprintf(module, sizeof module, "%s/%s-%d.opt.ll", directory, name, level);
    snprintf(object, sizeof object, "%s/%s-%d.o", directory, name, level);
    snprintf(library, PATH_MAX, "%s/%s-%d.so", directory, name, level);
    const char *const outputs[] = { source, module, object, library };
    for (int i = 0; i < 4; i++) {
        native_file(outputs[i]);
    }
    REQUIRE(write_text(source, ir, adapter));
    const char *passes = level ? "-passes=verify,default<O2>,verify" : "-passes=verify";
    const char *const optimize[] = { opt,  "-S",   passes, source,
                                     "-o", module, NULL };
    const char *const emit[] = {
        llc, "-filetype=obj", level ? "-O=2" : "-O=0", module, "-o", object,
        NULL
    };
    const char *const observed_emit[] = { llc,
                                          "-filetype=obj",
                                          level ? "-O=2" : "-O=0",
                                          "-relocation-model=pic",
                                          module,
                                          "-o",
                                          object,
                                          NULL };
    const char *const symbols[] = { nm, "--undefined-only", object, NULL };
    const char *const link[] = { lld, "-shared", object, "-o", library, NULL };
    char out[NATIVE_OUTPUT];
    REQUIRE(
        run_tool(optimize, directory, out) &&
        run_tool(pic ? observed_emit : emit, directory, out)
    );
    REQUIRE(run_tool(symbols, directory, out));
    if (*out) {
        fprintf(stderr, "%s has undefined symbols: %s\n", object, out);
    }
    REQUIRE(!*out);
    REQUIRE(run_tool(link, directory, out));
    return true;
}
/* A row field of a retained module's row `index`. */
static int row_int(const pin_value *rows, int index, const char *name)
{
    const pin_array *array = rows->ptr;
    return scalar_field(&array->items[index], name).i;
}
static const char *row_name(const pin_value *rows, int index)
{
    const pin_array *array = rows->ptr;
    return pin_text(scalar_field(&array->items[index], "name")).bytes;
}
static int row_count(const pin_value *rows)
{
    return ((const pin_array *)rows->ptr)->count;
}
/* The ordinal of function `name` among the module's functions and its row,
 * or -1. */
static int function_row(const pin_value *rows, const char *name, int *ordinal)
{
    *ordinal = -1;
    for (int i = 0; i < row_count(rows); i++) {
        if (row_int(rows, i, "kind") != SCALAR_FUNCTION_ROW) {
            continue;
        }
        ++*ordinal;
        if (!strcmp(row_name(rows, i), name)) {
            return i;
        }
    }
    return -1;
}
static const char *llvm_type(int type)
{
    return type == SCALAR_BOOL ? "i1" : "i32";
}
/* Appends to `adapter` the validation-only probe @probe_<name>(ptr): it reads
 * each parameter as an int32 of its argument array, truncating a bool's, and
 * returns the function's result, zero-extending a bool. */
static bool append_adapter(const pin_value *rows, const char *name, char **adapter)
{
    int ordinal;
    int head = function_row(rows, name, &ordinal);
    REQUIRE(head > 0 && ordinal > 0);
    int count = row_int(rows, head, "count"), result = row_int(rows, head, "type");
    char text[4096], call[1024] = "";
    int n = snprintf(text, sizeof text, "\ndefine i32 @probe_%s(ptr %%args) {\n", name);
    for (int i = 0; i < count; i++) {
        int type = row_int(rows, head + 1 + i, "type");
        n += snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%p%d = getelementptr i32, ptr %%args, i64 %d\n  %%w%d = load "
            "i32, ptr %%p%d\n",
            i,
            i,
            i,
            i
        );
        if (type == SCALAR_BOOL) {
            n += snprintf(text + n, sizeof text - (size_t)n, "  %%a%d = trunc i32 %%w%d to i1\n", i, i);
        } else {
            n += snprintf(text + n, sizeof text - (size_t)n, "  %%a%d = add i32 %%w%d, 0\n", i, i);
        }
        size_t used = strlen(call);
        snprintf(call + used, sizeof call - used, "%s%s %%a%d", i ? ", " : "", llvm_type(type), i);
    }
    n += snprintf(
        text + n,
        sizeof text - (size_t)n,
        "  %%r = call %s @chibi_scalar.%d(%s)\n",
        llvm_type(result),
        ordinal,
        call
    );
    if (result == SCALAR_BOOL) {
        n += snprintf(text + n, sizeof text - (size_t)n, "  %%z = zext i1 %%r to i32\n  ret i32 %%z\n}\n");
    } else {
        n += snprintf(text + n, sizeof text - (size_t)n, "  ret i32 %%r\n}\n");
    }
    REQUIRE(n < (int)sizeof text);
    size_t old = strlen(*adapter);
    *adapter = xrealloc(*adapter, old + (size_t)n + 1);
    memcpy(*adapter + old, text, (size_t)n + 1);
    return true;
}
/* A probe's arguments as the JSON array the compiler host takes. */
static void probe_json(const pin_value *rows, const scalar_probe *probe, char *json, size_t size)
{
    int ordinal;
    int head = function_row(rows, probe->function, &ordinal);
    int n = snprintf(json, size, "[");
    for (int i = 0; i < probe->count; i++) {
        bool flag = row_int(rows, head + 1 + i, "type") == SCALAR_BOOL;
        const char *separator = i ? "," : "";
        if (flag) {
            n += snprintf(json + n, size - (size_t)n, "%s%s", separator, probe->arguments[i] ? "true" : "false");
        } else {
            n += snprintf(json + n, size - (size_t)n, "%s%d", separator, probe->arguments[i]);
        }
    }
    snprintf(json + n, size - (size_t)n, "]");
}
/* Loads `bytes`, a closure, as the test's host on `backend`. */
static bool vm_load(const unsigned char *bytes, size_t length, int backend)
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
        fprintf(stderr, "scalar closure load: %s\n", error);
    }
    REQUIRE(host);
    return true;
}
/* The int32 the loaded closure's `function` of package scalar returns for
 * `json`, with a bool as 0 or 1. */
static bool vm_value(const char *function, const char *json, int32_t *out)
{
    char name[256];
    snprintf(name, sizeof name, "project:scalar:1:%s:callable", function);
    bool valid = compiler_host_invoke(
        host,
        script_catalog_id("package-symbol", name),
        json,
        strlen(json),
        TARGET_BUDGET,
        &result
    );
    if (!valid || result.flow_status != FLOW_OK) {
        fprintf(
            stderr,
            "scalar closure %s%s failed with status %d\n",
            function,
            json,
            result.flow_status
        );
    }
    REQUIRE(valid && result.flow_status == FLOW_OK);
    /* The host's result is the JSON array of the function's one result. */
    if (!strcmp(result.json, "[true]") || !strcmp(result.json, "[false]")) {
        *out = !strcmp(result.json, "[true]");
    } else {
        char *end;
        long value = result.json[0] == '[' ? strtol(result.json + 1, &end, 10) : 0;
        if (result.json[0] != '[' || strcmp(end, "]")) {
            fprintf(
                stderr,
                "scalar closure %s%s gave %s\n",
                function,
                json,
                result.json
            );
        }
        REQUIRE(
            result.json[0] == '[' && !strcmp(end, "]") && value >= INT32_MIN &&
            value <= INT32_MAX
        );
        *out = (int32_t)value;
    }
    free(result.json);
    result = (compiler_host_result){ 0 };
    return true;
}
/* The authored compiler's closure of `source` as package scalar. */
static bool vm_closure(int b, pin_string source, unsigned char **bytes, size_t *length)
{
    const source_package_input inputs[] = {
        { "scalar/Main.chibi", source.bytes, source.length }
    };
    pin_value out;
    REQUIRE(compile(b, false, false, false, inputs, 1, COMPILE_BUDGET, &out));
    const pin_union *value = out.ptr;
    REQUIRE(out.type == PIN_UNION && value && value->tag == tag(b, "Compiled"));
    *bytes = closure_bytes(value, length);
    return true;
}
/* The text of `ir` from `start` to the end of the definition it begins. */
static char *definition(const char *ir, const char *start)
{
    const char *at = strstr(ir, start);
    assert(at);
    const char *end = strstr(at, "\n}\n");
    assert(end);
    size_t length = (size_t)(end - at);
    char *text = xmalloc(length + 1);
    memcpy(text, at, length);
    text[length] = 0;
    return text;
}
/* Unoptimized IR keeps what the source decides at run time. Min compares its
 * two parameters and branches on the result. Choose calls Skipped between its
 * branch and the branch's true target, and Taken only after that target, so
 * an arm's call runs only when its guard holds. */
static bool keeps_guards(const char *name, const char *ir)
{
    if (!strcmp(name, "min")) {
        char *min = definition(ir, "define internal i32 @chibi_scalar.1(i32 %arg0, i32 %arg1)");
        bool kept = strstr(min, "store i32 %arg0, ptr %s0") &&
                    strstr(min, "store i32 %arg1, ptr %s1") &&
                    strstr(min, "%a3 = load i32, ptr %s0\n  %b3 = load i32, ptr %s1\n  %v3 = icmp slt i32 %a3, %b3") &&
                    strstr(min, "store i1 %v3, ptr %s2") &&
                    strstr(min, "br i1 %a4, label %p");
        free(min);
        REQUIRE(kept);
    }
    if (!strcmp(name, "lazy_arm")) {
        char *choose = definition(ir, "define internal i32 @chibi_scalar.1(i1 %arg0)");
        /* The guard's branch, not a budget check's `br i1 %available`. */
        const char *branch = strstr(choose, "  br i1 %a");
        while (branch && !isdigit((unsigned char)branch[strlen("  br i1 %a")])) {
            branch = strstr(branch + 1, "  br i1 %a");
        }
        int target = -1;
        REQUIRE(branch && sscanf(branch, "  br i1 %%a%*d, label %%p%d", &target) == 1);
        char label[32];
        snprintf(label, sizeof label, "\np%d:\n", target);
        const char *skipped = strstr(choose, "call i32 @chibi_scalar.3()");
        const char *arm = strstr(choose, label);
        const char *taken = strstr(choose, "call i32 @chibi_scalar.2()");
        bool lazy = skipped && arm && taken && branch < skipped && skipped < arm &&
                    arm < taken && !strstr(taken + 1, "call i32 @chibi_scalar.2()");
        free(choose);
        REQUIRE(lazy);
    }
    return true;
}
/* A call a case checks, its entry or a probe, and what it gave on the
 * bytecode and compiled VMs and natively at O0 and O2. */
typedef struct scalar_call
{
    scalar_probe probe;
    char json[256], symbol[256];
    int32_t values[4];
} scalar_call;
enum
{
    MAX_CASE_CALLS = MAX_CASE_PROBES + 1
};
/* Case `name`'s calls: its entry, then each of its probes. */
static int case_calls(const char *name, const pin_value *rows, int32_t expected, scalar_call calls[MAX_CASE_CALLS])
{
    scalar_probe probes[MAX_CASE_PROBES];
    int count = case_probes(name, probes);
    calls[0] = (scalar_call){ .probe = { name, "Main", 0, { 0 }, expected },
                              .json = "[]",
                              .symbol = "chibi_scalar" };
    for (int i = 0; i < count; i++) {
        scalar_call *call = &calls[i + 1];
        *call = (scalar_call){ .probe = probes[i] };
        probe_json(rows, &probes[i], call->json, sizeof call->json);
        snprintf(call->symbol, sizeof call->symbol, "probe_%s", probes[i].function);
    }
    return count + 1;
}
/* Each call's value from the authored compiler's closure on both VMs. */
static bool vm_values(const unsigned char *closure, size_t length, scalar_call *calls, int count)
{
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(vm_load(closure, length, backend));
        for (int i = 0; i < count; i++) {
            REQUIRE(vm_value(calls[i].probe.function, calls[i].json, &calls[i].values[backend]));
        }
        compiler_host_free(host);
        host = NULL;
    }
    return true;
}
typedef int32_t (*scalar_entry)();
typedef int32_t (*scalar_adapter)(const int32_t *);
enum
{
    CALL_SPEC = 160
};
/* Runs each call of `library` in order, in a child run of this binary (see
 * test_language_package_compiler_chibi_native_call) that must finish within
 * `seconds`, and stores each value in its call's `column`. The first call is
 * the entry; the others are probe adapters. */
static chibi_process_capture_result library_calls(
    const char *directory,
    const char *library,
    scalar_call *calls,
    int count,
    int column,
    double seconds
)
{
    char self[PATH_MAX], specs[MAX_CASE_CALLS][CALL_SPEC], out[NATIVE_OUTPUT];
    ssize_t length = readlink("/proc/self/exe", self, sizeof self - 1);
    assert(length > 0 && count <= MAX_CASE_CALLS);
    self[length] = 0;
    const char *argv[MAX_CASE_CALLS + 4] = { self, "--package-compiler-chibi-native-call", library };
    for (int i = 0; i < count; i++) {
        int n = snprintf(specs[i], CALL_SPEC, "%s", calls[i].symbol);
        for (int a = 0; i && a < calls[i].probe.count; a++) {
            n += snprintf(
                specs[i] + n,
                CALL_SPEC - (size_t)n,
                "%c%d",
                a ? ',' : ':',
                calls[i].probe.arguments[a]
            );
        }
        assert(n < CALL_SPEC);
        argv[3 + i] = specs[i];
    }
    argv[3 + count] = NULL;
    chibi_process_capture_result result = run_native(argv, directory, seconds, out);
    const char *at = out;
    for (int i = 0; native_passed(result) && i < count; i++) {
        char *end;
        long value = strtol(at, &end, 10);
        assert(end != at && *end == '\n' && value >= INT32_MIN && value <= INT32_MAX);
        calls[i].values[column] = (int32_t)value;
        at = end + 1;
    }
    return result;
}
/* The child of library_calls: opens `library`, then calls each spec in order,
 * `symbol` for the entry or `symbol:a,b` for an adapter, and prints each value
 * on its own line as it returns. */
static int library_child(const char *library, int count, char **specs)
{
    void *opened = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!opened) {
        fprintf(stderr, "native scalar load: %s\n", dlerror());
        return 1;
    }
    for (int i = 0; i < count; i++) {
        char symbol[CALL_SPEC];
        int32_t arguments[MAX_PROBE_ARGUMENTS];
        const char *at = specs[i] + strcspn(specs[i], ":");
        size_t length = (size_t)(at - specs[i]);
        assert(length < sizeof symbol);
        memcpy(symbol, specs[i], length);
        symbol[length] = 0;
        int n = 0;
        for (char *end; *at; at = end, n++) {
            assert(n < MAX_PROBE_ARGUMENTS);
            arguments[n] = (int32_t)strtol(at + 1, &end, 10);
        }
        void *function = dlsym(opened, symbol);
        if (!function) {
            fprintf(stderr, "native scalar symbol %s: %s\n", symbol, dlerror());
            return 1;
        }
        int32_t value = specs[i][length] ? ((scalar_adapter)function)(arguments)
                                         : ((scalar_entry)function)();
        printf("%d\n", value);
        fflush(stdout);
    }
    return 0;
}
/* Each call's value natively: `ir` with an adapter for each probed function,
 * built at O0 and at O2. */
static bool native_values(
    const char *directory,
    const char *name,
    const pin_value *rows,
    const char *ir,
    scalar_call *calls,
    int count
)
{
    char *adapter = xmalloc(1);
    *adapter = 0;
    for (int i = 1; i < count; i++) {
        if (i > 1 && !strcmp(calls[i].probe.function, calls[i - 1].probe.function)) {
            continue;
        }
        REQUIRE(append_adapter(rows, calls[i].probe.function, &adapter));
    }
    for (int level = 0; level < 2; level++) {
        char library[PATH_MAX];
        REQUIRE(native_library(directory, name, ir, adapter, level * 2, false, library));
        REQUIRE(native_passed(
            library_calls(directory, library, calls, count, 2 + level, CALLS_SECONDS)
        ));
    }
    free(adapter);
    return true;
}
/* Rename only one generated callee definition. Its call sites keep the old
 * symbol, which an observed wrapper supplies. No branch body is rewritten. */
static bool observed_body(char **ir, int ordinal, const char *name)
{
    char before[128], after[128];
    snprintf(before, sizeof before, "define internal i32 @chibi_scalar.%d()", ordinal);
    snprintf(after, sizeof after, "define internal i32 @chibi_test_%s_body()", name);
    const char *at = strstr(*ir, before);
    REQUIRE(at && !strstr(at + 1, before));
    size_t prefix = (size_t)(at - *ir), old = strlen(before),
           replacement = strlen(after);
    char *renamed = xmalloc(strlen(*ir) - old + replacement + 1);
    memcpy(renamed, *ir, prefix);
    memcpy(renamed + prefix, after, replacement);
    strcpy(renamed + prefix + replacement, at + old);
    free(*ir);
    *ir = renamed;
    return true;
}
/* Reset, Choose's return, Taken's entry count and Skipped's entry count. */
static bool lazy_trace_matches(const scalar_call calls[4], int column, int condition)
{
    return calls[0].values[column] == 0 &&
           calls[1].values[column] == (condition ? 1 : 2) &&
           calls[2].values[column] == condition &&
           calls[3].values[column] == !condition;
}
/* An additional validation-only module observes generated callee entries.
 * Normal optimization/result checks above still use the unmodified IR.
 * The extra-call variant keeps Choose's value but must fail the same trace
 * oracle on the true path, even after O2 inlines the pure original bodies. */
static bool observes_lazy_arm(const char *directory, const pin_value *rows, const char *ir)
{
    int taken, skipped;
    int taken_row = function_row(rows, "Taken", &taken);
    int skipped_row = function_row(rows, "Skipped", &skipped);
    REQUIRE(taken_row > 0 && skipped_row > 0);
    REQUIRE(row_int(rows, taken_row, "count") == 0 && row_int(rows, skipped_row, "count") == 0);
    REQUIRE(
        row_int(rows, taken_row, "type") == SCALAR_INT &&
        row_int(rows, skipped_row, "type") == SCALAR_INT
    );
    char *observed = xmalloc(strlen(ir) + 1), *probe = xmalloc(1);
    strcpy(observed, ir);
    *probe = 0;
    bool valid = observed_body(&observed, taken, "taken") &&
                 observed_body(&observed, skipped, "skipped") &&
                 append_adapter(rows, "Choose", &probe);
    for (int extra = 0; valid && extra < 2; extra++) {
        char discarded[128] = "", wrappers[2048];
        if (extra) {
            snprintf(discarded, sizeof discarded, "  %%discarded = call i32 @chibi_scalar.%d()\n", skipped);
        }
        int length = snprintf(
            wrappers,
            sizeof wrappers,
            "\n@chibi_test_taken = internal global i32 0\n"
            "@chibi_test_skipped = internal global i32 0\n"
            "define internal i32 @chibi_scalar.%d() {\nentry:\n"
            "  %%n = atomicrmw add ptr @chibi_test_taken, i32 1 monotonic\n"
            "%s"
            "  %%r = call i32 @chibi_test_taken_body()\n  ret i32 %%r\n}\n"
            "define internal i32 @chibi_scalar.%d() {\nentry:\n"
            "  %%n = atomicrmw add ptr @chibi_test_skipped, i32 1 monotonic\n"
            "  %%r = call i32 @chibi_test_skipped_body()\n  ret i32 %%r\n}\n"
            "define i32 @chibi_trace_reset() {\nentry:\n"
            "  store volatile i32 0, ptr @chibi_test_taken\n"
            "  store volatile i32 0, ptr @chibi_test_skipped\n  ret i32 0\n}\n"
            "define i32 @chibi_trace_taken() {\nentry:\n"
            "  %%n = load volatile i32, ptr @chibi_test_taken\n  ret i32 "
            "%%n\n}\n"
            "define i32 @chibi_trace_skipped() {\nentry:\n"
            "  %%n = load volatile i32, ptr @chibi_test_skipped\n  ret i32 "
            "%%n\n}\n",
            taken,
            discarded,
            skipped
        );
        assert(length > 0 && length < (int)sizeof wrappers);
        size_t probe_length = strlen(probe);
        char *adapter = xmalloc(probe_length + (size_t)length + 1);
        memcpy(adapter, probe, probe_length);
        memcpy(adapter + probe_length, wrappers, (size_t)length + 1);
        for (int level = 0; valid && level < 2; level++) {
            char library[PATH_MAX];
            valid = native_library(
                directory,
                extra ? "lazy_trace_extra" : "lazy_trace",
                observed,
                adapter,
                level * 2,
                true,
                library
            );
            for (int condition = 0; valid && condition < 2; condition++) {
                scalar_call calls[4] = {
                    { .symbol = "chibi_trace_reset" },
                    { .symbol = "probe_Choose",
                      .probe = { .count = 1, .arguments = { condition } } },
                    { .symbol = "chibi_trace_taken" },
                    { .symbol = "chibi_trace_skipped" }
                };
                int column = 2 + level;
                valid = native_passed(
                    library_calls(directory, library, calls, 4, column, CALLS_SECONDS)
                );
                bool matches = lazy_trace_matches(calls, column, condition);
                valid = valid && matches == (!extra || !condition) &&
                        calls[0].values[column] == 0 &&
                        calls[1].values[column] == (condition ? 1 : 2) &&
                        calls[2].values[column] == condition &&
                        calls[3].values[column] == (!condition || extra);
                fprintf(
                    stderr,
                    "lazy trace O%d extra %d condition %d: return %d, "
                    "Taken %d, Skipped %d, oracle %s\n",
                    level * 2,
                    extra,
                    condition,
                    calls[1].values[column],
                    calls[2].values[column],
                    calls[3].values[column],
                    matches ? "accept" : "reject"
                );
            }
        }
        free(adapter);
    }
    free(observed);
    free(probe);
    REQUIRE(valid);
    return true;
}
/* Every executed result matches its literal fixture or probe oracle. */
static bool calls_agree(const char *name, const scalar_call *calls, int count)
{
    for (int i = 0; i < count; i++) {
        const int32_t *values = calls[i].values;
        int32_t expected = calls[i].probe.expected;
        bool agree = scalar_results_match(values, expected);
        if (!agree) {
            fprintf(
                stderr,
                "scalar case %s %s%s: expected %d, VM %d %d, native O0 %d O2 "
                "%d\n",
                name,
                calls[i].probe.function,
                calls[i].json,
                expected,
                values[0],
                values[1],
                values[2],
                values[3]
            );
        }
        REQUIRE(agree);
    }
    return true;
}
/* The IR EmitBytes gives for `rows`, NUL-terminated, or NULL for none. */
static bool emitted_ir(int b, pin_value *rows, char **ir)
{
    pin_value emitted;
    REQUIRE(
        compiler_fixture_call(&consumer, b, "project:scalar_llvm", "EmitBytes", rows, 1, COMPILE_BUDGET, &input, &emitted)
    );
    const pin_array *bytes = emitted.ptr;
    REQUIRE(emitted.container == PIN_CONTAINER_ARRAY);
    *ir = NULL;
    if (!bytes->count) {
        return true;
    }
    *ir = xmalloc((size_t)bytes->count + 1);
    for (int i = 0; i < bytes->count; i++) {
        (*ir)[i] = (char)bytes->items[i].u8;
    }
    (*ir)[bytes->count] = 0;
    return true;
}
/* `source`'s producer rows from CollectScalar on VM b. */
static bool collected_rows(int b, const char *case_name, pin_value source, pin_value *rows)
{
    pin_value args[] = { text("/project"),
                         text("Scalar.chibi"),
                         source,
                         { 0 },
                         instruction_names() };
    REQUIRE(target(b, &args[3]));
    bool collected = compiler_fixture_call(
        &fixture,
        b,
        PACKAGE,
        "CollectScalar",
        args,
        5,
        COMPILE_BUDGET,
        &detached,
        rows
    );
    if (!collected) {
        fprintf(stderr, "scalar case %s: CollectScalar failed\n", case_name);
    }
    REQUIRE(collected);
    return true;
}
/* An accepted module, `ir` its IR, which this frees: each of its calls must
 * give one value on both VMs and natively at O0 and O2. */
static bool module_agrees(
    const char *directory,
    int b,
    const char *case_name,
    pin_string source,
    const pin_value *rows,
    char *ir,
    int32_t expected,
    scalar_call calls[MAX_CASE_CALLS]
)
{
    REQUIRE(keeps_guards(case_name, ir));
    unsigned char *closure;
    size_t length;
    REQUIRE(vm_closure(b, source, &closure, &length));
    int count = case_calls(case_name, rows, expected, calls);
    bool ran = vm_values(closure, length, calls, count) &&
               native_values(directory, case_name, rows, ir, calls, count);
    bool agree = ran && calls_agree(case_name, calls, count);
    if (agree && !strcmp(case_name, "lazy_arm")) {
        agree = observes_lazy_arm(directory, rows, ir);
    }
    free(closure);
    free(ir);
    REQUIRE(agree);
    return true;
}
/* Case `index` of the corpus: its producer rows on VM b must be what the case
 * expects, and an accepted module must run natively as it does on both VMs. */
static bool scalar_case_agrees(const char *directory, int index, int *accepted, int *deep)
{
    int b = index % 2;
    pin_value ordinal = { .type = PIN_I32, .i = index }, name, source, rows, checked;
    REQUIRE(
        compiler_fixture_call(&corpus, b, CASES_PACKAGE, "CaseName", &ordinal, 1, COMPILE_BUDGET, &detached, &name)
    );
    if (deep_case(pin_text(name))) {
        ++*deep;
        return true;
    }
    const char *case_name = pin_text(name).bytes;
    REQUIRE(
        compiler_fixture_call(&corpus, b, CASES_PACKAGE, "Source", &ordinal, 1, COMPILE_BUDGET, &detached, &source)
    );
    REQUIRE(collected_rows(b, case_name, source, &rows));
    REQUIRE(
        compiler_fixture_call(&corpus, b, CASES_PACKAGE, "Check", &rows, 1, COMPILE_BUDGET, &detached, &checked)
    );
    if (checked.i != 1) {
        fprintf(stderr, "scalar case %s: Check gave %d\n", case_name, checked.i);
    }
    REQUIRE(checked.i == 1);
    char *ir;
    REQUIRE(emitted_ir(b, &rows, &ir));
    bool rejected = row_int(&rows, 1, "kind") == SCALAR_REJECTION_ROW;
    REQUIRE(rejected == !ir);
    if (rejected) {
        return true;
    }
    pin_value expected;
    REQUIRE(
        compiler_fixture_call(&corpus, b, CASES_PACKAGE, "ExpectedValue", &ordinal, 1, COMPILE_BUDGET, &detached, &expected)
    );
    REQUIRE(expected.type == PIN_I32);
    ++*accepted;
    scalar_call calls[MAX_CASE_CALLS];
    return module_agrees(
        directory,
        b,
        case_name,
        pin_text(source),
        &rows,
        ir,
        expected.i,
        calls
    );
}
/* A scalar module with a constant expression the ordinary compiler folds
 * exactly, and the value its Main gives. They stay out of the 96-source
 * corpus, which Run.ps1 bounds. Fixed-width rows would wrap each one:
 * `(2147483647 + 1) / 2` to -2^30, and the square to 1. */
static const struct
{
    const char *name, *source;
    int32_t entry;
} FOLDED_SOURCES[] = {
    { "folded_return",
      "package scalar\nMain :: () -> int { return (2147483647 + 1) / 2; }\n",
      1073741824 },
    { "folded_square", "package scalar\nMain :: () -> int { return (2147483647 * 2147483647) / 2147483647; }\n", INT32_MAX },
    { "folded_operand",
      "package scalar\nMain :: () -> int { return Add(1); }\n"
      "Add :: (x: int) -> int { return x + ((2147483647 + 1) / 2); }\n",
      1073741825 },
    /* In an argument, a guard's condition and its arm. */
    { "folded_guard",
      "package scalar\nMain :: () -> int { return Pick(Id((2147483647 + 1) / "
      "2)); }\n"
      "Id :: (x: int) -> int { return x; }\n"
      "Pick :: (x: int) -> int {\n"
      "    if (x > (2147483647 + 1) / 4) { return (2147483647 * 2147483647) / "
      "2147483647; }\n"
      "    return 0;\n"
      "}\n",
      INT32_MAX },
    /* Not constant: the sum wraps at run time. */
    { "wrapped_operand",
      "package scalar\nMain :: () -> int { return Half(2147483647); }\n"
      "Half :: (x: int) -> int { return (x + 1) / 2; }\n",
      -1073741824 },
};
enum
{
    FOLDED_SOURCE_COUNT = (int)(sizeof FOLDED_SOURCES / sizeof *FOLDED_SOURCES)
};
/* Each folded source is accepted, and its Main gives its value on both VMs
 * and natively at O0 and O2. */
static bool folded_sources_agree(const char *directory)
{
    for (int i = 0; i < FOLDED_SOURCE_COUNT && !native_cancelled; i++) {
        int b = i % 2;
        const char *name = FOLDED_SOURCES[i].name;
        pin_value rows;
        REQUIRE(collected_rows(b, name, text(FOLDED_SOURCES[i].source), &rows));
        char *ir;
        REQUIRE(emitted_ir(b, &rows, &ir));
        if (!ir) {
            fprintf(stderr, "scalar case %s: rejected\n", name);
        }
        REQUIRE(ir);
        scalar_call calls[MAX_CASE_CALLS];
        pin_string source = { FOLDED_SOURCES[i].source,
                              strlen(FOLDED_SOURCES[i].source) };
        REQUIRE(module_agrees(
            directory,
            b,
            name,
            source,
            &rows,
            ir,
            FOLDED_SOURCES[i].entry,
            calls
        ));
        if (calls[0].values[0] != FOLDED_SOURCES[i].entry) {
            fprintf(
                stderr,
                "scalar case %s: Main gave %d, not %d\n",
                name,
                calls[0].values[0],
                FOLDED_SOURCES[i].entry
            );
        }
        REQUIRE(calls[0].values[0] == FOLDED_SOURCES[i].entry);
        arena_free(&detached);
    }
    return true;
}
/* A constant that folds outside its type is still a located error. */
static bool folded_overflow_rejected()
{
    static const char SOURCE[] = "package scalar\nMain :: () -> int { return "
                                 "2147483647 + 1; }\n";
    pin_value rows;
    REQUIRE(collected_rows(0, "folded_overflow", text(SOURCE), &rows));
    const pin_array *array = rows.ptr;
    REQUIRE(row_count(&rows) == 2 && row_int(&rows, 1, "kind") == SCALAR_REJECTION_ROW);
    const char *message = pin_text(scalar_field(&array->items[1], "text")).bytes;
    if (!strstr(message, "outside the range of int")) {
        fprintf(stderr, "scalar case folded_overflow: %s\n", message);
    }
    REQUIRE(strstr(message, "outside the range of int"));
    arena_free(&detached);
    return true;
}

/* A new directory for the native builds' files. */
static bool native_directory(char directory[PATH_MAX])
{
    const char *temporary = getenv("TMPDIR");
    snprintf(directory, PATH_MAX, "%s/chibi-native-scalar-XXXXXX", temporary && *temporary ? temporary : "/tmp");
    REQUIRE(mkdtemp(directory));
    return true;
}
/* Removes the files the native builds registered, then `directory`. A build
 * that failed may not have written all its files. */
static bool remove_native_files(const char *directory)
{
    bool removed = true;
    for (int i = 0; i < acount(native_files); i++) {
        removed = (!remove(native_files[i]) || errno == ENOENT) && removed;
        free(native_files[i]);
    }
    afree(native_files);
    REQUIRE(!rmdir(directory) && removed);
    return true;
}
/* Whether process `pid` has exited: it is gone, or a zombie whose parent has
 * not reaped it yet. Waits up to `seconds` for its kill to land. */
static bool process_ended(pid_t pid, double seconds)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    for (double deadline = chibi_process_now() + seconds;
         chibi_process_now() < deadline;) {
        FILE *file = fopen(path, "r");
        if (!file) {
            return true;
        }
        char state = 0;
        int fields = fscanf(file, "%*d (%*[^)]) %c", &state);
        fclose(file);
        if (fields == 1 && (state == 'Z' || state == 'X')) {
            return true;
        }
        usleep(10000);
    }
    return false;
}
/* How long a run that should end at its deadline or cancellation may take,
 * which allows for a slow sanitized host. */
static const double PROMPT_SECONDS = 10;

TEST_CASE(package_compiler_native_runs_are_bounded)
{
    char out[NATIVE_OUTPUT];
    /* A tool that hangs, with a descendant in its group, times out at its
     * deadline, and the descendant is killed with it. */
    const char *const hung[] = { "/bin/sh", "-c", "sleep 60 & echo $!; wait", NULL };
    double started = chibi_process_now();
    chibi_process_capture_result result = run_native(hung, "/", 0.5, out);
    REQUIRE(result.error == CHIBI_PROCESS_TIMEOUT);
    REQUIRE(chibi_process_now() - started < PROMPT_SECONDS);
    char *end;
    long descendant = strtol(out, &end, 10);
    REQUIRE(end != out && descendant > 0);
    REQUIRE(process_ended((pid_t)descendant, PROMPT_SECONDS));
    /* SIGTERM while a tool runs cancels it, and is not a timeout. */
    const char *const terminating[] = { "/bin/sh", "-c", "kill -TERM $PPID; sleep 60", NULL };
    started = chibi_process_now();
    result = run_native(terminating, "/", 60, out);
    bool cancelled = native_cancelled;
    native_cancelled = 0;
    REQUIRE(cancelled && result.error == CHIBI_PROCESS_CANCELLED);
    REQUIRE(chibi_process_now() - started < PROMPT_SECONDS);
    /* Generated code that does not return in time times out in its child.
     * It loops 2^35 times, far past the deadline, so a deadline that is not
     * enforced fails the test instead of hanging it. */
    char directory[PATH_MAX], library[PATH_MAX];
    REQUIRE(native_directory(directory));
    const char *loop = "define i32 @hang() {\n"
                       "entry:\n"
                       "  br label %loop\n"
                       "loop:\n"
                       "  %i = phi i64 [ 0, %entry ], [ %next, %loop ]\n"
                       "  %next = add i64 %i, 1\n"
                       "  %done = icmp eq i64 %next, 34359738368\n"
                       "  br i1 %done, label %exit, label %loop\n"
                       "exit:\n"
                       "  ret i32 0\n"
                       "}\n";
    scalar_call call = { .symbol = "hang" };
    bool built = native_library(directory, "hang", loop, "", 0, false, library);
    started = chibi_process_now();
    result = built ? library_calls(directory, library, &call, 1, 2, 1)
                   : (chibi_process_capture_result){ 0 };
    double elapsed = chibi_process_now() - started;
    REQUIRE(remove_native_files(directory) && built);
    REQUIRE(result.error == CHIBI_PROCESS_TIMEOUT && elapsed < PROMPT_SECONDS);
    return true;
}
/* A module whose entry returns 0 and whose function k + 1 applies operation
 * k to its parameters. */
static bool total_ir(char **ir)
{
    native_code code[4 + TOTAL_OPERATION_COUNT * 6];
    native_function functions[1 + TOTAL_OPERATION_COUNT];
    int parameters[TOTAL_OPERATION_COUNT * 2], slots[1 + TOTAL_OPERATION_COUNT * 3];
    int c = 0, p = 0, s = 0;
    code[c++] = (native_code){ .op = NATIVE_CHECK };
    code[c++] = (native_code){ .op = NATIVE_CONSTANT, .type = NATIVE_INT };
    code[c++] = (native_code){ .op = NATIVE_PUBLISH, .type = NATIVE_INT };
    code[c++] = (native_code){ .op = NATIVE_RETURN };
    slots[s++] = NATIVE_INT;
    functions[0] = (native_function){ NATIVE_INT, 0, 0, 0, 1, 0, c, 0, 0 };
    for (int k = 0; k < TOTAL_OPERATION_COUNT; k++) {
        int n = TOTAL_OPERATIONS[k].parameters, first = c;
        functions[k + 1] = (native_function){ NATIVE_INT, p, n, s, n + 1,
                                              c,          0, 0, 0 };
        code[c++] = (native_code){ .op = NATIVE_CHECK };
        for (int i = 0; i < n; i++) {
            code[c++] = (native_code){ .op = NATIVE_ARGUMENT,
                                       .type = NATIVE_INT,
                                       .output = i,
                                       .left = i };
            parameters[p++] = NATIVE_INT;
            slots[s++] = NATIVE_INT;
        }
        code[c++] = (native_code){ .op = TOTAL_OPERATIONS[k].op,
                                   .type = NATIVE_INT,
                                   .output = n,
                                   .left = 0,
                                   .right = n - 1 };
        slots[s++] = NATIVE_INT;
        code[c++] = (native_code){ .op = NATIVE_PUBLISH, .type = NATIVE_INT, .left = n };
        code[c++] = (native_code){ .op = NATIVE_RETURN };
        functions[k + 1].code = c - first;
    }
    native_module m = { functions,  1 + TOTAL_OPERATION_COUNT,
                        parameters, p,
                        slots,      s,
                        code,       c,
                        NULL,       0 };
    const pin_union *value;
    REQUIRE(native_emit(0, &m, &value));
    REQUIRE(value->tag == fixture_variant_tag(&consumer, 0, NATIVE_PACKAGE, "Result", "Emitted"));
    pin_string text = pin_text(value->values[0]);
    REQUIRE(
        !contains(text, "label %trap\n  br") && !contains(text, " nsw ") &&
        !contains(text, " nuw ")
    );
    *ir = xmalloc(text.length + 1);
    memcpy(*ir, text.bytes, text.length);
    (*ir)[text.length] = 0;
    return true;
}
/* The validation-only adapter probe_NAME(ptr) of each operation. */
static char *total_adapters()
{
    char *adapters = xmalloc(1);
    *adapters = 0;
    for (int k = 0; k < TOTAL_OPERATION_COUNT; k++) {
        bool binary = TOTAL_OPERATIONS[k].parameters == 2;
        char text[512];
        int n = snprintf(
            text,
            sizeof text,
            "\ndefine i32 @probe_%s(ptr %%args) {\n  %%a0 = load i32, ptr "
            "%%args\n",
            TOTAL_OPERATIONS[k].name
        );
        if (binary) {
            n += snprintf(text + n, sizeof text - (size_t)n, "  %%p1 = getelementptr i32, ptr %%args, i64 1\n  %%a1 = load i32, ptr %%p1\n");
        }
        snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%r = call i32 @chibi_scalar.%d(i32 %%a0%s)\n  ret i32 %%r\n}\n",
            k + 1,
            binary ? ", i32 %a1" : ""
        );
        size_t used = strlen(adapters);
        adapters = xrealloc(adapters, used + strlen(text) + 1);
        strcpy(adapters + used, text);
    }
    return adapters;
}
/* The entry, then operation k on every pair of TOTAL_VALUES, or every value
 * for a unary one. */
static int total_calls(int k, scalar_call calls[MAX_CASE_CALLS])
{
    int count = 0;
    calls[count++] = (scalar_call){ .probe = { "total", "Main", 0, { 0 }, 0 },
                                    .symbol = "chibi_scalar" };
    int n = TOTAL_OPERATIONS[k].parameters, rights = n == 2 ? TOTAL_VALUE_COUNT : 1;
    for (int i = 0; i < TOTAL_VALUE_COUNT; i++) {
        for (int j = 0; j < rights; j++) {
            int32_t a = TOTAL_VALUES[i], b = n == 2 ? TOTAL_VALUES[j] : 0;
            scalar_call *call = &calls[count++];
            *call = (scalar_call){ .probe = { "total",
                                              TOTAL_OPERATIONS[k].name,
                                              n,
                                              { a, b },
                                              total(TOTAL_OPERATIONS[k].op, a, b) } };
            snprintf(
                call->symbol,
                sizeof call->symbol,
                "probe_%s",
                TOTAL_OPERATIONS[k].name
            );
        }
    }
    assert(count <= MAX_CASE_CALLS);
    return count;
}

TEST_CASE(package_compiler_native_arithmetic_is_total)
{
    const source_package_input consumers[] = { SCALAR_CONSUMER_SOURCE_INPUTS };
    REQUIRE(
        compiler_fixture_prepare(&consumer, consumers, (int)(sizeof consumers / sizeof *consumers))
    );
    char *ir;
    REQUIRE(total_ir(&ir));
    char *adapters = total_adapters(), directory[PATH_MAX];
    REQUIRE(native_directory(directory));
    bool agreed = true;
    for (int level = 0; agreed && level < 2; level++) {
        char library[PATH_MAX];
        agreed = native_library(directory, "total", ir, adapters, level * 2, false, library);
        for (int k = 0; agreed && k < TOTAL_OPERATION_COUNT; k++) {
            scalar_call calls[MAX_CASE_CALLS];
            int count = total_calls(k, calls);
            agreed = native_passed(
                library_calls(directory, library, calls, count, 2 + level, CALLS_SECONDS)
            );
            for (int i = 1; agreed && i < count; i++) {
                const scalar_probe *probe = &calls[i].probe;
                agreed = calls[i].values[2 + level] == probe->expected;
                if (!agreed) {
                    fprintf(
                        stderr,
                        "native %s(%d, %d) at O%d gave %d, expected %d\n",
                        probe->function,
                        probe->arguments[0],
                        probe->arguments[1],
                        level * 2,
                        calls[i].values[2 + level],
                        probe->expected
                    );
                }
            }
        }
    }
    free(ir);
    free(adapters);
    REQUIRE(remove_native_files(directory) && agreed);
    return true;
}
TEST_CASE(package_compiler_scalar_runs_natively_like_the_vm)
{
    REQUIRE(prepare());
    const source_package_input consumers[] = { SCALAR_CONSUMER_SOURCE_INPUTS };
    REQUIRE(
        compiler_fixture_prepare(&consumer, consumers, (int)(sizeof consumers / sizeof *consumers))
    );
    const source_package_input cases[] = { SCALAR_CASES_SOURCE_INPUTS };
    REQUIRE(compiler_fixture_prepare(&corpus, cases, (int)(sizeof cases / sizeof *cases)));
    char directory[PATH_MAX];
    REQUIRE(native_directory(directory));
    pin_value count;
    REQUIRE(
        compiler_fixture_call(&corpus, 0, CASES_PACKAGE, "Count", NULL, 0, COMPILE_BUDGET, &detached, &count)
    );
    int accepted = 0, deep = 0;
    bool agreed = true;
    for (int i = 0; agreed && !native_cancelled && i < count.i; i++) {
        agreed = scalar_case_agrees(directory, i, &accepted, &deep);
        arena_free(&detached);
    }
    agreed = agreed && folded_sources_agree(directory) && folded_overflow_rejected();
    REQUIRE(remove_native_files(directory));
    if (native_cancelled) {
        fprintf(stderr, "native scalar run was cancelled\n");
    }
    REQUIRE(agreed && !native_cancelled);
    REQUIRE(accepted == SCALAR_ACCEPTED_CASES && deep == DEEP_CASE_COUNT);
    return true;
}
#endif
void test_language_package_compiler_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_compound_diagnostics_match_stage_zero);
    RUN_TEST_CASE(package_compiler_builds_runnable_packages);
    RUN_TEST_CASE(package_compiler_asserts_without_native_capability);
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
    RUN_TEST_CASE(package_compiler_float_constant_dependency);
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
    RUN_TEST_CASE(package_compiler_float_constant_dependency_compiled);
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
    RUN_TEST_CASE(package_compiler_keeps_text_state_per_owner);
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
void test_language_package_compiler_chibi_scalar()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_scalar_rows_outlive_the_producer);
    RUN_TEST_CASE(package_compiler_native_emitter_checks_calls);
    RUN_TEST_CASE(package_compiler_scalar_results_require_the_fixture_value);
    pu_clear_setup();
}
#if defined(__linux__) && defined(__x86_64__)
int test_language_package_compiler_chibi_native_call(int count, char **arguments)
{
    return library_child(arguments[0], count - 1, arguments + 1);
}
#else
int test_language_package_compiler_chibi_native_call(int, char **)
{
    abort();
}
#endif
void test_language_package_compiler_chibi_native()
{
#if defined(__linux__) && defined(__x86_64__)
    /* The supervised children run in their own process groups, so a signal
     * to this one cancels them instead of leaving them running. */
    struct sigaction cancel = { .sa_handler = cancel_native }, interrupt, terminate;
    sigemptyset(&cancel.sa_mask);
    sigaction(SIGINT, &cancel, &interrupt);
    sigaction(SIGTERM, &cancel, &terminate);
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_native_runs_are_bounded);
    RUN_TEST_CASE(package_compiler_native_arithmetic_is_total);
    RUN_TEST_CASE(package_compiler_scalar_runs_natively_like_the_vm);
    pu_clear_setup();
    sigaction(SIGINT, &interrupt, NULL);
    sigaction(SIGTERM, &terminate, NULL);
#else
    fprintf(stderr, "native scalar execution needs Linux x86-64, the one target the emitter admits\n");
    abort();
#endif
}

/* Fixed primitive-native argument association, checked against literal results
 * and the independent stage-zero reference. */
static const sample native_argument_samples[] = {
    { .name = "native positional control",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(\"abcd\", 1, 2); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native names in declaration order",
      .source = "package facts\nRun :: () -> string { return text_slice(Value: "
                "\"abcd\", Start: 1, Count: 2); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native reordered names",
      .source = "package facts\nRun :: () -> string { return text_slice(Count: "
                "2, Value: \"abcd\", Start: 1); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native mixed arguments",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(\"abcd\", Count: 2, Start: 1); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native unary name",
      .source = "package facts\nRun :: () -> int { return "
                "text_byte_length(Value: \"abcd\"); }",
      .calls = { { "Run", "[]", "[4]", NULL } } },
    { .name = "native loose label match",
      .source = "package facts\nRun :: () -> string { return text_slice(count: "
                "2, value: \"abcd\", start: 1); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native arguments evaluate in source order",
      .source = "package facts\n"
                "Number :: (order: ref int, digit: int, value: int) -> int { "
                "order = order * 10 + digit; return value; }\n"
                "Text :: (order: ref int) -> string { order = order * 10 + 2; "
                "return \"abcd\"; }\n"
                "Run :: () -> int {\n"
                "  order: int = 0;\n"
                "  sliced := text_slice(Count: Number(ref order, 3, 2), Value: "
                "Text(ref order), Start: Number(ref order, 1, 1));\n"
                "  if (!text_equal(sliced, \"bc\")) { return -1; }\n"
                "  return order;\n}",
      .calls = { { "Run", "[]", "[321]", NULL } } },
    { .name = "native unknown name",
      .source = "package facts\nRun :: () -> string { return text_slice(Value: "
                "\"abcd\", Start: 1, Missing: 2); }",
      .message = "Unknown argument 'Missing'",
      .line = 2,
      .column = 66 },
    { .name = "native duplicate name",
      .source = "package facts\nRun :: () -> string { return text_slice(Value: "
                "\"abcd\", Start: 1, Start: 2, Count: 2); }",
      .message = "Argument supplied more than once",
      .line = 2,
      .column = 66 },
    { .name = "native positional named duplicate",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(\"abcd\", Value: \"efgh\", Count: 2); }",
      .message = "Argument supplied more than once",
      .line = 2,
      .column = 49 },
    { .name = "native named CString expression location",
      .source = "package facts\nRun :: () -> int { return cstring_probe(value: "
                "(\"a\\u0000b\")); }",
      .c_string_probe = true,
      .message = "C-string boundary 'value' cannot accept NUL at byte 1",
      .line = 2,
      .column = 48 },
    { .name = "native named positional duplicate",
      .source = "package facts\nRun :: () -> string { return text_slice(Start: "
                "1, 2, Value: \"abcd\"); }",
      .message = "Argument supplied more than once",
      .line = 2,
      .column = 51 },
    { .name = "native reordered CString expression location",
      .source = "package facts\nRun :: () -> int { return cstring_pair(value: "
                "(\"a\\u0000b\"), prefix: 1); }",
      .c_string_probe = true,
      .message = "C-string boundary 'value' cannot accept NUL at byte 1",
      .line = 2,
      .column = 47 },
    { .name = "native extra argument",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(\"abcd\", 1, 2, 3); }",
      .message = "Too many arguments",
      .line = 2,
      .column = 55 },
};

TEST_CASE(native_arguments_reference_contract)
{
    /* Every valid call supplies all parameters of the existing text natives. */
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(&native_argument_samples[0], inputs);
    native_definition_pack packs[] = { CSTRING_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, inputs, count, packs, 1));
    for (int i = 0;
         i < (int)(sizeof native_argument_samples / sizeof *native_argument_samples);
         i++) {
        const sample *s = &native_argument_samples[i];
        fprintf(stderr, "native arguments reference: %s\n", s->name);
        if (s->message) {
            REQUIRE(rejects_like_oracle(s, s->message, s->line, s->column));
            continue;
        }
        unsigned char *bytes = NULL;
        size_t length = 0;
        REQUIRE(oracle_closure(s, &bytes, &length));
        bool passed = run(bytes, length, &s->calls[0], 0) &&
                      run(bytes, length, &s->calls[0], 1);
        free(bytes);
        REQUIRE(passed);
    }
    return true;
}

TEST_CASE(native_arguments_authored_contract)
{
    REQUIRE(prepare());
    bool all = true;
    for (int b = 0; b < 2; b++) {
        for (int i = 0;
             i < (int)(sizeof native_argument_samples / sizeof *native_argument_samples);
             i++) {
            const sample *s = &native_argument_samples[i];
            source_package_input inputs[SAMPLE_INPUTS];
            int count = sample_inputs(s, inputs);
            pin_value out;
            REQUIRE(
                compile(b, false, s->c_string_probe, false, inputs, count, COMPILE_BUDGET, &out)
            );
            const pin_union *value = out.ptr;
            REQUIRE(out.type == PIN_UNION && value);
            bool passed = false;
            if (value->tag == tag(b, "Rejected")) {
                pin_value message, line, column;
                REQUIRE(get(&value->values[0], "message", &message));
                REQUIRE(get(&value->values[0], "line", &line));
                REQUIRE(get(&value->values[0], "column", &column));
                passed = s->message && !strcmp(s->message, pin_text(message).bytes) &&
                         line.i == s->line && column.i == s->column;
                fprintf(
                    stderr,
                    "native arguments authored %d: %s: %s at %d:%d (%s)\n",
                    b,
                    s->name,
                    pin_text(message).bytes,
                    line.i,
                    column.i,
                    passed ? "expected" : "MISMATCH"
                );
            } else {
                REQUIRE(value->tag == tag(b, "Compiled") && value->count == 2);
                size_t length = 0;
                unsigned char *bytes = closure_bytes(value, &length);
                unsigned char *expected = NULL;
                size_t expected_length = 0;
                passed = !s->message &&
                         (b || (oracle_closure(s, &expected, &expected_length) &&
                                points_like_oracle(bytes, length, false))) &&
                         run(bytes, length, &s->calls[0], 0) &&
                         run(bytes, length, &s->calls[0], 1);
                free(expected);
                free(bytes);
                /* run() leaves a failed invocation for cleanup; continue the
                 * remaining fixtures without overwriting that ownership. */
                compiler_host_free(host);
                host = NULL;
                free(result.json);
                result = (compiler_host_result){ 0 };
                fprintf(
                    stderr,
                    "native arguments authored %d: %s: compiled (%s)\n",
                    b,
                    s->name,
                    passed ? "expected" : "MISMATCH"
                );
            }
            arena_free(&detached);
            all = passed && all;
        }
    }
    REQUIRE(all);
    return true;
}

void test_language_package_compiler_chibi_native_arguments_oracle()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_arguments_reference_contract);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_native_arguments()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_arguments_authored_contract);
    pu_clear_setup();
}
