#include "authoring/component_presentation.h"

#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *const COMPONENT_TAB_NAMES[COMPONENT_TAB_COUNT] = {
    "Object",
    "Physics",
    "Material",
    "Gameplay",
};

static const char *const ICON_NAMES[COMPONENT_ICON_COUNT] = {
    "camera",  "system",   "light",      "emitter", "water",   "terrain",
    "gamepad", "keyboard", "projectile", "health",  "diamond", "shape",
};

#define D(KEY, TAB, ICON)                                                      \
    { "GAME." KEY, COMPONENT_TAB_##TAB, COMPONENT_ICON_##ICON }
const presentation_default PRESENTATION_DEFAULTS[] = {
    D("EngineSystem", OBJECT, SYSTEM),
    D("GameAim", GAMEPLAY, NONE),
    D("GameAppearance", OBJECT, SHAPE),
    D("GameBody", PHYSICS, NONE),
    D("GameBody3D", PHYSICS, NONE),
    D("GameCamera", OBJECT, CAMERA),
    D("GameCharacter3D", PHYSICS, NONE),
    D("GameCollider", PHYSICS, NONE),
    D("GameCollider3D", PHYSICS, NONE),
    D("GameCombustion", GAMEPLAY, NONE),
    D("GameDash", GAMEPLAY, NONE),
    D("GameDestructible", PHYSICS, DIAMOND),
    D("GameDirectionalLight", MATERIAL, LIGHT),
    D("GameDrive", PHYSICS, NONE),
    D("GameEncounter", GAMEPLAY, NONE),
    D("GameEncounterMember", GAMEPLAY, NONE),
    D("GameEnvironment", MATERIAL, NONE),
    D("GameExperience", GAMEPLAY, NONE),
    D("GameFluidVolume", PHYSICS, WATER),
    D("GameFog", MATERIAL, NONE),
    D("GameFragmentGlow", MATERIAL, NONE),
    D("GameFuel", GAMEPLAY, NONE),
    D("GameGeometry3D", MATERIAL, NONE),
    D("GameGravity3D", PHYSICS, NONE),
    D("GameHRC2D", MATERIAL, LIGHT),
    D("GameHealth", GAMEPLAY, HEALTH),
    D("GameIgniter", GAMEPLAY, NONE),
    D("GameInput", GAMEPLAY, GAMEPAD),
    D("GameMaterial", MATERIAL, NONE),
    D("GameMaterialCavity", PHYSICS, NONE),
    D("GameMaterialFrame", PHYSICS, NONE),
    D("GameMeshAsset", MATERIAL, NONE),
    D("GameMeshParts", MATERIAL, NONE),
    D("GameMeshSurface2D", MATERIAL, NONE),
    D("GameMovement", GAMEPLAY, NONE),
    D("GameParticleEmitter", OBJECT, EMITTER),
    D("GamePickup", GAMEPLAY, NONE),
    D("GamePointLight", MATERIAL, LIGHT),
    D("GameProjectile", GAMEPLAY, PROJECTILE),
    D("GameReflection", MATERIAL, NONE),
    D("GameRhythm", GAMEPLAY, NONE),
    D("GameSdfSurface3D", MATERIAL, NONE),
    D("GameSensorContact", PHYSICS, NONE),
    D("GameShader", MATERIAL, NONE),
    D("GameSpawnRequest", GAMEPLAY, NONE),
    D("GameSpotLight", MATERIAL, LIGHT),
    D("GameSprite", MATERIAL, NONE),
    D("GameTilemap", MATERIAL, NONE),
    D("GameStarfield2D", MATERIAL, NONE),
    D("GameSubstance", PHYSICS, NONE),
    D("GameSurfaceInfluence", MATERIAL, NONE),
    D("GameSurfaceQuality", MATERIAL, NONE),
    D("GameTrail", MATERIAL, NONE),
    D("GameVelocity", PHYSICS, NONE),
    D("GameVision", GAMEPLAY, NONE),
    D("GameWeapon", GAMEPLAY, NONE),
    D("MaterialBreakProfile", PHYSICS, NONE),
    D("MaterialConstitutive", PHYSICS, NONE),
    D("MaterialDestructionWorld", PHYSICS, NONE),
    D("MaterialFluidWorld", PHYSICS, NONE),
    D("MaterialTerrain", MATERIAL, TERRAIN),
    D("MaterialThermal", PHYSICS, NONE),
};
#undef D
const int PRESENTATION_DEFAULT_COUNT = (int)(sizeof PRESENTATION_DEFAULTS /
                                             sizeof *PRESENTATION_DEFAULTS);

static const char *const DOCUMENT_KEYS[] = { "components" };
static const char *const COMPONENT_KEYS[] = { "tab", "icon", "fields" };
static const char *const FIELD_KEYS[] = { "range", "unit" };

static bool fail(char *error, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
    return false;
}

static int name_index(const char *name, const char *const *names, int count)
{
    for (int i = 0; name && i < count; i++) {
        if (strcmp(name, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/* The first key of `object` outside `keys`, or NULL. */
static const char *unknown_key(av_value *object, const char *const *keys, int count)
{
    av_obj_iter iterator = av_obj_iter_with(object);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        if (name_index(av_get_str(key), keys, count) < 0) {
            return av_get_str(key);
        }
    }
    return NULL;
}

static const presented_component *find_component(const component_presentation *p, uint64_t id)
{
    for (int i = 0; i < acount(p->components); i++) {
        if (p->components[i].id == id) {
            return &p->components[i];
        }
    }
    return NULL;
}

static const presented_field *find_field(const component_presentation *p, uint64_t component, int field)
{
    for (int i = 0; i < acount(p->fields); i++) {
        if (p->fields[i].component == component && p->fields[i].field == field) {
            return &p->fields[i];
        }
    }
    return NULL;
}

/* The engine's presentation of `desc`, or Object without an icon. */
static presented_component default_component(const ecs_component_desc *desc)
{
    for (int i = 0; desc->key && i < PRESENTATION_DEFAULT_COUNT; i++) {
        if (!strcmp(PRESENTATION_DEFAULTS[i].component, desc->key)) {
            return (presented_component){ desc->id,
                                          PRESENTATION_DEFAULTS[i].tab,
                                          PRESENTATION_DEFAULTS[i].icon };
        }
    }
    return (presented_component){ desc->id, COMPONENT_TAB_OBJECT, COMPONENT_ICON_NONE };
}

static int field_index(const type_desc *type, const char *name)
{
    for (int i = 0; i < type->field_count; i++) {
        if (strcmp(type->fields[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool numeric(pin_type type)
{
    return type >= PIN_I8 && type <= PIN_F64;
}

static bool read_field(
    component_presentation *out,
    const ecs_component_desc *desc,
    const char *component,
    av_value *key,
    char *error,
    size_t size
)
{
    const char *name = av_get_str(key);
    av_value *value = av_obj_iter_get_val(key);
    int index = field_index(desc->type, name);
    if (index < 0) {
        return fail(error, size, "%s: unknown field %s", component, name);
    }
    if (find_field(out, desc->id, index)) {
        return fail(error, size, "%s.%s is listed twice", component, name);
    }
    if (!av_is_obj(value)) {
        return fail(error, size, "%s.%s must be a mapping", component, name);
    }
    const char *extra = unknown_key(value, FIELD_KEYS, (int)(sizeof FIELD_KEYS / sizeof *FIELD_KEYS));
    if (extra) {
        return fail(error, size, "%s.%s: unknown key %s", component, name, extra);
    }
    if (!numeric(desc->type->fields[index].type) || desc->type->fields[index].color) {
        return fail(error, size, "%s.%s: range and unit need a numeric field", component, name);
    }
    const type_field *reflected = &desc->type->fields[index];
    presented_field field = { desc->id, index, reflected->min, reflected->max, NULL };
    av_value *range = av_obj_get(value, "range");
    if (range) {
        av_value *min = av_arr_get(range, 0), *max = av_arr_get(range, 1);
        if (av_arr_size(range) != 2 || !av_is_num(min) || !av_is_num(max) ||
            !(av_get_num(min) < av_get_num(max))) {
            return fail(error, size, "%s.%s: range needs a minimum below its maximum", component, name);
        }
        field.min = av_get_num(min);
        field.max = av_get_num(max);
    }
    av_value *unit = av_obj_get(value, "unit");
    if (unit && (!av_is_str(unit) || !av_get_len(unit))) {
        return fail(error, size, "%s.%s: unit must not be empty", component, name);
    }
    const char *text = unit ? av_get_str(unit) : reflected->unit;
    field.unit = text ? xstrdup(text) : NULL;
    apush(out->fields, field);
    return true;
}

static bool read_component(
    component_presentation *out,
    ecs_component_catalog registry,
    av_value *key,
    char *error,
    size_t size
)
{
    const char *name = av_get_str(key);
    av_value *value = av_obj_iter_get_val(key);
    const ecs_component_desc *desc = ecs_component_catalog_find(registry, name);
    if (!desc) {
        return fail(error, size, "unknown component %s", name);
    }
    if (find_component(out, desc->id)) {
        return fail(error, size, "component %s is listed twice", name);
    }
    if (!av_is_obj(value)) {
        return fail(error, size, "%s must be a mapping", name);
    }
    const char *extra = unknown_key(
        value,
        COMPONENT_KEYS,
        (int)(sizeof COMPONENT_KEYS / sizeof *COMPONENT_KEYS)
    );
    if (extra) {
        return fail(error, size, "%s: unknown key %s", name, extra);
    }
    presented_component component = default_component(desc);
    av_value *tab = av_obj_get(value, "tab");
    if (tab) {
        int index = name_index(av_get_str(tab), COMPONENT_TAB_NAMES, COMPONENT_TAB_COUNT);
        if (index < 0) {
            return fail(error, size, "%s: unknown tab %s", name, av_is_str(tab) ? av_get_str(tab) : "");
        }
        component.tab = (component_tab)index;
    }
    av_value *icon = av_obj_get(value, "icon");
    if (icon) {
        /* "none" clears an engine icon, so another component's shows. */
        bool none = av_is_str(icon) && !strcmp(av_get_str(icon), "none");
        int index = none ? COMPONENT_ICON_NONE
                         : name_index(av_get_str(icon), ICON_NAMES, COMPONENT_ICON_COUNT);
        if (!none && index < 0) {
            return fail(error, size, "%s: unknown icon %s", name, av_is_str(icon) ? av_get_str(icon) : "");
        }
        component.icon = (component_icon)index;
    }
    apush(out->components, component);
    av_value *fields = av_obj_get(value, "fields");
    if (fields && !av_is_obj(fields)) {
        return fail(error, size, "%s: fields must be a mapping", name);
    }
    av_obj_iter iterator = av_obj_iter_with(fields);
    av_value *field;
    while ((field = av_obj_iter_next(&iterator))) {
        if (!read_field(out, desc, name, field, error, size)) {
            return false;
        }
    }
    return true;
}

static bool read_document(
    component_presentation *out,
    ecs_component_catalog registry,
    av_value *root,
    char *error,
    size_t size
)
{
    if (!av_is_obj(root)) {
        return fail(error, size, "the document must be a mapping");
    }
    const char *extra = unknown_key(root, DOCUMENT_KEYS, (int)(sizeof DOCUMENT_KEYS / sizeof *DOCUMENT_KEYS));
    if (extra) {
        return fail(error, size, "unknown key %s", extra);
    }
    av_value *components = av_obj_get(root, "components");
    if (components && !av_is_obj(components)) {
        return fail(error, size, "components must be a mapping");
    }
    av_obj_iter iterator = av_obj_iter_with(components);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        if (!read_component(out, registry, key, error, size)) {
            return false;
        }
    }
    return true;
}

bool component_presentation_read(
    const char *text,
    ecs_component_catalog registry,
    component_presentation *out,
    char *error,
    size_t error_size
)
{
    assert(text && out);
    av_doc *doc = asset_text_read(text, error, error_size);
    if (!doc) {
        return false;
    }
    component_presentation next = { 0 };
    bool ok = read_document(&next, registry, av_doc_root(doc), error, error_size);
    av_doc_free(doc);
    if (!ok) {
        component_presentation_free(&next);
        return false;
    }
    component_presentation_free(out);
    *out = next;
    return true;
}

void component_presentation_free(component_presentation *presentation)
{
    for (int i = 0; i < acount(presentation->fields); i++) {
        free(presentation->fields[i].unit);
    }
    afree(presentation->fields);
    afree(presentation->components);
    *presentation = (component_presentation){ 0 };
}

component_tab component_presentation_tab(
    const component_presentation *presentation,
    const ecs_component_desc *desc
)
{
    const presented_component *component = find_component(presentation, desc->id);
    return component ? component->tab : default_component(desc).tab;
}

component_icon component_presentation_icon(
    const component_presentation *presentation,
    const ecs_component_desc *desc
)
{
    const presented_component *component = find_component(presentation, desc->id);
    return component ? component->icon : default_component(desc).icon;
}

field_hint component_presentation_field(
    const component_presentation *presentation,
    const ecs_component_desc *desc,
    int field
)
{
    const presented_field *listed = find_field(presentation, desc->id, field);
    if (listed) {
        return (field_hint){ listed->min, listed->max, listed->unit };
    }
    const type_field *reflected = &desc->type->fields[field];
    return (field_hint){ reflected->min, reflected->max, reflected->unit };
}
