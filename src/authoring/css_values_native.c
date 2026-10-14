#include "authoring/css_values.h"
#include "authoring/css_values_definition.h"
#include "execution/function.h"
#include "types/array.h"

#include <string.h>
static const char *query(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    css_value value = css_value_query(in[0].str, in[1].str);
    out[0].b = value.ok;
    out[1].b = value.known;
    out[2].b = value.simple;
    out[3].str = ctx->copy_string(ctx->value_storage, value.kind);
    out[4].d = value.value;
    out[5].str = ctx->copy_string(ctx->value_storage, value.unit);
    out[6].str = ctx->copy_string(ctx->value_storage, value.initial);
    out[7].str = ctx->copy_string(ctx->value_storage, value.ok ? in[1].str : "");
    out[8].b = value.inherited;
    out[9].b = value.shorthand;
    out[10].b = value.length;
    out[11].b = value.number;
    out[12].b = value.percent;
    out[13].b = value.color;
    out[14].b = value.integer;
    int count = *value.keywords ? 1 : 0;
    for (const char *at = value.keywords; *at; at++) {
        if (*at == ',') {
            count++;
        }
    }
    pin_array *options = pin_array_snapshot_allocate(ctx->value_storage, PIN_STR, "const char *", count);
    const char *at = value.keywords;
    for (int i = 0; i < count; i++) {
        const char *end = strchr(at, ',');
        if (!end) {
            end = at + strlen(at);
        }
        char option[128];
        size_t n = (size_t)(end - at);
        if (n >= sizeof option) {
            return "CSS keyword metadata exceeds bounds";
        }
        memcpy(option, at, n);
        option[n] = 0;
        options->items[i] = (pin_value){
            .type = PIN_STR,
            .str = ctx->copy_string(ctx->value_storage, option)
        };
        at = *end ? end + 1 : end;
    }
    out[15].ptr = options;
    for (int i = 0; i < 4; i++) {
        out[16 + i].f = value.rgba[i];
    }
    out[20].str = ctx->copy_string(ctx->value_storage, value.error);
    return NULL;
}
static const char *format(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    float rgba[4] = { in[6].f, in[7].f, in[8].f, in[9].f };
    char text[CSS_VALUE_TEXT_MAX + 1] = "", error[256] = "";
    out[0].b = css_value_format(
        in[0].str,
        in[1].str,
        in[2].str,
        in[3].d,
        in[4].str,
        in[5].str,
        rgba,
        text,
        sizeof text,
        error,
        sizeof error
    );
    out[1].str = ctx->copy_string(ctx->value_storage, text);
    out[2].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
const node_host_call CSS_VALUES_QUERY_CALL = { .context = query },
                     CSS_VALUES_FORMAT_CALL = { .context = format };
const node_access CSS_VALUES_ACCESS = { 0 };
