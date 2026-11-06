#include "support/asset_text.h"

#include "support/array.h"
#include "support/float_text.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <kdl/parser.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The KDL grammar is parsed by ckdl. This layer defines the typed asset
 * vocabulary: an implicit object root, named fields, and annotated arrays. */
typedef struct
{
    char *name;
    av_value *object, *arguments;
    bool array, force_object, children;
    int argument_count;
    av_source source;
} read_node;

static bool text_is(kdl_str text, const char *word)
{
    return text.len == strlen(word) && !memcmp(text.data, word, text.len);
}

static void read_error(const char *text, size_t at, char *error, size_t size, const char *message)
{
    int line = 1, column = 1;
    for (size_t i = 0; text[i] && i < at; i++) {
        if (text[i] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }
    if (error && size) {
        snprintf(error, size, "line %d, column %d: %s", line, column, message);
    }
}

static av_value *read_number(av_doc *doc, const kdl_number *number, const char *text, size_t size)
{
    if (number->type == KDL_NUMBER_TYPE_INTEGER) {
        return number->integer < 0 ? av_sint(doc, number->integer)
                                   : av_uint(doc, (uint64_t)number->integer);
    }
    if (number->type == KDL_NUMBER_TYPE_FLOATING_POINT) {
        char *literal = xmalloc(size + 1);
        size_t used = 0;
        for (size_t i = 0; i < size; i++) {
            if (text[i] != '_') {
                literal[used++] = text[i];
            }
        }
        literal[used] = 0;
        errno = 0;
        char *end;
        double value = strtod(literal, &end);
        bool valid = used && !*end && isfinite(value) &&
                     (errno != ERANGE || value != 0);
        free(literal);
        return valid ? av_real(doc, value) : NULL;
    }
    /* ckdl preserves integers above LLONG_MAX as text. Parse with the full
     * unsigned range instead of rounding them through a double. */
    char *number_text = xmalloc(number->string.len + 1);
    size_t used = 0;
    for (size_t i = 0; i < number->string.len; i++) {
        if (number->string.data[i] != '_') {
            number_text[used++] = number->string.data[i];
        }
    }
    number_text[used] = 0;
    const char *prefix = number_text + (number_text[0] == '+' || number_text[0] == '-');
    bool based = prefix[0] == '0' && prefix[1] && strchr("xob", prefix[1]);
    if (!based && (strchr(number_text, '.') || strchr(number_text, 'e') ||
                   strchr(number_text, 'E'))) {
        errno = 0;
        char *end;
        double value = strtod(number_text, &end);
        bool valid = *number_text && !*end && isfinite(value) &&
                     (errno != ERANGE || value != 0);
        free(number_text);
        return valid ? av_real(doc, value) : NULL;
    }
    if (number_text[0] == '-') {
        errno = 0;
        char *end;
        int64_t value = strtoll(number_text, &end, 10);
        bool valid = number_text[1] && !*end && errno != ERANGE;
        free(number_text);
        return valid ? av_sint(doc, value) : NULL;
    }
    char *digits = number_text;
    if (*digits == '+') {
        digits++;
    }
    int base = 10;
    if (digits[0] == '0' && digits[1] && strchr("xob", digits[1])) {
        base = digits[1] == 'x' ? 16 : digits[1] == 'o' ? 8 : 2;
        digits += 2;
    }
    errno = 0;
    char *end;
    uint64_t value = strtoull(digits, &end, base);
    bool valid = *digits && *digits != '-' && !*end && errno != ERANGE;
    free(number_text);
    return valid ? av_uint(doc, value) : NULL;
}

static av_value *read_value(av_doc *doc, const kdl_event_data *event, const char *text)
{
    if (event->value.type_annotation.len) {
        return NULL;
    }
    av_value *value = NULL;
    switch (event->value.type) {
        case KDL_TYPE_NULL: value = av_null(doc); break;
        case KDL_TYPE_BOOLEAN:
            value = av_bool(doc, event->value.boolean);
            break;
        case KDL_TYPE_STRING:
            value = av_strn(doc, event->value.string.data, event->value.string.len);
            break;
        case KDL_TYPE_NUMBER:
            value = read_number(
                doc,
                &event->value.number,
                text + event->start,
                event->end - event->start
            );
            if (av_is_uint(value)) {
                const char *literal = text + event->start;
                literal += *literal == '+';
                if (literal[0] == '0' && literal[1] == 'x') {
                    size_t digits = 0;
                    for (const char *p = literal + 2; p < text + event->end; p++) {
                        digits += *p != '_';
                    }
                    uint64_t number = av_get_uint(value);
                    value = digits == 8 && number <= UINT32_MAX
                              ? av_color(doc, (uint32_t)number)
                              : av_hex(doc, number);
                }
            }
            break;
    }
    if (value) {
        av_set_source(
            value,
            (av_source){ event->start, event->end, event->name_start, event->name_end }
        );
    }
    return value;
}

av_doc *av_doc_read(const char *text, char *error, size_t error_size)
{
    if (error && error_size) {
        error[0] = 0;
    }
    if (!text) {
        read_error("", 0, error, error_size, "null KDL input");
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    kdl_parser *parser = kdl_create_string_parser((kdl_str){ text, strlen(text) }, KDL_READ_VERSION_2);
    read_node *stack = NULL;
    const char *failure = NULL;
    size_t at = 0;
    for (;;) {
        kdl_event_data *event = kdl_parser_next_event(parser);
        at = event->start;
        if (event->event == KDL_EVENT_EOF) {
            break;
        }
        if (event->event == KDL_EVENT_PARSE_ERROR) {
            failure = event->value.string.data ? event->value.string.data : "invalid KDL";
            break;
        }
        if ((event->event == KDL_EVENT_START_NODE || event->event == KDL_EVENT_PROPERTY) &&
            memchr(event->name.data, 0, event->name.len)) {
            failure = "asset field names cannot contain a null byte";
            break;
        }
        if (event->event == KDL_EVENT_START_NODE) {
            if (acount(stack)) {
                stack[acount(stack) - 1].children = true;
            }
            read_node node = { 0 };
            node.name = xmalloc(event->name.len + 1);
            memcpy(node.name, event->name.data, event->name.len);
            node.name[event->name.len] = 0;
            node.object = av_obj(doc);
            node.arguments = av_arr(doc);
            node.array = text_is(event->value.type_annotation, "array");
            node.force_object = text_is(event->value.type_annotation, "object");
            node.source = (av_source){ event->start,
                                       event->end,
                                       event->name_start,
                                       event->name_end };
            apush(stack, node);
            if (event->value.type_annotation.len && !node.array && !node.force_object) {
                failure = "asset nodes support only (array) and (object) "
                          "annotations";
                break;
            }
            continue;
        }
        if (!acount(stack)) {
            failure = "asset value outside a node";
            break;
        }
        read_node *node = &stack[acount(stack) - 1];
        if (event->event == KDL_EVENT_ARGUMENT || event->event == KDL_EVENT_PROPERTY) {
            av_value *value = read_value(doc, event, text);
            if (!value) {
                failure = "unsupported typed or non-finite asset value, or "
                          "integer outside 64-bit range";
                break;
            }
            if (event->event == KDL_EVENT_ARGUMENT) {
                node->argument_count++;
                av_arr_append(node->arguments, value);
                continue;
            }
            char *key = xmalloc(event->name.len + 1);
            memcpy(key, event->name.data, event->name.len);
            key[event->name.len] = 0;
            bool duplicate = av_obj_get(node->object, key) != NULL;
            if (!duplicate) {
                av_obj_set(doc, node->object, key, value);
            }
            free(key);
            if (duplicate) {
                failure = "duplicate asset property";
                break;
            }
            continue;
        }
        if (event->event != KDL_EVENT_END_NODE) {
            continue;
        }
        size_t arguments = av_arr_size(node->arguments),
               fields = av_obj_size(node->object);
        if ((node->array && fields) ||
            (node->argument_count && (fields || node->force_object || node->children)) ||
            (!node->array && arguments > 1)) {
            failure = "asset node mixes scalar arguments and object children "
                      "or properties";
            break;
        }
        av_value *value = node->array ? node->arguments
                        : arguments   ? av_arr_get(node->arguments, 0)
                                      : node->object;
        if (av_is_ctn(value)) {
            node->source.end = event->start;
            av_set_source(value, node->source);
        } else {
            av_source source = av_get_source(value);
            source.name_start = node->source.name_start;
            source.name_end = node->source.name_end;
            av_set_source(value, source);
        }
        read_node *parent = acount(stack) > 1 ? &stack[acount(stack) - 2] : NULL;
        if (parent && parent->array) {
            if (strcmp(node->name, "-")) {
                failure = "array child nodes must be named '-'";
                break;
            }
            av_arr_append(parent->arguments, value);
        } else {
            av_value *object = parent ? parent->object : root;
            if (av_obj_get(object, node->name)) {
                failure = "duplicate asset field";
                break;
            }
            av_obj_set(doc, object, node->name, value);
        }
        free(node->name);
        asetlen(stack, acount(stack) - 1);
    }
    if (!failure && acount(stack)) {
        failure = "unclosed asset node";
    }
    if (failure) {
        read_error(text, at, error, error_size, failure);
    }
    for (int i = 0; i < acount(stack); i++) {
        free(stack[i].name);
    }
    afree(stack);
    kdl_destroy_parser(parser);
    if (failure) {
        av_doc_free(doc);
        return NULL;
    }
    av_set_source(root, (av_source){ 0, strlen(text), SIZE_MAX, SIZE_MAX });
    return doc;
}

av_doc *av_value_read(const char *text, char *error, size_t error_size)
{
    if (!text) {
        return av_doc_read(text, error, error_size);
    }
    size_t len = strlen(text);
    char *wrapped = xmalloc(len + 9);
    memcpy(wrapped, "__value ", 8);
    memcpy(wrapped + 8, text, len + 1);
    av_doc *doc = av_doc_read(wrapped, NULL, 0);
    free(wrapped);
    if (doc) {
        av_value *value = av_obj_get(av_doc_root(doc), "__value");
        if (value && !av_is_ctn(value)) {
            av_source source = av_get_source(value);
            source.start -= 8;
            source.end -= 8;
            source.name_start = source.name_end = SIZE_MAX;
            av_set_source(value, source);
            av_doc_set_root(doc, value);
            if (error && error_size) {
                error[0] = 0;
            }
            return doc;
        }
        av_doc_free(doc);
    }
    doc = av_doc_read(text, error, error_size);
    if (doc && !strncmp(text, "(array)-", 8)) {
        av_value *array = av_obj_get(av_doc_root(doc), "-");
        if (av_is_arr(array) && av_obj_size(av_doc_root(doc)) == 1) {
            av_doc_set_root(doc, array);
        }
    }
    return doc;
}

typedef struct
{
    char *data;
    size_t count, capacity;
    bool invalid;
} text_buffer;

static void write_text(text_buffer *buffer, const char *text, size_t size)
{
    if (buffer->count + size + 1 > buffer->capacity) {
        buffer->capacity = (buffer->count + size + 1) * 2;
        buffer->data = xrealloc(buffer->data, buffer->capacity);
    }
    memcpy(buffer->data + buffer->count, text, size);
    buffer->count += size;
    buffer->data[buffer->count] = 0;
}
static void write_word(text_buffer *buffer, const char *text)
{
    write_text(buffer, text, strlen(text));
}
static void write_string(text_buffer *buffer, const char *text, size_t size)
{
    kdl_str input = { text, size };
    kdl_owned_string escaped = kdl_escape_v(KDL_VERSION_2, &input, KDL_ESCAPE_DEFAULT);
    if (!escaped.data) {
        buffer->invalid = true;
        return;
    }
    write_word(buffer, "\"");
    write_text(buffer, escaped.data, escaped.len);
    write_word(buffer, "\"");
    kdl_free_string(&escaped);
}
static void write_name(text_buffer *buffer, const char *name)
{
    bool bare = *name == '-' && !name[1];
    if (isalpha((unsigned char)*name) || *name == '_') {
        bare = true;
        for (const char *p = name + 1; *p; p++) {
            bare &= isalnum((unsigned char)*p) || strchr("_.-", *p) != NULL;
        }
    }
    if (bare) {
        write_word(buffer, name);
        return;
    }
    write_string(buffer, name, strlen(name));
}
static bool write_scalar(text_buffer *buffer, const av_value *value)
{
    char number[64];
    switch (av_get_type(value)) {
        case AV_NULL: write_word(buffer, "#null"); return true;
        case AV_BOOL:
            write_word(buffer, av_get_bool(value) ? "#true" : "#false");
            return true;
        case AV_SINT:
            snprintf(number, sizeof number, "%" PRId64, av_get_sint(value));
            break;
        case AV_UINT:
            if (av_is_color(value)) {
                snprintf(number, sizeof number, "0x%08" PRIx64, av_get_uint(value));
            } else if (av_is_hex(value)) {
                snprintf(number, sizeof number, "0x%" PRIx64, av_get_uint(value));
            } else {
                snprintf(number, sizeof number, "%" PRIu64, av_get_uint(value));
            }
            break;
        case AV_REAL: {
            double real = av_get_num(value);
            if (!isfinite(real)) {
                return false;
            }
            float_text(number, sizeof number, real, false);
            if (!strchr(number, '.') && !strchr(number, 'e')) {
                strncat(number, ".0", sizeof number - strlen(number) - 1);
            }
            break;
        }
        case AV_STRING:
            write_string(buffer, av_get_str(value), av_get_len(value));
            return true;
        case AV_OBJECT:
        case AV_ARRAY: return false;
    }
    write_word(buffer, number);
    return true;
}
static void write_indent(text_buffer *buffer, int depth)
{
    for (int i = 0; i < depth; i++) {
        write_word(buffer, "    ");
    }
}
static bool write_node(text_buffer *buffer, const char *name, const av_value *value, int depth)
{
    write_indent(buffer, depth);
    if (av_is_arr(value)) {
        write_word(buffer, "(array)");
    }
    if (av_is_obj(value) && !av_obj_size(value)) {
        write_word(buffer, "(object)");
    }
    write_name(buffer, name);
    if (!av_is_ctn(value)) {
        write_word(buffer, " ");
        if (!write_scalar(buffer, value)) {
            return false;
        }
        write_word(buffer, "\n");
        return true;
    }
    size_t count = av_is_arr(value) ? av_arr_size(value) : av_obj_size(value);
    bool simple = true;
    for (size_t i = 0; i < count; i++) {
        simple &= !av_is_ctn(
            av_is_arr(value) ? av_arr_get(value, i) : av_obj_value_at(value, i)
        );
    }
    if (simple) {
        for (size_t i = 0; i < count; i++) {
            write_word(buffer, " ");
            if (av_is_obj(value)) {
                write_name(buffer, av_get_str(av_obj_key_at(value, i)));
                write_word(buffer, "=");
            }
            if (!write_scalar(buffer, av_is_arr(value) ? av_arr_get(value, i) : av_obj_value_at(value, i))) {
                return false;
            }
        }
        write_word(buffer, "\n");
        return true;
    }
    write_word(buffer, " {\n");
    for (size_t i = 0; i < count; i++) {
        const char *key = av_is_arr(value) ? "-"
                                           : av_get_str(av_obj_key_at(value, i));
        const av_value *child = av_is_arr(value) ? av_arr_get(value, i)
                                                 : av_obj_value_at(value, i);
        if (!write_node(buffer, key, child, depth + 1)) {
            return false;
        }
    }
    write_indent(buffer, depth);
    write_word(buffer, "}\n");
    return true;
}
char *av_value_write(const av_value *value)
{
    if (!value) {
        return NULL;
    }
    text_buffer buffer = { 0 };
    bool valid = true;
    if (av_is_obj(value)) {
        for (size_t i = 0; i < av_obj_size(value); i++) {
            valid &= write_node(
                &buffer,
                av_get_str(av_obj_key_at(value, i)),
                av_obj_value_at(value, i),
                0
            );
        }
    } else if (av_is_arr(value)) {
        valid = write_node(&buffer, "-", value, 0);
    } else {
        valid = write_scalar(&buffer, value);
    }
    if (!valid || buffer.invalid) {
        free(buffer.data);
        return NULL;
    }
    if (!buffer.data) {
        return xstrdup("");
    }
    return buffer.data;
}
char *av_doc_write(const av_doc *doc)
{
    return av_is_obj(av_doc_root(doc)) ? av_value_write(av_doc_root(doc)) : NULL;
}
av_doc *asset_text_read(const char *text, char *error, size_t error_size)
{
    return av_doc_read(text, error, error_size);
}
/* Only file emission drops automatic graph positions. In-memory trees retain
 * them, so snapshots and undo preserve the editor's current arrangement. */
static void strip_positions(av_value *value)
{
    if (av_is_obj(value)) {
        av_value *placed = av_obj_get(value, "placed");
        if (av_is_bool(placed) && !av_get_bool(placed) && av_obj_get(value, "type")) {
            av_obj_remove_key(value, "x");
            av_obj_remove_key(value, "y");
            av_obj_remove_key(value, "placed");
        }
        for (size_t i = 0; i < av_obj_size(value); i++) {
            strip_positions(av_obj_value_at(value, i));
        }
    }
    if (av_is_arr(value)) {
        for (size_t i = 0; i < av_arr_size(value); i++) {
            strip_positions(av_arr_get(value, i));
        }
    }
}
char *asset_text_to_disk(const char *text, char *error, size_t error_size)
{
    av_doc *doc = av_doc_read(text, error, error_size);
    if (!doc) {
        return NULL;
    }
    strip_positions(av_doc_root(doc));
    char *out = av_doc_write(doc);
    av_doc_free(doc);
    return out;
}

char *asset_text_write(const av_value *root)
{
    return av_is_obj(root) ? av_value_write(root) : NULL;
}
