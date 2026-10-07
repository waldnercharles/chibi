#include "assets/code_print.h"
#include "assets/code_program.h"
#include "assets/code_program_internal.h"
#include "assets/code_program_reconcile.h"
#include "assets/code_query.h"
#include "assets/code_source_diagnostics.h"
#include "assets/code_source_intelligence.h"
#include "assets/code_source_internal.h"
#include "assets/code_value.h"
#include "ecs/ecs.h"
#include "graph/compiler/compile.h"
#include "graph/document/problems.h"
#include "graph/document/serialize.h"
#include "graph/model/reactive.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void code_source_position(const code_source *source, int offset, int *line, int *column)
{
    int row = 1, col = 1;
    for (int i = 0; source->text[i] && i < offset; i++) {
        if (source->text[i] == '\n') {
            row++;
            col = 1;
            continue;
        }
        col++;
    }
    if (line) {
        *line = row;
    }
    if (column) {
        *column = col;
    }
}
void code_source_error(code_source *source, int start, int end, const char *message)
{
    int size = (int)strlen(source->text);
    start = start < 0 ? 0 : start > size ? size : start;
    end = end < start ? start : end > size ? size : end;
    code_source_diagnostic diagnostic = { .start = start, .end = end, .severity = CMPL_ERROR };
    code_source_position(source, start, &diagnostic.line, &diagnostic.column);
    snprintf(diagnostic.message, sizeof diagnostic.message, "%s", message && message[0] ? message : "Invalid source");
    apush(source->diagnostics, diagnostic);
}
bool code_source_is_structured(const char *text)
{
    code_lexer lexer;
    code_lex_init(&lexer, text ? text : "");
    code_token first = code_lex_next(&lexer);
    return code_token_is(&lexer, first, "behavior") ||
           code_token_is(&lexer, first, "library") ||
           code_token_is(&lexer, first, "system");
}
bool code_source_is_behavior(const code_source *source)
{
    if (code_source_is_structured(source->text)) {
        return true;
    }
    av_value *root = source->document ? av_doc_root(source->document) : NULL;
    return av_is_arr(av_obj_get(root, "graphs"));
}
static bool valid_strings(const av_value *value)
{
    if (av_is_str(value)) {
        return !memchr(av_get_str(value), 0, av_get_len(value));
    }
    if (av_is_arr(value)) {
        for (size_t i = 0; i < av_arr_size(value); i++) {
            if (!valid_strings(av_arr_get(value, i))) {
                return false;
            }
        }
    }
    if (av_is_obj(value)) {
        for (size_t i = 0; i < av_obj_size(value); i++) {
            if (!valid_strings(av_obj_key_at(value, i)) ||
                !valid_strings(av_obj_value_at(value, i))) {
                return false;
            }
        }
    }
    return true;
}
static code_source *parse(const char *text, const code_source *previous, const code_source_context *context)
{
    code_source *source = xcalloc(1, sizeof *source);
    source->text = xstrdup(text ? text : "");
    source->spans = asset_source_highlight(source->text);
    if (context) {
        source->context = *context;
    } else if (previous) {
        source->context = previous->context;
    }
    if (code_program_detect(source->text)) {
        if (previous && previous->asset) {
            for (int i = 0; i < acount(previous->asset->graphs); i++) {
                if (previous->asset->graphs[i]->id_counter > source->id_floor) {
                    source->id_floor = previous->asset->graphs[i]->id_counter;
                }
            }
        }
        source->asset = code_program_parse(source);
        if (source->asset && previous && previous->asset && previous->program) {
            code_program_match_previous(source, previous);
            for (int i = 0; i < acount(source->diagnostics); i++) {
                if (source->diagnostics[i].severity != CMPL_ERROR) {
                    continue;
                }
                behavior_asset_free(source->asset);
                source->asset = NULL;
                break;
            }
        }
        if (source->asset && previous && previous->asset) {
            char *metadata = code_source_metadata_export(previous->asset);
            code_source_metadata_restore(source, metadata);
            free(metadata);
        }
        if (source->asset) {
            code_program_refresh_locations(source);
            av_doc_free(source->document);
            source->document = av_doc_new();
            av_value *root = behavior_asset_to_value(source->document, source->asset);
            code_program *program = source->program;
            if (program->query) {
                av_obj_set(
                    source->document,
                    root,
                    "query",
                    av_clone(source->document, program->query)
                );
            }
            code_lexer lexer;
            code_lex_init(&lexer, source->text);
            if (code_token_is(&lexer, code_lex_next(&lexer), "system")) {
                av_obj_set(source->document, root, "kind", av_str(source->document, "system"));
            }
            av_doc_set_root(source->document, root);
        } else {
            code_program_free(source->program);
            source->program = NULL;
        }
        return source;
    }
    char error[256];
    source->document = av_doc_read(source->text, error, sizeof error);
    if (!source->document) {
        code_source_error(source, 0, (int)strlen(source->text), error);
        return source;
    }
    if (code_source_is_behavior(source)) {
        if (!valid_strings(av_doc_root(source->document))) {
            code_source_error(source, 0, (int)strlen(source->text), "Behavior source strings cannot contain embedded NUL bytes");
            return source;
        }
        source->asset = behavior_asset_from_value(av_doc_root(source->document));
        if (source->asset && (source->asset->domain == BEHAVIOR_DOMAIN_EVENT ||
                              source->asset->domain == BEHAVIOR_DOMAIN_LIBRARY)) {
            behavior_asset_free(source->asset);
            source->asset = NULL;
            code_source_error(source, 0, (int)strlen(source->text), "Executable assets require readable behavior, library or system source");
            return source;
        }
        if (!source->asset) {
            code_source_load_error(source, graph_last_error());
        }
    }
    return source;
}
code_source *code_source_parse(const char *text)
{
    return parse(text, NULL, NULL);
}
code_source *code_source_parse_previous(const char *text, const code_source *previous)
{
    return parse(text, previous, NULL);
}
code_source *code_source_parse_context(
    const char *text,
    const code_source *previous,
    const code_source_context *context
)
{
    return parse(text, previous, context);
}
void code_source_free(code_source *source)
{
    if (!source) {
        return;
    }
    free(source->text);
    afree(source->spans);
    for (int i = 0; i < acount(source->diagnostics); i++) {
        free(source->diagnostics[i].asset_name);
        free(source->diagnostics[i].graph_name);
    }
    afree(source->diagnostics);
    av_doc_free(source->document);
    behavior_asset_free(source->asset);
    afree(source->identity_matches);
    code_program_free(source->program);
    code_intelligence_clear(source);
    free(source);
}
behavior_asset *code_source_build_behavior(code_source *source)
{
    return source->asset ? behavior_asset_clone(source->asset) : NULL;
}
bool code_source_location(
    const code_source *source,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id,
    int *start,
    int *end
)
{
    if (source->asset) {
        const graph *owner = behavior_asset_find_graph(source->asset, graph_id);
        const node *authored = owner ? graph_node_by_id(owner, node_id) : NULL;
        if (!owner || (node_id && !authored) ||
            (pin_id && (!authored || !graph_pin_by_id(authored, pin_id, NULL, NULL)))) {
            return false;
        }
    }
    if (source->program) {
        return source->asset &&
               code_program_location(source->program, graph_id, node_id, pin_id, start, end);
    }
    return code_source_explicit_location(source, graph_id, node_id, pin_id, start, end);
}
behavior_asset *code_source_compile_behavior(code_source *source)
{
    if (!source->asset) {
        return NULL;
    }
    /* Remove previous semantic diagnostics before repeat analysis. */
    for (int i = acount(source->diagnostics) - 1; i >= 0; i--) {
        if (!source->diagnostics[i].rule) {
            continue;
        }
        free(source->diagnostics[i].asset_name);
        free(source->diagnostics[i].graph_name);
        memmove(
            &source->diagnostics[i],
            &source->diagnostics[i + 1],
            (size_t)(acount(source->diagnostics) - i - 1) * sizeof *source->diagnostics
        );
        asetlen(source->diagnostics, acount(source->diagnostics) - 1);
    }
    compile_report report;
    compile_report_init(&report);
    const behavior_asset **catalog = NULL;
    for (int i = 0; i < source->context.library_count; i++) {
        apush(catalog, source->context.libraries[i]);
    }
    for (int i = 0; i < source->context.event_count; i++) {
        apush(catalog, source->context.events[i]);
    }
    behavior_asset *linked = behavior_asset_validate(source->asset, catalog, acount(catalog), &report);
    afree(catalog);
    const code_program *program = source->program;
    if (linked && !compile_report_blocked(&report) && program && program->system) {
        behavior_asset_compile_for_system(linked, &report);
    }
    code_source_compile_diagnostics(source, linked, &report);
    behavior_asset_free(linked);
    bool errors = compile_report_blocked(&report);
    compile_report_free(&report);
    return errors ? NULL : behavior_asset_clone(source->asset);
}
char *code_source_format_with_metadata(const behavior_asset *asset, char **metadata)
{
    if (metadata) {
        *metadata = NULL;
    }
    if (asset->domain == BEHAVIOR_DOMAIN_EVENT ||
        asset->domain == BEHAVIOR_DOMAIN_LIBRARY) {
        char error[256];
        char *text = code_print_behavior_with_metadata(asset, metadata, error, sizeof error);
        if (!text) {
            return NULL;
        }
        return text;
    }
    av_doc *doc = av_doc_new();
    av_doc_set_root(doc, behavior_asset_to_value(doc, asset));
    char *text = av_doc_write(doc);
    av_doc_free(doc);
    return text;
}

char *code_source_format(const behavior_asset *asset)
{
    return code_source_format_with_metadata(asset, NULL);
}

typedef struct
{
    int start, end;
    char *text;
} source_edit;
static void append_text(char **out, const char *text)
{
    for (int i = 0; text[i]; i++) {
        apush(*out, text[i]);
    }
}
static const char *expression_label(const behavior_asset *asset, const node *expression, int index)
{
    const pin_spec *pin = &expression->input_pins[index];
    if (pin->binding && pin->binding->kind == PIN_BINDING_VARIABLE) {
        const behavior_variable
            *variable = behavior_asset_find_variable_by_id(asset, pin->binding->variable_id);
        if (variable) {
            return variable->name;
        }
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        if (graph_node_by_id(g, expression->id) != expression) {
            continue;
        }
        node *event = NULL;
        int output = -1;
        if (graph_input_source(g, expression, index, &event, &output) &&
            event->desc == &NODE_Event && event->reference_name &&
            !strcmp(event->reference_name, "Tick") &&
            !strcmp(event->output_pins[output].name, "DeltaSeconds")) {
            return "event.dt";
        }
    }
    return pin->display_name && pin->display_name[0] ? pin->display_name : pin->name;
}
const char *code_source_expression_input_label(const behavior_asset *asset, const node *expression, int input)
{
    assert(asset && expression && expression->desc == &NODE_BehaviorExpression);
    assert(input >= 0 && input < expression->input_count);
    return expression_label(asset, expression, input);
}
char *code_source_expression(const code_source *source, const behavior_asset *asset, const node *expression)
{
    char *readable = source && source->program
                       ? code_statement_display_expression(source->program, asset, expression)
                       : NULL;
    if (readable) {
        return readable;
    }
    const char *text = behavior_expression_source(asset, expression);
    if (!text) {
        return NULL;
    }
    code_lexer lexer;
    code_lex_init(&lexer, text);
    char *out = NULL;
    int copied = 0;
    for (;;) {
        code_token token = code_lex_next(&lexer);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        if (token.kind != CODE_TOKEN_NAME) {
            continue;
        }
        for (int i = 0; i < expression->input_count; i++) {
            const pin_spec *pin = &expression->input_pins[i];
            if (strlen(pin->name) != token.end - token.start ||
                memcmp(pin->name, text + token.start, token.end - token.start)) {
                continue;
            }
            for (int at = copied; at < (int)token.start; at++) {
                apush(out, text[at]);
            }
            char label[80];
            code_source_expression_input_name(NULL, asset, expression, i, label, sizeof label);
            append_text(&out, label);
            copied = (int)token.end;
            break;
        }
    }
    append_text(&out, text + copied);
    apush(out, 0);
    char *result = xstrdup(out);
    afree(out);
    free(readable);
    return result;
}
char *code_source_resolve_expression(
    const code_source *source,
    const behavior_asset *asset,
    const node *expression,
    const char *text,
    char *error,
    size_t size
)
{
    char *resolved = source && source->program
                       ? code_statement_resolve_expression(source->program, expression, text)
                       : NULL;
    if (resolved) {
        text = resolved;
    }
    (void)source;
    if (size) {
        error[0] = 0;
    }
    code_lexer lexer;
    code_lex_init(&lexer, text);
    char *out = NULL;
    int copied = 0;
    for (;;) {
        code_token token = code_lex_next(&lexer);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        if (token.kind != CODE_TOKEN_NAME) {
            continue;
        }
        code_lexer next = lexer;
        bool qualified = false;
        int end = (int)token.end;
        for (;;) {
            code_lexer look = next;
            code_token dot = code_lex_next(&look);
            if (dot.punctuation != '.') {
                break;
            }
            code_token member = code_lex_next(&look);
            if (member.kind != CODE_TOKEN_NAME) {
                break;
            }
            qualified = true;
            end = (int)member.end;
            next = look;
        }
        bool matched = false;
        for (int i = 0; i < expression->input_count; i++) {
            const pin_spec *pin = &expression->input_pins[i];
            if (qualified && strlen(pin->name) == token.end - token.start &&
                !memcmp(pin->name, text + token.start, token.end - token.start)) {
                /* The retained source resolver already produced this aggregate
                 * capture path; keep its member access intact. */
                lexer = next;
                matched = true;
                break;
            }
            char label[80];
            code_source_expression_input_name(NULL, asset, expression, i, label, sizeof label);
            if (strlen(label) != (size_t)(end - (int)token.start) ||
                memcmp(label, text + token.start, (size_t)(end - (int)token.start))) {
                continue;
            }
            for (int at = copied; at < (int)token.start; at++) {
                apush(out, text[at]);
            }
            append_text(&out, pin->name);
            copied = end;
            if (qualified) {
                lexer = next;
            }
            matched = true;
            break;
        }
        if (!matched && qualified) {
            if (size) {
                snprintf(error, size, "Unknown reflected expression input; add its binding or edit the behavior source");
            }
            afree(out);
            free(resolved);
            return NULL;
        }
    }
    append_text(&out, text + copied);
    apush(out, 0);
    char *result = xstrdup(out);
    afree(out);
    free(resolved);
    return result;
}
static bool inherit_authoring(code_source *source, const behavior_asset *asset)
{
    char *metadata = code_source_metadata_export(asset);
    bool valid = code_source_metadata_restore(source, metadata);
    free(metadata);
    return valid;
}

static char *apply_edits(const char *text, source_edit *edits)
{
    /* Byte edits are independent. Apply in source order without losing trivia. */
    for (int i = 1; i < acount(edits); i++) {
        source_edit value = edits[i];
        int j = i;
        while (j && edits[j - 1].start > value.start) {
            edits[j] = edits[j - 1];
            j--;
        }
        edits[j] = value;
    }
    char *out = NULL;
    int at = 0;
    for (int i = 0; i < acount(edits); i++) {
        source_edit *edit = &edits[i];
        if (edit->start < at) {
            afree(out);
            return NULL;
        }
        for (; at < edit->start; at++) {
            apush(out, text[at]);
        }
        append_text(&out, edit->text);
        at = edit->end;
    }
    append_text(&out, text + at);
    apush(out, 0);
    char *result = xstrdup(out);
    afree(out);
    return result;
}
static char *retain_comments(const char *text, const char *formatted)
{
    char *out = NULL;
    int metadata_start = -1, metadata_end = -1;
    code_source_metadata_range(text, &metadata_start, &metadata_end);
    /* Retain authored lexical comments when a structural edit regenerates source. */
    for (int i = 0; text[i];) {
        if (i == metadata_start) {
            i = metadata_end;
            continue;
        }
        if (text[i] == '"' || text[i] == '\'') {
            char quote = text[i++];
            while (text[i] && text[i] != quote) {
                if (text[i] == '\\' && text[i + 1]) {
                    i++;
                }
                i++;
            }
            if (text[i]) {
                i++;
            }
            continue;
        }
        if (text[i] == '/' && text[i + 1] == '/') {
            while (text[i] && text[i] != '\n') {
                apush(out, text[i++]);
            }
            apush(out, '\n');
            continue;
        }
        if (text[i] == '/' && text[i + 1] == '*') {
            int depth = 1;
            apush(out, text[i++]);
            apush(out, text[i++]);
            while (text[i] && depth) {
                if (text[i] == '/' && text[i + 1] == '*') {
                    depth++;
                    apush(out, text[i++]);
                    apush(out, text[i++]);
                    continue;
                }
                if (text[i] == '*' && text[i + 1] == '/') {
                    depth--;
                    apush(out, text[i++]);
                    apush(out, text[i++]);
                    continue;
                }
                apush(out, text[i++]);
            }
            apush(out, '\n');
            continue;
        }
        i++;
    }
    append_text(&out, formatted);
    apush(out, 0);
    char *result = xstrdup(out);
    afree(out);
    return result;
}
static code_source *printed_source(
    const code_source *source,
    const behavior_asset *asset,
    char *error,
    size_t size
)
{
    char *metadata = NULL;
    char *printed = code_source_format_with_metadata(asset, &metadata);
    if (!printed || !metadata) {
        free(printed);
        free(metadata);
        return NULL;
    }
    char *text = retain_comments(source->text, printed);
    av_doc *doc = av_json_read(metadata);
    free(metadata);
    av_value *anchors = av_obj_get(av_doc_root(doc), "anchors");
    /* Retaining ordinary comments changes offsets, never executable tokens. */
    for (size_t i = 0; i < av_arr_size(anchors); i++) {
        av_value *item = av_arr_get(anchors, i);
        uint64_t offset = av_get_uint(av_obj_get(item, "start"));
        code_lexer old, next;
        code_lex_init(&old, printed);
        code_lex_init(&next, text);
        for (;;) {
            code_token a = code_lex_next(&old), b = code_lex_next(&next);
            if (a.kind == CODE_TOKEN_END || a.kind == CODE_TOKEN_ERROR ||
                a.start >= offset) {
                av_obj_set(doc, item, "start", av_uint(doc, b.start));
                break;
            }
        }
    }
    metadata = av_json_write(av_doc_root(doc));
    av_doc_free(doc);
    code_source *result = code_source_parse_context(text, NULL, &source->context);
    bool restored = result->asset && code_source_metadata_restore(result, metadata);
    bool valid = restored && behavior_asset_equal(result->asset, asset);

    if (!valid && size) {
        const char *reason = !result->asset && acount(result->diagnostics)
                               ? result->diagnostics[0].message
                           : !restored
                               ? "optional metadata could not be restored"
                               : "restored native model differs";
        snprintf(error, size, "Readable graph source does not preserve the complete native asset: %s", reason);
    }
    free(metadata);
    free(printed);
    free(text);
    if (!valid) {
        code_source_free(result);
        return NULL;
    }
    return result;
}

static char *reconcile_native(
    const code_source *source,
    const behavior_asset *asset,
    char **presentation,
    char *error,
    size_t size
)
{
    if (presentation) {
        *presentation = NULL;
    }
    if (!source || !asset) {
        if (size) {
            snprintf(error, size, "Source reconciliation requires a native asset");
        }
        return NULL;
    }
    if (source->program) {
        char *text = code_program_reconcile(source, asset, error, size);
        if (!text) {
            return NULL;
        }
        code_source *check = code_source_parse_context(text, source, &source->context);
        free(text);
        bool valid = check->asset && inherit_authoring(check, asset) &&
                     behavior_asset_equal(check->asset, asset);
        if (!valid) {
            code_source_free(check);
            check = printed_source(source, asset, error, size);
            valid = check != NULL;
        }
        char *result = valid ? code_source_persist(check) : NULL;
        char *metadata = valid ? code_source_metadata_export_source(check) : NULL;
        code_source_free(check);
        if (!result) {
            free(metadata);
            return NULL;
        }
        check = code_source_parse_context(result, NULL, &source->context);
        valid = check->asset && code_source_metadata_restore(check, metadata) &&
                behavior_asset_equal(check->asset, asset);
        if (valid && presentation) {
            *presentation = metadata;
            metadata = NULL;
        }
        free(metadata);
        if (!valid && size) {
            snprintf(
                error,
                size,
                "Persisted source did not preserve the native behavior: %s",
                acount(check->diagnostics) ? check->diagnostics[0].message : "model mismatch"
            );
        }
        code_source_free(check);
        if (!valid) {
            free(result);
            return NULL;
        }
        return result;
    }
    if (source->asset && behavior_asset_equal(source->asset, asset)) {
        return xstrdup(source->text);
    }
    av_doc *document = av_doc_new();
    av_value *replacement = behavior_asset_to_value(document, asset);
    char *preserved = asset_source_reconcile(source->text, replacement, error, size);
    av_doc_free(document);
    if (preserved) {
        code_source *check = code_source_parse(preserved);
        bool valid = check->asset && inherit_authoring(check, asset) &&
                     behavior_asset_equal(check->asset, asset);
        code_source_free(check);
        if (valid) {
            return preserved;
        }
        free(preserved);
    }
    char *formatted = code_source_format(asset);
    if (!formatted) {
        if (size) {
            snprintf(error, size, "Behavior cannot be represented by readable source");
        }
        return NULL;
    }
    char *text = retain_comments(source->text, formatted);
    free(formatted);
    code_source *check = code_source_parse(text);
    bool valid = check->asset && inherit_authoring(check, asset) &&
                 behavior_asset_equal(check->asset, asset);
    if (!valid && size) {
        snprintf(error, size, "Graph source reconciliation cannot preserve the complete asset");
    }
    code_source_free(check);
    if (valid) {
        return text;
    }
    free(text);
    return NULL;
}

char *code_source_reconcile_with_metadata(
    const code_source *source,
    const behavior_asset *asset,
    char **presentation,
    char *error,
    size_t size
)
{
    if (!asset || !acount(asset->reactives)) {
        return reconcile_native(source, asset, presentation, error, size);
    }
    behavior_asset *normalized = behavior_asset_clone(asset);
    if (!normalized || !behavior_reactive_refresh(normalized, error, size) ||
        !behavior_reactive_validate(normalized, error, size)) {
        behavior_asset_free(normalized);
        if (presentation) {
            *presentation = NULL;
        }
        return NULL;
    }
    char *result = reconcile_native(source, normalized, presentation, error, size);
    behavior_asset_free(normalized);
    return result;
}

char *code_source_reconcile(const code_source *source, const behavior_asset *asset, char *error, size_t size)
{
    return code_source_reconcile_with_metadata(source, asset, NULL, error, size);
}

char *code_source_persist(const code_source *source)
{
    if (!source || !source->asset) {
        return NULL;
    }
    source_edit *edits = NULL;
    for (int i = 0; i < acount(source->spans); i++) {
        const asset_source_span *span = &source->spans[i];
        if (strcmp(span->cls, "tok-comment") ||
            strncmp(source->text + span->start, "// @node ", 9)) {
            continue;
        }
        int start = span->start, end = span->end;
        while (start && (source->text[start - 1] == ' ' ||
                         source->text[start - 1] == '\t')) {
            start--;
        }
        if ((!start || source->text[start - 1] == '\n') && source->text[end] == '\n') {
            end++;
        }
        apush(edits, ((source_edit){ start, end, xstrdup("") }));
    }
    int start, end;
    if (code_source_metadata_range(source->text, &start, &end)) {
        if (start && source->text[start - 1] == '\n') {
            start--;
        }
        if (source->text[end] == '\n') {
            end++;
        }
        apush(edits, ((source_edit){ start, end, xstrdup("") }));
    }
    char *text = apply_edits(source->text, edits);
    for (int i = 0; i < acount(edits); i++) {
        free(edits[i].text);
    }
    afree(edits);
    return text;
}

const char *code_source_kind(const code_source *source)
{
    if (!source || !source->asset) {
        return "invalid";
    }
    const char *kind = source->document
                         ? av_get_str(av_obj_get(av_doc_root(source->document), "kind"))
                         : NULL;
    if (kind && !strcmp(kind, "system")) {
        return "system";
    }
    return source->asset->domain == BEHAVIOR_DOMAIN_LIBRARY ? "library" : "behavior";
}
char *code_source_reconcile_query(const code_source *source, const av_value *query, char *error, size_t size)
{
    if (!source || !source->program) {
        if (size) {
            snprintf(error, size, "Query edits require readable executable source");
        }
        return NULL;
    }
    code_program *program = source->program;
    char *expression = query ? code_query_write(query) : NULL;
    if (query && !expression) {
        if (size) {
            snprintf(error, size, "Invalid system query");
        }
        return NULL;
    }
    int start = program->query_start, end = program->query_end;
    if (!end) {
        code_lexer lexer;
        code_lex_init(&lexer, source->text);
        code_token token;
        do {
            token = code_lex_next(&lexer);
        } while (token.kind != CODE_TOKEN_END && !code_token_is(&lexer, token, "{"));
        start = end = (int)token.end;
    }
    size_t length = strlen(source->text),
           extra = expression ? strlen(expression) + 16 : 0;
    char *text = xmalloc(length - (size_t)(end - start) + extra + 1);
    memcpy(text, source->text, (size_t)start);
    size_t at = (size_t)start;
    if (expression) {
        at += (size_t)snprintf(text + at, extra + 1, "\n    query %s;", expression);
    }
    memcpy(text + at, source->text + end, length - (size_t)end + 1);
    free(expression);
    code_source *check = code_source_parse_context(text, NULL, &source->context);
    bool valid = check->asset != NULL;
    if (!valid && size) {
        snprintf(
            error,
            size,
            "%s",
            acount(check->diagnostics) ? check->diagnostics[0].message : "Invalid query source"
        );
    }
    code_source_free(check);
    if (!valid) {
        free(text);
        return NULL;
    }
    return text;
}

behavior_asset *code_source_declarations(const char *text, char *error, size_t size)
{
    code_source *source = xcalloc(1, sizeof *source);
    source->text = xstrdup(text);
    source->asset = code_program_parse_declarations(source);
    behavior_asset *asset = source->asset;
    source->asset = NULL;
    if (!asset && size) {
        snprintf(
            error,
            size,
            "%s",
            acount(source->diagnostics) ? source->diagnostics[0].message : "Invalid source declarations"
        );
    }
    code_source_free(source);
    return asset;
}

uint64_t code_source_previous_identity(const code_source *source, uint64_t current)
{
    for (int i = 0; i < acount(source->identity_matches); i++) {
        if (source->identity_matches[i].current == current) {
            return source->identity_matches[i].previous;
        }
    }
    return 0;
}
