#include "runtime/text_editing_native.h"

#include "execution/function.h"
#include "runtime/text_editing.h"

#include <stdlib.h>

static const char *cursor(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    char error[256];
    int offset = 0;
    out[1].b = text_editing_cursor(in[0].str, in[1].i, in[2].str, in[3].i, in[4].i, &offset, error, sizeof error);
    out[0].i = offset;
    out[2].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *line(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    char error[256];
    text_line_view row = { 0 };
    out[5].b = text_editing_line(in[0].str, in[1].i, &row, error, sizeof error);
    out[0].i = row.start;
    out[1].i = row.end;
    out[2].i = row.next;
    out[3].i = row.line;
    out[4].i = row.column;
    out[6].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *slice(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    char error[256], *text = NULL;
    out[1].b = text_editing_slice(in[0].str, in[1].i, in[2].i, &text, error, sizeof error);
    out[0].str = ctx->copy_string(ctx->value_storage, text ? text : "");
    out[2].str = ctx->copy_string(ctx->value_storage, error);
    free(text);
    return NULL;
}
static const char *replace(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    char error[256], *text = NULL;
    out[1].b = text_editing_replace(in[0].str, in[1].i, in[2].i, in[3].str, &text, error, sizeof error);
    out[0].str = ctx->copy_string(ctx->value_storage, text ? text : "");
    out[2].str = ctx->copy_string(ctx->value_storage, error);
    free(text);
    return NULL;
}
const node_host_call NATIVE_TEXT_CURSOR_CALL = { .context = cursor };
const node_host_call NATIVE_TEXT_LINE_CALL = { .context = line };
const node_host_call NATIVE_TEXT_SLICE_CALL = { .context = slice };
const node_host_call NATIVE_TEXT_REPLACE_CALL = { .context = replace };

static const char *difference(
    const behavior_function_context *ctx,
    void *,
    const pin_value *in,
    pin_value *out
)
{
    char error[256];
    text_difference change;
    out[5].b = text_editing_difference(in[0].str, in[1].str, &change, error, sizeof error);
    out[0].i = change.start;
    out[1].i = change.end;
    out[2].str = ctx->copy_string(
        ctx->value_storage,
        change.replacement ? change.replacement : ""
    );
    out[3].i = change.removed;
    out[4].i = change.inserted;
    out[6].str = ctx->copy_string(ctx->value_storage, error);
    free(change.replacement);
    return NULL;
}
const node_host_call NATIVE_TEXT_DIFFERENCE_CALL = { .context = difference };

static const char *prefix_lines(
    const behavior_function_context *ctx,
    void *,
    const pin_value *in,
    pin_value *out
)
{
    char error[256], *text = NULL;
    int start, end;
    out[3].b = text_editing_prefix_lines(
        in[0].str,
        in[1].i,
        in[2].i,
        in[3].str,
        in[4].str,
        &text,
        &start,
        &end,
        error,
        sizeof error
    );
    out[0].str = ctx->copy_string(ctx->value_storage, text ? text : "");
    out[1].i = start;
    out[2].i = end;
    out[4].str = ctx->copy_string(ctx->value_storage, error);
    free(text);
    return NULL;
}
const node_host_call NATIVE_TEXT_PREFIX_LINES_CALL = { .context = prefix_lines };
