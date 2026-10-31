#include "package_compiler_chibi.h"

#include "backend.h"
#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/module_internal.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "float_conversion_cases.h"
#include "package_compiler_chibi_source.h"
#include "runtime/arithmetic_definition.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/math_definition.h"
#include "runtime/native_capabilities.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
#include "runtime/text_format.h"
#include "runtime/vectors_definition.h"
#include "scalar_host.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_target.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"
#include "types/value_document.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__) && defined(__x86_64__)
#include "host/native_libraries.h"
#include "host/process.h"
#include "host/stack_overflow.h"

#include <dlfcn.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
extern char **environ;
#endif

#ifndef CHIBI_CORE_MATH_ROOT
#define CHIBI_CORE_MATH_ROOT "packages/core/math"
#endif

/* The authored package compiler, prepared by stage zero, compiles each sample
 * package, with the packages it imports, to a closure against the natives the
 * compiler host links. A fresh runtime host loads it and calls every function
 * on bytecode and SLJIT, and stage zero's own closure of the same packages
 * must give the same JSON result. Rejections report their exact message and
 * position. */

enum
{
    CALLS = 14,
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

/* Focused selectors group unions, constants, patterns and integer types
 * (TYPE_SAMPLES); imports, core-pack calls, moves, reads in place and array
 * methods (IMPORT_SAMPLES, run in Release); and maps, references, arrays and
 * ranges (COLLECTION_SAMPLES). */

/* `source` is package facts, and `more` another file of it when present;
 * `library` is package lib when present, and `core` adds core:strings.
 * `collection_source` and `collection_dependency` add core:host_only and
 * core:host_dependency; `project_modules` checks that the emitted closure
 * contains exactly that many project modules and no collection rows. A
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
 * Copied function, which assigns its array, does not. A `lends` sample's Lent
 * functions pass locals to natives and keep or return natives' results
 * without a copy,
 * and its Copied function copies the element it passes. A `drops` sample's
 * Plain functions pass temporaries that hold no collection and drop none, and
 * its Held functions drop the temporary arrays they pass. A `debug_metadata`
 * sample compares source points and watches with stage zero on both compiler
 * backends. A `traced` sample's calls take no arguments and deliver the
 * watches stage zero's do, in its order and with its values; a traced
 * `c_string_probe` sample's calls run only that way, because the compiler
 * host does not link the probe natives, and return int. */

/* The second parameter deliberately carries the C-string boundary. */

/* Overloads that report which one ran: number or f64, and an int and an f64
 * in either order. */

/* Borrowed native result banks exercise tuple copies without authored named
 * collection results, which stage one does not support. */

#define PROBE_DEFINITION(CANONICAL, SOURCE, INPUT, PURE)                       \
    { .canonical_name = CANONICAL,                                             \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = INPUT,                                                         \
      .input_count = sizeof INPUT / sizeof *INPUT,                             \
      .outputs = CSTRING_OUTPUT,                                               \
      .output_count = 1,                                                       \
      .completion_output = -1,                                                 \
      .pure = PURE }

#undef PROBE_DEFINITION

#define PROBE_BINDING(CANONICAL, CALL, ACCESS)                                 \
    {                                                                          \
        .canonical_name = CANONICAL, .implementation = {                       \
            .call = &CALL,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }

#undef PROBE_BINDING

/* default_pair's overloads: an int then an f64 that defaults to 5, and the
 * reverse. Each reports which one ran. */

#define UNCHECKED_COLLECTION_ERROR_BODY                                        \
    "@error pub Failure :: struct { code: int; }\n"                            \
    "pub Read :: () -> Failure? { return {code: 3}; }\n"                       \
    "pub Broken :: () { Read(); }\n"

#undef UNCHECKED_COLLECTION_ERROR_BODY

/* scalar_llvm's IR of `rows` for native_abi's target `function`; *ir is NULL
 * when it rejects them. */
static bool target_ir(const char *function, const pin_value *rows, char **ir)
{
    REQUIRE(scalar_host_emit(function, rows, ir));
    if (!**ir) {
        free(*ir);
        *ir = NULL;
    }
    return true;
}
/* Source-level contracts use literal answers. They do not ask the constant
 * evaluator or either compiler to calculate the expected result. */

/* The rows and sources a test reads live in `detached`; `input` holds what a
 * test builds for a native call. */
static arena input, detached;

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
}

static pin_value text(const char *s)
{
    pin_value value = { 0 };
    assert(!pin_value_text(&input, &value, s, strlen(s)));
    return value;
}

/* Writes an int or int64 field. */

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
/* The data functions the compiler host links: no flow, outcomes or
 * suspension. It links the compiler's text profile, which the compiler is
 * compiled for; a game links the public text pack, which also converts bytes
 * to text. */

static bool get(const pin_value *row, const char *name, pin_value *out)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, out));
    return true;
}

static bool prepare()
{
    return scalar_host_open();
}
/* The sample's files: package facts, then lib and core:strings when it
 * uses them. */

/* This host's target, as Compile and CollectScalar take it. */

/* The names of this host's prepared instructions, in their order. */

enum
{
    SCALAR_ARGUMENTS = 8
};
/* CollectScalar's arguments for `source` at `path` on VM b: the project, the
 * path and source, this host's target and instruction names, and the
 * production natives it links. */

/* Compile's arguments for `inputs`: the project, the core collection, the
 * files, debugger points and watches, this host's target and the natives it
 * links: with `game`, also the core pack a game links. */

/* Ordinary compilation and bootstrap use only production host natives. */

/* Math packages link the ordinary native libm pack, outside the compiler
 * host's intentionally restricted JSON/text/array execution profile. */

/* Whether a point is one this compiler places as stage zero does: a
 * function's entry and completions, each statement's, each evaluated
 * expression's and each symbol's it reads or stores. */

/* Each function of stage one's module has stage zero's function and
 * statement points, as many of each, in any order. */

/* Each function of stage one's module has stage zero's watches, as many of
 * each, in any order. */

/* Stage zero's closure of the same packages, whose calls must give the same
 * results. */

/* Own the compiler's detached byte output after its result arena is retired. */

/* Compile the discovered test package with each compiler backend, then run
 * the same fixed assertions on each independently loaded target backend. */

/* The values `Build(count)` copies in `host`. */

/* An intrinsic's capability ID is "CHII" and its number. */

/* How many of `f`'s instructions load an array's stride. */

/* An array of strings, unions or records with a string field is never packed,
 * so stage one reads its elements from `items` alone, or through the sequence
 * intrinsic; any other array may be, so its reads in place still choose the
 * representation by its stride. In the root module, only the Packed functions
 * load a stride. */

/* A literal or an authored function's result is new in the function's
 * storage, so stage one moves it into a local or a return without a snapshot;
 * an element read from an array is still copied, by the sequence intrinsic
 * that reads it. */

/* Variants built in the caller's storage, keeping the locals built there. */

/* A returned variant keeps its payload values, which the return copies to the
 * caller; one kept in a local copies them, since the local's array changes. */

/* A function builds the variant literal it returns in its caller's storage;
 * one that returns a local it assigns does not. Arrays live on the managed
 * heap, so a returned one is never copied, and a function that changes its
 * arrays through their places, as Counted and Paired do, builds nothing
 * there. */

/* Whether f's first push or extension drops its item at once: the next host
 * call drops a value. */

/* Arrays are values a loop retains, so neither a loop over an array nothing
 * changes nor one whose body changes its array snapshots it; CopiedWalk's
 * result shows the walk still sees the array as it was. A local nothing
 * changes reads its element in place without a host call, as stage zero lends
 * it, unless its array changes. WalkedSum walks a parameter and a local
 * nothing changes as they are, without copying either. */

/* Literals whose binary64 and binary32 readings LiteralBits must agree on:
 * exact and halfway values, the ends of both ranges, long and hexadecimal
 * spellings. */

/* Whether a literal's significant digits are nonzero. */

/* The reading LiteralBits implements, which is stage zero's where long double
 * is double: the binary64 nearest the literal and the binary32 nearest that;
 * a nonzero literal whose binary64 is infinite, zero or subnormal is out of
 * range. */

/* Spellings longer than the listed ones: the exact decimals of binary64
 * values that are binary32 ties, and 800 digits followed by one that decides
 * the rounding, or by an exponent that moves the point back. */

/* LiteralBits reads number literals of floating types by that reading:
 * the listed spellings on both compiler backends, with both signs on the
 * compiled one, and there also the long spellings and generated ones across
 * the binary64 range. */

/* The packages of a closure, linked against the compiler host's natives,
 * which `native` holds; NULL when it does not load. */

/* Repeated equivalent bindings share one emitted specialization, while
 * distinct bindings have different IDs. Source points separately compare
 * those IDs with stage zero's independently prepared modules. */

/* A native never changes a value it is passed, so a call lends it a local,
 * and the result it returns is a temporary a local takes over. An element it
 * is passed is copied. */

/* A drop releases only collections, so a temporary that holds none is not
 * dropped. */

/* Runs a call with the probe natives: the compiler host links only its own,
 * so the closure is read with them and its function run directly. */

/* The receiver input must be captured before the authored item call. Comparing
 * watch sets alone cannot detect a watch moved across that call. Each fixture
 * function has exactly one update and one authored invocation. */

/* Observe actual delivery as well as emitted order. Payload pointers belong
 * to the invocation; the callbacks retain only their checked literal values. */

/* Compare serialized function flags, independently of the runtime's derived
 * effect summary. Direct, recursive and imported HOST callers must retain it. */

/* Each module retains stage zero's source points, watches and, when supplied,
 * effects for the target's natives. */

/* Each compiler backend has a focused selector. Bytecode compares source
 * points with stage zero; both compare explicit target effects. */
/* Stage zero's packages for the sample, linked against the fixture's
 * natives, and its selected diagnostic package (facts by default). */

enum
{
    TRACED_WATCHES = 64
};

/* The watches one call delivers, in order, with each scalar payload's bits. */

/* Runs `function`, which takes no arguments and returns one value, from a
 * closure linked with the probe natives, tracing the watches it delivers. */

/* Each traced call returns what stage zero's does and delivers its watches,
 * on both backends. */

/* Unused collection packages have no emitted row, even an empty one. All
 * independent project packages retain nonempty module rows. */

/* Invalid Unicode scalars are authored data failures, as stage zero's
 * char_check emits, rather than failures attributed to a native host call. */

/* Without debugger points and watches, a closure's functions have none, and
 * its loops and branches still run as they do with them. */
/* The closure the compiler builds for sample `s` without debug information,
 * or NULL when it does not compile. */

/* Without debug information, a push or extension of a local array updates it
 * in place and drops a temporary item at once rather than holding it until
 * the function returns. */

/* The instructions compiling a package of `records` records takes, counted
 * across every call, or zero when it does not compile. */

/* Declaring each type's array and view optionals takes one pass over the
 * type table, not a search of it for each type: twice the records take about
 * twice the steps, where the search took three times as many. */

/* Stage zero currently resolves assert through its native catalog, so its
 * ordinary pure-target oracle cannot model this authored intrinsic contract.
 * Compile directly with no game/native assertion pack and inspect the emitted
 * checked instruction before running both execution backends. Existing oracle
 * samples remain unchanged. */

/* The literal diagnostics also hold in the independent stage-zero frontend,
 * including reference and boxed targets and record fields. */

/* Values as the compiler host takes arguments and projects results: a JSON
 * array; malloc-owned. */

/* Stage two's rejection of the compiler's sources, from Compile's result. */

/* Writes bytes to folder/name. */

/* With CHIBI_BOOTSTRAP_SEED naming a folder, leaves stage two and Compile's
 * arguments there, the seed tools/build.sh build bootstrap-artifact writes the bootstrap
 * artifact from. */

/* Stage two reads the listed floating literals as stage one does: each
 * LiteralBits result projects to the same JSON. */

/* Whether stage two, loaded in `host`, compiles sample `s` as stage one does:
 * to the same closure, or to the same rejection. A closure is compared as
 * bytes, since its JSON output outgrows the host's projection limits. */

/* Stage zero prepares stage one, which compiles its own packages, without
 * debugger points and watches, to a closure: stage two. Stage two, loaded by
 * the compiler host, compiles the same packages to the same bytes, every
 * sample as stage one does, and reads the listed floating literals as stage
 * one does. */
/* The functions of the package compiler's module `p` that copy their
 * arguments, other than those that run once per compile or per source, each
 * reported. */

/* Behavior assets. Stage one compiles each against a game this test declares,
 * on both compiler backends; its module declares stage zero's identities and
 * plays as stage zero's does. Counter is the Counter story's behavior: a
 * retained state, and a Tick handler that increments it and writes it,
 * converted to a float, to a field of the owner's component. */

/* Sources stage one rejects, with its message. Where stage zero rejects one
 * too, stage one gives its message at its position. */

enum
{
    SPOT = 101,
    SPOT_COMPONENT = 901,
    QUERY_TARGETS = 902,
    TICK = 11,
    MOVE = 12,
    OWNER = 30,
    ENTITY = 700,
    OTHER_HANDLE = 701,
    SCENE_GLOBALS = 702,
    GET_TERRAIN = 800,
    GET_OTHER = 801,
    GET_RESOLVED = 802,
    GET_TARGET = 803,
    BEHAVIOR_ARGUMENTS = 17,
    TICKS = 5
};

/* The game: Spot, two nominal handle types, and keyed scene globals, with
 * Tick, Move, `self`, and the unsupported scalar global `pace`. */

/* The entity `globals.Target` names: the playing world's target. Fresh worlds
 * number their entities alike, so both players of a comparison share it. */

/* The getters preserve the caller's typed result header. Equal payload bits
 * for the two handle types must never make them assignment-compatible. */

/* Stage zero's module of `source`. */

/* The index of the type `id` among the catalog's, -1 for none. */

/* Behavior targets append these fixture getters after the production host
 * natives. Package compilation and bootstrap keep their original tables. */

/* CompileBehavior's game arguments, as the game target exports them: each
 * event and its payload fields, each type and its fields, the entity type,
 * no operators that combine records, the globals, and no record makes. */

/* Stage one's prepared closure for `source`, using the production catalog export. */

/* The game's types and events, as the player links them. */

/* The module stage one compiles `source` to on backend `b`, linked against
 * the game. */

/* A behavior module playing in its own world, on one owner holding a Spot,
 * beside the target holding another. */

/* Production catalog export must carry nominal native ports all the way to
 * prepared host banks. SpotAlias shares Spot's layout; OtherSpot does not
 * share its identity. An unsupported native remains harmless until selected. */
enum
{
    CATALOG_ALIAS = 710,
    CATALOG_OTHER = 711,
    CATALOG_ECHO = 5100,
    CATALOG_ENTITY,
    CATALOG_PAIR,
    CATALOG_ALIASED,
    CATALOG_SEED,
    CATALOG_CHECK,
    CATALOG_OTHER_RESULT,
    CATALOG_UNSUPPORTED
};
/* Decode the production JSON against the actual authored entry signature.
 * No test-side table reconstructs or repairs its port metadata. */

enum
{
    CATALOG_SELECT_SPOT = 6100,
    CATALOG_SELECT_ENTITY,
    CATALOG_COLLISION,
    CATALOG_INFERRED,
    CATALOG_MAKE
};

/* The bounded scalar-char prerequisite deliberately does not enable its
 * container, pointer, field or arithmetic forms. Native acceptance proves
 * these failures describe stage-one coverage rather than invalid Chibi. */

/* A state's resolved nominal type must be checked after grouping, before
 * its integer storage or default can enter the behavior state table. */

/* Task results keep cancellation distinct from valid zero/false/empty values.
 * Start runs up to its first yield; joining a completed/cancelled task continues
 * synchronously, and direct await preserves the ordinary return type. */

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
    REQUIRE(value.type == PIN_STR && arena_contains(&detached, value.text));
    REQUIRE(
        pin_text_equal(pin_text(value), (pin_string){ .bytes = expected, .length = strlen(expected) })
    );
    return true;
}
static const char
    SCALAR_SOURCE[] = "package scalar\n"
                      "Main :: () -> int { return Pick(7, true); }\n"
                      "Pick :: (x: int, b: bool) -> int { return x; }\n";
/* Two concrete Remap instances, one with a named function and one with an
 * inline function. Main's independent arithmetic oracle is 50 + 150 = 200. */

static const char GENERIC_SCALAR_SOURCE
    [] = "package scalar\n"
         "Main :: () -> int { return int(Named(0.25) * 100) + int(Inline(0.5) "
         "* 100); }\n"
         "Named :: (sample: number) -> number { return Remap(sample, Gain); }\n"
         "Inline :: (sample: number) -> number { return Remap(sample, (value: "
         "number) => value * 3); }\n"
         "Remap :: (sample: $T, transform: fn(value: T) -> T) -> T { return "
         "transform(value: sample); }\n"
         "Gain :: (input: number) -> number { return input * 2; }\n";
static const char GENERIC_INTEGER_SCALAR_SOURCE
    [] = "package scalar\n"
         "Main :: () -> int { return Named(25) + Inline(50); }\n"
         "Named :: (sample: int) -> int { return Remap(sample, Gain); }\n"
         "Inline :: (sample: int) -> int { return Remap(sample, (value: int) "
         "=> value * 3); }\n"
         "Remap :: (sample: $T, transform: fn(value: T) -> T) -> T { return "
         "transform(value: sample); }\n"
         "Gain :: (input: int) -> int { return input * 2; }\n";
/* A guarded module as CollectScalar gives it: Pick's guard compares its
 * parameters and its arm calls Id. */
static const char GUARD_SOURCE[] = "package scalar\n"
                                   "Main :: () -> int { return Pick(1, 2); }\n"
                                   "Pick :: (a: int, b: int) -> int {\n"
                                   "    if (a < b) { return Id(a); }\n"
                                   "    return b;\n"
                                   "}\n"
                                   "Id :: (x: int) -> int { return x; }\n";
/* Low(298) is 298's low byte, 42, shifted left once; Main widens it to int. */
static const char
    WIDTH_SOURCE[] = "package scalar\n"
                     "Main :: () -> int { return int(Low(298)); }\n"
                     "Low :: (x: int) -> u8 { return u8(wrap: x) << 1; }\n";
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
    SCALAR_IF_ROW = 9,
    SCALAR_COMPARE_ROW = 10,
    SCALAR_ARITHMETIC_ROW = 11,
    SCALAR_UNARY_ROW = 12,
    SCALAR_CONVERT_ROW = 13,
    SCALAR_LOCAL_ROW = 14,
    SCALAR_ASSIGN_ROW = 15,
    SCALAR_ELSE_ROW = 16,
    SCALAR_END_ROW = 17,
    SCALAR_LOGICAL_ROW = 18,
    SCALAR_WHILE_ROW = 19,
    SCALAR_FOR_ROW = 20,
    SCALAR_BREAK_ROW = 21,
    SCALAR_CONTINUE_ROW = 22,
    SCALAR_SWITCH_ROW = 23,
    SCALAR_CASE_ROW = 24,
    SCALAR_RECORD_ROW = 25,
    SCALAR_MEMBER_ROW = 26,
    SCALAR_ZERO_ROW = 27,
    SCALAR_UPDATE_ROW = 28,
    SCALAR_FIELD_ROW = 29,
    SCALAR_UNION_ROW = 30,
    SCALAR_VARIANT_ROW = 31,
    SCALAR_MAKE_ROW = 32,
    SCALAR_TAG_ROW = 33,
    SCALAR_PAYLOAD_ROW = 34,
    SCALAR_TEXT_ROW = 35,
    SCALAR_ASSERT_ROW = 36,
    SCALAR_TEXT_LENGTH = 1,
    SCALAR_TEXT_BYTE = 2,
    SCALAR_TEXT_SLICE = 3,
    SCALAR_TEXT_FORMAT = 4,
    SCALAR_RANGE_EXCLUSIVE = 1,
    SCALAR_INT = 1,
    SCALAR_BOOL = 2,
    SCALAR_INT8 = 3,
    SCALAR_BYTE = 4,
    SCALAR_INT16 = 5,
    SCALAR_UINT16 = 6,
    SCALAR_UINT = 7,
    SCALAR_INT64 = 8,
    SCALAR_WORD = 9,
    SCALAR_FLOAT = 10,
    SCALAR_DOUBLE = 11,
    SCALAR_STRING = 12,
    SCALAR_RECORD = 32
};
/* The kind of each of GUARD_SOURCE's rows, and the rows edited. */
static const int GUARD_KINDS[] = {
    SCALAR_SOURCE_ROW,    SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,  SCALAR_LITERAL_ROW,
    SCALAR_CALL_ROW,      SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW,
    SCALAR_PARAMETER_ROW, SCALAR_IF_ROW,       SCALAR_READ_ROW,
    SCALAR_READ_ROW,      SCALAR_COMPARE_ROW,  SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,      SCALAR_CALL_ROW,     SCALAR_END_ROW,
    SCALAR_RETURN_ROW,    SCALAR_READ_ROW,     SCALAR_FUNCTION_ROW,
    SCALAR_PARAMETER_ROW, SCALAR_RETURN_ROW,   SCALAR_READ_ROW,
};
enum
{
    GUARD_CALLEE = 7,
    GUARD_ROW = 10,
    GUARD_CONDITION = 11,
    GUARD_COMPARE = 13,
    GUARD_ARM = 14,
    GUARD_ARM_READ = 15,
    GUARD_END = 17,
    GUARD_FINAL = 18,
    GUARD_ID_READ = 23,
    GUARD_ROWS = (int)(sizeof GUARD_KINDS / sizeof *GUARD_KINDS),
    GUARD_EDITS = 5
};
/* Pick declares x, then in an if statement whose condition is a logical
 * and its consequence's own local y, adds y to x, and otherwise returns b.
 * Pick(1, 2) is 3. */
static const char
    CONTROL_ROWS_SOURCE[] = "package scalar\n"
                            "Main :: () -> int { return Pick(1, 2); }\n"
                            "Pick :: (a: int, b: int) -> int {\n"
                            "    x := a;\n"
                            "    if (a < b && !(b < 0)) {\n"
                            "        y := b;\n"
                            "        x += y;\n"
                            "    } else {\n"
                            "        return b;\n"
                            "    }\n"
                            "    return x;\n"
                            "}\n";
/* The kind of each of CONTROL_ROWS_SOURCE's rows, and the rows edited. */
static const int CONTROL_KINDS[] = {
    SCALAR_SOURCE_ROW,    SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,  SCALAR_LITERAL_ROW,
    SCALAR_CALL_ROW,      SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW,
    SCALAR_PARAMETER_ROW, SCALAR_LOCAL_ROW,    SCALAR_READ_ROW,
    SCALAR_IF_ROW,        SCALAR_READ_ROW,     SCALAR_READ_ROW,
    SCALAR_COMPARE_ROW,   SCALAR_LOGICAL_ROW,  SCALAR_READ_ROW,
    SCALAR_LITERAL_ROW,   SCALAR_COMPARE_ROW,  SCALAR_UNARY_ROW,
    SCALAR_LOCAL_ROW,     SCALAR_READ_ROW,     SCALAR_ASSIGN_ROW,
    SCALAR_READ_ROW,      SCALAR_READ_ROW,     SCALAR_ARITHMETIC_ROW,
    SCALAR_ELSE_ROW,      SCALAR_RETURN_ROW,   SCALAR_READ_ROW,
    SCALAR_END_ROW,       SCALAR_RETURN_ROW,   SCALAR_READ_ROW,
};
enum
{
    CONTROL_X = 10,
    CONTROL_X_VALUE = 11,
    CONTROL_IF = 12,
    CONTROL_LOGICAL = 16,
    CONTROL_NOT = 20,
    CONTROL_Y = 21,
    CONTROL_ASSIGN = 23,
    CONTROL_ELSE = 27,
    CONTROL_ELSE_READ = 29,
    CONTROL_END = 30,
    CONTROL_FINAL = 31,
    CONTROL_FINAL_READ = 32,
    CONTROL_ROWS = (int)(sizeof CONTROL_KINDS / sizeof *CONTROL_KINDS)
};
/* Sum sums a for loop's indices until a break, then counts up in an endless
 * while loop whose continue returns to its head. Sum(3) is 2. */
static const char
    LOOP_ROWS_SOURCE[] = "package scalar\n"
                         "Main :: () -> int { return Sum(3); }\n"
                         "Sum :: (n: int) -> int {\n"
                         "    total := 0;\n"
                         "    for (i in 0..<n) {\n"
                         "        if (i == 1) { break; }\n"
                         "        total += i;\n"
                         "    }\n"
                         "    while (true) {\n"
                         "        if (total > 1) { return total; }\n"
                         "        total += 1;\n"
                         "        if (total < 0) { continue; }\n"
                         "    }\n"
                         "}\n";
/* The kind of each of LOOP_ROWS_SOURCE's rows, and the rows edited. */
static const int LOOP_KINDS[] = {
    SCALAR_SOURCE_ROW,     SCALAR_DOCUMENT_ROW,   SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,     SCALAR_LITERAL_ROW,    SCALAR_CALL_ROW,
    SCALAR_FUNCTION_ROW,   SCALAR_PARAMETER_ROW,  SCALAR_LOCAL_ROW,
    SCALAR_LITERAL_ROW,    SCALAR_FOR_ROW,        SCALAR_LITERAL_ROW,
    SCALAR_READ_ROW,       SCALAR_IF_ROW,         SCALAR_READ_ROW,
    SCALAR_LITERAL_ROW,    SCALAR_COMPARE_ROW,    SCALAR_BREAK_ROW,
    SCALAR_END_ROW,        SCALAR_ASSIGN_ROW,     SCALAR_READ_ROW,
    SCALAR_READ_ROW,       SCALAR_ARITHMETIC_ROW, SCALAR_END_ROW,
    SCALAR_WHILE_ROW,      SCALAR_LITERAL_ROW,    SCALAR_IF_ROW,
    SCALAR_READ_ROW,       SCALAR_LITERAL_ROW,    SCALAR_COMPARE_ROW,
    SCALAR_RETURN_ROW,     SCALAR_READ_ROW,       SCALAR_END_ROW,
    SCALAR_ASSIGN_ROW,     SCALAR_READ_ROW,       SCALAR_LITERAL_ROW,
    SCALAR_ARITHMETIC_ROW, SCALAR_IF_ROW,         SCALAR_READ_ROW,
    SCALAR_LITERAL_ROW,    SCALAR_COMPARE_ROW,    SCALAR_CONTINUE_ROW,
    SCALAR_END_ROW,        SCALAR_END_ROW,
};
enum
{
    LOOP_FOR = 10,
    LOOP_FIRST = 11,
    LOOP_LAST = 12,
    LOOP_GUARD = 13,
    LOOP_BREAK = 17,
    LOOP_ADD = 19,
    LOOP_FOR_END = 23,
    LOOP_WHILE = 24,
    LOOP_TRUE = 25,
    LOOP_COUNT_READ = 34,
    LOOP_CONTINUE = 41,
    LOOP_WHILE_END = 43,
    LOOP_ROWS = (int)(sizeof LOOP_KINDS / sizeof *LOOP_KINDS)
};
/* Pick switches on x: its default arm, in the middle, declares y. Every arm
 * returns, so the switch ends Pick. Pick(2) is 20. */
static const char
    SWITCH_ROWS_SOURCE[] = "package scalar\n"
                           "Main :: () -> int { return Pick(2); }\n"
                           "Pick :: (x: int) -> int {\n"
                           "    switch x {\n"
                           "        case 1: return 10;\n"
                           "        case: y := x; return y;\n"
                           "        case 2: return 20;\n"
                           "    }\n"
                           "}\n";
/* The kind of each of SWITCH_ROWS_SOURCE's rows, and the rows edited. */
static const int SWITCH_KINDS[] = {
    SCALAR_SOURCE_ROW,   SCALAR_DOCUMENT_ROW,  SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,   SCALAR_LITERAL_ROW,   SCALAR_CALL_ROW,
    SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW, SCALAR_SWITCH_ROW,
    SCALAR_READ_ROW,     SCALAR_CASE_ROW,      SCALAR_LITERAL_ROW,
    SCALAR_RETURN_ROW,   SCALAR_LITERAL_ROW,   SCALAR_CASE_ROW,
    SCALAR_LOCAL_ROW,    SCALAR_READ_ROW,      SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,     SCALAR_CASE_ROW,      SCALAR_LITERAL_ROW,
    SCALAR_RETURN_ROW,   SCALAR_LITERAL_ROW,   SCALAR_END_ROW,
};
enum
{
    SWITCH_FUNCTION = 6,
    SWITCH_ROW = 8,
    SWITCH_VALUE = 9,
    SWITCH_FIRST = 10,
    SWITCH_FIRST_VALUE = 11,
    SWITCH_FIRST_RETURN = 12,
    SWITCH_DEFAULT = 14,
    SWITCH_LAST = 19,
    SWITCH_LAST_RETURN = 21,
    SWITCH_LAST_VALUE = 22,
    SWITCH_END = 23,
    SWITCH_ROWS = (int)(sizeof SWITCH_KINDS / sizeof *SWITCH_KINDS)
};
/* Make builds a Point from its zero, its fields in another order than
 * declared, and Main adds 1 to the x field of the copy it returns, then
 * returns that field: Make(3) gives 4. */
static const char RECORD_ROWS_SOURCE
    [] = "package scalar\n"
         "Point :: struct { x: int; y: number; }\n"
         "Main :: () -> int { p := Make(3); p.x += 1; return p.x; }\n"
         "Make :: (x: int) -> Point { return {y: 0.5, x: x}; }\n";
/* The kind of each of RECORD_ROWS_SOURCE's rows, and the rows edited. */
static const int RECORD_KINDS[] = {
    SCALAR_SOURCE_ROW, SCALAR_DOCUMENT_ROW, SCALAR_RECORD_ROW,
    SCALAR_MEMBER_ROW, SCALAR_MEMBER_ROW,   SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,  SCALAR_LITERAL_ROW,  SCALAR_CALL_ROW,
    SCALAR_ASSIGN_ROW, SCALAR_READ_ROW,     SCALAR_READ_ROW,
    SCALAR_FIELD_ROW,  SCALAR_LITERAL_ROW,  SCALAR_ARITHMETIC_ROW,
    SCALAR_UPDATE_ROW, SCALAR_RETURN_ROW,   SCALAR_READ_ROW,
    SCALAR_FIELD_ROW,  SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW,
    SCALAR_RETURN_ROW, SCALAR_ZERO_ROW,     SCALAR_LITERAL_ROW,
    SCALAR_UPDATE_ROW, SCALAR_READ_ROW,     SCALAR_UPDATE_ROW,
};
enum
{
    RECORD_DECLARATION = 2,
    RECORD_X = 3,
    RECORD_Y = 4,
    RECORD_LOCAL = 6,
    RECORD_ASSIGN = 9,
    RECORD_COMPOUND_FIELD = 12,
    RECORD_COMPOUND_UPDATE = 15,
    RECORD_READ = 17,
    RECORD_FIELD = 18,
    RECORD_MAKE = 19,
    RECORD_ZERO = 22,
    RECORD_Y_UPDATE = 24,
    RECORD_ROWS = (int)(sizeof RECORD_KINDS / sizeof *RECORD_KINDS)
};
/* Make builds the Box variant of Shape, and Main tests it with `is`, then
 * switches on it, binding the Box arm's payloads: Make(3) gives 3. The last
 * arm is the switch's default, since it has none. */
static const char UNION_ROWS_SOURCE
    [] = "package scalar\n"
         "Shape :: union { Dot | Box(w: int, h: number) }\n"
         "Main :: () -> int {\n"
         "    s := Make(3);\n"
         "    if (s is .Dot) { return 0; }\n"
         "    switch s { case .Dot: return 1; case .Box(w, h): return w; }\n"
         "}\n"
         "Make :: (w: int) -> Shape { return Shape.Box(w, 0.5); }\n";
/* The kind of each of UNION_ROWS_SOURCE's rows, and the rows edited. */
static const int UNION_KINDS[] = {
    SCALAR_SOURCE_ROW,   SCALAR_DOCUMENT_ROW,  SCALAR_UNION_ROW,
    SCALAR_VARIANT_ROW,  SCALAR_VARIANT_ROW,   SCALAR_MEMBER_ROW,
    SCALAR_MEMBER_ROW,   SCALAR_FUNCTION_ROW,  SCALAR_LOCAL_ROW,
    SCALAR_LITERAL_ROW,  SCALAR_CALL_ROW,      SCALAR_IF_ROW,
    SCALAR_READ_ROW,     SCALAR_TAG_ROW,       SCALAR_LITERAL_ROW,
    SCALAR_COMPARE_ROW,  SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,
    SCALAR_END_ROW,      SCALAR_SWITCH_ROW,    SCALAR_READ_ROW,
    SCALAR_TAG_ROW,      SCALAR_CASE_ROW,      SCALAR_LITERAL_ROW,
    SCALAR_RETURN_ROW,   SCALAR_LITERAL_ROW,   SCALAR_CASE_ROW,
    SCALAR_LOCAL_ROW,    SCALAR_READ_ROW,      SCALAR_PAYLOAD_ROW,
    SCALAR_LOCAL_ROW,    SCALAR_READ_ROW,      SCALAR_PAYLOAD_ROW,
    SCALAR_RETURN_ROW,   SCALAR_READ_ROW,      SCALAR_END_ROW,
    SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW, SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,     SCALAR_LITERAL_ROW,   SCALAR_MAKE_ROW,
};
enum
{
    UNION_DECLARATION = 2,
    UNION_DOT = 3,
    UNION_BOX = 4,
    UNION_W = 5,
    UNION_H = 6,
    UNION_MAIN = 7,
    UNION_LOCAL = 8,
    UNION_IS_TAG = 13,
    UNION_IS_VARIANT = 14,
    UNION_DOT_RETURN = 17,
    UNION_SWITCH = 19,
    UNION_SWITCH_TAG = 21,
    UNION_DOT_ARM = 22,
    UNION_DOT_VARIANT = 23,
    UNION_BOX_ARM = 26,
    UNION_W_LOCAL = 27,
    UNION_W_PAYLOAD = 29,
    UNION_H_LOCAL = 30,
    UNION_H_PAYLOAD = 32,
    UNION_MAKE = 41,
    UNION_ROWS = (int)(sizeof UNION_KINDS / sizeof *UNION_KINDS)
};
/* Main passes Name a string and compares its result with an equal literal,
 * so the module's one text serves both: Main returns 1. */
static const char
    STRING_ROWS_SOURCE[] = "package scalar\n"
                           "Main :: () -> int {\n"
                           "    s := Name(\"one\");\n"
                           "    if (s != \"one\") { return 0; }\n"
                           "    return 1;\n"
                           "}\n"
                           "Name :: (n: string) -> string { return n; }\n";
/* The kind of each of STRING_ROWS_SOURCE's rows, and the rows edited. */
static const int STRING_KINDS[] = {
    SCALAR_SOURCE_ROW,   SCALAR_DOCUMENT_ROW,  SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,    SCALAR_LITERAL_ROW,   SCALAR_CALL_ROW,
    SCALAR_IF_ROW,       SCALAR_READ_ROW,      SCALAR_LITERAL_ROW,
    SCALAR_COMPARE_ROW,  SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,
    SCALAR_END_ROW,      SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,
    SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW, SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,
};
enum
{
    STRING_LOCAL = 3,
    STRING_ARGUMENT = 4,
    STRING_READ = 7,
    STRING_LITERAL = 8,
    STRING_COMPARE = 9,
    STRING_ZERO = 11,
    STRING_NAME = 15,
    STRING_PARAMETER = 16,
    STRING_PARAMETER_READ = 18,
    STRING_ROWS = (int)(sizeof STRING_KINDS / sizeof *STRING_KINDS)
};
/* Main slices a literal, then reads its view's byte count and first byte:
 * 2 + 'b'. */
static const char TEXT_ROWS_SOURCE[] = "package scalar\n"
                                       "Main :: () -> int {\n"
                                       "    s := text_slice(\"abcd\", 1, 2);\n"
                                       "    return text_byte_length(s) + "
                                       "text_byte_at(s, 0);\n"
                                       "}\n";
/* The kind of each of TEXT_ROWS_SOURCE's rows, and the rows edited. */
static const int TEXT_KINDS[] = {
    SCALAR_SOURCE_ROW,  SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,   SCALAR_LITERAL_ROW,  SCALAR_LITERAL_ROW,
    SCALAR_LITERAL_ROW, SCALAR_TEXT_ROW,     SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,    SCALAR_TEXT_ROW,     SCALAR_READ_ROW,
    SCALAR_LITERAL_ROW, SCALAR_TEXT_ROW,     SCALAR_ARITHMETIC_ROW,
};
enum
{
    TEXT_SOURCE = 4,
    TEXT_START = 5,
    TEXT_SLICE = 7,
    TEXT_LENGTH = 10,
    TEXT_INDEX = 12,
    TEXT_BYTE = 13,
    TEXT_ROWS = (int)(sizeof TEXT_KINDS / sizeof *TEXT_KINDS)
};
/* Main formats an i64, a bool and a string and returns the byte count of
 * what it gives. */
static const char FORMAT_ROWS_SOURCE[] = "package scalar\n"
                                         "Main :: () -> int {\n"
                                         "    s := format_text(\"{}{}{}\", "
                                         "i64(7), true, \"x\");\n"
                                         "    return text_byte_length(s);\n"
                                         "}\n";
/* The kind of each of FORMAT_ROWS_SOURCE's rows, and the rows edited. */
static const int FORMAT_KINDS[] = {
    SCALAR_SOURCE_ROW,  SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,   SCALAR_LITERAL_ROW,  SCALAR_LITERAL_ROW,
    SCALAR_LITERAL_ROW, SCALAR_LITERAL_ROW,  SCALAR_TEXT_ROW,
    SCALAR_RETURN_ROW,  SCALAR_READ_ROW,     SCALAR_TEXT_ROW,
};
enum
{
    FORMAT_FORMAT = 4,
    FORMAT_INT = 5,
    FORMAT_TEXT = 8,
    FORMAT_ROWS = (int)(sizeof FORMAT_KINDS / sizeof *FORMAT_KINDS)
};
/* Main asserts a comparison before it returns. */
static const char ASSERT_ROWS_SOURCE[] = "package scalar\n"
                                         "Main :: () -> int {\n"
                                         "    x: int = 1;\n"
                                         "    assert(x < 2, \"x is small\");\n"
                                         "    return x;\n"
                                         "}\n";
/* The kind of each of ASSERT_ROWS_SOURCE's rows, and the rows edited. */
static const int ASSERT_KINDS[] = {
    SCALAR_SOURCE_ROW, SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,  SCALAR_LITERAL_ROW,  SCALAR_ASSERT_ROW,
    SCALAR_READ_ROW,   SCALAR_LITERAL_ROW,  SCALAR_COMPARE_ROW,
    SCALAR_RETURN_ROW, SCALAR_READ_ROW,
};
enum
{
    ASSERT_STATEMENT = 5,
    ASSERT_COMPARE = 8,
    ASSERT_ROWS = (int)(sizeof ASSERT_KINDS / sizeof *ASSERT_KINDS)
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
static const char LAYOUT[] = "invalid scalar module layout",
                  COMPARISON[] = "scalar comparison differs from its operands",
                  SEGMENT[] = "invalid scalar statement row";
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
static const char
    ARITHMETIC[] = "scalar arithmetic differs from its number operands";
static const malformed_guard MALFORMED_GUARDS[] = {
    /* An if statement with no condition, one that never ends, and an else
     * with no end. */
    { { EDIT(GUARD_ROW, "count", 0) }, LAYOUT },
    { { EDIT(GUARD_FINAL, "kind", SCALAR_IF_ROW) }, LAYOUT },
    { { EDIT(GUARD_END, "kind", SCALAR_ELSE_ROW) }, LAYOUT },
    /* A function whose parameter count would wrap past the rows. */
    { { EDIT(GUARD_CALLEE, "count", INT32_MAX) }, LAYOUT },
    /* A comparison's operator, arity and result. */
    { { EDIT(GUARD_COMPARE, "value", 0) }, COMPARISON },
    { { EDIT(GUARD_COMPARE, "value", 7) }, COMPARISON },
    { { EDIT(GUARD_COMPARE, "count", 1) }, COMPARISON },
    { { EDIT(GUARD_COMPARE, "type", SCALAR_INT) }, COMPARISON },
    /* A comparison of an int with a bool, and an ordering of two bools. */
    { { EDIT(GUARD_CONDITION + 1, "kind", SCALAR_LITERAL_ROW),
        EDIT(GUARD_CONDITION + 1, "type", SCALAR_BOOL),
        EDIT(GUARD_CONDITION + 1, "value", 0),
        EDIT(GUARD_CONDITION + 1, "literal", 1) },
      COMPARISON },
    { { EDIT(GUARD_CONDITION, "kind", SCALAR_LITERAL_ROW),
        EDIT(GUARD_CONDITION, "type", SCALAR_BOOL),
        EDIT(GUARD_CONDITION + 1, "kind", SCALAR_LITERAL_ROW),
        EDIT(GUARD_CONDITION + 1, "type", SCALAR_BOOL),
        EDIT(GUARD_CONDITION + 1, "value", 0) },
      COMPARISON },
    /* A condition that leaves three ints rather than one bool: the
     * comparison becomes a read of b. */
    { { EDIT(GUARD_COMPARE, "kind", SCALAR_READ_ROW),
        EDIT(GUARD_COMPARE, "type", SCALAR_INT),
        EDIT(GUARD_COMPARE, "count", 0) },
      "scalar condition is not one bool" },
    /* An if row that carries a type, a consequence that ends past its if
     * statement, a condition operand inside the consequence, a final return
     * that starts inside the if statement, and an end with another range. */
    { { EDIT(GUARD_ROW, "type", SCALAR_INT) }, SEGMENT },
    { { COPY(GUARD_ARM, "end", GUARD_FINAL) }, SEGMENT },
    { { COPY(GUARD_CONDITION, "start", GUARD_ARM_READ),
        COPY(GUARD_CONDITION, "end", GUARD_ARM_READ),
        COPY(GUARD_CONDITION, "line", GUARD_ARM_READ),
        COPY(GUARD_CONDITION, "column", GUARD_ARM_READ) },
      SEGMENT },
    { { COPY(GUARD_FINAL, "start", GUARD_ROW),
        COPY(GUARD_FINAL, "line", GUARD_ROW),
        COPY(GUARD_FINAL, "column", GUARD_ROW) },
      SEGMENT },
    { { COPY(GUARD_END, "end", GUARD_FINAL) }, SEGMENT },
    /* A read past the variables, and a body that ends in an assignment. */
    { { EDIT(GUARD_ARM_READ, "value", 2) }, "scalar read differs from its variable" },
    { { EDIT(GUARD_FINAL, "kind", SCALAR_ASSIGN_ROW),
        EDIT(GUARD_FINAL, "type", SCALAR_INT),
        EDIT(GUARD_FINAL, "value", 1) },
      "scalar function can end without returning" },
    /* Id calls Main, so the arm's call recurses, under another name. */
    { { EDIT(GUARD_ID_READ, "kind", SCALAR_CALL_ROW),
        EDIT(GUARD_ID_READ, "value", 0),
        { .row = GUARD_ID_READ, .field = "name", .text = "Other", .from = -1 } },
      "scalar call differs from its callee's signature" },
};
static const char LOGIC[] = "scalar logic differs from its bool operands",
                  READ[] = "scalar read differs from its variable";
static const malformed_guard MALFORMED_CONTROL[] = {
    /* A logical with no right rows, with right rows past its condition, whose
     * right side is an int, or with another operator. */
    { { EDIT(CONTROL_LOGICAL, "count", 0) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "count", 5) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "count", 1) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "count", INT32_MAX) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "value", 3) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "type", SCALAR_INT) }, LOGIC },
    /* A not of an int. */
    { { EDIT(CONTROL_NOT, "type", SCALAR_INT) }, ARITHMETIC },
    /* x read in its own initializer, y after its consequence and in the
     * alternative. */
    { { EDIT(CONTROL_X_VALUE, "value", 2) }, READ },
    { { EDIT(CONTROL_FINAL_READ, "value", 3) }, READ },
    { { EDIT(CONTROL_ELSE_READ, "value", 3) }, READ },
    /* A local that is not the next variable, has no name or no scalar type. */
    { { EDIT(CONTROL_X, "value", 3) }, "invalid scalar local row" },
    { { { .row = CONTROL_X, .field = "name", .text = "", .from = -1 } },
      "invalid scalar local row" },
    { { EDIT(CONTROL_X, "type", 0) }, "invalid scalar local row" },
    /* An assignment to no visible variable, or of another type. */
    { { EDIT(CONTROL_ASSIGN, "value", 5) }, "scalar assignment differs from its variable" },
    { { EDIT(CONTROL_ASSIGN, "type", SCALAR_BOOL) },
      "scalar assignment differs from its variable" },
    /* A second else, an else outside its if statement, and an end of
     * another range. */
    { { EDIT(CONTROL_END, "kind", SCALAR_ELSE_ROW) }, LAYOUT },
    { { COPY(CONTROL_ELSE, "end", CONTROL_FINAL) }, SEGMENT },
    { { COPY(CONTROL_END, "start", CONTROL_ELSE),
        COPY(CONTROL_END, "line", CONTROL_ELSE),
        COPY(CONTROL_END, "column", CONTROL_ELSE) },
      SEGMENT },
    /* An if statement with an else whose consequence falls through ends the
     * body only when its alternative does too: the final return becomes an
     * assignment. */
    { { EDIT(CONTROL_FINAL, "kind", SCALAR_ASSIGN_ROW),
        EDIT(CONTROL_FINAL, "type", SCALAR_INT),
        EDIT(CONTROL_FINAL, "value", 2) },
      "scalar function can end without returning" },
};
static const char LOCAL[] = "invalid scalar local row",
                  NO_RETURN[] = "scalar function can end without returning";
static const malformed_guard MALFORMED_LOOPS[] = {
    /* A continue that breaks out of the endless loop, an endless loop on
     * false, and a while row that becomes an if statement, which leaves the
     * continue outside every loop. */
    { { EDIT(LOOP_CONTINUE, "kind", SCALAR_BREAK_ROW) }, NO_RETURN },
    { { EDIT(LOOP_TRUE, "literal", 0) }, NO_RETURN },
    { { EDIT(LOOP_WHILE, "kind", SCALAR_IF_ROW) }, LAYOUT },
    /* A break with an operand, a type or a value. */
    { { EDIT(LOOP_BREAK, "count", 1) }, LAYOUT },
    { { EDIT(LOOP_BREAK, "type", SCALAR_INT) }, SEGMENT },
    { { EDIT(LOOP_CONTINUE, "value", 1) }, SEGMENT },
    /* A while condition that is an int and a range bound that is a bool. */
    { { EDIT(LOOP_TRUE, "type", SCALAR_INT) }, "scalar condition is not one bool" },
    { { EDIT(LOOP_FIRST, "type", SCALAR_BOOL) }, "scalar range differs from its int bounds" },
    /* An index that is not the next variable, is not an int, has unknown
     * flags or no name. */
    { { EDIT(LOOP_FOR, "value", 5) }, LOCAL },
    { { EDIT(LOOP_FOR, "type", SCALAR_BOOL) }, LOCAL },
    { { EDIT(LOOP_FOR, "literal", 8) }, LOCAL },
    { { { .row = LOOP_FOR, .field = "name", .text = "", .from = -1 } }, LOCAL },
    /* An assignment to the index, and reads of the index in its own bound
     * and after its loop. */
    { { EDIT(LOOP_ADD, "value", 2) }, "scalar assignment differs from its variable" },
    { { EDIT(LOOP_LAST, "value", 2) }, READ },
    { { EDIT(LOOP_COUNT_READ, "value", 2) }, READ },
    /* A loop end of another range, and a body statement that starts inside
     * the bounds. */
    { { COPY(LOOP_FOR_END, "end", LOOP_WHILE_END) }, SEGMENT },
    { { COPY(LOOP_GUARD, "start", LOOP_LAST),
        COPY(LOOP_GUARD, "line", LOOP_LAST),
        COPY(LOOP_GUARD, "column", LOOP_LAST) },
      SEGMENT },
};
static const char
    SWITCH_VALUE_TYPE[] = "scalar switch value is not one integer";
static const malformed_guard MALFORMED_SWITCHES[] = {
    /* A statement before the first arm, an arm outside a switch, and a
     * second default arm. */
    { { EDIT(SWITCH_FIRST, "kind", SCALAR_RETURN_ROW) }, LAYOUT },
    { { EDIT(SWITCH_ROW, "kind", SCALAR_IF_ROW) }, LAYOUT },
    { { EDIT(SWITCH_FIRST, "count", 0),
        EDIT(SWITCH_FIRST_VALUE, "kind", SCALAR_CASE_ROW),
        EDIT(SWITCH_FIRST_VALUE, "literal", 0) },
      SEGMENT },
    /* A switch on a bool, on a float, and of a type its value is not. */
    { { EDIT(SWITCH_ROW, "type", SCALAR_BOOL) }, SWITCH_VALUE_TYPE },
    { { EDIT(SWITCH_ROW, "type", SCALAR_FLOAT) }, SWITCH_VALUE_TYPE },
    { { EDIT(SWITCH_ROW, "type", SCALAR_INT64) }, SWITCH_VALUE_TYPE },
    /* An arm of another type than its switch, and an arm whose value is. */
    { { EDIT(SWITCH_FIRST, "type", SCALAR_BYTE) }, SEGMENT },
    { { EDIT(SWITCH_FIRST_VALUE, "type", SCALAR_BOOL) },
      "scalar case value differs from its switch value" },
    /* An arm with a value or a name, one that ends past its switch, an arm
     * statement inside its arm's value, and an end of another range. */
    { { EDIT(SWITCH_LAST, "value", 1) }, SEGMENT },
    { { { .row = SWITCH_LAST, .field = "name", .text = "x", .from = -1 } }, SEGMENT },
    { { COPY(SWITCH_LAST, "end", SWITCH_FUNCTION) }, SEGMENT },
    { { COPY(SWITCH_FIRST_RETURN, "start", SWITCH_FIRST_VALUE),
        COPY(SWITCH_FIRST_RETURN, "line", SWITCH_FIRST_VALUE),
        COPY(SWITCH_FIRST_RETURN, "column", SWITCH_FIRST_VALUE) },
      SEGMENT },
    { { COPY(SWITCH_END, "end", SWITCH_FUNCTION) }, SEGMENT },
    /* The default arm's y read in the arm after it. */
    { { EDIT(SWITCH_LAST_VALUE, "kind", SCALAR_READ_ROW),
        EDIT(SWITCH_LAST_VALUE, "value", 1),
        EDIT(SWITCH_LAST_VALUE, "literal", 0) },
      READ },
    /* An arm that does not return: its return becomes an assignment. */
    { { EDIT(SWITCH_LAST_RETURN, "kind", SCALAR_ASSIGN_ROW),
        EDIT(SWITCH_LAST_RETURN, "type", SCALAR_INT),
        EDIT(SWITCH_LAST_RETURN, "value", 0) },
      NO_RETURN },
};
static const char RECORD_ROW[] = "invalid scalar record row",
                  MEMBER_ROW[] = "invalid scalar record member row",
                  FIELD_READ[] = "scalar field read differs from its record",
                  FIELD_WRITE[] = "scalar field write differs from its record";
static const malformed_guard MALFORMED_RECORDS[] = {
    /* A record whose members overrun into the function after them, one
     * with no name, and a record that does not lie where it is declared. */
    { { EDIT(RECORD_DECLARATION, "count", 3) }, MEMBER_ROW },
    { { EDIT(RECORD_DECLARATION, "count", INT32_MAX) }, RECORD_ROW },
    { { { .row = RECORD_DECLARATION, .field = "name", .text = "", .from = -1 } },
      RECORD_ROW },
    { { EDIT(RECORD_DECLARATION, "type", SCALAR_INT) }, RECORD_ROW },
    /* A member of its own record type, which holds itself, out of order,
     * named as another, or outside its record. */
    { { EDIT(RECORD_Y, "type", SCALAR_RECORD) }, RECORD_ROW },
    { { EDIT(RECORD_Y, "type", SCALAR_STRING) }, FIELD_WRITE },
    { { EDIT(RECORD_X, "value", 1) }, MEMBER_ROW },
    { { { .row = RECORD_Y, .field = "name", .text = "x", .from = -1 } }, MEMBER_ROW },
    { { COPY(RECORD_Y, "end", RECORD_MAKE) }, MEMBER_ROW },
    /* A function returning a record the module does not declare, which
     * its caller's call reaches first, and a local or read of another type
     * than its value or variable. */
    { { EDIT(RECORD_MAKE, "type", SCALAR_RECORD + 1) },
      "scalar call differs from its callee's signature" },
    { { EDIT(RECORD_LOCAL, "type", SCALAR_INT) }, "scalar value differs from its variable" },
    { { EDIT(RECORD_ASSIGN, "type", SCALAR_INT) },
      "scalar assignment differs from its variable" },
    { { EDIT(RECORD_READ, "type", SCALAR_INT) }, READ },
    /* A zero of a scalar type; a field read of the float field, as a float
     * or of a scalar; a write of the int to the float field or to a
     * scalar. */
    { { EDIT(RECORD_ZERO, "type", SCALAR_INT) }, "scalar record zero names no record" },
    { { EDIT(RECORD_FIELD, "value", 1) }, FIELD_READ },
    { { EDIT(RECORD_FIELD, "value", 2) }, FIELD_READ },
    { { EDIT(RECORD_FIELD, "type", SCALAR_FLOAT) }, FIELD_READ },
    { { EDIT(RECORD_FIELD, "count", 2) }, FIELD_READ },
    { { EDIT(RECORD_COMPOUND_FIELD, "value", -1) }, FIELD_READ },
    { { EDIT(RECORD_Y_UPDATE, "value", 0) }, FIELD_WRITE },
    { { EDIT(RECORD_COMPOUND_UPDATE, "value", 1) }, FIELD_WRITE },
    { { EDIT(RECORD_COMPOUND_UPDATE, "type", SCALAR_INT) }, FIELD_WRITE },
    { { EDIT(RECORD_COMPOUND_UPDATE, "count", 1) }, FIELD_WRITE },
};
static const char UNION_ROW[] = "invalid scalar union row",
                  VARIANT_ROW[] = "invalid scalar union variant row",
                  PAYLOAD_ROW[] = "invalid scalar union member row",
                  VARIANT[] = "scalar variant differs from its union",
                  TAG[] = "scalar tag reads no union",
                  PAYLOAD[] = "scalar payload read differs from its union";
static const malformed_guard MALFORMED_UNIONS[] = {
    /* A union whose variants overrun into its payloads, one with no name,
     * one that does not lie where it is declared, and one named as a
     * record would be. */
    { { EDIT(UNION_DECLARATION, "count", 3) }, UNION_ROW },
    { { { .row = UNION_DECLARATION, .field = "name", .text = "", .from = -1 } }, UNION_ROW },
    { { EDIT(UNION_DECLARATION, "type", SCALAR_INT) }, UNION_ROW },
    { { EDIT(UNION_DECLARATION, "value", 1) }, UNION_ROW },
    /* A variant out of order, named as another, outside its union, or with
     * fewer payloads than follow it, which leaves a member where a function
     * must start. */
    { { EDIT(UNION_BOX, "value", 0) }, VARIANT_ROW },
    { { { .row = UNION_BOX, .field = "name", .text = "Dot", .from = -1 } }, VARIANT_ROW },
    { { COPY(UNION_BOX, "end", UNION_MAIN) }, VARIANT_ROW },
    { { EDIT(UNION_BOX, "count", 1) }, LAYOUT },
    /* Payload counts that would wrap the next variant's row negative. */
    { { EDIT(UNION_DOT, "count", INT32_MAX) }, UNION_ROW },
    { { EDIT(UNION_BOX, "count", INT32_MAX) }, UNION_ROW },
    { { EDIT(UNION_DECLARATION, "count", INT32_MAX) }, UNION_ROW },
    /* A payload numbered as another, of a record type that holds the union
     * (a cycle), of a string, which reads back as another type, named as
     * another in its variant, or outside its variant. */
    { { EDIT(UNION_H, "value", 0) }, PAYLOAD_ROW },
    { { EDIT(UNION_H, "type", SCALAR_RECORD) }, UNION_ROW },
    { { EDIT(UNION_H, "type", SCALAR_STRING) }, PAYLOAD },
    { { { .row = UNION_H, .field = "name", .text = "w", .from = -1 } }, PAYLOAD_ROW },
    { { COPY(UNION_H, "end", UNION_MAIN) }, PAYLOAD_ROW },
    /* A variant made with another's payloads, too few of them or as a
     * scalar. */
    { { EDIT(UNION_MAKE, "value", 0) }, VARIANT },
    { { EDIT(UNION_MAKE, "count", 1) }, VARIANT },
    { { EDIT(UNION_MAKE, "type", SCALAR_INT) }, VARIANT },
    /* A tag of another type or arity, a payload of another type than its
     * binding or none of its union, and a zero or field read of a union,
     * which only a record has. */
    { { EDIT(UNION_IS_TAG, "type", SCALAR_BOOL) }, TAG },
    { { EDIT(UNION_IS_TAG, "count", 2) }, TAG },
    { { EDIT(UNION_IS_TAG, "value", 1) }, TAG },
    { { EDIT(UNION_H_PAYLOAD, "value", 0) }, PAYLOAD },
    { { EDIT(UNION_H_PAYLOAD, "value", 2) }, PAYLOAD },
    { { EDIT(UNION_W_PAYLOAD, "type", SCALAR_FLOAT) }, PAYLOAD },
    { { EDIT(UNION_DOT_RETURN, "kind", SCALAR_ZERO_ROW),
        EDIT(UNION_DOT_RETURN, "type", SCALAR_RECORD) },
      "scalar record zero names no record" },
    { { EDIT(UNION_IS_TAG, "kind", SCALAR_FIELD_ROW) }, FIELD_READ },
    /* The tag as a bool, which its variant's literal no longer matches. */
    { { EDIT(UNION_IS_VARIANT, "type", SCALAR_BOOL),
        EDIT(UNION_IS_VARIANT, "literal", 0) },
      "scalar comparison differs from its operands" },
};
static const char OPERAND_ROW[] = "invalid scalar operand row",
                  NONCANONICAL[] = "noncanonical scalar literal";
static const malformed_guard MALFORMED_STRINGS[] = {
    /* Text on an int literal or a read, and a string literal whose
     * `literal` is not 0. */
    { { { .row = STRING_ZERO, .field = "text", .text = "x", .from = -1 } }, OPERAND_ROW },
    { { { .row = STRING_READ, .field = "text", .text = "one", .from = -1 } }, OPERAND_ROW },
    { { EDIT(STRING_LITERAL, "literal", 1) }, NONCANONICAL },
    /* Strings ordered, and a string compared with an int. */
    { { EDIT(STRING_COMPARE, "value", 1) }, COMPARISON },
    { { EDIT(STRING_COMPARE, "value", 4) }, COMPARISON },
    { { EDIT(STRING_LITERAL, "type", SCALAR_INT),
        { .row = STRING_LITERAL, .field = "text", .text = "", .from = -1 } },
      COMPARISON },
    /* A string local or read of another type, and a call whose string
     * argument meets an int parameter. */
    { { EDIT(STRING_LOCAL, "type", SCALAR_INT) }, "scalar value differs from its variable" },
    { { EDIT(STRING_PARAMETER_READ, "type", SCALAR_BOOL) }, READ },
    { { EDIT(STRING_PARAMETER, "type", SCALAR_INT) },
      "scalar call argument differs from its parameter" },
};
static const char TEXT_READ[] = "scalar text read differs from its operands";
/* A Text row's operator, arity, name and result, and its string or int
 * operand of another type. */
static const malformed_guard MALFORMED_TEXTS[] = {
    { { EDIT(TEXT_SLICE, "value", 0) }, TEXT_READ },
    { { EDIT(TEXT_SLICE, "value", 5) }, TEXT_READ },
    { { EDIT(TEXT_SLICE, "count", 2) }, TEXT_READ },
    { { EDIT(TEXT_SLICE, "type", SCALAR_INT) }, TEXT_READ },
    { { EDIT(TEXT_LENGTH, "type", SCALAR_STRING) }, TEXT_READ },
    { { { .row = TEXT_LENGTH, .field = "name", .text = "x", .from = -1 } }, TEXT_READ },
    { { EDIT(TEXT_BYTE, "value", SCALAR_TEXT_LENGTH) }, TEXT_READ },
    { { EDIT(TEXT_BYTE, "value", SCALAR_TEXT_SLICE), EDIT(TEXT_BYTE, "count", 3) },
      TEXT_READ },
    { { EDIT(TEXT_START, "type", SCALAR_BOOL) }, TEXT_READ },
    { { EDIT(TEXT_INDEX, "type", SCALAR_INT64) }, TEXT_READ },
    { { EDIT(TEXT_SOURCE, "type", SCALAR_INT),
        { .row = TEXT_SOURCE, .field = "text", .text = "", .from = -1 } },
      TEXT_READ },
};
/* A format of more operands than the stack holds, of none, an int result
 * and an int format. */
static const malformed_guard MALFORMED_FORMATS[] = {
    { { EDIT(FORMAT_TEXT, "count", 5) }, TEXT_READ },
    { { EDIT(FORMAT_TEXT, "count", 0) }, TEXT_READ },
    { { EDIT(FORMAT_TEXT, "type", SCALAR_INT) }, TEXT_READ },
    { { EDIT(FORMAT_FORMAT, "type", SCALAR_INT),
        { .row = FORMAT_FORMAT, .field = "text", .text = "", .from = -1 } },
      TEXT_READ },
};
/* An assert's type, value, name and literal, and a condition that is an
 * int. */
static const malformed_guard MALFORMED_ASSERTS[] = {
    { { EDIT(ASSERT_STATEMENT, "type", SCALAR_BOOL) }, SEGMENT },
    { { EDIT(ASSERT_STATEMENT, "value", 1) }, SEGMENT },
    { { { .row = ASSERT_STATEMENT, .field = "name", .text = "x", .from = -1 } }, SEGMENT },
    { { { .row = ASSERT_STATEMENT, .field = "literal", .value = 1, .from = -1 } }, SEGMENT },
    { { EDIT(ASSERT_COMPARE, "kind", SCALAR_ARITHMETIC_ROW),
        EDIT(ASSERT_COMPARE, "type", SCALAR_INT) },
      "scalar condition is not one bool" },
};
/* Arithmetic's operator, arity and result, and arithmetic on a bool. */
static const malformed_guard MALFORMED_ARITHMETIC[] = {
    { { EDIT(ARITHMETIC_ADD, "value", 0) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_ADD, "value", 11) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_ADD, "count", 1) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_ADD, "type", SCALAR_BOOL) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_NEGATE, "value", 0) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_NEGATE, "value", 3) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_NEGATE, "count", 2) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_SECOND_READ, "kind", SCALAR_LITERAL_ROW),
        EDIT(ARITHMETIC_SECOND_READ, "type", SCALAR_BOOL),
        EDIT(ARITHMETIC_SECOND_READ, "value", 0),
        EDIT(ARITHMETIC_SECOND_READ, "literal", 1) },
      ARITHMETIC },
    { { EDIT(ARITHMETIC_LAST_READ, "kind", SCALAR_LITERAL_ROW),
        EDIT(ARITHMETIC_LAST_READ, "type", SCALAR_BOOL),
        EDIT(ARITHMETIC_LAST_READ, "value", 0) },
      ARITHMETIC },
};
/* The kind of each of WIDTH_SOURCE's rows, and the rows edited. */
static const int WIDTH_KINDS[] = {
    SCALAR_SOURCE_ROW,  SCALAR_DOCUMENT_ROW,   SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,  SCALAR_LITERAL_ROW,    SCALAR_CALL_ROW,
    SCALAR_CONVERT_ROW, SCALAR_FUNCTION_ROW,   SCALAR_PARAMETER_ROW,
    SCALAR_RETURN_ROW,  SCALAR_READ_ROW,       SCALAR_CONVERT_ROW,
    SCALAR_LITERAL_ROW, SCALAR_ARITHMETIC_ROW,
};
enum
{
    WIDTH_WIDENED = 6,
    WIDTH_READ = 10,
    WIDTH_WRAPPED = 11,
    WIDTH_SHIFT_COUNT = 12,
    WIDTH_SHIFT = 13,
    WIDTH_ROWS = (int)(sizeof WIDTH_KINDS / sizeof *WIDTH_KINDS)
};
/* A conversion's mode, arity, type and operand, a literal outside its type,
 * a literal on another row, mixed shift operands, and a return that does not
 * widen to its result. */
static const char
    CONVERSION[] = "scalar conversion is not one the runtime implements";
static const malformed_guard MALFORMED_WIDTHS[] = {
    { { EDIT(WIDTH_WIDENED, "value", 2) }, CONVERSION },
    { { EDIT(WIDTH_WIDENED, "count", 2) }, CONVERSION },
    { { EDIT(WIDTH_WRAPPED, "type", SCALAR_BOOL) }, CONVERSION },
    { { EDIT(WIDTH_READ, "kind", SCALAR_LITERAL_ROW),
        EDIT(WIDTH_READ, "type", SCALAR_BOOL),
        EDIT(WIDTH_READ, "value", 0) },
      CONVERSION },
    { { EDIT(WIDTH_SHIFT_COUNT, "literal", 256) }, "noncanonical scalar literal" },
    { { EDIT(WIDTH_SHIFT_COUNT, "literal", -1) }, "noncanonical scalar literal" },
    { { EDIT(WIDTH_READ, "literal", 1) }, "invalid scalar operand row" },
    { { EDIT(WIDTH_SHIFT_COUNT, "type", SCALAR_INT) }, ARITHMETIC },
    { { EDIT(WIDTH_SHIFT, "value", 11) }, ARITHMETIC },
    { { EDIT(WIDTH_WIDENED, "type", SCALAR_BYTE) },
      "scalar return differs from its function's result" },
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
/* native_abi's target `function`, such as "LinuxX64", on VM b. */

/* scalar_llvm's EmitBytesFor of `rows` and native_abi's target `function`. */

static bool validated(pin_value *rows, const char *message)
{
    char *error, *ir;
    REQUIRE(scalar_host_validate(rows, &error));
    REQUIRE(scalar_host_emit("LinuxX64", rows, &ir));
    bool valid = !strcmp(error, message);
    if (!valid) {
        fprintf(stderr, "scalar_llvm Validate gave \"%s\", not \"%s\"\n", error, message);
        const pin_array *dump = rows->ptr;
        for (int i = 0; i < dump->count && i < 40; i++) {
            fprintf(
                stderr,
                "row %d kind %d type %d value %d count %d literal %lld start "
                "%d end %d line %d col %d\n",
                i,
                (int)scalar_field(&dump->items[i], "kind").i,
                (int)scalar_field(&dump->items[i], "type").i,
                (int)scalar_field(&dump->items[i], "value").i,
                (int)scalar_field(&dump->items[i], "count").i,
                (long long)scalar_field(&dump->items[i], "literal").i,
                (int)scalar_field(&dump->items[i], "start").i,
                (int)scalar_field(&dump->items[i], "end").i,
                (int)scalar_field(&dump->items[i], "line").i,
                (int)scalar_field(&dump->items[i], "column").i
            );
        }
    }
    bool emitted = *ir;
    free(error);
    free(ir);
    REQUIRE(valid && emitted == !*message);
    return true;
}
/* Each edit applied to the producer's rows, the message Validate gives, and
 * the rows restored. */
static bool rejects_guard(pin_value *rows, const malformed_guard *malformed)
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
        if (saved[i].type == PIN_I64) {
            value = (pin_value){ .type = PIN_I64, .i64 = edit->value };
        }
        if (edit->text) {
            value = text(edit->text);
        }
        if (edit->from >= 0) {
            value = scalar_field(guard_row(rows, edit->from), edit->field);
        }
        write_row(rows, edit->row, edit->field, value);
    }
    bool rejected = validated(rows, malformed->message);
    for (int i = count - 1; i >= 0; i--) {
        write_row(rows, malformed->edits[i].row, malformed->edits[i].field, saved[i]);
    }
    return rejected;
}
/* scalar_llvm accepts the producer's rows, of the `kinds` it gives, rejects
 * each malformed variant of them when its layout, operators, operand types,
 * ranges or calls are not what a producer gives, and emits nothing for it. */
static bool rejects_malformed(
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
    REQUIRE(validated(rows, ""));
    bool rejected = true;
    for (int i = 0; i < variants; i++) {
        if (!rejects_guard(rows, &malformed[i])) {
            fprintf(stderr, "malformed variant %d\n", i);
            rejected = false;
        }
    }
    REQUIRE(rejected);
    REQUIRE(validated(rows, ""));
    return true;
}
/* WIDTH_SOURCE's typed rows: 298 is an int argument that Low wraps to a byte,
 * its shift count is a byte literal, and Main widens Low's byte to int. */
static bool widened_rows(const pin_value *rows)
{
    static const struct
    {
        int row, type, value;
        int64_t literal;
    } EXPECTED[] = {
        { 4, SCALAR_INT, 0, 298 },
        { 5, SCALAR_BYTE, 1, 0 },
        { WIDTH_WIDENED, SCALAR_INT, 0, 0 },
        { WIDTH_READ, SCALAR_INT, 0, 0 },
        { WIDTH_WRAPPED, SCALAR_BYTE, 1, 0 },
        { WIDTH_SHIFT_COUNT, SCALAR_BYTE, 0, 1 },
        { WIDTH_SHIFT, SCALAR_BYTE, 9, 0 },
    };
    for (int i = 0; i < (int)(sizeof EXPECTED / sizeof *EXPECTED); i++) {
        pin_value *row = guard_row(rows, EXPECTED[i].row);
        REQUIRE(scalar_field(row, "type").i == EXPECTED[i].type);
        REQUIRE(scalar_field(row, "value").i == EXPECTED[i].value);
        REQUIRE(scalar_field(row, "literal").i64 == EXPECTED[i].literal);
    }
    return true;
}
/* LOOP_ROWS_SOURCE's for row: an exclusive range over int bounds that
 * declares i, Sum's third variable. */
static bool looped_rows(const pin_value *rows)
{
    pin_value *row = guard_row(rows, LOOP_FOR);
    REQUIRE(scalar_field(row, "type").i == SCALAR_INT);
    REQUIRE(scalar_field(row, "value").i == 2);
    REQUIRE(scalar_field(row, "count").i == 2);
    REQUIRE(scalar_field(row, "literal").i64 == SCALAR_RANGE_EXCLUSIVE);
    REQUIRE(retained_text(row, "name", "i"));
    REQUIRE(scalar_field(guard_row(rows, LOOP_TRUE), "literal").i64 == 1);
    return true;
}
/* SWITCH_ROWS_SOURCE's switch and arms: each of x's type, a value arm with
 * its one literal and the default arm with none. */
static bool switched_rows(const pin_value *rows)
{
    static const struct
    {
        int row, count;
    } EXPECTED[] = { { SWITCH_ROW, 1 },
                     { SWITCH_FIRST, 1 },
                     { SWITCH_DEFAULT, 0 },
                     { SWITCH_LAST, 1 } };
    for (int i = 0; i < (int)(sizeof EXPECTED / sizeof *EXPECTED); i++) {
        pin_value *row = guard_row(rows, EXPECTED[i].row);
        REQUIRE(scalar_field(row, "type").i == SCALAR_INT);
        REQUIRE(scalar_field(row, "count").i == EXPECTED[i].count);
    }
    REQUIRE(scalar_field(guard_row(rows, SWITCH_LAST_VALUE), "literal").i64 == 20);
    return true;
}
/* RECORD_ROWS_SOURCE's record, its fields in declared order, and the record
 * type of Make's result, Main's local and their rows; the literal's int
 * field is updated last, as written. */
static bool recorded_rows(const pin_value *rows)
{
    pin_value *record = guard_row(rows, RECORD_DECLARATION);
    REQUIRE(
        retained_text(record, "name", "Point") && scalar_field(record, "count").i == 2
    );
    REQUIRE(retained_text(guard_row(rows, RECORD_X), "name", "x"));
    REQUIRE(scalar_field(guard_row(rows, RECORD_X), "type").i == SCALAR_INT);
    REQUIRE(retained_text(guard_row(rows, RECORD_Y), "name", "y"));
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y), "type").i == SCALAR_FLOAT);
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y), "value").i == 1);
    static const int RECORD_TYPED[] = { RECORD_LOCAL, RECORD_ASSIGN,
                                        RECORD_READ,  RECORD_MAKE,
                                        RECORD_ZERO,  RECORD_Y_UPDATE };
    for (int i = 0; i < (int)(sizeof RECORD_TYPED / sizeof *RECORD_TYPED); i++) {
        REQUIRE(scalar_field(guard_row(rows, RECORD_TYPED[i]), "type").i == SCALAR_RECORD);
    }
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y_UPDATE), "value").i == 1);
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y_UPDATE + 2), "value").i == 0);
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y_UPDATE - 1), "type").i == SCALAR_FLOAT);
    REQUIRE(scalar_field(guard_row(rows, RECORD_FIELD), "type").i == SCALAR_INT);
    return true;
}
/* STRING_ROWS_SOURCE's strings: each literal holds its decoded bytes and
 * literal 0, its variables, Name's result and its read are strings, and its
 * comparison gives a bool. Its IR holds one text, which both literals name.
 * A literal longer than any source is noncanonical. */
static bool stringed_rows(pin_value *rows)
{
    static const int STRING_TYPED[] = { STRING_LOCAL,         STRING_ARGUMENT,
                                        STRING_READ,          STRING_LITERAL,
                                        STRING_NAME,          STRING_PARAMETER,
                                        STRING_PARAMETER_READ };
    for (int i = 0; i < (int)(sizeof STRING_TYPED / sizeof *STRING_TYPED); i++) {
        REQUIRE(scalar_field(guard_row(rows, STRING_TYPED[i]), "type").i == SCALAR_STRING);
    }
    static const int STRING_LITERALS[] = { STRING_ARGUMENT, STRING_LITERAL };
    for (int i = 0; i < 2; i++) {
        pin_value *row = guard_row(rows, STRING_LITERALS[i]);
        REQUIRE(
            retained_text(row, "text", "one") && scalar_field(row, "literal").i64 == 0
        );
    }
    REQUIRE(scalar_field(guard_row(rows, STRING_COMPARE), "type").i == SCALAR_BOOL);
    char *ir;
    REQUIRE(target_ir("LinuxX64", rows, &ir) && ir);
    bool shared = strstr(ir, "@chibi_scalar.text.0 = private unnamed_addr constant [4 x i8] c\"one\\00\"") &&
                  !strstr(ir, "@chibi_scalar.text.1");
    free(ir);
    REQUIRE(shared);
    pin_value *literal = guard_row(rows, STRING_LITERAL);
    pin_value saved = scalar_field(literal, "text");
    /* A literal has no length bound. */
    enum
    {
        LONG_TEXT = 40000
    };
    char *long_text = xmalloc(LONG_TEXT + 1);
    memset(long_text, 'a', LONG_TEXT);
    long_text[LONG_TEXT] = 0;
    write_row(rows, STRING_LITERAL, "text", text(long_text));
    free(long_text);
    REQUIRE(validated(rows, ""));
    write_row(rows, STRING_LITERAL, "text", saved);
    return true;
}
/* TEXT_ROWS_SOURCE's Text rows: each names its operator and the operands it
 * consumes, a slice gives a string and a byte count or byte an int. */
static bool texted_rows(pin_value *rows)
{
    static const struct
    {
        int row, value, count, type;
    } TEXTS[] = {
        { TEXT_SLICE, SCALAR_TEXT_SLICE, 3, SCALAR_STRING },
        { TEXT_LENGTH, SCALAR_TEXT_LENGTH, 1, SCALAR_INT },
        { TEXT_BYTE, SCALAR_TEXT_BYTE, 2, SCALAR_INT },
    };
    for (int i = 0; i < (int)(sizeof TEXTS / sizeof *TEXTS); i++) {
        pin_value *row = guard_row(rows, TEXTS[i].row);
        REQUIRE(scalar_field(row, "value").i == TEXTS[i].value);
        REQUIRE(scalar_field(row, "count").i == TEXTS[i].count);
        REQUIRE(scalar_field(row, "type").i == TEXTS[i].type);
    }
    REQUIRE(retained_text(guard_row(rows, TEXT_SOURCE), "text", "abcd"));
    return true;
}
/* ASSERT_ROWS_SOURCE's assert has its call's range and its condition's
 * three operands, and lowers to a branch past a Fail that names the range in
 * a failure constant, which the context carries. */
static bool asserted_rows(pin_value *rows)
{
    static const char call[] = "assert(x < 2, \"x is small\")";
    const pin_value *row = guard_row(rows, ASSERT_STATEMENT);
    int start = (int)(strstr(ASSERT_ROWS_SOURCE, call) - ASSERT_ROWS_SOURCE);
    REQUIRE(scalar_field(row, "count").i == 3 && scalar_field(row, "type").i == 0);
    REQUIRE(scalar_field(row, "start").i == start);
    REQUIRE(scalar_field(row, "end").i == start + (int)strlen(call));
    REQUIRE(scalar_field(row, "line").i == 4 && scalar_field(row, "column").i == 5);
    char *ir, constant[160];
    REQUIRE(target_ir("LinuxX64", rows, &ir) && ir);
    snprintf(
        constant,
        sizeof constant,
        "@chibi_scalar.failure.0.7 = private unnamed_addr constant { i64, "
        "i32, i32 } { i64 0, i32 %d, i32 %d }\n",
        start,
        start + (int)strlen(call)
    );
    bool lowered = strstr(ir, constant) &&
                   strstr(ir, "define i32 @chibi_scalar(ptr %ctx) #0 {\n") &&
                   strstr(ir, "  br i1 %a6, label %p8, label %p7\n") &&
                   strstr(
                       ir,
                       "p7:\n  %fail7.at = getelementptr inbounds { ptr, ptr, "
                       "ptr, ptr }, ptr %ctx, i32 0, i32 1\n"
                       "  store ptr @chibi_scalar.failure.0.7, ptr "
                       "%fail7.at\n  br label %failed\n"
                   ) &&
                   strstr(ir, "failed:\n  ret i32 0\n");
    if (!lowered) {
        fprintf(stderr, "%s\n", ir);
    }
    free(ir);
    REQUIRE(lowered);
    return true;
}
/* FORMAT_ROWS_SOURCE's format consumes its format and each argument as it
 * is to give a string. */
static bool formatted_rows(pin_value *rows)
{
    pin_value *row = guard_row(rows, FORMAT_TEXT);
    REQUIRE(scalar_field(row, "value").i == SCALAR_TEXT_FORMAT);
    REQUIRE(scalar_field(row, "count").i == 4 && scalar_field(row, "type").i == SCALAR_STRING);
    REQUIRE(scalar_field(guard_row(rows, FORMAT_INT), "type").i == SCALAR_INT64);
    REQUIRE(retained_text(guard_row(rows, FORMAT_FORMAT), "text", "{}{}{}"));
    return true;
}
/* UNION_ROWS_SOURCE's union, its variants and their payloads numbered across
 * it, the union type of Make's result, Main's local and the rows that read or
 * make one, the Tag and variant ordinals its `is` and arms test, its last
 * arm as the default, and its bindings' locals. */
static bool unioned_rows(const pin_value *rows)
{
    pin_value *declared = guard_row(rows, UNION_DECLARATION);
    REQUIRE(
        retained_text(declared, "name", "Shape") &&
        scalar_field(declared, "count").i == 2
    );
    REQUIRE(retained_text(guard_row(rows, UNION_DOT), "name", "Dot"));
    REQUIRE(scalar_field(guard_row(rows, UNION_DOT), "count").i == 0);
    REQUIRE(retained_text(guard_row(rows, UNION_BOX), "name", "Box"));
    REQUIRE(scalar_field(guard_row(rows, UNION_BOX), "value").i == 1);
    REQUIRE(scalar_field(guard_row(rows, UNION_BOX), "count").i == 2);
    REQUIRE(retained_text(guard_row(rows, UNION_W), "name", "w"));
    REQUIRE(scalar_field(guard_row(rows, UNION_W), "type").i == SCALAR_INT);
    REQUIRE(retained_text(guard_row(rows, UNION_H), "name", "h"));
    REQUIRE(scalar_field(guard_row(rows, UNION_H), "type").i == SCALAR_FLOAT);
    REQUIRE(scalar_field(guard_row(rows, UNION_H), "value").i == 1);
    static const int UNION_TYPED[] = { UNION_LOCAL,
                                       UNION_LOCAL + 2,
                                       UNION_IS_TAG - 1,
                                       UNION_MAKE };
    for (int i = 0; i < (int)(sizeof UNION_TYPED / sizeof *UNION_TYPED); i++) {
        REQUIRE(scalar_field(guard_row(rows, UNION_TYPED[i]), "type").i == SCALAR_RECORD);
    }
    REQUIRE(scalar_field(guard_row(rows, UNION_MAKE), "value").i == 1);
    REQUIRE(scalar_field(guard_row(rows, UNION_MAKE), "count").i == 2);
    static const int INT_TYPED[] = { UNION_IS_TAG,  UNION_IS_VARIANT,
                                     UNION_SWITCH,  UNION_SWITCH_TAG,
                                     UNION_DOT_ARM, UNION_DOT_VARIANT,
                                     UNION_BOX_ARM, UNION_W_LOCAL };
    for (int i = 0; i < (int)(sizeof INT_TYPED / sizeof *INT_TYPED); i++) {
        REQUIRE(scalar_field(guard_row(rows, INT_TYPED[i]), "type").i == SCALAR_INT);
    }
    REQUIRE(scalar_field(guard_row(rows, UNION_IS_VARIANT), "literal").i64 == 0);
    REQUIRE(scalar_field(guard_row(rows, UNION_DOT_ARM), "count").i == 1);
    REQUIRE(scalar_field(guard_row(rows, UNION_BOX_ARM), "count").i == 0);
    REQUIRE(retained_text(guard_row(rows, UNION_W_LOCAL), "name", "w"));
    REQUIRE(retained_text(guard_row(rows, UNION_H_LOCAL), "name", "h"));
    REQUIRE(scalar_field(guard_row(rows, UNION_H_LOCAL), "type").i == SCALAR_FLOAT);
    REQUIRE(scalar_field(guard_row(rows, UNION_H_LOCAL), "value").i == 2);
    REQUIRE(scalar_field(guard_row(rows, UNION_W_PAYLOAD), "value").i == 0);
    REQUIRE(scalar_field(guard_row(rows, UNION_H_PAYLOAD), "value").i == 1);
    return true;
}
/* Every produced call names a concrete numeric function, and erased
 * callable arguments never become runtime parameters or operand rows. */
static bool generic_scalar_rows(const pin_value *rows)
{
    const pin_array *array = rows->ptr;
    REQUIRE(array->count > 1);
    pin_value *document = guard_row(rows, 1);
    if (scalar_field(document, "kind").i == SCALAR_REJECTION_ROW) {
        fprintf(
            stderr,
            "generic scalar: %s\n",
            pin_text(scalar_field(document, "text")).bytes
        );
    }
    REQUIRE(array->count > 2);
    pin_value *first = guard_row(rows, 2);
    REQUIRE(scalar_field(first, "kind").i == SCALAR_FUNCTION_ROW);
    pin_value *functions[8];
    int count = 0, calls = 0;
    for (int i = 2; i < array->count; i++) {
        pin_value *row = guard_row(rows, i);
        if (scalar_field(row, "kind").i != SCALAR_FUNCTION_ROW) {
            continue;
        }
        REQUIRE(count < 8);
        functions[count++] = row;
        REQUIRE(scalar_field(row, "literal").i64 == 0);
        REQUIRE(scalar_field(row, "count").i <= 1);
    }
    REQUIRE(count == 7);
    for (int i = 2; i < array->count; i++) {
        pin_value *row = guard_row(rows, i);
        if (scalar_field(row, "kind").i != SCALAR_CALL_ROW) {
            continue;
        }
        int target = scalar_field(row, "value").i;
        REQUIRE(target >= 0 && target < count);
        REQUIRE(
            scalar_field(row, "count").i ==
            scalar_field(functions[target], "count").i
        );
        REQUIRE(pin_text_equal(
            pin_text(scalar_field(row, "name")),
            pin_text(scalar_field(functions[target], "name"))
        ));
        calls++;
    }
    REQUIRE(calls == 6);
    return true;
}
/* CollectScalar's rows own their shape and strings: they outlive the
 * producer's packages and inputs, and a consumer prepared afterwards emits
 * them as LLVM IR that keeps the call, and validates guarded and arithmetic
 * rows. */
TEST_CASE(package_compiler_scalar_rows_outlive_the_producer)
{
    REQUIRE(prepare());
    pin_value produced[2], guarded[2], computed[2], widened[2], controlled[2],
        looped[2], switched[2], recorded[2], unioned[2], stringed[2], texted[2],
        formatted[2], asserted[2], specialized[2], specialized_integer[2];
    const char *const texts[] = {
        SCALAR_SOURCE,      GUARD_SOURCE,          ARITHMETIC_SOURCE,
        WIDTH_SOURCE,       CONTROL_ROWS_SOURCE,   LOOP_ROWS_SOURCE,
        SWITCH_ROWS_SOURCE, RECORD_ROWS_SOURCE,    UNION_ROWS_SOURCE,
        STRING_ROWS_SOURCE, TEXT_ROWS_SOURCE,      FORMAT_ROWS_SOURCE,
        ASSERT_ROWS_SOURCE, GENERIC_SCALAR_SOURCE, GENERIC_INTEGER_SCALAR_SOURCE
    };
    pin_value *outputs[] = { produced, guarded,     computed,
                             widened,  controlled,  looped,
                             switched, recorded,    unioned,
                             stringed, texted,      formatted,
                             asserted, specialized, specialized_integer };
    for (size_t i = 0; i < sizeof texts / sizeof *texts; i++) {
        REQUIRE(scalar_host_collect("Scalar.chibi", texts[i], &detached, &outputs[i][0]));
    }
    for (int b = 0; b < 1; b++) {
        REQUIRE(generic_scalar_rows(&specialized[b]));
        REQUIRE(validated(&specialized[b], ""));
        REQUIRE(generic_scalar_rows(&specialized_integer[b]));
        REQUIRE(validated(&specialized_integer[b], ""));
        int64_t expected;
        REQUIRE(scalar_host_case_value(&specialized_integer[b], &expected));
        REQUIRE(expected == 200);
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
        char *ir;
        REQUIRE(target_ir("LinuxX64", &produced[b], &ir) && ir);
        bool called = strstr(ir, "define internal i32 @chibi_scalar.1(i32 %arg0, i1 %arg1)") &&
                      strstr(ir, "call i32 @chibi_scalar.1(i32 %c3_0, i1 %c3_1)");
        free(ir);
        REQUIRE(called);
        REQUIRE(rejects_malformed(
            &guarded[b],
            GUARD_KINDS,
            GUARD_ROWS,
            MALFORMED_GUARDS,
            (int)(sizeof MALFORMED_GUARDS / sizeof *MALFORMED_GUARDS)
        ));
        REQUIRE(rejects_malformed(
            &computed[b],
            ARITHMETIC_KINDS,
            ARITHMETIC_ROWS,
            MALFORMED_ARITHMETIC,
            (int)(sizeof MALFORMED_ARITHMETIC / sizeof *MALFORMED_ARITHMETIC)
        ));
        REQUIRE(widened_rows(&widened[b]));
        REQUIRE(rejects_malformed(
            &widened[b],
            WIDTH_KINDS,
            WIDTH_ROWS,
            MALFORMED_WIDTHS,
            (int)(sizeof MALFORMED_WIDTHS / sizeof *MALFORMED_WIDTHS)
        ));
        REQUIRE(rejects_malformed(
            &controlled[b],
            CONTROL_KINDS,
            CONTROL_ROWS,
            MALFORMED_CONTROL,
            (int)(sizeof MALFORMED_CONTROL / sizeof *MALFORMED_CONTROL)
        ));
        REQUIRE(looped_rows(&looped[b]));
        REQUIRE(rejects_malformed(
            &looped[b],
            LOOP_KINDS,
            LOOP_ROWS,
            MALFORMED_LOOPS,
            (int)(sizeof MALFORMED_LOOPS / sizeof *MALFORMED_LOOPS)
        ));
        REQUIRE(switched_rows(&switched[b]));
        REQUIRE(rejects_malformed(
            &switched[b],
            SWITCH_KINDS,
            SWITCH_ROWS,
            MALFORMED_SWITCHES,
            (int)(sizeof MALFORMED_SWITCHES / sizeof *MALFORMED_SWITCHES)
        ));
        REQUIRE(rejects_malformed(
            &recorded[b],
            RECORD_KINDS,
            RECORD_ROWS,
            MALFORMED_RECORDS,
            (int)(sizeof MALFORMED_RECORDS / sizeof *MALFORMED_RECORDS)
        ));
        REQUIRE(recorded_rows(&recorded[b]));
        REQUIRE(unioned_rows(&unioned[b]));
        REQUIRE(rejects_malformed(
            &unioned[b],
            UNION_KINDS,
            UNION_ROWS,
            MALFORMED_UNIONS,
            (int)(sizeof MALFORMED_UNIONS / sizeof *MALFORMED_UNIONS)
        ));
        REQUIRE(rejects_malformed(
            &stringed[b],
            STRING_KINDS,
            STRING_ROWS,
            MALFORMED_STRINGS,
            (int)(sizeof MALFORMED_STRINGS / sizeof *MALFORMED_STRINGS)
        ));
        REQUIRE(stringed_rows(&stringed[b]));
        REQUIRE(texted_rows(&texted[b]));
        REQUIRE(rejects_malformed(
            &texted[b],
            TEXT_KINDS,
            TEXT_ROWS,
            MALFORMED_TEXTS,
            (int)(sizeof MALFORMED_TEXTS / sizeof *MALFORMED_TEXTS)
        ));
        REQUIRE(formatted_rows(&formatted[b]));
        REQUIRE(rejects_malformed(
            &formatted[b],
            FORMAT_KINDS,
            FORMAT_ROWS,
            MALFORMED_FORMATS,
            (int)(sizeof MALFORMED_FORMATS / sizeof *MALFORMED_FORMATS)
        ));
        REQUIRE(rejects_malformed(
            &asserted[b],
            ASSERT_KINDS,
            ASSERT_ROWS,
            MALFORMED_ASSERTS,
            (int)(sizeof MALFORMED_ASSERTS / sizeof *MALFORMED_ASSERTS)
        ));
        REQUIRE(asserted_rows(&asserted[b]));
    }
    return true;
}
/* llvm_native's op and type ordinals, and its tables as Emit takes them.
 * Functions own no locals. */
enum
{
    NATIVE_BOOL = 1,
    NATIVE_INT = 6,
    NATIVE_FLOAT = 10,
    NATIVE_CONSTANT = 0,
    NATIVE_ARGUMENT = 1,
    NATIVE_PUBLISH = 4,
    NATIVE_RETURN = 5,
    NATIVE_NE = 24,
    NATIVE_CHECK = 27,
    NATIVE_CALL = 28,
    NATIVE_INSERT = 30,
    NATIVE_EXTRACT = 31,
    NATIVE_TEXT = 13,
    NATIVE_RECORD = 32,
    NATIVE_ADD = 7,
    NATIVE_TEXT_BYTE = 33,
    NATIVE_TEXT_DROP = 34,
    NATIVE_TEXT_TAKE = 35
};
typedef struct native_code
{
    int op, type, output, left, right, target;
    int64_t literal;
} native_code;
typedef struct native_function
{
    int result, first_parameter, parameters, first_slot, slots, first_code,
        code, first_argument, arguments;
} native_function;
typedef struct native_record
{
    int first, count;
} native_record;
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
    const native_record *records;
    int record_count;
    const int *fields;
    int field_count;
    const char *const *texts;
    int text_count;
    /* Line tables: the source file, or NULL for none, its directory, each
     * function's name and each code row's line and column. */
    const char *file, *directory;
    const char *const *names;
    const int *lines, *columns;
} native_module;

static const char *const CODE_FIELDS[] = { "op",    "type",   "output", "left",
                                           "right", "target", "literal" };
static const char *const FUNCTION_FIELDS[] = { "result",     "first_parameter",
                                               "parameters", "first_slot",
                                               "slots",      "first_code",
                                               "code",       "first_argument",
                                               "arguments" };
static const char *const RECORD_FIELDS[] = { "first", "count" };
enum
{
    CODE_WIDTH = (int)(sizeof CODE_FIELDS / sizeof *CODE_FIELDS),
    FUNCTION_WIDTH = (int)(sizeof FUNCTION_FIELDS / sizeof *FUNCTION_FIELDS),
    RECORD_WIDTH = (int)(sizeof RECORD_FIELDS / sizeof *RECORD_FIELDS)
};
/* count Instruction, Function or Record records whose fields, in declaration
 * order, take consecutive values. No records is an empty array. */

/* native_abi's target `function` and m's tables, the arguments Emit and
 * EmitLibrary share. */

/* Emits m for native_abi's target `function` under budget 1, its first
 * function the entry, or `main` for a program. */

/* Emits m for native_abi's target `function` with its entry chibi_scalar. */

/* Emits m for native_abi's target `function` as the library `name` under
 * budget 1, its functions named and identified by `names` and `ids`, `count`
 * of each. */

/* Emits m for Linux x86-64. */

/* main() returns Pick(7, true, 9), and Pick :: (int, bool, int) -> int
 * returns its third argument. Slots s0..s2 hold the arguments in call order. */

/* The call module as the library Lib: every function internal, one wrapper
 * per word signature, and the manifest the only exported symbol. */

/* Sets one field of code[change.index], at the width of that field. */

/* The IR native_emit gives for m. */

/* The native LLVM fixtures target 64-bit layouts. Portable hosts still store
 * pin_string as their own bytes pointer, 32-bit length and owner pointer. */

#if UINTPTR_MAX == UINT64_MAX

#else

#endif
/* Main returns whether its two texts differ: each constant is a private
 * global of its bytes and a NUL, named by its index in the text table, and
 * `!=` negates the module's one byte comparison. */

/* The text module with one instruction field changed. */

/* CollectScalarPackages' arguments for the project files `inputs`: no
 * collection, the paths and sources, this host's target and the natives. */

/* A function of an imported project package is one module with its caller:
 * the entry is the first function of the first file, whichever package order
 * puts the dependency in front, the qualified call is a call row to the
 * import's function, which the emitter lowers as a call, and a qualified
 * constant is the value it folds to. */
TEST_CASE(package_compiler_scalar_rows_span_imported_packages)
{
    REQUIRE(prepare());
    static const source_package_input packages[] = {
        { .path = "Main.chibi",
          .text = "package main\nimport lib \"project:lib\"\n"
                  "Main :: () -> int { return lib.Add(3, lib.Twice(2)) * "
                  "lib.Limit; }\n" },
        { .path = "lib/Lib.chibi",
          .text = "package lib\npub Limit :: 5;\n"
                  "pub Twice :: (n: int) -> int { return n * 2; "
                  "}\n"
                  "pub Add :: (a: int, b: int) -> int { return a + b; }\n" },
    };
    pin_value produced;
    REQUIRE(scalar_host_collect_packages(packages, 2, &detached, &produced));
    const pin_array *rows = produced.ptr;
    REQUIRE(produced.container == PIN_CONTAINER_ARRAY && rows->count > 1);
    if (rows->count == 2) {
        fprintf(
            stderr,
            "scalar packages: %s\n",
            pin_text(scalar_field(guard_row(&produced, 1), "text")).bytes
        );
    }
    REQUIRE(rows->count > 2);
    REQUIRE(scalar_field(&rows->items[2], "kind").i != SCALAR_REJECTION_ROW);
    REQUIRE(retained_text(&rows->items[2], "name", "Main"));
    REQUIRE(validated(&produced, ""));
    char *ir;
    REQUIRE(target_ir("LinuxX64", &produced, &ir) && ir);
    bool spans = strstr(ir, "define internal i32 @chibi_scalar.1(") &&
                 strstr(ir, "define internal i32 @chibi_scalar.2(") &&
                 strstr(ir, "call i32 @chibi_scalar.");
    if (!spans) {
        fprintf(stderr, "%s\n", ir);
    }
    free(ir);
    REQUIRE(spans);
    return true;
}
/* A package set may import a collection's packages, such as core:strings,
 * whose files lie among the inputs under the collection's path. */
TEST_CASE(package_compiler_scalar_rows_span_collection_packages)
{
    REQUIRE(prepare());
    static const source_package_input packages[] = {
        { .path = "Main.chibi",
          .text = "package main\nimport strings \"core:strings\"\n"
                  "Main :: () -> int { return strings.ByteLength(\"hello\"); "
                  "}\n" },
        { .path = "/sdk/core/strings/Strings.chibi",
          .text = "package strings\npub ByteLength :: (value: string) -> int "
                  "{ return text_byte_length(value); }\n" },
    };
    pin_value produced;
    REQUIRE(
        scalar_host_collect_collection("core", "/sdk/core", packages, 2, &detached, &produced)
    );
    const pin_array *rows = produced.ptr;
    REQUIRE(produced.container == PIN_CONTAINER_ARRAY && rows->count > 1);
    if (rows->count == 2) {
        fprintf(
            stderr,
            "scalar collection: %s\n",
            pin_text(scalar_field(guard_row(&produced, 1), "text")).bytes
        );
    }
    REQUIRE(rows->count > 2);
    REQUIRE(validated(&produced, ""));
    char *ir;
    REQUIRE(target_ir("LinuxX64", &produced, &ir) && ir);
    bool spans = strstr(ir, "define internal i32 @chibi_scalar.1(") != NULL;
    free(ir);
    REQUIRE(spans);
    return true;
}
/* A rejection inside an imported package lies in the joined source its rows
 * hold, at the declaration that caused it. */
TEST_CASE(package_compiler_scalar_rejections_lie_in_package_sets)
{
    REQUIRE(prepare());
    static const source_package_input packages[] = {
        { .path = "Main.chibi",
          .text = "package main\nimport lib \"project:lib\"\n"
                  "Main :: () -> int { return lib.Add(1, 2); }\n" },
        { .path = "lib/Lib.chibi",
          .text = "package lib\npub Add :: (a: int, b: int) -> int { return a "
                  "+ b; }\n"
                  "pub Table :: struct { m: [int: int]; }\n" },
    };
    pin_value produced;
    REQUIRE(scalar_host_collect_packages(packages, 2, &detached, &produced));
    const pin_array *rows = produced.ptr;
    REQUIRE(rows->count == 2);
    REQUIRE(scalar_field(&rows->items[1], "kind").i == SCALAR_REJECTION_ROW);
    const char *joined = pin_text(scalar_field(&rows->items[0], "text")).bytes;
    int start = (int)scalar_field(&rows->items[1], "start").i;
    int end = (int)scalar_field(&rows->items[1], "end").i;
    REQUIRE(start > (int)strlen(packages[0].text) && end <= (int)strlen(joined));
    REQUIRE(!strncmp(joined + start, "m: [int: int];", (size_t)(end - start)));
    return true;
}
/* llvm_native's call and float contracts: definitions, argument order,
 * floating encodings and conversions, and the malformed or cyclic plans it
 * rejects before writing IR. */

/* A shared wrong result is not an oracle. Each executed value must match
 * the fixture's literal, including Main's value. */
static bool scalar_results_match(const int64_t values[4], int64_t expected)
{
    return values[0] == expected && values[1] == expected &&
           values[2] == expected && values[3] == expected;
}
/* Forms the scalar subset cannot express, each with its message and the
 * source its diagnostic covers: an `is` that binds, map and pointer payloads, a text native given a named argument,
 * a record formatted and a call of another native. */
static const struct
{
    const char *source, *message, *excerpt;
} UNSUPPORTED[] = {
    { "package scalar\nShape :: union { Dot | Box(w: int) }\n"
      "Main :: () -> int { s := Shape.Box(1); if (s is .Box(w)) { return w; } "
      "return 0; }\n",
      "scalar proof binds payloads only in switch arms",
      ".Box(w)" },
    { "package scalar\nNamed :: union { Tag(items: [int: int]) }\nMain :: () "
      "-> int "
      "{ return 1; }\n",
      "scalar proof requires number, bool, string, array, record and union "
      "payloads",
      "items: [int: int]" },
    { "package scalar\nWrapped :: union { Of(p: ^int) }\n"
      "Main :: () -> int { return 1; }\n",
      "scalar proof requires number, bool, string, array, record and union "
      "payloads",
      "p: ^int" },
    { "package scalar\nMain :: () -> int { return text_byte_length(Value: "
      "\"ab\"); }\n",
      "scalar proof passes a text native's arguments in order",
      "Value: \"ab\"" },
    { "package scalar\nPoint :: struct { x: int; }\n"
      "Main :: () -> int { p: Point = {x: 1}; return "
      "text_byte_length(format_text(\"{}\", p)); }\n",
      "scalar proof formats only numbers, bools and strings",
      "p" },
    /* The call step types format_text's arguments first, as the VM's. */
    { "package scalar\nMain :: () -> int { return "
      "text_byte_length(format_text(\"{}\", 7)); }\n",
      "a number needs an integer type here",
      "7" },
    /* No native but a text read is known. */
    { "package scalar\nMain :: () -> int { if (text_equal(\"a\", \"b\")) { "
      "return 1; } return 0; }\n",
      "Unknown function 'text_equal'",
      "text_equal" },
};
/* Each of UNSUPPORTED is a located rejection on both VMs, its source and
 * diagnostic and nothing else. */
static bool scalar_rejects_unsupported()
{
    for (int i = 0; i < (int)(sizeof UNSUPPORTED / sizeof *UNSUPPORTED); i++) {
        const char *source = UNSUPPORTED[i].source;
        pin_value rows;
        REQUIRE(scalar_host_collect("Scalar.chibi", source, &detached, &rows));
        const pin_array *array = rows.ptr;
        REQUIRE(array->count == 2);
        pin_value *rejection = guard_row(&rows, 1);
        REQUIRE(scalar_field(rejection, "kind").i == SCALAR_REJECTION_ROW);
        int start = scalar_field(rejection, "start").i,
            end = scalar_field(rejection, "end").i;
        pin_string message = pin_text(scalar_field(rejection, "text"));
        bool expected = !strcmp(message.bytes, UNSUPPORTED[i].message) &&
                        end - start == (int)strlen(UNSUPPORTED[i].excerpt) &&
                        !strncmp(source + start, UNSUPPORTED[i].excerpt, (size_t)(end - start));
        if (!expected) {
            fprintf(
                stderr,
                "unsupported rejection %d: %s at %.*s\n",
                i,
                message.bytes,
                end - start,
                source + start
            );
        }
        REQUIRE(expected);
        arena_free(&detached);
    }
    return true;
}
/* CollectScalar's rows of `source` at libraries/Tools.chibi on VM b. */
static bool library_rows(const char *source, pin_value *rows)
{
    return scalar_host_collect("libraries/Tools.chibi", source, &detached, rows);
}
/* Library sources that are not one library of plain functions, each with
 * its message. A source holds at most one library, so the grammar rejects
 * anything after it. */
static const struct
{
    const char *source, *message;
} LIBRARY_REJECTIONS[] = {
    { "Tools :: library {\n    Point :: struct { x: int; }\n}\n",
      "a native library holds only functions without attributes" },
    { "Tools :: library {\n}\n", "scalar proof requires at least one function" },
    { "Tools :: library {\n    One :: () -> int { return 1; }\n"
      "    One :: () -> int { return 2; }\n}\n",
      "Duplicate declaration 'One'" },
};
/* A library's Document row names it, and each Function row's literal is the
 * stable symbol ID the runtime binds the function by. */
static bool scalar_collects_libraries()
{
    static const char
        source[] = "Tools :: library {\n"
                   "    Add :: (a: int, b: int) -> int { return a + b; }\n"
                   "    Name :: () -> string { return \"tools\"; }\n"
                   "}\n";
    const int64_t ids[] = {
        (int64_t)script_catalog_id("symbol", "libraries/Tools.chibi:1:Add:callable"),
        (int64_t)script_catalog_id("symbol", "libraries/Tools.chibi:1:Name:callable"),
    };
    {
        pin_value rows;
        REQUIRE(library_rows(source, &rows));
        const pin_array *array = rows.ptr;
        REQUIRE(scalar_field(guard_row(&rows, 1), "kind").i == SCALAR_DOCUMENT_ROW);
        REQUIRE(retained_text(guard_row(&rows, 1), "text", "Tools"));
        int functions = 0;
        for (int i = 2; i < array->count; i++) {
            pin_value *row = guard_row(&rows, i);
            if (scalar_field(row, "kind").i != SCALAR_FUNCTION_ROW) {
                continue;
            }
            REQUIRE(functions < 2 && scalar_field(row, "literal").i64 == ids[functions]);
            functions++;
        }
        REQUIRE(functions == 2);
        arena_free(&detached);
        for (size_t i = 0; i < sizeof LIBRARY_REJECTIONS / sizeof *LIBRARY_REJECTIONS;
             i++) {
            REQUIRE(library_rows(LIBRARY_REJECTIONS[i].source, &rows));
            array = rows.ptr;
            if (array->count != 2) {
                fprintf(
                    stderr,
                    "library rejection %zu gave %d rows for:\n%s\n",
                    i,
                    array->count,
                    LIBRARY_REJECTIONS[i].source
                );
            }
            REQUIRE(array->count == 2);
            pin_value *rejection = guard_row(&rows, 1);
            REQUIRE(scalar_field(rejection, "kind").i == SCALAR_REJECTION_ROW);
            pin_string message = pin_text(scalar_field(rejection, "text"));
            if (strcmp(message.bytes, LIBRARY_REJECTIONS[i].message)) {
                fprintf(stderr, "library rejection %zu: %s\n", i, message.bytes);
            }
            REQUIRE(!strcmp(message.bytes, LIBRARY_REJECTIONS[i].message));
            arena_free(&detached);
        }
    }
    return true;
}
/* A retain local of a library function is a state of the library: a Local
 * row whose literal is Retained, then the state's global, loaded and stored
 * around the variable, and the manifest a host reads the states through. */
static bool scalar_retains_library_states()
{
    static const char source[] = "Tools :: library {\n"
                                 "    Frame :: () -> int {\n"
                                 "        count: retain int = 4;\n"
                                 "        count += 1;\n"
                                 "        return count;\n"
                                 "    }\n"
                                 "}\n";
    {
        pin_value rows;
        REQUIRE(library_rows(source, &rows));
        const pin_array *array = rows.ptr;
        if (array->count > 1 &&
            scalar_field(guard_row(&rows, 1), "kind").i == SCALAR_REJECTION_ROW) {
            fprintf(
                stderr,
                "retained library rejected: %s\n",
                pin_text(scalar_field(guard_row(&rows, 1), "text")).bytes
            );
        }
        REQUIRE(array->count > 2 && scalar_field(guard_row(&rows, 1), "kind").i == SCALAR_DOCUMENT_ROW);
        char *ir;
        REQUIRE(target_ir("LinuxX64", &rows, &ir) && ir);
        bool emits = strstr(ir, "@chibi_scalar.retained.0 = internal global i32 4\n") &&
                     strstr(ir, "load i32, ptr @chibi_scalar.retained.0") &&
                     strstr(ir, "store i32 %a") &&
                     strstr(ir, "c\"Frame.count\\00\"") &&
                     strstr(ir, "@chibi_retained_Tools = constant { i32, i32, ptr, ptr, ptr, ptr } { i32 1, i32 1, ");
        if (!emits) {
            fprintf(stderr, "retained library IR:\n%s\n", ir);
        }
        free(ir);
        REQUIRE(emits);
        arena_free(&detached);
    }
    return true;
}
/* One fixture load serves both, since each load takes minutes. */
TEST_CASE(package_compiler_scalar_rejects_unions_and_reads_libraries)
{
    REQUIRE(prepare());
    REQUIRE(scalar_rejects_unsupported());
    REQUIRE(scalar_collects_libraries());
    return true;
}
TEST_CASE(package_compiler_scalar_retains_library_states)
{
    REQUIRE(prepare());
    REQUIRE(scalar_retains_library_states());
    return true;
}
TEST_CASE(package_compiler_scalar_results_require_the_fixture_value)
{
    static const int32_t expected[] = { 42, 0, INT32_MAX };
    for (int i = 0; i < 3; i++) {
        int literal;
        REQUIRE(scalar_host_case_expected(i, &literal));
        REQUIRE(literal == expected[i]);
        int64_t values[4] = { expected[i], expected[i], expected[i], expected[i] };
        REQUIRE(scalar_results_match(values, literal));
        int64_t wrong = expected[i] == 42 ? 41 : 1;
        const int64_t consensus[4] = { wrong, wrong, wrong, wrong };
        REQUIRE(!scalar_results_match(consensus, literal));
        for (int column = 0; column < 4; column++) {
            values[column] = wrong;
            REQUIRE(!scalar_results_match(values, literal));
            values[column] = expected[i];
        }
    }
    return true;
}
/* count records whose fields, in declaration order, take consecutive values,
 * as a JSON array of objects. */
static yyjson_mut_val *records_json(
    yyjson_mut_doc *doc,
    const char *const *fields,
    int width,
    const int64_t *values,
    int count
)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < count; i++) {
        yyjson_mut_val *object = yyjson_mut_obj(doc);
        for (int j = 0; j < width; j++) {
            yyjson_mut_obj_add_sint(doc, object, fields[j], values[i * width + j]);
        }
        yyjson_mut_arr_append(array, object);
    }
    return array;
}
static yyjson_mut_val *ints_json(yyjson_mut_doc *doc, const int *values, int count)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < count; i++) {
        yyjson_mut_arr_add_sint(doc, array, values[i]);
    }
    return array;
}
static yyjson_mut_val *texts_json(yyjson_mut_doc *doc, const char *const *items, int count)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < count; i++) {
        yyjson_mut_arr_add_str(doc, array, items[i]);
    }
    return array;
}
/* The LLVM text llvm_native.Emit gives m for native_abi's target `target`
 * under budget 1, its first function the entry; NULL when it rejects m.
 * scalar_host_consumer frees the arguments document. */
static bool native_module_ir(const char *target, const native_module *m, char **ir)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *arguments = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, arguments);
    yyjson_mut_arr_add_str(doc, arguments, target);
    /* llvm_native.Function has every field of the module's tables; the tests'
     * native_function leaves out the locals, which are zero. */
    static const char *const LLVM_FUNCTION_FIELDS[] = {
        "result", "first_parameter", "parameters", "first_slot",
        "slots",  "first_local",     "locals",     "first_code",
        "code",   "first_argument",  "arguments"
    };
    enum
    {
        LLVM_FUNCTION_WIDTH = (int)(sizeof LLVM_FUNCTION_FIELDS / sizeof *LLVM_FUNCTION_FIELDS)
    };
    int64_t *functions = xmalloc(
        sizeof *functions * LLVM_FUNCTION_WIDTH * (size_t)m->function_count
    );
    for (int i = 0; i < m->function_count; i++) {
        const native_function *f = &m->functions[i];
        const int64_t fields[LLVM_FUNCTION_WIDTH] = { f->result,
                                                      f->first_parameter,
                                                      f->parameters,
                                                      f->first_slot,
                                                      f->slots,
                                                      0,
                                                      0,
                                                      f->first_code,
                                                      f->code,
                                                      f->first_argument,
                                                      f->arguments };
        memcpy(&functions[i * LLVM_FUNCTION_WIDTH], fields, sizeof fields);
    }
    yyjson_mut_arr_append(
        arguments,
        records_json(doc, LLVM_FUNCTION_FIELDS, LLVM_FUNCTION_WIDTH, functions, m->function_count)
    );
    free(functions);
    yyjson_mut_arr_append(arguments, ints_json(doc, m->parameters, m->parameter_count));
    yyjson_mut_arr_append(arguments, ints_json(doc, m->slots, m->slot_count));
    yyjson_mut_arr_append(arguments, ints_json(doc, NULL, 0));
    int64_t *code = xmalloc(sizeof *code * CODE_WIDTH * (size_t)m->code_count);
    for (int i = 0; i < m->code_count; i++) {
        const native_code *c = &m->code[i];
        const int64_t fields[CODE_WIDTH] = { c->op,     c->type,  c->output,
                                             c->left,   c->right, c->target,
                                             c->literal };
        memcpy(&code[i * CODE_WIDTH], fields, sizeof fields);
    }
    yyjson_mut_arr_append(arguments, records_json(doc, CODE_FIELDS, CODE_WIDTH, code, m->code_count));
    free(code);
    yyjson_mut_arr_append(arguments, ints_json(doc, m->arguments, m->argument_count));
    int64_t *records = xmalloc(
        sizeof *records * RECORD_WIDTH * (size_t)(m->record_count + 1)
    );
    for (int i = 0; i < m->record_count; i++) {
        records[i * RECORD_WIDTH] = m->records[i].first;
        records[i * RECORD_WIDTH + 1] = m->records[i].count;
    }
    yyjson_mut_arr_append(
        arguments,
        records_json(doc, RECORD_FIELDS, RECORD_WIDTH, records, m->record_count)
    );
    free(records);
    yyjson_mut_arr_append(arguments, ints_json(doc, m->fields, m->field_count));
    yyjson_mut_arr_append(arguments, texts_json(doc, m->texts, m->text_count));
    yyjson_mut_val *source = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, source, "file", m->file ? m->file : "");
    yyjson_mut_obj_add_str(doc, source, "directory", m->directory ? m->directory : "");
    yyjson_mut_arr_append(arguments, source);
    int placed = m->file ? m->code_count : 0;
    yyjson_mut_arr_append(arguments, texts_json(doc, m->names, m->file ? m->function_count : 0));
    yyjson_mut_arr_append(arguments, ints_json(doc, m->lines, placed));
    yyjson_mut_arr_append(arguments, ints_json(doc, m->columns, placed));
    yyjson_mut_arr_add_false(doc, arguments);
    yyjson_mut_arr_add_sint(doc, arguments, 1);
    yyjson_doc *result;
    REQUIRE(scalar_host_consumer("EmitModule", doc, &result));
    yyjson_val *text = yyjson_arr_get(yyjson_doc_get_root(result), 0);
    *ir = NULL;
    if (yyjson_get_len(text)) {
        *ir = xmalloc(yyjson_get_len(text) + 1);
        memcpy(*ir, yyjson_get_str(text), yyjson_get_len(text) + 1);
    }
    yyjson_doc_free(result);
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
 * through a validation-only IR adapter that passes it int64 arguments and
 * returns an int64, which is how every integer type and bool reaches it; the
 * adapter is not a foreign ABI. The modules run on Linux x86-64 here, and
 * the other targets the emitter admits run them from a bundle (see
 * test/native_aot/targets). CHIBI_LLVM_BIN names the directory of opt, llc,
 * ld.lld and llvm-nm; nothing is found through PATH. */
/* The corpus's accepted cases. */
enum
{
    SCALAR_ACCEPTED_CASES = 87
};
/* Cases whose 64-deep operand nesting takes CollectScalar past the stage-zero
 * fixture's call depth limit (SCRIPT_CALL_DEPTH_LIMIT). Run.ps1 runs them on
 * the recovered compiler, which has no such limit. */

static const char *const DEEP_CASES[] = { "deep_operands" };
enum
{
    DEEP_CASE_COUNT = (int)(sizeof DEEP_CASES / sizeof *DEEP_CASES)
};

enum
{
    MAX_PROBE_ARGUMENTS = 3
};
typedef struct scalar_probe
{
    const char *source_case, *function;
    int count;
    int64_t arguments[MAX_PROBE_ARGUMENTS];
    int64_t expected;
} scalar_probe;

/* The six comparisons of the operators case, in the order of their source
 * functions, each probed with every pair. */

static const scalar_probe SCALAR_PROBES[] = {
    { "recursion", "Sum", 1, { 0 }, 0 },
    { "recursion", "Sum", 1, { 10 }, 55 },
    { "mutual_recursion", "A", 1, { 4 }, 1 },
    { "mutual_recursion", "B", 1, { 4 }, 2 },
    { "recursive_guard", "Down", 1, { -1 }, -1 },
    { "recursive_guard", "Down", 1, { 7 }, 0 },
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
    /* The low byte of an int, widened back. */
    { "conversion", "Low", 1, { -1 }, 255 },
    { "conversion", "Low", 1, { 256 }, 0 },
    { "conversion", "Low", 1, { INT32_MIN }, 0 },
    { "conversion", "Low", 1, { INT32_MAX }, 255 },
    { "shift", "Shl", 1, { -1 }, -4 },
    { "shift", "Shl", 1, { INT32_MIN }, 0 },
    { "shift", "Shl", 1, { INT32_MAX }, -4 },
    /* Each path of a && b's short circuit. */
    { "logical_and", "Mid", 3, { 2, 1, 3 }, 2 },
    { "logical_and", "Mid", 3, { 1, 3, 2 }, 1 },
    { "logical_and", "Mid", 3, { 1, 2, 3 }, 2 },
    { "bool_comparison", "Same", 2, { 1, 1 }, 1 },
    { "bool_comparison", "Same", 2, { 0, 1 }, 0 },
    { "helper_statement", "Id", 1, { INT32_MIN }, INT32_MIN },
};
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

/* The deadlines of an LLVM tool and of a library's calls, and the output
 * kept of each. */
static const double TOOL_SECONDS = 60, CALLS_SECONDS = 20, SPIN_SECONDS = 1;
enum
{
    NATIVE_OUTPUT = 1 << 16
};
/* The standard error of the last native run. */
static char native_errors[NATIVE_OUTPUT];
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
    chibi_process_output output = { out, NATIVE_OUTPUT, 0, false },
                         errors = { native_errors, sizeof native_errors, 0, false };
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
            native_errors
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
/* `ir` and then `adapter`, built at `level` with the driver's exact opt and
 * llc arguments into `object`. `module` receives the path of the optimized
 * module in `directory`, and `undefined` the object's undefined symbols,
 * one per line. */
static bool native_object(
    const char *directory,
    const char *name,
    const char *ir,
    const char *adapter,
    int level,
    const char *object,
    char module[PATH_MAX],
    char undefined[NATIVE_OUTPUT]
)
{
    char opt[PATH_MAX], llc[PATH_MAX], nm[PATH_MAX];
    REQUIRE(llvm_tool("opt", opt) && llvm_tool("llc", llc) && llvm_tool("llvm-nm", nm));
    char source[PATH_MAX];
    snprintf(source, sizeof source, "%s/%s-%d.ll", directory, name, level);
    snprintf(module, PATH_MAX, "%s/%s-%d.opt.ll", directory, name, level);
    native_file(source);
    native_file(module);
    REQUIRE(write_text(source, ir, adapter));
    const char *passes = level ? "-passes=verify,default<O2>,verify" : "-passes=verify";
    const char *const optimize[] = { opt,  "-S",   passes, source,
                                     "-o", module, NULL };
    const char *const emit[] = { llc,
                                 "-filetype=obj",
                                 level ? "-O=2" : "-O=0",
                                 "-relocation-model=pic",
                                 module,
                                 "-o",
                                 object,
                                 NULL };
    const char *const symbols[] = { nm, "--undefined-only", "--just-symbol-name", object, NULL };
    REQUIRE(run_tool(optimize, directory, undefined) && run_tool(emit, directory, undefined));
    REQUIRE(run_tool(symbols, directory, undefined));
    return true;
}
/* `ir` and then `adapter`, built at `level` with the driver's exact object
 * arguments, then linked as `directory`/`name`.so, which `library` names. */
static bool native_library(
    const char *directory,
    const char *name,
    const char *ir,
    const char *adapter,
    int level,
    char library[PATH_MAX]
)
{
    char lld[PATH_MAX], object[PATH_MAX], module[PATH_MAX], out[NATIVE_OUTPUT];
    REQUIRE(llvm_tool("ld.lld", lld));
    snprintf(object, sizeof object, "%s/%s-%d.o", directory, name, level);
    snprintf(library, PATH_MAX, "%s/%s-%d.so", directory, name, level);
    native_file(object);
    native_file(library);
    REQUIRE(native_object(directory, name, ir, adapter, level, object, module, out));
    if (*out) {
        fprintf(stderr, "%s has undefined symbols: %s\n", object, out);
    }
    REQUIRE(!*out);
    const char *const link[] = { lld, "-shared", object, "-o", library, NULL };
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
/* A scalar_facts type's bits, 1 for bool, whether it is unsigned, and
 * whether it is a float or double. */
static int scalar_bits(int type)
{
    static const int BITS[] = { [SCALAR_INT] = 32,   [SCALAR_BOOL] = 1,
                                [SCALAR_INT8] = 8,   [SCALAR_BYTE] = 8,
                                [SCALAR_INT16] = 16, [SCALAR_UINT16] = 16,
                                [SCALAR_UINT] = 32,  [SCALAR_INT64] = 64,
                                [SCALAR_WORD] = 64,  [SCALAR_FLOAT] = 32,
                                [SCALAR_DOUBLE] = 64 };
    assert(type > 0 && type < (int)(sizeof BITS / sizeof *BITS) && BITS[type]);
    return BITS[type];
}
static bool scalar_unsigned(int type)
{
    return type == SCALAR_BOOL || type == SCALAR_BYTE ||
           type == SCALAR_UINT16 || type == SCALAR_UINT || type == SCALAR_WORD;
}
static bool scalar_floating(int type)
{
    return type == SCALAR_FLOAT || type == SCALAR_DOUBLE;
}
static const char *llvm_type(int type)
{
    if (scalar_floating(type)) {
        return type == SCALAR_FLOAT ? "float" : "double";
    }
    static const char *const NAMES[] = { [1] = "i1",
                                         [8] = "i8",
                                         [16] = "i16",
                                         [32] = "i32",
                                         [64] = "i64" };
    return NAMES[scalar_bits(type)];
}
/* Appends to `adapter` the validation-only probe @probe_<name>(ptr): it reads
 * each parameter as an int64 of its argument array, truncated to the
 * parameter's type, and returns the function's result extended to int64 as
 * its type's value: sign-extended when signed, zero-extended otherwise. A
 * float or double travels as its encoding's bits, zero-extended, and a NaN
 * result as the canonical quiet NaN, since IEEE leaves a NaN's sign and
 * payload open. */
static bool append_adapter(const pin_value *rows, const char *name, char **adapter)
{
    int ordinal;
    int head = function_row(rows, name, &ordinal);
    REQUIRE(head > 0 && ordinal > 0);
    int count = row_int(rows, head, "count"), result = row_int(rows, head, "type");
    char text[4096], call[1024] = "";
    int n = snprintf(text, sizeof text, "\ndefine i64 @probe_%s(ptr %%args) {\n", name);
    for (int i = 0; i < count; i++) {
        int type = row_int(rows, head + 1 + i, "type");
        n += snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%p%d = getelementptr i64, ptr %%args, i64 %d\n  %%w%d = load "
            "i64, ptr %%p%d\n",
            i,
            i,
            i,
            i
        );
        if (type == SCALAR_FLOAT) {
            n += snprintf(
                text + n,
                sizeof text - (size_t)n,
                "  %%t%d = trunc i64 %%w%d to i32\n  %%a%d = bitcast i32 %%t%d "
                "to float\n",
                i,
                i,
                i,
                i
            );
        } else if (type == SCALAR_DOUBLE) {
            n += snprintf(text + n, sizeof text - (size_t)n, "  %%a%d = bitcast i64 %%w%d to double\n", i, i);
        } else if (scalar_bits(type) == 64) {
            n += snprintf(text + n, sizeof text - (size_t)n, "  %%a%d = add i64 %%w%d, 0\n", i, i);
        } else {
            n += snprintf(
                text + n,
                sizeof text - (size_t)n,
                "  %%a%d = trunc i64 %%w%d to %s\n",
                i,
                i,
                llvm_type(type)
            );
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
    if (scalar_floating(result)) {
        const char *type = llvm_type(result);
        int bits = scalar_bits(result);
        n += snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%n = fcmp uno %s %%r, 0.0\n  %%c = select i1 %%n, %s "
            "0x7FF8000000000000, %s %%r\n  %%b = bitcast %s %%c to i%d\n",
            type,
            type,
            type,
            type,
            bits
        );
        n += snprintf(
            text + n,
            sizeof text - (size_t)n,
            bits == 64 ? "  ret i64 %%b\n}\n" : "  %%z = zext i32 %%b to i64\n  ret i64 %%z\n}\n"
        );
    } else if (scalar_bits(result) == 64) {
        n += snprintf(text + n, sizeof text - (size_t)n, "  ret i64 %%r\n}\n");
    } else {
        n += snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%z = %s %s %%r to i64\n  ret i64 %%z\n}\n",
            scalar_unsigned(result) ? "zext" : "sext",
            llvm_type(result)
        );
    }
    REQUIRE(n < (int)sizeof text);
    size_t old = strlen(*adapter);
    *adapter = xrealloc(*adapter, old + (size_t)n + 1);
    memcpy(*adapter + old, text, (size_t)n + 1);
    return true;
}
/* The canonical quiet NaN of a float or double, which adapters return for
 * every NaN. */

/* The bits a float or double `value` of `type` travels as: a float's
 * zero-extended, and a NaN canonical. */

/* A probe's arguments as the JSON array the compiler host takes. A float or
 * double is a JSON real, NaN or an infinity, never an integer, so -0 keeps
 * its sign. */

/* Loads `bytes`, a closure, as the test's host on `backend`. */

/* The value of `type` the loaded closure's `function` of package scalar
 * returns for `json`, as the int64 its probe adapter gives: a bool as 0 or 1,
 * a uint64 as its bits, and a float or double as floating_bits. */

/* The authored compiler's closure of `source` as package scalar. */

/* The text of `ir` from `start` to the end of the definition it begins. */

/* The text of `ir` from label `p<pc>` on, or NULL. */

/* Unoptimized IR keeps what the source decides at run time. Min moves its
 * parameters to locals, compares them and branches on the result. Choose's
 * branch goes to its consequence, which calls Taken, and otherwise jumps past
 * it to the call of Skipped, so each runs only on its own path. Mid compares
 * b with c only after branching on a < b. */

/* A call a case checks, its entry or a probe, and what it gave on the
 * bytecode and compiled VMs and natively at O0 and O2. An entry of a module
 * that calls the runtime takes a context. */
typedef struct scalar_call
{
    scalar_probe probe;
    char json[128], symbol[64];
    int64_t values[4];
    bool context;
} scalar_call;
enum
{
    MAX_CASE_CALLS = MAX_CASE_PROBES + 1,
    /* A width or floating module's entry and probes, the most of any
     * module. */
    MAX_WIDTH_CALLS = 1024
};
/* Case `name`'s calls: its entry, then each of its probes. */

/* Each call's value from the authored compiler's closure on both VMs. */

enum
{
    CALL_SPEC = 160
};
/* Runs each call of `library` in order, in a child run of this binary (see
 * test_language_package_compiler_chibi_native_call) that must finish within
 * `seconds`, and stores each value in its call's `column`. A call of a
 * probe_ symbol is a probe adapter, with or without arguments; any other,
 * such as the entry, takes none, or only a context, and returns an int32. */
static chibi_process_capture_result library_calls(
    const char *directory,
    const char *library,
    scalar_call *calls,
    int count,
    int column,
    double seconds
)
{
    char self[PATH_MAX], out[NATIVE_OUTPUT];
    ssize_t length = readlink("/proc/self/exe", self, sizeof self - 1);
    assert(length > 0 && count <= MAX_WIDTH_CALLS);
    self[length] = 0;
    char (*specs)[CALL_SPEC] = xmalloc(sizeof *specs * (size_t)count);
    const char **argv = xmalloc(sizeof *argv * (size_t)(count + 4));
    argv[0] = self;
    argv[1] = "--package-compiler-chibi-native-call";
    argv[2] = library;
    for (int i = 0; i < count; i++) {
        bool adapter = !strncmp(calls[i].symbol, "probe_", strlen("probe_"));
        assert(!adapter || !calls[i].context);
        int n = snprintf(
            specs[i],
            CALL_SPEC,
            "%s%s%s",
            calls[i].context ? "+" : "",
            calls[i].symbol,
            adapter ? ":" : ""
        );
        for (int a = 0; adapter && a < calls[i].probe.count; a++) {
            n += snprintf(
                specs[i] + n,
                CALL_SPEC - (size_t)n,
                "%s%" PRId64,
                a ? "," : "",
                calls[i].probe.arguments[a]
            );
        }
        assert(n < CALL_SPEC);
        argv[3 + i] = specs[i];
    }
    argv[3 + count] = NULL;
    chibi_process_capture_result result = run_native(argv, directory, seconds, out);
    free(argv);
    free(specs);
    const char *at = out;
    for (int i = 0; native_passed(result) && i < count; i++) {
        char *end;
        errno = 0;
        calls[i].values[column] = strtoll(at, &end, 10);
        assert(end != at && *end == '\n' && !errno);
        at = end + 1;
    }
    return result;
}
/* The child of library_calls: opens `library`, then calls each spec in order,
 * `symbol` for the entry, `+symbol` for one that takes a context, or
 * `symbol:a,b` for an adapter, `symbol:` for one of no arguments, and prints
 * each value on its own line as it returns. A context's heap is freed after
 * its call, as the VM frees it. */
/* A library's retained-state manifest, chibi_retained_<Name>, as
 * llvm_native's RetainedManifest writes it. */
typedef struct
{
    uint32_t abi;
    int32_t count;
    const uint8_t *types;
    const char *names;
    execution_native_entry get, set;
} retained_manifest;
/* The `index`th of the NUL-ended names. */
static const char *retained_name(const retained_manifest *manifest, int index)
{
    const char *at = manifest->names;
    for (; index > 0; index--) {
        at += strlen(at) + 1;
    }
    return at;
}
/* Calls `library`'s Frame, a function of no arguments that returns an int. */
static bool frame_call(void *library, const char *name, int64_t *value)
{
    char symbol[64];
    snprintf(symbol, sizeof symbol, "chibi_natives_%s", name);
    const execution_native_manifest *natives = dlsym(library, symbol);
    for (int i = 0; natives && i < natives->count; i++) {
        if (!strcmp(natives->exports[i].name, "Frame")) {
            uint64_t word = 0;
            natives->exports[i].entry(NULL, NULL, &word);
            *value = (int32_t)word;
            return true;
        }
    }
    return false;
}
static const retained_manifest *retained_of(void *library, const char *name)
{
    char symbol[64];
    snprintf(symbol, sizeof symbol, "chibi_retained_%s", name);
    return dlsym(library, symbol);
}
/* The edit-and-continue child: the loop `first` runs three Frames, then the
 * loop `second`, an edit of it, takes its place. The host reads the old
 * states, migrates them by name and type, sets them in the new library and
 * runs two more Frames, without calling Main. Each Frame's value is printed. */
static int enc_child(const char *first, const char *second)
{
    stack_overflow_init();
    void *old = dlopen(first, RTLD_NOW | RTLD_LOCAL),
         *next = dlopen(second, RTLD_NOW | RTLD_LOCAL);
    if (!old || !next) {
        fprintf(stderr, "native generation load: %s\n", dlerror());
        return 1;
    }
    int64_t value;
    for (int i = 0; i < 3; i++) {
        if (!frame_call(old, "Loop", &value)) {
            return 1;
        }
        printf("%" PRId64 "\n", value);
    }
    const retained_manifest *before = retained_of(old, "Loop"),
                            *after = retained_of(next, "Loop");
    if (!before || !after || before->count > 16 || after->count > 16) {
        return 1;
    }
    uint64_t was[16], now[16];
    before->get(NULL, NULL, was);
    after->get(NULL, NULL, now);
    for (int i = 0; i < after->count; i++) {
        for (int j = 0; j < before->count; j++) {
            if (!strcmp(retained_name(after, i), retained_name(before, j)) &&
                after->types[i] == before->types[j]) {
                now[i] = was[j];
            }
        }
    }
    after->set(NULL, now, NULL);
    for (int i = 0; i < 2; i++) {
        if (!frame_call(next, "Loop", &value)) {
            return 1;
        }
        printf("%" PRId64 "\n", value);
    }
    fflush(stdout);
    return 0;
}
typedef int32_t (*scalar_entry)();
typedef int32_t (*scalar_runtime_entry)(execution_native_context *);
typedef int64_t (*scalar_adapter)(const int64_t *);

static int library_child(const char *library, int count, char **specs)
{
    if (count == 2 && !strcmp(specs[0], "continue")) {
        return enc_child(library, specs[1]);
    }
    stack_overflow_init();
    void *opened = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!opened) {
        fprintf(stderr, "native scalar load: %s\n", dlerror());
        return 1;
    }
    for (int i = 0; i < count; i++) {
        char symbol[CALL_SPEC];
        int64_t arguments[MAX_PROBE_ARGUMENTS];
        bool context = specs[i][0] == '+';
        const char *spec = specs[i] + context;
        const char *at = spec + strcspn(spec, ":");
        size_t length = (size_t)(at - spec);
        assert(length < sizeof symbol);
        memcpy(symbol, spec, length);
        symbol[length] = 0;
        bool adapter = *at;
        int n = 0;
        for (char *end; adapter && *at && at[1]; at = end, n++) {
            assert(n < MAX_PROBE_ARGUMENTS);
            arguments[n] = strtoll(at + 1, &end, 10);
        }
        void *function = dlsym(opened, symbol);
        if (!function) {
            fprintf(stderr, "native scalar symbol %s: %s\n", symbol, dlerror());
            return 1;
        }
        int64_t value;
        if (context) {
            execution_native_frame frame;
            execution_native_frame_init(
                &frame,
                (execution_native_runtime){ NATIVE_FORMAT_TEXT_HOST.value }
            );
            value = ((scalar_runtime_entry)function)(&frame.context);
            execution_native_frame_free(&frame);
        } else {
            value = adapter ? ((scalar_adapter)function)(arguments)
                            : ((scalar_entry)function)();
        }
        printf("%" PRId64 "\n", value);
        fflush(stdout);
    }
    return 0;
}
/* The other targets native_abi admits. When CHIBI_NATIVE_BUNDLE names a
 * directory, each module the native tests run is also emitted for each of
 * them and built with the driver's opt and llc arguments into the bundle,
 * whose runner (test/native_aot/targets) links and runs it on that target.
 * Each has native_abi's function, the bundle's subdirectory, the triple and
 * the data layout LLVM 18 gives it, which the driver admits, and the
 * symbols its objects may leave undefined, which the bundle's runtime.o
 * defines from `runtime`, or none. Windows code that uses floating point
 * references _fltused, and a frame larger than a page calls __chkstk; the C
 * runtime would define both. This __chkstk is compiler-rt's x86-64 probe, as
 * tools/native_aot/Driver.chibi's start object defines it: it touches each
 * page from the stack pointer down by RAX bytes and changes no register. */
enum
{
    BUNDLE_RUNTIME_SYMBOLS = 2
};
static const struct
{
    const char *function, *directory, *triple, *layout, *runtime;
    const char *defines[BUNDLE_RUNTIME_SYMBOLS];
} BUNDLE_TARGETS[] = {
    { "MacOSArm64",
      "macos-arm64",
      "arm64-apple-macosx11.0.0",
      "e-m:o-i64:64-i128:128-n32:64-S128",
      NULL,
      { NULL } },
    { "WindowsX64",
      "windows-x64",
      "x86_64-pc-windows-msvc",
      "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:"
      "64-S128",
      "@_fltused = dso_local global i32 0\n"
      "module asm \".text\"\n"
      "module asm \".globl __chkstk\"\n"
      "module asm \"__chkstk:\"\n"
      "module asm \"pushq %rcx\"\n"
      "module asm \"pushq %rax\"\n"
      "module asm \"cmpq $0x1000, %rax\"\n"
      "module asm \"leaq 24(%rsp), %rcx\"\n"
      "module asm \"jb 1f\"\n"
      "module asm \"2:\"\n"
      "module asm \"subq $0x1000, %rcx\"\n"
      "module asm \"testq %rcx, (%rcx)\"\n"
      "module asm \"subq $0x1000, %rax\"\n"
      "module asm \"cmpq $0x1000, %rax\"\n"
      "module asm \"ja 2b\"\n"
      "module asm \"1:\"\n"
      "module asm \"subq %rax, %rcx\"\n"
      "module asm \"testq %rcx, (%rcx)\"\n"
      "module asm \"popq %rax\"\n"
      "module asm \"popq %rcx\"\n"
      "module asm \"retq\"\n",
      { "__chkstk", "_fltused" } },
};
enum
{
    BUNDLE_TARGET_COUNT = (int)(sizeof BUNDLE_TARGETS / sizeof *BUNDLE_TARGETS)
};
/* Whether each line of `undefined` names a symbol target `t`'s runtime
 * defines. */
static bool bundle_runtime_defines(int t, const char *undefined)
{
    for (const char *at = undefined; *at;) {
        size_t length = strcspn(at, "\n");
        bool defined = false;
        for (int i = 0; i < BUNDLE_RUNTIME_SYMBOLS && BUNDLE_TARGETS[t].defines[i];
             i++) {
            const char *symbol = BUNDLE_TARGETS[t].defines[i];
            defined = defined ||
                      (strlen(symbol) == length && !strncmp(at, symbol, length));
        }
        if (!defined) {
            return false;
        }
        at += length + (at[length] == '\n');
    }
    return true;
}
/* The modules bundled so far, which number each one's objects: generated
 * modules share some corpus cases' names. */
static int bundled_modules;
static const char *bundle_root()
{
    const char *root = getenv("CHIBI_NATIVE_BUNDLE");
    return root && *root ? root : NULL;
}
/* Whether the module at `path` declares exactly target t's triple and
 * layout. */
static bool bundle_module_targets(const char *path, int t)
{
    char *text = file_read_text(path);
    REQUIRE(text);
    char triple[128], layout[256];
    snprintf(
        triple,
        sizeof triple,
        "\ntarget triple = \"%s\"\n",
        BUNDLE_TARGETS[t].triple
    );
    snprintf(
        layout,
        sizeof layout,
        "\ntarget datalayout = \"%s\"\n",
        BUNDLE_TARGETS[t].layout
    );
    bool declared = strstr(text, triple) && strstr(text, layout) &&
                    !strstr(strstr(text, triple) + 1, "\ntarget triple");
    free(text);
    if (!declared) {
        fprintf(
            stderr,
            "%s does not declare %s's triple and layout\n",
            path,
            BUNDLE_TARGETS[t].directory
        );
    }
    return declared;
}
/* Builds `ir`, emitted for bundle target t, and `adapter` at O0 and O2 into
 * the bundle, and lists each of `calls` after its object with the value
 * both VMs and the native Linux runs gave. */
static bool bundle_module(
    const char *directory,
    int t,
    const char *name,
    const char *ir,
    const char *adapter,
    const scalar_call *calls,
    int count
)
{
    char path[PATH_MAX], label[128];
    snprintf(
        path,
        sizeof path,
        "%s/%s/modules.txt",
        bundle_root(),
        BUNDLE_TARGETS[t].directory
    );
    snprintf(label, sizeof label, "%s-%s", BUNDLE_TARGETS[t].directory, name);
    FILE *list = fopen(path, "a");
    REQUIRE(list);
    bool built = true;
    for (int level = 0; level < 3; level += 2) {
        char file[128], object[PATH_MAX], module[PATH_MAX], undefined[NATIVE_OUTPUT];
        snprintf(file, sizeof file, "%d-%s-%d.o", bundled_modules, name, level);
        snprintf(
            object,
            sizeof object,
            "%s/%s/%s",
            bundle_root(),
            BUNDLE_TARGETS[t].directory,
            file
        );
        built = native_object(directory, label, ir, adapter, level, object, module, undefined) &&
                bundle_module_targets(module, t);
        if (built && !bundle_runtime_defines(t, undefined)) {
            fprintf(stderr, "%s has undefined symbols: %s\n", object, undefined);
            built = false;
        }
        if (!built) {
            break;
        }
        fprintf(list, "module %s\n", file);
        for (int i = 0; i < count; i++) {
            const scalar_probe *probe = &calls[i].probe;
            fprintf(
                list,
                "call %s %" PRId64 " %d",
                calls[i].symbol,
                probe->expected,
                probe->count
            );
            for (int a = 0; a < probe->count; a++) {
                fprintf(list, " %" PRId64, probe->arguments[a]);
            }
            fputc('\n', list);
        }
    }
    bundled_modules++;
    REQUIRE(!fclose(list) && built);
    return true;
}
/* An adapter for each function `calls` probe after the entry. */
static bool call_adapters(const pin_value *rows, const scalar_call *calls, int count, char **adapter)
{
    *adapter = xmalloc(1);
    **adapter = 0;
    for (int i = 1; i < count; i++) {
        if (i > 1 && !strcmp(calls[i].probe.function, calls[i - 1].probe.function)) {
            continue;
        }
        REQUIRE(append_adapter(rows, calls[i].probe.function, adapter));
    }
    return true;
}
/* Whether module `ir` calls the runtime: its entry takes a context. */
static bool calls_runtime(const char *ir)
{
    return strstr(ir, "@chibi_scalar(ptr %ctx)");
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
    /* No adapter passes a context, so a module that calls the runtime is
     * checked through its entry alone. */
    calls[0].context = calls_runtime(ir);
    REQUIRE(!calls[0].context || count == 1);
    char *adapter;
    REQUIRE(call_adapters(rows, calls, count, &adapter));
    for (int level = 0; level < 2; level++) {
        char library[PATH_MAX];
        REQUIRE(native_library(directory, name, ir, adapter, level * 2, library));
        REQUIRE(native_passed(
            library_calls(directory, library, calls, count, 2 + level, CALLS_SECONDS)
        ));
    }
    free(adapter);
    return true;
}
/* Rename only one generated callee definition. Its call sites keep the old
 * symbol, which an observed wrapper supplies. No branch body is rewritten. */

/* Reset, Choose's return, Taken's entry count and Skipped's entry count. */

/* An additional validation-only module observes generated callee entries.
 * Normal optimization/result checks above still use the unmodified IR.
 * The extra-call variant keeps Choose's value but must fail the same trace
 * oracle on the true path, even after O2 inlines the pure original bodies. */

/* Every executed result matches its literal fixture or probe oracle. */

/* The IR EmitBytesFor gives for `rows` and native_abi's target `function`,
 * NUL-terminated, or NULL for none. */

/* The Linux x86-64 IR EmitBytesFor gives for `rows`, or NULL for none. */
static bool emitted_ir(pin_value *rows, char **ir)
{
    return target_ir("LinuxX64", rows, ir);
}
/* `source`'s producer rows at `path` from CollectScalar on VM b. */
static bool collected_at(const char *path, const char *case_name, const char *source, pin_value *rows)
{
    bool collected = scalar_host_collect(path, source, &detached, rows);
    if (!collected) {
        fprintf(stderr, "scalar case %s: CollectScalar failed\n", case_name);
    }
    REQUIRE(collected);
    return true;
}
/* With a bundle, the module of `rows` for each other target, with `calls`,
 * whose values every run agreed on. */

static bool collected_rows(const char *case_name, const char *source, pin_value *rows)
{
    return collected_at("Scalar.chibi", case_name, source, rows);
}
/* Each of `calls` of an accepted module, `ir` its IR, gives its expected
 * value on both VMs, from the authored compiler's closure of `source` on VM
 * b, and natively at O0 and O2. */

/* An accepted module, `ir` its IR, which this frees: each of its calls must
 * give one value on both VMs and natively at O0 and O2. */

/* Case `index` of the corpus: its producer rows on VM b must be what the case
 * expects, and an accepted module must run natively as it does on both VMs. */

#define IDS_10 "Id(Id(Id(Id(Id(Id(Id(Id(Id(Id("
#define CLOSES_10 "))))))))))"
#define NESTED_IDS IDS_10 IDS_10 IDS_10 IDS_10 IDS_10
#define NESTED_CLOSES CLOSES_10 CLOSES_10 CLOSES_10 CLOSES_10 CLOSES_10
/* A scalar module with a constant expression the ordinary compiler folds
 * exactly, a conversion it must call instead, calls that omit defaulted
 * arguments or records, and the value its Main gives. They stay out of the 96-source
 * corpus, which Run.ps1 bounds. Fixed-width rows would wrap each constant:
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
      "package scalar\nMain :: () -> int { return Pick(Id((2147483647 + 1) "
      "/ "
      "2)); }\n"
      "Id :: (x: int) -> int { return x; }\n"
      "Pick :: (x: int) -> int {\n"
      "    if (x > (2147483647 + 1) / 4) { return (2147483647 * "
      "2147483647) / "
      "2147483647; }\n"
      "    return 0;\n"
      "}\n",
      INT32_MAX },
    /* A conversion an authored function shadows is a call to it, with that
     * function's result type, also inside an expression and as a built-in
     * conversion's argument. */
    { "shadowed_conversion",
      "package scalar\nMain :: () -> int { return i8(1); }\n"
      "i8 :: (v: int) -> int { return v + 1; }\n",
      2 },
    { "shadowed_nested_conversion",
      "package scalar\nMain :: () -> int { return int(i8(127)) + i8(100) * "
      "2; }\n"
      "i8 :: (v: int) -> int { return v + 1; }\n",
      330 },
    /* A default of every kind of type, and a call that gives some
     * arguments and omits the rest: 6 + 20 + 10 + (-1 + 255 + 15 + 2 - 9). */
    { "defaults",
      "package scalar\nMain :: () -> int { return Mix() + Mix(10) + "
      "Mix(10, "
      "false) + Small(); }\n"
      "Mix :: (a: int = 3, twice: bool = true) -> int { if (twice) { "
      "return a "
      "* 2; } "
      "return a; }\n"
      "Small :: (b: i8 = -1, u: u8 = 255, w: u64 = "
      "18446744073709551615, f: number = 0.5,\n"
      "          d: f64 = -2.25) -> int {\n"
      "    return int(b) + int(u) + int(w >> 60) + int(f * 4.0) + int(d * "
      "4.0);\n"
      "}\n",
      298 },
    /* Records: literals in any field order, copies that leave their source
     * alone, field writes and compound writes, record parameters and
     * results, nested calls, and a u8 field that wraps:
     * 50 + (206 + 8 - 9 + 1 + 1000) + 5. */
    { "records",
      "package scalar\n"
      "Vec :: struct { x: number; y: number; }\n"
      "Cell :: struct { alive: bool; count: u8; weight: f64; id: i64; "
      "}\n"
      "Main :: () -> int { return Vectors() + Cells() + Walk(); }\n"
      "Add :: (p: Vec, q: Vec) -> Vec { return {x: p.x + q.x, y: p.y + "
      "q.y}; "
      "}\n"
      "Vectors :: () -> int {\n"
      "    a: Vec = {x: 1.5, y: -2.0};\n"
      "    b := a;\n"
      "    b.x = 4.0;\n"
      "    b.y *= 3.0;\n"
      "    s := Add(a, b);\n"
      "    return int(s.x * 10.0) + int(s.y) + int(a.x * 2.0);\n"
      "}\n"
      "Grow :: (c: Cell) -> Cell {\n"
      "    next := c;\n"
      "    next.count += 3;\n"
      "    next.weight = next.weight * 2.0;\n"
      "    next.id -= 1;\n"
      "    next.alive = next.count > 2;\n"
      "    return next;\n"
      "}\n"
      "Cells :: () -> int {\n"
      "    c := Grow(Grow({alive: true, count: 200, weight: 0.25, id: "
      "-7}));\n"
      "    total: int = 0;\n"
      "    if (c.alive) { total += int(c.count) + int(c.weight * 8.0) + "
      "int(c.id); }\n"
      "    w := Grow({id: 0, weight: 1.0, count: 254, alive: true});\n"
      "    total += int(w.count);\n"
      "    if (!w.alive) { total += 1000; }\n"
      "    return total;\n"
      "}\n"
      "Walk :: () -> int {\n"
      "    a: Vec = {y: -2.0, x: 1.5};\n"
      "    for (i in 0..<3) { a = Add(a, {y: 1.0, x: number(i)}); }\n"
      "    return int(a.x) + int(a.y);\n"
      "}\n",
      1261 },
    /* Named results: two to four of mixed types, assigned, returned in
     * order, each value assigned before the next is evaluated, by a bare
     * return or at the end, read by name from a call or a local; and one
     * named result, a number or a record, which is its call's value and
     * also projected by name, returned and read through:
     * 132 + 146 + 10012 + 22 + 108 + 30 + 37649. */
    { "named_results",
      "package scalar\n"
      "Vec :: struct { x: int; y: int; }\n"
      "Main :: () -> int { return Pairs() + Mixed() + Quads() + Orders() + "
      "Clamps() + Single() + Records(); }\n"
      "DivMod :: (a: int, b: int) -> (q: int, r: int) { return (a / b, a % b); "
      "}\n"
      "Pairs :: () -> int { d := DivMod(17, 5); return d.q * 10 + d.r + "
      "DivMod(9, 4).r * 100; }\n"
      "Split :: (v: f64) -> (whole: int, half: bool, rest: f64) {\n"
      "    whole = int(v);\n"
      "    rest = v - f64(whole);\n"
      "    half = rest >= 0.5;\n"
      "    return;\n"
      "}\n"
      "Mixed :: () -> int {\n"
      "    s := Split(7.75);\n"
      "    total := s.whole * 10;\n"
      "    if (s.half) { total += 1; }\n"
      "    return total + int(s.rest * 100.0);\n"
      "}\n"
      "Quad :: (seed: u8) -> (a: u8, b: i64, c: bool, d: number) {\n"
      "    a = seed;\n"
      "    a += 250;\n"
      "    b = i64(seed) * 1000;\n"
      "    c = a < seed;\n"
      "    d = 0.25;\n"
      "}\n"
      "Quads :: () -> int {\n"
      "    q := Quad(10);\n"
      "    n := int(q.a) + int(q.b);\n"
      "    if (q.c) { n += 7; }\n"
      "    return n + int(q.d * 4.0);\n"
      "}\n"
      "Order :: () -> (p: int, q: int) { p = 1; q = 2; return (q, p); }\n"
      "Orders :: () -> int { o := Order(); return o.p * 10 + o.q; }\n"
      "Clamp :: (v: int) -> (low: bool, value: int) {\n"
      "    value = v;\n"
      "    if (v < 0) { low = true; value = 0; return; }\n"
      "    return (false, v * 2);\n"
      "}\n"
      "Clamps :: () -> int {\n"
      "    c := Clamp(-5);\n"
      "    n := Clamp(4).value;\n"
      "    if (c.low) { n += 100; }\n"
      "    return n + c.value;\n"
      "}\n"
      "Twice :: (v: int) -> (doubled: int) { doubled = v * 2; }\n"
      "Single :: () -> int {\n"
      "    t := Twice(4);\n"
      "    w: i64 = Twice(3);\n"
      "    return t + t.doubled + Twice(1).doubled + int(w) + w.doubled;\n"
      "}\n"
      "Make :: (v: int) -> (p: Vec) { p.x = v; p.y = v + 1; return; }\n"
      "Forward :: () -> Vec { return Make(7).p; }\n"
      "Records :: () -> int {\n"
      "    m := Make(3);\n"
      "    q := Make(2).p;\n"
      "    return m.x + m.y * 10 + Make(5).y + Make(5).p.y * 100 + Forward().x "
      "* 1000 + q.y * 10000;\n"
      "}\n",
      48099 },
    /* A record through 50 nested calls. Each call takes one compiler frame,
     * as an int call does, so this stays within the 64 frames stage zero
     * gives the compiler; two frames per call would exceed them. */
    { "nested_records",
      "package scalar\nVec :: struct { x: int; }\n"
      "Main :: () -> int { v: Vec = {x: 7}; return " NESTED_IDS "v" NESTED_CLOSES ".x; }\n"
      "Id :: (p: Vec) -> Vec { return p; }\n",
      7 },
    /* Unions: variants with and without payloads, made, passed, returned,
     * assigned and tested with `is`, switches whose arms bind payloads, with
     * a default arm or covering every variant without one, and a switch in
     * a loop that breaks and continues: 123 + 111 + (250 + 251 + 252 + 253). */
    { "unions",
      "package scalar\n"
      "Shape :: union { Empty | Circle(r: number) | Rect(w: int, h: int) | "
      "Flag(lit: bool, weight: f64) }\n"
      "Step :: union { Stop | Go(n: u8) }\n"
      "Main :: () -> int { return Areas() + Tests() + Steps(); }\n"
      "Area :: (s: Shape) -> int {\n"
      "    switch s {\n"
      "        case .Empty: return 0;\n"
      "        case .Circle(r): return int(r * r * 3.0);\n"
      "        case .Rect(w, h): return w * h;\n"
      "        case .Flag(lit, weight):\n"
      "            if (lit) { return int(weight * 2.0); }\n"
      "            return -1;\n"
      "    }\n"
      "}\n"
      "Grow :: (s: Shape) -> Shape {\n"
      "    switch s {\n"
      "        case .Rect(w, h): return Shape.Rect(w + 1, h * 2);\n"
      "        case .Circle(r): return Shape.Circle(r + 0.5);\n"
      "        case: return s;\n"
      "    }\n"
      "}\n"
      "Areas :: () -> int {\n"
      "    a := Shape.Rect(3, 4);\n"
      "    b: Shape = Shape.Circle(2.0);\n"
      "    c := Grow(Grow(a));\n"
      "    total := Area(a) + Area(b) + Area(c) + Area(Shape.Empty) + "
      "Area(Shape.Flag(true, 10.25));\n"
      "    a = Grow(Shape.Flag(false, 1.0));\n"
      "    return total + Area(a);\n"
      "}\n"
      "Tests :: () -> int {\n"
      "    s := Grow(Shape.Circle(1.0));\n"
      "    n: int = 0;\n"
      "    if (s is .Circle) { n += 1; }\n"
      "    if (s is not .Rect) { n += 10; }\n"
      "    if (Shape.Empty is .Empty && !(s is .Empty)) { n += 100; }\n"
      "    return n;\n"
      "}\n"
      "Steps :: () -> int {\n"
      "    s := Step.Go(250);\n"
      "    total: int = 0;\n"
      "    for (i in 0..<10) {\n"
      "        switch s {\n"
      "            case .Stop: break;\n"
      "            case .Go(n):\n"
      "                total += int(n);\n"
      "                if (n > 252) { s = Step.Stop; continue; }\n"
      "                s = Step.Go(n + 1);\n"
      "        }\n"
      "    }\n"
      "    return total;\n"
      "}\n",
      1240 },
    /* Past the bounds the facts once had: a union of 18 variants, one of
     * 20 payloads all bound in one arm, a record of 20 fields and 17 more
     * records: 210 + 210 + 1000 + 5. */
    { "widest_union",
      "package scalar\n"
      "Wide :: union { Empty | All(a: int, b: int, c: int, d: int, e: int, f: "
      "int, g: int, h: int, i: int, j: int, k: int, l: int, m: int, n: int, o: "
      "int, p: int, q: int, r: int, s: int, t: int) |\n"
      "    V2 | V3 | V4 | V5 | V6 | V7 | V8 | V9 | V10 | V11 | V12 | V13 | V14 "
      "| V15 | V16 | V17 }\n"
      "Big :: struct { a: int; b: int; c: int; d: int; e: int; f: int; g: int; "
      "h: int; i: int; j: int; k: int; l: int; m: int; n: int; o: int; p: int; "
      "q: int; r: int; s: int; t: int; }\n"
      "R1 :: struct { x: int; }\nR2 :: struct { x: int; }\nR3 :: struct { x: "
      "int; }\nR4 :: struct { x: int; }\nR5 :: struct { x: int; }\nR6 :: "
      "struct { x: int; }\nR7 :: struct { x: int; }\nR8 :: struct { x: int; "
      "}\nR9 :: struct { x: int; }\nR10 :: struct { x: int; }\nR11 :: struct { "
      "x: int; }\nR12 :: struct { x: int; }\nR13 :: struct { x: int; }\nR14 :: "
      "struct { x: int; }\nR15 :: struct { x: int; }\nR16 :: struct { x: int; "
      "}\nR17 :: struct { x: int; }\n"
      "Main :: () -> int { r: R17 = {x: 5}; return Sum(Wide.All(1, 2, 3, 4, 5, "
      "6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20)) + Fields() + "
      "Sum(Wide.V17) + r.x; }\n"
      "Sum :: (w: Wide) -> int {\n"
      "    switch w {\n"
      "        case .Empty: return 0;\n"
      "        case .All(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, "
      "s, t):\n"
      "            return (a + b + c + d + e) + (f + g + h + i + j) + (k + l + "
      "m + n + o) + (p + q + r + s + t);\n"
      "        case .V17: return 1000;\n"
      "        case: return -1;\n"
      "    }\n"
      "}\n"
      "Fields :: () -> int {\n"
      "    b: Big = {a: 1, b: 2, c: 3, d: 4, e: 5, f: 6, g: 7, h: 8, i: 9, j: "
      "10, k: 11, l: 12, m: 13, n: 14, o: 15, p: 16, q: 17, r: 18, s: 19, t: "
      "20};\n"
      "    return (b.a + b.b + b.c + b.d + b.e) + (b.f + b.g + b.h + b.i + "
      "b.j) + (b.k + b.l + b.m + b.n + b.o) + (b.p + b.q + b.r + b.s + b.t);\n"
      "}\n",
      1425 },
    /* Strings: literals, parameters, results, locals, assignments,
     * defaults and arguments, compared by their bytes with == and !=,
     * including the empty string, a prefix, a last byte that differs,
     * escapes, UTF-8 and an embedded NUL, and in a loop. Each check is one
     * bit: Names() 61, Escapes() 127 and Loops() 3. */
    { "strings",
      "package scalar\n"
      "Main :: () -> int { return Names() + Escapes() * 1000 + Loops() * "
      "1000000; }\n"
      "Name :: (n: int) -> string {\n"
      "    if (n == 0) { return \"zero\"; }\n"
      "    if (n == 1) { return \"one\"; }\n"
      "    return \"many\";\n"
      "}\n"
      "Same :: (a: string, b: string) -> bool { return a == b; }\n"
      "Greet :: (name: string = \"world\") -> string { return name; }\n"
      "Bit :: (b: bool, weight: int) -> int {\n"
      "    if (b) { return weight; }\n"
      "    return 0;\n"
      "}\n"
      "Names :: () -> int {\n"
      "    s := Name(1);\n"
      "    t: string = \"\";\n"
      "    empty := t == \"\";\n"
      "    t = s;\n"
      "    return Bit(s == \"one\", 1) + Bit(s != \"one\", 2) + Bit(empty, "
      "4) "
      "+ Bit(Same(t, s), 8) +\n"
      "        Bit(Same(Greet(), \"world\"), 16) + Bit(Greet(\"x\") == "
      "\"x\", "
      "32);\n"
      "}\n"
      "Escapes :: () -> int {\n"
      "    quote := \"say \\\"hi\\\"\\\\\\n\";\n"
      "    return Bit(quote == \"say \\\"hi\\\"\\\\\\n\", 1) + Bit(quote "
      "!= "
      "\"say \\\"hi\\\"\\\\\\t\", 2) +\n"
      "        Bit(\"é\U0001F408\" == \"é\U0001F408\", 4) + Bit(\"on\" "
      "!= \"one\", 8) + Bit(\"onf\" != \"one\", 16) +\n"
      "        Bit(\"\" != \"a\", 32) + Bit(\"a\\u0000b\" == \"a\\u0000b\" "
      "&& "
      "\"a\\u0000b\" != \"a\", 64);\n"
      "}\n"
      "Loops :: () -> int {\n"
      "    n: int = 0;\n"
      "    for (i in 0..<5) {\n"
      "        if (Name(i) == \"many\") { n += 1; }\n"
      "    }\n"
      "    return n;\n"
      "}\n",
      3127061 },
    /* Strings held by records and union payloads: written, copied, passed,
     * returned and compared. Fields() 15 (one bit per check) and Variants()
     * 7, so Main is 15 + 7 * 1000. */
    { "string fields",
      "package scalar\n"
      "Label :: struct { name: string; count: int; }\n"
      "Shape :: union { Named(text: string, size: int) | Empty }\n"
      "Main :: () -> int { return Fields() + Variants() * 1000; }\n"
      "Rename :: (label: Label, name: string) -> Label {\n"
      "    renamed := label;\n"
      "    renamed.name = name;\n"
      "    return renamed;\n"
      "}\n"
      "Make :: (text: string) -> Shape { return Shape.Named(text, 7); }\n"
      "Bit :: (b: bool, weight: int) -> int {\n"
      "    if (b) { return weight; }\n"
      "    return 0;\n"
      "}\n"
      "Fields :: () -> int {\n"
      "    label: Label = {name: \"left\", count: 2};\n"
      "    other := Rename(label, \"right\");\n"
      "    return Bit(label.name == \"left\", 1) + Bit(other.name == "
      "\"right\", 2) +\n"
      "        Bit(label.name != other.name, 4) + Bit(other.count == 2, 8);\n"
      "}\n"
      "Variants :: () -> int {\n"
      "    shape := Make(\"abc\");\n"
      "    switch shape {\n"
      "        case .Named(text, size):\n"
      "            if (text == \"abc\") { return size; }\n"
      "            return 0;\n"
      "        case .Empty:\n"
      "            return -1;\n"
      "    }\n"
      "    return 0;\n"
      "}\n",
      7015 },
    /* Package constants are their folded values: alone, in folded
     * expressions, as a shift count and typed number, f64 and u64 values,
     * declared after a constant that names them.
     * Add(40, 2) + 8 + 100 + 1000 + Limit() (-40 + 2). */
    { "constants",
      "package scalar\n"
      "M: int : N / 20;\n"
      "N: int : 40;\n"
      "Half: number : 0.5;\n"
      "Big: f64 : 1e300;\n"
      "Wide: u64 : 18446744073709551615;\n"
      "Shift: u32 : 3;\n"
      "Main :: () -> int {\n"
      "    total := Add(N, M) + (1 << Shift);\n"
      "    if (Half < 1.0 && Big > 1e299) { total = total + 100; }\n"
      "    if (Wide == 18446744073709551615) { total = total + 1000; }\n"
      "    return total + Limit();\n"
      "}\n"
      "Add :: (a: int, b: int) -> int { return a + b; }\n"
      "Limit :: () -> int { return Add(-N, M); }\n",
      1112 },
    /* Not constant: the sum wraps at run time. */
    { "wrapped_operand",
      "package scalar\nMain :: () -> int { return Half(2147483647); }\n"
      "Half :: (x: int) -> int { return (x + 1) / 2; }\n",
      -1073741824 },
};
#undef NESTED_CLOSES
#undef NESTED_IDS
#undef CLOSES_10
#undef IDS_10
enum
{
    FOLDED_SOURCE_COUNT = (int)(sizeof FOLDED_SOURCES / sizeof *FOLDED_SOURCES)
};
/* Text natives through core:strings functions copied as they are:
 * byte counts, bytes inside and outside a string, slices clamped at
 * each end and slices of slices, compared and searched. Each value is
 * mixed into Main's result, which wraps. The source is joined from
 * pieces, each under C99's 4095-byte bound on a string literal. */

enum
{
    TEXT_NATIVES_ENTRY = -386181014
};
/* Allocating text through core:strings functions copied as they are, and
 * format_text of each scalar type, widths, precision, hex, explicit indexes,
 * escaped braces, an embedded NUL and no arguments. Each string's bytes are
 * mixed into Main's result, which wraps. The core:strings functions they
 * search with are TEXT_NATIVES'. */

enum
{
    TEXT_FORMATS_ENTRY = 1366853637
};
/* Why module `name`'s rows on VM b have no IR: the producer's rejection or
 * the consumer's validation error. */

/* Module `name` is accepted, and its Main gives `entry` on VM b and
 * natively at O0 and O2. */

/* TEXT_FORMATS, with TEXT_NATIVES' functions but its Main, gives its value
 * on both VMs and natively at O0 and O2, each native call formatting in a
 * context whose heap is freed after it. */

/* Each folded source and the text natives' source is accepted, and its Main
 * gives its value on both VMs and natively at O0 and O2. */

/* A constant that folds outside its type is still a located error. */

/* The integer types, as the width modules declare them: each one's name, the
 * name its functions take, its scalar_facts type, bits and signedness. */

typedef struct scalar_width
{
    const char *name, *title;
    int type, bits;
    bool is_unsigned;
} scalar_width;
static const scalar_width WIDTHS[] = {
    { "i8", "Int8", SCALAR_INT8, 8, false },
    { "u8", "Byte", SCALAR_BYTE, 8, true },
    { "i16", "Int16", SCALAR_INT16, 16, false },
    { "u16", "Uint16", SCALAR_UINT16, 16, true },
    { "int", "Int", SCALAR_INT, 32, false },
    { "u32", "Uint", SCALAR_UINT, 32, true },
    { "i64", "Int64", SCALAR_INT64, 64, false },
    { "u64", "Uint64", SCALAR_WORD, 64, true },
};
enum
{
    SCALAR_WIDTHS = (int)(sizeof WIDTHS / sizeof *WIDTHS),
    WIDTH_INT = 4,
    WIDTH_INT64 = 6,
    WIDTH_WORD = 7,
    WIDTH_SOURCE_BYTES = 4096
};
/* `bits` as a value of `w`: its low bits, sign-extended when `w` is signed,
 * so a uint64 is its bits. */

/* Whether value `a` of `t` is less than value `b` of `u`. */

/* The operations of a width module: the function, its expression over `a`
 * and `b` of the module's type or the uint shift count `n`, and its arity. */
enum
{
    WIDTH_ADD,
    WIDTH_SUB,
    WIDTH_MUL,
    WIDTH_DIV,
    WIDTH_MOD,
    WIDTH_AND,
    WIDTH_OR,
    WIDTH_XOR,
    WIDTH_LT,
    WIDTH_LE,
    WIDTH_GT,
    WIDTH_GE,
    WIDTH_EQ,
    WIDTH_NE,
    WIDTH_SHL,
    WIDTH_SHR,
    WIDTH_SHL_FAR,
    WIDTH_SHR_FAR,
    WIDTH_NEG,
    WIDTH_NOT,
    WIDTH_OPERATIONS
};
static const struct
{
    const char *name, *expression;
    int parameters;
} WIDTH_OPERATION[WIDTH_OPERATIONS] = {
    [WIDTH_ADD] = { "Add", "a + b", 2 },
    [WIDTH_SUB] = { "Sub", "a - b", 2 },
    [WIDTH_MUL] = { "Mul", "a * b", 2 },
    [WIDTH_DIV] = { "Div", "a / b", 2 },
    [WIDTH_MOD] = { "Mod", "a % b", 2 },
    [WIDTH_AND] = { "And", "a & b", 2 },
    [WIDTH_OR] = { "Or", "a | b", 2 },
    [WIDTH_XOR] = { "Xor", "a ^ b", 2 },
    [WIDTH_LT] = { "Lt", "a < b", 2 },
    [WIDTH_LE] = { "Le", "a <= b", 2 },
    [WIDTH_GT] = { "Gt", "a > b", 2 },
    [WIDTH_GE] = { "Ge", "a >= b", 2 },
    [WIDTH_EQ] = { "Eq", "a == b", 2 },
    [WIDTH_NE] = { "Ne", "a != b", 2 },
    [WIDTH_SHL] = { "Shl", "a << n", 2 },
    [WIDTH_SHR] = { "Shr", "a >> n", 2 },
    [WIDTH_SHL_FAR] = { "ShlFar", "a << 300", 1 },
    [WIDTH_SHR_FAR] = { "ShrFar", "a >> 300", 1 },
    [WIDTH_NEG] = { "Neg", "-a", 1 },
    [WIDTH_NOT] = { "Not", "~a", 1 },
};
/* The far shifts' constant count, which ShiftStep clamps to the type. */

/* The language's integer contract for operation `op` on values of `w`:
 * nothing fails, and a shift count is a uint at or past the width. */

/* The boundary values a width module's operations take: the least and
 * greatest, those next to them, 0, 1 and -1, or the top bit unsigned. */

/* The uint shift counts: in range, the width's edges and past it, and past
 * every narrower type's range, which the count's conversion clamps. */

/* The operations module of `w`: Main, then one function per operation. */

/* A call of `function` with `count` arguments that must give `expected`. */

/* The entry, then each operation of `w` on every pair of its samples, or
 * each sample with every shift count, or each sample alone. */

/* The values conversions take: each type's edges and those next to them, as
 * an int64 sample or, past it, a uint64 one. */

/* `T(value)` and `T(wrap: value)` for a value of `from` and T `to`. */

/* The conversions module of `to`: Main, then ClampT and WrapT for each type
 * T, which convert their T to `to`. */

/* The entry, then both conversions of each type's every sample it holds. */

/* `source`, a width module, is accepted on VM b, and each of its `calls`
 * gives its oracle's value on both VMs and natively at O0 and O2. */

/* Every integer type's operations and its conversions from every type, at
 * their boundaries, run natively as the integer contract and both VMs give
 * them. */

/* The operations float and double take, of WIDTH_OPERATION. */

static const int FLOATING_OPERATIONS[] = { WIDTH_ADD, WIDTH_SUB, WIDTH_MUL,
                                           WIDTH_DIV, WIDTH_LT,  WIDTH_LE,
                                           WIDTH_GT,  WIDTH_GE,  WIDTH_EQ,
                                           WIDTH_NE };
enum
{
    FLOATING_OPERATION_COUNT = (int)(sizeof FLOATING_OPERATIONS / sizeof *FLOATING_OPERATIONS),
    FLOATING_SAMPLES = 9
};

/* The values a floating module's operations take: both zeros, a finite value
 * of each sign, one that rounds, the extremes, both infinities and NaN. */

/* IEEE operation `op` on `a` and `b` at the width of `type`, rounding to
 * nearest. Every ordered comparison is false for NaN, and != true. */

/* The ints a floating module adds to its type: an int becomes a float
 * through double, so 2^24 + 1 rounds to even. */

/* The operations module of floating `type`: Main, each operation, then the
 * typing rules around it: literals, an int operand, and for float its
 * widening to double. */

/* The entry, each operation on every pair of samples, and the typing rules'
 * calls. A float literal is the binary32 nearest its binary64, so the
 * literal just past halfway between 1 and the next float rounds to 1. */

/* The values floats convert to integers from: each integer type's edges,
 * with fractions on either side, past every type, the infinities, NaN and
 * the least subnormals. */

/* `T(value)` for a float or double `value` and integer type `to`: truncated
 * toward zero, saturated at the type's bounds, and 0 for NaN. */

/* The ints converted to float through double rounding: below 2^53 and 2^24
 * they are exact, and past them each rounds to nearest, with 2^60 + 2^36 +
 * 1 rounding twice. */

/* The integer `value` of `from` as floating `type`: an int becomes a
 * double, then a float. */

/* The conversions module of floating `type`: Main, To<T> and From<T>
 * between it and each integer type T, and Narrow from double to float or
 * Widen from float to double. */

/* The entry, each To<T> of every sample as `type` holds it, each From<T> of
 * every integer sample T holds, and Narrow or Widen of every sample. */

/* Float and double operations, conversions to and from every integer type
 * and between the two, at their boundaries and IEEE's special values, run
 * natively as C's IEEE arithmetic and both VMs give them. */

/* Control flow over every statement form: else-if chains that end or fall
 * through, nested consequences with locals of their own, compound
 * assignments to parameters and to a byte local, every logical form and
 * bool equality. */

/* What CONTROL_SOURCE's integer functions give, as C computes them with the
 * integer contract: nothing fails, and a byte keeps its low bits. */

/* The entry, then each integer function on its samples, Far with each byte
 * and count, Choose with each
 * condition and ordered pair, and each bool function with every
 * combination of its arguments. */

/* Locals, assignments, nested if statements and logic run natively as C and
 * both VMs give them. */

/* Every loop form: ranges up and down, inclusive and exclusive, to the ends
 * of int and over byte bounds; a while loop with a condition, an endless one
 * that only a return leaves, nested loops whose break and continue leave or
 * restart the innermost, and an outer break and continue that pass over an
 * inner loop. Each range folds its indices into a hash, so
 * their order and count both show. */

/* What LOOP_SOURCE's functions give, as C computes them with the integer
 * contract. Indices are int64, so no C loop wraps at an end of int. */

/* The entry, each range function on every pair of RANGE_ENDS at most 16
 * apart, which includes every empty range, Bytes on every pair of bytes,
 * and the while loops on their samples. */

/* Loops, break and continue run natively as C and both VMs give them. */

/* Switches: arms that all return, a default arm written first that runs
 * last, a byte and an int64 value, arm values that are calls and
 * expressions, one repeated so only its first arm can run, break and
 * continue through a switch to its loop, and a switch nested in an arm. */

/* What SWITCH_SOURCE's functions give, as C computes them with the integer
 * contract. */

/* The entry, then each function on its samples: the int ones at and around
 * their arms' values and at the ends of int, Narrow on every byte, Long at
 * its arms and the ends of int64, and Pair on every pair of its samples. */

/* Switches run natively as C and both VMs give them. */

/* A function well past the bounds the facts once had: twelve parameters,
 * WIDE_LOOPS reversed exclusive ranges, then WIDE_CHAINS assignments of a
 * logical chain of five reads, then a return. Short chains keep stage zero's
 * operand recursion within its 64 calls. */
enum
{
    WIDE_LOOPS = 40,
    WIDE_CHAINS = 20
};

/* `function`(`argument`) of `source` fails on both VMs with `status`. */

/* How a native call of a function taking one int ends, at O0 and O2, within
 * `seconds`: with `value`, when `error` is CHIBI_PROCESS_OK and `signal` 0;
 * otherwise with `error`, and for CHIBI_PROCESS_OK with `signal`'s status
 * and `report` in its standard error. */

/* Each of `calls`, of functions of `source`, ends natively as it says. */

/* Spin never returns. Both VMs fail its call at their instruction budget.
 * Natively its loop runs unchecked, as Odin's does, and LLVM keeps it at O0
 * and O2, so the call runs until its deadline stops it. */

/* Direct, mutual and two-way recursion, recursion through a loop, and a
 * function that only ever calls itself. Forever's subtraction keeps its
 * call from being a tail call, which O2 would turn into an endless loop. */

/* What RECURSION_SOURCE's functions give, as C computes them with the
 * integer contract. */

/* The entry, then each function at the depths every backend runs: Sum, Even
 * and Odd at each depth below stage zero's call depth limit, Fib and Nodes on
 * their first 12, within the VMs' instruction budget, and Gcd on pairs of
 * zero, signs, the ends of int and consecutive Fibonacci numbers, its
 * deepest. */

/* Recursion runs natively as C and both VMs give it. */

/* Natively recursion is unchecked, as Rust's is: Sum, Even and Odd run 10,000
 * calls deep, far past any VM's limit, and Forever's endless recursion
 * faults in the stack's guard page, which the host reports as a stack
 * overflow before it aborts, at O0 and O2, well before the deadline. */

/* A function of LONG_LOCALS locals, then LONG_ASSIGNMENTS assignments, then a
 * return, one statement each, well past the bounds the facts once had. */
enum
{
    LONG_LOCALS = 40,
    LONG_ASSIGNMENTS = 100
};

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
/* Whether process `pid` has exited: it is gone, or a zombie whose parent
 * has not reaped it yet. Waits up to `seconds` for its kill to land. */
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

/* Starts the bundle, creating its directory if needed: an empty list of
 * modules for each target, and for a target whose objects reference a
 * runtime symbol, an object that defines it, which its runner links into
 * each library. */
TEST_CASE(native_bundle_starts_empty)
{
    if (!bundle_root()) {
        return true;
    }
    char directory[PATH_MAX];
    REQUIRE(native_directory(directory));
    bool started = !mkdir(bundle_root(), 0700) || errno == EEXIST;
    for (int t = 0; started && t < BUNDLE_TARGET_COUNT; t++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", bundle_root(), BUNDLE_TARGETS[t].directory);
        started = !mkdir(path, 0700) || errno == EEXIST;
        snprintf(
            path,
            sizeof path,
            "%s/%s/modules.txt",
            bundle_root(),
            BUNDLE_TARGETS[t].directory
        );
        started = started && write_text(path, "", "");
        if (!started || !BUNDLE_TARGETS[t].runtime) {
            continue;
        }
        char ir[128], object[PATH_MAX], module[PATH_MAX], undefined[NATIVE_OUTPUT];
        snprintf(ir, sizeof ir, "target triple = \"%s\"\n", BUNDLE_TARGETS[t].triple);
        snprintf(
            object,
            sizeof object,
            "%s/%s/runtime.o",
            bundle_root(),
            BUNDLE_TARGETS[t].directory
        );
        started = native_object(directory, "runtime", ir, BUNDLE_TARGETS[t].runtime, 0, object, module, undefined) &&
                  !*undefined && bundle_module_targets(module, t);
    }
    REQUIRE(remove_native_files(directory) && started);
    return true;
}
/* A dense switch, which O2 lowers through a table, links into a shared
 * library and runs: the driver's objects are position independent. */
static bool lookup_tables_link(const char *directory)
{
    static const char
        TABLE[] = "@index = internal global i32 3\n"
                  "define i32 @table() {\n"
                  "entry:\n"
                  "  %x = load volatile i32, ptr @index\n"
                  "  switch i32 %x, label %other [ i32 0, label %a\n"
                  "    i32 1, label %b\n    i32 2, label %c\n"
                  "    i32 3, label %d\n    i32 4, label %e ]\n"
                  "a:\n  ret i32 11\nb:\n  ret i32 23\nc:\n  ret i32 5\n"
                  "d:\n  ret i32 47\ne:\n  ret i32 9\nother:\n  ret i32 0\n"
                  "}\n";
    char library[PATH_MAX];
    REQUIRE(native_library(directory, "table", TABLE, "", 2, library));
    scalar_call call = { .symbol = "table" };
    REQUIRE(native_passed(library_calls(directory, library, &call, 1, 2, CALLS_SECONDS)));
    REQUIRE(call.values[2] == 47);
    return true;
}
/* The SIGSEGV handler a stack_overflow child installs before the report,
 * with SIGUSR1 in its mask: it exits 7 when that mask is in force. */

/* The child of stack_overflow_leaves_other_faults. `mode` is "handled" or
 * "default", the action SIGSEGV has before the report is installed, then
 * "-fault" for a write to an unmapped page or "-sent" for raise(SIGSEGV). */

/* The SIGSEGV handler a stack_overflow child installs before the report,
 * with SIGUSR1 in its mask: it exits 7 when that mask is in force. */

/* The child of stack_overflow_leaves_other_faults. `mode` is "handled" or
 * "default", the action SIGSEGV has before the report is installed, then
 * "-fault" for a write to an unmapped page or "-sent" for raise(SIGSEGV). */
static void handled_fault(int)
{
    sigset_t blocked;
    sigprocmask(SIG_SETMASK, NULL, &blocked);
    _exit(sigismember(&blocked, SIGUSR1) ? 7 : 8);
}
int test_language_stack_overflow_child(const char *mode)
{
    struct sigaction before = { .sa_handler = SIG_DFL };
    sigemptyset(&before.sa_mask);
    if (!strncmp(mode, "handled", strlen("handled"))) {
        before.sa_handler = handled_fault;
        sigaddset(&before.sa_mask, SIGUSR1);
    }
    sigaction(SIGSEGV, &before, NULL);
    stack_overflow_init();
    if (strstr(mode, "-fault")) {
        *(volatile int *)(uintptr_t)16 = 0;
    } else {
        raise(SIGSEGV);
    }
    return 1;
}
/* A SIGSEGV that is not a stack overflow, a fault or a sent signal, goes to
 * the action installed before the report, with that action's mask. */
TEST_CASE(stack_overflow_leaves_other_faults)
{
    static const struct
    {
        const char *mode;
        int status;
    } cases[] = { { "handled-fault", 7 },
                  { "handled-sent", 7 },
                  { "default-fault", -SIGSEGV },
                  { "default-sent", -SIGSEGV } };
    char self[PATH_MAX], out[NATIVE_OUTPUT];
    ssize_t length = readlink("/proc/self/exe", self, sizeof self - 1);
    REQUIRE(length > 0);
    self[length] = 0;
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        const char *const argv[] = { self, "--stack-overflow-child", cases[i].mode, NULL };
        chibi_process_capture_result ended = run_native(argv, "/", CALLS_SECONDS, out);
        REQUIRE(ended.error == CHIBI_PROCESS_OK && ended.status == cases[i].status);
        REQUIRE(!strstr(native_errors, "stack overflow"));
    }
    return true;
}
/* The rows of the library `source`, emitted for Linux x86-64 as LLVM text. */
static bool library_ir(const char *source, char **ir)
{
    pin_value rows;
    REQUIRE(library_rows(source, &rows));
    const pin_array *array = rows.ptr;
    REQUIRE(array->count > 2);
    REQUIRE(target_ir("LinuxX64", &rows, ir) && *ir);
    arena_free(&detached);
    return true;
}
/* Edit and Continue on native code: a loop's Frame counts with a retained
 * local; the loop is edited to count by ten and to add a new retained
 * bonus; the host moves the count to the edited library and its next Frames
 * carry on from it, with the new state at its initializer, and Main, which
 * a library lacks, is never called. */
TEST_CASE(package_compiler_native_edit_and_continue_keeps_state)
{
    static const char before[] = "Loop :: library {\n"
                                 "    Frame :: () -> int {\n"
                                 "        count: retain int = 0;\n"
                                 "        count += 1;\n"
                                 "        return count;\n"
                                 "    }\n"
                                 "}\n",
                      after[] = "Loop :: library {\n"
                                "    Frame :: () -> int {\n"
                                "        count: retain int = 0;\n"
                                "        bonus: retain int = 100;\n"
                                "        count += 10;\n"
                                "        return count + bonus;\n"
                                "    }\n"
                                "}\n";
    REQUIRE(prepare());
    char directory[PATH_MAX], first[PATH_MAX], second[PATH_MAX], self[PATH_MAX],
        out[NATIVE_OUTPUT];
    REQUIRE(native_directory(directory));
    char *ir;
    REQUIRE(library_ir(before, &ir));
    bool built = native_library(directory, "loop-before", ir, "", 0, first);
    free(ir);
    REQUIRE(built && library_ir(after, &ir));
    built = native_library(directory, "loop-after", ir, "", 0, second);
    free(ir);
    REQUIRE(built);
    ssize_t length = readlink("/proc/self/exe", self, sizeof self - 1);
    REQUIRE(length > 0);
    self[length] = 0;
    const char *const argv[] = { self,   "--package-compiler-chibi-native-call",
                                 first,  "continue",
                                 second, NULL };
    chibi_process_capture_result ran = run_native(argv, directory, CALLS_SECONDS, out);
    REQUIRE(native_passed(ran));
    /* Three Frames of the first, then the edited one carries the count on. */
    REQUIRE(!strcmp(out, "1\n2\n3\n113\n123\n"));
    REQUIRE(remove_native_files(directory));
    return true;
}
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
    bool linked = lookup_tables_link(directory);
    bool built = native_library(directory, "hang", loop, "", 0, library);
    started = chibi_process_now();
    result = built ? library_calls(directory, library, &call, 1, 2, 1)
                   : (chibi_process_capture_result){ 0 };
    double elapsed = chibi_process_now() - started;
    REQUIRE(remove_native_files(directory) && linked && built);
    REQUIRE(result.error == CHIBI_PROCESS_TIMEOUT && elapsed < PROMPT_SECONDS);
    return true;
}
/* A module whose entry returns 0 and whose function k + 1 applies operation
 * k to its parameters. */

static bool total_ir(const char *function, char **ir)
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
    char *text;
    REQUIRE(native_module_ir(function, &m, &text) && text);
    REQUIRE(
        !strstr(text, "label %trap\n  br") && !strstr(text, " nsw ") &&
        !strstr(text, " nuw ")
    );
    *ir = text;
    return true;
}
/* The validation-only adapter probe_NAME(ptr) of each operation, which
 * takes and returns int64 as append_adapter's do. */
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
            "\ndefine i64 @probe_%s(ptr %%args) {\n  %%w0 = load i64, ptr "
            "%%args\n  %%a0 = trunc i64 %%w0 to i32\n",
            TOTAL_OPERATIONS[k].name
        );
        if (binary) {
            n += snprintf(text + n, sizeof text - (size_t)n, "  %%p1 = getelementptr i64, ptr %%args, i64 1\n  %%w1 = load i64, ptr %%p1\n  %%a1 = trunc i64 %%w1 to i32\n");
        }
        snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%r = call i32 @chibi_scalar.%d(i32 %%a0%s)\n  %%z = sext "
            "i32 "
            "%%r to i64\n  ret i64 %%z\n}\n",
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

/* With a bundle, the total module for each other target, with every
 * operation's calls. */
static bool total_bundled(const char *directory, const char *adapters)
{
    if (!bundle_root()) {
        return true;
    }
    scalar_call *calls = xmalloc(sizeof *calls * MAX_WIDTH_CALLS);
    int count = 0;
    for (int k = 0; k < TOTAL_OPERATION_COUNT; k++) {
        count += total_calls(k, calls + count);
    }
    assert(count <= MAX_WIDTH_CALLS);
    bool built = true;
    for (int t = 0; built && t < BUNDLE_TARGET_COUNT; t++) {
        char *ir = NULL;
        built = total_ir(BUNDLE_TARGETS[t].function, &ir) &&
                bundle_module(directory, t, "total", ir, adapters, calls, count);
        free(ir);
    }
    free(calls);
    REQUIRE(built);
    return true;
}
TEST_CASE(package_compiler_native_arithmetic_is_total)
{
    char *ir;
    REQUIRE(total_ir("LinuxX64", &ir));
    char *adapters = total_adapters(), directory[PATH_MAX];
    REQUIRE(native_directory(directory));
    bool agreed = true;
    for (int level = 0; agreed && level < 2; level++) {
        char library[PATH_MAX];
        agreed = native_library(directory, "total", ir, adapters, level * 2, library);
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
                        "native %s(%" PRId64 ", %" PRId64
                        ") at O%d gave %" PRId64 ", expected %" PRId64 "\n",
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
    agreed = agreed && total_bundled(directory, adapters);
    free(adapters);
    REQUIRE(remove_native_files(directory) && agreed);
    return true;
}
/* The corpus's array cases run natively at O0 and O2 and give the value each
 * case expects. A probe adapter passes no context, which arrays need to
 * allocate, so each is checked through its entry. An index outside an array
 * ends the program with its message. */
TEST_CASE(package_compiler_native_arrays_run_and_check_indexes)
{
    REQUIRE(prepare());
    char directory[PATH_MAX];
    REQUIRE(native_directory(directory));
    int count;
    REQUIRE(scalar_host_case_count(&count));
    int ran = 0;
    for (int index = 0; index < count; index++) {
        char *name, *source;
        pin_value rows;
        int expected;
        REQUIRE(scalar_host_case_text(false, index, &name));
        if (strncmp(name, "array_", strlen("array_")) &&
            strncmp(name, "union_array", strlen("union_array")) &&
            strncmp(name, "record_", strlen("record_")) &&
            strncmp(name, "switch_on", strlen("switch_on"))) {
            free(name);
            continue;
        }
        REQUIRE(scalar_host_case_text(true, index, &source));
        REQUIRE(collected_rows(name, source, &rows));
        free(source);
        if (row_int(&rows, 1, "kind") == SCALAR_REJECTION_ROW) {
            free(name);
            arena_free(&detached);
            continue;
        }
        REQUIRE(scalar_host_case_expected(index, &expected));
        char *ir;
        REQUIRE(emitted_ir(&rows, &ir));
        REQUIRE(ir);
        scalar_call entry = { .symbol = "chibi_scalar" };
        REQUIRE(native_values(directory, name, &rows, ir, &entry, 1));
        if (entry.values[2] != expected || entry.values[3] != expected) {
            fprintf(
                stderr,
                "native %s gave %" PRId64 " at O0 and %" PRId64
                " at O2, expected %d\n",
                name,
                entry.values[2],
                entry.values[3],
                expected
            );
        }
        REQUIRE(entry.values[2] == expected && entry.values[3] == expected);
        free(ir);
        free(name);
        ran++;
        arena_free(&detached);
    }
    REQUIRE(ran >= 18);
    /* Index 3 is past the three elements, as is -1. */
    static const char *const OUTSIDE[] = {
        "package scalar\nMain :: () -> int {\n    items: int[] = [1, 2, 3];\n  "
        "  return items[Where()];\n}\n"
        "Where :: () -> int { return 3; }\n",
        "package scalar\nMain :: () -> int {\n    items: int[] = [1, 2, 3];\n  "
        "  items[Where()] = 4;\n    return 0;\n}\n"
        "Where :: () -> int { return -1; }\n",
    };
    static const char *const MESSAGES[] = {
        "native array index 3 is out of range for length 3",
        "native array index -1 is out of range for length 3"
    };
    for (int i = 0; i < 2; i++) {
        pin_value rows;
        REQUIRE(collected_rows("outside", OUTSIDE[i], &rows));
        char *ir;
        REQUIRE(emitted_ir(&rows, &ir));
        REQUIRE(ir);
        for (int level = 0; level < 2; level++) {
            char library[PATH_MAX];
            REQUIRE(native_library(directory, "outside", ir, "", level * 2, library));
            scalar_call entry = { .symbol = "chibi_scalar", .context = true };
            chibi_process_capture_result
                ended = library_calls(directory, library, &entry, 1, 2, CALLS_SECONDS);
            REQUIRE(ended.error == CHIBI_PROCESS_OK && ended.status == -SIGABRT);
            REQUIRE(strstr(native_errors, MESSAGES[i]));
        }
        free(ir);
        arena_free(&detached);
    }
    REQUIRE(remove_native_files(directory));
    return true;
}
#endif
/* On every host: each corpus case's producer rows are what the case expects,
 * and an accepted module's rows validate and emit Linux x86-64 IR, which the
 * native lane runs only on Linux x86-64. */
TEST_CASE(package_compiler_scalar_corpus_rows_validate_and_emit)
{
    REQUIRE(prepare());
    int count;
    REQUIRE(scalar_host_case_count(&count));
    for (int index = 0; index < count; index++) {
        char *name, *source;
        pin_value rows;
        int checked;
        REQUIRE(scalar_host_case_text(false, index, &name));
        // The 64-deep nesting passes the stage-zero fixture's call depth.
        if (!strcmp(name, "deep_operands")) {
            free(name);
            continue;
        }
        REQUIRE(scalar_host_case_text(true, index, &source));
        REQUIRE(scalar_host_collect("Scalar.chibi", source, &detached, &rows));
        REQUIRE(scalar_host_case_check(&rows, &checked));
        const pin_array *produced = rows.ptr;
        pin_value *second = &produced->items[1];
        bool rejected = scalar_field(second, "kind").i == SCALAR_REJECTION_ROW;
        if (checked != 1) {
            int from = rejected ? (int)scalar_field(second, "start").i : 0,
                to = rejected ? (int)scalar_field(second, "end").i : 0;
            fprintf(
                stderr,
                "scalar case %s: Check gave %d%s%s at `%.*s`\n",
                name,
                checked,
                rejected ? ", rejected: " : "",
                rejected ? pin_text(scalar_field(second, "text")).bytes : "",
                to > from ? to - from : 0,
                to > from ? source + from : ""
            );
        }
        // A rejection's message is the parser's, which the recovered compiler's
        // differs from in places; the corpus's native lane and Run.ps1 check it.
        REQUIRE(checked == 1 || rejected);
        if (!rejected && !validated(&rows, "")) {
            fprintf(stderr, "scalar case %s: its rows do not validate and emit\n", name);
            return false;
        }
        free(name);
        free(source);
        arena_free(&detached);
    }
    return true;
}
/* Query tests use the target's canonical Tick(dt: number), while the older
 * behavior fixtures above exercise an event named DeltaSeconds. */

/* Play without the target entity, so a query's rows are the owner and the
 * entities the test adds. */

/* Each source's queries and system header compile into its own module, in
 * whatever position the project lists it: each module matches the one its
 * source compiles to alone, and its queries run. */
/* Compile `sources` together, then play each query-bearing module alone. A
 * system adds 2 to every row's y; a behavior's rows end at (x, y). */

/* A query captures a local and calls a helper whose loop changes a local
 * that shadows a state. An earlier source whose offsets run past the query's
 * must hide neither from the later one: each scan stays in its own source. */

/* This test-only source joins the compiler fixture's package, so its probe
 * can exercise private scope collection without adding a shipped export. */

/* The consumer's llvm_native.Emit takes its module as JSON tables on every
 * host: Main calls Pick(7, true, 9), and Pick returns its third argument. */
TEST_CASE(package_compiler_native_modules_emit_through_the_consumer)
{
    REQUIRE(prepare());
    static const native_code code[] = {
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
    static const native_function functions[] = {
        { NATIVE_INT, 0, 0, 0, 4, 0, 7, 0, 3 },
        { NATIVE_INT, 0, 3, 4, 1, 7, 4, 3, 0 }
    };
    static const int parameters[] = { NATIVE_INT, NATIVE_BOOL, NATIVE_INT };
    static const int slots[] = { NATIVE_INT, NATIVE_BOOL, NATIVE_INT, NATIVE_INT, NATIVE_INT };
    static const int arguments[] = { 0, 1, 2 };
    native_module m = { functions, 2,    parameters, 3,         slots,
                        5,         code, 11,         arguments, 3 };
    char *ir;
    REQUIRE(native_module_ir("LinuxX64", &m, &ir) && ir);
    bool emitted = strstr(ir, "define internal i32 @chibi_scalar.1(") &&
                   strstr(ir, "call i32 @chibi_scalar.1(");
    if (!emitted) {
        fprintf(stderr, "%s\n", ir);
    }
    free(ir);
    REQUIRE(emitted);
    /* A call to a function with another signature is rejected: no text. */
    native_code changed[11];
    memcpy(changed, code, sizeof changed);
    changed[4].left = 2;
    m.code = changed;
    REQUIRE(native_module_ir("LinuxX64", &m, &ir) && !ir);
    return true;
}
void test_language_package_compiler_chibi_scalar()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_scalar_rows_outlive_the_producer);
    RUN_TEST_CASE(package_compiler_scalar_rows_span_imported_packages);
    RUN_TEST_CASE(package_compiler_scalar_rows_span_collection_packages);
    RUN_TEST_CASE(package_compiler_scalar_rejections_lie_in_package_sets);
    RUN_TEST_CASE(package_compiler_scalar_results_require_the_fixture_value);
    RUN_TEST_CASE(package_compiler_scalar_rejects_unions_and_reads_libraries);
    RUN_TEST_CASE(package_compiler_scalar_retains_library_states);
    RUN_TEST_CASE(package_compiler_scalar_corpus_rows_validate_and_emit);
    RUN_TEST_CASE(package_compiler_native_modules_emit_through_the_consumer);
    pu_clear_setup();
    scalar_host_close();
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
int test_language_stack_overflow_child(const char *)
{
    abort();
}

#endif
#if defined(__linux__) && defined(__x86_64__)
static void cancel_native(int)
{
    native_cancelled = 1;
}
#endif
void test_language_package_compiler_chibi_native()
{
#if defined(__linux__) && defined(__x86_64__)
    if (!getenv("CHIBI_LLVM_BIN")) {
        fprintf(stderr, "native scalar execution skipped: set CHIBI_LLVM_BIN to the directory of opt, llc, ld.lld and llvm-nm\n");
        return;
    }
    /* The supervised children run in their own process groups, so a signal
     * to this one cancels them instead of leaving them running. */
    struct sigaction cancel = { .sa_handler = cancel_native }, interrupt, terminate;
    sigemptyset(&cancel.sa_mask);
    sigaction(SIGINT, &cancel, &interrupt);
    sigaction(SIGTERM, &cancel, &terminate);
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_bundle_starts_empty);
    RUN_TEST_CASE(stack_overflow_leaves_other_faults);
    RUN_TEST_CASE(package_compiler_native_runs_are_bounded);
    RUN_TEST_CASE(package_compiler_native_arithmetic_is_total);
    RUN_TEST_CASE(package_compiler_native_arrays_run_and_check_indexes);
    RUN_TEST_CASE(package_compiler_native_edit_and_continue_keeps_state);
    pu_clear_setup();
    scalar_host_close();
    sigaction(SIGINT, &interrupt, NULL);
    sigaction(SIGTERM, &terminate, NULL);
#else
    fprintf(stderr, "native scalar execution skipped: it runs on Linux x86-64; other targets run its bundle\n");
#endif
}

/* Fixed primitive-native argument association, checked against literal
 * results and the independent stage-zero reference. */

/* Stage zero picks native.overload's integer or text overload by the
 * argument; an untyped number never converts to text, so it cannot pick the
 * text one. native.kernel's number overload takes literals and typed values
 * other than i64, which takes the f64 one. */
