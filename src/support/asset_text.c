#include "support/asset_text.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <yaml.h>

/* One scalar grammar serves both directions. Plain scalars that spell JSON
 * null, a boolean, or a JSON number keep that type; everything else is a
 * string. Writing quotes strings that would read back as another type or
 * that YAML 1.1 tools would reinterpret. */
static const char *const NULL_WORDS[] = { "", "~", "null", "Null", "NULL", NULL };
static const char *const TRUE_WORDS[] = { "true", "True", "TRUE", NULL };
static const char *const FALSE_WORDS[] = { "false", "False", "FALSE", NULL };
static const char *const YAML11_WORDS[] = { "y",   "Y",  "yes", "Yes", "YES",
                                            "n",   "N",  "no",  "No",  "NO",
                                            "on",  "On", "ON",  "off", "Off",
                                            "OFF", NULL };

static bool is_word(const char *text, size_t length, const char *const *words)
{
    for (int i = 0; words[i]; i++) {
        if (strlen(words[i]) == length && !memcmp(text, words[i], length)) {
            return true;
        }
    }
    return false;
}

static size_t skip_digits(const char *text, size_t length, size_t at)
{
    while (at < length && isdigit((unsigned char)text[at])) {
        at++;
    }
    return at;
}

static bool is_json_number(const char *text, size_t length)
{
    size_t at = length && text[0] == '-';
    if (at >= length || !isdigit((unsigned char)text[at])) {
        return false;
    }
    at = text[at] == '0' ? at + 1 : skip_digits(text, length, at);
    if (at < length && text[at] == '.') {
        if (at + 1 >= length || !isdigit((unsigned char)text[at + 1])) {
            return false;
        }
        at = skip_digits(text, length, at + 1);
    }
    if (at < length && (text[at] == 'e' || text[at] == 'E')) {
        at++;
        at += at < length && (text[at] == '+' || text[at] == '-');
        if (at >= length || !isdigit((unsigned char)text[at])) {
            return false;
        }
        at = skip_digits(text, length, at);
    }
    return at == length;
}

static bool needs_quotes(const char *text, size_t length)
{
    if (is_word(text, length, NULL_WORDS) || is_word(text, length, TRUE_WORDS) ||
        is_word(text, length, FALSE_WORDS) || is_word(text, length, YAML11_WORDS)) {
        return true;
    }
    size_t at = length && (text[0] == '-' || text[0] == '+' || text[0] == '.');
    return at < length &&
           (isdigit((unsigned char)text[at]) || (at && text[at] == '.'));
}

/* --- YAML to JSON -------------------------------------------------------- */

typedef struct reader
{
    yyjson_mut_doc *doc;
    yyjson_mut_val **containers;
    yyjson_mut_val **keys;
    yyjson_mut_val *root;
} reader;

static yyjson_mut_val *scalar_value(yyjson_mut_doc *doc, const yaml_event_t *event)
{
    const char *text = (const char *)event->data.scalar.value;
    size_t length = event->data.scalar.length;
    if (event->data.scalar.style != YAML_PLAIN_SCALAR_STYLE) {
        return yyjson_mut_strncpy(doc, text, length);
    }
    if (is_word(text, length, NULL_WORDS)) {
        return yyjson_mut_null(doc);
    }
    if (is_word(text, length, TRUE_WORDS) || is_word(text, length, FALSE_WORDS)) {
        return yyjson_mut_bool(doc, is_word(text, length, TRUE_WORDS));
    }
    if (is_json_number(text, length)) {
        return yyjson_mut_rawncpy(doc, text, length);
    }
    return yyjson_mut_strncpy(doc, text, length);
}

/* Adds a value to the open container; mapping keys must be strings. */
static bool reader_add(reader *r, yyjson_mut_val *value)
{
    int depth = acount(r->containers);
    if (!depth) {
        r->root = value;
        return true;
    }
    yyjson_mut_val *parent = r->containers[depth - 1];
    if (yyjson_mut_is_arr(parent)) {
        return yyjson_mut_arr_append(parent, value);
    }
    if (!r->keys[depth - 1]) {
        r->keys[depth - 1] = value;
        return yyjson_mut_is_str(value);
    }
    bool added = yyjson_mut_obj_add(parent, r->keys[depth - 1], value);
    r->keys[depth - 1] = NULL;
    return added;
}

static char *yaml_to_json(const char *yaml, char *error, size_t error_size)
{
    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, strlen(yaml));
    reader r = { .doc = yyjson_mut_doc_new(NULL) };
    int documents = 0;
    const char *problem = NULL;
    yaml_mark_t mark = { 0 };
    bool done = false;
    while (!done && !problem) {
        yaml_event_t event;
        if (!yaml_parser_parse(&parser, &event)) {
            problem = parser.problem ? parser.problem : "invalid YAML";
            mark = parser.problem_mark;
            break;
        }
        mark = event.start_mark;
        switch (event.type) {
            case YAML_STREAM_END_EVENT: done = true; break;
            case YAML_DOCUMENT_START_EVENT:
                problem = ++documents > 1 ? "expected a single document" : NULL;
                break;
            case YAML_ALIAS_EVENT:
                problem = "anchors and aliases are not supported";
                break;
            case YAML_SCALAR_EVENT:
                problem = reader_add(&r, scalar_value(r.doc, &event))
                            ? NULL
                            : "mapping keys must be strings";
                break;
            case YAML_SEQUENCE_START_EVENT:
            case YAML_MAPPING_START_EVENT: {
                yyjson_mut_val *value = event.type == YAML_SEQUENCE_START_EVENT
                                          ? yyjson_mut_arr(r.doc)
                                          : yyjson_mut_obj(r.doc);
                problem = reader_add(&r, value) ? NULL : "mapping keys must be strings";
                apush(r.containers, value);
                apush(r.keys, NULL);
                break;
            }
            case YAML_SEQUENCE_END_EVENT:
            case YAML_MAPPING_END_EVENT:
                asetlen(r.containers, acount(r.containers) - 1);
                asetlen(r.keys, acount(r.keys) - 1);
                break;
            default: break;
        }
        yaml_event_delete(&event);
    }
    yaml_parser_delete(&parser);
    char *json = NULL;
    if (problem) {
        snprintf(error, error_size, "line %zu, column %zu: %s", mark.line + 1, mark.column + 1, problem);
    } else if (!r.root) {
        snprintf(error, error_size, "asset document is empty");
    } else {
        yyjson_mut_doc_set_root(r.doc, r.root);
        json = yyjson_mut_write(r.doc, 0, NULL);
    }
    yyjson_mut_doc_free(r.doc);
    afree(r.containers);
    afree(r.keys);
    return json;
}

/* In-memory documents are JSON objects; files are YAML mappings. */
static bool json_text(const char *text)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }
    return *text == '{';
}

yyjson_doc *asset_text_read(const char *text, char *error, size_t error_size)
{
    if (json_text(text)) {
        yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
        if (!doc) {
            snprintf(error, error_size, "asset document is not valid JSON");
        }
        return doc;
    }
    char *json = yaml_to_json(text, error, error_size);
    yyjson_doc *doc = json ? yyjson_read(json, strlen(json), 0) : NULL;
    free(json);
    return doc;
}

/* --- JSON to YAML -------------------------------------------------------- */

typedef struct buffer
{
    char *data;
    size_t size;
    size_t capacity;
} buffer;

static int write_buffer(void *user, unsigned char *data, size_t size)
{
    buffer *out = user;
    if (out->size + size + 1 > out->capacity) {
        out->capacity = (out->size + size + 1) * 2;
        out->data = xrealloc(out->data, out->capacity);
    }
    memcpy(out->data + out->size, data, size);
    out->size += size;
    out->data[out->size] = '\0';
    return 1;
}

static bool emit_scalar(yaml_emitter_t *emitter, const char *text, size_t length, yaml_scalar_style_t style)
{
    yaml_event_t event;
    yaml_scalar_event_initialize(&event, NULL, NULL, (yaml_char_t *)text, (int)length, 1, 1, style);
    return yaml_emitter_emit(emitter, &event);
}

/* Short lists of scalars, such as pin IDs, read best on one line. */
static bool flow_sequence(yyjson_val *array)
{
    size_t width = 0, i, count;
    yyjson_val *item;
    yyjson_arr_foreach(array, i, count, item)
    {
        if (yyjson_is_ctn(item) ||
            (yyjson_is_str(item) && strchr(yyjson_get_str(item), '\n'))) {
            return false;
        }
        width += (yyjson_is_str(item) || yyjson_is_raw(item) ? yyjson_get_len(item)
                                                             : 5) +
                 2;
    }
    return width <= 80;
}

static bool automatic_position(yyjson_val *object, const char *key)
{
    return yyjson_is_false(yyjson_obj_get(object, "placed")) &&
           (!strcmp(key, "x") || !strcmp(key, "y") || !strcmp(key, "placed"));
}

static bool emit_value(yaml_emitter_t *emitter, yyjson_val *value)
{
    yaml_event_t event;
    if (yyjson_is_obj(value)) {
        yaml_mapping_start_event_initialize(
            &event,
            NULL,
            NULL,
            1,
            yyjson_obj_size(value) ? YAML_BLOCK_MAPPING_STYLE : YAML_FLOW_MAPPING_STYLE
        );
        if (!yaml_emitter_emit(emitter, &event)) {
            return false;
        }
        size_t i, count;
        yyjson_val *key, *item;
        yyjson_obj_foreach(value, i, count, key, item)
        {
            if (automatic_position(value, yyjson_get_str(key))) {
                continue;
            }
            if (!emit_value(emitter, key) || !emit_value(emitter, item)) {
                return false;
            }
        }
        yaml_mapping_end_event_initialize(&event);
        return yaml_emitter_emit(emitter, &event);
    }
    if (yyjson_is_arr(value)) {
        bool flow = !yyjson_arr_size(value) || flow_sequence(value);
        yaml_sequence_start_event_initialize(&event, NULL, NULL, 1, flow ? YAML_FLOW_SEQUENCE_STYLE : YAML_BLOCK_SEQUENCE_STYLE);
        if (!yaml_emitter_emit(emitter, &event)) {
            return false;
        }
        size_t i, count;
        yyjson_val *item;
        yyjson_arr_foreach(value, i, count, item)
        {
            if (!emit_value(emitter, item)) {
                return false;
            }
        }
        yaml_sequence_end_event_initialize(&event);
        return yaml_emitter_emit(emitter, &event);
    }
    if (yyjson_is_null(value)) {
        return emit_scalar(emitter, "null", 4, YAML_PLAIN_SCALAR_STYLE);
    }
    if (yyjson_is_bool(value)) {
        const char *text = yyjson_get_bool(value) ? "true" : "false";
        return emit_scalar(emitter, text, strlen(text), YAML_PLAIN_SCALAR_STYLE);
    }
    if (yyjson_is_raw(value)) {
        return emit_scalar(emitter, yyjson_get_raw(value), yyjson_get_len(value), YAML_PLAIN_SCALAR_STYLE);
    }
    const char *text = yyjson_get_str(value);
    size_t length = yyjson_get_len(value);
    yaml_scalar_style_t style = memchr(text, '\n', length) ? YAML_LITERAL_SCALAR_STYLE
                              : needs_quotes(text, length)
                                  ? YAML_DOUBLE_QUOTED_SCALAR_STYLE
                                  : YAML_ANY_SCALAR_STYLE;
    return emit_scalar(emitter, text, length, style);
}

char *asset_text_to_disk(const char *text, char *error, size_t error_size)
{
    char *converted = json_text(text) ? NULL : yaml_to_json(text, error, error_size);
    if (!json_text(text) && !converted) {
        return NULL;
    }
    const char *json = converted ? converted : text;
    yyjson_doc *doc = yyjson_read(json, strlen(json), YYJSON_READ_NUMBER_AS_RAW);
    free(converted);
    if (!doc) {
        snprintf(error, error_size, "asset document is not valid JSON");
        return NULL;
    }
    buffer out = { 0 };
    yaml_emitter_t emitter;
    yaml_emitter_initialize(&emitter);
    yaml_emitter_set_output(&emitter, write_buffer, &out);
    yaml_emitter_set_unicode(&emitter, 1);
    yaml_emitter_set_width(&emitter, -1);
    yaml_event_t event;
    yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
    bool ok = yaml_emitter_emit(&emitter, &event);
    yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1);
    ok = ok && yaml_emitter_emit(&emitter, &event) &&
         emit_value(&emitter, yyjson_doc_get_root(doc));
    yaml_document_end_event_initialize(&event, 1);
    ok = ok && yaml_emitter_emit(&emitter, &event);
    yaml_stream_end_event_initialize(&event);
    ok = ok && yaml_emitter_emit(&emitter, &event);
    if (!ok) {
        snprintf(error, error_size, "%s", emitter.problem ? emitter.problem : "YAML could not be written");
        free(out.data);
        out.data = NULL;
    }
    yaml_emitter_delete(&emitter);
    yyjson_doc_free(doc);
    return out.data;
}
