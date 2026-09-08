#include "editor/workspace/gameplay_input.h"

#include "editor/workspace/editor.h"
#include "editor/workspace/input.h"

void app_gameplay_input(app_scene_authoring *scene, engine *runtime, engine_viewport *viewport)
{
    int viewport_focused = engine_viewport_focused(viewport);
    if (viewport_focused && app_input_pressed(APP_INPUT_ESCAPE)) {
        engine_viewport_release_focus();
        viewport_focused = 0;
    }
    bool accepts_input = viewport_focused && !editor_host_captures_keyboard();
    if (!accepts_input) {
        app_scene_authoring_move(scene, 0, 0);
    }
    engine_set_focus(runtime, viewport_focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_CANVAS);
    if (accepts_input) {
        float x, y;
        app_input_movement(&x, &y);
        app_scene_authoring_move(scene, x, y);
        if (app_input_pressed(APP_INPUT_FIRE)) {
            app_scene_authoring_input(scene, "Fire", 1.0, 1);
        }
    }
}
