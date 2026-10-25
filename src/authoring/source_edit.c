#include "authoring/source_edit_internal.h"
#include "authoring/source_literal.h"
#include "authoring/source_presentation_details.h"
#include "support/array.h"
#include "support/text_edit.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_EDIT_BYTES = 512 * 1024,
    MAX_EXPRESSION_BYTES = 64 * 1024
};

static char *fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return NULL;
}
uint64_t authoring_source_edit_generation(const authoring_source_presentation *presentation)
{
    return script_catalog_id("source-edit-generation", authoring_source_presentation_text(presentation));
}
static const source_presentation_origin *origin(
    const authoring_source_presentation *presentation,
    uint64_t identity
)
{
    for (int i = 0; i < authoring_source_presentation_origin_count(presentation); i++) {
        const source_presentation_origin
            *o = authoring_source_presentation_origin_at(presentation, i);
        if (o->identity == identity) {
            return o;
        }
    }
    return NULL;
}
const source_presentation_origin *source_edit_statement_origin(
    const authoring_source_presentation *presentation,
    const source_presentation_origin *selected
)
{
    if (!selected || selected->pin || !selected->node ||
        selected->kind != SOURCE_PRESENTATION_EXPRESSION) {
        return selected;
    }
    for (int i = 0; i < authoring_source_presentation_origin_count(presentation); i++) {
        const source_presentation_origin
            *o = authoring_source_presentation_origin_at(presentation, i);
        if (!o->pin && o->kind == SOURCE_PRESENTATION_STATEMENT &&
            o->graph == selected->graph && o->node == selected->node) {
            return o;
        }
    }
    return selected;
}
static char *slice(const char *text, script_range range)
{
    size_t size = (size_t)(range.end - range.start);
    char *out = xmalloc(size + 1);
    memcpy(out, text + range.start, size);
    out[size] = '\0';
    return out;
}
static bool valid_range(const char *text, script_range range)
{
    return range.start >= 0 && range.end > range.start &&
           (size_t)range.end <= strlen(text);
}
typedef struct
{
    const script_expression *expression;
    const script_function *payload;
    const script_catalog_capability *event;
    int count;
    bool fields;
} result_schema;
static result_schema schema(const script_analysis *a, const script_catalog *catalog, int index)
{
    result_schema out = { 0 };
    for (int depth = 0; index >= 0 && index < a->expression_count && depth < 256;
         depth++) {
        const script_expression *e = &a->expressions[index];
        if (e->type.flags & SCRIPT_TYPE_RESULTS) {
            out.fields = true;
            if (e->symbol >= 0 && e->symbol < a->symbol_count &&
                a->symbols[e->symbol].kind == SCRIPT_PARAMETER) {
                int f = a->symbols[e->symbol].function;
                if (f >= 0 && f < a->function_count) {
                    out.payload = &a->functions[f];
                    out.count = out.payload->parameter_count;
                    return out;
                }
            }
            out.event = script_catalog_capability_by_id(catalog, e->type.nominal);
            if (out.event && out.event->kind == SCRIPT_CAPABILITY_EVENT) {
                out.count = out.event->parameter_count;
                return out;
            }
            out.event = NULL;
        }
        if (e->result_count) {
            out.expression = e;
            out.count = e->result_count;
            out.fields |= e->result_count > 1;
            return out;
        }
        if (e->kind != SCRIPT_EXPR_SYMBOL || e->symbol < 0 ||
            e->symbol >= a->symbol_count) {
            out.count = e->type.storage == PIN_FLOW ? 0 : 1;
            return out;
        }
        index = a->symbols[e->symbol].initializer;
        if (index < 0) {
            out.count = e->type.storage == PIN_FLOW ? 0 : 1;
        }
    }
    return out;
}
static const char *result_name(
    const script_analysis *a,
    const script_catalog *catalog,
    result_schema s,
    int index
)
{
    if (s.payload) {
        return a->symbols[s.payload->parameters[index]].name;
    }
    if (s.event) {
        return s.event->parameters[index].name;
    }
    const script_expression *e = s.expression;
    if (!e) {
        return NULL;
    }
    const script_catalog_capability *c = script_catalog_capability_by_id(catalog, e->capability);
    if (c && index < c->result_count) {
        return c->results[index].name;
    }
    if (e->kind == SCRIPT_EXPR_CALL && e->right >= 0 && e->right < a->function_count) {
        const script_function *f = &a->functions[e->right];
        if (index < f->result_count) {
            return a->symbols[f->results[index]].name;
        }
    }
    return NULL;
}
typedef source_edit_connection connection;
static connection connect_value(
    const script_analysis *analysis,
    const script_catalog *catalog,
    const source_presentation_origin *producer,
    int result,
    char *error,
    size_t size
)
{
    connection out = { 0 };
    const script_symbol *declaration = producer->kind == SOURCE_PRESENTATION_SYMBOL
                                         ? &analysis->symbols[producer->record]
                                         : NULL;
    int index = declaration ? declaration->initializer : producer->record;
    const script_expression *e = index >= 0 ? &analysis->expressions[index] : NULL;
    result_schema results = schema(analysis, catalog, index);
    if (declaration && index < 0) {
        results.count = 1;
    }
    if (declaration && declaration->type.flags & SCRIPT_TYPE_RESULTS) {
        results.fields = true;
        if (index < 0 && declaration->kind == SCRIPT_PARAMETER &&
            declaration->function >= 0 &&
            declaration->function < analysis->function_count) {
            results.payload = &analysis->functions[declaration->function];
            results.count = results.payload->parameter_count;
        } else if (index < 0) {
            results.event = script_catalog_capability_by_id(
                catalog,
                declaration->type.nominal
            );
            if (results.event && results.event->kind == SCRIPT_CAPABILITY_EVENT) {
                results.count = results.event->parameter_count;
            } else {
                results.event = NULL;
            }
        }
    }
    int count = results.count;
    if (result < 0 || result >= count) {
        fail(error, size, "Producer has no selected data result");
        return out;
    }
    out.type = results.payload
                 ? analysis->symbols[results.payload->parameters[result]].type
             : results.event      ? results.event->parameters[result].type
             : results.expression ? results.expression->results[result]
             : declaration        ? declaration->type
                                  : e->type;
    const script_symbol *named = declaration;
    if (!named && e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0) {
        named = &analysis->symbols[e->symbol];
    } else if (!named) {
        for (int i = 0; i < analysis->symbol_count; i++) {
            const script_symbol *symbol = &analysis->symbols[i];
            if ((symbol->kind == SCRIPT_LOCAL || symbol->kind == SCRIPT_ACTION) &&
                symbol->initializer == producer->record) {
                named = symbol;
                break;
            }
        }
    }
    unsigned effects = e ? e->effects : 0;
    if (results.expression) {
        effects |= results.expression->effects;
    }
    if (!named && ((e && e->awaited) ||
                   effects & (SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL | SCRIPT_EFFECT_HOST |
                              SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_RANDOM))) {
        fail(error, size, "Name this producer's result before connecting it; the connection would evaluate it twice");
        return out;
    }
    char *value = named ? xstrdup(named->name)
                        : slice(analysis->text, producer->range);
    const char *field = results.fields
                          ? result_name(analysis, catalog, results, result)
                          : NULL;
    if (results.fields && !field) {
        free(value);
        fail(error, size, "Selected tuple result has no declared source name");
        return out;
    }
    size_t bytes = strlen(value) + (field ? strlen(field) + 1 : 0) + 3;
    out.text = xmalloc(bytes);
    if (named) {
        snprintf(out.text, bytes, field ? "%s.%s" : "%s", value, field);
        out.symbol = named->id;
    } else {
        snprintf(out.text, bytes, field ? "(%s).%s" : "(%s)", value, field);
        out.prefix = 1;
        out.inline_value = true;
    }
    free(value);
    return out;
}
source_edit_connection source_edit_producer(
    const script_analysis *analysis,
    const script_catalog *catalog,
    const authoring_source_presentation *presentation,
    uint64_t identity,
    int result,
    char *error,
    size_t size
)
{
    const source_presentation_origin *producer = origin(presentation, identity);
    if (!authoring_source_presentation_matches(presentation, analysis, catalog) ||
        !producer || producer->record < 0 ||
        !valid_range(analysis->text, producer->range) ||
        (producer->kind != SOURCE_PRESENTATION_EXPRESSION &&
         producer->kind != SOURCE_PRESENTATION_SYMBOL) ||
        (producer->kind == SOURCE_PRESENTATION_EXPRESSION &&
         producer->record >= analysis->expression_count) ||
        (producer->kind == SOURCE_PRESENTATION_SYMBOL &&
         producer->record >= analysis->symbol_count)) {
        fail(error, size, "Connection has no current producer expression");
        return (source_edit_connection){ 0 };
    }
    return connect_value(analysis, catalog, producer, result, error, size);
}
static bool comment(TSNode node)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), "comment");
}
static int line_start(const char *text, int offset)
{
    while (offset && text[offset - 1] != '\n') {
        offset--;
    }
    return offset;
}
static bool spaces(const char *text, int start, int end)
{
    for (int i = start; i < end; i++) {
        if (!isspace((unsigned char)text[i])) {
            return false;
        }
    }
    return true;
}
script_range source_edit_whole_statement(const script_syntax *syntax, script_range range)
{
    TSNode node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(syntax->tree),
        (uint32_t)range.start,
        (uint32_t)range.end
    );
    while (!ts_node_is_null(node) && (int)ts_node_start_byte(node) != range.start) {
        node = ts_node_parent(node);
    }
    TSNode statement = node;
    TSNode previous = ts_node_prev_named_sibling(node);
    while (comment(previous) &&
           spaces(syntax->text, (int)ts_node_end_byte(previous), range.start)) {
        int start = (int)ts_node_start_byte(previous);
        if (!spaces(syntax->text, line_start(syntax->text, start), start)) {
            break;
        }
        range.start = start;
        node = previous;
        previous = ts_node_prev_named_sibling(previous);
    }
    int start = line_start(syntax->text, range.start);
    if (spaces(syntax->text, start, range.start)) {
        range.start = start;
    }
    TSNode next = ts_node_next_named_sibling(statement);
    if (comment(next) &&
        ts_node_start_point(next).row == ts_node_end_point(statement).row) {
        range.end = (int)ts_node_end_byte(next);
    }
    int end = range.end;
    while (syntax->text[end] && syntax->text[end] != '\n' &&
           isspace((unsigned char)syntax->text[end])) {
        end++;
    }
    if (syntax->text[end] == '\n') {
        range.end = end + 1;
    }
    return range;
}
static int parent(const script_analysis *analysis, int child)
{
    for (int i = 0; i < analysis->statement_count; i++) {
        const script_statement *statement = &analysis->statements[i];
        if (statement->kind != SCRIPT_STMT_BLOCK &&
            statement->kind != SCRIPT_STMT_SEQUENCE) {
            continue;
        }
        for (int j = 0; j < statement->child_count; j++) {
            if (statement->children[j] == child) {
                return i;
            }
        }
    }
    return -1;
}
static bool same_symbol(
    const script_analysis *old,
    const script_analysis *current,
    const script_use *use,
    int offset
)
{
    const script_use *resolved = script_use_at(current, offset);
    return resolved && use->symbol >= 0 && resolved->symbol >= 0 &&
           old->symbols[use->symbol].id == current->symbols[resolved->symbol].id &&
           use->member == resolved->member;
}
static char *checked(
    const script_analysis *analysis,
    const script_catalog *catalog,
    char *text,
    const connection *value,
    const source_presentation_origin *producer,
    int destination,
    char *error,
    size_t size
)
{
    if (!text) {
        return fail(error, size, "Source edit ranges overlap");
    }
    script_syntax *syntax = script_parse(text);
    script_analysis
        *candidate = script_analyze_document(syntax, catalog, analysis->identity);
    bool valid = candidate->valid;
    if (!valid) {
        fail(
            error,
            size,
            candidate->diagnostic_count ? candidate->diagnostics[0].message : "Edited source is invalid"
        );
    }
    if (valid && value && value->symbol) {
        const script_use *use = script_use_at(candidate, destination);
        valid = use && use->symbol >= 0 &&
                candidate->symbols[use->symbol].id == value->symbol;
    }
    if (valid && value && value->inline_value) {
        for (int i = 0; valid && i < analysis->use_count; i++) {
            const script_use *use = &analysis->uses[i];
            if (use->range.start >= producer->range.start &&
                use->range.end <= producer->range.end && use->symbol >= 0) {
                valid = same_symbol(
                    analysis,
                    candidate,
                    use,
                    destination + value->prefix + use->range.start -
                        producer->range.start
                );
            }
        }
    }
    if (!valid && candidate->valid) {
        fail(error, size, "Connection would resolve a producer name to a different declaration in this scope");
    }
    script_analysis_free(candidate);
    script_syntax_free(syntax);
    if (!valid) {
        free(text);
        return NULL;
    }
    if (size) {
        error[0] = '\0';
    }
    return text;
}
char *source_edit_check_connection(
    const script_analysis *analysis,
    const script_catalog *catalog,
    char *text,
    const source_edit_connection *value,
    const source_presentation_origin *producer,
    int destination,
    char *error,
    size_t size
)
{
    return checked(analysis, catalog, text, value, producer, destination, error, size);
}
bool source_edit_single_expression(const char *text)
{
    static const char prefix[] = "__Edit :: behavior { __candidate := ";
    size_t length = strlen(text);
    char *probe = xmalloc(sizeof prefix + length + 4);
    snprintf(probe, sizeof prefix + length + 4, "%s%s; }", prefix, text);
    script_syntax *syntax = script_parse(probe);
    TSNode root = ts_tree_root_node(syntax->tree);
    TSNode node = ts_node_named_descendant_for_byte_range(
        root,
        sizeof prefix - 1,
        (uint32_t)(sizeof prefix - 1 + length)
    );
    while (!ts_node_is_null(node) && strcmp(ts_node_type(node), "state_declaration")) {
        node = ts_node_parent(node);
    }
    if (ts_node_is_null(node)) {
        script_syntax_free(syntax);
        free(probe);
        return false;
    }
    TSNode expression = ts_node_child_by_field_name(node, "value", 5);
    int start = (int)sizeof prefix - 1, end = start + (int)length;
    while (start < end && isspace((unsigned char)probe[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)probe[end - 1])) {
        end--;
    }
    bool valid = !syntax->diagnostic_count && !ts_node_is_null(expression) &&
                 (int)ts_node_start_byte(expression) == start &&
                 (int)ts_node_end_byte(expression) == end;
    script_syntax_free(syntax);
    free(probe);
    return valid;
}
static char *edit_text(
    const script_analysis *analysis,
    const script_catalog *catalog,
    const authoring_source_presentation *presentation,
    const authoring_edit_request *request,
    char *error,
    size_t size
)
{
    assert(analysis && catalog && presentation && request);
    if (!analysis->valid ||
        request->generation != authoring_source_edit_generation(presentation) ||
        !authoring_source_presentation_matches(presentation, analysis, catalog)) {
        return fail(error, size, "Source changed; refresh this gesture's semantic generation");
    }
    if (strlen(analysis->text) > MAX_EDIT_BYTES) {
        return fail(error, size, "Source edit exceeds the 512 KiB document limit");
    }
    const source_presentation_origin *target = origin(presentation, request->origin);
    if (!target || !valid_range(analysis->text, target->range)) {
        return fail(error, size, "Gesture has no current editable source origin");
    }
    if (request->kind == AUTHORING_EDIT_EXPRESSION ||
        request->kind == AUTHORING_EDIT_CONNECT) {
        if (target->kind != SOURCE_PRESENTATION_EXPRESSION ||
            target->record < 0 || target->record >= analysis->expression_count) {
            return fail(error, size, "Select a value expression to edit this input");
        }
        connection value = { 0 };
        const source_presentation_origin *producer = NULL;
        if (request->kind == AUTHORING_EDIT_CONNECT) {
            producer = origin(presentation, request->producer);
            if (!producer || producer->record < 0 ||
                !valid_range(analysis->text, producer->range) ||
                (producer->kind != SOURCE_PRESENTATION_EXPRESSION &&
                 producer->kind != SOURCE_PRESENTATION_SYMBOL) ||
                (producer->kind == SOURCE_PRESENTATION_EXPRESSION &&
                 producer->record >= analysis->expression_count) ||
                (producer->kind == SOURCE_PRESENTATION_SYMBOL &&
                 producer->record >= analysis->symbol_count)) {
                return fail(error, size, "Connection has no current producer expression");
            }
            value = connect_value(analysis, catalog, producer, request->producer_result, error, size);
            if (!value.text) {
                return NULL;
            }
        } else if (!request->expression || strlen(request->expression) > MAX_EXPRESSION_BYTES) {
            return fail(error, size, "Provide an expression within the 64 KiB edit limit");
        }
        if (request->kind == AUTHORING_EDIT_EXPRESSION &&
            !source_edit_single_expression(request->expression)) {
            return fail(error, size, "Provide exactly one value expression");
        }
        text_edit edit = { target->range.start,
                           target->range.end,
                           value.text ? value.text : request->expression };
        char *text = text_edits_apply(analysis->text, &edit, 1);
        char *out = checked(
            analysis,
            catalog,
            text,
            &value,
            producer,
            target->range.start,
            error,
            size
        );
        free(value.text);
        return out;
    }
    if (target->kind != SOURCE_PRESENTATION_STATEMENT || target->record < 0 ||
        target->record >= analysis->statement_count ||
        parent(analysis, target->record) < 0) {
        return fail(error, size, "Select a whole statement in a block or sequence");
    }
    script_syntax *syntax = script_parse(analysis->text);
    script_range from = source_edit_whole_statement(syntax, target->range);
    text_edit edits[2] = { { from.start, from.end, "" } };
    int count = 1;
    char *moved = NULL;
    if (request->kind != AUTHORING_EDIT_DELETE_STATEMENT) {
        const source_presentation_origin *sibling = origin(presentation, request->producer);
        if (!sibling || sibling->kind != SOURCE_PRESENTATION_STATEMENT ||
            sibling->record == target->record || sibling->record < 0 ||
            sibling->record >= analysis->statement_count ||
            parent(analysis, sibling->record) != parent(analysis, target->record)) {
            script_syntax_free(syntax);
            return fail(error, size, "Move statements only within the same block or sequence");
        }
        if (request->kind != AUTHORING_EDIT_MOVE_BEFORE &&
            request->kind != AUTHORING_EDIT_MOVE_AFTER) {
            script_syntax_free(syntax);
            return fail(error, size, "Unknown source gesture");
        }
        script_range to = source_edit_whole_statement(syntax, sibling->range);
        int at = request->kind == AUTHORING_EDIT_MOVE_BEFORE ? to.start : to.end;
        if (at > from.start && at < from.end) {
            script_syntax_free(syntax);
            return fail(error, size, "Attached statement comments overlap this move");
        }
        moved = slice(analysis->text, from);
        edits[1] = (text_edit){ at, at, moved };
        count = 2;
    }
    char *text = text_edits_apply(analysis->text, edits, count);
    free(moved);
    script_syntax_free(syntax);
    return checked(analysis, catalog, text, NULL, NULL, 0, error, size);
}

typedef struct
{
    int start, end, length;
} offset_edit;
static bool remap_range(script_range *range, const offset_edit *edits, int count, script_range moved, int destination)
{
    if (moved.end > moved.start && range->start >= moved.start &&
        range->end <= moved.end) {
        int start = destination > moved.end ? destination - (moved.end - moved.start)
                                            : destination;
        range->end += start - moved.start;
        range->start += start - moved.start;
        return true;
    }
    int start = range->start, end = range->end;
    for (int i = 0; i < count; i++) {
        offset_edit e = edits[i];
        int delta = e.length - (e.end - e.start);
        if (e.start == e.end) {
            if (range->start >= e.start) {
                start += delta;
            }
            if (range->end > e.start) {
                end += delta;
            }
            continue;
        }
        if (range->start == e.start && range->end == e.end && e.length) {
            end += delta;
            continue;
        }
        if (range->start >= e.end) {
            start += delta;
        } else if (range->start >= e.start) {
            return false;
        }
        if (range->end >= e.end) {
            end += delta;
        } else if (range->end > e.start) {
            return false;
        }
    }
    range->start = start;
    range->end = end;
    return end > start;
}
static bool remap_candidate(
    const source_presentation_origin *before,
    const source_presentation_origin *after,
    script_range range
)
{
    return after->node && before->kind == after->kind &&
           before->owner == after->owner && before->output == after->output &&
           before->flow == after->flow && before->port_index == after->port_index &&
           !!before->pin == !!after->pin && range.start == after->range.start &&
           range.end == after->range.end;
}
static bool remap_claimed(const source_presentation_identity_map *map, uint64_t identity)
{
    for (int i = 0; i < acount(map); i++) {
        if (map[i].current == identity) {
            return true;
        }
    }
    return false;
}
static bool remap_layout(
    const authoring_source_presentation *old,
    authoring_source_presentation *current,
    const offset_edit *edits,
    int count,
    script_range moved,
    int destination
)
{
    source_presentation_identity_map *map = NULL;
    for (int i = 0; i < authoring_source_presentation_origin_count(old); i++) {
        const source_presentation_origin *before = authoring_source_presentation_origin_at(old, i);
        script_range range = before->range;
        if (!before->node || !remap_range(&range, edits, count, moved, destination)) {
            continue;
        }
        script_range node_range = range;
        if (before->pin) {
            const source_presentation_origin *node = authoring_source_presentation_origin(
                old,
                before->graph,
                before->node,
                0
            );
            assert(node);
            node_range = node->range;
            if (!remap_range(&node_range, edits, count, moved, destination)) {
                continue;
            }
        }
        bool same_role = false;
        if (!before->pin) {
            for (int j = 0; j < authoring_source_presentation_origin_count(current);
                 j++) {
                const source_presentation_origin
                    *after = authoring_source_presentation_origin_at(current, j);
                if (remap_candidate(before, after, range) &&
                    before->display_class == after->display_class) {
                    same_role = true;
                    break;
                }
            }
        }
        for (int j = 0; j < authoring_source_presentation_origin_count(current); j++) {
            const source_presentation_origin
                *after = authoring_source_presentation_origin_at(current, j);
            if (!remap_candidate(before, after, range) ||
                remap_claimed(map, after->identity)) {
                continue;
            }
            if (before->pin) {
                const source_presentation_origin *node = authoring_source_presentation_origin(
                    current,
                    after->graph,
                    after->node,
                    0
                );
                assert(node);
                if (before->node_kind != after->node_kind ||
                    (!before->output && before->display_class != after->display_class) ||
                    node_range.start != node->range.start ||
                    node_range.end != node->range.end) {
                    continue;
                }
            }
            const graph *from = behavior_asset_find_graph(
                authoring_source_presentation_view(old),
                before->graph
            );
            graph *to = behavior_asset_find_graph(
                authoring_source_presentation_view(current),
                after->graph
            );
            const node *a = graph_node_by_id(from, before->node);
            node *b = graph_node_by_id(to, after->node);
            if (!a || !b) {
                break;
            }
            /* Prefer the same role when reads and writes share a range. An
             * expression replacement can change role, but Entry/Return cannot. */
            if (!before->pin &&
                (same_role || before->kind == SOURCE_PRESENTATION_FUNCTION) &&
                a->desc->display_class != b->desc->display_class) {
                continue;
            }
            apush(
                map,
                ((source_presentation_identity_map){ before->identity, after->identity })
            );
            if (before->pin) {
                if (!before->flow && after->pin && before->port_index >= 0 &&
                    before->port_index <
                        (before->output ? a->output_count : a->input_count) &&
                    after->port_index >= 0 &&
                    after->port_index <
                        (after->output ? b->output_count : b->input_count)) {
                    const pin_spec *pin = before->output
                                            ? &a->output_pins[before->port_index]
                                            : &a->input_pins[before->port_index];
                    int status = graph_set_pin_watch(
                        to,
                        b,
                        after->output,
                        after->port_index,
                        (pin->flags & PIN_FLAG_WATCH) != 0
                    );
                    assert(!status);
                    (void)status;
                }
            } else if (!after->pin) {
                b->x = a->x;
                b->y = a->y;
                b->placed = a->placed;
                b->presentation = a->presentation;
                b->breakpoint = a->breakpoint;
                b->breakpoint_enabled = a->breakpoint_enabled;
                b->show_advanced = a->show_advanced;
                graph_set_node_comment(to, b, a->comment, a->comment_hidden);
            }
            break;
        }
    }
    bool carried = source_presentation_details_carry(old, current, map, acount(map));
    afree(map);
    return carried;
}
void authoring_edit_result_free(authoring_edit_result *result)
{
    free(result->text);
    free(result->metadata);
    *result = (authoring_edit_result){ 0 };
}
static bool build_changes(
    const script_analysis *a,
    const script_catalog *c,
    const authoring_source_presentation *p,
    const authoring_edit_request *r,
    source_edit_changes *changes,
    char *error,
    size_t size
)
{
    switch (r->kind) {
        case AUTHORING_EDIT_DISCONNECT:
        case AUTHORING_EDIT_RECONNECT:
            return source_edit_arguments(a, c, p, r, changes, error, size);
        case AUTHORING_EDIT_CREATE_VARIABLE_GET:
        case AUTHORING_EDIT_CREATE_VARIABLE_SET:
        case AUTHORING_EDIT_CREATE_CAPABILITY:
        case AUTHORING_EDIT_CREATE_EVENT:
            return source_edit_capability(a, c, p, r, changes, error, size);
        case AUTHORING_EDIT_CREATE_FUNCTION:
        case AUTHORING_EDIT_CREATE_MACRO:
        case AUTHORING_EDIT_CREATE_EXPRESSION:
        case AUTHORING_EDIT_RENAME_DECLARATION:
        case AUTHORING_EDIT_RETYPE_VARIABLE:
        case AUTHORING_EDIT_SET_DISABLED:
        case AUTHORING_EDIT_NAME_VALUE:
            return source_edit_declarations(a, c, p, r, changes, error, size);
        case AUTHORING_EDIT_SIGNATURE:
            return source_edit_signature(a, c, p, r, changes, error, size);
        case AUTHORING_EDIT_BIND_INPUT:
            return source_edit_binding(a, c, p, r, changes, error, size);
        case AUTHORING_EDIT_EXTRACT_FUNCTION:
        case AUTHORING_EDIT_EXTRACT_MACRO:
        case AUTHORING_EDIT_INLINE_CALL:
            return source_edit_refactor(a, c, p, r, changes, error, size);
        default:
            return source_edit_structural(a, c, p, r, changes, error, size);
    }
}
bool authoring_source_edit(
    const script_analysis *analysis,
    const script_catalog *catalog,
    const authoring_source_presentation *presentation,
    const authoring_edit_request *request,
    authoring_edit_result *result,
    char *error,
    size_t size
)
{
    assert(result);
    *result = (authoring_edit_result){ 0 };
    authoring_edit_request typed = *request;
    const source_presentation_origin *selected = origin(presentation, typed.origin);
    if (typed.kind == AUTHORING_EDIT_DELETE_STATEMENT ||
        typed.kind == AUTHORING_EDIT_MOVE_BEFORE ||
        typed.kind == AUTHORING_EDIT_MOVE_AFTER) {
        selected = source_edit_statement_origin(presentation, selected);
        if (selected) {
            typed.origin = selected->identity;
        }
        const source_presentation_origin *sibling = source_edit_statement_origin(
            presentation,
            origin(presentation, typed.producer)
        );
        if (sibling) {
            typed.producer = sibling->identity;
        }
    }
    request = &typed;
    if (selected && selected->kind == SOURCE_PRESENTATION_SYMBOL &&
        selected->record >= 0 && selected->record < analysis->symbol_count &&
        (typed.kind == AUTHORING_EDIT_CONNECT || typed.kind == AUTHORING_EDIT_EXPRESSION ||
         typed.kind == AUTHORING_EDIT_VALUE)) {
        int index = analysis->symbols[selected->record].initializer;
        if (index >= 0 && index < analysis->expression_count) {
            for (int i = 0;
                 i < authoring_source_presentation_origin_count(presentation);
                 i++) {
                const source_presentation_origin
                    *o = authoring_source_presentation_origin_at(presentation, i);
                if (o->kind == SOURCE_PRESENTATION_EXPRESSION && o->record == index) {
                    typed.origin = o->identity;
                    request = &typed;
                    break;
                }
            }
        }
    }
    char *literal = NULL;
    if (typed.kind == AUTHORING_EDIT_VALUE) {
        if (!analysis->valid ||
            typed.generation != authoring_source_edit_generation(presentation) ||
            !authoring_source_presentation_matches(presentation, analysis, catalog)) {
            fail(error, size, "Source changed; refresh this gesture's semantic generation");
            return false;
        }
        const source_presentation_origin *target = origin(presentation, typed.origin);
        if (!typed.value || !target || target->kind != SOURCE_PRESENTATION_EXPRESSION) {
            fail(error, size, "Select a value expression for this typed edit");
            return false;
        }
        literal = authoring_source_literal(catalog, target->type, typed.value, error, size);
        if (!literal) {
            return false;
        }
        typed.kind = AUTHORING_EDIT_EXPRESSION;
        typed.expression = literal;
        request = &typed;
    }
    source_edit_changes change = { 0 };
    bool structural = request->kind >= AUTHORING_EDIT_DELETE_SELECTION;
    char *text = NULL;
    if (structural) {
        if (!analysis->valid ||
            request->generation != authoring_source_edit_generation(presentation) ||
            !authoring_source_presentation_matches(presentation, analysis, catalog)) {
            free(literal);
            fail(error, size, "Source changed; refresh this gesture's semantic generation");
            return false;
        }
        if (strlen(analysis->text) > MAX_EDIT_BYTES) {
            free(literal);
            fail(error, size, "Source edit exceeds the 512 KiB document limit");
            return false;
        }
        bool changed = build_changes(analysis, catalog, presentation, request, &change, error, size);
        if (!changed) {
            source_edit_changes_free(&change);
            free(literal);
            return false;
        }
        text = change.text;
        change.text = NULL;
        text = checked(analysis, catalog, text, NULL, NULL, 0, error, size);
        if (!text) {
            source_edit_changes_free(&change);
        }
    } else {
        text = edit_text(analysis, catalog, presentation, request, error, size);
    }
    free(literal);
    if (!text) {
        return false;
    }
    const source_presentation_origin *target = origin(presentation, request->origin);
    offset_edit *edits = NULL;
    int destination = 0;
    script_range moved = { 0 };
    if (structural) {
        for (int i = 0; i < acount(change.patches); i++) {
            apush(
                edits,
                ((offset_edit){ change.patches[i].start,
                                change.patches[i].end,
                                (int)strlen(change.patches[i].text) })
            );
        }
        moved = change.moved;
        destination = change.destination;
    } else if (request->kind == AUTHORING_EDIT_EXPRESSION || request->kind == AUTHORING_EDIT_CONNECT) {
        int length = (int)strlen(text) - (int)strlen(analysis->text) +
                     target->range.end - target->range.start;
        apush(edits, ((offset_edit){ target->range.start, target->range.end, length }));
    } else {
        script_syntax *before = script_parse(analysis->text);
        moved = source_edit_whole_statement(before, target->range);
        apush(edits, ((offset_edit){ moved.start, moved.end, 0 }));
        if (request->kind != AUTHORING_EDIT_DELETE_STATEMENT) {
            script_range sibling = source_edit_whole_statement(
                before,
                origin(presentation, request->producer)->range
            );
            destination = request->kind == AUTHORING_EDIT_MOVE_BEFORE
                            ? sibling.start
                            : sibling.end;
            apush(
                edits,
                ((offset_edit){ destination, destination, moved.end - moved.start })
            );
        } else {
            moved = (script_range){ 0 };
        }
        script_syntax_free(before);
    }
    script_syntax *syntax = script_parse(text);
    script_analysis
        *candidate = script_analyze_document(syntax, catalog, analysis->identity);
    assert(candidate->valid);
    authoring_source_presentation *view = authoring_source_presentation_new(candidate, catalog, NULL);
    assert(view);
    bool carried = remap_layout(presentation, view, edits, acount(edits), moved, destination);
    afree(edits);
    source_edit_changes_free(&change);
    result->text = text;
    result->metadata = carried ? authoring_source_presentation_metadata(view) : NULL;
    authoring_source_presentation_free(view);
    script_analysis_free(candidate);
    script_syntax_free(syntax);
    if (!result->metadata) {
        authoring_edit_result_free(result);
        fail(error, size, "Could not retain source presentation metadata");
        return false;
    }
    return true;
}
