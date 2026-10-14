/* A managed image primitive: dynamic sources can name only textures the host
 * binds in this render context. No application or preview policy. */
#include "assets/ui_texture.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
    rml_element *image;
    bool dirty;
} texture_view;
static void update(rml_element *element, void *user)
{
    texture_view *view = user;
    if (!view) {
        view = xcalloc(1, sizeof *view);
        view->dirty = true;
        rml_element_set_user(element, view);
        view->image = control_part(element, "img", "texture-image");
        rml_element_set_property(view->image, "display", "block");
        rml_element_set_property(view->image, "width", "100%");
        rml_element_set_property(view->image, "height", "100%");
        rml_element_set_property(view->image, "pointer-events", "none");
    }
    if (!view->dirty) {
        return;
    }
    view->dirty = false;
    char source[UI_TEXTURE_KEY_MAX + 5] = "";
    size_t length = rml_element_attribute(element, "src", source, sizeof source);
    bool valid = length < sizeof source && ui_texture_source_valid(source);
    control_flag(element, "data-invalid", length && !valid);
    if (valid) {
        rml_element_set_attribute(view->image, "src", source);
    } else {
        rml_element_remove_attribute(view->image, "src");
    }
}
static void attribute(rml_element *, void *user, const char *name)
{
    texture_view *view = user;
    if (view && !strcmp(name, "src")) {
        view->dirty = true;
    }
}
static const rml_element_hooks HOOKS = { .update = update,
                                         .attribute = attribute,
                                         .release = free };
void texture_view_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&HOOKS);
    }
    rml_register_element_instancer("texture-view", instancer);
}
