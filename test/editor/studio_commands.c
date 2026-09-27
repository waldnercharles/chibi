#include "editor/studio/studio_internal.h"
#include "support/xalloc.h"
#include "test/suite_internal.h"

#include <stdlib.h>
#include <string.h>

/* Every command has an id that finds it again, so authored UI can run it by
 * emit('studio.<id>'), and no two share one. */
void test_studio_commands()
{
    bool found = true, unique = true;
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        found &= studio_command_named(studio_command_id((studio_command)c)) == c;
        for (int d = 0; d < c; d++) {
            unique &= strcmp(
                          studio_command_id((studio_command)c),
                          studio_command_id((studio_command)d)
                      ) != 0;
        }
    }
    test_check(found && unique && studio_command_named("missing") < 0, "each Studio command has its own id that names it");
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
