#include "execution/function.h"
#include "host/authoring_platform_definition.h"
#include "platform/cute.h"
#include "render/device_controls.h"
#include "types/array.h"

#include <assert.h>
#include <string.h>
static void copy_result(const behavior_function_context *ctx, pin_value *out, authoring_platform_result r)
{
    r.state = ctx->copy_string(ctx->value_storage, r.state);
    r.status = ctx->copy_string(ctx->value_storage, r.status);
    r.message = ctx->copy_string(ctx->value_storage, r.message);
    r.value = ctx->copy_string(ctx->value_storage, r.value);
    r.log = ctx->copy_string(ctx->value_storage, r.log);
    assert(out->type == PIN_STRUCT && out->ptr);
    memcpy(out->ptr, &r, sizeof r);
}
static const char *request(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    copy_result(
        ctx,
        out,
        authoring_platform_request(
            service,
            in[0].str ? in[0].str : "",
            in[1].str ? in[1].str : "",
            in[2].d,
            in[3].d,
            in[4].i
        )
    );
    return NULL;
}
static const char *poll(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    copy_result(ctx, out, authoring_platform_poll(service, in[0].i));
    return NULL;
}
static const char *cancel(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    copy_result(ctx, out, authoring_platform_cancel(service, in[0].i));
    return NULL;
}
static const char *release(void *service, const pin_value *in, pin_value *out)
{
    out[0].b = authoring_platform_release(service, in[0].i);
    return NULL;
}
static const char *environment(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *,
    pin_value *out
)
{
    authoring_platform_environment e = authoring_platform_environment_get(service);
    e.platform = ctx->copy_string(ctx->value_storage, e.platform);
    e.theme = ctx->copy_string(ctx->value_storage, e.theme);
    assert(out->ptr);
    memcpy(out->ptr, &e, sizeof e);
    return NULL;
}
static const char *controllers(const behavior_function_context *ctx, void *, const pin_value *, pin_value *out)
{
    pin_array *rows = pin_array_snapshot_allocate(
        ctx->value_storage,
        PIN_STRUCT,
        "PhysicalControllerState",
        INPUT_CONTROLLER_MAX
    );
    for (int slot = 0; slot < INPUT_CONTROLLER_MAX; slot++) {
        authoring_physical_controller *row = arena_alloc(ctx->value_storage, sizeof *row);
        input_controller_state state = controller_state(slot);
        *row = (authoring_physical_controller){ .slot = slot,
                                                .connected = state.connected,
                                                .buttons = (int)state.buttons };
        if (state.connected) {
            row->left_x = cf_joypad_axis(slot, CF_JOYPAD_AXIS_LEFTX) / 32768.0;
            row->left_y = cf_joypad_axis(slot, CF_JOYPAD_AXIS_LEFTY) / 32768.0;
            row->right_x = cf_joypad_axis(slot, CF_JOYPAD_AXIS_RIGHTX) / 32768.0;
            row->right_y = cf_joypad_axis(slot, CF_JOYPAD_AXIS_RIGHTY) / 32768.0;
            row->left_trigger = cf_joypad_axis(slot, CF_JOYPAD_AXIS_TRIGGERLEFT) / 32767.0;
            row->right_trigger = cf_joypad_axis(slot, CF_JOYPAD_AXIS_TRIGGERRIGHT) /
                                 32767.0;
        }
        rows->items[slot].ptr = row;
    }
    out[0].ptr = rows;
    return NULL;
}
#define CALL(CONTEXT)                                                          \
    { .service = "authoring_platform",                                         \
      .missing_service = "Authoring platform host service unavailable",        \
      .context = CONTEXT }
const node_host_call AUTHORING_PLATFORM_REQUEST_CALL = CALL(request),
                     AUTHORING_PLATFORM_POLL_CALL = CALL(poll),
                     AUTHORING_PLATFORM_CANCEL_CALL = CALL(cancel),
                     AUTHORING_PLATFORM_ENVIRONMENT_CALL = CALL(environment),
                     AUTHORING_PHYSICAL_CONTROLLERS_CALL = CALL(controllers);
const node_host_call AUTHORING_PLATFORM_RELEASE_CALL = {
    .service = "authoring_platform",
    .missing_service = "Authoring platform host service unavailable",
    .invoke = release
};
const node_access AUTHORING_PLATFORM_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
