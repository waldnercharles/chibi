/* Studio Properties editor: context tabs, reflected component fields,
   behaviors and scene physics. */
#include "editor/documents/component_properties.h"
#include "editor/studio/studio_internal.h"
#include "editor/ui/labels.h"
#include "graph/model/pin_fmt.h"
#include "graph/runtime/value_array.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const uint32_t TAB_ICONS[STUDIO_TAB_COUNT] = {
    KIT_ICON_SETTINGS, KIT_ICON_PROTOTYPE, KIT_ICON_PHYSICS, KIT_ICON_MATERIAL,
    KIT_ICON_GAMEPLAY, KIT_ICON_GRAPH,     KIT_ICON_HEXAGON, KIT_ICON_VARIABLE,
};

static const char *tab_label(studio_tab tab)
{
    switch (tab) {
        case STUDIO_TAB_SCENE: return "Scene";
        case STUDIO_TAB_BEHAVIORS: return "Behaviors";
        case STUDIO_TAB_NODE: return "Node";
        case STUDIO_TAB_MEMBER: return "Member";
        default: return COMPONENT_TAB_NAMES[tab - STUDIO_TAB_OBJECT];
    }
}

static const char *short_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot + 1 : name;
}

/* editor/Components.yaml names a component's tab; the rest belong to Object. */
static studio_tab tab_of(const studio *s, const ecs_component_desc *desc)
{
    return (studio_tab)(STUDIO_TAB_OBJECT +
                        component_presentation_tab(&s->presentation.hints, desc));
}

/* An authored range draws a slider; an authored unit follows the value. */
static void number_hints(
    const studio *s,
    const ecs_component_desc *desc,
    const type_field *field,
    kit_number_desc *d
)
{
    const presented_field *hint = component_presentation_field(
        &s->presentation.hints,
        desc,
        (int)(field - desc->type->fields)
    );
    if (!hint) {
        return;
    }
    if (hint->min < hint->max) {
        d->min = hint->min;
        d->max = hint->max;
        d->slider = true;
        if (!d->integer) {
            d->step = 0.01;
            d->precision = 3;
        }
    }
    if (hint->unit) {
        d->unit = hint->unit;
    }
}

void studio_properties_header(studio *s, int area)
{
    (void)area;
    kit_search(s->kit, "search", s->property_search, sizeof s->property_search, "Search properties");
}

static const game_prototype_component *base_component(
    const scene_actor *actor,
    const ecs_component_desc *desc,
    game_prototype_component *added
)
{
    *added = (game_prototype_component){ .desc = desc };
    for (int j = 0; actor->base && j < acount(actor->base->components); j++) {
        if (actor->base->components[j].desc->id == desc->id) {
            return &actor->base->components[j];
        }
    }
    return added;
}

static bool field_overridden(
    const app_scene_authoring *scene,
    const scene_actor *actor,
    const game_prototype_component *component,
    int field
)
{
    pin_value value = { 0 }, inherited = { 0 };
    if (ecs_component_read_field(scene->world, actor->entity, component->desc->id, field, &value) !=
        ECS_OK) {
        return false;
    }
    inherited = value;
    if (component->data) {
        pin_read_field(component->data, &component->desc->type->fields[field], &inherited);
    }
    return !pin_value_equal(&value, &inherited);
}

/* One Undo step per gesture: the first change opens an edit, the commit records it. */
static void write_field(
    studio *s,
    scene_actor *actor,
    ecs_component_id component,
    int field,
    pin_value before,
    pin_value value,
    bool changed,
    bool active
)
{
    app_scene_authoring *scene = s->scene;
    bool pending = scene->property_actor.slot &&
                   ecs_entity_to_u64(scene->property_actor) ==
                       ecs_entity_to_u64(actor->entity) &&
                   scene->property_edit.component == component &&
                   scene->property_edit.field == field;
    if (changed && editable_scalar(&value)) {
        if (!pending) {
            finish_transform_edit(scene, false);
            scene->property_actor = actor->entity;
            scene->property_edit = (scene_field_edit){ .component = component,
                                                       .field = field,
                                                       .before = before,
                                                       .after = before };
            scene->property_dirty = scene->scene_dirty;
            pending = true;
        }
        if (ecs_component_write_field(scene->world, actor->entity, component, field, value) ==
            ECS_OK) {
            ecs_component_read_field(
                scene->world,
                actor->entity,
                component,
                field,
                &scene->property_edit.after
            );
            scene->scene_dirty = true;
            asset_index_invalidate(scene);
        }
    }
    if (pending && !active) {
        finish_property_edit(scene);
    }
}

static void context_target(studio *s, scene_actor *actor, ecs_component_id component, int field)
{
    if (!kit_item_context_clicked(s->kit)) {
        return;
    }
    s->context.entity = ecs_entity_to_u64(actor->entity);
    s->context.component = component;
    s->context.field = field;
    kit_popup_open_at(s->kit, "field-menu", s->in.mouse_x, s->in.mouse_y);
}

static void position_row(studio *s, scene_actor *actor, bool locked)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    const GameTransform
        *position = GAME_GameTransform_get_const(scene->world, actor->entity);
    if (!position || !studio_matches(s->property_search, "Position")) {
        return;
    }
    game_prototype_component added;
    const game_prototype_component
        *component = base_component(actor, &GAME_COMPONENT_GameTransform, &added);
    kit_prop_begin(k, "position", "Position");
    kit_push(k, "joined", NULL, "joined");
    GameTransform after = *position;
    bool changed = false, active = false, committed = false;
    for (int axis = 0; axis < 2; axis++) {
        double v = axis ? position->y : position->x;
        kit_number_desc d = {
            .step = 1,
            .precision = 2,
            .addon = axis ? "Y" : "X",
            .addon_cls = axis ? "y" : "x",
            .flags = (locked ? KIT_READONLY : 0) |
                     (field_overridden(scene, actor, component, axis) ? KIT_OVERRIDDEN
                                                                      : 0),
        };
        if (kit_number(k, axis ? "y" : "x", &v, &d)) {
            changed = true;
            if (axis) {
                after.y = (float)v;
            } else {
                after.x = (float)v;
            }
        }
        active |= kit_item_active(k);
        committed |= kit_item_committed(k);
        context_target(s, actor, GAME_COMPONENT_GameTransform.id, axis);
    }
    kit_pop(k);
    kit_prop_end(k);
    if (locked) {
        return;
    }
    if (changed) {
        if (!scene->edit_actor.slot || ecs_entity_to_u64(scene->edit_actor) !=
                                           ecs_entity_to_u64(actor->entity)) {
            begin_transform_edit(scene, actor->entity);
        }
        write_position(scene, actor->entity, after);
        scene->scene_dirty = true;
    }
    if (scene->edit_actor.slot && !scene->dragging_actor &&
        (committed || (!active && changed))) {
        finish_transform_edit(scene, false);
    }
}

/* One control per reflected value type. Returns true when the user changed the
 * value; *active reports an unfinished gesture. Strings commit on Enter or blur. */
static bool value_control(
    studio *s,
    const ecs_component_desc *desc,
    const type_field *field,
    pin_value *value,
    unsigned flags,
    bool *active,
    char *text,
    size_t text_size
)
{
    kit *k = s->kit;
    bool changed = false;
    *active = false;
    bool radians = strcmp(field->name, "radians") == 0;
    switch (value->type) {
        case PIN_BOOL: {
            bool b = value->b;
            changed = kit_checkbox(k, "value", &b, NULL, flags);
            value->b = b;
            break;
        }
        case PIN_I32:
        case PIN_ENUM: {
            const editor_choices *choices = property_choices(desc->type, field);
            int *slot = value->type == PIN_ENUM ? &value->e : &value->i;
            int count = choices ? editor_choices_count(choices) : 0;
            if (count > 0 && count <= 32) {
                const char *labels[32];
                int current = -1;
                for (int i = 0; i < count; i++) {
                    editor_choice choice = editor_choice_at(choices, i);
                    labels[i] = choice.label;
                    if (choice.value == *slot) {
                        current = i;
                    }
                }
                int selected = current;
                if (kit_choice(k, "value", &selected, labels, count, flags) &&
                    selected >= 0) {
                    *slot = editor_choice_at(choices, selected).value;
                    changed = true;
                }
                break;
            }
            double v = *slot;
            kit_number_desc d = { .step = 1, .integer = true, .flags = flags };
            number_hints(s, desc, field, &d);
            changed = kit_number(k, "value", &v, &d);
            *slot = (int32_t)v;
            *active = kit_item_active(k) && !kit_item_committed(k);
            break;
        }
        case PIN_U32:
            if (appearance_color_field(desc->type, field)) {
                uint32_t color = value->u;
                changed = kit_color(k, "value", &color, flags);
                value->u = color;
                *active = kit_item_active(k);
                break;
            }
            /* fallthrough */
        case PIN_I8:
        case PIN_U8:
        case PIN_I16:
        case PIN_U16:
        case PIN_I64:
        case PIN_U64: {
            double v = value->type == PIN_U32 ? value->u
                     : value->type == PIN_I8  ? value->i8
                     : value->type == PIN_U8  ? value->u8
                     : value->type == PIN_I16 ? value->i16
                     : value->type == PIN_U16 ? value->u16
                     : value->type == PIN_I64 ? (double)value->i64
                                              : (double)value->u64;
            bool unsigned_type = value->type == PIN_U32 || value->type == PIN_U8 ||
                                 value->type == PIN_U16 || value->type == PIN_U64;
            kit_number_desc d = { .step = 1, .integer = true, .flags = flags };
            number_hints(s, desc, field, &d);
            changed = kit_number(k, "value", &v, &d);
            if (unsigned_type && v < 0) {
                v = 0;
            }
            switch (value->type) {
                case PIN_U32: value->u = (uint32_t)v; break;
                case PIN_I8: value->i8 = (int8_t)v; break;
                case PIN_U8: value->u8 = (uint8_t)v; break;
                case PIN_I16: value->i16 = (int16_t)v; break;
                case PIN_U16: value->u16 = (uint16_t)v; break;
                case PIN_I64: value->i64 = (int64_t)v; break;
                default: value->u64 = (uint64_t)v; break;
            }
            *active = kit_item_active(k) && !kit_item_committed(k);
            break;
        }
        case PIN_F32:
        case PIN_F64: {
            double v = value->type == PIN_F32 ? value->f : value->d;
            kit_number_desc d = { .step = fabs(v) >= 100 ? 1 : 0.1,
                                  .precision = 2,
                                  .flags = flags };
            if (radians) {
                v = v * 180 / M_PI;
                d.unit = "\xc2\xb0";
                d.step = 1;
                d.precision = 1;
            }
            number_hints(s, desc, field, &d);
            changed = kit_number(k, "value", &v, &d);
            if (radians) {
                v = v * M_PI / 180;
            }
            if (value->type == PIN_F32) {
                value->f = (float)v;
            } else {
                value->d = v;
            }
            *active = kit_item_active(k) && !kit_item_committed(k);
            break;
        }
        case PIN_NAME:
        case PIN_STR:
            snprintf(text, text_size, "%s", value->str ? value->str : "");
            changed = kit_text_field(k, "value", text, text_size, NULL, flags);
            value->str = text;
            break;
        case PIN_HANDLE:
            kit_label(
                k,
                "value",
                "muted",
                value->u64
                    ? runtime_entity_label(s->scene, ecs_entity_from_u64(value->u64))
                    : "None"
            );
            break;
        default:
            kit_label(k, "value", "muted", pin_value_to_string(value));
            break;
    }
    return changed;
}

static bool field_label(
    studio *s,
    const game_prototype_component *component,
    const type_field *field,
    char *label,
    size_t size
)
{
    property_label(component, field, label, size);
    if (strcmp(field->name, "radians") == 0) {
        snprintf(label, size, "Rotation");
    }
    return studio_matches(s->property_search, label);
}

static void field_row(
    studio *s,
    scene_actor *actor,
    const game_prototype_component *component,
    int index,
    bool locked
)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    const ecs_component_desc *desc = component->desc;
    const type_field *field = &desc->type->fields[index];
    pin_value value = { 0 };
    char label[128], text[512];
    if (ecs_component_read_field(scene->world, actor->entity, desc->id, index, &value) !=
            ECS_OK ||
        !field_label(s, component, field, label, sizeof label)) {
        return;
    }
    pin_value before = value;
    unsigned flags = (locked ? KIT_READONLY : 0) |
                     (field_overridden(scene, actor, component, index) ? KIT_OVERRIDDEN
                                                                       : 0);
    bool active = false;
    kit_prop_begin(k, field->name, label);
    bool changed = value_control(s, desc, field, &value, flags, &active, text, sizeof text);
    context_target(s, actor, desc->id, index);
    kit_prop_end(k);
    if (!locked) {
        write_field(s, actor, desc->id, index, before, value, changed, active);
    }
}

/* Reset one instance value to its prototype default as a single Undo step. */
static void reset_field(studio *s, scene_actor *actor)
{
    app_scene_authoring *scene = s->scene;
    const ecs_component_desc *desc = NULL;
    for (int i = 0; i < scene->registry.component_count; i++) {
        if (scene->registry.components[i]->id == s->context.component) {
            desc = scene->registry.components[i];
        }
    }
    if (!desc) {
        return;
    }
    game_prototype_component added;
    const game_prototype_component *component = base_component(actor, desc, &added);
    pin_value before = { 0 }, inherited = { 0 };
    if (!component->data || ecs_component_read_field(
                                scene->world,
                                actor->entity,
                                desc->id,
                                s->context.field,
                                &before
                            ) != ECS_OK) {
        return;
    }
    inherited = before;
    pin_read_field(component->data, &desc->type->fields[s->context.field], &inherited);
    if (desc->id == GAME_COMPONENT_GameTransform.id) {
        const GameTransform
            *position = GAME_GameTransform_get_const(scene->world, actor->entity);
        GameTransform after = *position;
        if (s->context.field) {
            after.y = inherited.f;
        } else {
            after.x = inherited.f;
        }
        begin_transform_edit(scene, actor->entity);
        write_position(scene, actor->entity, after);
        scene->scene_dirty = true;
        finish_transform_edit(scene, false);
        return;
    }
    write_field(s, actor, desc->id, s->context.field, before, inherited, true, false);
}

static void field_menu(studio *s, scene_actor *actor, bool locked)
{
    kit *k = s->kit;
    if (!kit_popup_begin(k, "field-menu", "menu")) {
        return;
    }
    bool target = actor && s->context.entity == ecs_entity_to_u64(actor->entity);
    bool overridden = false;
    if (target) {
        for (int i = 0; i < s->scene->registry.component_count; i++) {
            const ecs_component_desc *desc = s->scene->registry.components[i];
            if (desc->id == s->context.component) {
                game_prototype_component added;
                overridden = field_overridden(
                    s->scene,
                    actor,
                    base_component(actor, desc, &added),
                    s->context.field
                );
            }
        }
    }
    kit_menu_item_desc reset = { .label = "Reset to Prototype",
                                 .icon = KIT_ICON_RESET,
                                 .checked = -1,
                                 .disabled = locked || !overridden };
    if (kit_menu_item(k, "reset", &reset)) {
        reset_field(s, actor);
    }
    kit_popup_end(k);
}

static void component_panel(studio *s, scene_actor *actor, const ecs_component_desc *desc, bool locked)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    game_prototype_component added;
    const game_prototype_component *component = base_component(actor, desc, &added);
    char title[128];
    editor_component_label(desc->name, title, sizeof title);
    bool transform = desc->id == GAME_COMPONENT_GameTransform.id;
    if (s->property_search[0] && !studio_matches(s->property_search, title)) {
        bool any = transform && studio_matches(s->property_search, "Position");
        for (int i = 0; !any && i < desc->type->field_count; i++) {
            char label[128];
            property_label(component, &desc->type->fields[i], label, sizeof label);
            any = studio_matches(s->property_search, label);
        }
        if (!any) {
            return;
        }
    }
    int overrides = 0;
    for (int i = 0; i < desc->type->field_count; i++) {
        overrides += field_overridden(scene, actor, component, i);
    }
    char badge[16] = "";
    if (overrides) {
        snprintf(badge, sizeof badge, "%d", overrides);
    }
    bool default_open = transform || desc->id == GAME_COMPONENT_GameAppearance.id ||
                        desc->id == GAME_COMPONENT_GameOrientation.id;
    if (kit_panel_begin(k, short_name(desc->name), title, badge, default_open || s->property_search[0])) {
        if (transform) {
            position_row(s, actor, locked);
        } else {
            const void *data = ecs_component_data_const(scene->world, actor->entity, desc->id);
            for (int i = 0; i < desc->type->field_count; i++) {
                if (property_field_visible(desc, &desc->type->fields[i], data)) {
                    field_row(s, actor, component, i, locked);
                }
            }
        }
    }
    kit_panel_end(k);
}

static void identity(studio *s, scene_actor *actor)
{
    kit *k = s->kit;
    kit_push(k, "identity", NULL, "identity");
    kit_push(k, "glyph", NULL, "glyph");
    kit_icon(k, "icon", NULL, studio_prototype_icon(s, actor->prototype_name));
    kit_pop(k);
    kit_push(k, "who", NULL, "who");
    kit_label(k, "name", "title", actor->name);
    kit_push(k, "lineage", NULL, "lineage");
    kit_label(k, "of", NULL, "Instance of");
    kit_button_desc prototype = { .label = actor->prototype_name,
                                  .icon = KIT_ICON_PROTOTYPE,
                                  .tip = "Open prototype" };
    if (kit_button(k, "prototype", &prototype)) {
        app_scene_authoring_open_prototype(s->scene, actor->prototype_name);
    }
    kit_pop(k);
    kit_pop(k);
    kit_pop(k);
}

static void behaviors_panel(studio *s, scene_actor *actor, bool locked)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    char **names = instance_behavior_names(scene, actor->entity);
    char count[16];
    snprintf(count, sizeof count, "%d", acount(names));
    if (kit_panel_begin(k, "behaviors", "Behaviors", NULL, true)) {
        for (int i = 0; i < acount(names); i++) {
            if (!studio_matches(s->property_search, names[i])) {
                continue;
            }
            /* A row opens its behavior in the graph, like a row in Assets;
   the open one is marked. */
            kit_push(k, names[i], NULL, "behavior-row");
            bool open = scene->graph_document_id &&
                        !strcmp(scene->graph_document_id, names[i]);
            kit_flag(k, "data-current", open);
            kit_icon(k, "icon", NULL, KIT_ICON_GRAPH);
            kit_label(k, "name", "name", names[i]);
            kit_button_desc detach = { .icon = KIT_ICON_CLOSE,
                                       .size = KIT_SM,
                                       .tip = "Detach",
                                       .flags = locked ? KIT_DISABLED : 0 };
            bool detached = kit_button(k, "detach", &detach);
            if (detached) {
                app_scene_authoring_set_instance_behavior(scene, names[i], false);
            }
            kit_pop(k);
            if (!detached && kit_item_clicked(k) && !open) {
                app_scene_authoring_open_behavior(scene, names[i]);
            }
        }
        kit_push(k, "add", NULL, "add-row");
        kit_button_desc attach = { .label = "Attach Behavior",
                                   .icon = KIT_ICON_ADD,
                                   .variant = KIT_SUBTLE,
                                   .flags = locked ? KIT_DISABLED : 0 };
        if (kit_button(k, "attach", &attach)) {
            kit_popup_open(k, "attach-behavior");
        }
        kit_pop(k);
    }
    kit_panel_end(k);
    if (kit_popup_begin(k, "attach-behavior", "menu")) {
        const asset_index *index = app_scene_authoring_asset_index(scene);
        int total = index ? asset_index_asset_count(index) : 0;
        for (int i = 0; i < total; i++) {
            const asset_index_asset *asset = asset_index_asset_at(index, i);
            if (asset->kind != ASSET_INDEX_BEHAVIOR || !asset->name) {
                continue;
            }
            bool attached = false;
            for (int j = 0; j < acount(names); j++) {
                attached |= strcmp(names[j], asset->name) == 0;
            }
            char key[24];
            snprintf(key, sizeof key, "b%d", i);
            kit_menu_item_desc d = { .label = asset->name,
                                     .icon = KIT_ICON_GRAPH,
                                     .checked = attached ? 1 : 0 };
            if (kit_menu_item(k, key, &d)) {
                app_scene_authoring_set_instance_behavior(scene, asset->name, !attached);
            }
        }
        kit_popup_end(k);
    }
    free_behavior_names(names);
}

static void scene_panels(studio *s, bool locked)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    game_physics_settings settings = game_physics_configuration(scene->live->physics);
    game_physics_settings next = settings;
    unsigned flags = locked ? KIT_READONLY : 0;
    if (kit_panel_begin(k, "physics", "Physics", NULL, true)) {
        if (studio_matches(s->property_search, "Workers")) {
            kit_prop_begin(k, "workers", "Workers");
            double v = settings.workers;
            kit_number_desc d = { .min = 0,
                                  .max = GAME_PHYSICS_WORKER_MAX,
                                  .step = 1,
                                  .integer = true,
                                  .flags = flags };
            if (kit_number(k, "value", &v, &d)) {
                next.workers = (int)v;
            }
            kit_prop_end(k);
        }
        if (studio_matches(s->property_search, "Substeps")) {
            kit_prop_begin(k, "substeps", "Substeps");
            double v = settings.substeps;
            kit_number_desc d = { .min = 1,
                                  .max = GAME_PHYSICS_SUBSTEP_MAX,
                                  .step = 1,
                                  .integer = true,
                                  .flags = flags };
            if (kit_number(k, "value", &v, &d)) {
                next.substeps = (int)v;
            }
            kit_prop_end(k);
        }
        static const struct
        {
            const char *key, *label;
            size_t offset;
        } bools[] = {
            { "sleep", "Allow sleeping", offsetof(game_physics_settings, sleep) },
            { "continuous",
              "Continuous collision",
              offsetof(game_physics_settings, continuous) },
            { "force_drive", "Drive with forces", offsetof(game_physics_settings, force_drive) },
        };
        for (size_t i = 0; i < sizeof bools / sizeof *bools; i++) {
            if (!studio_matches(s->property_search, bools[i].label)) {
                continue;
            }
            kit_prop_begin(k, bools[i].key, bools[i].label);
            bool *value = (bool *)((char *)&next + bools[i].offset);
            kit_checkbox(k, "value", value, NULL, flags);
            kit_prop_end(k);
        }
        if (studio_matches(s->property_search, "Contact Hz")) {
            kit_prop_begin(k, "hertz", "Contact Hz");
            double v = settings.contact_hertz;
            kit_number_desc d = { .min = 0,
                                  .max = 240,
                                  .step = 1,
                                  .precision = 1,
                                  .unit = "Hz",
                                  .flags = flags };
            if (kit_number(k, "value", &v, &d)) {
                next.contact_hertz = (float)v;
            }
            kit_prop_end(k);
        }
        if (studio_matches(s->property_search, "Damping")) {
            kit_prop_begin(k, "damping", "Damping");
            double v = settings.contact_damping;
            kit_number_desc d = { .min = 0,
                                  .max = 100,
                                  .step = 0.1,
                                  .precision = 2,
                                  .flags = flags };
            if (kit_number(k, "value", &v, &d)) {
                next.contact_damping = (float)v;
            }
            kit_prop_end(k);
        }
    }
    kit_panel_end(k);
    if (!locked && memcmp(&next, &settings, sizeof next) != 0 &&
        app_scene_authoring_set_physics(scene, next) != 0) {
        studio_report(s, "Invalid physics settings");
    }
}

/* Prototype edits preview live and record one Undo step per gesture. */
static void prototype_row(
    studio *s,
    scene_prototype_asset *asset,
    const game_prototype_component *component,
    int index,
    bool locked
)
{
    kit *k = s->kit;
    const type_field *field = &component->desc->type->fields[index];
    pin_value value = { 0 };
    char label[128], text[512], key[192];
    pin_read_field(component->data, field, &value);
    if (!field_label(s, component, field, label, sizeof label)) {
        return;
    }
    bool active = false;
    kit_prop_begin(k, field->name, label);
    bool changed = value_control(
        s,
        component->desc,
        field,
        &value,
        locked ? KIT_READONLY : 0,
        &active,
        text,
        sizeof text
    );
    kit_prop_end(k);
    if (locked) {
        return;
    }
    snprintf(key, sizeof key, "%s.%s", component->desc->name, field->name);
    bool gesture = strcmp(s->prototype_edit, key) == 0;
    if (changed && editable_scalar(&value)) {
        if (!gesture) {
            prototype_history_before(s->scene, asset->prototype);
            snprintf(s->prototype_edit, sizeof s->prototype_edit, "%s", key);
            gesture = true;
        }
        write_prototype_field(s->scene, asset, component->desc->name, field->name, value);
    }
    if (gesture && !active) {
        s->prototype_edit[0] = '\0';
    }
}

static void prototype_properties(studio *s, scene_prototype_asset *asset, bool locked)
{
    kit *k = s->kit;
    game_prototype *prototype = asset->prototype;
    if (s->tab == STUDIO_TAB_OBJECT) {
        int instances = 0;
        for (int i = 0; i < acount(s->scene->actors); i++) {
            instances += s->scene->actors[i].prototype_name &&
                         strcmp(s->scene->actors[i].prototype_name, prototype->name) == 0;
        }
        char lineage[64];
        snprintf(
            lineage,
            sizeof lineage,
            "%d components \xc2\xb7 %d %s",
            acount(prototype->components),
            instances,
            instances == 1 ? "instance" : "instances"
        );
        kit_push(k, "identity", NULL, "identity");
        kit_push(k, "glyph", NULL, "glyph");
        kit_icon(k, "icon", NULL, studio_prototype_icon(s, prototype->name));
        kit_pop(k);
        kit_push(k, "who", NULL, "who");
        kit_push(k, "title", NULL, "row");
        kit_label(k, "name", "title", prototype->name);
        kit_label(k, "badge", "badge accent", "Prototype");
        kit_pop(k);
        kit_label(k, "lineage", "lineage", lineage);
        kit_pop(k);
        kit_pop(k);
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        const ecs_component_desc *desc = component->desc;
        if (tab_of(s, desc) != s->tab || !component->data) {
            continue;
        }
        char title[128];
        editor_component_label(desc->name, title, sizeof title);
        bool any = studio_matches(s->property_search, title);
        for (int f = 0; !any && f < desc->type->field_count; f++) {
            char label[128];
            any = field_label(s, component, &desc->type->fields[f], label, sizeof label);
        }
        if (!any) {
            continue;
        }
        bool default_open = desc->id == GAME_COMPONENT_GameTransform.id ||
                            desc->id == GAME_COMPONENT_GameAppearance.id ||
                            desc->id == GAME_COMPONENT_GameOrientation.id;
        if (kit_panel_begin(k, short_name(desc->name), title, NULL, default_open || s->property_search[0])) {
            for (int f = 0; f < desc->type->field_count; f++) {
                if (property_field_visible(
                        desc,
                        &desc->type->fields[f],
                        component->data
                    )) {
                    prototype_row(s, asset, component, f, locked);
                }
            }
        }
        kit_panel_end(k);
    }
}

void studio_properties_body(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    bool prototype_document = app_scene_authoring_document(scene).kind ==
                              APP_EDITOR_DOCUMENT_PROTOTYPE;
    scene_prototype_asset *asset = prototype_document ? selected_prototype(scene)
                                                      : NULL;
    scene_actor *actor = prototype_document ? NULL : selected_actor(scene);
    bool locked = !studio_editable(s);
    bool available[STUDIO_TAB_COUNT] = { [STUDIO_TAB_SCENE] = true };
    if (asset && asset->prototype) {
        available[STUDIO_TAB_OBJECT] = true;
        for (int i = 0; i < acount(asset->prototype->components); i++) {
            available[tab_of(s, asset->prototype->components[i].desc)] = true;
        }
    } else if (actor) {
        available[STUDIO_TAB_OBJECT] = available[STUDIO_TAB_BEHAVIORS] = true;
        for (int i = 0; i < scene->registry.component_count; i++) {
            const ecs_component_desc *desc = scene->registry.components[i];
            if (ecs_component_has(scene->world, actor->entity, desc->id)) {
                available[tab_of(s, desc)] = true;
            }
        }
    }
    /* The Node tab follows the graph selection, as Object follows the
   scene's: a newly selected node shows its details, a newly selected
   object its components. */
    uint64_t node = studio_graph_node(s) ? studio_graph_node(s)
                                         : studio_graph_comment(s);
    available[STUDIO_TAB_NODE] = node != 0;
    if (node && node != s->shown_node) {
        s->tab = STUDIO_TAB_NODE;
    } else if (s->tab == STUDIO_TAB_NODE && scene->selected != s->shown_actor && actor) {
        s->tab = STUDIO_TAB_OBJECT;
    }
    /* Member follows the Members selection the same way. */
    uint64_t member = studio_member_shown(s);
    available[STUDIO_TAB_MEMBER] = member != 0;
    if (member && member != s->shown_member) {
        s->tab = STUDIO_TAB_MEMBER;
    }
    s->shown_member = member;
    s->shown_node = node;
    s->shown_actor = scene->selected;
    if (!available[s->tab]) {
        s->tab = actor || asset ? STUDIO_TAB_OBJECT : STUDIO_TAB_SCENE;
    }
    kit_push(k, "properties", NULL, "properties");
    kit_push(k, "tabs", "nav", "prop-tabs");
    for (int t = 0; t < STUDIO_TAB_COUNT; t++) {
        if (!available[t]) {
            continue;
        }
        if (t == STUDIO_TAB_BEHAVIORS ||
            (t == STUDIO_TAB_NODE && !available[STUDIO_TAB_BEHAVIORS])) {
            kit_leaf(k, t == STUDIO_TAB_NODE ? "sep2" : "sep", NULL, "tab-sep");
        }
        if (t == STUDIO_TAB_MEMBER && !available[STUDIO_TAB_BEHAVIORS] &&
            !available[STUDIO_TAB_NODE]) {
            kit_leaf(k, "sep3", NULL, "tab-sep");
        }
        char key[16];
        snprintf(key, sizeof key, "tab%d", t);
        kit_button_desc d = { .icon = TAB_ICONS[t],
                              .tip = tab_label((studio_tab)t),
                              .flags = s->tab == (studio_tab)t ? KIT_ON : 0 };
        if (kit_button(k, key, &d)) {
            s->tab = (studio_tab)t;
        }
        kit_attr(k, "data-tip-side", "right");
    }
    kit_pop(k);
    kit_push(k, "scroll", NULL, "prop-scroll");
    if (s->tab == STUDIO_TAB_SCENE) {
        scene_panels(s, locked);
    } else if (s->tab == STUDIO_TAB_NODE) {
        studio_node_properties(s);
    } else if (s->tab == STUDIO_TAB_MEMBER) {
        studio_member_properties(s);
    } else if (asset && asset->prototype) {
        prototype_properties(s, asset, locked);
    } else if (!actor) {
        kit_label(k, "empty", "prop-empty", "No selection");
    } else {
        if (s->tab == STUDIO_TAB_OBJECT) {
            identity(s, actor);
        }
        if (s->tab == STUDIO_TAB_BEHAVIORS) {
            behaviors_panel(s, actor, locked);
        } else {
            for (int i = 0; i < scene->registry.component_count; i++) {
                const ecs_component_desc *desc = scene->registry.components[i];
                if (ecs_component_has(scene->world, actor->entity, desc->id) &&
                    tab_of(s, desc) == s->tab) {
                    component_panel(s, actor, desc, locked);
                }
            }
        }
    }
    kit_pop(k);
    kit_pop(k);
    field_menu(s, actor, locked);
}
