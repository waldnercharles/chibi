#include "editor/workspace/gameplay_input.h"

#include "editor/workspace/editor.h"
#include "editor/workspace/input.h"

void app_gameplay_sample(app_scene_authoring *scene, bool accepts_input, const CF_V2 *mouse_target)
{
    if (!accepts_input) {
        app_scene_authoring_move(scene, 0, 0);
        app_scene_authoring_aim(scene, (CF_V2){ 0 }, false);
        app_scene_authoring_input(scene, "Fire", 0, 0);
        app_scene_authoring_input(scene, "Dash", 0, 0);
        return;
    }
    float x, y;
    app_input_movement(&x, &y);
    app_scene_authoring_move(scene, x, y);
    if (app_input_aim(&x, &y)) {
        app_scene_authoring_aim(scene, (CF_V2){ x, y }, true);
    } else if (app_input_mouse_aiming() && mouse_target) {
        app_scene_authoring_aim_at(scene, *mouse_target, app_input_firing());
    } else {
        app_scene_authoring_aim(scene, (CF_V2){ 0 }, false);
    }
    const app_input_action buttons[] = { APP_INPUT_FIRE, APP_INPUT_DASH };
    const char *actions[] = { "Fire", "Dash" };
    for (int i = 0; i < 2; i++) {
        if (app_input_pressed(buttons[i])) {
            app_scene_authoring_input(scene, actions[i], 1, 1);
        }
        if (app_input_released(buttons[i])) {
            app_scene_authoring_input(scene, actions[i], 0, 0);
        }
    }
}

void app_gameplay_input(app_scene_authoring *scene, engine *runtime, engine_viewport *viewport)
{
    bool focused = engine_viewport_focused(viewport);
    if (focused && app_input_pressed(APP_INPUT_ESCAPE)) {
        engine_viewport_release_focus();
        focused = false;
    }
    bool accepts_input = focused && !editor_host_captures_keyboard();
    if (!accepts_input) {
        app_gameplay_sample(scene, false, NULL);
    }
    engine_set_focus(runtime, focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_CANVAS);
    if (!accepts_input) {
        return;
    }
    float x = 0, y = 0;
    bool mouse = engine_viewport_mouse_world(viewport, &x, &y);
    CF_V2 target = { x, -y };
    app_gameplay_sample(scene, true, mouse ? &target : NULL);
}
