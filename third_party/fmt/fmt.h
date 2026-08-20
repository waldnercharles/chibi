/* Vendored from the GeometryWars fork of stclib/STC c11/fmt.h.
 *   fork:    https://github.com/stclib/STC (include/c11/fmt.h)
 *   vendored from: GeometryWars include/fmt.h + src/fmt.c
 * MIT License, (c) operamint 2022-2026. The fork splits the original
 * single header into a declaration header and fmt.c, adds positional
 * {N} placeholders, and switches the scratch buffer to a C23 VLA.
 * LOCAL FIX: the fork's char* destination mapped to printf, which writes
 * to stdout instead of the buffer; restored to sprintf per upstream.
 * Note that char* destinations are unbounded sprintf; prefer fmt_stream
 * (bounded, vsnprintf-based), which is what core uses.
 */

#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#if defined _WIN32
#include <malloc.h>
#elif defined __APPLE__
#include <stdlib.h>
#else
#include <alloca.h>
#endif
#define fmt_OVERLOAD(name, ...)                                                \
    fmt_JOIN(name##_, fmt_NUMARGS(__VA_ARGS__))(__VA_ARGS__)
#define fmt_JOIN(a, b) _fmt_JOIN0(a, b)
#define fmt_NUMARGS(...) _fmt_APPLY_ARG_N((__VA_ARGS__, _fmt_RSEQ_N))
#define _fmt_JOIN0(a, b) a##b
#define _fmt_APPLY_ARG_N(args) _fmt_ARG_N args
#define _fmt_RSEQ_N 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
#define _fmt_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) \
    N

#if defined FMT_NDEBUG || defined STC_NDEBUG || defined NDEBUG
#define fmt_OK(exp) (void)(exp)
#else
#define fmt_OK(exp) assert(exp)
#endif

typedef struct
{
    char *data;
    ptrdiff_t cap, len;
    _Bool overwrite;
} fmt_stream;

struct tm;
const char *_fmt_time(const char *fmt, const struct tm *tm, char *buf, int bufsize);
void fmt_close(fmt_stream *ss);
int _fmt_parse(char *p, int nargs, const char *fmt, const char **specifiers);
void _fmt_sprint(fmt_stream *, const char *fmt, ...);

#define _fmt_init(_fs, _fmt, fmt, nargs)                                       \
    const char *_fmt = fmt;                                                    \
    char _fs[strlen(_fmt) + nargs * 8lu + 1lu] // "{}" => "%99$@Lg" => +8 max

/* FOREACH macro infrastructure for applying _fc to arguments */
#define _fmt_FOREACH_0(fn)
#define _fmt_FOREACH_1(fn, a) fn(a)
#define _fmt_FOREACH_2(fn, a, ...) fn(a), _fmt_FOREACH_1(fn, __VA_ARGS__)
#define _fmt_FOREACH_3(fn, a, ...) fn(a), _fmt_FOREACH_2(fn, __VA_ARGS__)
#define _fmt_FOREACH_4(fn, a, ...) fn(a), _fmt_FOREACH_3(fn, __VA_ARGS__)
#define _fmt_FOREACH_5(fn, a, ...) fn(a), _fmt_FOREACH_4(fn, __VA_ARGS__)
#define _fmt_FOREACH_6(fn, a, ...) fn(a), _fmt_FOREACH_5(fn, __VA_ARGS__)
#define _fmt_FOREACH_7(fn, a, ...) fn(a), _fmt_FOREACH_6(fn, __VA_ARGS__)
#define _fmt_FOREACH_8(fn, a, ...) fn(a), _fmt_FOREACH_7(fn, __VA_ARGS__)
#define _fmt_FOREACH_9(fn, a, ...) fn(a), _fmt_FOREACH_8(fn, __VA_ARGS__)
#define _fmt_FOREACH_10(fn, a, ...) fn(a), _fmt_FOREACH_9(fn, __VA_ARGS__)
#define _fmt_FOREACH_11(fn, a, ...) fn(a), _fmt_FOREACH_10(fn, __VA_ARGS__)
#define _fmt_FOREACH_12(fn, a, ...) fn(a), _fmt_FOREACH_11(fn, __VA_ARGS__)
#define _fmt_FOREACH_13(fn, a, ...) fn(a), _fmt_FOREACH_12(fn, __VA_ARGS__)
#define _fmt_FOREACH_14(fn, a, ...) fn(a), _fmt_FOREACH_13(fn, __VA_ARGS__)

#define _fmt_FOREACH__(fn, n, ...) _fmt_FOREACH_##n(fn, __VA_ARGS__)
#define _fmt_FOREACH_(fn, n, ...) _fmt_FOREACH__(fn, n, __VA_ARGS__)
#define _fmt_FOREACH(fn, ...)                                                  \
    _fmt_FOREACH_(fn, fmt_NUMARGS(__VA_ARGS__), __VA_ARGS__)

/* Count variadic arguments minus 2 (for 'to' and 'fmt') */
#define _fmt_NARGS_OFFSET(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) \
    N
#define _fmt_NARGS(...)                                                        \
    _fmt_NARGS_OFFSET(__VA_ARGS__, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define fmt_print(...) fmt_printd(stdout, __VA_ARGS__)
#define fmt_println(...) fmt_printd((fmt_stream *)0, __VA_ARGS__)
#define fmt_time(fmt, tm, MAXLEN)                                              \
    _fmt_time(fmt, tm, (char[MAXLEN + 1]){ 0 }, MAXLEN + 1)

#define fmt_sv "{:.*s}"
#define fmt_svarg(sv) (int)(sv).size, (sv).buf

/* Unified fmt_printd implementation using FOREACH */
#define fmt_printd(to, fmt, ...)                                                                \
    do {                                                                                        \
        _fmt_init(_fs, _fmt, fmt, _fmt_NARGS(to, fmt __VA_OPT__(, ) __VA_ARGS__));              \
        const char *_specs[] = { __VA_OPT__(_fmt_FOREACH(_fc, __VA_ARGS__), ) NULL };           \
        int _n = _fmt_parse(_fs, _fmt_NARGS(to, fmt __VA_OPT__(, ) __VA_ARGS__), _fmt, _specs); \
        fmt_OK(_n < 99); /* Check for parse errors */                                           \
        _fmt_fn(to)(to, _fs __VA_OPT__(, ) __VA_ARGS__);                                        \
    } while (0)

#define _fmt_fn(x)                                                               \
    _Pragma("clang diagnostic push") _Pragma(                                    \
        "clang diagnostic ignored \"-Wdeprecated-declarations\""                 \
    ) _Generic((x), FILE *: fprintf, char *: sprintf, fmt_stream *: _fmt_sprint) \
        _Pragma("clang diagnostic pop")

#if defined(_MSC_VER) && !defined(__clang__)
#define _signed_char_hhd
#else
#define _signed_char_hhd                                                       \
    signed char                                                                \
      : "c"                                                                    \
      ,
#endif

#define _fc(x)                                                                 \
    _Generic(                                                                  \
        x,                                                                     \
        _Bool: "d",                                                            \
        unsigned char: "hhu",                                                  \
        _signed_char_hhd char: "c",                                            \
        short: "hd",                                                           \
        unsigned short: "hu",                                                  \
        int: "d",                                                              \
        unsigned: "u",                                                         \
        long: "ld",                                                            \
        unsigned long: "lu",                                                   \
        long long: "lld",                                                      \
        unsigned long long: "llu",                                             \
        float: "g",                                                            \
        double: "@g",                                                          \
        long double: "@Lg",                                                    \
        char *: "s",                                                           \
        wchar_t *: "ls",                                                       \
        void *: "p",                                                           \
        const char *: "s",                                                     \
        const wchar_t *: "ls",                                                 \
        const void *: "p"                                                      \
    )
