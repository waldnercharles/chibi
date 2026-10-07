#include "assets/code_source_internal.h"
#include "graph/document/serialize.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <string.h>

static bool location(const av_value *value, int *start, int *end)
{
    if (!value) {
        return false;
    }
    av_source span = av_get_source(value);
    if (span.start == SIZE_MAX || span.end == SIZE_MAX) {
        return false;
    }
    *start = (int)span.start;
    *end = (int)span.end;
    return true;
}
static av_value *item_by_id(const av_value *items, uint64_t id)
{
    for (size_t i = 0; i < av_arr_size(items); i++) {
        av_value *value = av_arr_get(items, i);
        av_value *identity = av_is_obj(value) ? av_obj_get(value, "id") : value;
        if (av_is_int(identity) && av_get_uint(identity) == id) {
            return value;
        }
    }
    return NULL;
}
bool code_source_explicit_location(
    const code_source *source,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id,
    int *start,
    int *end
)
{
    if (!source->document || source->program) {
        return false;
    }
    av_value *root = av_doc_root(source->document);
    av_value *graphs = av_obj_get(root, "graphs");
    av_value *written = NULL;
    for (size_t i = 0; i < av_arr_size(graphs); i++) {
        av_value *candidate = av_arr_get(graphs, i);
        if (av_get_uint(av_obj_get(candidate, "id")) == graph_id) {
            written = candidate;
            break;
        }
    }
    if (!written) {
        return false;
    }
    av_value *record = item_by_id(av_obj_get(written, "nodes"), node_id);
    if (record && pin_id) {
        const graph *g = source->asset
                           ? behavior_asset_find_graph(source->asset, graph_id)
                           : NULL;
        const node *n = g ? graph_node_by_id(g, node_id) : NULL;
        int output, index;
        const pin_spec *pin = n ? graph_pin_by_id(n, pin_id, &output, &index) : NULL;
        if (pin) {
            av_value *side = av_obj_get(record, output ? "outputs" : "inputs");
            av_value *value = NULL;
            if (av_is_arr(side)) {
                value = item_by_id(side, pin_id);
            } else if (av_is_obj(side)) {
                char key[SERIALIZE_KEY_SIZE];
                serialize_pin_key(n, output, index, key, sizeof key);
                value = av_obj_get(side, key);
            }
            if (location(value, start, end)) {
                return true;
            }
        }
    }
    if (location(record, start, end)) {
        return true;
    }
    /* Derived expression bodies may be omitted from persistence; their source
     * field owns compiler diagnostics from the regenerated graph. */
    if (location(av_obj_get(written, "math_expression"), start, end)) {
        return true;
    }
    return location(written, start, end);
}

static code_token *tokens(const char *text)
{
    code_token *result = NULL;
    code_lexer lexer;
    code_lex_init(&lexer, text);
    for (;;) {
        code_token token = code_lex_next(&lexer);
        apush(result, token);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            break;
        }
    }
    return result;
}
static bool token_equal(const char *a, code_token x, const char *b, code_token y)
{
    return x.end - x.start == y.end - y.start &&
           !memcmp(a + x.start, b + y.start, x.end - x.start);
}
/* Resolution only replaces an identifier or its first qualified member with
 * one generated identifier. The rest of the source, including trivia, stays
 * byte-identical. Map errors within a replacement to the whole authored name. */
void code_source_expression_error(
    code_source *source,
    int source_start,
    const char *original,
    const char *resolved,
    int offset,
    int length,
    const char *message
)
{
    code_token *before = tokens(original), *after = tokens(resolved);
    int a = 0, b = 0;
    int start = offset, end = offset + length;
    int mapped_start = -1, mapped_end = -1;
    int previous_before = 0, previous_after = 0;
    while (a < acount(before) && b < acount(after)) {
        code_token x = before[a], y = after[b];
        if (mapped_start < 0 && start < (int)y.start) {
            mapped_start = previous_before + start - previous_after;
        }
        if (mapped_end < 0 && end <= (int)y.start) {
            mapped_end = previous_before + end - previous_after;
        }
        int next_a = a + 1;
        bool replaced = !token_equal(original, x, resolved, y);
        if (replaced && x.kind == CODE_TOKEN_NAME && y.kind == CODE_TOKEN_NAME &&
            a + 2 < acount(before) && before[a + 1].punctuation == '.' &&
            before[a + 2].kind == CODE_TOKEN_NAME) {
            x.end = before[a + 2].end;
            next_a = a + 3;
        }
        if (mapped_start < 0 && start >= (int)y.start && start < (int)y.end) {
            mapped_start = replaced ? (int)x.start
                                    : (int)x.start + start - (int)y.start;
        }
        if (mapped_end < 0 && end > (int)y.start && end <= (int)y.end) {
            mapped_end = replaced ? (int)x.end : (int)x.start + end - (int)y.start;
        }
        previous_before = (int)x.end;
        previous_after = (int)y.end;
        a = next_a;
        b++;
    }
    if (mapped_start < 0) {
        mapped_start = previous_before + start - previous_after;
    }
    if (mapped_end < 0) {
        mapped_end = previous_before + end - previous_after;
    }
    afree(before);
    afree(after);
    code_source_error(source, source_start + mapped_start, source_start + mapped_end, message);
}

static const av_value *malformed_pin(const av_value *pin)
{
    if (!av_is_obj(pin)) {
        return NULL;
    }
    av_value *type = av_obj_get(pin, "type");
    pin_type parsed;
    if (type && !serialize_read_pin_type(type, &parsed)) {
        return type;
    }
    const char *strings[] = { "type_name", "split_type_name", "name", "kind", "display_name" };
    for (size_t i = 0; i < sizeof strings / sizeof *strings; i++) {
        av_value *field = av_obj_get(pin, strings[i]);
        if (field && !av_is_str(field) && !av_is_null(field)) {
            return field;
        }
    }
    return NULL;
}
void code_source_load_error(code_source *source, const char *message)
{
    const av_value *invalid = NULL;
    av_value *graphs = av_obj_get(av_doc_root(source->document), "graphs");
    for (size_t i = 0; !invalid && i < av_arr_size(graphs); i++) {
        av_value *nodes = av_obj_get(av_arr_get(graphs, i), "nodes");
        for (size_t j = 0; !invalid && j < av_arr_size(nodes); j++) {
            av_value *n = av_arr_get(nodes, j);
            av_value *type = av_obj_get(n, "type");
            if (!av_is_obj(n)) {
                invalid = n;
                break;
            }
            if (!av_is_str(type) || !registry_node_by_name(av_get_str(type))) {
                invalid = type ? type : n;
                break;
            }
            for (int output = 0; !invalid && output < 2; output++) {
                av_value *pins = av_obj_get(n, output ? "outputs" : "inputs");
                size_t count = av_is_arr(pins) ? av_arr_size(pins) : av_obj_size(pins);
                for (size_t k = 0; !invalid && k < count; k++) {
                    av_value *pin = av_is_arr(pins) ? av_arr_get(pins, k)
                                                    : av_obj_value_at(pins, k);
                    invalid = malformed_pin(pin);
                }
            }
        }
    }
    int start = 0, end = (int)strlen(source->text);
    location(invalid, &start, &end);
    code_source_error(source, start, end, message);
}
