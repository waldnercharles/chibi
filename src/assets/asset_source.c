#include "assets/asset_source.h"

#include "authoring/asset_compiler_host.h"
#include "authoring/source_intelligence.h"
#include "language/syntax.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void asset_source_position(const asset_source *source, int offset, int *line, int *column)
{
    *line = *column = 1;
    for (int i = 0; i < offset && source->text[i]; i++) {
        if (source->text[i] == '\n') {
            (*line)++;
            *column = 1;
        } else {
            (*column)++;
        }
    }
}

asset_source_span *asset_source_highlight(const char *text)
{
    script_syntax *syntax = script_parse(text);
    int count = 0;
    authoring_highlight *highlights = authoring_source_syntax_highlights(syntax, &count);
    asset_source_span *spans = NULL;
    for (int i = 0; i < count; i++) {
        apush(
            spans,
            ((asset_source_span){ highlights[i].range.start,
                                  highlights[i].range.end,
                                  highlights[i].cls })
        );
    }
    free(highlights);
    script_syntax_free(syntax);
    return spans;
}

asset_source *asset_source_parse(const char *text)
{
    asset_source *source = xcalloc(1, sizeof *source);
    source->text = xstrdup(text);
    source->spans = asset_source_highlight(text);
    asset_compiler_diagnostic diagnostic = { 0 };
    source->document = asset_source_read(text, NULL, NULL, &diagnostic);
    if (!source->document) {
        asset_source_diagnostic item = { .start = diagnostic.start,
                                         .end = diagnostic.end,
                                         .line = diagnostic.line,
                                         .column = diagnostic.column };
        snprintf(item.message, sizeof item.message, "%s", diagnostic.message);
        apush(source->diagnostics, item);
    }
    return source;
}

void asset_source_free(asset_source *source)
{
    if (!source) {
        return;
    }
    free(source->text);
    av_doc_free(source->document);
    afree(source->spans);
    afree(source->diagnostics);
    free(source);
}

static const av_value *value_at(const av_value *value, int at, const char **name)
{
    for (size_t i = 0, n = av_is_obj(value) ? av_obj_size(value) : av_arr_size(value);
         i < n;
         i++) {
        const av_value *child = av_is_obj(value) ? av_obj_value_at(value, i)
                                                 : av_arr_get(value, i);
        av_source range = av_get_source(child);
        if ((range.start != SIZE_MAX && at >= (int)range.start &&
             at < (int)range.end) ||
            (range.name_start != SIZE_MAX && at >= (int)range.name_start &&
             at < (int)range.name_end)) {
            const av_value *nested = av_is_ctn(child) ? value_at(child, at, name)
                                                      : NULL;
            if (nested) {
                return nested;
            }
            *name = av_is_obj(value) ? av_get_str(av_obj_key_at(value, i)) : NULL;
            return child;
        }
    }
    return NULL;
}

const av_value *asset_source_value_at(const asset_source *source, int at, const char **name)
{
    *name = NULL;
    return source->document ? value_at(av_doc_root(source->document), at, name) : NULL;
}

void asset_source_complete(
    const asset_source *source,
    int offset,
    const char *path,
    const char *const *names,
    int name_count,
    asset_source_completions *out
)
{
    (void)names;
    (void)name_count;
    *out = (asset_source_completions){ 0 };
    script_syntax *syntax = script_parse(source->text);
    script_catalog catalog = { 0 };
    script_analysis *analysis = script_analyze(syntax, &catalog);
    authoring_document document = { path ? path : "Asset.chibi", syntax, analysis };
    authoring_navigation *navigation = authoring_navigation_new(&document, 1, &catalog);
    authoring_completions completions = { 0 };
    authoring_source_complete(navigation, 0, offset, &completions);
    out->start = completions.range.start;
    out->end = completions.range.end;
    for (int i = 0; i < completions.count; i++) {
        const authoring_completion *item = &completions.items[i];
        code_completion_kind kind = CODE_COMPLETE_VALUE;
        switch (item->kind) {
            case AUTHORING_COMPLETE_CALLABLE:
                kind = CODE_COMPLETE_FUNCTION;
                break;
            case AUTHORING_COMPLETE_TYPE: kind = CODE_COMPLETE_TYPE; break;
            case AUTHORING_COMPLETE_FIELD: kind = CODE_COMPLETE_FIELD; break;
            case AUTHORING_COMPLETE_KEYWORD:
                kind = CODE_COMPLETE_KEYWORD;
                break;
            default: break;
        }
        apush(
            out->items,
            ((asset_source_completion){ .label = xstrdup(item->label),
                                        .detail = "Chibi source",
                                        .insert = xstrdup(item->insert),
                                        .kind = kind })
        );
    }
    authoring_completions_free(&completions);
    authoring_navigation_free(navigation);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
}

void asset_source_completions_free(asset_source_completions *out)
{
    for (int i = 0; i < acount(out->items); i++) {
        free(out->items[i].label);
        free(out->items[i].insert);
    }
    afree(out->items);
    *out = (asset_source_completions){ 0 };
}

char *asset_source_hover(const asset_source *source, int offset)
{
    script_syntax *syntax = script_parse(source->text);
    script_catalog catalog = { 0 };
    script_analysis *analysis = script_analyze(syntax, &catalog);
    authoring_document document = { "Asset.chibi", syntax, analysis };
    authoring_navigation *navigation = authoring_navigation_new(&document, 1, &catalog);
    char *text = authoring_source_hover(navigation, 0, offset);
    authoring_navigation_free(navigation);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return text;
}

char *asset_source_reconcile(const char *text, const av_value *replacement, char *error, size_t size)
{
    asset_compiler_diagnostic diagnostic = { 0 };
    av_doc *before = asset_source_read(text, NULL, NULL, &diagnostic);
    char *result = NULL;
    if (before) {
        result = asset_source_reconcile_compiled(text, av_doc_root(before), replacement, NULL, &diagnostic);
    }
    av_doc_free(before);
    if (size) {
        snprintf(error, size, "%s", result ? "" : diagnostic.message);
    }
    return result;
}
