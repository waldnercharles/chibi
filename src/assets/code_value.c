#include "assets/code_value.h"

#include "support/code_lex.h"
#include "support/float_text.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODE_VALUE_MAX_DEPTH 256

typedef struct
{
    code_lexer lexer;
    code_token token;
    av_doc *doc;
    char *error;
    size_t error_size;
    bool failed;
} reader;

static void fail(reader *r, const char *message)
{
    if (!r->failed && r->error && r->error_size) {
        snprintf(
            r->error,
            r->error_size,
            "line %d, column %d: %s",
            r->token.line,
            r->token.column,
            message
        );
    }
    r->failed = true;
}
static void next(reader *r)
{
    r->token = code_lex_next(&r->lexer);
    if (r->token.kind == CODE_TOKEN_ERROR) {
        fail(r, "unterminated string or comment");
    }
}
static bool punctuation(reader *r, char c)
{
    return r->token.kind == CODE_TOKEN_PUNCTUATION && r->token.punctuation == c;
}
static bool consume(reader *r, char c)
{
    if (!punctuation(r, c)) {
        char message[40];
        snprintf(message, sizeof message, "expected '%c'", c);
        fail(r, message);
        return false;
    }
    next(r);
    return !r->failed;
}
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}
static bool unicode(reader *r, size_t *at, size_t end, unsigned *value)
{
    bool braces = *at < end && r->lexer.text[*at] == '{';
    if (braces) {
        (*at)++;
    }
    int digits = 0;
    *value = 0;
    while (*at < end && (braces ? r->lexer.text[*at] != '}' : digits < 4)) {
        int digit = hex_digit(r->lexer.text[(*at)++]);
        if (digit < 0 || digits++ >= 6) {
            return false;
        }
        *value = *value * 16 + (unsigned)digit;
    }
    if (braces) {
        if (!digits || *at >= end || r->lexer.text[(*at)++] != '}') {
            return false;
        }
    } else if (digits != 4) {
        return false;
    }
    return *value <= 0x10ffff;
}
static char *string(reader *r, size_t *length)
{
    code_token token = r->token;
    char *out = xmalloc(token.end - token.start + 1);
    size_t used = 0;
    for (size_t at = token.start + 1; at < token.end - 1;) {
        unsigned char c = (unsigned char)r->lexer.text[at++];
        if (c < 32) {
            fail(r, "unescaped control character");
            break;
        }
        if (c != '\\') {
            out[used++] = (char)c;
            continue;
        }
        if (at >= token.end - 1) {
            fail(r, "incomplete escape");
            break;
        }
        c = (unsigned char)r->lexer.text[at++];
        switch (c) {
            case '"':
            case '\\':
            case '/': out[used++] = (char)c; break;
            case 'n': out[used++] = '\n'; break;
            case 'r': out[used++] = '\r'; break;
            case 't': out[used++] = '\t'; break;
            case 'b': out[used++] = '\b'; break;
            case 'f': out[used++] = '\f'; break;
            case 'u': {
                unsigned scalar;
                if (!unicode(r, &at, token.end - 1, &scalar)) {
                    fail(r, "invalid Unicode escape");
                    break;
                }
                if (scalar >= 0xd800 && scalar <= 0xdbff) {
                    unsigned low;
                    if (at + 2 > token.end - 1 || r->lexer.text[at] != '\\' ||
                        r->lexer.text[at + 1] != 'u') {
                        fail(r, "unpaired Unicode surrogate");
                        break;
                    }
                    at += 2;
                    if (!unicode(r, &at, token.end - 1, &low) || low < 0xdc00 ||
                        low > 0xdfff) {
                        fail(r, "unpaired Unicode surrogate");
                        break;
                    }
                    scalar = 0x10000 + (scalar - 0xd800) * 1024 + low - 0xdc00;
                }
                if (scalar >= 0xdc00 && scalar <= 0xdfff) {
                    fail(r, "unpaired Unicode surrogate");
                    break;
                }
                if (scalar < 0x80) {
                    out[used++] = (char)scalar;
                } else if (scalar < 0x800) {
                    out[used++] = (char)(0xc0 | (scalar >> 6));
                    out[used++] = (char)(0x80 | (scalar & 63));
                } else if (scalar < 0x10000) {
                    out[used++] = (char)(0xe0 | (scalar >> 12));
                    out[used++] = (char)(0x80 | ((scalar >> 6) & 63));
                    out[used++] = (char)(0x80 | (scalar & 63));
                } else {
                    out[used++] = (char)(0xf0 | (scalar >> 18));
                    out[used++] = (char)(0x80 | ((scalar >> 12) & 63));
                    out[used++] = (char)(0x80 | ((scalar >> 6) & 63));
                    out[used++] = (char)(0x80 | (scalar & 63));
                }
                break;
            }
            default: fail(r, "invalid string escape"); break;
        }
        if (r->failed) {
            break;
        }
    }
    out[used] = 0;
    if (r->failed) {
        free(out);
        return NULL;
    }
    if (length) {
        *length = used;
    }
    next(r);
    return out;
}
static av_value *number(reader *r)
{
    code_token token = r->token;
    char *literal = xmalloc(token.end - token.start + 1);
    size_t used = 0;
    for (size_t i = token.start; i < token.end; i++) {
        if (r->lexer.text[i] != '_') {
            literal[used++] = r->lexer.text[i];
        }
    }
    literal[used] = 0;
    const char *digits = literal + (literal[0] == '+' || literal[0] == '-');
    int base = 10;
    bool based = digits[0] == '0' && digits[1] && strchr("xob", digits[1]);
    if (based) {
        base = digits[1] == 'x' ? 16 : digits[1] == 'o' ? 8 : 2;
        digits += 2;
    }
    char *end;
    av_value *value = NULL;
    errno = 0;
    if (!based && (strchr(digits, '.') || strchr(digits, 'e') || strchr(digits, 'E'))) {
        double real = strtod(literal, &end);
        if (*literal && !*end && isfinite(real) && (errno != ERANGE || real != 0)) {
            value = av_real(r->doc, real);
        }
    } else {
        uint64_t magnitude = strtoull(digits, &end, base);
        bool valid = *digits && !*end && errno != ERANGE;
        if (valid && literal[0] == '-') {
            if (magnitude <= (uint64_t)INT64_MAX + 1) {
                value = av_sint(
                    r->doc,
                    magnitude == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)magnitude
                );
            }
        } else if (valid) {
            value = base == 16 ? (strlen(digits) == 8 && magnitude <= UINT32_MAX
                                      ? av_color(r->doc, (uint32_t)magnitude)
                                      : av_hex(r->doc, magnitude))
                               : av_uint(r->doc, magnitude);
        }
    }
    free(literal);
    if (!value) {
        fail(r, "invalid or out-of-range number");
        return NULL;
    }
    next(r);
    return value;
}
static av_value *read_value(reader *r, int depth)
{
    if (depth > CODE_VALUE_MAX_DEPTH) {
        fail(r, "Literal nesting is too deep");
        return NULL;
    }
    if (r->failed) {
        return NULL;
    }
    code_token begin = r->token;
    av_value *value = NULL;
    size_t finish = begin.end;
    if (punctuation(r, '{')) {
        value = av_obj(r->doc);
        next(r);
        while (!r->failed && !punctuation(r, '}')) {
            code_token key = r->token;
            char *name = NULL;
            if (key.kind == CODE_TOKEN_STRING) {
                size_t length;
                name = string(r, &length);
                if (name && strlen(name) != length) {
                    fail(r, "field name contains a null byte");
                }
            } else if (key.kind == CODE_TOKEN_NAME) {
                size_t end = key.end;
                next(r);
                while (!r->failed && (punctuation(r, '.') || punctuation(r, '-'))) {
                    next(r);
                    if (r->token.kind != CODE_TOKEN_NAME) {
                        fail(r, "expected qualified field name");
                        break;
                    }
                    end = r->token.end;
                    next(r);
                }
                name = xmalloc(end - key.start + 1);
                memcpy(name, r->lexer.text + key.start, end - key.start);
                name[end - key.start] = 0;
                key.end = end;
            } else {
                fail(r, "expected field name");
            }
            if (!name || r->failed) {
                free(name);
                break;
            }
            if (av_obj_get(value, name)) {
                free(name);
                fail(r, "duplicate field name");
                break;
            }
            if (!consume(r, ':')) {
                free(name);
                break;
            }
            av_value *child = read_value(r, depth + 1);
            if (!child) {
                free(name);
                break;
            }
            av_source range = av_get_source(child);
            range.name_start = key.start;
            range.name_end = key.end;
            av_set_source(child, range);
            av_obj_add_val(r->doc, value, name, child);
            free(name);
            if (punctuation(r, '}')) {
                break;
            }
            if (!consume(r, ',')) {
                break;
            }
        }
        finish = r->token.end;
        consume(r, '}');
    } else if (punctuation(r, '[')) {
        value = av_arr(r->doc);
        next(r);
        while (!r->failed && !punctuation(r, ']')) {
            av_value *child = read_value(r, depth + 1);
            if (!child) {
                break;
            }
            av_arr_append(value, child);
            if (punctuation(r, ']')) {
                break;
            }
            if (!consume(r, ',')) {
                break;
            }
        }
        finish = r->token.end;
        consume(r, ']');
    } else if (begin.kind == CODE_TOKEN_STRING) {
        size_t length;
        char *decoded = string(r, &length);
        if (decoded) {
            value = av_strn(r->doc, decoded, length);
            free(decoded);
        }
    } else if (begin.kind == CODE_TOKEN_NUMBER) {
        value = number(r);
    } else if (code_token_is(&r->lexer, begin, "true")) {
        value = av_bool(r->doc, true);
        next(r);
    } else if (code_token_is(&r->lexer, begin, "false")) {
        value = av_bool(r->doc, false);
        next(r);
    } else if (code_token_is(&r->lexer, begin, "null")) {
        value = av_null(r->doc);
        next(r);
    } else {
        fail(r, "Expected a literal value");
    }
    if (r->failed || !value) {
        return NULL;
    }
    av_set_source(value, (av_source){ begin.start, finish, SIZE_MAX, SIZE_MAX });
    return value;
}
av_doc *code_value_read(const char *text, char *error, size_t size)
{
    if (error && size) {
        error[0] = 0;
    }
    reader r = { .doc = av_doc_new(), .error = error, .error_size = size };
    code_lex_init(&r.lexer, text);
    next(&r);
    av_value *value = read_value(&r, 0);
    if (!r.failed && r.token.kind != CODE_TOKEN_END) {
        fail(&r, "Unexpected trailing literal text");
    }
    if (r.failed || !value) {
        av_doc_free(r.doc);
        return NULL;
    }
    av_doc_set_root(r.doc, value);
    return r.doc;
}

typedef struct
{
    char *data;
    size_t count, capacity;
    bool invalid;
} text_buffer;
static void write_text(text_buffer *b, const char *text, size_t size)
{
    if (b->count + size + 1 > b->capacity) {
        b->capacity = (b->count + size + 1) * 2;
        b->data = xrealloc(b->data, b->capacity);
    }
    memcpy(b->data + b->count, text, size);
    b->count += size;
    b->data[b->count] = 0;
}
static void word(text_buffer *b, const char *text)
{
    write_text(b, text, strlen(text));
}
static void write_string(text_buffer *b, const char *text, size_t size)
{
    word(b, "\"");
    for (size_t i = 0; i < size; i++) {
        unsigned char c = (unsigned char)text[i];
        switch (c) {
            case '"': word(b, "\\\""); break;
            case '\\': word(b, "\\\\"); break;
            case '\n': word(b, "\\n"); break;
            case '\r': word(b, "\\r"); break;
            case '\t': word(b, "\\t"); break;
            case '\b': word(b, "\\b"); break;
            case '\f': word(b, "\\f"); break;
            default:
                if (c < 32) {
                    char escaped[7];
                    snprintf(escaped, sizeof escaped, "\\u%04x", c);
                    word(b, escaped);
                } else {
                    write_text(b, text + i, 1);
                }
                break;
        }
    }
    word(b, "\"");
}
static void indent(text_buffer *b, int depth)
{
    for (int i = 0; i < depth; i++) {
        word(b, "    ");
    }
}
static void write_key(text_buffer *b, const char *key)
{
    bool bare = isalpha((unsigned char)*key) || *key == '_';
    for (const char *p = key; *p; p++) {
        bare &= isalnum((unsigned char)*p) || *p == '_';
    }
    if (bare) {
        word(b, key);
        return;
    }
    write_string(b, key, strlen(key));
}
static void write_value(text_buffer *b, const av_value *value, int depth)
{
    if (depth > CODE_VALUE_MAX_DEPTH) {
        b->invalid = true;
        return;
    }
    char number_text[64];
    switch (av_get_type(value)) {
        case AV_NULL: word(b, "null"); return;
        case AV_BOOL: word(b, av_get_bool(value) ? "true" : "false"); return;
        case AV_SINT:
            snprintf(number_text, sizeof number_text, "%" PRId64, av_get_sint(value));
            break;
        case AV_UINT:
            if (av_is_color(value)) {
                snprintf(number_text, sizeof number_text, "0x%08" PRIx64, av_get_uint(value));
            } else if (av_is_hex(value)) {
                snprintf(number_text, sizeof number_text, "0x%" PRIx64, av_get_uint(value));
            } else {
                snprintf(number_text, sizeof number_text, "%" PRIu64, av_get_uint(value));
            }
            break;
        case AV_REAL:
            if (!isfinite(av_get_num(value))) {
                b->invalid = true;
                return;
            }
            float_text(number_text, sizeof number_text, av_get_num(value), false);
            if (!strchr(number_text, '.') && !strchr(number_text, 'e')) {
                strncat(number_text, ".0", sizeof number_text - strlen(number_text) - 1);
            }
            break;
        case AV_STRING:
            write_string(b, av_get_str(value), av_get_len(value));
            return;
        case AV_OBJECT:
            word(b, "{");
            if (av_obj_size(value)) {
                word(b, "\n");
            }
            for (size_t i = 0; i < av_obj_size(value); i++) {
                indent(b, depth + 1);
                write_key(b, av_get_str(av_obj_key_at(value, i)));
                word(b, ": ");
                write_value(b, av_obj_value_at(value, i), depth + 1);
                word(b, i + 1 < av_obj_size(value) ? ",\n" : "\n");
            }
            if (av_obj_size(value)) {
                indent(b, depth);
            }
            word(b, "}");
            return;
        case AV_ARRAY: {
            bool simple = true;
            for (size_t i = 0; i < av_arr_size(value); i++) {
                simple &= !av_is_ctn(av_arr_get(value, i));
            }
            word(b, "[");
            if (!simple) {
                word(b, "\n");
            }
            for (size_t i = 0; i < av_arr_size(value); i++) {
                if (!simple) {
                    indent(b, depth + 1);
                }
                write_value(b, av_arr_get(value, i), depth + 1);
                if (i + 1 < av_arr_size(value)) {
                    word(b, simple ? ", " : ",");
                }
                if (!simple) {
                    word(b, "\n");
                }
            }
            if (!simple) {
                indent(b, depth);
            }
            word(b, "]");
            return;
        }
    }
    word(b, number_text);
}
char *code_value_write(const av_value *value)
{
    if (!value) {
        return NULL;
    }
    text_buffer buffer = { 0 };
    write_value(&buffer, value, 0);
    if (buffer.invalid) {
        free(buffer.data);
        return NULL;
    }
    return buffer.data;
}
