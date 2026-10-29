#include "assets/input_map.h"

#include "support/array.h"
#include "support/asset_text.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CONTROL_ENUM "Control"
#define CONTROL_LIMIT 64
#define BUTTON_ENUM "PadControl"

static bool fail(char *error, size_t size, const char *name, const char *message)
{
    snprintf(error, size, "%s: %s%s%s", INPUT_MAP_FILE, name ? name : "", name ? ": " : "", message);
    return false;
}
static bool read_sources(const av_value *sources, uint64_t *out)
{
    if (!av_is_arr(sources) || !av_arr_size(sources)) {
        return false;
    }
    for (size_t i = 0; i < av_arr_size(sources); i++) {
        const av_value *label = av_arr_get(sources, i);
        int control;
        if (!av_is_str(label) ||
            !type_catalog_enum_value(CONTROL_ENUM, av_get_str(label), &control) ||
            control <= 0 || control >= CONTROL_LIMIT) {
            return false;
        }
        *out |= UINT64_C(1) << control;
    }
    return true;
}
static bool read_buttons(const av_value *buttons, uint32_t *out)
{
    if (!av_is_arr(buttons) || !av_arr_size(buttons)) {
        return false;
    }
    for (size_t i = 0; i < av_arr_size(buttons); i++) {
        const av_value *label = av_arr_get(buttons, i);
        int button;
        if (!av_is_str(label) ||
            !type_catalog_enum_value(BUTTON_ENUM, av_get_str(label), &button) ||
            button <= 0 || button >= 32) {
            return false;
        }
        *out |= UINT32_C(1) << button;
    }
    return true;
}
static bool read_action(const char *name, const av_value *value, input_action *out, char *error, size_t size)
{
    if (!name[0]) {
        return fail(error, size, NULL, "an action needs a name");
    }
    const av_value *sources = av_obj_get(value, "sources");
    const av_value *firing = av_obj_get(value, "firing");
    const av_value *controller = av_obj_get(value, "controller");
    const av_value *buttons = av_obj_get(value, "buttons");
    size_t fields = (sources != NULL) + (firing != NULL) +
                    (controller != NULL) + (buttons != NULL);
    if (!av_is_obj(value) || av_obj_size(value) != fields ||
        (firing && !av_is_bool(firing))) {
        return fail(error, size, name, "an action has sources, controller buttons and an optional firing flag");
    }
    *out = (input_action){ .name = name, .firing = firing && av_get_bool(firing) };
    if (sources && !read_sources(sources, &out->controls)) {
        return fail(error, size, name, "sources must list one or more Control labels");
    }
    if (!sources && !buttons) {
        return fail(error, size, name, "an action needs sources or controller buttons");
    }
    if ((controller != NULL) != (buttons != NULL) ||
        (controller && (!av_is_uint(controller) ||
                        av_get_uint(controller) >= INPUT_CONTROLLER_MAX))) {
        return fail(error, size, name, "controller buttons need a controller slot from 0 to 3");
    }
    if (buttons && !read_buttons(buttons, &out->buttons)) {
        return fail(error, size, name, "buttons must list one or more PadControl labels");
    }
    out->controller = controller ? (int)av_get_uint(controller) : 0;
    return true;
}
bool input_map_read(const char *text, input_action **out, char *error, size_t size)
{
    *out = NULL;
    char message[192];
    av_doc *document = asset_text_read(text, message, sizeof message);
    if (!document) {
        return fail(error, size, NULL, message);
    }
    const av_value *root = av_doc_root(document);
    if (av_obj_size(root) > INPUT_ACTION_MAX) {
        av_doc_free(document);
        snprintf(message, sizeof message, "a map holds at most %d actions", INPUT_ACTION_MAX);
        return fail(error, size, NULL, message);
    }
    bool valid = true;
    for (size_t i = 0; valid && i < av_obj_size(root); i++) {
        input_action action;
        valid = read_action(
            av_get_str(av_obj_key_at(root, i)),
            av_obj_value_at(root, i),
            &action,
            error,
            size
        );
        if (valid) {
            action.name = xstrdup(action.name);
            apush(*out, action);
        }
    }
    av_doc_free(document);
    if (!valid) {
        input_map_free(*out);
        *out = NULL;
    }
    return valid;
}
char *input_map_write(const input_action *map)
{
    av_doc *document = av_doc_new();
    av_value *root = av_obj(document);
    av_doc_set_root(document, root);
    for (int i = 0; i < acount(map); i++) {
        av_value *action = av_obj_add_obj(document, root, map[i].name);
        if (map[i].controls) {
            av_value *sources = av_obj_add_arr(document, action, "sources");
            for (int control = 1; control < CONTROL_LIMIT; control++) {
                if (map[i].controls & (UINT64_C(1) << control)) {
                    av_arr_add_str(document, sources, type_catalog_enum_label(CONTROL_ENUM, control));
                }
            }
        }
        if (map[i].buttons) {
            av_obj_add_int(document, action, "controller", map[i].controller);
            av_value *buttons = av_obj_add_arr(document, action, "buttons");
            for (int button = 1; button < 32; button++) {
                if (map[i].buttons & (UINT32_C(1) << button)) {
                    av_arr_add_str(document, buttons, type_catalog_enum_label(BUTTON_ENUM, button));
                }
            }
        }
        if (map[i].firing) {
            av_obj_add_bool(document, action, "firing", true);
        }
    }
    char *text = asset_text_write(root);
    av_doc_free(document);
    return text;
}
void input_map_add(input_action **map, const char *name, uint64_t controls, bool firing)
{
    for (int i = 0; i < acount(*map); i++) {
        if (!strcmp((*map)[i].name, name)) {
            (*map)[i].controls |= controls;
            (*map)[i].firing |= firing;
            return;
        }
    }
    apush(*map, ((input_action){ .name = xstrdup(name), .controls = controls, .firing = firing }));
}
void input_map_free(input_action *map)
{
    for (int i = 0; i < acount(map); i++) {
        free((char *)map[i].name);
    }
    afree(map);
}
