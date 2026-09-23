#include "editor/commands/quick_edit.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

typedef struct
{
    const char *begin, *end;
} text_span;

typedef struct
{
    yyjson_val *object;
    const char *id;
} target_match;

static const char *json_string(yyjson_val *object, const char *key)
{
    const char *value = yyjson_get_str(yyjson_obj_get(object, key));
    return value ? value : "";
}

static bool word_char(unsigned char value)
{
    return isalnum(value) != 0;
}

static bool equal_fold(const char *left, const char *right)
{
    while (*left && *right &&
           tolower((unsigned char)*left) == tolower((unsigned char)*right)) {
        left++;
        right++;
    }
    return !*left && !*right;
}

static const char *find_phrase(const char *text, const char *phrase)
{
    size_t length = strlen(phrase);
    if (!length) {
        return NULL;
    }
    for (const char *at = text; *at; at++) {
        size_t i = 0;
        while (i < length && at[i] &&
               tolower((unsigned char)at[i]) == tolower((unsigned char)phrase[i])) {
            i++;
        }
        if (i == length && (at == text || !word_char((unsigned char)at[-1])) &&
            !word_char((unsigned char)at[i])) {
            return at;
        }
    }
    return NULL;
}

static bool has_phrase(const char *text, const char *phrase)
{
    return find_phrase(text, phrase) != NULL;
}

static bool has_any(const char *text, const char *const *phrases, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (has_phrase(text, phrases[i])) {
            return true;
        }
    }
    return false;
}

static bool unsafe_request(const char *request)
{
    static const char *const unsafe[] = {
        "and",  "or",     "but",     "unless", "except", "without",
        "if",   "when",   "while",   "every",  "all",    "each",
        "both", "either", "neither", "not",    "don't",  "dont"
    };
    return strchr(request, ',') || strchr(request, ';') ||
           has_phrase(request, "make a copy") ||
           has_phrase(request, "create a copy") ||
           has_phrase(request, "duplicate") || has_phrase(request, "clone") ||
           has_any(request, unsafe, sizeof unsafe / sizeof unsafe[0]);
}

static const char *last_marker(const char *text, const char *marker, int *count)
{
    const char *last = NULL, *at = text;
    size_t size = strlen(marker);
    *count = 0;
    while ((at = find_phrase(at, marker))) {
        last = at + size;
        (*count)++;
        at += size;
    }
    return last;
}

static text_span trimmed_tail(const char *tail)
{
    text_span span = { .begin = tail, .end = tail + strlen(tail) };
    while (span.begin < span.end && isspace((unsigned char)*span.begin)) {
        span.begin++;
    }
    while (span.end > span.begin &&
           (isspace((unsigned char)span.end[-1]) || span.end[-1] == '.' ||
            span.end[-1] == '!' || span.end[-1] == '?')) {
        span.end--;
    }
    return span;
}

static bool span_equal(text_span span, const char *text)
{
    size_t length = (size_t)(span.end - span.begin);
    if (strlen(text) != length) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (tolower((unsigned char)span.begin[i]) != tolower((unsigned char)text[i])) {
            return false;
        }
    }
    return true;
}

static bool span_number(text_span span, double *value)
{
    char buffer[96];
    size_t length = (size_t)(span.end - span.begin);
    if (!length || length >= sizeof buffer) {
        return false;
    }
    memcpy(buffer, span.begin, length);
    buffer[length] = 0;
    char *end;
    double parsed = strtod(buffer, &end);
    if (end == buffer || *end || !isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool object_kind(yyjson_val *object, const char *kind)
{
    const char *value = yyjson_get_str(yyjson_obj_get(object, "kind"));
    return value && equal_fold(value, kind);
}

static bool component_has(yyjson_val *field, const char *name)
{
    const char *component = yyjson_get_str(yyjson_obj_get(field, "component"));
    const char *key = yyjson_get_str(yyjson_obj_get(field, "component_key"));
    return (component && has_phrase(component, name)) ||
           (key && has_phrase(key, name));
}

static bool field_is(yyjson_val *field, const char *name, const char *type)
{
    return equal_fold(json_string(field, "name"), name) &&
           equal_fold(json_string(field, "type"), type);
}

static yyjson_val *find_prototype(yyjson_val *objects, const char *name)
{
    yyjson_val *found = NULL;
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(objects, i, count, object)
    {
        const char *candidate = yyjson_get_str(yyjson_obj_get(object, "name"));
        if (!object_kind(object, "prototype") || !candidate ||
            !equal_fold(candidate, name)) {
            continue;
        }
        const char *found_id = json_string(found, "id");
        const char *candidate_id = json_string(object, "id");
        if (found && strcmp(found_id, candidate_id)) {
            return NULL;
        }
        found = object;
    }
    return found;
}

static bool same_target(target_match left, target_match right)
{
    return left.id && right.id && !strcmp(left.id, right.id);
}

static bool add_target(yyjson_val *objects, yyjson_val *candidate, bool explicit_instance, target_match *target)
{
    if (!object_kind(candidate, "prototype") && !object_kind(candidate, "entity")) {
        return true;
    }
    if (explicit_instance && !object_kind(candidate, "entity")) {
        return true;
    }
    yyjson_val *owner = candidate;
    if (object_kind(candidate, "entity") && !explicit_instance) {
        const char *prototype = yyjson_get_str(yyjson_obj_get(candidate, "prototype"));
        owner = prototype ? find_prototype(objects, prototype) : NULL;
        if (!owner) {
            return false;
        }
    }
    const char *id = yyjson_get_str(yyjson_obj_get(owner, "id"));
    if (!id || !*id) {
        return false;
    }
    target_match match = { .object = owner, .id = id };
    if (target->object && !same_target(*target, match)) {
        return false;
    }
    *target = match;
    return true;
}

static bool resolve_target(
    const char *request,
    yyjson_val *catalog,
    const char *value_marker,
    bool explicit_instance,
    target_match *target
)
{
    yyjson_val *objects = yyjson_obj_get(catalog, "objects");
    size_t i, count;
    yyjson_val *object;
    bool named = false;
    yyjson_arr_foreach(objects, i, count, object)
    {
        if (!object_kind(object, "prototype") && !object_kind(object, "entity")) {
            continue;
        }
        const char *name = yyjson_get_str(yyjson_obj_get(object, "name"));
        const char *at = name ? find_phrase(request, name) : NULL;
        if (!at || (value_marker && at >= value_marker)) {
            continue;
        }
        named = true;
        if (!add_target(objects, object, explicit_instance, target)) {
            return false;
        }
    }
    return named && target->object;
}

static bool has_choice(yyjson_val *field)
{
    return yyjson_is_arr(yyjson_obj_get(field, "choices"));
}

static yyjson_val *field_for_request(
    yyjson_val *fields,
    const char *request,
    bool color,
    bool collider_shape,
    bool visual_width
)
{
    bool border = has_phrase(request, "border"), glow = has_phrase(request, "glow");
    if (border && glow) {
        return NULL;
    }
    yyjson_val *found = NULL;
    size_t i, count;
    yyjson_val *field;
    yyjson_arr_foreach(fields, i, count, field)
    {
        const char *name = json_string(field, "name");
        const char *label = json_string(field, "label");
        bool matches = color
                         ? equal_fold(json_string(field, "type"), "color") &&
                               (border ? has_phrase(name, "border")
                                : glow ? has_phrase(name, "glow")
                                       : equal_fold(name, "color") ||
                                             has_phrase(label, "appearance color"))
                     : collider_shape ? field_is(field, "shape", "choice") &&
                                            component_has(field, "GameCollider") &&
                                            has_choice(field)
                     : visual_width
                         ? field_is(field, "width", "number") &&
                               component_has(field, "GameAppearance")
                         : equal_fold(json_string(field, "type"), "number") &&
                               !equal_fold(name, "width") &&
                               !equal_fold(name, "shape") &&
                               (has_phrase(request, name) ||
                                has_phrase(request, label));
        if (!matches) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = field;
    }
    return found;
}

static yyjson_val *choice_for_span(yyjson_val *field, text_span span)
{
    static const char *const articles[] = { "a ", "an ", "the " };
    for (size_t i = 0; i < sizeof articles / sizeof articles[0]; i++) {
        const char *article = articles[i];
        size_t length = strlen(article);
        if ((size_t)(span.end - span.begin) <= length) {
            continue;
        }
        size_t j = 0;
        while (j < length && tolower((unsigned char)span.begin[j]) == article[j]) {
            j++;
        }
        if (j == length) {
            span.begin += length;
            break;
        }
    }
    yyjson_val *found = NULL;
    yyjson_val *choices = yyjson_obj_get(field, "choices");
    size_t i, count;
    yyjson_val *choice;
    yyjson_arr_foreach(choices, i, count, choice)
    {
        const char *label = yyjson_get_str(yyjson_obj_get(choice, "label"));
        if (!label || !span_equal(span, label)) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = choice;
    }
    return found;
}

static bool hex_color(text_span span, uint64_t *value, int *digits)
{
    if (span.begin == span.end || *span.begin != '#') {
        return false;
    }
    size_t length = (size_t)(span.end - span.begin);
    if (length != 7 && length != 9) {
        return false;
    }
    uint64_t parsed = 0;
    for (size_t i = 1; i < length; i++) {
        unsigned char ch = (unsigned char)span.begin[i];
        int digit = isdigit(ch) ? ch - '0'
                  : tolower(ch) >= 'a' && tolower(ch) <= 'f' ? tolower(ch) - 'a' + 10
                                                             : -1;
        if (digit < 0) {
            return false;
        }
        parsed = (parsed << 4) | (unsigned)digit;
    }
    *digits = (int)(length - 1);
    *value = parsed;
    return true;
}

static bool color_word(text_span span, uint32_t *rgb)
{
    static const struct
    {
        const char *name;
        uint32_t rgb;
    } colors[] = { { "black", 0x000000 }, { "white", 0xffffff },
                   { "red", 0xff0000 },   { "green", 0x00ff00 },
                   { "blue", 0x0000ff },  { "yellow", 0xffff00 },
                   { "cyan", 0x00ffff },  { "magenta", 0xff00ff },
                   { "gray", 0x808080 },  { "grey", 0x808080 } };
    for (size_t i = 0; i < sizeof colors / sizeof colors[0]; i++) {
        if (span_equal(span, colors[i].name)) {
            *rgb = colors[i].rgb;
            return true;
        }
    }
    return false;
}

static bool edit_value(
    const char *request,
    yyjson_val *field,
    const char *to_tail,
    const char *by_tail,
    bool has_to,
    bool has_by,
    yyjson_mut_doc *doc,
    yyjson_mut_val *operation
)
{
    text_span value_span = trimmed_tail(has_to ? to_tail : by_tail);
    yyjson_val *choice = choice_for_span(field, value_span);
    if (choice) {
        yyjson_val *value = yyjson_obj_get(choice, "value");
        if (!value) {
            return false;
        }
        yyjson_mut_obj_add_val(doc, operation, "value", yyjson_val_mut_copy(doc, value));
        return true;
    }

    if (equal_fold(json_string(field, "type"), "color")) {
        if (!has_to || has_by) {
            return false;
        }
        uint64_t packed;
        int digits;
        if (hex_color(value_span, &packed, &digits)) {
            if (digits == 6) {
                yyjson_val *old = yyjson_obj_get(field, "value");
                if (!yyjson_is_num(old)) {
                    return false;
                }
                packed = (packed << 8) | (yyjson_get_uint(old) & 255u);
            }
            yyjson_mut_obj_add_uint(doc, operation, "value", packed);
            return true;
        }
        uint32_t rgb;
        yyjson_val *old = yyjson_obj_get(field, "value");
        if (!color_word(value_span, &rgb) || !yyjson_is_num(old)) {
            return false;
        }
        packed = ((uint64_t)rgb << 8) | (yyjson_get_uint(old) & 255u);
        yyjson_mut_obj_add_uint(doc, operation, "value", packed);
        return true;
    }

    double number;
    if (!span_number(value_span, &number)) {
        return false;
    }
    if (has_by) {
        static const char *const increases[] = { "increase", "raise", "grow", "enlarge" };
        static const char *const decreases[] = { "decrease", "lower", "reduce", "shrink" };
        bool increase = has_any(request, increases, sizeof increases / sizeof increases[0]);
        bool decrease = has_any(request, decreases, sizeof decreases / sizeof decreases[0]);
        if (increase == decrease) {
            return false;
        }
        yyjson_val *old = yyjson_obj_get(field, "value");
        if (!yyjson_is_num(old)) {
            return false;
        }
        number = yyjson_get_num(old) + (increase ? number : -number);
        if (!isfinite(number)) {
            return false;
        }
    }
    bool integer = yyjson_get_bool(yyjson_obj_get(field, "integer"));
    if (integer && floor(number) != number) {
        return false;
    }
    if (integer) {
        if (number < -9223372036854775808.0 || number >= 9223372036854775808.0) {
            return false;
        }
        yyjson_mut_obj_add_int(doc, operation, "value", (int64_t)number);
    } else {
        yyjson_mut_obj_add_real(doc, operation, "value", number);
    }
    return true;
}

static bool has_capability(yyjson_val *catalog, const char *name)
{
    yyjson_val *capabilities = yyjson_obj_get(catalog, "capabilities");
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(capabilities, i, count, value)
    {
        const char *text = yyjson_get_str(value);
        if (text && !strcmp(text, name)) {
            return true;
        }
    }
    return false;
}

char *editor_command_quick_edit(const char *packet_json)
{
    if (!packet_json) {
        return NULL;
    }
    yyjson_doc *packet_doc = yyjson_read(packet_json, strlen(packet_json), 0);
    yyjson_val *packet = packet_doc ? yyjson_doc_get_root(packet_doc) : NULL;
    yyjson_val *catalog = yyjson_obj_get(packet, "catalog");
    const char *request = yyjson_get_str(yyjson_obj_get(packet, "command"));
    const char *revision = yyjson_get_str(yyjson_obj_get(catalog, "revision"));
    const char *document = yyjson_get_str(yyjson_obj_get(catalog, "document"));
    if (!yyjson_is_obj(packet) || !yyjson_is_obj(catalog) || !request ||
        !*request || !revision || !*revision || !document || !*document ||
        !yyjson_is_arr(yyjson_obj_get(catalog, "objects")) ||
        !has_capability(catalog, "set") || unsafe_request(request)) {
        yyjson_doc_free(packet_doc);
        return NULL;
    }

    int to_count, by_count;
    const char *to_tail = last_marker(request, "to", &to_count);
    const char *by_tail = last_marker(request, "by", &by_count);
    if (to_count > 1 || by_count > 1 || (to_count && by_count)) {
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    const char *value_marker = to_count ? to_tail : by_count ? by_tail : NULL;
    bool explicit_instance = has_phrase(request, "instance") ||
                             has_phrase(request, "copy") ||
                             has_phrase(request, "itself") ||
                             has_phrase(request, "own") ||
                             has_phrase(request, "in this scene") ||
                             has_phrase(request, "in the scene") ||
                             has_phrase(request, "only this") ||
                             has_phrase(request, "just this") ||
                             has_phrase(request, "this entity") ||
                             has_phrase(request, "this object");
    if (explicit_instance && has_phrase(request, "prototype")) {
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    target_match target = { 0 };
    if (!resolve_target(request, catalog, value_marker, explicit_instance, &target)) {
        yyjson_doc_free(packet_doc);
        return NULL;
    }

    static const char *const colors[] = { "color", "colour" };
    static const char *const collider[] = { "collider", "collision", "hitbox" };
    static const char *const shapes[] = { "shape", "circle", "round" };
    bool color = has_any(request, colors, sizeof colors / sizeof colors[0]);
    bool collision = has_any(request, collider, sizeof collider / sizeof collider[0]);
    bool collider_shape = collision &&
                          has_any(request, shapes, sizeof shapes / sizeof shapes[0]);
    bool width = has_phrase(request, "width");
    bool visual_width = width && (has_phrase(request, "visual") ||
                                  has_phrase(request, "appearance"));
    if ((int)color + (int)collider_shape + (int)width > 1 ||
        (width && !visual_width) || (collision && !collider_shape) ||
        (has_phrase(request, "shape") && !collider_shape) ||
        (!to_count && !by_count) || (color && by_count) ||
        (collider_shape && by_count) || (width && !to_count && !by_count)) {
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    if (!color && !collider_shape && !visual_width &&
        !has_any(
            request,
            (
                const char *const[]
            ){ "set", "change", "make", "turn", "increase", "raise", "grow", "enlarge", "decrease", "lower", "reduce", "shrink" },
            12
        )) {
        yyjson_doc_free(packet_doc);
        return NULL;
    }

    yyjson_val *fields = yyjson_obj_get(target.object, "fields");
    yyjson_val *field = field_for_request(fields, request, color, collider_shape, visual_width);
    if (!field || (!has_choice(field) && !equal_fold(json_string(field, "type"), "color") &&
                   !equal_fold(json_string(field, "type"), "number"))) {
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    text_span value_span = trimmed_tail(to_count ? to_tail : by_tail);
    if (has_phrase(request, "percent") || has_phrase(request, "percentage") ||
        has_phrase(request, "quarter") || has_phrase(request, "half") ||
        has_phrase(request, "twice") || has_phrase(request, "double") ||
        has_phrase(request, "triple") || has_phrase(request, "pixel") ||
        memchr(value_span.begin, '%', (size_t)(value_span.end - value_span.begin))) {
        yyjson_doc_free(packet_doc);
        return NULL;
    }

    yyjson_mut_doc *result_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(result_doc);
    yyjson_mut_doc_set_root(result_doc, result);
    yyjson_mut_obj_add_bool(result_doc, result, "ok", true);
    yyjson_mut_obj_add_str(result_doc, result, "provider", "quick_edit");
    yyjson_mut_obj_add_str(result_doc, result, "status", "needs_review");
    yyjson_mut_obj_add_str(result_doc, result, "message", "Review changes");
    yyjson_mut_obj_add_strcpy(result_doc, result, "revision", revision);
    yyjson_mut_val *operations = yyjson_mut_obj_add_arr(result_doc, result, "operations");
    yyjson_mut_val *operation = yyjson_mut_arr_add_obj(result_doc, operations);
    yyjson_mut_obj_add_str(result_doc, operation, "op", "set");
    yyjson_mut_obj_add_strcpy(result_doc, operation, "target", target.id);
    const char *field_id = yyjson_get_str(yyjson_obj_get(field, "id"));
    if (!field_id || !*field_id) {
        yyjson_mut_doc_free(result_doc);
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    yyjson_mut_obj_add_strcpy(result_doc, operation, "field", field_id);
    if (!edit_value(request, field, to_tail, by_tail, to_count != 0, by_count != 0, result_doc, operation)) {
        yyjson_mut_doc_free(result_doc);
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    yyjson_mut_val *context = yyjson_mut_obj_add_obj(result_doc, result, "context");
    yyjson_mut_obj_add_strcpy(result_doc, context, "document", document);
    yyjson_mut_obj_add_str(result_doc, context, "operation", "set");
    yyjson_mut_val *targets = yyjson_mut_obj_add_arr(result_doc, context, "targets");
    yyjson_mut_arr_add_strcpy(result_doc, targets, target.id);
    char *output = yyjson_mut_write(result_doc, 0, NULL);
    yyjson_mut_doc_free(result_doc);
    yyjson_doc_free(packet_doc);
    return output;
}
