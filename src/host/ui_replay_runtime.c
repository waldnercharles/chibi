#include "host/ui_replay_runtime.h"

#include "host/package_identity.h"
#include "render/ui.h"
#include "render/viewport.h"
#include "runtime/scenes/scene.h"
#include "support/sha256.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
struct ui_replay_session
{
    ui_replay *replay;
    yyjson_mut_doc *result;
    yyjson_mut_val *root, *events, *assertions;
    int event, assertion, completed_frames;
    char error[512];
    bool identity;
};
char *ui_replay_read_file(const char *path, size_t maximum, size_t *bytes)
{
    *bytes = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    struct stat info;
    if (fstat(fileno(file), &info) || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || (uint64_t)info.st_size > maximum) {
        fclose(file);
        return NULL;
    }
    size_t size = (size_t)info.st_size;
    char *data = xmalloc(size + 1);
    bool ok = fread(data, 1, size, file) == size && fgetc(file) == EOF &&
              !ferror(file);
    fclose(file);
    if (!ok) {
        free(data);
        return NULL;
    }
    data[size] = 0;
    *bytes = size;
    return data;
}
static bool fail(ui_replay_session *session, const char *message)
{
    snprintf(session->error, sizeof session->error, "%s", message);
    return false;
}
ui_replay_session *ui_replay_session_new(ui_replay *replay, const char *artifact, char *error, size_t size)
{
    size_t bytes;
    char *data = ui_replay_read_file(artifact, 64 * 1024 * 1024, &bytes);
    if (!data) {
        if (size) {
            snprintf(error, size, "Cannot hash bounded replay artifact");
        }
        return NULL;
    }
    ui_replay_session *s = xcalloc(1, sizeof *s);
    s->replay = replay;
    s->result = yyjson_mut_doc_new(NULL);
    s->root = yyjson_mut_obj(s->result);
    yyjson_mut_doc_set_root(s->result, s->root);
    s->events = yyjson_mut_arr(s->result);
    s->assertions = yyjson_mut_arr(s->result);
    yyjson_mut_obj_add_uint(s->result, s->root, "version", 1);
    yyjson_mut_obj_add_val(s->result, s->root, "events", s->events);
    yyjson_mut_obj_add_val(s->result, s->root, "assertions", s->assertions);
    char hash[65];
    sha256_hex(data, bytes, hash);
    free(data);
    yyjson_mut_obj_add_strcpy(s->result, s->root, "artifact_sha256", hash);
    yyjson_mut_val *source = yyjson_mut_obj(s->result);
    yyjson_mut_obj_add_strcpy(s->result, source, "kind", "prepared-artifact");
    yyjson_mut_obj_add_strcpy(s->result, source, "path", artifact);
    yyjson_mut_obj_add_strcpy(s->result, source, "sha256", hash);
    yyjson_mut_obj_add_val(s->result, s->root, "source", source);
    yyjson_mut_obj_add_strcpy(s->result, s->root, "request_sha256", replay->request_sha256);
    package_identity identity = { 0 };
    char identity_error[256];
    s->identity = package_identity_load(&identity, NULL, identity_error, sizeof identity_error) ==
                  PACKAGE_IDENTITY_OK;
    yyjson_mut_val *sdk = yyjson_mut_obj(s->result);
    yyjson_mut_obj_add_bool(s->result, sdk, "available", s->identity);
    if (s->identity) {
        yyjson_mut_obj_add_strcpy(s->result, sdk, "version", identity.version);
        yyjson_mut_obj_add_strcpy(s->result, sdk, "build_id", identity.build_id);
        yyjson_mut_obj_add_strcpy(s->result, sdk, "manifest_sha256", identity.manifest_sha256);
    } else {
        yyjson_mut_obj_add_strcpy(s->result, sdk, "error", identity_error);
    }
    package_identity_free(&identity);
    yyjson_mut_obj_add_val(s->result, s->root, "sdk", sdk);
    return s;
}
static rml_input_key key(const char *name)
{
    if (strlen(name) == 1 && *name >= 'A' && *name <= 'Z') {
        return RML_INPUT_KEY_A + (*name - 'A');
    }
    if (strlen(name) == 1 && *name >= '0' && *name <= '9') {
        return RML_INPUT_KEY_0 + (*name - '0');
    }
    static const struct
    {
        const char *name;
        rml_input_key key;
    } keys[] = { { "Space", RML_INPUT_KEY_SPACE },
                 { "Tab", RML_INPUT_KEY_TAB },
                 { "Enter", RML_INPUT_KEY_ENTER },
                 { "Escape", RML_INPUT_KEY_ESCAPE },
                 { "Backspace", RML_INPUT_KEY_BACKSPACE },
                 { "Delete", RML_INPUT_KEY_DELETE },
                 { "Insert", RML_INPUT_KEY_INSERT },
                 { "Home", RML_INPUT_KEY_HOME },
                 { "End", RML_INPUT_KEY_END },
                 { "PageUp", RML_INPUT_KEY_PAGE_UP },
                 { "PageDown", RML_INPUT_KEY_PAGE_DOWN },
                 { "Left", RML_INPUT_KEY_LEFT },
                 { "Right", RML_INPUT_KEY_RIGHT },
                 { "Up", RML_INPUT_KEY_UP },
                 { "Down", RML_INPUT_KEY_DOWN },
                 { ";", RML_INPUT_KEY_OEM_1 },
                 { "=", RML_INPUT_KEY_OEM_PLUS },
                 { ",", RML_INPUT_KEY_OEM_COMMA },
                 { "-", RML_INPUT_KEY_OEM_MINUS },
                 { ".", RML_INPUT_KEY_OEM_PERIOD },
                 { "/", RML_INPUT_KEY_OEM_2 },
                 { "`", RML_INPUT_KEY_OEM_3 },
                 { "[", RML_INPUT_KEY_OEM_4 },
                 { "\\", RML_INPUT_KEY_OEM_5 },
                 { "]", RML_INPUT_KEY_OEM_6 },
                 { "'", RML_INPUT_KEY_OEM_7 } };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        if (!strcmp(name, keys[i].name)) {
            return keys[i].key;
        }
    }
    if (*name == 'F') {
        return RML_INPUT_KEY_F1 + atoi(name + 1) - 1;
    }
    return RML_INPUT_KEY_UNKNOWN;
}
static rml_element *match(ui_replay_session *s, rml_context *context, const char *selector, bool allow_missing)
{
    rml_element *matches[2] = { 0 };
    int count = rml_element_query_selector_all(rml_context_root(context), selector, matches, 2);
    if (count > 1 || (!count && !allow_missing)) {
        fail(s, count > 1 ? "UI selector is ambiguous" : "UI selector is missing");
        return NULL;
    }
    return count ? matches[0] : NULL;
}
bool ui_replay_session_input(ui_replay_session *s, int frame, engine_viewport *viewport)
{
    if (s->event == s->replay->event_count ||
        s->replay->events[s->event].frame != frame) {
        return true;
    }
    render_ui *ui = engine_viewport_ui(viewport);
    rml_context *context = ui ? render_ui_context(ui) : NULL;
    if (!context) {
        return fail(s, "Replay has no live UI context");
    }
    while (s->event < s->replay->event_count &&
           s->replay->events[s->event].frame == frame) {
        ui_replay_event *event = &s->replay->events[s->event];
        rml_input input = { .modifiers = event->modifiers & 15 };
        if (event->modifiers & 128) {
#ifdef __APPLE__
            input.modifiers |= RML_INPUT_MOD_META;
#else
            input.modifiers |= RML_INPUT_MOD_CONTROL;
#endif
        }
        yyjson_mut_val *record = yyjson_val_mut_copy(
            s->result,
            yyjson_arr_get(yyjson_obj_get(yyjson_doc_get_root(s->replay->document), "events"), s->event)
        );
        yyjson_mut_arr_add_val(s->events, record);
        if (event->kind <= UI_REPLAY_WHEEL) {
            rml_element *element = match(s, context, event->selector, false);
            if (!element) {
                return false;
            }
            rml_vec2 offset = rml_element_absolute_offset(element, RML_BOX_BORDER),
                     size = rml_element_box_size(element, RML_BOX_BORDER);
            if (!isfinite(offset.x) || !isfinite(offset.y) ||
                !isfinite(size.x) || !isfinite(size.y) ||
                !rml_element_is_visible(element, true) || size.x <= 0 ||
                size.y <= 0 || rml_element_has_attribute(element, "disabled")) {
                return fail(s, "UI target is hidden, disabled or empty");
            }
            input.x = offset.x + (float)event->x * size.x;
            input.y = offset.y + (float)event->y * size.y;
            rml_element *hit = rml_context_element_at_point(
                context,
                (rml_vec2){ input.x, input.y },
                NULL
            );
            while (hit && hit != element) {
                hit = rml_element_parent(hit);
            }
            if (!hit) {
                return fail(s, "UI target is covered or outside its visible clip");
            }
            yyjson_mut_obj_add_real(s->result, record, "resolved_x", input.x);
            yyjson_mut_obj_add_real(s->result, record, "resolved_y", input.y);
            input.type = RML_INPUT_POINTER_MOVE;
            engine_viewport_ui_event(viewport, &input);
            if (event->kind == UI_REPLAY_CLICK || event->kind == UI_REPLAY_DOWN) {
                input.type = RML_INPUT_POINTER_DOWN;
                engine_viewport_ui_event(viewport, &input);
            }
            if (event->kind == UI_REPLAY_CLICK || event->kind == UI_REPLAY_UP) {
                input.type = RML_INPUT_POINTER_UP;
                engine_viewport_ui_event(viewport, &input);
            }
            if (event->kind == UI_REPLAY_WHEEL) {
                input.type = RML_INPUT_POINTER_WHEEL;
                input.wheel_y = (float)event->delta;
                engine_viewport_ui_event(viewport, &input);
            }
        } else if (event->kind == UI_REPLAY_TEXT) {
            if (!rml_context_focus_element(context)) {
                return fail(s, "Text replay requires a focused control");
            }
            input.type = RML_INPUT_TEXT_INPUT;
            input.text = event->text;
            input.text_size = strlen(event->text);
            engine_viewport_ui_event(viewport, &input);
        } else {
            input.type = event->kind == UI_REPLAY_KEY_DOWN ? RML_INPUT_KEY_PRESS
                                                           : RML_INPUT_KEY_RELEASE;
            input.key = key(event->key);
            engine_viewport_ui_event(viewport, &input);
        }
        yyjson_mut_obj_add_bool(s->result, record, "dispatched", true);
        s->event++;
    }
    return true;
}
bool ui_replay_session_check(
    ui_replay_session *s,
    int frame,
    const ui_world *world,
    const scene_instance *scene,
    engine_viewport *viewport
)
{
    s->completed_frames = frame + 1;
    rml_context *context = render_ui_context(engine_viewport_ui(viewport));
    while (s->assertion < s->replay->assertion_count &&
           s->replay->assertions[s->assertion].frame == frame) {
        ui_replay_assertion *assertion = &s->replay->assertions[s->assertion];
        yyjson_mut_val *record = yyjson_val_mut_copy(
            s->result,
            yyjson_arr_get(yyjson_obj_get(yyjson_doc_get_root(s->replay->document), "assertions"), s->assertion)
        );
        yyjson_mut_arr_add_val(s->assertions, record);
        bool passed = false;
        if (assertion->data) {
            ecs_entity authored = scene_instance_lookup(scene, assertion->entity);
            ecs_entity
                named = ecs_lookup(world->ecs, (ecs_entity){ 0 }, assertion->entity);
            if (authored.slot && named.slot &&
                ecs_entity_to_u64(authored) != ecs_entity_to_u64(named)) {
                return fail(s, "UI replay entity path is ambiguous between scene metadata and ECS names");
            }
            ecs_entity entity = authored.slot ? authored : named;
            yyjson_mut_obj_add_bool(s->result, record, "entity_resolved", entity.slot != 0);
            yyjson_mut_obj_add_str(s->result, record, "entity_path_kind", authored.slot ? "authored-scene" : "ecs-name");
            uint64_t id = ecs_entity_to_u64(entity);
            const av_value *value = NULL;
            for (int i = 0; entity.slot && i < ui_data_count(world->model); i++) {
                const ui_datum *datum = ui_data_at(world->model, i);
                if (datum->entity == id && !strcmp(datum->name, assertion->name)) {
                    value = datum->value;
                    break;
                }
            }
            if (value && yyjson_is_bool(assertion->expected) &&
                av_get_type(value) == AV_BOOL) {
                bool actual = av_get_bool(value);
                passed = actual == yyjson_get_bool(assertion->expected);
                yyjson_mut_obj_add_bool(s->result, record, "actual", actual);
            } else if (
                value && yyjson_is_num(assertion->expected) &&
                (av_get_type(value) == AV_REAL ||
                 av_get_type(value) == AV_SINT || av_get_type(value) == AV_UINT)
            ) {
                double actual = av_get_num(value);
                passed = isfinite(actual) &&
                         actual == yyjson_get_num(assertion->expected);
                if (isfinite(actual)) {
                    yyjson_mut_obj_add_real(s->result, record, "actual", actual);
                } else {
                    yyjson_mut_obj_add_null(s->result, record, "actual");
                }
            } else if (value && yyjson_is_str(assertion->expected) && av_get_type(value) == AV_STRING) {
                const char *actual = av_get_str(value);
                passed = !strcmp(actual, yyjson_get_str(assertion->expected));
                if (strlen(actual) <= 4096) {
                    yyjson_mut_obj_add_strcpy(s->result, record, "actual", actual);
                }
            } else {
                yyjson_mut_obj_add_null(s->result, record, "actual");
            }
        } else {
            rml_element *element = match(s, context, assertion->selector, true);
            if (s->error[0]) {
                return false;
            }
            bool visible = element && rml_element_is_visible(element, true);
            if (visible) {
                rml_vec2 bounds = rml_element_box_size(element, RML_BOX_BORDER);
                visible = bounds.x > 0 && bounds.y > 0;
            }
            passed = visible == yyjson_get_bool(assertion->expected);
            yyjson_mut_obj_add_bool(s->result, record, "actual", visible);
        }
        yyjson_mut_obj_add_bool(s->result, record, "passed", passed);
        s->assertion++;
        if (!passed) {
            snprintf(
                s->error,
                sizeof s->error,
                "UI replay assertion%d failed atframe%d",
                s->assertion - 1,
                frame
            );
            return false;
        }
    }
    return true;
}
bool ui_replay_session_finished(const ui_replay_session *s)
{
    return !s->error[0] && s->completed_frames == s->replay->frames &&
           s->event == s->replay->event_count &&
           s->assertion == s->replay->assertion_count;
}
const char *ui_replay_session_error(const ui_replay_session *s)
{
    return s->error;
}
bool ui_replay_session_write(
    ui_replay_session *s,
    const char *path,
    const char *capture,
    bool complete,
    const char *backend,
    const char *behavior,
    int width,
    int height,
    double fixed_dt
)
{
    complete = complete && ui_replay_session_finished(s);
    yyjson_mut_obj_add_bool(s->result, s->root, "passed", complete);
    yyjson_mut_obj_add_bool(s->result, s->root, "complete", complete && s->identity);
    yyjson_mut_obj_add_strcpy(
        s->result,
        s->root,
        "error",
        s->error[0] ? s->error
        : complete  ? ""
                    : "Host did not complete all requested frames"
    );
    yyjson_mut_obj_add_strcpy(s->result, s->root, "backend", backend);
    yyjson_mut_obj_add_strcpy(s->result, s->root, "behavior_backend", behavior);
    yyjson_mut_obj_add_int(s->result, s->root, "width", width);
    yyjson_mut_obj_add_int(s->result, s->root, "height", height);
    yyjson_mut_obj_add_real(s->result, s->root, "fixed_dt", fixed_dt);
    yyjson_mut_obj_add_int(s->result, s->root, "frames", s->replay->frames);
    yyjson_mut_obj_add_int(s->result, s->root, "completed_frames", s->completed_frames);
    if (capture && complete) {
        size_t bytes;
        char *data = ui_replay_read_file(capture, 2 * 1024 * 1024, &bytes);
        if (!data) {
            return false;
        }
        char hash[65];
        sha256_hex(data, bytes, hash);
        free(data);
        yyjson_mut_obj_add_strcpy(s->result, s->root, "capture_sha256", hash);
        yyjson_mut_obj_add_strcpy(s->result, s->root, "capture", capture);
    }
    char *unsigned_result = yyjson_mut_write(s->result, 0, NULL);
    char hash[65];
    sha256_hex(unsigned_result, strlen(unsigned_result), hash);
    free(unsigned_result);
    yyjson_mut_obj_add_strcpy(s->result, s->root, "result_sha256", hash);
    char *result = yyjson_mut_write(s->result, 0, NULL);
    size_t length = strlen(result);
    FILE *file = length <= 2 * 1024 * 1024 ? fopen(path, "wb") : NULL;
    bool ok = file && fwrite(result, 1, length, file) == length;
    if (file && fclose(file)) {
        ok = false;
    }
    free(result);
    return ok;
}
void ui_replay_session_free(ui_replay_session *s)
{
    if (s) {
        ui_replay_free(s->replay);
        yyjson_mut_doc_free(s->result);
        free(s);
    }
}
