#include "editor/commands/codex_response.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

typedef struct
{
    const char *event;
    size_t event_size;
    char *data;
    size_t data_size;
    size_t data_capacity;
    bool has_data;
    bool completed;
    char *normalized;
} response_stream;

static void set_error(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message ? message : "Codex request failed");
    }
}

static bool text_equals(const char *text, size_t size, const char *expected)
{
    return strlen(expected) == size && !memcmp(text, expected, size);
}

static bool append_bytes(char **buffer, size_t *length, size_t *capacity, const char *value, size_t value_size)
{
    if (value_size > SIZE_MAX - *length - 1) {
        return false;
    }
    size_t needed = *length + value_size + 1;
    if (needed > *capacity) {
        size_t next = *capacity ? *capacity : 128;
        while (next < needed) {
            if (next > SIZE_MAX / 2) {
                next = needed;
                break;
            }
            next *= 2;
        }
        char *grown = realloc(*buffer, next);
        if (!grown) {
            return false;
        }
        *buffer = grown;
        *capacity = next;
    }
    memcpy(*buffer + *length, value, value_size);
    *length += value_size;
    (*buffer)[*length] = 0;
    return true;
}

static const char *error_message(yyjson_val *root)
{
    yyjson_val *error = yyjson_obj_get(root, "error");
    const char *message = yyjson_get_str(error);
    if (yyjson_is_obj(error)) {
        message = yyjson_get_str(yyjson_obj_get(error, "message"));
    }
    if (message && *message) {
        return message;
    }
    yyjson_val *response = yyjson_obj_get(root, "response");
    error = yyjson_obj_get(response, "error");
    message = yyjson_get_str(error);
    if (yyjson_is_obj(error)) {
        message = yyjson_get_str(yyjson_obj_get(error, "message"));
    }
    return message && *message ? message : NULL;
}

static char *tool_response(const char *arguments, size_t arguments_size)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val *choices = yyjson_mut_obj_add_arr(doc, root, "choices");
    yyjson_mut_val *choice = choices ? yyjson_mut_arr_add_obj(doc, choices) : NULL;
    yyjson_mut_val *message = choice ? yyjson_mut_obj_add_obj(doc, choice, "message")
                                     : NULL;
    yyjson_mut_val *calls = message
                              ? yyjson_mut_obj_add_arr(doc, message, "tool_calls")
                              : NULL;
    yyjson_mut_val *call = calls ? yyjson_mut_arr_add_obj(doc, calls) : NULL;
    yyjson_mut_val *function = call ? yyjson_mut_obj_add_obj(doc, call, "function")
                                    : NULL;
    bool ok = function &&
              yyjson_mut_obj_add_str(doc, function, "name", "submit_plan") &&
              yyjson_mut_obj_add_strncpy(doc, function, "arguments", arguments, arguments_size);
    char *json = ok ? yyjson_mut_write(doc, 0, NULL) : NULL;
    yyjson_mut_doc_free(doc);
    return json;
}

static char *text_response(const char *content, size_t content_size)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val *choices = yyjson_mut_obj_add_arr(doc, root, "choices");
    yyjson_mut_val *choice = choices ? yyjson_mut_arr_add_obj(doc, choices) : NULL;
    yyjson_mut_val *message = choice ? yyjson_mut_obj_add_obj(doc, choice, "message")
                                     : NULL;
    bool ok = message &&
              yyjson_mut_obj_add_strncpy(doc, message, "content", content, content_size);
    char *json = ok ? yyjson_mut_write(doc, 0, NULL) : NULL;
    yyjson_mut_doc_free(doc);
    return json;
}

static bool normalize_completed(yyjson_val *root, response_stream *stream, char *error, size_t error_size)
{
    yyjson_val *response = yyjson_obj_get(root, "response");
    yyjson_val *output = yyjson_obj_get(response, "output");
    if (!yyjson_is_arr(output)) {
        set_error(error, error_size, "Codex completed without a response output");
        return false;
    }

    yyjson_val *function_call = NULL;
    size_t function_call_count = 0;
    char *content = NULL;
    size_t content_size = 0;
    size_t content_capacity = 0;
    size_t index, max;
    yyjson_val *item;
    yyjson_arr_foreach(output, index, max, item)
    {
        const char *type = yyjson_get_str(yyjson_obj_get(item, "type"));
        if (!type) {
            set_error(error, error_size, "Codex returned an invalid output item");
            free(content);
            return false;
        }
        if (!strcmp(type, "reasoning")) {
            continue;
        }
        if (!strcmp(type, "function_call")) {
            function_call = item;
            function_call_count++;
            continue;
        }
        if (!strcmp(type, "message")) {
            yyjson_val *parts = yyjson_obj_get(item, "content");
            if (!yyjson_is_arr(parts)) {
                set_error(error, error_size, "Codex returned a message without text content");
                free(content);
                return false;
            }
            size_t part_index, part_max;
            yyjson_val *part;
            yyjson_arr_foreach(parts, part_index, part_max, part)
            {
                const char *part_type = yyjson_get_str(yyjson_obj_get(part, "type"));
                yyjson_val *text = yyjson_obj_get(part, "text");
                if (!part_type || strcmp(part_type, "output_text") ||
                    !yyjson_is_str(text)) {
                    set_error(error, error_size, "Codex returned non-text message content");
                    free(content);
                    return false;
                }
                if (!append_bytes(
                        &content,
                        &content_size,
                        &content_capacity,
                        yyjson_get_str(text),
                        yyjson_get_len(text)
                    )) {
                    set_error(error, error_size, "Could not allocate Codex response text");
                    free(content);
                    return false;
                }
            }
            continue;
        }
        set_error(error, error_size, "Codex returned an unsupported output item");
        free(content);
        return false;
    }

    if (function_call_count > 1) {
        set_error(error, error_size, "Codex returned more than one function call");
        free(content);
        return false;
    }
    if (function_call_count == 1) {
        const char *name = yyjson_get_str(yyjson_obj_get(function_call, "name"));
        yyjson_val *arguments = yyjson_obj_get(function_call, "arguments");
        if (!name || strcmp(name, "submit_plan")) {
            set_error(error, error_size, "Codex called an unexpected function");
            free(content);
            return false;
        }
        if (content_size) {
            set_error(error, error_size, "Codex returned both text and a function call");
            free(content);
            return false;
        }
        if (!yyjson_is_str(arguments) || !yyjson_get_len(arguments)) {
            set_error(error, error_size, "Codex returned invalid function arguments");
            free(content);
            return false;
        }
        const char *argument_text = yyjson_get_str(arguments);
        yyjson_doc *argument_doc = yyjson_read(argument_text, yyjson_get_len(arguments), 0);
        if (!argument_doc) {
            set_error(error, error_size, "Codex returned invalid function argument JSON");
            free(content);
            return false;
        }
        yyjson_doc_free(argument_doc);
        stream->normalized = tool_response(argument_text, yyjson_get_len(arguments));
        free(content);
        if (!stream->normalized) {
            set_error(error, error_size, "Could not encode Codex function response");
            return false;
        }
        return true;
    }

    if (!content_size) {
        set_error(error, error_size, "Codex returned no function call or text content");
        free(content);
        return false;
    }
    yyjson_doc *content_doc = yyjson_read(content, content_size, 0);
    if (!content_doc) {
        set_error(error, error_size, "Codex response text was not valid JSON");
        free(content);
        return false;
    }
    yyjson_doc_free(content_doc);
    stream->normalized = text_response(content, content_size);
    free(content);
    if (!stream->normalized) {
        set_error(error, error_size, "Could not encode Codex text response");
        return false;
    }
    return true;
}

static bool process_frame(response_stream *stream, char *error, size_t error_size)
{
    if (!stream->has_data) {
        stream->event = NULL;
        stream->event_size = 0;
        return true;
    }
    yyjson_doc *doc = yyjson_read(stream->data, stream->data_size, 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    const char *type = yyjson_get_str(yyjson_obj_get(root, "type"));
    bool failed_event = text_equals(stream->event, stream->event_size, "error") ||
                        text_equals(stream->event, stream->event_size, "response.failed") ||
                        (type && (!strcmp(type, "error") ||
                                  !strcmp(type, "response.failed")));
    bool incomplete_event = text_equals(stream->event, stream->event_size, "response.incomplete") ||
                            (type && !strcmp(type, "response.incomplete"));
    bool completed_event = text_equals(stream->event, stream->event_size, "response.completed") ||
                           (type && !strcmp(type, "response.completed"));
    if (failed_event || incomplete_event) {
        set_error(
            error,
            error_size,
            error_message(root) ? error_message(root)
            : incomplete_event  ? "Codex response was incomplete"
                                : "Codex response failed"
        );
        yyjson_doc_free(doc);
        return false;
    }
    if (!doc) {
        if (completed_event) {
            set_error(error, error_size, "Codex returned invalid completion JSON");
            return false;
        }
        stream->event = NULL;
        stream->event_size = 0;
        return true;
    }
    if (completed_event) {
        if (stream->completed) {
            set_error(error, error_size, "Codex returned multiple completed responses");
            yyjson_doc_free(doc);
            return false;
        }
        if (!normalize_completed(root, stream, error, error_size)) {
            yyjson_doc_free(doc);
            return false;
        }
        stream->completed = true;
    }
    yyjson_doc_free(doc);
    stream->event = NULL;
    stream->event_size = 0;
    return true;
}

char *editor_codex_response_normalize(const char *sse, char *error, size_t error_size)
{
    if (error_size) {
        error[0] = 0;
    }
    if (!sse) {
        set_error(error, error_size, "Codex returned no response");
        return NULL;
    }

    response_stream stream = { 0 };
    size_t input_size = strlen(sse);
    size_t position = 0;
    bool valid = true;
    while (position < input_size) {
        size_t line_start = position;
        while (position < input_size && sse[position] != '\n') {
            position++;
        }
        size_t line_size = position - line_start;
        if (position < input_size) {
            position++;
        }
        if (line_size && sse[line_start + line_size - 1] == '\r') {
            line_size--;
        }
        if (!line_size) {
            valid = process_frame(&stream, error, error_size);
            stream.data_size = 0;
            stream.has_data = false;
            if (stream.data) {
                stream.data[0] = 0;
            }
            if (!valid) {
                break;
            }
            continue;
        }
        const char *line = sse + line_start;
        if (line[0] == ':') {
            continue;
        }
        const char *colon = memchr(line, ':', line_size);
        size_t field_size = colon ? (size_t)(colon - line) : line_size;
        const char *value = colon ? colon + 1 : line + line_size;
        size_t value_size = colon ? line_size - field_size - 1 : 0;
        if (value_size && *value == ' ') {
            value++;
            value_size--;
        }
        if (text_equals(line, field_size, "event")) {
            stream.event = value;
            stream.event_size = value_size;
        }
        if (text_equals(line, field_size, "data")) {
            if (!append_bytes(&stream.data, &stream.data_size, &stream.data_capacity, value, value_size) ||
                !append_bytes(&stream.data, &stream.data_size, &stream.data_capacity, "\n", 1)) {
                set_error(error, error_size, "Could not allocate Codex event data");
                valid = false;
                break;
            }
            stream.has_data = true;
        }
    }
    if (valid && (stream.has_data || stream.event)) {
        valid = process_frame(&stream, error, error_size);
    }
    free(stream.data);
    if (!valid) {
        free(stream.normalized);
        return NULL;
    }
    if (!stream.completed) {
        free(stream.normalized);
        set_error(error, error_size, "Codex stream ended before response.completed");
        return NULL;
    }
    return stream.normalized;
}
