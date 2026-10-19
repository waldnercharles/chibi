#include "package_compiler_chibi.h"

#include "compiler_fixture.h"
#include "execution/definition.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "package_compiler_chibi_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/native_capabilities.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/union.h"

#include <assert.h>
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
    CALLS = 10,
    COMPILE_BUDGET = 20000000,
    TARGET_BUDGET = 100000
};
/* A NULL expected result means the call must fail. */
typedef struct
{
    const char *function, *arguments, *expected;
} sample_call;
/* `source` is package facts; `library` is package lib when present, and
 * `core` adds core:strings. */
typedef struct
{
    const char *name, *source, *library;
    bool core;
    sample_call calls[CALLS];
    const char *message;
    int line, column;
} sample;
static const sample samples[] = {
    { .name = "constant",
      .source = "package facts\nAnswer :: pure fn() -> int { return 42; }",
      .calls = { { "Answer", "[]", "[42]" } } },
    { .name = "parameters",
      .source = "package facts\nAdd :: pure fn(a: int, b: int) -> int { return "
                "a + b * 2 - (a - b) % 3; }",
      .calls = { { "Add", "[3,4]", "[12]" }, { "Add", "[-5,2]", "[0]" } } },
    { .name = "loops",
      .source = "package facts\nSum :: pure fn(n: int) -> int {\n"
                "    total: int = 0;\n    i: int = 0;\n"
                "    while (i < n) { total += i; i += 1; }\n    return "
                "total;\n}",
      .calls = { { "Sum", "[10]", "[45]" }, { "Sum", "[0]", "[0]" } } },
    { .name = "recursion",
      .source = "package facts\nFib :: pure fn(n: int) -> int {\n"
                "    if (n < 2) { return n; }\n    return Fib(n - 1) + Fib(n - "
                "2);\n}",
      .calls = { { "Fib", "[15]", "[610]" } } },
    { .name = "control",
      .source = "package facts\nCount :: pure fn(n: int) -> int {\n"
                "    found: int = 0;\n    i: int = 0;\n"
                "    while (true) {\n        i += 1;\n        if (i > n) { "
                "break; }\n"
                "        if (i % 3 == 0 || i % 5 == 0) { continue; } else { "
                "found += 1; }\n    }\n"
                "    return found;\n}",
      .calls = { { "Count", "[15]", "[8]" } } },
    { .name = "logic",
      .source = "package facts\nEven :: pure fn(n: int) -> bool { return !(n % "
                "2 != 0) && n >= 0; }\n"
                "Both :: pure fn(a: int, b: int) -> bool { return Even(a) && "
                "Even(b) || a == b; }",
      .calls = { { "Even", "[4]", "[true]" },
                 { "Even", "[-4]", "[false]" },
                 { "Both", "[2,4]", "[true]" },
                 { "Both", "[3,3]", "[true]" } } },
    { .name = "records",
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; title: string; open: bool; "
                "}\n"
                "Make :: pure fn(x: int, y: int) -> Point {\n"
                "    p: Point = {x: x, y: y, title: \"p\", open: x < y};\n"
                "    return p;\n}\n"
                "Sum :: pure fn(p: Point) -> int { return p.x + p.y; }\n"
                "Moved :: pure fn(p: Point, dx: int) -> Point {\n"
                "    return {title: p.title, open: p.open, y: p.y, x: p.x + "
                "dx};\n}\n"
                "Total :: pure fn(n: int) -> int {\n"
                "    acc: Point = {x: 0, y: 0, title: \"\", open: false};\n"
                "    i: int = 0;\n"
                "    while (i < n) { acc = Moved(acc, i); i += 1; }\n"
                "    first := acc;\n    acc = Moved(acc, 100);\n"
                "    return Sum(acc) + Sum(Make(1, 2)) + first.x;\n}",
      .calls = { { "Make", "[1,2]", "[{\"x\":1,\"y\":2,\"title\":\"p\",\"open\":true}]" },
                 { "Moved",
                   "[{\"x\":1,\"y\":2,\"title\":\"q\",\"open\":false},5]",
                   "[{\"x\":6,\"y\":2,\"title\":\"q\",\"open\":false}]" },
                 { "Total", "[5]", "[123]" } } },
    { .name = "strings",
      .source = "package facts\n"
                "Name :: pure fn(name: string) -> string {\n"
                "    if (name == \"\") { return \"nobody\"; }\n    return "
                "name;\n}\n"
                "Same :: pure fn(a: string, b: string) -> bool { return a == b "
                "&& !(a != b); }\n"
                "Differ :: pure fn(a: string, b: string) -> bool { return a != "
                "b; }\n"
                "Quoted :: pure fn() -> string { text: string = "
                "\"a\\\"b\\u0041\"; "
                "return text; }",
      .calls = { { "Name", "[\"\"]", "[\"nobody\"]" },
                 { "Name", "[\"ada\"]", "[\"ada\"]" },
                 { "Same", "[\"x\",\"x\"]", "[true]" },
                 { "Same", "[\"x\",\"y\"]", "[false]" },
                 { "Differ", "[\"x\",\"y\"]", "[true]" },
                 { "Quoted", "[]", "[\"a\\\"bA\"]" } } },
    { .name = "quoted names",
      .source = "package facts\n"
                "Pair :: struct { \"first\": int; second: int; }\n"
                "Make :: pure fn(\"n\": int) -> Pair {\n"
                "    \"p\": Pair = {\"first\": n, second: n + 1};\n    return "
                "p;\n}\n"
                "Total :: pure fn(p: Pair) -> int { return p.\"first\" + "
                "p.second; }",
      .calls = { { "Make", "[1]", "[{\"first\":1,\"second\":2}]" },
                 { "Total", "[{\"first\":2,\"second\":3}]", "[5]" } } },
    { .name = "arrays",
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Squares :: pure fn(n: int) -> int[] {\n"
                "    out: int[] = [];\n    i: int = 0;\n"
                "    while (i < n) { out = array_add(out, i * i); i += 1; }\n"
                "    return out;\n}\n"
                "Total :: pure fn(xs: int[]) -> int {\n"
                "    sum: int = 0;\n"
                "    foreach (x in xs) { if (x == 4) { continue; } sum += x; "
                "}\n"
                "    return sum;\n}\n"
                "Edit :: pure fn() -> int {\n"
                "    xs: int[] = [1, 2, 3];\n    xs[1] = 20;\n    xs[2] += 5;\n"
                "    copy := xs;\n    more: int[] = [7, 8];\n    xs = "
                "append(xs, more);\n"
                "    return xs[1] + xs[2] + length(xs) * 100 + length(copy) * "
                "1000;\n}\n"
                "Points :: pure fn(n: int) -> Point[] {\n"
                "    ps: Point[] = [];\n    i: int = 0;\n"
                "    while (i < n) { p: Point = {x: i, y: i * 2}; ps = "
                "array_add(ps, p); i += 1; }\n"
                "    return ps;\n}\n"
                "SumY :: pure fn(ps: Point[]) -> int {\n"
                "    total: int = 0;\n"
                "    foreach (p in ps) { total += p.y; if (total > 100) { "
                "break; } }\n"
                "    return total + ps[0].x;\n}\n"
                "Words :: pure fn() -> string[] {\n"
                "    ws: string[] = [\"a\", \"b\", \"c\"];\n    return "
                "slice(ws, 1, 2);\n}\n"
                "Counts :: pure fn(n: int) -> int { return length(Squares(n)) "
                "+ Total(Squares(n)); }\n"
                "Pick :: pure fn(xs: int[], i: int) -> int { return xs[i]; }\n"
                "Index :: pure fn(i: int) -> int { return i; }\n"
                "Nested :: pure fn(ps: Point[]) -> int {\n"
                "    total: int = 0;\n    ws: string[] = [\"a\", \"bb\"];\n"
                "    foreach (p in ps) {\n        if (p.x == 0) { continue; }\n"
                "        foreach (w in ws) { if (w == \"a\") { continue; } "
                "total += p.x * 10; }\n    }\n"
                "    counts: int[] = [0, 0, 0];\n    counts[Index(2)] += 7;\n"
                "    return total + counts[2];\n}",
      .calls = { { "Squares", "[4]", "[[0,1,4,9]]" },
                 { "Total", "[[0,1,4,9]]", "[10]" },
                 { "Edit", "[]", "[3528]" },
                 { "Points", "[3]", "[[{\"x\":0,\"y\":0},{\"x\":1,\"y\":2},{\"x\":2,\"y\":4}]]" },
                 { "SumY", "[[{\"x\":5,\"y\":1},{\"x\":0,\"y\":2}]]", "[8]" },
                 { "Words", "[]", "[[\"b\",\"c\"]]" },
                 { "Counts", "[3]", "[4]" },
                 { "Pick", "[[1,2],2]", NULL },
                 { "Nested",
                   "[[{\"x\":0,\"y\":1},{\"x\":2,\"y\":0},{\"x\":3,\"y\":0}]]",
                   "[57]" } } },
    { .name = "unions",
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Shape :: union { Circle(radius: int) | Box(width: int, "
                "height: int) | Spot(at: Point)\n"
                "    | Named(name: string, sides: int[]) | Empty }\n"
                "Make :: pure fn(kind: int) -> Shape {\n"
                "    if (kind == 0) { return Shape.Circle(3); }\n"
                "    if (kind == 1) { return Shape.Box(2, 5); }\n"
                "    if (kind == 2) { p: Point = {x: 4, y: 6}; return "
                "Shape.Spot(p); }\n"
                "    if (kind == 3) { sides: int[] = [1, 2, 3]; return "
                "Shape.Named(\"tri\", sides); }\n"
                "    return Shape.Empty;\n}\n"
                "Sum :: pure fn(p: Point) -> int { return p.x + p.y; }\n"
                "Area :: pure fn(shape: Shape) -> int {\n"
                "    switch shape {\n"
                "        case .Circle(radius): return radius * radius * 3;\n"
                "        case .Box(width, height): return width * height;\n"
                "        case .Spot(at): total := Sum(at); return total + "
                "at.x;\n"
                "        case .Named(name, sides):\n"
                "            total: int = 0;\n"
                "            foreach (side in sides) { total += side; }\n"
                "            if (name == \"tri\") { total += 100; }\n"
                "            return total;\n"
                "        case .Empty: return -1;\n"
                "    }\n    return -2;\n}\n"
                "Score :: pure fn(n: int) -> int {\n"
                "    total: int = 0;\n    i: int = 0;\n"
                "    while (i < n) { total += Area(Make(i)); i += 1; }\n"
                "    return total;\n}\n"
                "Kinds :: pure fn() -> int {\n"
                "    shapes: Shape[] = [Shape.Circle(1), Shape.Empty, "
                "Shape.Box(1, 1)];\n"
                "    count: int = 0;\n"
                "    foreach (shape in shapes) {\n"
                "        switch shape {\n            case .Empty: count += "
                "10;\n            case .Box: count += 100;\n"
                "            case: count += 1;\n        }\n"
                "    }\n    return count;\n}\n"
                "Pair :: pure fn(a: Shape, b: Shape) -> int {\n"
                "    switch a {\n"
                "        case .Empty:\n"
                "            switch b {\n                case .Empty: return "
                "2;\n                case: return 1;\n            }\n"
                "        case .Circle(radius): return radius;\n"
                "        case .Box(width, height): return width;\n"
                "        case .Spot(at): return at.x;\n"
                "        case .Named(name, sides): return length(sides);\n"
                "    }\n    return -1;\n}\n"
                "Nested :: pure fn() -> int {\n"
                "    return Pair(Shape.Empty, Shape.Empty) * 10 + "
                "Pair(Shape.Empty, Shape.Circle(1)) * 100 + Pair(Shape.Box(3, "
                "4), Shape.Empty);\n}",
      .calls = { { "Score", "[5]", "[156]" },
                 { "Kinds", "[]", "[111]" },
                 { "Nested", "[]", "[123]" },
                 { "Make", "[1]", "[{\"tag\":6300533083688230396,\"values\":[2,5]}]" },
                 { "Make", "[2]", "[{\"tag\":10337135207335990755,\"values\":[{\"x\":4,\"y\":6}]}]" },
                 { "Make", "[3]", "[{\"tag\":16893218631787042844,\"values\":[\"tri\",[1,2,3]]}]" },
                 { "Make", "[4]", "[{\"tag\":16895435817925979816,\"values\":[]}]" } } },
    { .name = "imports",
      .source = "package facts\n"
                "import lib \"project:lib\"\n"
                "import strings \"core:strings\"\n"
                "Total :: pure fn(n: int) -> int { return lib.Twice(n) + "
                "lib.Sum(lib.Make(n, 3)); }\n"
                "Local :: pure fn() -> int { p: lib.Point = {x: 2, y: 5}; "
                "return lib.Sum(p) + p.x; }\n"
                "Shapes :: pure fn() -> int {\n"
                "    return lib.Size(lib.Shape.Dot(lib.Make(1, 2))) + "
                "lib.Size(lib.Shape.Line(7)) * 10 +\n"
                "        lib.Size(lib.Shape.Nothing);\n}\n"
                "Match :: pure fn(n: int) -> int {\n"
                "    shape := lib.Shape.Line(n);\n"
                "    switch shape { case .Line(size): return size; case: "
                "return 0; }\n"
                "    return -1;\n}\n"
                "Measure :: pure fn(s: string) -> int {\n"
                "    return strings.ByteLength(s) * 10 + strings.Compare(s, "
                "\"m\") + lib.Width(s) * 100;\n}\n"
                "Echo :: pure fn(p: lib.Point) -> lib.Point { return p; }\n"
                "Points :: pure fn(n: int) -> lib.Point[] { points: "
                "lib.Point[] = [lib.Make(n, n)]; return points; }",
      .library = "package lib\n"
                 "import text \"core:strings\"\n"
                 "Point :: struct { x: int; y: int; }\n"
                 "Shape :: union { Dot(at: Point) | Line(size: int) | Nothing "
                 "}\n"
                 "Make :: pure fn(x: int, y: int) -> Point { p: Point = {x: x, "
                 "y: y}; return p; }\n"
                 "Sum :: pure fn(p: Point) -> int { return p.x + p.y; }\n"
                 "Twice :: pure fn(n: int) -> int { return Helper(n) * 2; }\n"
                 "Helper :: pure fn(n: int) -> int { return n + 1; }\n"
                 "Width :: pure fn(s: string) -> int { return "
                 "text.ByteLength(s); }\n"
                 "Size :: pure fn(shape: Shape) -> int {\n"
                 "    switch shape {\n"
                 "        case .Dot(at): return Sum(at);\n"
                 "        case .Line(size): return size;\n"
                 "        case .Nothing: return 0;\n"
                 "    }\n    return -1;\n}",
      .core = true,
      .calls = { { "Total", "[4]", "[17]" },
                 { "Local", "[]", "[9]" },
                 { "Shapes", "[]", "[73]" },
                 { "Match", "[6]", "[6]" },
                 { "Measure", "[\"zebra\"]", "[551]" },
                 { "Echo", "[{\"x\":1,\"y\":2}]", "[{\"x\":1,\"y\":2}]" },
                 { "Points", "[3]", "[[{\"x\":3,\"y\":3}]]" } } },
    { .name = "bounds",
      .source = "package facts\nPick :: pure fn(xs: int[], i: int) -> int { "
                "return xs[i]; }",
      .calls = { { "Pick", "[[1,2],1]", "[2]" },
                 { "Pick", "[[1,2],-1]", NULL },
                 { "Pick", "[[],0]", NULL } } },
    { .name = "unknown name",
      .source = "package facts\nAnswer :: pure fn() -> int { return missing; }",
      .message = "Unknown name 'missing'",
      .line = 2,
      .column = 37 },
    { .name = "arity",
      .source = "package facts\nOne :: pure fn(a: int) -> int { return a; "
                "}\nTwo :: pure fn() -> int { return One(); }",
      .message = "call has the wrong number of arguments",
      .line = 3,
      .column = 34 },
    { .name = "types",
      .source = "package facts\nAnswer :: pure fn() -> int { return true; }",
      .message = "returned value differs from the result type",
      .line = 2,
      .column = 30 },
    { .name = "untyped literal",
      .source = "package facts\nPair :: struct { a: int; }\n"
                "One :: pure fn() -> int { p := {a: 1}; return p.a; }",
      .message = "Struct literal needs an explicit record type",
      .line = 3,
      .column = 32 },
    { .name = "unknown field",
      .source = "package facts\nPair :: struct { a: int; }\n"
                "One :: pure fn(p: Pair) -> int { return p.b; }",
      .message = "Unknown field 'b'",
      .line = 3,
      .column = 41 },
    { .name = "missing field",
      .source = "package facts\nPair :: struct { a: int; b: int; }\n"
                "One :: pure fn() -> Pair { return {a: 1}; }",
      .message = "Struct literal needs every field",
      .line = 3,
      .column = 35 },
    { .name = "duplicate member",
      .source = "package facts\nPair :: struct { a: int; b: int; }\n"
                "One :: pure fn() -> Pair { return {a: 1, a: 2}; }",
      .message = "Duplicate literal member 'a'",
      .line = 3,
      .column = 42 },
    { .name = "shared scope",
      .source = "package facts\nPoint :: struct { x: int; }\n"
                "Point :: pure fn() -> int { return 1; }",
      .message = "Duplicate declaration 'Point'",
      .line = 3,
      .column = 1 },
    { .name = "no value",
      .source = "package facts\nNothing :: pure fn() { }\n"
                "One :: pure fn() -> int { x := Nothing(); return 0; }",
      .message = "call has no value",
      .line = 3,
      .column = 27 },
    { .name = "untyped array",
      .source = "package facts\nOne :: pure fn() -> int { xs := [1]; return 0; "
                "}",
      .message = "Array literal needs an explicit array type",
      .line = 2,
      .column = 33 },
    { .name = "unknown function",
      .source = "package facts\nOne :: pure fn() -> int { return Missing(1); }",
      .message = "Unknown function 'Missing'",
      .line = 2,
      .column = 34 },
    { .name = "index type",
      .source = "package facts\nOne :: pure fn(xs: int[]) -> int { return "
                "xs[true]; }",
      .message = "index needs an int",
      .line = 2,
      .column = 46 },
    { .name = "foreach scalar",
      .source = "package facts\nOne :: pure fn(n: int) -> int { foreach (x in "
                "n) { } return 0; }",
      .message = "foreach needs an array",
      .line = 2,
      .column = 47 },
    { .name = "parameter element",
      .source = "package facts\nOne :: pure fn(xs: int[]) -> int { xs[0] = 1; "
                "return 0; }",
      .message = "element assignment needs a local array",
      .line = 2,
      .column = 36 },
    { .name = "variadic native",
      .source = "package facts\nOne :: pure fn() -> string { return "
                "format_text(\"{}\", 1); }",
      .message = "native 'format_text' is not supported yet",
      .line = 2,
      .column = 37 },
    { .name = "inferred mismatch",
      .source = "package facts\nOne :: pure fn(xs: int[]) -> int { ys := "
                "array_add(xs, true); return length(ys); }",
      .message = "argument differs from its parameter type",
      .line = 2,
      .column = 42 },
    { .name = "switch scalar",
      .source = "package facts\nU :: union { A | B }\nOne :: pure fn(n: int) "
                "-> int { switch n { case .A: return 1; } return 0; }",
      .message = "switch needs a union value",
      .line = 3,
      .column = 40 },
    { .name = "unknown variant",
      .source = "package facts\nU :: union { A | B }\nOne :: pure fn() -> U { "
                "return U.C; }",
      .message = "Unknown variant 'C'",
      .line = 3,
      .column = 32 },
    { .name = "partial bindings",
      .source = "package facts\nU :: union { A(x: int, y: int) }\n"
                "One :: pure fn(u: U) -> int { switch u { case .A(x): return "
                "x; } return 0; }",
      .message = "A union payload pattern must bind every field",
      .line = 3,
      .column = 47 },
    { .name = "assigned binding",
      .source = "package facts\nU :: union { A(x: int) }\n"
                "One :: pure fn(u: U) -> int { switch u { case .A(x): x = 2; "
                "return x; } return 0; }",
      .message = "pattern bindings cannot be assigned",
      .line = 3,
      .column = 54 },
    { .name = "missing payload",
      .source = "package facts\nU :: union { A(x: int) }\nOne :: pure fn() -> "
                "U { return U.A; }",
      .message = "variant 'A' needs its payload",
      .line = 3,
      .column = 32 },
    { .name = "unknown imported function",
      .source = "package facts\nimport lib \"project:lib\"\nOne :: pure fn() "
                "-> int { return lib.Missing(1); }",
      .library = "package lib\nTwo :: pure fn() -> int { return 2; }",
      .message = "Unknown function 'Missing'",
      .line = 3,
      .column = 38 },
    { .name = "alias value",
      .source = "package facts\nimport lib \"project:lib\"\nOne :: pure fn() "
                "-> int { return lib; }",
      .library = "package lib\nTwo :: pure fn() -> int { return 2; }",
      .message = "Unknown name 'lib'",
      .line = 3,
      .column = 34 },
    { .name = "non-exhaustive switch",
      .source = "package facts\nU :: union { A | B }\nOne :: pure fn(u: U) -> "
                "int { switch u { case .A: return 2; } return 1; }",
      .message = "Non-exhaustive switch: a union alternative is missing",
      .line = 3,
      .column = 31 },
    { .name = "duplicate arm",
      .source = "package facts\nU :: union { A | B }\n"
                "One :: pure fn(u: U) -> int { switch u { case .A: return 2; "
                "case .A: return 3; case .B: return 4; } return 1; }",
      .message = "Unreachable duplicate union pattern",
      .line = 3,
      .column = 66 },
    { .name = "duplicate default",
      .source = "package facts\nU :: union { A | B }\n"
                "One :: pure fn(u: U) -> int { switch u { case: return 2; "
                "case: return 3; } return 1; }",
      .message = "Duplicate default arm",
      .line = 3,
      .column = 58 },
    { .name = "duplicate field",
      .source = "package facts\nPair :: struct { a: int; a: bool; }",
      .message = "Duplicate field 'a'",
      .line = 2,
      .column = 26 },
    { .name = "embedded NUL",
      .source = "package facts\nBad :: pure fn() -> string { return "
                "\"a\\u0000b\"; }",
      .message = "Invalid string or embedded NUL",
      .line = 2,
      .column = 37 },
};
enum
{
    SAMPLES = sizeof samples / sizeof *samples
};
static compiler_fixture fixture, oracle;
static native_capabilities *linked;
/* Compile's native tables per compiler backend, built once from `linked`. */
static pin_value linked_tables[2][3];
static arena input, detached;
static compiler_host *host;
static compiler_host_result result;

static void cleanup()
{
    compiler_fixture_free(&fixture);
    compiler_fixture_free(&oracle);
    native_capabilities_free(linked);
    linked = NULL;
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
    size_t bytes = strlen(s) + 1;
    char *copy = arena_alloc(&input, bytes);
    memcpy(copy, s, bytes);
    return (pin_value){ .type = PIN_STR, .str = copy };
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
    pin_write_field(row->ptr, field, (pin_value){ .type = PIN_I32, .i = value });
    return true;
}
static bool put_value(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    pin_write_field(row->ptr, field, value);
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
/* A port's storage as Compile takes it: 0 for a type the call infers, -1 for
 * one the compiler cannot pass. */
static int port_storage(script_type type)
{
    unsigned inferred = SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_GENERIC;
    if (type.flags & SCRIPT_TYPE_UNTYPED) {
        return type.flags & ~inferred ? -1 : 0;
    }
    return type.flags || type.nominal || type.storage == PIN_FLOW ? -1
                                                                  : (int)type.storage;
}
/* The data functions the compiler host links: no flow, outcomes or
 * suspension. */
static bool natives(int b, pin_value *out)
{
    if (linked_tables[b][0].ptr) {
        memcpy(out, linked_tables[b], sizeof linked_tables[b]);
        return true;
    }
    if (!linked) {
        const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                                 NATIVE_ARRAY_DEFINITION_PACK,
                                                 COMPILER_JSON_DEFINITION_PACK,
                                                 COMPILER_SYNTAX_DEFINITION_PACK };
        char error[256];
        linked = native_capabilities_from_definition_packs(NULL, 0, packs, 4, error, sizeof error);
        REQUIRE(linked);
    }
    const script_catalog *catalog = native_capabilities_catalog(linked);
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
            script_type type = j < c->parameter_count
                                 ? c->parameters[j].type
                                 : c->results[j - c->parameter_count].type;
            pin_value port = row(b, "Port");
            REQUIRE(put(&port, "storage", port_storage(type)));
            REQUIRE(put(&port, "container", (int)type.container));
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
    memcpy(linked_tables[b], out, sizeof linked_tables[b]);
    afree(functions);
    afree(ports);
    afree(identities);
    return true;
}
static bool get(const pin_value *row, const char *name, pin_value *out)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    pin_read_field(row->ptr, field, out);
    return true;
}
static uint64_t tag(int b, const char *variant)
{
    const authoring_package *owner = compiler_fixture_package(&fixture, b, PACKAGE);
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(&fixture, b, PACKAGE, "Result")
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, variant)) {
            return type->variants[i].tag;
        }
    }
    return 0;
}
static bool prepare()
{
    const source_package_input sources[] = { PACKAGE_COMPILER_SOURCE_INPUTS };
    native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                       COMPILER_SYNTAX_DEFINITION_PACK };
    REQUIRE(
        compiler_fixture_prepare_packs(&fixture, sources, (int)(sizeof sources / sizeof *sources), packs, 2)
    );
    return true;
}
/* The sample's files: package facts, then lib and core:strings when it
 * uses them. */
static int sample_inputs(const sample *s, source_package_input inputs[3])
{
    int count = 0;
    inputs[count++] = (source_package_input){ "facts/Main.chibi",
                                              s->source,
                                              strlen(s->source) };
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
static bool compile(int b, const sample *s, pin_value *out)
{
    const char *collection_names[] = { "core" };
    const char *collection_paths[] = { "/sdk/core" };
    source_package_input inputs[3];
    int count = sample_inputs(s, inputs);
    const char *paths[3], *sources[3];
    for (int i = 0; i < count; i++) {
        paths[i] = inputs[i].path;
        sources[i] = inputs[i].text;
    }
    pin_value args[11];
    args[0] = text("/project");
    args[1] = strings(collection_names, 1);
    args[2] = strings(collection_paths, 1);
    args[3] = strings(paths, count);
    args[4] = strings(sources, count);
    const pin_record *record = compiler_fixture_record(&fixture, b, PACKAGE, "Target", &input);
    REQUIRE(record);
    args[5] = (pin_value){ .type = PIN_STRUCT,
                           .ptr = arena_alloc(&input, record->type.size),
                           .record = record,
                           .type_name = record->type.name };
    memset(args[5].ptr, 0, record->type.size);
    REQUIRE(put(&args[5], "pointer_storage", BF_POINTER_TYPE));
    REQUIRE(put(&args[5], "pointer_size", (int)sizeof(void *)));
    REQUIRE(put(&args[5], "value_size", (int)sizeof(pin_value)));
    REQUIRE(put(&args[5], "payload_offset", (int)offsetof(pin_value, d)));
    REQUIRE(put(&args[5], "results_offset", (int)offsetof(behavior_function_context, results)));
    REQUIRE(put(&args[5], "type_offset", (int)offsetof(pin_value, type)));
    REQUIRE(put(&args[5], "container_offset", (int)offsetof(pin_value, container)));
    REQUIRE(put(&args[5], "name_offset", (int)offsetof(pin_value, type_name)));
    REQUIRE(put(&args[5], "record_offset", (int)offsetof(pin_value, record)));
    REQUIRE(put(&args[5], "count_offset", (int)offsetof(pin_array, count)));
    REQUIRE(put(&args[5], "items_offset", (int)offsetof(pin_array, items)));
    REQUIRE(put(&args[5], "data_failure", BEHAVIOR_FUNCTION_HOST_DATA));
    REQUIRE(put(&args[5], "argument_failure", BEHAVIOR_FUNCTION_ARGUMENT));
    args[6] = (pin_value){ .type = PIN_U64, .u64 = execution_prepared_abi() };
    int instructions = execution_prepared_instruction_count();
    pin_array *names = pin_array_snapshot_allocate(&input, PIN_STR, NULL, instructions);
    for (int i = 0; i < instructions; i++) {
        names->items[i] = text(execution_prepared_instruction_name(i));
    }
    args[7] = (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = names };
    REQUIRE(natives(b, &args[8]));
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, "Compile", args, 11, COMPILE_BUDGET, &detached, out)
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
    host = compiler_host_load(bytes, length, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE, error, sizeof error);
    if (!host) {
        fprintf(stderr, "package compiler load: %s\n", error);
    }
    REQUIRE(host);
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
/* Stage zero's closure of the same packages, whose calls must give the same
 * results. */
static bool oracle_closure(const sample *s, unsigned char **closure, size_t *length)
{
    source_package_input inputs[3];
    int count = sample_inputs(s, inputs);
    compiler_fixture_free(&oracle);
    REQUIRE(compiler_fixture_prepare(&oracle, inputs, count));
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
TEST_CASE(package_compiler_builds_runnable_packages)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < (int)SAMPLES; i++) {
            const sample *s = &samples[i];
            pin_value out;
            REQUIRE(compile(b, s, &out));
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
                if (strcmp(message.str, s->message) || line.i != s->line ||
                    column.i != s->column) {
                    fprintf(
                        stderr,
                        "package compiler %s: %s at %d:%d\n",
                        s->name,
                        message.str,
                        line.i,
                        column.i
                    );
                }
                REQUIRE(
                    !strcmp(message.str, s->message) && line.i == s->line &&
                    column.i == s->column
                );
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
                    message.str,
                    line.i,
                    column.i
                );
            }
            REQUIRE(value->tag == tag(b, "Compiled") && value->count == 2);
            const pin_array *array = value->values[1].ptr;
            size_t length = (size_t)array->count;
            unsigned char *bytes = xmalloc(length);
            for (size_t k = 0; k < length; k++) {
                bytes[k] = array->items[k].u8;
            }
            arena_free(&detached);
            unsigned char *expected = NULL;
            size_t expected_length = 0;
            bool ran = b || oracle_closure(s, &expected, &expected_length);
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
    }
    return true;
}
void test_language_package_compiler_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_packages);
    pu_clear_setup();
}
