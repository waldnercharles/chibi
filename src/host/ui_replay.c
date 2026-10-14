#include "host/ui_replay.h"

#include "runtime/json_query.h"
#include "support/sha256.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(char *error, size_t size, const char *path, const char *expected)
{
    if (size) {
        snprintf(error, size, "ui-replay.invalid %s: expected %s", path, expected);
    }
    return false;
}
static bool keys(yyjson_val *object, const char *const *names, size_t count)
{
    if (!yyjson_is_obj(object)) {
        return false;
    }
    yyjson_obj_iter iter = yyjson_obj_iter_with(object);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        bool found = false;
        for (size_t i = 0; i < count; i++) {
            found |= !strcmp(yyjson_get_str(key), names[i]);
        }
        if (!found) {
            return false;
        }
    }
    return true;
}
static const char *text(yyjson_val *object, const char *name, size_t maximum)
{
    yyjson_val *value = yyjson_obj_get(object, name);
    if (!yyjson_is_str(value) || !yyjson_get_len(value) ||
        yyjson_get_len(value) > maximum) {
        return NULL;
    }
    return yyjson_get_str(value);
}
static bool frame(yyjson_val *object, int frames, int previous, int *out)
{
    yyjson_val *value = yyjson_obj_get(object, "frame");
    if (!yyjson_is_int(value) || yyjson_get_sint(value) < previous ||
        yyjson_get_sint(value) >= frames) {
        return false;
    }
    *out = (int)yyjson_get_sint(value);
    return true;
}
static bool key_valid(const char *key)
{
    if (!key) {
        return false;
    }
    if (strlen(key) == 1 &&
        ((*key >= 'A' && *key <= 'Z') || (*key >= '0' && *key <= '9'))) {
        return true;
    }
    static const char *const known[] = {
        "Space", "Tab", "Enter",  "Escape",   "Backspace", "Delete", "Insert",
        "Home",  "End", "PageUp", "PageDown", "Left",      "Right",  "Up",
        "Down",  "F1",  "F2",     "F3",       "F4",        "F5",     "F6",
        "F7",    "F8",  "F9",     "F10",      "F11",       "F12",    ";",
        "=",     ",",   "-",      ".",        "/",         "`",      "[",
        "\\",    "]",   "'"
    };
    for (size_t i = 0; i < sizeof known / sizeof *known; i++) {
        if (!strcmp(key, known[i])) {
            return true;
        }
    }
    return false;
}
static bool modifiers(yyjson_val *object, unsigned *out)
{
    yyjson_val *array = yyjson_obj_get(object, "modifiers");
    if (!array) {
        return true;
    }
    if (!yyjson_is_arr(array) || yyjson_arr_size(array) > 5) {
        return false;
    }
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(array, i, count, value)
    {
        if (!yyjson_is_str(value)) {
            return false;
        }
        const char *name = yyjson_get_str(value);
        unsigned flag = !strcmp(name, "Ctrl")  ? 1
                      : !strcmp(name, "Shift") ? 2
                      : !strcmp(name, "Alt")   ? 4
                      : !strcmp(name, "Meta")  ? 8
                      : !strcmp(name, "Mod")   ? 128
                                               : 0;
        if (!flag || (*out & flag)) {
            return false;
        }
        *out |= flag;
    }
    return true;
}
static bool coordinate(yyjson_val *object, const char *name, double *out)
{
    yyjson_val *value = yyjson_obj_get(object, name);
    if (!value) {
        *out = .5;
        return true;
    }
    if (!yyjson_is_num(value)) {
        return false;
    }
    *out = yyjson_get_num(value);
    return isfinite(*out) && *out >= 0 && *out <= 1;
}
ui_replay *ui_replay_parse(const char *input, size_t bytes, char *error, size_t size)
{
    if (size) {
        error[0] = 0;
    }
    if (!input || !bytes || bytes > UI_REPLAY_BYTES_MAX || memchr(input, 0, bytes)) {
        fail(error, size, "/", "nonempty UTF-8 JSON up to256KiB");
        return NULL;
    }
    char *terminated = xmalloc(bytes + 1);
    memcpy(terminated, input, bytes);
    terminated[bytes] = 0;
    json_query_result checked = { 0 };
    bool valid = query_json(terminated, "", &checked, error, size);
    json_query_result_free(&checked);
    if (!valid) {
        free(terminated);
        return NULL;
    }
    yyjson_doc *document = yyjson_read(terminated, bytes, 0);
    free(terminated);
    yyjson_val *root = yyjson_doc_get_root(document);
    static const char *const root_keys[] = { "version", "frames", "events", "assertions" };
    yyjson_val *version = yyjson_obj_get(root, "version"),
               *frames = yyjson_obj_get(root, "frames"),
               *events = yyjson_obj_get(root, "events"),
               *assertions = yyjson_obj_get(root, "assertions");
    if (!keys(root, root_keys, 4) || !yyjson_is_int(version) ||
        yyjson_get_sint(version) != 1 || !yyjson_is_int(frames) ||
        yyjson_get_sint(frames) < 1 || yyjson_get_sint(frames) > UI_REPLAY_FRAMES_MAX ||
        !yyjson_is_arr(events) || !yyjson_is_arr(assertions) ||
        yyjson_arr_size(events) + yyjson_arr_size(assertions) > UI_REPLAY_RECORDS_MAX) {
        yyjson_doc_free(document);
        fail(error, size, "/", "version1, frames1..60, events/assertions arrays with <=256combined records");
        return NULL;
    }
    ui_replay *out = xcalloc(1, sizeof *out);
    out->document = document;
    sha256_hex(input, bytes, out->request_sha256);
    out->frames = (int)yyjson_get_sint(frames);
    static const char *const event_keys[] = { "frame", "type",  "selector",
                                              "key",   "text",  "x",
                                              "y",     "delta", "modifiers" };
    static const char *const kinds[] = { "click",        "pointer-move",
                                         "pointer-down", "pointer-up",
                                         "wheel",        "key-down",
                                         "key-up",       "text" };
    size_t i, count;
    yyjson_val *value;
    int previous = 0;
    yyjson_arr_foreach(events, i, count, value)
    {
        char path[64];
        snprintf(path, sizeof path, "/events/%zu", i);
        ui_replay_event *event = &out->events[out->event_count];
        const char *kind = text(value, "type", 32);
        int selected = -1;
        if (kind) {
            for (int j = 0; j < 8; j++) {
                if (!strcmp(kind, kinds[j])) {
                    selected = j;
                }
            }
        }
        if (!keys(value, event_keys, 9) || selected < 0 ||
            !frame(value, out->frames, previous, &event->frame) ||
            !modifiers(value, &event->modifiers)) {
            fail(error, size, path, "known event, monotonic frame, unique known modifiers");
            goto invalid;
        }
        previous = event->frame;
        event->kind = selected;
        if (selected <= UI_REPLAY_WHEEL) {
            event->selector = text(value, "selector", 512);
            if (!event->selector || !coordinate(value, "x", &event->x) ||
                !coordinate(value, "y", &event->y)) {
                fail(error, size, path, "selector and normalized x/y in0..1");
                goto invalid;
            }
            if (selected == UI_REPLAY_WHEEL) {
                yyjson_val *delta = yyjson_obj_get(value, "delta");
                event->delta = yyjson_get_num(delta);
                if (!yyjson_is_num(delta) || !isfinite(event->delta) ||
                    fabs(event->delta) > 100) {
                    fail(error, size, path, "wheel delta in-100..100");
                    goto invalid;
                }
            }
        } else if (selected == UI_REPLAY_TEXT) {
            event->text = text(value, "text", 16384);
            if (!event->text) {
                fail(error, size, path, "nonempty text up to16KiB");
                goto invalid;
            }
        } else {
            event->key = text(value, "key", 32);
            if (!key_valid(event->key)) {
                fail(error, size, path, "known key");
                goto invalid;
            }
        }
        out->event_count++;
    }
    static const char *const assertion_keys[] = { "frame",  "type", "selector",
                                                  "entity", "name", "equals" };
    previous = 0;
    yyjson_arr_foreach(assertions, i, count, value)
    {
        char path[64];
        snprintf(path, sizeof path, "/assertions/%zu", i);
        ui_replay_assertion *assertion = &out->assertions[out->assertion_count];
        const char *kind = text(value, "type", 16);
        if (!keys(value, assertion_keys, 6) ||
            !frame(value, out->frames, previous, &assertion->frame) || !kind ||
            (strcmp(kind, "data") && strcmp(kind, "visible"))) {
            fail(error, size, path, "data/visible assertion with monotonic frame");
            goto invalid;
        }
        previous = assertion->frame;
        assertion->data = !strcmp(kind, "data");
        assertion->expected = yyjson_obj_get(value, "equals");
        if (assertion->data) {
            assertion->entity = text(value, "entity", 512);
            assertion->name = text(value, "name", 512);
            if (!assertion->entity || !assertion->name ||
                (!yyjson_is_bool(assertion->expected) &&
                 !yyjson_is_num(assertion->expected) &&
                 !yyjson_is_str(assertion->expected)) ||
                (yyjson_is_str(assertion->expected) &&
                 yyjson_get_len(assertion->expected) > 4096)) {
                fail(error, size, path, "entity/name and typed bool/number/string equals");
                goto invalid;
            }
        } else {
            assertion->selector = text(value, "selector", 512);
            if (!assertion->selector || !yyjson_is_bool(assertion->expected)) {
                fail(error, size, path, "selector and boolean equals");
                goto invalid;
            }
        }
        out->assertion_count++;
    }
    return out;
invalid:
    ui_replay_free(out);
    return NULL;
}
void ui_replay_free(ui_replay *replay)
{
    if (replay) {
        yyjson_doc_free(replay->document);
        free(replay);
    }
}
