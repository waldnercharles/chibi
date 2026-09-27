#include "editor/studio/studio_internal.h"
#include "support/xalloc.h"
#include "test/suite_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* A camelCase name a data binding reads, as commands.<id>.enabled. */
static bool binding_name(const char *id)
{
    bool ok = id[0] >= 'a' && id[0] <= 'z';
    for (const char *c = id; *c; c++) {
        ok &= isalnum((unsigned char)*c) != 0;
    }
    return ok;
}

/* The member `field` of `commands.<id>` in a data model's JSON. */
static yyjson_mut_val *command_field(yyjson_mut_doc *doc, studio_command command, const char *field)
{
    yyjson_mut_val *commands = yyjson_mut_obj_get(yyjson_mut_doc_get_root(doc), "commands");
    return yyjson_mut_obj_get(yyjson_mut_obj_get(commands, studio_command_id(command)), field);
}

/* `commands` holds each command's state by id, and shows a change only when
   something differs. */
static void commands_data()
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(doc, yyjson_mut_obj(doc));
    studio_commands_add(doc);
    studio_command_state states[STUDIO_COMMAND_COUNT];
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        const char *id = studio_command_id((studio_command)c);
        states[c] = (studio_command_state){ .info = { .id = id, .label = id, .menu = id } };
    }
    states[STUDIO_COMMAND_PLAY].info = (studio_command_info){
        .label = "Pause",
        .menu = "Pause",
        .shortcut = "Space",
        .icon = KIT_ICON_PAUSE
    };
    states[STUDIO_COMMAND_SAVE].enabled = true;
    states[STUDIO_COMMAND_TOGGLE_ASSETS].checked = true;
    bool shown = studio_commands_show(doc, states);
    bool same = !studio_commands_show(doc, states);
    const char *pause = "\xf3\xb0\x8f\xa4"; /* KIT_ICON_PAUSE as UTF-8 */
    bool held = !strcmp(yyjson_mut_get_str(command_field(doc, STUDIO_COMMAND_PLAY, "label")), "Pause") &&
                !strcmp(yyjson_mut_get_str(command_field(doc, STUDIO_COMMAND_PLAY, "shortcut")), "Space") &&
                !strcmp(yyjson_mut_get_str(command_field(doc, STUDIO_COMMAND_PLAY, "icon")), pause) &&
                !strcmp(yyjson_mut_get_str(command_field(doc, STUDIO_COMMAND_STOP, "shortcut")), "") &&
                yyjson_mut_get_bool(command_field(doc, STUDIO_COMMAND_SAVE, "enabled")) &&
                !yyjson_mut_get_bool(command_field(doc, STUDIO_COMMAND_UNDO, "enabled")) &&
                yyjson_mut_get_bool(
                    command_field(doc, STUDIO_COMMAND_TOGGLE_ASSETS, "checked")
                );
    states[STUDIO_COMMAND_SAVE].enabled = false;
    bool changed = studio_commands_show(doc, states) &&
                   !yyjson_mut_get_bool(command_field(doc, STUDIO_COMMAND_SAVE, "enabled"));
    test_check(shown && same && held && changed, "the commands variable holds each command's label, shortcut, icon, enabled and checked by id");
    yyjson_mut_doc_free(doc);
}

/* Every command has an id that finds it again, so authored UI can run it by
 * emit('studio.<id>') and bind it as commands.<id>, and no two share one. */
void test_studio_commands()
{
    bool found = true, unique = true, bindable = true;
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        found &= studio_command_named(studio_command_id((studio_command)c)) == c;
        bindable &= binding_name(studio_command_id((studio_command)c));
        for (int d = 0; d < c; d++) {
            unique &= strcmp(
                          studio_command_id((studio_command)c),
                          studio_command_id((studio_command)d)
                      ) != 0;
        }
    }
    test_check(found && unique && studio_command_named("missing") < 0, "each Studio command has its own id that names it");
    test_check(bindable, "each Studio command's id is a camelCase name data bindings read");
    commands_data();
}

/* Zoom In and Zoom Out step UI Scale through the settings table's presets,
 * as the Settings page's select offers them, and Reset Zoom restores its
 * default. */
void test_studio_zoom()
{
    const studio_setting *setting = NULL;
    for (int i = 0; i < studio_setting_count; i++) {
        setting = !strcmp(studio_settings[i].key, "uiScale") ? &studio_settings[i]
                                                             : setting;
    }
    studio *s = xcalloc(1, sizeof *s);
    s->prefs.ui_scale = setting->min;
    bool stepped = true;
    for (int i = 1; i < setting->choice_count; i++) {
        studio_zoom(s, 1);
        stepped &= s->prefs.ui_scale == strtof(setting->choices[i].value, NULL);
    }
    studio_zoom(s, 1);
    bool top = s->prefs.ui_scale == setting->max;
    studio_zoom(s, -1);
    bool down = s->prefs.ui_scale ==
                strtof(setting->choices[setting->choice_count - 2].value, NULL);
    studio_zoom(s, 0);
    test_check(
        stepped && top && down && s->prefs.ui_scale == setting->fallback,
        "Zoom In and Out step UI Scale through the settings table's presets"
    );
    free(s);
}
