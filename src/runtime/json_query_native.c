#include "runtime/json_query_native.h"

#include "execution/function.h"
#include "runtime/json_query.h"

#include <assert.h>

static pin_value string(const behavior_function_context *ctx, const char *text)
{
    return (pin_value){ .type = PIN_STR,
                        .type_name = "const char *",
                        .str = ctx->copy_string(ctx->value_storage, text ? text : "") };
}
static const char *query(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    assert(ctx->copy_string);
    json_query_result result = { 0 };
    char error[256];
    bool ok = query_json(in[0].str, in[1].str, &result, error, sizeof error);
    out[0] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = ok };
    out[1] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = result.found };
    out[2] = string(ctx, result.kind);
    out[3] = string(ctx, result.string);
    out[4] = (pin_value){ .type = PIN_F64, .type_name = "double", .d = result.number };
    out[5] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = result.boolean };
    out[6] = string(ctx, result.json);
    out[7] = (pin_value){ .type = PIN_I32, .type_name = "int", .i = result.count };
    out[8] = string(ctx, error);
    json_query_result_free(&result);
    return NULL;
}
const node_host_call JSON_QUERY_CALL = { .context = query };
const node_access JSON_QUERY_ACCESS = { .flags = NODE_EFFECT_PURE };
