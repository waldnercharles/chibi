#include "runtime/value_json_native.h"

#include "execution/function.h"
#include "runtime/value_json.h"

#include <assert.h>
#include <stdlib.h>

static const char *encode(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    assert(ctx->copy_string);
    char *text = NULL, error[256];
    bool ok = value_to_json(in, &text, error, sizeof error);
    out[0] = (pin_value){ .type = PIN_STR,
                          .type_name = "const char *",
                          .str = ctx->copy_string(ctx->value_storage, ok ? text : "") };
    out[1] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = ok };
    out[2] = (pin_value){ .type = PIN_STR,
                          .type_name = "const char *",
                          .str = ctx->copy_string(ctx->value_storage, error) };
    free(text);
    return NULL;
}

const node_host_call VALUE_JSON_CALL = { .context = encode };
const node_access VALUE_JSON_ACCESS = { .flags = NODE_EFFECT_PURE };
