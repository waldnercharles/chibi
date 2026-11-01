#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/component.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/program_internal.h"
#include "execution/request.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A frame references the locals it owns. */
bool program_capture_reference(
    arena *storage,
    behavior_function_context *ctx,
    const execution_module_capture *capture,
    pin_value *out
)
{
    if (!ctx->data || capture->value < 0 ||
        capture->value >= acount(ctx->data->function->data_values)) {
        return false;
    }
    *out = execution_reference_value(
        storage,
        ctx,
        &ctx->data->values[capture->value],
        capture->type
    );
    return true;
}
