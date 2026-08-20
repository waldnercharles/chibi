/* Vendored from the GeometryWars fork of stclib/STC c11/fmt.h.
 *   fork:    https://github.com/stclib/STC (include/c11/fmt.h)
 *   vendored from: GeometryWars include/fmt.h + src/fmt.c
 * MIT License, (c) operamint 2022-2026. The fork splits the original
 * single header into a declaration header and fmt.c, adds positional
 * {N} placeholders, and switches the scratch buffer to a C23 VLA.
 * LOCAL FIXES: the fork's char* destination mapped to printf, which
 * writes to stdout instead of the buffer; restored to sprintf per
 * upstream (see fmt.h). _fmt_sprint crashed on an empty result: no
 * buffer was allocated, then vsnprintf wrote to NULL. char*
 * destinations are unbounded sprintf; prefer fmt_stream (bounded,
 * vsnprintf-based), which is what core uses.
 */

#include "fmt.h"

#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

void fmt_close(fmt_stream *ss) { free(ss->data); }

const char *_fmt_time(const char *fmt, const struct tm *tm, char *buf, int bufsize)
{
    strftime(buf, (size_t)bufsize, fmt, tm);
    return buf;
}

void _fmt_sprint(fmt_stream *ss, const char *fmt, ...)
{
    va_list args, args2;
    va_start(args, fmt);
    if (ss == NULL) {
        vprintf(fmt, args);
        putchar('\n');
        goto done1;
    }
    va_copy(args2, args);
    const int n = vsnprintf(NULL, 0U, fmt, args);
    if (n < 0) goto done2;
    const ptrdiff_t pos = ss->overwrite ? 0 : ss->len;
    ss->len = pos + n;
    /* Keep at least one byte: an empty result (n == 0) must still get a
     * buffer for the terminating NUL, otherwise vsnprintf writes to NULL. */
    if (ss->len + 1 > ss->cap) {
        ss->cap = ss->len + ss->cap / 2;
        if (ss->cap < ss->len + 1) ss->cap = ss->len + 1;
        ss->data = (char *)realloc(ss->data, (size_t)ss->cap + 1U);
    }
    vsnprintf(ss->data + pos, (size_t)n + 1, fmt, args2);
done2:
    va_end(args2);
done1:
    va_end(args);
}

int _fmt_parse(char *p, int nargs, const char *fmt, const char **specifiers)
{
    char *arg, *p0, ch;
    int n = 0, empty, id, seq_count = 0;
    do {
        switch ((ch = *fmt)) {
            case '%': *p++ = '%'; break;
            case '}':
                if (*++fmt == '}') break; /* ok */
                n = 99;
                continue;
            case '{':
                if (*++fmt == '{') break; /* ok */

                /* Parse positional argument ID if present */
                id = -1;
                if (*fmt >= '0' && *fmt <= '9') {
                    id = 0;
                    while (*fmt >= '0' && *fmt <= '9') {
                        id = id * 10 + (*fmt - '0');
                        fmt++;
                    }
                }

                /* Determine argument index and validate bounds */
                int arg_idx;
                if (id >= 0) {
                    arg_idx = id;
                    if (arg_idx >= nargs) {
                        n = 99;
                        continue;
                    }
                } else {
                    arg_idx = seq_count++;
                    if (arg_idx >= nargs) continue;
                }

                ++n; /* Count valid placeholders */
                if (*fmt != ':' && *fmt != '}') n = 99;
                fmt += (*fmt == ':');
                empty = *fmt == '}';

                arg = (char *)specifiers[arg_idx];
                *p++ = '%';

                /* Add positional parameter if ID specified */
                if (id >= 0) {
                    p += sprintf(p, "%d$", id + 1); /* printf uses 1-based indexing */
                }

                p0 = p;
                while (1) {
                    switch (*fmt) {
                        case '\0': n = 99; /* FALLTHRU */
                        case '}': goto done;
                        case '<': *p++ = '-', ++fmt; break;
                        case '>': p0 = NULL; /* FALLTHRU */
                        case '-': ++fmt; break;
                        case '*':
                            if (++n <= nargs)
                                arg = (char *)specifiers[n - 1]; /* FALLTHRU */
                        default: *p++ = *fmt++;
                    }
                }
            done:
                switch (*arg) {
                    case 'g':
                        if (empty) memcpy(p, ".8", 2), p += 2;
                        break;
                    case '@':
                        ++arg;
                        if (empty) memcpy(p, ".16", 3), p += 3;
                        break;
                }
                if (strchr("csdioxXufFeEaAgGnp", fmt[-1]) == NULL)
                    while (*arg) *p++ = *arg++;
                if (p0 && (p[-1] == 's' || p[-1] == 'c')) /* left-align str */
                    memmove(p0 + 1, p0, (size_t)(p++ - p0)), *p0 = '-';
                fmt += *fmt == '}';
                continue;
        }
        *p++ = *fmt++;
    } while (ch);
    return n;
}
