#include "authoring/workspace_simulation.h"
#include "authoring/workspace_simulation_definition.h"
#include "execution/function.h"
#include "types/array.h"

#include <limits.h>
static void outputs(const behavior_function_context *c, pin_value *out, workspace_simulation_result r)
{
    out[0].b = r.ok;
    out[1].b = r.admitted;
    out[2].b = r.recording;
    out[3].b = r.replaying;
    out[4].b = r.truncated;
    out[5].b = r.pending;
    out[6].i = r.keys;
    out[7].i = r.steps > INT_MAX ? INT_MAX : (int)r.steps;
    out[8].d = r.scale;
    out[9].str = c->copy_string(c->value_storage, r.timeline ? r.timeline : "");
    out[10].str = c->copy_string(c->value_storage, r.error ? r.error : "");
    out[11].str = c->copy_string(c->value_storage, r.hash ? r.hash : "");
    out[12].i = (int)r.captured;
    out[13].b = r.typing;
    out[14].i = r.end_step > INT_MAX ? INT_MAX : (int)r.end_step;
}
static const char *sample(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    const pin_array *keys = in[1].ptr, *connected = in[2].ptr, *buttons = in[3].ptr;
    int n = keys ? keys->count : 0, nc = connected ? connected->count : 0,
        nb = buttons ? buttons->count : 0;
    if (n < 0 || n > 64 || (n && !keys->items) || nc != nb ||
        (nc != 0 && nc != INPUT_CONTROLLER_MAX) ||
        (nc && (!connected->items || !buttons->items))) {
        outputs(c, out, (workspace_simulation_result){ .error = "Physical input arrays require<=64 keys and0 or4 matching controller rows" });
        return NULL;
    }
    const char *names[64];
    for (int i = 0; i < n; i++) {
        names[i] = keys->items[i].str;
    }
    workspace_simulation_input value = {
        .movement = { in[4].f, in[5].f },
        .aim = { in[6].f, in[7].f },
        .aim_point = in[8].b,
        .firing = in[9].b,
        .pointer = { .origin = { in[10].f, in[11].f, in[12].f },
                     .direction = { in[13].f, in[14].f, in[15].f },
                     .active = in[16].b }
    };
    char error[256];
    if (!workspace_simulation_keys(names, n, &value.controls, error, sizeof error)) {
        outputs(c, out, (workspace_simulation_result){ .error = error });
        return NULL;
    }
    for (int i = 0; i < nc; i++) {
        value.controllers[i] = (input_controller_state){
            .connected = connected->items[i].b,
            .buttons = (uint32_t)buttons->items[i].i
        };
    }
    outputs(c, out, workspace_simulation_sample(host, in[0].i, &value));
    return NULL;
}
static const char *clear(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    outputs(c, out, workspace_simulation_clear(host, in[0].i));
    return NULL;
}
static const char *timeline(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    if (in[2].i < 0) {
        outputs(c, out, (workspace_simulation_result){ .error = "Recording seed must be nonnegative" });
        return NULL;
    }
    outputs(
        c,
        out,
        workspace_simulation_timeline(
            host,
            in[0].i,
            in[1].str,
            (unsigned)in[2].i,
            in[3].str
        )
    );
    return NULL;
}
static const char *scale(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    outputs(
        c,
        out,
        workspace_simulation_time_scale(host, in[0].i, in[1].b, in[2].d)
    );
    return NULL;
}
static const char *generate_materials(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    outputs(
        c,
        out,
        workspace_simulation_materials(
            host,
            in[0].i,
            in[1].str,
            in[2].f,
            in[3].f,
            in[4].f
        )
    );
    return NULL;
}
static const char *ui_input(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    outputs(
        c,
        out,
        workspace_simulation_ui(
            host,
            in[0].i,
            in[1].str,
            in[2].str,
            in[3].str,
            in[4].f,
            in[5].f,
            in[6].f,
            in[7].f,
            in[8].i,
            (unsigned)in[9].i
        )
    );
    return NULL;
}
#define OPS(X)                                                                 \
    X("Sample Workspace Simulation", sample)                                   \
    X("Clear Workspace Simulation Input", clear)                               \
    X("Workspace Simulation Timeline", timeline)                               \
    X("Workspace Simulation Time Scale", scale)                                \
    X("Workspace Material Generation", generate_materials)                     \
    X("Workspace Target UI Input", ui_input)
static void unavailable(const behavior_function_context *c, pin_value *out)
{
    outputs(c, out, (workspace_simulation_result){ .error = "Workspace simulation unavailable: no active project" });
}
#define CALL(N, F)                                                             \
    static const char *F##_dispatch(                                           \
        const behavior_function_context *c,                                    \
        void *host,                                                            \
        const pin_value *in,                                                   \
        pin_value *out                                                         \
    )                                                                          \
    {                                                                          \
        if (!host) {                                                           \
            unavailable(c, out);                                               \
            return NULL;                                                       \
        }                                                                      \
        return F(c, host, in, out);                                            \
    }                                                                          \
    static const node_host_call F##_call = {                                   \
        .service = "workspace_simulation",                                     \
        .missing_service = "Workspace simulation requires an authoring host",  \
        .optional_service = true,                                              \
        .context = F##_dispatch                                                \
    };
OPS(CALL)
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define BIND(N, F)                                                             \
    { .canonical_name = N,                                                     \
      .implementation = { .call = &F##_call, .access = &ACCESS } },
static const native_definition_binding BINDINGS[] = { OPS(BIND) };
const native_definition_pack WORKSPACE_SIMULATION_DEFINITION_PACK = {
    .definitions = WORKSPACE_SIMULATION_DEFINITIONS,
    .definition_count = WORKSPACE_SIMULATION_OPERATION_COUNT,
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
