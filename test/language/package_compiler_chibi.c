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
#include "support/asset_json.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/copy.h"
#include "types/union.h"
#include "types/value_document.h"

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
    ARGUMENTS = 11,
    COMPILE_BUDGET = 20000000,
    SELF_BUDGET = 2000000000,
    TARGET_BUDGET = 100000
};
/* A NULL expected result means the call must fail. */
typedef struct
{
    const char *function, *arguments, *expected;
} sample_call;
/* Each group of samples runs under its own selector, so each selector stays
 * within its time ceiling: unions, constants, patterns and integer types
 * (TYPE_SAMPLES), and the imports sample, which compiles all of core:strings
 * (IMPORT_SAMPLES). */
typedef enum
{
    BASE_SAMPLES,
    TYPE_SAMPLES,
    IMPORT_SAMPLES
} sample_group;
/* `source` is package facts; `library` is package lib when present, and
 * `core` adds core:strings. An `updates` sample's `Build(count)` updates a
 * local array in place: doubling its count at most doubles the values it
 * copies, where copying the array on each update would quadruple them. A
 * `boxed` sample's Boxed functions read only arrays that are never packed,
 * and its Packed functions arrays that may be. */
typedef struct
{
    const char *name, *source, *library;
    sample_group group;
    bool core, updates, boxed;
    sample_call calls[CALLS];
    const char *message;
    int line, column;
} sample;
static const sample samples[] = {
    { .name = "constant",
      .source = "package facts\nAnswer :: () -> int { return 42; }",
      .calls = { { "Answer", "[]", "[42]" } } },
    { .name = "parameters",
      .source = "package facts\nAdd :: (a: int, b: int) -> int { return "
                "a + b * 2 - (a - b) % 3; }",
      .calls = { { "Add", "[3,4]", "[12]" }, { "Add", "[-5,2]", "[0]" } } },
    { .name = "loops",
      .source = "package facts\nSum :: (n: int) -> int {\n"
                "    total: int = 0;\n    i: int = 0;\n"
                "    while (i < n) { total += i; i += 1; }\n    return "
                "total;\n}",
      .calls = { { "Sum", "[10]", "[45]" }, { "Sum", "[0]", "[0]" } } },
    { .name = "recursion",
      .source = "package facts\nFib :: (n: int) -> int {\n"
                "    if (n < 2) { return n; }\n    return Fib(n - 1) + Fib(n - "
                "2);\n}",
      .calls = { { "Fib", "[15]", "[610]" } } },
    { .name = "control",
      .source = "package facts\nCount :: (n: int) -> int {\n"
                "    found: int = 0;\n    i: int = 0;\n"
                "    while (true) {\n        i += 1;\n        if (i > n) { "
                "break; }\n"
                "        if (i % 3 == 0 || i % 5 == 0) { continue; } else { "
                "found += 1; }\n    }\n"
                "    return found;\n}",
      .calls = { { "Count", "[15]", "[8]" } } },
    { .name = "logic",
      .source = "package facts\nEven :: (n: int) -> bool { return !(n % "
                "2 != 0) && n >= 0; }\n"
                "Both :: (a: int, b: int) -> bool { return Even(a) && "
                "Even(b) || a == b; }",
      .calls = { { "Even", "[4]", "[true]" },
                 { "Even", "[-4]", "[false]" },
                 { "Both", "[2,4]", "[true]" },
                 { "Both", "[3,3]", "[true]" } } },
    { .name = "records",
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
      .calls = { { "Make", "[1,2]", "[{\"x\":1,\"y\":2,\"title\":\"p\",\"open\":true}]" },
                 { "Moved",
                   "[{\"x\":1,\"y\":2,\"title\":\"q\",\"open\":false},5]",
                   "[{\"x\":6,\"y\":2,\"title\":\"q\",\"open\":false}]" },
                 { "Total", "[5]", "[123]" } } },
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
      .calls = { { "Name", "[\"\"]", "[\"nobody\"]" },
                 { "Name", "[\"ada\"]", "[\"ada\"]" },
                 { "Same", "[\"x\",\"x\"]", "[true]" },
                 { "Same", "[\"x\",\"y\"]", "[false]" },
                 { "Differ", "[\"x\",\"y\"]", "[true]" },
                 { "Quoted", "[]", "[\"a\\\"bA\"]" } } },
    { .name = "arrays",
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Squares :: (n: int) -> int[] {\n"
                "    out: int[] = [];\n    i: int = 0;\n"
                "    while (i < n) { out = array_add(out, i * i); i += 1; }\n"
                "    return out;\n}\n"
                "Total :: (xs: int[]) -> int {\n"
                "    sum: int = 0;\n"
                "    for (x in xs) { if (x == 4) { continue; } sum += x; "
                "}\n"
                "    return sum;\n}\n"
                "Edit :: () -> int {\n"
                "    xs: int[] = [1, 2, 3];\n    xs[1] = 20;\n    xs[2] += 5;\n"
                "    copy := xs;\n    more: int[] = [7, 8];\n    xs = "
                "append(xs, more);\n"
                "    return xs[1] + xs[2] + length(xs) * 100 + length(copy) * "
                "1000;\n}\n"
                "Points :: (n: int) -> Point[] {\n"
                "    ps: Point[] = [];\n    i: int = 0;\n"
                "    while (i < n) { p: Point = {x: i, y: i * 2}; ps = "
                "array_add(ps, p); i += 1; }\n"
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
      .calls = { { "Score", "[5]", "[156]" },
                 { "Kinds", "[]", "[111]" },
                 { "Nested", "[]", "[123]" },
                 { "Make", "[1]", "[{\"tag\":6300533083688230396,\"values\":[2,5]}]" },
                 { "Make", "[2]", "[{\"tag\":10337135207335990755,\"values\":[{\"x\":4,\"y\":6}]}]" },
                 { "Make", "[3]", "[{\"tag\":16893218631787042844,\"values\":[\"tri\",[1,2,3]]}]" },
                 { "Make", "[4]", "[{\"tag\":16895435817925979816,\"values\":[]}]" } } },
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
                 "Point :: struct { x: int; y: int; }\n"
                 "Shape :: union { Dot(at: Point) | Line(size: int) | Nothing "
                 "}\n"
                 "Make :: (x: int, y: int) -> Point { p: Point = {x: x, "
                 "y: y}; return p; }\n"
                 "Sum :: (p: Point) -> int { return p.x + p.y; }\n"
                 "Twice :: (n: int) -> int { return Helper(n) * 2; }\n"
                 "Helper :: (n: int) -> int { return n + 1; }\n"
                 "Width :: (s: string) -> int { return "
                 "text.ByteLength(s); }\n"
                 "Size :: (shape: Shape) -> int {\n"
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
                "Scale :: () -> int { return lib.Scaled(2); }",
      .library = "package lib\n"
                 "Step: int : 100;\n"
                 "Low: int : -2147483648;\n"
                 "High: int : 2147483647;\n"
                 "Scaled :: (n: int) -> int { return n * Step; }",
      .calls = { { "Sum", "[3]", "[148]" },
                 { "Hidden", "[]", "[1]" },
                 { "Limits", "[]", "[-1]" },
                 { "Scale", "[]", "[200]" } } },
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
      .calls = { { "Score", "[0]", "[5]" },
                 { "Score", "[1]", "[100]" },
                 { "Score", "[2]", "[34]" },
                 { "Score", "[3]", "[-1]" },
                 { "Score", "[4]", "[0]" },
                 { "Kind", "[2]", "[7]" },
                 { "Kind", "[4]", "[-5]" },
                 { "Count", "[]", "[10]" },
                 { "Walk", "[]", "[33]" },
                 { "Empty", "[4]", "[true]" } } },
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
      .calls = { { "Shadow", "[3]", "[13]" }, { "Arm", "[]", "[2]" } } },
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
                "    out = array_add(out, byte(wrap: 300));\n"
                "    out = array_add(out, Small);\n"
                "    return out;\n}\n"
                "High :: (n: int) -> int { return int(wrap: (Mask >> "
                "56) + uint64(n)); }\n"
                "Mixed :: (a: int, b: int64) -> int64 { return a + b; "
                "}\n"
                "Store :: (a: int) -> int64 { x: int64 = a; x += 10; x "
                "*= 3; return x; }\n"
                "Narrow :: (v: int64) -> int { return int(v); }\n"
                "Clamp :: (v: int) -> byte { return byte(clamp: v); }",
      .calls = { { "Hash", "[[1,2,3]]", "[11570874782335668893]" },
                 { "Bytes", "[]", "[[1,2,255,44,200]]" },
                 { "High", "[1]", "[256]" },
                 { "Mixed", "[2,5000000000]", "[5000000002]" },
                 { "Store", "[4]", "[42]" },
                 { "Narrow", "[7]", "[7]" },
                 { "Narrow", "[5000000000]", NULL },
                 { "Clamp", "[300]", "[255]" },
                 { "Clamp", "[-5]", "[0]" } } },
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
      .calls = { { "Shifts", "[1]", "[16]" },
                 { "Equal", "[3,3]", "[true]" },
                 { "Twice", "[]", "[-10000000000]" },
                 { "Negative", "[-1]", "[true]" },
                 { "Pick", "[5]", "[5000000035]" } } },
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
      .calls = { { "Sizes", "[3]", "[73]" },
                 { "Words", "[]", "[3]" },
                 { "Points", "[]", "[4]" },
                 { "Wide", "[]", "[257]" } } },
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
      .calls = { { "BoxedWord", "[[\"a\",\"b\",\"c\"],1]", "[\"b\"]" },
                 { "BoxedWord", "[[\"a\"],1]", NULL },
                 { "BoxedWord", "[[\"a\"],-1]", NULL },
                 { "BoxedWords", "[[\"a\",\"b\",\"b\"]]", "[2]" },
                 { "BoxedNotes", "[[{\"text\":\"x\",\"count\":1},{\"text\":\"y\",\"count\":2}],1]", "[203]" },
                 { "BoxedNotes", "[[{\"text\":\"x\",\"count\":1}],1]", NULL },
                 { "BoxedShapes", "[2]", "[517]" },
                 { "BoxedShapes", "[1]", "[17]" },
                 { "BoxedShapes", "[3]", NULL },
                 { "PackedPoints", "[[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}],1]", "[306]" } } },
    { .name = "formatting",
      .source = "package facts\n"
                "Label :: (n: int, name: string) -> string { return "
                "format_text(\"{}:{}\", n, name); }\n"
                "Plain :: () -> string { return format_text(\"plain\"); "
                "}\n"
                "Mixed :: (flag: bool) -> string { big: uint64 = "
                "0xFFFFFFFFFFFFFFFF; return format_text(\"{} {} {}\", flag, "
                "big, int64(-3)); }",
      .calls = { { "Label", "[3,\"x\"]", "[\"3:x\"]" },
                 { "Plain", "[]", "[\"plain\"]" },
                 { "Mixed", "[true]", "[\"true 18446744073709551615 -3\"]" } } },
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
      .calls = { { "Decode", "[\"\\\"a\\\\u00e9\\\"\"]", "[\"a\303\251/3\"]" },
                 { "Decode", "[\"bad\"]", "[\"?\"]" },
                 { "Valid", "[\"\\\"x\\\"\"]", "[true]" },
                 { "Length", "[\"\\\"abc\\\"\"]", "[3]" } } },
    { .name = "written parameters",
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Grow :: (xs: int[], n: int) -> int[] { xs = "
                "array_add(xs, n); xs[0] = 9; return xs; }\n"
                "Swap :: (p: Point, q: Point) -> int { p = q; return "
                "p.x * 10 + q.y; }\n"
                "Caller :: () -> int {\n"
                "    xs: int[] = [1, 2];\n"
                "    ys := Grow(xs, 5);\n"
                "    a: Point = {x: 1, y: 2};\n"
                "    b: Point = {x: 3, y: 4};\n"
                "    return xs[0] * 1000 + length(ys) * 100 + ys[0] * 10 + "
                "Swap(a, b) - a.x * 34;\n}",
      .calls = { { "Caller", "[]", "[1390]" } } },
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
      .calls = { { "Fields", "[4]", "[\"a 10 true 6 #7 7\"]" } } },
    { .name = "record push",
      .source = "package facts\nNode :: struct { kind: string; start: int; }\n"
                "Build :: (count: int) -> int {\n"
                "    nodes: Node[] = [];\n"
                "    index: int = 0;\n"
                "    while (index < count) {\n"
                "        node: Node = {kind: \"a\", start: index};\n"
                "        nodes = array_add(nodes, node);\n"
                "        index += 1;\n"
                "    }\n"
                "    return length(nodes);\n}",
      .updates = true,
      .calls = { { "Build", "[3]", "[3]" } } },
    { .name = "extension",
      .source = "package facts\nBuild :: (count: int) -> int {\n"
                "    xs: int[] = [];\n"
                "    index: int = 0;\n"
                "    while (index < count) {\n"
                "        xs = append(xs, [index, index]);\n"
                "        index += 1;\n"
                "    }\n"
                "    return length(xs);\n}",
      .updates = true,
      .calls = { { "Build", "[3]", "[6]" } } },
    { .name = "bounds",
      .source = "package facts\nPick :: (xs: int[], i: int) -> int { "
                "return xs[i]; }",
      .calls = { { "Pick", "[[1,2],1]", "[2]" },
                 { "Pick", "[[1,2],-1]", NULL },
                 { "Pick", "[[],0]", NULL } } },
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
    { .name = "range for",
      .source = "package facts\nOne :: () -> int { for i in 0..3 { } return 0; "
                "}",
      .message = "for loops support only `for name in array`",
      .line = 2,
      .column = 20 },
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
                "array_add(xs, true); return length(ys); }",
      .message = "argument differs from its parameter type",
      .line = 2,
      .column = 35 },
    { .name = "switch scalar",
      .source = "package facts\nU :: union { A | B }\nOne :: (n: int) "
                "-> int { switch n { case .A: return 1; } return 0; }",
      .message = "switch needs a union value",
      .line = 3,
      .column = 33 },
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
    { .name = "unknown imported function",
      .source = "package facts\nimport lib \"project:lib\"\nOne :: () "
                "-> int { return lib.Missing(1); }",
      .library = "package lib\nTwo :: () -> int { return 2; }",
      .message = "Unknown function 'Missing'",
      .line = 3,
      .column = 31 },
    { .name = "alias conflict",
      .source = "package facts\nimport lib \"project:lib\"\nlib :: () "
                "-> int { return 1; }",
      .library = "package lib\nTwo :: () -> int { return 2; }",
      .message = "Package import alias conflicts with a package declaration",
      .line = 2,
      .column = 1 },
    { .name = "alias value",
      .source = "package facts\nimport lib \"project:lib\"\nOne :: () "
                "-> int { return lib; }",
      .library = "package lib\nTwo :: () -> int { return 2; }",
      .message = "Unknown name 'lib'",
      .line = 3,
      .column = 27 },
    { .name = "constant type",
      .source = "package facts\nOn: bool : true;",
      .message = "constant type is not supported yet",
      .line = 2,
      .column = 5 },
    { .name = "constant expression",
      .source = "package facts\nA: int : 1 + 2;",
      .message = "constant value must be an integer literal",
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
    { .name = "unsigned subtraction",
      .source = "package facts\nOne :: (a: uint64, b: uint64) -> uint64 "
                "{ return a - b; }",
      .message = "unsupported arithmetic type",
      .line = 2,
      .column = 50 },
    { .name = "byte arithmetic",
      .source = "package facts\nOne :: (a: byte) -> byte { return a + "
                "1; }",
      .message = "unsupported arithmetic type",
      .line = 2,
      .column = 35 },
    { .name = "unsigned ordering",
      .source = "package facts\nOne :: (a: uint64, b: uint64) -> bool { "
                "return a < b; }",
      .message = "unsupported arithmetic type",
      .line = 2,
      .column = 48 },
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
    { .name = "untyped constant",
      .source = "package facts\nTwo :: 2;",
      .message = "an untyped number is a double; give the constant an integer "
                 "type",
      .line = 2,
      .column = 8 },
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
    { .name = "embedded NUL",
      .source = "package facts\nBad :: () -> string { return "
                "\"a\\u0000b\"; }",
      .message = "Invalid string or embedded NUL",
      .line = 2,
      .column = 30 },
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
            const script_catalog_parameter *declared = j < c->parameter_count
                                                         ? &c->parameters[j]
                                                         : &c->results[j - c->parameter_count];
            script_type type = declared->type;
            pin_value port = row(b, "Port");
            REQUIRE(put_value(&port, "name", text(declared->name ? declared->name : "")));
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
static pin_value files(const source_package_input *inputs, int count, bool sources)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = text(sources ? inputs[i].text : inputs[i].path);
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
/* Compile's arguments for `inputs`: the project, the core collection, the
 * files, this host's target and the natives it links. */
static bool arguments(int b, const source_package_input *inputs, int count, pin_value args[ARGUMENTS])
{
    const char *collection_names[] = { "core" };
    const char *collection_paths[] = { "/sdk/core" };
    args[0] = text("/project");
    args[1] = strings(collection_names, 1);
    args[2] = strings(collection_paths, 1);
    args[3] = files(inputs, count, false);
    args[4] = files(inputs, count, true);
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
    REQUIRE(put(&args[5], "data_offset", (int)offsetof(pin_array, data)));
    REQUIRE(put(&args[5], "stride_offset", (int)offsetof(pin_array, stride)));
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
    return true;
}
static bool compile(int b, const source_package_input *inputs, int count, uintptr_t budget, pin_value *out)
{
    pin_value args[ARGUMENTS];
    REQUIRE(arguments(b, inputs, count, args));
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
    /* The natives the stage-one target links. */
    native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                       COMPILER_SYNTAX_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&oracle, inputs, count, packs, 2));
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
    host = compiler_host_load(bytes, length, BEHAVIOR_SLJIT, error, sizeof error);
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
static bool run_samples(sample_group group)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < (int)SAMPLES; i++) {
            const sample *s = &samples[i];
            if (s->group != group) {
                continue;
            }
            source_package_input inputs[3];
            int count = sample_inputs(s, inputs);
            pin_value out;
            REQUIRE(compile(b, inputs, count, COMPILE_BUDGET, &out));
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
            size_t length;
            unsigned char *bytes = closure_bytes(value, &length);
            arena_free(&detached);
            unsigned char *expected = NULL;
            size_t expected_length = 0;
            bool ran = (!s->updates || updates_in_place(bytes, length, s->name)) &&
                       (!s->boxed || reads_boxed(bytes, length)) &&
                       (b || oracle_closure(s, &expected, &expected_length));
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
TEST_CASE(package_compiler_builds_runnable_packages)
{
    return run_samples(BASE_SAMPLES);
}
TEST_CASE(package_compiler_builds_runnable_typed_packages)
{
    return run_samples(TYPE_SAMPLES);
}
TEST_CASE(package_compiler_builds_runnable_importing_packages)
{
    return run_samples(IMPORT_SAMPLES);
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
/* Stage two, loaded in `host`, compiles every sample as stage one does: its
 * Compile result projects to the same JSON, the same closure or the same
 * rejection. */
static bool stage_two_samples()
{
    uint64_t entry = script_catalog_id("package-symbol", "project:package_compiler:1:Compile:callable");
    for (int i = 0; i < (int)SAMPLES; i++) {
        source_package_input inputs[3];
        int count = sample_inputs(&samples[i], inputs);
        pin_value args[ARGUMENTS], out;
        REQUIRE(arguments(1, inputs, count, args));
        REQUIRE(
            compiler_fixture_call(&fixture, 1, PACKAGE, "Compile", args, ARGUMENTS, COMPILE_BUDGET, &detached, &out)
        );
        size_t length, expected_length;
        char *json = document_json(args, ARGUMENTS, &length);
        char *expected = document_json(&out, 1, &expected_length);
        arena_free(&detached);
        bool called = compiler_host_invoke(host, entry, json, length, SELF_BUDGET, &result);
        bool same = called && result.flow_status == FLOW_OK &&
                    result.length == expected_length &&
                    !memcmp(result.json, expected, expected_length);
        if (!same) {
            fprintf(
                stderr,
                "package compiler stage two %s: %s\n",
                samples[i].name,
                called ? result.json : result.diagnostic
            );
        }
        free(json);
        free(expected);
        free(result.json);
        result = (compiler_host_result){ 0 };
        REQUIRE(same);
    }
    return true;
}
/* Stage zero prepares stage one, which compiles its own packages to a closure:
 * stage two. Stage two, loaded by the compiler host, compiles the same
 * packages to the same bytes, and every sample as stage one does. */
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
    REQUIRE(arguments(1, inputs, count, args));
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
            message.str,
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
    host = compiler_host_load(stage_two, length, BEHAVIOR_SLJIT, error, sizeof error);
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
    return stage_two_samples();
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
void test_language_package_compiler_chibi_bootstrap()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_compiles_itself);
    pu_clear_setup();
}
