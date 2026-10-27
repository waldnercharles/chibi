#include "authoring/native_bindings.h"
#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "runtime/arrays_definition.h"
#include "runtime/native_capabilities.h"
#include "runtime/text_definition.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_bootstrap.h"
#include "toolchain/compiler_host.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* Pins integer semantics, the contract native lowering must keep: no integer
 * operation fails at run time. One sample per width covers wrapping add, sub,
 * mul and negation; division and remainder by zero, which give 0 and the
 * dividend, and of the signed minimum by -1, which wrap to the minimum with
 * remainder 0; unsigned shift counts at or past the width, which shift every
 * bit out; signed and unsigned order; and conversions from int64, uint64 and
 * double, where T(v) clamps and wrap: keeps the low bits. The other samples
 * cover the compile-time rules for literal and constant operands and mixed
 * widths, and evaluation order. Stage zero and the authored compiler its
 * bootstrap artifact holds each compile every sample, and each closure runs
 * every call on bytecode and SLJIT. */
enum
{
    CALLS = 64,
    COMPILE_BUDGET = 200000000,
    CALL_BUDGET = 100000
};
/* A call gives `expected`, its result array as JSON. No integer operation
 * fails, so a failing call never agrees. */
typedef struct
{
    const char *function, *arguments, *expected;
} integer_call;
/* A sample with a message is one both compilers reject with it at
 * line:column. */
typedef struct
{
    const char *name, *source, *message;
    int line, column;
    integer_call calls[CALLS];
} integer_sample;
/* Every operation on one width T. A signed T also negates. */
#define WIDTH_SOURCE(T)                                                        \
    "package facts\n"                                                          \
    "Add :: (a: " T ", b: " T ") -> " T " { return a + b; }\n"                 \
    "Sub :: (a: " T ", b: " T ") -> " T " { return a - b; }\n"                 \
    "Mul :: (a: " T ", b: " T ") -> " T " { return a * b; }\n"                 \
    "Div :: (a: " T ", b: " T ") -> " T " { return a / b; }\n"                 \
    "Rem :: (a: " T ", b: " T ") -> " T " { return a % b; }\n"                 \
    "Shl :: (a: " T ", n: u8) -> " T " { return " T "(wrap: a << n); }\n"      \
    "Shr :: (a: " T ", n: u8) -> " T " { return " T "(wrap: a >> n); }\n"      \
    "Less :: (a: " T ", b: " T ") -> bool { return a < b; }\n"                 \
    "Converted :: (v: i64) -> " T " { return " T "(v); }\n"                    \
    "Wrapped :: (v: i64) -> " T " { return " T "(wrap: v); }\n"                \
    "ConvertedWide :: (v: u64) -> " T " { return " T "(v); }\n"                \
    "WrappedWide :: (v: u64) -> " T " { return " T "(wrap: v); }\n"            \
    "Truncated :: (v: f64) -> " T " { return " T "(v); }\n"                    \
    "NotANumber :: () -> " T " { z: f64 = 0.0; return " T "(z / z); }\n"       \
    "Count :: (n: ref int, v: " T ") -> " T " { n += 1; return v; }\n"         \
    "CountWide :: (n: ref int, v: i64) -> i64 { n += 1; return v; }\n"         \
    "UnusedDiv :: (a: " T ", b: " T                                            \
    ") -> int { n := 0; q := a / Count(ref n, "                                \
    "b); return n; }\n"                                                        \
    "UnusedShl :: (a: " T ", s: u8) -> int { n := 0; q := Count(ref n, a) << " \
    "s; return n; }\n"                                                         \
    "UnusedConverted :: (v: i64) -> int { n := 0; x := " T                     \
    "(CountWide(ref n, "                                                       \
    "v)); return n; }\n"
#define SIGNED_SOURCE(T)                                                       \
    WIDTH_SOURCE(T) "Neg :: (a: " T ") -> " T " { return -a; }\n"

static const integer_sample samples[] = {
    { .name = "i8",
      .source = SIGNED_SOURCE("i8"),
      .calls = { { .function = "Add", .arguments = "[127,1]", .expected = "[-128]" },
                 { .function = "Add", .arguments = "[-128,-1]", .expected = "[127]" },
                 { .function = "Sub", .arguments = "[-128,1]", .expected = "[127]" },
                 { .function = "Sub", .arguments = "[127,-1]", .expected = "[-128]" },
                 { .function = "Mul", .arguments = "[127,2]", .expected = "[-2]" },
                 { .function = "Mul", .arguments = "[-128,-1]", .expected = "[-128]" },
                 { .function = "Div", .arguments = "[7,-2]", .expected = "[-3]" },
                 { .function = "Div", .arguments = "[-7,2]", .expected = "[-3]" },
                 { .function = "Div", .arguments = "[-128,1]", .expected = "[-128]" },
                 { .function = "Div", .arguments = "[1,0]", .expected = "[0]" },
                 { .function = "Div", .arguments = "[-128,-1]", .expected = "[-128]" },
                 { .function = "Rem", .arguments = "[-7,2]", .expected = "[-1]" },
                 { .function = "Rem", .arguments = "[7,-2]", .expected = "[1]" },
                 { .function = "Rem", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Rem", .arguments = "[-128,-1]", .expected = "[0]" },
                 { .function = "Neg", .arguments = "[-128]", .expected = "[-128]" },
                 { .function = "Neg", .arguments = "[127]", .expected = "[-127]" },
                 { .function = "Shl", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,7]", .expected = "[-128]" },
                 { .function = "Shl", .arguments = "[127,1]", .expected = "[-2]" },
                 { .function = "Shl", .arguments = "[1,8]", .expected = "[0]" },
                 { .function = "Shl", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Shr", .arguments = "[-128,7]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[-1,1]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[-128,8]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Less", .arguments = "[-128,127]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[-1,0]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[127,-128]", .expected = "[false]" },
                 { .function = "Converted", .arguments = "[127]", .expected = "[127]" },
                 { .function = "Converted", .arguments = "[128]", .expected = "[127]" },
                 { .function = "Converted", .arguments = "[-129]", .expected = "[-128]" },
                 { .function = "Wrapped", .arguments = "[128]", .expected = "[-128]" },
                 { .function = "Wrapped", .arguments = "[261]", .expected = "[5]" },
                 { .function = "Wrapped", .arguments = "[-129]", .expected = "[127]" },
                 { .function = "ConvertedWide", .arguments = "[127]", .expected = "[127]" },
                 { .function = "ConvertedWide", .arguments = "[128]", .expected = "[127]" },
                 { .function = "WrappedWide",
                   .arguments = "[18446744073709551615]",
                   .expected = "[-1]" },
                 { .function = "WrappedWide", .arguments = "[128]", .expected = "[-128]" },
                 { .function = "Truncated", .arguments = "[2.9]", .expected = "[2]" },
                 { .function = "Truncated", .arguments = "[-2.9]", .expected = "[-2]" },
                 { .function = "Truncated", .arguments = "[1e300]", .expected = "[127]" },
                 { .function = "Truncated", .arguments = "[-1e300]", .expected = "[-128]" },
                 { .function = "NotANumber", .arguments = "[]", .expected = "[0]" },
                 { .function = "UnusedDiv", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "UnusedDiv",
                   .arguments = "[-128,-1]",
                   .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,8]", .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,255]", .expected = "[1]" },
                 { .function = "UnusedConverted",
                   .arguments = "[128]",
                   .expected = "[1]" }, } },
    { .name = "u8",
      .source = WIDTH_SOURCE("u8"),
      .calls = { { .function = "Add", .arguments = "[255,1]", .expected = "[0]" },
                 { .function = "Sub", .arguments = "[0,1]", .expected = "[255]" },
                 { .function = "Mul", .arguments = "[255,2]", .expected = "[254]" },
                 { .function = "Mul", .arguments = "[255,255]", .expected = "[1]" },
                 { .function = "Div", .arguments = "[255,2]", .expected = "[127]" },
                 { .function = "Div", .arguments = "[1,0]", .expected = "[0]" },
                 { .function = "Rem", .arguments = "[255,10]", .expected = "[5]" },
                 { .function = "Rem", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,7]", .expected = "[128]" },
                 { .function = "Shl", .arguments = "[255,1]", .expected = "[254]" },
                 { .function = "Shl", .arguments = "[1,8]", .expected = "[0]" },
                 { .function = "Shl", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Shr", .arguments = "[255,7]", .expected = "[1]" },
                 { .function = "Shr", .arguments = "[128,1]", .expected = "[64]" },
                 { .function = "Shr", .arguments = "[255,8]", .expected = "[0]" },
                 { .function = "Less", .arguments = "[0,255]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[128,1]", .expected = "[false]" },
                 { .function = "Less", .arguments = "[1,128]", .expected = "[true]" },
                 { .function = "Converted", .arguments = "[-1]", .expected = "[0]" },
                 { .function = "Wrapped", .arguments = "[-1]", .expected = "[255]" },
                 { .function = "Converted", .arguments = "[255]", .expected = "[255]" },
                 { .function = "Converted", .arguments = "[256]", .expected = "[255]" },
                 { .function = "Wrapped", .arguments = "[256]", .expected = "[0]" },
                 { .function = "Wrapped", .arguments = "[261]", .expected = "[5]" },
                 { .function = "ConvertedWide", .arguments = "[255]", .expected = "[255]" },
                 { .function = "ConvertedWide", .arguments = "[256]", .expected = "[255]" },
                 { .function = "WrappedWide",
                   .arguments = "[18446744073709551615]",
                   .expected = "[255]" },
                 { .function = "Truncated", .arguments = "[2.9]", .expected = "[2]" },
                 { .function = "Truncated", .arguments = "[-2.9]", .expected = "[0]" },
                 { .function = "Truncated", .arguments = "[1e300]", .expected = "[255]" },
                 { .function = "Truncated", .arguments = "[-1e300]", .expected = "[0]" },
                 { .function = "NotANumber", .arguments = "[]", .expected = "[0]" },
                 { .function = "UnusedDiv", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,8]", .expected = "[1]" },
                 { .function = "UnusedConverted",
                   .arguments = "[-1]",
                   .expected = "[1]" }, } },
    { .name = "i16",
      .source = SIGNED_SOURCE("i16"),
      .calls = { { .function = "Add", .arguments = "[32767,1]", .expected = "[-32768]" },
                 { .function = "Add", .arguments = "[-32768,-1]", .expected = "[32767]" },
                 { .function = "Sub", .arguments = "[-32768,1]", .expected = "[32767]" },
                 { .function = "Sub", .arguments = "[32767,-1]", .expected = "[-32768]" },
                 { .function = "Mul", .arguments = "[32767,2]", .expected = "[-2]" },
                 { .function = "Mul", .arguments = "[-32768,-1]", .expected = "[-32768]" },
                 { .function = "Div", .arguments = "[7,-2]", .expected = "[-3]" },
                 { .function = "Div", .arguments = "[-7,2]", .expected = "[-3]" },
                 { .function = "Div", .arguments = "[-32768,1]", .expected = "[-32768]" },
                 { .function = "Div", .arguments = "[1,0]", .expected = "[0]" },
                 { .function = "Div", .arguments = "[-32768,-1]", .expected = "[-32768]" },
                 { .function = "Rem", .arguments = "[-7,2]", .expected = "[-1]" },
                 { .function = "Rem", .arguments = "[7,-2]", .expected = "[1]" },
                 { .function = "Rem", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Rem", .arguments = "[-32768,-1]", .expected = "[0]" },
                 { .function = "Neg", .arguments = "[-32768]", .expected = "[-32768]" },
                 { .function = "Neg", .arguments = "[32767]", .expected = "[-32767]" },
                 { .function = "Shl", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,15]", .expected = "[-32768]" },
                 { .function = "Shl", .arguments = "[32767,1]", .expected = "[-2]" },
                 { .function = "Shl", .arguments = "[1,16]", .expected = "[0]" },
                 { .function = "Shl", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Shr", .arguments = "[-32768,15]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[-1,1]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[-32768,16]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Less", .arguments = "[-32768,32767]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[-1,0]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[32767,-32768]", .expected = "[false]" },
                 { .function = "Converted", .arguments = "[32767]", .expected = "[32767]" },
                 { .function = "Converted", .arguments = "[32768]", .expected = "[32767]" },
                 { .function = "Converted", .arguments = "[-32769]", .expected = "[-32768]" },
                 { .function = "Wrapped", .arguments = "[32768]", .expected = "[-32768]" },
                 { .function = "Wrapped", .arguments = "[65541]", .expected = "[5]" },
                 { .function = "Wrapped", .arguments = "[-32769]", .expected = "[32767]" },
                 { .function = "ConvertedWide", .arguments = "[32767]", .expected = "[32767]" },
                 { .function = "ConvertedWide", .arguments = "[32768]", .expected = "[32767]" },
                 { .function = "WrappedWide",
                   .arguments = "[18446744073709551615]",
                   .expected = "[-1]" },
                 { .function = "WrappedWide", .arguments = "[32768]", .expected = "[-32768]" },
                 { .function = "Truncated", .arguments = "[2.9]", .expected = "[2]" },
                 { .function = "Truncated", .arguments = "[-2.9]", .expected = "[-2]" },
                 { .function = "Truncated", .arguments = "[1e300]", .expected = "[32767]" },
                 { .function = "Truncated", .arguments = "[-1e300]", .expected = "[-32768]" },
                 { .function = "NotANumber", .arguments = "[]", .expected = "[0]" },
                 { .function = "UnusedDiv", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "UnusedDiv",
                   .arguments = "[-32768,-1]",
                   .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,16]", .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,255]", .expected = "[1]" },
                 { .function = "UnusedConverted",
                   .arguments = "[32768]",
                   .expected = "[1]" }, } },
    { .name = "u16",
      .source = WIDTH_SOURCE("u16"),
      .calls = { { .function = "Add", .arguments = "[65535,1]", .expected = "[0]" },
                 { .function = "Sub", .arguments = "[0,1]", .expected = "[65535]" },
                 { .function = "Mul", .arguments = "[65535,2]", .expected = "[65534]" },
                 { .function = "Mul", .arguments = "[65535,65535]", .expected = "[1]" },
                 { .function = "Div", .arguments = "[65535,2]", .expected = "[32767]" },
                 { .function = "Div", .arguments = "[1,0]", .expected = "[0]" },
                 { .function = "Rem", .arguments = "[65535,10]", .expected = "[5]" },
                 { .function = "Rem", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,15]", .expected = "[32768]" },
                 { .function = "Shl", .arguments = "[65535,1]", .expected = "[65534]" },
                 { .function = "Shl", .arguments = "[1,16]", .expected = "[0]" },
                 { .function = "Shl", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Shr", .arguments = "[65535,15]", .expected = "[1]" },
                 { .function = "Shr", .arguments = "[32768,1]", .expected = "[16384]" },
                 { .function = "Shr", .arguments = "[65535,16]", .expected = "[0]" },
                 { .function = "Less", .arguments = "[0,65535]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[32768,1]", .expected = "[false]" },
                 { .function = "Less", .arguments = "[1,32768]", .expected = "[true]" },
                 { .function = "Converted", .arguments = "[-1]", .expected = "[0]" },
                 { .function = "Wrapped", .arguments = "[-1]", .expected = "[65535]" },
                 { .function = "Converted", .arguments = "[65535]", .expected = "[65535]" },
                 { .function = "Converted", .arguments = "[65536]", .expected = "[65535]" },
                 { .function = "Wrapped", .arguments = "[65536]", .expected = "[0]" },
                 { .function = "Wrapped", .arguments = "[65541]", .expected = "[5]" },
                 { .function = "ConvertedWide", .arguments = "[65535]", .expected = "[65535]" },
                 { .function = "ConvertedWide", .arguments = "[65536]", .expected = "[65535]" },
                 { .function = "WrappedWide",
                   .arguments = "[18446744073709551615]",
                   .expected = "[65535]" },
                 { .function = "Truncated", .arguments = "[2.9]", .expected = "[2]" },
                 { .function = "Truncated", .arguments = "[-2.9]", .expected = "[0]" },
                 { .function = "Truncated", .arguments = "[1e300]", .expected = "[65535]" },
                 { .function = "Truncated", .arguments = "[-1e300]", .expected = "[0]" },
                 { .function = "NotANumber", .arguments = "[]", .expected = "[0]" },
                 { .function = "UnusedDiv", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,16]", .expected = "[1]" },
                 { .function = "UnusedConverted",
                   .arguments = "[-1]",
                   .expected = "[1]" }, } },
    { .name = "int",
      .source = SIGNED_SOURCE("int"),
      .calls = { { .function = "Add", .arguments = "[2147483647,1]", .expected = "[-2147483648]" },
                 { .function = "Add", .arguments = "[-2147483648,-1]", .expected = "[2147483647]" },
                 { .function = "Sub", .arguments = "[-2147483648,1]", .expected = "[2147483647]" },
                 { .function = "Sub", .arguments = "[2147483647,-1]", .expected = "[-2147483648]" },
                 { .function = "Mul", .arguments = "[2147483647,2]", .expected = "[-2]" },
                 { .function = "Mul", .arguments = "[-2147483648,-1]", .expected = "[-2147483648]" },
                 { .function = "Div", .arguments = "[7,-2]", .expected = "[-3]" },
                 { .function = "Div", .arguments = "[-7,2]", .expected = "[-3]" },
                 { .function = "Div", .arguments = "[-2147483648,1]", .expected = "[-2147483648]" },
                 { .function = "Div", .arguments = "[1,0]", .expected = "[0]" },
                 { .function = "Div", .arguments = "[-2147483648,-1]", .expected = "[-2147483648]" },
                 { .function = "Rem", .arguments = "[-7,2]", .expected = "[-1]" },
                 { .function = "Rem", .arguments = "[7,-2]", .expected = "[1]" },
                 { .function = "Rem", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Rem", .arguments = "[-2147483648,-1]", .expected = "[0]" },
                 { .function = "Neg", .arguments = "[-2147483648]", .expected = "[-2147483648]" },
                 { .function = "Neg", .arguments = "[2147483647]", .expected = "[-2147483647]" },
                 { .function = "Shl", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,31]", .expected = "[-2147483648]" },
                 { .function = "Shl", .arguments = "[2147483647,1]", .expected = "[-2]" },
                 { .function = "Shl", .arguments = "[1,32]", .expected = "[0]" },
                 { .function = "Shl", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Shr", .arguments = "[-2147483648,31]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[-1,1]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[-2147483648,32]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Less", .arguments = "[-2147483648,2147483647]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[-1,0]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[2147483647,-2147483648]", .expected = "[false]" },
                 { .function = "Converted", .arguments = "[2147483647]", .expected = "[2147483647]" },
                 { .function = "Converted", .arguments = "[2147483648]", .expected = "[2147483647]" },
                 { .function = "Converted", .arguments = "[-2147483649]", .expected = "[-2147483648]" },
                 { .function = "Wrapped", .arguments = "[2147483648]", .expected = "[-2147483648]" },
                 { .function = "Wrapped", .arguments = "[4294967301]", .expected = "[5]" },
                 { .function = "Wrapped", .arguments = "[-2147483649]", .expected = "[2147483647]" },
                 { .function = "ConvertedWide", .arguments = "[2147483647]", .expected = "[2147483647]" },
                 { .function = "ConvertedWide", .arguments = "[2147483648]", .expected = "[2147483647]" },
                 { .function = "WrappedWide",
                   .arguments = "[18446744073709551615]",
                   .expected = "[-1]" },
                 { .function = "WrappedWide", .arguments = "[2147483648]", .expected = "[-2147483648]" },
                 { .function = "Truncated", .arguments = "[2.9]", .expected = "[2]" },
                 { .function = "Truncated", .arguments = "[-2.9]", .expected = "[-2]" },
                 { .function = "Truncated", .arguments = "[1e300]", .expected = "[2147483647]" },
                 { .function = "Truncated", .arguments = "[-1e300]", .expected = "[-2147483648]" },
                 { .function = "NotANumber", .arguments = "[]", .expected = "[0]" },
                 { .function = "UnusedDiv", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "UnusedDiv",
                   .arguments = "[-2147483648,-1]",
                   .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,32]", .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,255]", .expected = "[1]" },
                 { .function = "UnusedConverted",
                   .arguments = "[2147483648]",
                   .expected = "[1]" }, } },
    { .name = "u32",
      .source = WIDTH_SOURCE("u32"),
      .calls = { { .function = "Add", .arguments = "[4294967295,1]", .expected = "[0]" },
                 { .function = "Sub", .arguments = "[0,1]", .expected = "[4294967295]" },
                 { .function = "Mul", .arguments = "[4294967295,2]", .expected = "[4294967294]" },
                 { .function = "Mul", .arguments = "[4294967295,4294967295]", .expected = "[1]" },
                 { .function = "Div", .arguments = "[4294967295,2]", .expected = "[2147483647]" },
                 { .function = "Div", .arguments = "[1,0]", .expected = "[0]" },
                 { .function = "Rem", .arguments = "[4294967295,10]", .expected = "[5]" },
                 { .function = "Rem", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,31]", .expected = "[2147483648]" },
                 { .function = "Shl", .arguments = "[4294967295,1]", .expected = "[4294967294]" },
                 { .function = "Shl", .arguments = "[1,32]", .expected = "[0]" },
                 { .function = "Shl", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Shr", .arguments = "[4294967295,31]", .expected = "[1]" },
                 { .function = "Shr", .arguments = "[2147483648,1]", .expected = "[1073741824]" },
                 { .function = "Shr", .arguments = "[4294967295,32]", .expected = "[0]" },
                 { .function = "Less", .arguments = "[0,4294967295]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[2147483648,1]", .expected = "[false]" },
                 { .function = "Less", .arguments = "[1,2147483648]", .expected = "[true]" },
                 { .function = "Converted", .arguments = "[-1]", .expected = "[0]" },
                 { .function = "Wrapped", .arguments = "[-1]", .expected = "[4294967295]" },
                 { .function = "Converted", .arguments = "[4294967295]", .expected = "[4294967295]" },
                 { .function = "Converted", .arguments = "[4294967296]", .expected = "[4294967295]" },
                 { .function = "Wrapped", .arguments = "[4294967296]", .expected = "[0]" },
                 { .function = "Wrapped", .arguments = "[4294967301]", .expected = "[5]" },
                 { .function = "ConvertedWide", .arguments = "[4294967295]", .expected = "[4294967295]" },
                 { .function = "ConvertedWide", .arguments = "[4294967296]", .expected = "[4294967295]" },
                 { .function = "WrappedWide",
                   .arguments = "[18446744073709551615]",
                   .expected = "[4294967295]" },
                 { .function = "Truncated", .arguments = "[2.9]", .expected = "[2]" },
                 { .function = "Truncated", .arguments = "[-2.9]", .expected = "[0]" },
                 { .function = "Truncated", .arguments = "[1e300]", .expected = "[4294967295]" },
                 { .function = "Truncated", .arguments = "[-1e300]", .expected = "[0]" },
                 { .function = "NotANumber", .arguments = "[]", .expected = "[0]" },
                 { .function = "UnusedDiv", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,32]", .expected = "[1]" },
                 { .function = "UnusedConverted",
                   .arguments = "[-1]",
                   .expected = "[1]" }, } },
    { .name = "i64",
      .source = SIGNED_SOURCE("i64"),
      .calls = { { .function = "Add",
                   .arguments = "[9223372036854775807,1]",
                   .expected = "[-9223372036854775808]" },
                 { .function = "Add",
                   .arguments = "[-9223372036854775808,-1]",
                   .expected = "[9223372036854775807]" },
                 { .function = "Sub",
                   .arguments = "[-9223372036854775808,1]",
                   .expected = "[9223372036854775807]" },
                 { .function = "Sub",
                   .arguments = "[9223372036854775807,-1]",
                   .expected = "[-9223372036854775808]" },
                 { .function = "Mul", .arguments = "[9223372036854775807,2]", .expected = "[-2]" },
                 { .function = "Mul",
                   .arguments = "[-9223372036854775808,-1]",
                   .expected = "[-9223372036854775808]" },
                 { .function = "Div", .arguments = "[7,-2]", .expected = "[-3]" },
                 { .function = "Div", .arguments = "[-7,2]", .expected = "[-3]" },
                 { .function = "Div",
                   .arguments = "[-9223372036854775808,1]",
                   .expected = "[-9223372036854775808]" },
                 { .function = "Div", .arguments = "[1,0]", .expected = "[0]" },
                 { .function = "Div",
                   .arguments = "[-9223372036854775808,-1]",
                   .expected = "[-9223372036854775808]" },
                 { .function = "Rem", .arguments = "[-7,2]", .expected = "[-1]" },
                 { .function = "Rem", .arguments = "[7,-2]", .expected = "[1]" },
                 { .function = "Rem", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Rem", .arguments = "[-9223372036854775808,-1]", .expected = "[0]" },
                 { .function = "Neg",
                   .arguments = "[-9223372036854775808]",
                   .expected = "[-9223372036854775808]" },
                 { .function = "Neg",
                   .arguments = "[9223372036854775807]",
                   .expected = "[-9223372036854775807]" },
                 { .function = "Shl", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,63]", .expected = "[-9223372036854775808]" },
                 { .function = "Shl", .arguments = "[9223372036854775807,1]", .expected = "[-2]" },
                 { .function = "Shl", .arguments = "[1,64]", .expected = "[0]" },
                 { .function = "Shl", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Shr", .arguments = "[-9223372036854775808,63]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[-1,1]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[-9223372036854775808,64]", .expected = "[-1]" },
                 { .function = "Shr", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Less",
                   .arguments = "[-9223372036854775808,9223372036854775807]",
                   .expected = "[true]" },
                 { .function = "Less", .arguments = "[-1,0]", .expected = "[true]" },
                 { .function = "Less",
                   .arguments = "[9223372036854775807,-9223372036854775808]",
                   .expected = "[false]" },
                 { .function = "Converted",
                   .arguments = "[-9223372036854775808]",
                   .expected = "[-9223372036854775808]" },
                 { .function = "Wrapped",
                   .arguments = "[-9223372036854775808]",
                   .expected = "[-9223372036854775808]" },
                 { .function = "ConvertedWide",
                   .arguments = "[9223372036854775807]",
                   .expected = "[9223372036854775807]" },
                 { .function = "ConvertedWide",
                   .arguments = "[9223372036854775808]",
                   .expected = "[9223372036854775807]" },
                 { .function = "WrappedWide",
                   .arguments = "[18446744073709551615]",
                   .expected = "[-1]" },
                 { .function = "WrappedWide",
                   .arguments = "[9223372036854775808]",
                   .expected = "[-9223372036854775808]" },
                 { .function = "Truncated", .arguments = "[2.9]", .expected = "[2]" },
                 { .function = "Truncated", .arguments = "[-2.9]", .expected = "[-2]" },
                 { .function = "Truncated", .arguments = "[1e300]", .expected = "[9223372036854775807]" },
                 { .function = "Truncated", .arguments = "[-1e300]", .expected = "[-9223372036854775808]" },
                 { .function = "NotANumber", .arguments = "[]", .expected = "[0]" },
                 { .function = "UnusedDiv", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "UnusedDiv",
                   .arguments = "[-9223372036854775808,-1]",
                   .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,64]", .expected = "[1]" },
                 { .function = "UnusedShl",
                   .arguments = "[1,255]",
                   .expected = "[1]" }, } },
    { .name = "u64",
      .source = WIDTH_SOURCE("u64"),
      .calls = { { .function = "Add", .arguments = "[18446744073709551615,1]", .expected = "[0]" },
                 { .function = "Sub", .arguments = "[0,1]", .expected = "[18446744073709551615]" },
                 { .function = "Mul",
                   .arguments = "[18446744073709551615,2]",
                   .expected = "[18446744073709551614]" },
                 { .function = "Mul",
                   .arguments = "[18446744073709551615,18446744073709551615]",
                   .expected = "[1]" },
                 { .function = "Div",
                   .arguments = "[18446744073709551615,2]",
                   .expected = "[9223372036854775807]" },
                 { .function = "Div", .arguments = "[1,0]", .expected = "[0]" },
                 { .function = "Rem", .arguments = "[18446744073709551615,10]", .expected = "[5]" },
                 { .function = "Rem", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "Shl", .arguments = "[1,63]", .expected = "[9223372036854775808]" },
                 { .function = "Shl",
                   .arguments = "[18446744073709551615,1]",
                   .expected = "[18446744073709551614]" },
                 { .function = "Shl", .arguments = "[1,64]", .expected = "[0]" },
                 { .function = "Shl", .arguments = "[1,255]", .expected = "[0]" },
                 { .function = "Shr", .arguments = "[18446744073709551615,63]", .expected = "[1]" },
                 { .function = "Shr",
                   .arguments = "[9223372036854775808,1]",
                   .expected = "[4611686018427387904]" },
                 { .function = "Shr", .arguments = "[18446744073709551615,64]", .expected = "[0]" },
                 { .function = "Less", .arguments = "[0,18446744073709551615]", .expected = "[true]" },
                 { .function = "Less", .arguments = "[9223372036854775808,1]", .expected = "[false]" },
                 { .function = "Less", .arguments = "[1,9223372036854775808]", .expected = "[true]" },
                 { .function = "Converted", .arguments = "[-1]", .expected = "[0]" },
                 { .function = "Wrapped", .arguments = "[-1]", .expected = "[18446744073709551615]" },
                 { .function = "Converted",
                   .arguments = "[9223372036854775807]",
                   .expected = "[9223372036854775807]" },
                 { .function = "Wrapped",
                   .arguments = "[-9223372036854775808]",
                   .expected = "[9223372036854775808]" },
                 { .function = "ConvertedWide",
                   .arguments = "[18446744073709551615]",
                   .expected = "[18446744073709551615]" },
                 { .function = "WrappedWide",
                   .arguments = "[18446744073709551615]",
                   .expected = "[18446744073709551615]" },
                 { .function = "Truncated", .arguments = "[2.9]", .expected = "[2]" },
                 { .function = "Truncated", .arguments = "[-2.9]", .expected = "[0]" },
                 { .function = "Truncated", .arguments = "[1e300]", .expected = "[18446744073709551615]" },
                 { .function = "Truncated", .arguments = "[-1e300]", .expected = "[0]" },
                 { .function = "NotANumber", .arguments = "[]", .expected = "[0]" },
                 { .function = "UnusedDiv", .arguments = "[1,0]", .expected = "[1]" },
                 { .function = "UnusedShl", .arguments = "[1,64]", .expected = "[1]" },
                 { .function = "UnusedConverted",
                   .arguments = "[-1]",
                   .expected = "[1]" }, } },
    { .name = "literal sum overflows its context",
      .source = "package facts\n"
                "F :: () -> i8 { return 127 + 1; }\n",
      .message = "Constant value is outside the range of i8",
      .line = 2,
      .column = 24 },
    { .name = "literal division by zero",
      .source = "package facts\n"
                "F :: () -> int { return 1 / 0; }\n",
      .message = "Constant division by zero",
      .line = 2,
      .column = 25 },
    { .name = "literal remainder by zero",
      .source = "package facts\n"
                "F :: () -> int { return 1 % 0; }\n",
      .message = "Constant division by zero",
      .line = 2,
      .column = 25 },
    { .name = "literal minimum by -1",
      .source = "package facts\n"
                "F :: () -> int { return -2147483648 / -1; }\n",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 25 },
    { .name = "literal shift past the width",
      .source = "package facts\n"
                "F :: () -> int { return 1 << 32; }\n",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 25 },
    { .name = "literal negative shift",
      .source = "package facts\n"
                "F :: () -> int { return 1 << -1; }\n",
      .message = "Shift count must not be negative",
      .line = 2,
      .column = 30 },
    { .name = "literal checked conversion",
      .source = "package facts\n"
                "F :: () -> u8 { return u8(300); }\n",
      .message = "Constant does not fit in u8; use u8(wrap: value)",
      .line = 2,
      .column = 24 },
    { .name = "literal wrap",
      .source = "package facts\n"
                "W :: () -> u8 { return u8(wrap: 300); }\n",
      .calls = { { .function = "W", .arguments = "[]", .expected = "[44]" } } },
    { .name = "constant sum overflows its type",
      .source = "package facts\n"
                "C: i8 : 127 + 1;\n"
                "F :: () -> i8 { return C; }\n",
      .message = "Constant value is outside the range of i8",
      .line = 2,
      .column = 9 },
    { .name = "constant division by zero",
      .source = "package facts\n"
                "C :: 1 / 0;\n"
                "F :: () -> int { return C; }\n",
      .message = "Constant division by zero",
      .line = 2,
      .column = 6 },
    { .name = "constant shift past the width",
      .source = "package facts\n"
                "C: int : 1 << 32;\n"
                "F :: () -> int { return C; }\n",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 10 },
    { .name = "constant arithmetic",
      .source = "package facts\n"
                "Q: int : -7 / 2;\n"
                "R: int : -7 % 2;\n"
                "S: int : -1 >> 1;\n"
                "F :: () -> i64 { return i64(Q) * 100 + i64(R) * 10 + "
                "i64(S); }\n",
      .calls = { { .function = "F", .arguments = "[]", .expected = "[-311]" } } },
    { .name = "constant minimum by -1",
      .source = "package facts\n"
                "M: int : -2147483648 / -1;\n"
                "F :: () -> int { return M; }\n",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 10 },
    { .name = "constant wrap",
      .source = "package facts\n"
                "C: u8 : u8(wrap: 300);\n"
                "F :: () -> u8 { return C; }\n",
      .calls = { { .function = "F", .arguments = "[]", .expected = "[44]" } } },
    { .name = "variable by literal zero",
      .source = "package facts\n"
                "F :: (a: int) -> int { return a / 0; }\n",
      .message = "Constant division by zero",
      .line = 2,
      .column = 31 },
    { .name = "variable shift by a literal past the width",
      .source = "package facts\n"
                "F :: (a: i8) -> i8 { return a << 8; }\n",
      .calls = { { .function = "F", .arguments = "[1]", .expected = "[0]" } } },
    { .name = "variable plus a literal outside its type",
      .source = "package facts\n"
                "F :: (a: i8) -> i8 { return a + 200; }\n",
      .message = "Numeric literal is outside the range of i8",
      .line = 2,
      .column = 33 },
    { .name = "unsigned negation",
      .source = "package facts\n"
                "F :: (a: u8) -> u8 { return -a; }\n",
      .calls = { { .function = "F", .arguments = "[1]", .expected = "[255]" } } },
    { .name = "unsigned compared with a negative literal",
      .source = "package facts\n"
                "F :: (a: u8) -> bool { return a < -1; }\n",
      .message = "Numeric literal is outside the range of u8",
      .line = 2,
      .column = 35 },
    { .name = "narrow sum widened after the operation",
      .source = "package facts\n"
                "F :: (a: i8, b: i8) -> int { return a + b; }\n"
                "G :: (a: u8, b: u8) -> int { return a * b; }\n"
                "H :: (a: i16) -> i64 { return a << 15; }\n"
                "U :: (a: u32) -> u64 { return a + 1; }\n",
      .calls = { { .function = "F", .arguments = "[127,1]", .expected = "[-128]" },
                 { .function = "G", .arguments = "[255,255]", .expected = "[1]" },
                 { .function = "H", .arguments = "[1]", .expected = "[-32768]" },
                 { .function = "U", .arguments = "[4294967295]", .expected = "[0]" } } },
    { .name = "mixed widths need a conversion",
      .source = "package facts\n"
                "F :: (a: i8, b: int) -> int { return a + b; }\n"
                "G :: (a: u8, b: i8) -> int { return a + b; }\n"
                "H :: (a: u32, b: int) -> i64 { return a + b; }\n"
                "L :: (a: u32, b: int) -> bool { return a < b; }\n"
                "S :: (a: u8, n: int) -> int { return a << n; }\n",
      .message = "Mismatched integer types i8 and int; convert one operand",
      .line = 2,
      .column = 38 },
    { .name = "mixed sum narrowed by its result",
      .source = "package facts\n"
                "G :: (a: u8, b: i8) -> i16 { return a + b; }\n",
      .message = "Mismatched integer types u8 and i8; convert one operand",
      .line = 2,
      .column = 37 },
    { .name = "signed and unsigned 64-bit mix",
      .source = "package facts\n"
                "F :: (a: u64, b: i64) -> u64 { return a + b; }\n",
      .message = "Mismatched integer types u64 and i64; convert one operand",
      .line = 2,
      .column = 39 },
    { .name = "compound assignment wraps",
      .source = "package facts\n"
                "F :: (a: i8) -> i8 { x := a; x += 1; x *= 2; return x; }\n"
                "G :: (a: u8) -> u8 { x := a; x -= 1; return x; }\n",
      .calls = { { .function = "F", .arguments = "[127]", .expected = "[0]" },
                 { .function = "G", .arguments = "[0]", .expected = "[255]" } } },
    { .name = "compound division by literal zero",
      .source = "package facts\n"
                "H :: (a: int) -> int { x := a; x /= 0; return x; }\n",
      .message = "Constant division by zero",
      .line = 2,
      .column = 32 },
    { .name = "bitwise operations keep the width",
      .source = "package facts\n"
                "N :: (a: u8) -> u8 { return ~a; }\n"
                "M :: (a: i8) -> i8 { return ~a; }\n"
                "X :: (a: u16, b: u16) -> u16 { return a ^ b; }\n",
      .calls = { { .function = "N", .arguments = "[0]", .expected = "[255]" },
                 { .function = "M", .arguments = "[0]", .expected = "[-1]" },
                 { .function = "X", .arguments = "[65535,1]", .expected = "[65534]" } } },
    { .name = "zero divisor and clamping in one expression",
      .source = "package facts\n"
                "F :: (a: int, b: int) -> int { return (a / b) + int(u8(a)); "
                "}\n"
                "G :: (a: int, b: int) -> int { return int(u8(a)) + (a / b); "
                "}\n",
      .calls = { { .function = "F", .arguments = "[300,0]", .expected = "[255]" },
                 { .function = "G", .arguments = "[300,0]", .expected = "[255]" } } },
    /* Each operand appends its digit to the trace, so the trace is the order
     * the operands ran in; the reversed spellings are the negative control. */
    { .name = "operands evaluate left to right",
      .source = "package facts\n"
                "Tag :: (t: ref int, d: int, v: int) -> int { t = t * 10 + d; "
                "return v; }\n"
                "Sum :: () -> int { t := 0; x := Tag(ref t, 1, 3) + Tag(ref t, 2, 4); "
                "return t; }\n"
                "Swapped :: () -> int { t := 0; x := Tag(ref t, 2, 4) + Tag(ref t, 1, "
                "3); return t; }\n"
                "Quotient :: () -> int { t := 0; x := Tag(ref t, 1, 7) / Tag(ref t, 2, "
                "0); return t; }\n"
                "Shifted :: () -> int { t := 0; x := Tag(ref t, 1, 1) << u32(Tag(ref t, "
                "2, 40)); return t; }\n"
                "Ordered :: () -> int { t := 0; b := Tag(ref t, 2, 5) < Tag(ref t, 1, 3); "
                "return t; }\n",
      .calls = { { .function = "Sum", .arguments = "[]", .expected = "[12]" },
                 { .function = "Swapped", .arguments = "[]", .expected = "[21]" },
                 { .function = "Quotient", .arguments = "[]", .expected = "[12]" },
                 { .function = "Shifted", .arguments = "[]", .expected = "[12]" },
                 { .function = "Ordered", .arguments = "[]", .expected = "[21]" } } },
};
enum
{
    SAMPLES = sizeof samples / sizeof *samples
};

static const char PATH[] = "facts/Main.chibi";
static native_capabilities *native;
static compiler_host *authored, *host;
static compiler_host_result result;
static void cleanup()
{
    native_capabilities_free(native);
    compiler_host_free(authored);
    compiler_host_free(host);
    native = NULL;
    authored = host = NULL;
    free(result.json);
    result = (compiler_host_result){ 0 };
}
static void setup_native()
{
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK };
    char error[256];
    native = native_capabilities_from_definition_packs(NULL, 0, packs, 2, error, sizeof error);
    assert(native);
}
static void setup()
{
    setup_native();
    char error[256];
    compiler_bootstrap options = {
        .folder = CHIBI_SOURCE_ROOT "/tools/compiler/bootstrap",
        .compiler = CHIBI_SOURCE_ROOT "/tools/compiler",
        .core = CHIBI_SOURCE_ROOT "/packages/core"
    };
    authored = compiler_bootstrap_load(&options, error, sizeof error);
    if (!authored) {
        fprintf(stderr, "integer semantics bootstrap: %s\n", error);
    }
}
/* A compiler's outcome for one sample: its closure, or its rejection. */
typedef struct
{
    unsigned char *closure;
    size_t length;
    char message[256];
    int line, column;
} compile_outcome;
/* Stage zero's closure of the sample's package, or its first diagnostic. */
static bool stage_zero(const integer_sample *s, compile_outcome *out)
{
    const source_package_input input = { PATH, s->source, strlen(s->source) };
    source_package_snapshot_options options = { .project_root = "/compiler",
                                                .files = &input,
                                                .file_count = 1 };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    REQUIRE(snapshot);
    const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
    if (error) {
        snprintf(out->message, sizeof out->message, "%s", error->diagnostic.message);
        out->line = error->diagnostic.range.line;
        out->column = error->diagnostic.range.column;
        source_package_snapshot_free(snapshot);
        return true;
    }
    script_lower_bindings bindings = authoring_native_bindings(native);
    authoring_packages *packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(native),
        &bindings,
        BEHAVIOR_BYTECODE,
        NULL
    );
    REQUIRE(packages && authoring_packages_count(packages) == 1);
    const authoring_package *package = authoring_packages_at(packages, 0);
    bool written = true;
    if (package->diagnostic_count) {
        const script_diagnostic *d = &package->diagnostics[0];
        snprintf(out->message, sizeof out->message, "%s", d->message);
        out->line = d->range.line;
        out->column = d->range.column;
    } else {
        unsigned char *prepared = NULL;
        size_t size = 0;
        uint64_t id = execution_module_id(package->module);
        written = execution_prepared_write(
            package->module,
            &prepared,
            &size,
            out->message,
            sizeof out->message
        );
        const package_module row = { package->identity, id, prepared, size };
        written = written && compiler_artifact_write(
                                 &row,
                                 1,
                                 id,
                                 &out->closure,
                                 &out->length,
                                 out->message,
                                 sizeof out->message
                             );
        free(prepared);
    }
    authoring_packages_free(packages);
    REQUIRE(written);
    return true;
}
/* Compile's arguments for the sample's one file, for the authored compiler's
 * own host. */
static char *compile_input(const integer_sample *s, size_t *length)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *arguments = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, arguments);
    yyjson_mut_arr_add_str(doc, arguments, "/project");
    yyjson_mut_arr_add_str(doc, yyjson_mut_arr_add_arr(doc, arguments), "core");
    yyjson_mut_arr_add_str(doc, yyjson_mut_arr_add_arr(doc, arguments), "/sdk/core");
    yyjson_mut_arr_add_str(doc, yyjson_mut_arr_add_arr(doc, arguments), PATH);
    yyjson_mut_arr_add_str(doc, yyjson_mut_arr_add_arr(doc, arguments), s->source);
    yyjson_mut_arr_add_false(doc, arguments);
    size_t target_length = 0;
    char *target = compiler_host_target_json(authored, &target_length);
    yyjson_doc *target_doc = yyjson_read(target, target_length, 0);
    yyjson_val *values = yyjson_doc_get_root(target_doc);
    for (size_t i = 0; i < yyjson_arr_size(values); i++) {
        yyjson_mut_arr_append(arguments, yyjson_val_mut_copy(doc, yyjson_arr_get(values, i)));
    }
    char *json = yyjson_mut_write(doc, 0, length);
    yyjson_doc_free(target_doc);
    free(target);
    yyjson_mut_doc_free(doc);
    return json;
}
/* Compile's rejection of the input: Rejected's diagnostic. */
static bool rejection(const char *json, size_t length, compile_outcome *out)
{
    REQUIRE(compiler_host_invoke(
        authored,
        compiler_host_entry("project:package_compiler:1:Compile:callable"),
        json,
        length,
        COMPILE_BUDGET,
        &result
    ));
    yyjson_doc *doc = yyjson_read(result.json, result.length, 0);
    yyjson_val *rejected = yyjson_arr_get(yyjson_doc_get_root(doc), 0);
    yyjson_val *diagnostic = yyjson_arr_get(yyjson_obj_get(rejected, "values"), 0);
    const char *message = yyjson_get_str(yyjson_obj_get(diagnostic, "message"));
    snprintf(out->message, sizeof out->message, "%s", message ? message : "");
    out->line = (int)yyjson_get_int(yyjson_obj_get(diagnostic, "line"));
    out->column = (int)yyjson_get_int(yyjson_obj_get(diagnostic, "column"));
    yyjson_doc_free(doc);
    free(result.json);
    result = (compiler_host_result){ 0 };
    REQUIRE(message);
    return true;
}
/* The authored compiler's closure of the sample's package, or its rejection. */
static bool authored_compile(const integer_sample *s, compile_outcome *out)
{
    REQUIRE(authored);
    size_t length = 0;
    char *input = compile_input(s, &length);
    REQUIRE(input);
    compiler_host_bytes bytes = { 0 };
    bool called = compiler_host_invoke_bytes(
        authored,
        compiler_host_entry("project:package_compiler:1:CompileClosure:callable"),
        input,
        length,
        COMPILE_BUDGET,
        COMPILER_HOST_MAX_OUTPUT_BYTES,
        &bytes
    );
    if (!called) {
        fprintf(stderr, "integer semantics %s authored: %s\n", s->name, bytes.diagnostic);
    }
    out->closure = bytes.bytes;
    out->length = bytes.length;
    bool valid = called && (bytes.length || rejection(input, length, out));
    free(input);
    REQUIRE(valid);
    return true;
}
static const char *const COMPILERS[] = { "stage zero", "authored" };
static const char *const BACKENDS[] = { "bytecode", "compiled" };
/* Whether the call gives its expected result from compiler `c`'s closure,
 * loaded in `host`. */
static bool call_agrees(const integer_sample *s, const integer_call *call, int c, int backend)
{
    char symbol[256];
    snprintf(symbol, sizeof symbol, "project:facts:1:%s:callable", call->function);
    bool valid = compiler_host_invoke(
        host,
        compiler_host_entry(symbol),
        call->arguments,
        strlen(call->arguments),
        CALL_BUDGET,
        &result
    );
    bool entered = !result.counts.overflow &&
                   !result.counts.inspection_bytecode_fallbacks &&
                   !result.counts.size_bytecode_fallbacks &&
                   (backend == 0 ? result.counts.bytecode_entries > 0 &&
                                       !result.counts.sljit_entries &&
                                       !result.counts.wasm_entries
                    : TEST_COMPILED_BACKEND == BEHAVIOR_SLJIT
                        ? result.counts.sljit_entries > 0 &&
                              !result.counts.bytecode_entries && !result.counts.wasm_entries
                        : result.counts.wasm_entries > 0 &&
                              !result.counts.bytecode_entries &&
                              !result.counts.sljit_entries);
    bool passed = valid && result.flow_status == FLOW_OK && entered;
    bool agrees = passed && !strcmp(result.json, call->expected);
    if (!agrees) {
        fprintf(
            stderr,
            "integer semantics %s %s%s, %s on %s: %s %s (status %d; expected "
            "%s)\n",
            s->name,
            call->function,
            call->arguments,
            COMPILERS[c],
            BACKENDS[backend],
            passed ? "gives" : "fails:",
            passed ? result.json : result.diagnostic,
            result.flow_status,
            call->expected
        );
    }
    free(result.json);
    result = (compiler_host_result){ 0 };
    return agrees;
}
/* Whether every call agrees on both backends. */
static bool runs_alike(const integer_sample *s, const compile_outcome *out, int c)
{
    bool agrees = true;
    for (int backend = 0; backend < 2; backend++) {
        char error[256];
        host = compiler_host_load(
            out->closure,
            out->length,
            backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
            error,
            sizeof error
        );
        if (!host) {
            fprintf(stderr, "integer semantics %s %s load: %s\n", s->name, COMPILERS[c], error);
        }
        REQUIRE(host);
        for (int i = 0; i < CALLS && s->calls[i].function; i++) {
            agrees = call_agrees(s, &s->calls[i], c, backend) && agrees;
        }
        compiler_host_free(host);
        host = NULL;
    }
    return agrees;
}
/* Whether compiler `c` rejects the sample as expected, or compiles it and
 * gives every call's result. */
static bool compiles_alike(const integer_sample *s, const compile_outcome *out, int c)
{
    const char *message = s->message;
    if (out->closure) {
        if (message) {
            fprintf(
                stderr,
                "integer semantics %s, %s: compiles (expected %s)\n",
                s->name,
                COMPILERS[c],
                message
            );
        }
        return runs_alike(s, out, c) && !message;
    }
    bool agrees = message && !strcmp(out->message, message) &&
                  out->line == s->line && out->column == s->column;
    if (!agrees) {
        fprintf(
            stderr,
            "integer semantics %s, %s: rejects %s at %d:%d (expected %s at "
            "%d:%d)\n",
            s->name,
            COMPILERS[c],
            out->message,
            out->line,
            out->column,
            message ? message : "a closure",
            s->line,
            s->column
        );
    }
    return agrees;
}
static bool sample_agrees(const integer_sample *s)
{
    compile_outcome outs[2] = { 0 };
    bool compiled = stage_zero(s, &outs[0]) && authored_compile(s, &outs[1]);
    bool agrees = compiled;
    for (int c = 0; compiled && c < 2; c++) {
        agrees = compiles_alike(s, &outs[c], c) && agrees;
    }
    free(outs[0].closure);
    free(outs[1].closure);
    return agrees;
}
TEST_CASE(integer_semantics_pin_every_width)
{
    bool agrees = true;
    for (int i = 0; i < (int)SAMPLES; i++) {
        agrees = sample_agrees(&samples[i]) && agrees;
    }
    REQUIRE(agrees);
    return true;
}
void test_language_integer_semantics()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(integer_semantics_pin_every_width);
    pu_clear_setup();
}

/* Stage-zero regressions use the production package/closure path above and
 * literal results on both backends. The preserved authored seed is a separate
 * compiler and is not evidence for these analyzer fixes. Optional selection
 * isolates assertion regressions without hiding the other original failures. */
static const integer_sample shift_samples[] = {
    { .name = "exact-counts",
      .source = "package facts\nF :: () -> i8 { return (i8(1) << 200) >> "
                "199; }",
      .calls = { { "F", "[]", "[2]" } } },
    { .name = "contextual-counts",
      .source = "package facts\nF :: () -> i8 { return (1 << 200) >> 199; }",
      .calls = { { "F", "[]", "[2]" } } },
    { .name = "dynamic-left",
      .source = "package facts\nF :: (a: i8) -> i8 { return a << "
                "((9223372036854775807 + 1) - 9223372036854775807); }",
      .calls = { { "F", "[1]", "[2]" } } },
    { .name = "negative-below-int64",
      .source = "package facts\nF :: () -> i8 { return i8(1) << (0 - "
                "9223372036854775807 - 2); }",
      .message = "Shift count must not be negative",
      .line = 2,
      .column = 34 },
    { .name = "dynamic-negative-below-int64",
      .source = "package facts\nF :: (a: i8) -> i8 { return a << (0 - "
                "9223372036854775807 - 2); }",
      .message = "Constant value is outside the range of i64",
      .line = 2,
      .column = 35 },
    { .name = "left-limit",
      .source = "package facts\nF :: () -> i8 { return i8(0) << 4097; }",
      .message = "Constant shift count is too large",
      .line = 2,
      .column = 33 },
    { .name = "right-limit",
      .source = "package facts\nF :: () -> i8 { return i8(1) >> 4097; }",
      .message = "Constant shift count is too large",
      .line = 2,
      .column = 33 },
    { .name = "limit-inclusive",
      .source = "package facts\nF :: () -> i8 { return (i8(0) << 4096) + "
                "(i8(1) >> 4096); }",
      .calls = { { "F", "[]", "[0]" } } },
    { .name = "runtime-counts",
      .source = "package facts\n"
                "F :: (a: i8) -> i8 { return a << 200; }\n"
                "G :: (a: i8, n: u64) -> i8 { return a << n; }\n"
                "H :: (a: i8) -> i8 { return a << 5000; }\n",
      .calls = { { "F", "[1]", "[0]" }, { "G", "[1,200]", "[0]" }, { "H", "[1]", "[0]" } } },
};
static const integer_sample *shift_sample;
TEST_CASE(integer_shift_count)
{
    compile_outcome out = { 0 };
    bool compiled = stage_zero(shift_sample, &out);
    bool agrees = compiled && compiles_alike(shift_sample, &out, 0);
    free(out.closure);
    REQUIRE(agrees);
    return true;
}
void test_language_integer_shift_counts(const char *name)
{
    pu_setup(setup_native, cleanup);
    bool found = false;
    for (size_t i = 0; i < sizeof shift_samples / sizeof *shift_samples; i++) {
        if (name && strcmp(name, shift_samples[i].name)) {
            continue;
        }
        found = true;
        shift_sample = &shift_samples[i];
        pu_run_test(shift_sample->name, integer_shift_count);
    }
    if (!found) {
        fprintf(stderr, "Unknown integer shift case: %s\n", name);
        abort();
    }
    pu_clear_setup();
}

/* The same literal and located-diagnostic oracles also constrain the authored
 * compiler. Its emitted closures must enter each requested backend. */
TEST_CASE(integer_shift_count_authored)
{
    REQUIRE(sample_agrees(shift_sample));
    return true;
}
void test_language_integer_shift_counts_authored()
{
    pu_setup(setup, cleanup);
    for (size_t i = 0; i < sizeof shift_samples / sizeof *shift_samples; i++) {
        shift_sample = &shift_samples[i];
        pu_run_test(shift_sample->name, integer_shift_count_authored);
    }
    pu_clear_setup();
}
