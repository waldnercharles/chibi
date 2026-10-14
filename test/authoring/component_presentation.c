#include "authoring/component_presentation.h"

#include "runtime/component_catalog.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The engine presents its own components with no project document; a document
 * overrides each key it names. Two fixed in-memory documents, CPU only. */
static component_presentation presentation;

static void cleanup()
{
    component_presentation_free(&presentation);
}
static ecs_component_catalog registry()
{
    return (ecs_component_catalog){ REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
}
static const ecs_component_desc *component(const char *key)
{
    const ecs_component_desc *desc = ecs_component_catalog_find(registry(), key);
    assert(desc);
    return desc;
}
static field_hint hint(const char *key, const char *name)
{
    const ecs_component_desc *desc = component(key);
    for (int i = 0; i < desc->type->field_count; i++) {
        if (!strcmp(desc->type->fields[i].name, name)) {
            return component_presentation_field(&presentation, desc, i);
        }
    }
    assert(!"reflected field");
    return (field_hint){ 0 };
}
static bool unit_is(field_hint h, const char *unit)
{
    return unit ? h.unit && !strcmp(h.unit, unit) : !h.unit;
}
TEST_CASE(engine_components_present_without_a_document)
{
    /* The engine's document names only real components and valid keys. */
    char *engine = component_presentation_engine_text();
    char error[256] = { 0 };
    component_presentation listed = { 0 };
    bool valid = component_presentation_read(engine, registry(), &listed, error, sizeof error);
    free(engine);
    REQUIRE(valid && acount(listed.components) > 0);
    component_presentation_free(&listed);
    REQUIRE(component_presentation_tab(&presentation, component("Collider")) == COMPONENT_TAB_PHYSICS);
    REQUIRE(component_presentation_icon(&presentation, component("Camera")) == COMPONENT_ICON_CAMERA);
    REQUIRE(component_presentation_tab(&presentation, component("Camera")) == COMPONENT_TAB_OBJECT);
    field_hint radius = hint("Collider", "radius");
    REQUIRE(unit_is(radius, "u") && radius.min == radius.max);
    field_hint friction = hint("Collider", "friction");
    REQUIRE(unit_is(friction, NULL) && friction.min == 0 && friction.max == 1);
    field_hint width = hint("Collider", "width");
    REQUIRE(unit_is(width, NULL) && width.min == width.max);
    field_hint blast = hint("Combustion", "blast_radius");
    REQUIRE(unit_is(blast, "u") && blast.min == 0 && blast.max == 1024);
    REQUIRE(unit_is(hint("Camera", "fov"), "rad"));
    REQUIRE(unit_is(hint("Camera", "far_clip"), "u"));
    REQUIRE(component_presentation_tab(&presentation, component("Character3D")) == COMPONENT_TAB_PHYSICS);
    field_hint height = hint("Collider3D", "height");
    REQUIRE(unit_is(height, "u") && height.min == height.max);
    field_hint restitution = hint("Collider3D", "restitution");
    REQUIRE(unit_is(restitution, NULL) && restitution.min == 0 && restitution.max == 1);
    return true;
}
TEST_CASE(a_document_overrides_engine_presentation_key_by_key)
{
    const char *document = "components {\n"
                           "    Collider icon=\"shape\"\n"
                           "    Body tab=\"Object\"\n"
                           "    Camera icon=\"none\"\n"
                           "    Combustion {\n"
                           "        fields {\n"
                           "            blast_radius unit=\"m\"\n"
                           "            ignition {\n"
                           "                (array)range 0 2\n"
                           "            }\n"
                           "        }\n"
                           "    }\n"
                           "}\n";
    char error[256] = { 0 };
    REQUIRE(component_presentation_read(document, registry(), &presentation, error, sizeof error));
    const ecs_component_desc *collider = component("Collider");
    REQUIRE(component_presentation_tab(&presentation, collider) == COMPONENT_TAB_PHYSICS);
    REQUIRE(component_presentation_icon(&presentation, collider) == COMPONENT_ICON_SHAPE);
    REQUIRE(component_presentation_tab(&presentation, component("Body")) == COMPONENT_TAB_OBJECT);
    REQUIRE(component_presentation_icon(&presentation, component("Camera")) == COMPONENT_ICON_NONE);
    REQUIRE(component_presentation_tab(&presentation, component("Combustion")) == COMPONENT_TAB_GAMEPLAY);
    field_hint blast = hint("Combustion", "blast_radius");
    REQUIRE(unit_is(blast, "m") && blast.min == 0 && blast.max == 1024);
    field_hint ignition = hint("Combustion", "ignition");
    REQUIRE(unit_is(ignition, NULL) && ignition.min == 0 && ignition.max == 2);
    /* An invalid document keeps the last valid read. */
    const char *invalid = "components {\n"
                          "    Body tab=\"Nowhere\"\n"
                          "}\n";
    REQUIRE(!component_presentation_read(invalid, registry(), &presentation, error, sizeof error));
    REQUIRE(strstr(error, "unknown tab"));
    const char *color = "components {\n"
                        "    Appearance {\n"
                        "        fields {\n"
                        "            color unit=\"u\"\n"
                        "        }\n"
                        "    }\n"
                        "}\n";
    REQUIRE(!component_presentation_read(color, registry(), &presentation, error, sizeof error));
    REQUIRE(strstr(error, "numeric field"));
    REQUIRE(component_presentation_tab(&presentation, component("Body")) == COMPONENT_TAB_OBJECT);
    return true;
}
void test_component_presentation()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(engine_components_present_without_a_document);
    RUN_TEST_CASE(a_document_overrides_engine_presentation_key_by_key);
    pu_clear_setup();
}
