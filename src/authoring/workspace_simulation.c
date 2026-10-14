#include "authoring/workspace_simulation.h"

#include "render/shader_publish.h"
#include "runtime/components.h"
#include "runtime/declared_components.h"
#include "support/asset_json.h"
#include "support/rooted_files.h"
#include "support/sha256.h"
#include "support/utf8.h"
#include "support/xalloc.h"
#include "ui/input_keys.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct workspace_simulation
{
    workspace_runtime *runtime;
    int session;
    unsigned seed;
    uint64_t end_step;
    bool ui_interrupted;
    char identity[65], hash[65], timeline_hash[65], error[512];
    char *timeline;
};
typedef struct
{
    render_resource_scope render;
    declared_components_scope types;
    world *world;
    engine *engine;
} scope;
static bool failure(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static bool digest_valid(const char *s)
{
    if (!s || strlen(s) != 64) {
        return false;
    }
    for (int i = 0; i < 64; i++) {
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}
static av_value *key_value(av_doc *doc, const input_keyframe *k)
{
    av_value *a = av_arr(doc);
    av_arr_append(a, av_sint(doc, k->step));
    const float values[] = { k->movement.x, k->movement.y, k->aim.x, k->aim.y };
    for (int i = 0; i < 4; i++) {
        av_arr_append(a, av_float(doc, values[i]));
    }
    av_arr_append(a, av_bool(doc, k->aim_point));
    av_arr_append(a, av_bool(doc, k->firing));
    char bits[17];
    snprintf(bits, sizeof bits, "%016llx", (unsigned long long)k->controls);
    av_arr_append(a, av_str(doc, bits));
    av_value *controllers = av_arr(doc);
    av_arr_append(a, controllers);
    for (int i = 0; i < INPUT_CONTROLLER_MAX; i++) {
        av_value *c = av_arr(doc);
        av_arr_append(controllers, c);
        av_arr_append(c, av_bool(doc, k->controllers[i].connected));
        av_arr_append(c, av_uint(doc, k->controllers[i].buttons));
    }
    av_arr_append(a, av_bool(doc, k->pointer.active));
    const float pointer[] = { k->pointer.origin.x,    k->pointer.origin.y,
                              k->pointer.origin.z,    k->pointer.direction.x,
                              k->pointer.direction.y, k->pointer.direction.z };
    for (int i = 0; i < 6; i++) {
        av_arr_append(a, av_float(doc, pointer[i]));
    }
    return a;
}
bool workspace_simulation_encode(
    const input_keyframe *keys,
    int count,
    unsigned seed,
    uint64_t steps,
    const char *identity,
    char **json,
    char *error,
    size_t size
)
{
    if (!keys || count < 1 || count > WORKSPACE_TIMELINE_KEYS_MAX ||
        keys[0].step || steps > INT_MAX || !digest_valid(identity) || !json) {
        return failure(error, size, "Invalid bounded input timeline or executable identity");
    }
    for (int i = 0; i < count; i++) {
        if (!input_keyframe_valid(&keys[i]) ||
            (i && keys[i].step < keys[i - 1].step) || (uint64_t)keys[i].step > steps) {
            return failure(error, size, "Timeline keys must be finite and ordered within recorded fixed steps");
        }
    }
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "version", 2);
    av_obj_add_obj(doc, root, "initial_save");
    av_obj_add_str(doc, root, "identity", identity);
    av_obj_add_uint(doc, root, "seed", seed);
    av_obj_add_uint(doc, root, "steps", steps);
    av_value *a = av_obj_add_arr(doc, root, "keys");
    for (int i = 0; i < count; i++) {
        av_arr_append(a, key_value(doc, &keys[i]));
    }
    char *text = av_json_write(root);
    av_doc_free(doc);
    if (!text || strlen(text) > WORKSPACE_TIMELINE_BYTES_MAX) {
        free(text);
        return failure(error, size, "Input timeline exceeds one MiB");
    }
    *json = text;
    return true;
}
static bool json_depth(const char *text)
{
    bool quoted = false, escape = false;
    int depth = 0;
    for (const char *p = text; *p; p++) {
        if (quoted) {
            if (escape) {
                escape = false;
            } else if (*p == '\\') {
                escape = true;
            } else if (*p == '"') {
                quoted = false;
            }
            continue;
        }
        if (*p == '"') {
            quoted = true;
        } else if (*p == '{' || *p == '[') {
            if (++depth > 6) {
                return false;
            }
        } else if (*p == '}' || *p == ']') {
            if (--depth < 0) {
                return false;
            }
        }
    }
    return !depth && !quoted;
}
static bool number(yyjson_val *v, float *out)
{
    if (!yyjson_is_num(v)) {
        return false;
    }
    double x = yyjson_get_num(v);
    if (!isfinite(x) || fabs(x) > FLT_MAX) {
        return false;
    }
    float y = (float)x;
    if (!isfinite(y)) {
        return false;
    }
    *out = y;
    return true;
}
static bool read_key(yyjson_val *a, input_keyframe *k)
{
    if (!yyjson_is_arr(a) || yyjson_arr_size(a) != 16) {
        return false;
    }
    yyjson_val *step = yyjson_arr_get(a, 0);
    if (!yyjson_is_int(step) || yyjson_get_sint(step) < 0 ||
        yyjson_get_uint(step) > INT_MAX) {
        return false;
    }
    k->step = (int)yyjson_get_uint(step);
    if (!number(yyjson_arr_get(a, 1), &k->movement.x) ||
        !number(yyjson_arr_get(a, 2), &k->movement.y) ||
        !number(yyjson_arr_get(a, 3), &k->aim.x) ||
        !number(yyjson_arr_get(a, 4), &k->aim.y)) {
        return false;
    }
    yyjson_val *point = yyjson_arr_get(a, 5), *firing = yyjson_arr_get(a, 6),
               *bits = yyjson_arr_get(a, 7), *cs = yyjson_arr_get(a, 8),
               *active = yyjson_arr_get(a, 9);
    if (!yyjson_is_bool(point) || !yyjson_is_bool(firing) ||
        !yyjson_is_str(bits) || yyjson_get_len(bits) != 16 || !yyjson_is_arr(cs) ||
        yyjson_arr_size(cs) != INPUT_CONTROLLER_MAX || !yyjson_is_bool(active)) {
        return false;
    }
    k->aim_point = yyjson_get_bool(point);
    k->firing = yyjson_get_bool(firing);
    k->pointer.active = yyjson_get_bool(active);
    const char *hex = yyjson_get_str(bits);
    for (int i = 0; i < 16; i++) {
        unsigned digit = hex[i] >= '0' && hex[i] <= '9' ? (unsigned)(hex[i] - '0')
                       : hex[i] >= 'a' && hex[i] <= 'f'
                           ? (unsigned)(hex[i] - 'a' + 10)
                           : 16;
        if (digit > 15) {
            return false;
        }
        k->controls = (k->controls << 4) | digit;
    }
    for (int i = 0; i < INPUT_CONTROLLER_MAX; i++) {
        yyjson_val *c = yyjson_arr_get(cs, (size_t)i);
        if (!yyjson_is_arr(c) || yyjson_arr_size(c) != 2) {
            return false;
        }
        yyjson_val *connected = yyjson_arr_get(c, 0), *buttons = yyjson_arr_get(c, 1);
        if (!yyjson_is_bool(connected) || !yyjson_is_uint(buttons) ||
            yyjson_get_uint(buttons) > UINT32_MAX) {
            return false;
        }
        k->controllers[i].connected = yyjson_get_bool(connected);
        k->controllers[i].buttons = (uint32_t)yyjson_get_uint(buttons);
    }
    float *fields[] = { &k->pointer.origin.x,    &k->pointer.origin.y,
                        &k->pointer.origin.z,    &k->pointer.direction.x,
                        &k->pointer.direction.y, &k->pointer.direction.z };
    for (int i = 0; i < 6; i++) {
        if (!number(yyjson_arr_get(a, (size_t)(10 + i)), fields[i])) {
            return false;
        }
    }
    return input_keyframe_valid(k);
}
bool workspace_simulation_decode(
    const char *text,
    const char *identity,
    input_keyframe **keys,
    int *count,
    unsigned *seed,
    uint64_t *steps,
    char *error,
    size_t size
)
{
    if (!text || strlen(text) > WORKSPACE_TIMELINE_BYTES_MAX || !json_depth(text) ||
        !digest_valid(identity) || !keys || !count || !seed || !steps) {
        return failure(error, size, "Invalid bounded input timeline");
    }
    yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *version = yyjson_obj_get(root, "version"),
               *id = yyjson_obj_get(root, "identity"),
               *sd = yyjson_obj_get(root, "seed"),
               *st = yyjson_obj_get(root, "steps"),
               *rows = yyjson_obj_get(root, "keys"),
               *initial_save = yyjson_obj_get(root, "initial_save");
    size_t n = yyjson_arr_size(rows);
    bool ok = yyjson_is_obj(root) && yyjson_obj_size(root) == 6 &&
              yyjson_is_obj(initial_save) && yyjson_obj_size(initial_save) == 0 &&
              yyjson_is_int(version) && yyjson_get_uint(version) == 2 &&
              yyjson_is_str(id) && !strcmp(yyjson_get_str(id), identity) &&
              yyjson_is_uint(sd) && yyjson_get_uint(sd) <= UINT_MAX &&
              yyjson_is_uint(st) && yyjson_get_uint(st) <= INT_MAX &&
              yyjson_is_arr(rows) && n > 0 && n <= WORKSPACE_TIMELINE_KEYS_MAX;
    input_keyframe *result = ok ? xcalloc(n, sizeof *result) : NULL;
    for (size_t i = 0; ok && i < n; i++) {
        ok = read_key(yyjson_arr_get(rows, i), &result[i]) &&
             (!i ? !result[i].step : result[i].step >= result[i - 1].step) &&
             (uint64_t)result[i].step <= yyjson_get_uint(st);
    }
    if (ok) {
        *keys = result;
        *count = (int)n;
        *seed = (unsigned)yyjson_get_uint(sd);
        *steps = yyjson_get_uint(st);
    } else {
        free(result);
    }
    yyjson_doc_free(doc);
    return ok || failure(error, size, "Timeline schema, ordering or executable/SDK identity differs");
}

bool workspace_simulation_keys(const char *const *keys, int count, uint64_t *out, char *error, size_t size)
{
    if (count < 0 || count > 64 || (count && !keys) || !out) {
        return failure(error, size, "At most64 physical control names are admitted");
    }
    static const struct
    {
        const char *name;
        Control control;
    } names[] = { { "MouseLeft", CONTROL_MOUSE_LEFT },
                  { "MouseRight", CONTROL_MOUSE_RIGHT },
                  { "MouseMiddle", CONTROL_MOUSE_MIDDLE },
                  { "Space", CONTROL_SPACE },
                  { "Shift", CONTROL_SHIFT },
                  { "Control", CONTROL_CONTROL },
                  { "Alt", CONTROL_ALT },
                  { "Tab", CONTROL_TAB },
                  { "Enter", CONTROL_ENTER },
                  { "Backspace", CONTROL_BACKSPACE },
                  { "ArrowUp", CONTROL_ARROW_UP },
                  { "ArrowDown", CONTROL_ARROW_DOWN },
                  { "ArrowLeft", CONTROL_ARROW_LEFT },
                  { "ArrowRight", CONTROL_ARROW_RIGHT },
                  { "Up", CONTROL_ARROW_UP },
                  { "Down", CONTROL_ARROW_DOWN },
                  { "Left", CONTROL_ARROW_LEFT },
                  { "Right", CONTROL_ARROW_RIGHT } };
    uint64_t bits = 0;
    for (int i = 0; i < count; i++) {
        const char *key = keys[i];
        if (!key || !key[0] || strlen(key) > 32) {
            return failure(error, size, "Physical control name is invalid");
        }
        Control control = CONTROL_NONE;
        if (!key[1]) {
            char c = key[0];
            if (c >= 'a' && c <= 'z') {
                c = (char)(c - 'a' + 'A');
            }
            if (c >= 'A' && c <= 'Z') {
                control = (Control)(CONTROL_A + c - 'A');
            } else if (c >= '0' && c <= '9') {
                control = (Control)(CONTROL_0 + c - '0');
            }
        }
        for (size_t n = 0; !control && n < sizeof names / sizeof *names; n++) {
            if (!strcmp(key, names[n].name)) {
                control = names[n].control;
            }
        }
        if (!control) {
            return failure(error, size, "Unknown physical control name");
        }
        bits |= UINT64_C(1) << control;
    }
    *out = bits;
    return true;
}
static bool enter(workspace_simulation *s, int session, scope *scope)
{
    if (workspace_runtime_read_only(s->runtime)) {
        return failure(s->error, sizeof s->error, "Static presentation has no simulation/input capability");
    }
    workspace_runtime_result current = workspace_runtime_current(s->runtime);
    if (!current.ready || current.session != session) {
        return failure(s->error, sizeof s->error, "Runtime session is stale");
    }
    if (s->session != session) {
        s->session = session;
        s->seed = 0;
        s->end_step = 0;
        s->ui_interrupted = false;
        s->identity[0] = s->hash[0] = s->timeline_hash[0] = 0;
        free(s->timeline);
        s->timeline = NULL;
    }
    scope->world = (world *)workspace_runtime_world(s->runtime);
    scope->engine = world_engine(scope->world);
    if (!render_resource_scope_enter(
            render_resource_context_for_world(scope->world),
            &scope->render,
            s->error,
            sizeof s->error
        )) {
        return false;
    }
    if (!declared_components_scope_enter(
            workspace_runtime_types(s->runtime),
            &scope->types
        )) {
        bool ok = render_resource_scope_leave(&scope->render, s->error, sizeof s->error);
        assert(ok);
        (void)ok;
        return failure(s->error, sizeof s->error, "Runtime type scope is unavailable");
    }
    s->error[0] = 0;
    return true;
}
static void leave(scope *scope)
{
    bool types = declared_components_scope_leave(&scope->types),
         render = render_resource_scope_leave(&scope->render, NULL, 0);
    assert(types && render);
    (void)types;
    (void)render;
}
static workspace_simulation_result result(workspace_simulation *s, engine *e, bool ok, bool admitted)
{
    int keys = 0;
    if (e) {
        engine_recorded_input(e, &keys);
    }
    const engine_frame_timing *timing = e ? engine_last_timing(e) : NULL;
    return (workspace_simulation_result){
        .ok = ok,
        .admitted = admitted,
        .recording = e && engine_recording_input(e),
        .replaying = e && engine_replaying_input(e),
        .truncated = e && engine_recorded_input_truncated(e),
        .keys = keys,
        .end_step = s->end_step,
        .steps = timing ? timing->simulation_steps : 0,
        .scale = e ? engine_time_scale(e) : 1,
        .timeline = s->timeline ? s->timeline : "",
        .error = s->error,
        .hash = s->hash
    };
}
workspace_simulation *workspace_simulation_new(workspace_runtime *runtime)
{
    assert(runtime);
    workspace_simulation *s = xcalloc(1, sizeof *s);
    s->runtime = runtime;
    return s;
}
void workspace_simulation_free(workspace_simulation *s)
{
    if (!s) {
        return;
    }
    scope scope = { 0 };
    if (s->session && enter(s, s->session, &scope)) {
        engine_stop_recording(scope.engine);
        engine_stop_replay(scope.engine);
        engine_cancel_input(scope.engine);
        leave(&scope);
    }
    free(s->timeline);
    free(s);
}
static void attest(workspace_simulation *s, int session, const char *operation, av_doc *doc)
{
    bool ok = workspace_runtime_record_call(s->runtime, session, operation, av_doc_root(doc));
    assert(ok);
    (void)ok;
}
workspace_simulation_result workspace_simulation_sample(
    workspace_simulation *s,
    int session,
    const workspace_simulation_input *sample
)
{
    input_keyframe key = { 0 };
    if (sample) {
        key = (input_keyframe){ .movement = sample->movement,
                                .aim = sample->aim,
                                .aim_point = sample->aim_point,
                                .firing = sample->firing,
                                .controls = sample->controls,
                                .pointer = sample->pointer };
        memcpy(key.controllers, sample->controllers, sizeof key.controllers);
    }
    if (!sample || !input_keyframe_valid(&key)) {
        failure(s->error, sizeof s->error, "Finite valid physical input sample required");
        return result(s, NULL, false, false);
    }
    scope scope = { 0 };
    if (!enter(s, session, &scope)) {
        return result(s, NULL, false, false);
    }
    engine *e = scope.engine;
    if (!engine_has_input(e) || !engine_is_playing(e) || engine_replaying_input(e)) {
        failure(s->error, sizeof s->error, "Live input requires a playing session with input and no replay");
        workspace_simulation_result r = result(s, e, false, false);
        leave(&scope);
        return r;
    }
    if (!engine_devices_admissible(e, sample->controls, sample->controllers)) {
        failure(s->error, sizeof s->error, "Physical input transition budget refused before mutation");
        workspace_simulation_result r = result(s, e, false, false);
        leave(&scope);
        return r;
    }
    engine_set_focus(e, ENGINE_FOCUS_VIEWPORT);
    bool firing = false;
    int devices = engine_sample_devices(e, sample->controls, sample->controllers, &firing);
    int movement = engine_sample_movement(
        e,
        sample->movement.x,
        sample->movement.y
    );
    int aim = engine_sample_aim(e, sample->aim, sample->firing || firing, sample->aim_point);
    int pointer = engine_sample_pointer_ray(e, sample->pointer);
    bool ok = devices >= 0 && movement >= 0 && aim >= 0 && pointer >= 0;
    if (!ok) {
        failure(s->error, sizeof s->error, "Input admitted but target behavior execution failed");
    }
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_bool(doc, root, "ok", ok);
    av_obj_set(doc, root, "sample", key_value(doc, &key));
    attest(s, session, "physical-input", doc);
    av_doc_free(doc);
    workspace_simulation_result r = result(s, e, ok, true);
    leave(&scope);
    return r;
}
workspace_simulation_result workspace_simulation_clear(workspace_simulation *s, int session)
{
    scope scope = { 0 };
    if (!enter(s, session, &scope)) {
        return result(s, NULL, false, false);
    }
    engine_cancel_input(scope.engine);
    av_doc *doc = av_doc_new();
    av_doc_set_root(doc, av_obj(doc));
    attest(s, session, "input-focus-lost", doc);
    av_doc_free(doc);
    workspace_simulation_result r = result(s, scope.engine, true, true);
    leave(&scope);
    return r;
}
static bool identity(workspace_simulation *s, int session, char out[65])
{
    const char *artifact = workspace_runtime_artifact_sha256(s->runtime),
               *build = workspace_runtime_sdk_build(s->runtime),
               *manifest = workspace_runtime_sdk_manifest(s->runtime);
    if (!digest_valid(artifact) || !digest_valid(build) || !digest_valid(manifest)) {
        return failure(s->error, sizeof s->error, "Recording requires exact loaded executable and SDK identity");
    }
    if (!workspace_runtime_verify_artifact(
            s->runtime,
            session,
            s->error,
            sizeof s->error
        )) {
        return false;
    }
    char combined[197];
    snprintf(combined, sizeof combined, "%s:%s:%s", artifact, build, manifest);
    sha256_hex(combined, strlen(combined), out);
    return true;
}
workspace_simulation_result workspace_simulation_timeline(
    workspace_simulation *s,
    int session,
    const char *operation,
    unsigned seed,
    const char *text
)
{
    char *replay_text = NULL;
    if (operation && !strcmp(operation, "replay") && text) {
        size_t bytes = 0;
        while (bytes <= WORKSPACE_TIMELINE_BYTES_MAX && text[bytes]) {
            bytes++;
        }
        if (bytes > WORKSPACE_TIMELINE_BYTES_MAX) {
            failure(s->error, sizeof s->error, "Timeline exceeds its byte budget");
            return result(s, NULL, false, false);
        }
        replay_text = xmalloc(bytes + 1);
        memcpy(replay_text, text, bytes + 1);
    }
    scope scope = { 0 };
    if (!enter(s, session, &scope)) {
        free(replay_text);
        return result(s, NULL, false, false);
    }
    engine *e = scope.engine;
    bool ok = true, admitted = false;
    char candidate_identity[65];
    if (!operation || !engine_has_input(e)) {
        ok = failure(s->error, sizeof s->error, "Timeline operation requires target input");
    } else if (!strcmp(operation, "record")) {
        if (!identity(s, session, candidate_identity)) {
            ok = false;
        } else {
            player_restart_input input = { .seed = seed,
                                           .initial_save_json = "{}",
                                           .record_limit = WORKSPACE_TIMELINE_KEYS_MAX };
            workspace_runtime_result
                restarted = workspace_runtime_restart(s->runtime, session, &input, &admitted);
            ok = restarted.ok;
            if (ok) {
                memcpy(s->identity, candidate_identity, sizeof s->identity);
                s->seed = seed;
                free(s->timeline);
                s->timeline = NULL;
                s->hash[0] = s->timeline_hash[0] = 0;
                s->end_step = 0;
                s->ui_interrupted = false;
                bool attached = workspace_runtime_input_timeline(s->runtime, session, NULL);
                assert(attached);
                (void)attached;
                admitted = true;
            } else {
                failure(s->error, sizeof s->error, "Fresh bounded recording could not start");
            }
        }
    } else if (!strcmp(operation, "stop")) {
        engine_stop_recording(e);
        admitted = true;
    } else if (!strcmp(operation, "export")) {
        int count;
        const input_keyframe *keys = engine_recorded_input(e, &count);
        if (!identity(s, session, candidate_identity) ||
            strcmp(candidate_identity, s->identity)) {
            ok = failure(s->error, sizeof s->error, "Recording executable changed or identity is unavailable");
        } else if (s->ui_interrupted) {
            ok = failure(s->error, sizeof s->error, "UI interaction interrupted gameplay-only recording; start a fresh recording before export");
        } else if (engine_recorded_input_truncated(e)) {
            ok = failure(s->error, sizeof s->error, "Recording exceeded its key budget; partial timeline cannot be exported");
        } else if (!s->identity[0]) {
            ok = failure(s->error, sizeof s->error, "No bounded recording belongs to this runtime session");
        } else {
            char *encoded = NULL;
            ok = workspace_simulation_encode(
                keys,
                count,
                s->seed,
                engine_last_timing(e)->simulation_steps,
                s->identity,
                &encoded,
                s->error,
                sizeof s->error
            );
            if (ok) {
                free(s->timeline);
                s->timeline = encoded;
                s->end_step = engine_last_timing(e)->simulation_steps;
                bool attached = workspace_runtime_input_timeline(s->runtime, session, encoded);
                assert(attached);
                (void)attached;
                sha256_hex(encoded, strlen(encoded), s->hash);
                snprintf(s->timeline_hash, sizeof s->timeline_hash, "%s", s->hash);
            }
        }
    } else if (!strcmp(operation, "replay")) {
        input_keyframe *keys = NULL;
        int count = 0;
        unsigned stored_seed = 0;
        uint64_t steps = 0;
        ok = identity(s, session, candidate_identity) && workspace_simulation_decode(
                                                             replay_text,
                                                             candidate_identity,
                                                             &keys,
                                                             &count,
                                                             &stored_seed,
                                                             &steps,
                                                             s->error,
                                                             sizeof s->error
                                                         );
        if (ok) {
            player_restart_input input = { .seed = stored_seed,
                                           .initial_save_json = "{}",
                                           .replay = keys,
                                           .replay_count = count };
            workspace_runtime_result
                restarted = workspace_runtime_restart(s->runtime, session, &input, &admitted);
            ok = restarted.ok;
            if (ok) {
                memcpy(s->identity, candidate_identity, sizeof s->identity);
                s->seed = stored_seed;
                free(s->timeline);
                s->timeline = replay_text;
                replay_text = NULL;
                s->end_step = steps;
                bool attached = workspace_runtime_input_timeline(s->runtime, session, s->timeline);
                assert(attached);
                (void)attached;
                sha256_hex(s->timeline, strlen(s->timeline), s->hash);
                snprintf(s->timeline_hash, sizeof s->timeline_hash, "%s", s->hash);
                admitted = true;
            } else {
                failure(s->error, sizeof s->error, "Fresh replay world could not start");
            }
        }
        free(keys);

    } else if (!strcmp(operation, "stop-replay")) {
        engine_stop_replay(e);
        admitted = true;
    } else if (strcmp(operation, "status")) {
        ok = failure(s->error, sizeof s->error, "Unknown timeline operation");
    }
    if (!ok && admitted) {
        engine_stop_recording(e);
        engine_stop_replay(e);
        s->ui_interrupted = true;
    }
    free(replay_text);
    if (admitted) {
        av_doc *doc = av_doc_new();
        av_value *root = av_obj(doc);
        av_doc_set_root(doc, root);
        av_obj_add_str(doc, root, "action", operation);
        av_obj_add_uint(doc, root, "seed", s->seed);
        av_obj_add_str(doc, root, "timeline_sha256", s->timeline_hash);
        attest(s, session, "timeline", doc);
        av_doc_free(doc);
    }
    workspace_simulation_result r = result(s, e, ok, admitted);
    leave(&scope);
    return r;
}
workspace_simulation_result workspace_simulation_time_scale(
    workspace_simulation *s,
    int session,
    bool write,
    double scale
)
{
    if (!isfinite(scale) || scale <= 0 || scale > 8) {
        failure(s->error, sizeof s->error, "Time scale must be finite and positive, at most8");
        return result(s, NULL, false, false);
    }
    scope scope = { 0 };
    if (!enter(s, session, &scope)) {
        return result(s, NULL, false, false);
    }
    if (write) {
        engine_set_time_scale(scope.engine, scale);
        av_doc *doc = av_doc_new();
        av_value *root = av_obj(doc);
        av_doc_set_root(doc, root);
        av_obj_set(doc, root, "scale", av_real(doc, engine_time_scale(scope.engine)));
        attest(s, session, "time-scale", doc);
        av_doc_free(doc);
    }
    workspace_simulation_result r = result(s, scope.engine, true, write);
    leave(&scope);
    return r;
}
workspace_simulation_result workspace_simulation_materials(
    workspace_simulation *s,
    int session,
    const char *operation,
    float x,
    float y,
    float radius
)
{
    if (!operation || !isfinite(x) || !isfinite(y) || !isfinite(radius) ||
        radius < 0 || radius > 4096) {
        failure(s->error, sizeof s->error, "Finite generation view and radius0..4096 required");
        return result(s, NULL, false, false);
    }
    scope scope = { 0 };
    if (!enter(s, session, &scope)) {
        return result(s, NULL, false, false);
    }
    materials *m = world_materials(scope.world);
    bool ok = true, admitted = false;
    if (!m) {
        ok = failure(s->error, sizeof s->error, "Runtime has no material generation service");
    } else if (!strcmp(operation, "status")) {
    } else if (!strcmp(operation, "inspect")) {
        material_hash_parts parts = materials_hash_parts(m);
        snprintf(s->hash, sizeof s->hash, "%016llx", (unsigned long long)parts.hash);
    } else if (strcmp(operation, "prepare") && strcmp(operation, "reset")) {
        ok = failure(s->error, sizeof s->error, "Unknown material generation operation");
    } else if (
        engine_is_playing(scope.engine) || engine_last_timing(scope.engine)->simulation_steps
    ) {
        ok = failure(s->error, sizeof s->error, "Generated preview operations require a fresh paused world");
    } else {
        admitted = true;
        if (!strcmp(operation, "reset")) {
            materials_reset(m);
        } else {
            material_stream_view view = { x, y, radius, 0 };
            materials_set_stream_deferred(m, true);
            materials_set_stream_views(m, &view, 1);
            const char *error = materials_prepare(m);
            if (error) {
                ok = failure(s->error, sizeof s->error, error);
            }
        }
        av_doc *doc = av_doc_new();
        av_value *root = av_obj(doc);
        av_doc_set_root(doc, root);
        av_obj_add_str(doc, root, "action", operation);
        av_obj_add_float(doc, root, "x", x);
        av_obj_add_float(doc, root, "y", y);
        av_obj_add_float(doc, root, "radius", radius);
        av_obj_add_bool(doc, root, "ok", ok);
        attest(s, session, "material-generation", doc);
        av_doc_free(doc);
    }
    workspace_simulation_result r = result(s, scope.engine, ok, admitted);
    r.pending = m && materials_streaming(m);
    leave(&scope);
    return r;
}

workspace_simulation_result workspace_simulation_ui(
    workspace_simulation *s,
    int session,
    const char *operation,
    const char *name,
    const char *text,
    float x,
    float y,
    float wheel_x,
    float wheel_y,
    int button,
    unsigned modifiers
)
{
    static const char *const operations[] = { "move",   "leave", "down",
                                              "up",     "wheel", "key-down",
                                              "key-up", "text" };
    int type = -1;
    for (int i = 0; operation && i < 8; i++) {
        if (!strcmp(operation, operations[i])) {
            type = i;
        }
    }
    rml_input input = { .type = (rml_input_type)type,
                        .x = x,
                        .y = y,
                        .wheel_x = wheel_x,
                        .wheel_y = wheel_y,
                        .button = button,
                        .modifiers = modifiers,
                        .text = text,
                        .text_size = text ? strlen(text) : 0 };
    if (type == RML_INPUT_KEY_PRESS || type == RML_INPUT_KEY_RELEASE) {
        for (int i = 1; name && i <= RML_KEY_RMETA; i++) {
            char scratch[4];
            const char *canonical = ui_key_name((rml_key)i, scratch);
            if (canonical && !strcmp(canonical, name)) {
                input.key = (rml_input_key)i;
                break;
            }
        }
    }
    unsigned capture = 0;
    bool typing = false, dispatched = false;
    bool ok = workspace_runtime_ui_input(
        s->runtime,
        session,
        &input,
        &capture,
        &typing,
        &dispatched,
        s->error,
        sizeof s->error
    );
    engine *e = NULL;
    bool admitted = ok || dispatched;
    if (admitted) {
        char dispatch_error[sizeof s->error];
        snprintf(dispatch_error, sizeof dispatch_error, "%s", s->error);
        scope scope = { 0 };
        if (enter(s, session, &scope)) {
            e = scope.engine;
            /* UI actions are not represented by input_keyframe. Prevent a
             * misleading gameplay-only recording/replay across UI effects. */
            if (dispatched || (capture && type != RML_INPUT_POINTER_MOVE &&
                               type != RML_INPUT_POINTER_LEAVE)) {
                if (engine_recording_input(e)) {
                    s->ui_interrupted = true;
                }
                engine_stop_recording(e);
                engine_stop_replay(e);
            }
            leave(&scope);
            if (!ok) {
                snprintf(s->error, sizeof s->error, "%s", dispatch_error);
            }
        } else {
            ok = false;
        }
    }
    workspace_simulation_result r = result(s, e, ok, admitted);
    r.captured = capture;
    r.typing = typing;
    return r;
}
