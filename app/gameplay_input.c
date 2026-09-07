#include "gameplay_input.h"

#include "cute_host.h"
#include "editor/editor.h"
#include "movement_input.h"

void app_gameplay_input(app_scene_authoring *scene, engine *runtime, engine_viewport *viewport)
{
    int viewport_focused = engine_viewport_focused(viewport);
    if (viewport_focused && cf_key_just_pressed(CF_KEY_ESCAPE)) {
        engine_viewport_release_focus();
        engine_route_escape(runtime);
        viewport_focused = 0;
    }
    engine_set_focus(runtime, viewport_focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_CANVAS);
    if (viewport_focused && !editor_host_captures_keyboard()) {
        app_movement_input movement = app_movement_from_keys(
            cf_key_down(CF_KEY_A) || cf_key_down(CF_KEY_LEFT),
            cf_key_down(CF_KEY_D) || cf_key_down(CF_KEY_RIGHT),
            cf_key_down(CF_KEY_W) || cf_key_down(CF_KEY_UP),
            cf_key_down(CF_KEY_S) || cf_key_down(CF_KEY_DOWN)
        );
        app_scene_authoring_move(scene, movement.x, movement.y);
        if (cf_key_just_pressed(CF_KEY_SPACE)) {
            app_scene_authoring_input(scene, "Fire", 1.0, 1);
        }
    }
}
