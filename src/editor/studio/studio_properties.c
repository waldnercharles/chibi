/* Studio Properties editor: context tabs, reflected component fields,
   behaviors and scene physics. */
#include "editor/collab/actions_scene.h"
#include "editor/documents/component_properties.h"
#include "editor/studio/studio_internal.h"
#include "editor/ui/labels.h"
#include "graph/model/pin_fmt.h"
#include "graph/model/registry.h"
#include "graph/model/type.h"
#include "graph/runtime/value_array.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

/* editor/Components.kdl names a component's tab; the rest belong to Object. */
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

static void number_bounds(pin_type type, kit_number_desc *d)
{
    d->single_precision = type == PIN_F32;
    double min, max;
    switch (type) {
        case PIN_I8:
            min = INT8_MIN;
            max = INT8_MAX;
            break;
        case PIN_U8:
            min = 0;
            max = UINT8_MAX;
            break;
        case PIN_I16:
            min = INT16_MIN;
            max = INT16_MAX;
            break;
        case PIN_U16:
            min = 0;
            max = UINT16_MAX;
            break;
        case PIN_I32:
        case PIN_ENUM:
            min = INT32_MIN;
            max = INT32_MAX;
            break;
        case PIN_U32:
            min = 0;
            max = UINT32_MAX;
            break;
        case PIN_F32:
            min = -FLT_MAX;
            max = FLT_MAX;
            break;
        default: return;
    }
    bool bounded = d->min != 0 || d->max != 0;
    d->min = bounded ? fmax(d->min, min) : min;
    d->max = bounded ? fmin(d->max, max) : max;
}

static bool integer64_control(kit *k, pin_value *value, unsigned flags)
{
    char text[64];
    if (value->type == PIN_I64) {
        snprintf(text, sizeof text, "%" PRId64, value->i64);
    } else {
        snprintf(text, sizeof text, "%" PRIu64, value->u64);
    }
    if (!kit_text_field(k, "value", text, sizeof text, NULL, flags)) {
        return false;
    }
    char *start = text;
    while (isspace((unsigned char)*start)) {
        start++;
    }
    if (value->type == PIN_U64 && *start == '-') {
        return false;
    }
    errno = 0;
    char *end;
    pin_value parsed = *value;
    if (value->type == PIN_I64) {
        parsed.i64 = strtoimax(start, &end, 10);
    } else {
        parsed.u64 = strtoumax(start, &end, 10);
    }
    if (errno == ERANGE || end == start) {
        return false;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return false;
    }
    *value = parsed;
    return true;
}

/* Reflected vectors are shown as one joined control with typed axes. */
static const type_desc *vector_type(const pin_value *value)
{
    if (value->type != PIN_STRUCT || value->container != PIN_CONTAINER_SINGLE ||
        !value->ptr) {
        return NULL;
    }
    const type_desc *type = registry_type_by_name(value->type_name);
    if (!type || type->field_count < 2 || type->field_count > 4 ||
        !type_is_plain(type)) {
        return NULL;
    }
    static const char *const axes[] = { "x", "y", "z", "w" };
    for (int i = 0; i < type->field_count; i++) {
        const type_field *axis = &type->fields[i];
        if (!axis->name || strcmp(axis->name, axes[i]) ||
            (axis->type != PIN_F32 && axis->type != PIN_F64)) {
            return NULL;
        }
    }
    return type;
}

bool studio_properties_live(studio *s)
{
    return studio_live(s) && !studio_designer_active(s) &&
           s->scene->document.kind != APP_EDITOR_DOCUMENT_PROTOTYPE;
}

/* While the scene runs, Properties edits its running world: the header says
   so, tinted (studio_sources.c) as Unity tints its editor in Play Mode, and
   offers Keep Changes (Unreal's Keep Simulation Changes). A prototype stays
   read-only, since editing it respawns every instance. */
static void live_header(studio *s)
{
    kit *k = s->kit;
    if (!studio_properties_live(s)) {
        kit_label(k, "locked", "badge", "Read-only");
        kit_tip(k, "Stop playing to edit prototypes", NULL, NULL);
        return;
    }
    kit_label(k, "live", "badge accent", "Live");
    kit_tip(k, "Changes made while playing revert when you stop", NULL, NULL);
    studio_command_info keep = studio_command_describe(s, STUDIO_COMMAND_KEEP_CHANGES);
    bool enabled = studio_command_enabled(s, STUDIO_COMMAND_KEEP_CHANGES);
    kit_button_desc button = { .icon = keep.icon,
                               .label = "Keep",
                               .flags = enabled ? 0 : KIT_DISABLED };
    if (kit_button(k, "keep", &button)) {
        studio_command_run(s, STUDIO_COMMAND_KEEP_CHANGES);
    }
    kit_tip(
        k,
        keep.label,
        keep.shortcut,
        enabled ? "Copy this object's live values into the scene when you stop"
                : "Select an object placed in the scene"
    );
}

void studio_properties_header(studio *s, int area)
{
    /* Visual designer (workstream H): a UI document's Details. */
    if (studio_designer_active(s)) {
        studio_designer_details_header(s, area);
        return;
    }
    kit_search(s->kit, "search", s->property_search, sizeof s->property_search, "Search properties");
    studio_find_field(s, STUDIO_EDITOR_PROPERTIES);
    if (studio_live(s)) {
        live_header(s);
    }
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

/* ---------- Multi-object editing ---------- */

/* Unity's multi-object editing: with several objects selected, Properties
   shows the components they all have, a value they differ in as mixed, and
   writes a change to each of them as one Undo step. */
static bool editing_selection(studio *s, const scene_actor *actor)
{
    return actor == selected_actor(s->scene) && scene_selection_count(s->scene) > 1;
}

/* The other selected objects that have `component`, by actor index; the
   caller frees the array. */
static int *other_selected(studio *s, ecs_component_id component)
{
    app_scene_authoring *scene = s->scene;
    int *others = NULL;
    for (int i = 0; i < acount(scene->actors); i++) {
        if (i != scene->selected && scene_instance_selected(scene, i) &&
            ecs_component_has(scene->world, scene->actors[i].entity, component)) {
            apush(others, i);
        }
    }
    return others;
}

/* Whether every selected object has `component`. */
static bool shared_component(studio *s, ecs_component_id component)
{
    app_scene_authoring *scene = s->scene;
    for (int i = 0; i < acount(scene->actors); i++) {
        if (scene_instance_selected(scene, i) &&
            !ecs_component_has(scene->world, scene->actors[i].entity, component)) {
            return false;
        }
    }
    return true;
}

/* KIT_MIXED when another selected object's field differs from `value`. */
static unsigned mixed_flag(
    studio *s,
    const scene_actor *actor,
    ecs_component_id component,
    int field,
    const pin_value *value
)
{
    if (!editing_selection(s, actor)) {
        return 0;
    }
    int *others = other_selected(s, component);
    bool mixed = false;
    for (int i = 0; i < acount(others) && !mixed; i++) {
        pin_value other = { 0 };
        mixed = ecs_component_read_field(
                    s->scene->world,
                    s->scene->actors[others[i]].entity,
                    component,
                    field,
                    &other
                ) == ECS_OK &&
                !pin_value_equal(&other, value);
    }
    afree(others);
    return mixed ? KIT_MIXED : 0;
}

/* A field's change in Properties, one gesture's frame. */
typedef struct
{
    ecs_component_id component;
    int field;
    pin_value before, value;
    bool changed, active;
    bool selection; /* the other selected objects take the change too */
} field_change;

/* Writes `c` to `entity`, whose field is `current`: a vector takes only the
   axes the change moved, so each object keeps its others. */
static void write_merged(ecs_world *world, ecs_entity entity, const field_change *c, pin_value current)
{
    const type_desc *type = c->value.type == PIN_STRUCT
                              ? registry_type_by_name(c->value.type_name)
                              : NULL;
    if (!type || !current.ptr || !c->before.ptr) {
        ecs_component_write_field(world, entity, c->component, c->field, c->value);
        return;
    }
    void *merged = xmalloc(type->size);
    memcpy(merged, current.ptr, type->size);
    for (int i = 0; i < type->field_count; i++) {
        pin_value before, after;
        pin_read_field(c->before.ptr, &type->fields[i], &before);
        pin_read_field(c->value.ptr, &type->fields[i], &after);
        if (!pin_value_equal(&before, &after)) {
            pin_write_field(merged, &type->fields[i], after);
        }
    }
    pin_value value = { .type = PIN_STRUCT, .type_name = type->name, .ptr = merged };
    ecs_component_write_field(world, entity, c->component, c->field, value);
    free(merged);
}

/* The change on the other selected objects; `record` keeps each one's value
   before the gesture, which finish_property_edit records with it. */
static void write_others(studio *s, const field_change *c, bool record)
{
    app_scene_authoring *scene = s->scene;
    int *others = other_selected(s, c->component);
    for (int i = 0; i < acount(others); i++) {
        ecs_entity entity = scene->actors[others[i]].entity;
        pin_value current = { 0 };
        if (ecs_component_read_field(scene->world, entity, c->component, c->field, &current) !=
            ECS_OK) {
            continue;
        }
        bool tracked = false;
        for (int t = 0; t < acount(scene->property_others) && !tracked; t++) {
            tracked = ecs_entity_to_u64(scene->property_others[t].entity) ==
                      ecs_entity_to_u64(entity);
        }
        if (record && !tracked) {
            scene_field_target target = { .entity = entity };
            bool copied = scene_property_value_copy(&target.before, current);
            assert(copied);
            apush(scene->property_others, target);
        }
        write_merged(scene->world, entity, c, current);
    }
    afree(others);
}

/* One Undo step per gesture: the first change opens an edit, the commit
   records it. While the scene runs, a change writes only its running world. */
static void write_field(studio *s, scene_actor *actor, field_change c)
{
    app_scene_authoring *scene = s->scene;
    if (studio_live(s)) {
        if (c.changed && editable_scalar(&c.value)) {
            ecs_component_write_field(scene->world, actor->entity, c.component, c.field, c.value);
            if (c.selection) {
                write_others(s, &c, false);
            }
        }
        return;
    }
    bool pending = scene->property_actor.slot &&
                   ecs_entity_to_u64(scene->property_actor) ==
                       ecs_entity_to_u64(actor->entity) &&
                   scene->property_edit.component == c.component &&
                   scene->property_edit.field == c.field;
    if (c.changed && editable_scalar(&c.value)) {
        if (!pending) {
            finish_transform_edit(scene, false);
            scene->property_actor = actor->entity;
            scene->property_edit = (scene_field_edit){ .component = c.component,
                                                       .field = c.field };
            if (!scene_property_value_copy(&scene->property_edit.before, c.before) ||
                !scene_property_value_copy(&scene->property_edit.after, c.before)) {
                abort();
            }
            scene->property_dirty = scene->scene_dirty;
            pending = true;
        }
        if (scene_collab_can_write_locally()) {
            if (ecs_component_write_field(scene->world, actor->entity, c.component, c.field, c.value) ==
                ECS_OK) {
                pin_value written = { 0 };
                if (ecs_component_read_field(scene->world, actor->entity, c.component, c.field, &written) !=
                    ECS_OK) {
                    abort();
                }
                scene_property_value_free(&scene->property_edit.after);
                if (!scene_property_value_copy(&scene->property_edit.after, written)) {
                    abort();
                }
                scene->scene_dirty = true;
                asset_index_invalidate(scene);
            }
            if (c.selection) {
                write_others(s, &c, true);
            }
        } else if (scene_collab_is_editor()) {
            scene_property_value_free(&scene->property_edit.after);
            if (!scene_property_value_copy(&scene->property_edit.after, c.value)) {
                abort();
            }
        }
    }
    if (pending && !c.active) {
        finish_property_edit(scene);
    }
}

void studio_set_field(studio *s, scene_actor *actor, ecs_component_id component, int field, const char *text)
{
    pin_value before = { 0 };
    if (ecs_component_read_field(s->scene->world, actor->entity, component, field, &before) !=
        ECS_OK) {
        return;
    }
    pin_value value = before;
    value.str = text;
    write_field(
        s,
        actor,
        (field_change){ .component = component,
                        .field = field,
                        .before = before,
                        .value = value,
                        .changed = true }
    );
}

/* An asset dragged over the field shows on its objects in the Scene. */
static void preview_drop(studio *s, scene_actor *actor, ecs_component_id component, int field, const char *key)
{
    const char *name = studio_drop_shown(s, key);
    if (!name) {
        return;
    }
    scene_preview_field(s->scene, actor->entity, component, field, name);
    int *others = editing_selection(s, actor) ? other_selected(s, component) : NULL;
    for (int i = 0; i < acount(others); i++) {
        scene_preview_field(s->scene, s->scene->actors[others[i]].entity, component, field, name);
    }
    afree(others);
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
    bool changed = false, active = false, committed = false,
         moved[2] = { false, false };
    for (int axis = 0; axis < 2; axis++) {
        double v = axis ? position->y : position->x;
        pin_value shown = { .type = PIN_F32, .f = (float)v };
        kit_number_desc d = {
            .min = -FLT_MAX,
            .max = FLT_MAX,
            .step = 1,
            .precision = 2,
            .single_precision = true,
            .addon = axis ? "Y" : "X",
            .addon_cls = axis ? "y" : "x",
            .flags = (locked ? KIT_READONLY : 0) |
                     (field_overridden(scene, actor, component, axis) ? KIT_OVERRIDDEN
                                                                      : 0) |
                     mixed_flag(s, actor, GAME_COMPONENT_GameTransform.id, axis, &shown),
        };
        if (kit_number(k, axis ? "y" : "x", &v, &d)) {
            changed = moved[axis] = true;
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
    /* Several objects take the axis as a field, one Undo step for them all. */
    if (editing_selection(s, actor)) {
        for (int axis = 0; axis < 2; axis++) {
            write_field(
                s,
                actor,
                (field_change){
                    .component = GAME_COMPONENT_GameTransform.id,
                    .field = axis,
                    .before = { .type = PIN_F32,
                                .f = axis ? position->y : position->x },
                    .value = { .type = PIN_F32, .f = axis ? after.y : after.x },
                    .changed = moved[axis],
                    .active = active && !committed,
                    .selection = true }
            );
        }
        return;
    }
    if (changed) {
        if (!scene->edit_actor.slot || ecs_entity_to_u64(scene->edit_actor) !=
                                           ecs_entity_to_u64(actor->entity)) {
            begin_transform_edit(scene, actor->entity);
        }
        write_transform_edit(scene, actor->entity, after);
        if (scene_collab_can_write_locally() && !studio_live(s)) {
            scene->scene_dirty = true;
        }
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
            /* A mixed checkbox sets every object on its first click. */
            bool b = !(flags & KIT_MIXED) && value->b;
            changed = kit_checkbox(k, "value", &b, NULL, flags);
            value->b = b;
            break;
        }
        case PIN_I32:
        case PIN_ENUM: {
            const editor_choices *choices = property_choices(desc->type, field);
            int *slot = value->type == PIN_ENUM ? &value->e : &value->i;
            int count = choices ? editor_choices_count(choices) : 0;
            if (count > 0) {
                int metadata_current = -1;
                bool described = false;
                for (int i = 0; i < count; i++) {
                    editor_choice choice = editor_choice_at(choices, i);
                    described |= choice.group || choice.icon;
                    if (choice.value == *slot) {
                        metadata_current = i;
                    }
                }
                if (described) {
                    kit_command_item *items = NULL;
                    for (int i = 0; i < count; i++) {
                        editor_choice choice = editor_choice_at(choices, i);
                        apush(
                            items,
                            ((kit_command_item){ .label = choice.label,
                                                 .group = choice.group,
                                                 .keywords = choice.name,
                                                 .icon = choice.icon,
                                                 .current = i == metadata_current })
                        );
                    }
                    int selected = flags & KIT_MIXED ? -1 : metadata_current;
                    if (kit_choice_items(k, "value", &selected, items, count, flags) &&
                        selected >= 0) {
                        *slot = editor_choice_at(choices, selected).value;
                        changed = true;
                    }
                    afree(items);
                    break;
                }
                const char **labels = NULL;
                int current = -1;
                for (int i = 0; i < count; i++) {
                    editor_choice choice = editor_choice_at(choices, i);
                    apush(labels, choice.label);
                    if (choice.value == *slot) {
                        current = i;
                    }
                }
                int selected = flags & KIT_MIXED ? -1 : current;
                if (kit_choice(k, "value", &selected, labels, count, flags) &&
                    selected >= 0) {
                    *slot = editor_choice_at(choices, selected).value;
                    changed = true;
                }
                afree(labels);
                break;
            }
            double v = *slot;
            kit_number_desc d = { .step = 1, .integer = true, .flags = flags };
            number_hints(s, desc, field, &d);
            number_bounds(value->type, &d);
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
        case PIN_U16: {
            double v = value->type == PIN_U32 ? value->u
                     : value->type == PIN_I8  ? value->i8
                     : value->type == PIN_U8  ? value->u8
                     : value->type == PIN_I16 ? value->i16
                                              : value->u16;
            bool unsigned_type = value->type == PIN_U32 || value->type == PIN_U8 ||
                                 value->type == PIN_U16 || value->type == PIN_U64;
            kit_number_desc d = { .step = 1, .integer = true, .flags = flags };
            number_hints(s, desc, field, &d);
            number_bounds(value->type, &d);
            changed = kit_number(k, "value", &v, &d);
            if (unsigned_type && v < 0) {
                v = 0;
            }
            switch (value->type) {
                case PIN_U32: value->u = (uint32_t)v; break;
                case PIN_I8: value->i8 = (int8_t)v; break;
                case PIN_U8: value->u8 = (uint8_t)v; break;
                case PIN_I16: value->i16 = (int16_t)v; break;
                default: value->u16 = (uint16_t)v; break;
            }
            *active = kit_item_active(k) && !kit_item_committed(k);
            break;
        }
        case PIN_I64:
        case PIN_U64:
            changed = integer64_control(k, value, flags);
            *active = kit_item_active(k) && !kit_item_committed(k);
            break;
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
            number_bounds(value->type, &d);
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
        case PIN_STRUCT: {
            const type_desc *type = vector_type(value);
            if (!type) {
                break;
            }
            void *storage = xmalloc(type->size);
            memcpy(storage, value->ptr, type->size);
            static const char *const labels[] = { "X", "Y", "Z", "W" };
            bool vector_active = false;
            kit_push(k, "joined", NULL, "joined");
            for (int i = 0; i < type->field_count; i++) {
                const type_field *axis = &type->fields[i];
                pin_value component = { 0 };
                pin_read_field(storage, axis, &component);
                double v = component.type == PIN_F32 ? component.f : component.d;
                kit_number_desc d = { .step = fabs(v) >= 100 ? 1 : 0.1,
                                      .precision = 2,
                                      .addon = labels[i],
                                      .addon_cls = i == 0 ? "x"
                                                 : i == 1 ? "y"
                                                          : NULL,
                                      .flags = flags };
                number_hints(s, desc, field, &d);
                number_bounds(component.type, &d);
                if (kit_number(k, axis->name, &v, &d)) {
                    pin_value updated = { .type = component.type };
                    if (component.type == PIN_F32) {
                        updated.f = (float)v;
                    } else {
                        updated.d = v;
                    }
                    pin_write_field(storage, axis, updated);
                    changed = true;
                }
                vector_active |= kit_item_active(k) && !kit_item_committed(k);
            }
            kit_pop(k);
            *active = vector_active;
            if (changed) {
                *value = (pin_value){ .type = PIN_STRUCT,
                                      .type_name = type->name,
                                      .ptr = storage };
            } else {
                free(storage);
            }
            break;
        }
        case PIN_NAME:
        case PIN_STR: {
            /* A mixed text stays as it is until something is typed. */
            bool mixed = flags & KIT_MIXED;
            snprintf(
                text,
                text_size,
                "%s",
                mixed        ? ""
                : value->str ? value->str
                             : ""
            );
            changed = kit_text_field(k, "value", text, text_size, mixed ? "\xe2\x80\x94" : NULL, flags) &&
                      !(mixed && !text[0]);
            value->str = text;
            break;
        }
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

/* A text field naming an asset (reference_named_field_kind) picks one of
 * its kind; true when one was chosen for `key`, which value then holds. */
static bool asset_control(studio *s, const char *key, reference_asset_kind kind, pin_value *value, unsigned flags)
{
    studio_pick_desc desc = { .kind = kind, .none = "None", .text = true };
    const char *chosen = studio_asset_field(s, key, value->str, &desc, flags);
    if (!chosen || !strcmp(chosen, value->str ? value->str : "")) {
        return false;
    }
    value->str = chosen;
    return true;
}

/* The kind of asset a field's text names, or REFERENCE_ASSET_KIND_COUNT. */
static reference_asset_kind named_kind(
    const ecs_component_desc *desc,
    const type_field *field,
    const pin_value *value
)
{
    bool text = value->type == PIN_STR || value->type == PIN_NAME;
    return text ? reference_named_field_kind(desc->type->name, field->name)
                : REFERENCE_ASSET_KIND_COUNT;
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

/* Hovering a property row shows its field's source comment. */
static void field_tip(kit *k, const type_field *field, const char *label)
{
    if (field->description) {
        kit_tip(k, label, NULL, field->description);
    }
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
    bool pending = scene->property_actor.slot &&
                   ecs_entity_to_u64(scene->property_actor) ==
                       ecs_entity_to_u64(actor->entity) &&
                   scene->property_edit.component == desc->id &&
                   scene->property_edit.field == index;
    if (pending && scene_collab_is_editor()) {
        value = scene->property_edit.after;
    }
    const void *original_struct = value.ptr;
    unsigned flags = (locked ? KIT_READONLY : 0) |
                     (field_overridden(scene, actor, component, index) ? KIT_OVERRIDDEN
                                                                       : 0) |
                     mixed_flag(s, actor, desc->id, index, &before);
    bool active = false;
    kit_prop_begin(k, field->name, label);
    reference_asset_kind named = named_kind(desc, field, &value);
    char key[192];
    snprintf(
        key,
        sizeof key,
        "%llu/%s.%s",
        (unsigned long long)ecs_entity_to_u64(actor->entity),
        desc->name,
        field->name
    );
    bool changed = named < REFERENCE_ASSET_KIND_COUNT
                     ? asset_control(s, key, named, &value, flags)
                     : value_control(s, desc, field, &value, flags, &active, text, sizeof text);
    if (named < REFERENCE_ASSET_KIND_COUNT && !locked) {
        preview_drop(s, actor, desc->id, index, key);
    }
    context_target(s, actor, desc->id, index);
    kit_prop_end(k);
    field_tip(k, field, label);
    if (!locked) {
        write_field(
            s,
            actor,
            (field_change){ .component = desc->id,
                            .field = index,
                            .before = before,
                            .value = value,
                            .changed = changed,
                            .active = active,
                            .selection = editing_selection(s, actor) }
        );
    }
    if (value.type == PIN_STRUCT && value.ptr != original_struct) {
        free(value.ptr);
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
        write_transform_edit(scene, actor->entity, after);
        if (scene_collab_can_write_locally() && !studio_live(s)) {
            scene->scene_dirty = true;
        }
        finish_transform_edit(scene, false);
        return;
    }
    write_field(
        s,
        actor,
        (field_change){ .component = desc->id,
                        .field = s->context.field,
                        .before = before,
                        .value = inherited,
                        .changed = true }
    );
}

/* Reset to Prototype resets each selected object, to its own prototype's
   value, as one Undo step. */
static void reset_selection(studio *s, scene_actor *actor)
{
    if (!editing_selection(s, actor)) {
        reset_field(s, actor);
        return;
    }
    app_scene_authoring *scene = s->scene;
    finish_transform_edit(scene, false);
    scene_history_begin_group(scene->history);
    for (int i = 0; i < acount(scene->actors); i++) {
        if (scene_instance_selected(scene, i) &&
            ecs_component_has(scene->world, scene->actors[i].entity, s->context.component)) {
            reset_field(s, &scene->actors[i]);
        }
    }
    scene_history_end_group(scene->history);
}

static void field_menu(studio *s, scene_actor *actor, bool locked)
{
    kit *k = s->kit;
    if (!kit_popup_begin(k, "field-menu", "menu")) {
        return;
    }
    bool target = actor && s->context.entity == ecs_entity_to_u64(actor->entity);
    bool overridden = false;
    for (int i = 0; target && i < s->scene->registry.component_count; i++) {
        const ecs_component_desc *desc = s->scene->registry.components[i];
        if (desc->id != s->context.component) {
            continue;
        }
        /* Any selected object's override enables it. */
        for (int a = 0; a < acount(s->scene->actors); a++) {
            scene_actor *other = &s->scene->actors[a];
            bool selected = other == actor ||
                            (editing_selection(s, actor) &&
                             scene_instance_selected(s->scene, a) &&
                             ecs_component_has(s->scene->world, other->entity, desc->id));
            game_prototype_component added;
            overridden |= selected && field_overridden(
                                          s->scene,
                                          other,
                                          base_component(other, desc, &added),
                                          s->context.field
                                      );
        }
    }
    kit_menu_item_desc reset = { .label = "Reset to Prototype",
                                 .icon = KIT_ICON_RESET,
                                 .checked = -1,
                                 .disabled = locked || !overridden };
    if (kit_menu_item(k, "reset", &reset)) {
        reset_selection(s, actor);
    }
    kit_popup_end(k);
}

#define LIVE_COMPONENT_MENU "live-component-menu"

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
    /* While the scene runs, an authored object's component menu keeps its
       values alone. */
    if (studio_live(s) && actor->identity && kit_panel_menu(k)) {
        snprintf(s->components.menu, sizeof s->components.menu, "%s", desc->name);
        kit_popup_open_at(k, LIVE_COMPONENT_MENU, s->in.mouse_x, s->in.mouse_y);
    }
}

static void live_component_menu(studio *s)
{
    kit *k = s->kit;
    if (!kit_popup_begin(k, LIVE_COMPONENT_MENU, "menu")) {
        return;
    }
    char title[128];
    editor_component_label(s->components.menu, title, sizeof title);
    kit_menu_group(k, "group", title);
    kit_menu_item_desc keep = { .label = "Keep Changes",
                                .icon = KIT_ICON_PIN,
                                .checked = -1 };
    if (kit_menu_item(k, "keep", &keep)) {
        for (int i = 0; i < s->scene->registry.component_count; i++) {
            const ecs_component_desc *desc = s->scene->registry.components[i];
            if (!strcmp(desc->name, s->components.menu)) {
                studio_keep_changes(s, desc);
            }
        }
    }
    kit_popup_end(k);
}

/* Why a structural control is disabled while a run holds the scene, for its
   tip; NULL otherwise. */
static const char *while_playing(const studio *s, bool locked, const char *reason)
{
    return locked && runtime_session(s->scene) ? reason : NULL;
}

/* The name edits in place, as Unity's Inspector names an object: Enter or
   leaving the field renames it, Escape keeps the name. Authored objects
   have an identity; spawned ones do not. */
static void name_field(studio *s, scene_actor *actor, bool locked)
{
    kit *k = s->kit;
    uint64_t entity = ecs_entity_to_u64(actor->entity);
    char name[256];
    snprintf(name, sizeof name, "%s", actor->name);
    kit_push(k, "name", NULL, "name-field");
    bool committed = kit_text_field(k, "field", name, sizeof name, NULL, locked ? KIT_READONLY : 0);
    const char *why = actor->identity
                        ? while_playing(s, locked, "Stop playing to rename")
                        : "Spawned while playing; Stop removes it";
    kit_tip(k, "Name", locked ? NULL : studio_chord(s, "F2"), why);
    /* Rename (F2) focuses it while no area shows the Outliner. */
    if (s->rename.entity == entity && !s->rename.outliner) {
        kit_focus(k);
        s->rename.entity = 0;
    }
    kit_pop(k);
    if (committed && !locked) {
        studio_rename_commit(s, entity, name);
    }
}

static void identity(studio *s, scene_actor *actor, bool locked)
{
    kit *k = s->kit;
    kit_push(k, "identity", NULL, "identity");
    kit_push(k, "glyph", NULL, "glyph");
    kit_icon(k, "icon", NULL, studio_prototype_icon(s, actor->prototype_name));
    kit_pop(k);
    kit_push(k, "who", NULL, "who");
    name_field(s, actor, locked);
    kit_push(k, "lineage", NULL, "lineage");
    kit_label(k, "of", NULL, actor->identity ? "Instance of" : "Spawned from");
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

/* Keeps only the selected objects of `prototype` selected, as Unity's
   "Narrow the Selection" does. */
static void narrow_selection(studio *s, const char *prototype)
{
    app_scene_authoring *scene = s->scene;
    finish_transform_edit(scene, false);
    for (int i = 0; i < acount(scene->actors); i++) {
        scene->actors[i].marked = scene_instance_selected(scene, i) &&
                                  !strcmp(scene->actors[i].prototype_name, prototype);
    }
    for (int i = 0;
         !scene->actors[scene->selected].marked && i < acount(scene->actors);
         i++) {
        scene->selected = scene->actors[i].marked ? i : scene->selected;
    }
}

/* With several objects selected: how many, and a chip per prototype among
   them, with its count, which narrows the selection to that prototype's. */
static void selection_identity(studio *s)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    kit_push(k, "identity", NULL, "identity");
    kit_push(k, "glyph", NULL, "glyph");
    kit_icon(k, "icon", NULL, KIT_ICON_SELECTION);
    kit_pop(k);
    kit_push(k, "who", NULL, "who");
    char title[64];
    snprintf(title, sizeof title, "%d objects", scene_selection_count(scene));
    kit_label(k, "name", "title", title);
    kit_push(k, "lineage", NULL, "lineage prototypes");
    for (int i = 0; i < acount(scene->actors); i++) {
        const char *prototype = scene->actors[i].prototype_name;
        int count = 0, first = i;
        for (int j = 0; j < acount(scene->actors); j++) {
            bool same = scene_instance_selected(scene, j) &&
                        !strcmp(scene->actors[j].prototype_name, prototype);
            first = same && j < first ? j : first;
            count += same;
        }
        if (!scene_instance_selected(scene, i) || first != i) {
            continue;
        }
        char key[16], label[160];
        snprintf(key, sizeof key, "p%d", i);
        snprintf(label, sizeof label, count > 1 ? "%s \xc3\x97%d" : "%s", prototype, count);
        kit_button_desc chip = { .label = label,
                                 .icon = studio_prototype_icon(s, prototype),
                                 .tip = "Select only these" };
        if (kit_button(k, key, &chip)) {
            narrow_selection(s, prototype);
        }
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
    const char *why = while_playing(s, locked, "Stop playing to change behaviors");
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
                                       .flags = locked ? KIT_DISABLED : 0 };
            bool detached = kit_button(k, "detach", &detach);
            kit_tip(k, "Detach", NULL, why);
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
        kit_tip(k, why, NULL, NULL);
        kit_pop(k);
    }
    kit_panel_end(k);
    if (kit_popup_begin(k, "attach-behavior", "menu")) {
        const reference_index *index = app_scene_authoring_asset_index(scene);
        int total = index ? reference_index_asset_count(index) : 0;
        for (int i = 0; i < total; i++) {
            const reference_asset *asset = reference_index_asset_at(index, i);
            if (asset->kind != REFERENCE_ASSET_BEHAVIOR || !asset->name) {
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

/* The scene's root ui document: the project's default, or one of its ui
 * assets. */
static void page_panel(studio *s, bool locked)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    if (!studio_matches(s->property_search, "Root document")) {
        return;
    }
    if (!kit_panel_begin(k, "ui", "UI", NULL, true)) {
        kit_panel_end(k);
        return;
    }
    char project[160];
    snprintf(project, sizeof project, "Project (%s)", scene->default_ui ? scene->default_ui : "none");
    kit_prop_begin(k, "root", "Root document");
    studio_pick_desc desc = { .kind = REFERENCE_ASSET_UI, .none = project };
    const char *chosen = studio_asset_field(s, "scene.ui", scene->ui, &desc, locked ? KIT_READONLY : 0);
    if (chosen && strcmp(chosen, scene->ui ? scene->ui : "")) {
        app_scene_authoring_set_ui(scene, chosen);
    }
    kit_prop_end(k);
    kit_panel_end(k);
}

static void scene_panels(studio *s, bool locked)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    const char *why = while_playing(s, locked, "Stop playing to change scene settings");
    if (why) {
        kit_label(k, "stopped", "prop-note", why);
    }
    page_panel(s, locked);
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
                                  .single_precision = true,
                                  .flags = flags };
            if (kit_number(k, "value", &v, &d)) {
                next.contact_hertz = (float)v;
            }
            kit_prop_end(k);
        }
        if (studio_matches(s->property_search, "Damping")) {
            kit_prop_begin(k, "damping", "Damping");
            double v = settings.contact_damping;
            kit_number_desc d = { .single_precision = true,
                                  .min = 0,
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
    const void *original_struct = value.ptr;
    if (!field_label(s, component, field, label, sizeof label)) {
        return;
    }
    bool active = false;
    unsigned flags = locked ? KIT_READONLY : 0;
    kit_prop_begin(k, field->name, label);
    reference_asset_kind named = named_kind(component->desc, field, &value);
    char pick[192];
    snprintf(
        pick,
        sizeof pick,
        "%s/%s.%s",
        asset->prototype->name,
        component->desc->name,
        field->name
    );
    bool changed = named < REFERENCE_ASSET_KIND_COUNT
                     ? asset_control(s, pick, named, &value, flags)
                     : value_control(s, component->desc, field, &value, flags, &active, text, sizeof text);
    kit_prop_end(k);
    field_tip(k, field, label);
    if (locked) {
        if (value.type == PIN_STRUCT && value.ptr != original_struct) {
            free(value.ptr);
        }
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
    if (value.type == PIN_STRUCT && value.ptr != original_struct) {
        free(value.ptr);
    }
}

/* ---------- Add and remove prototype components ---------- */

#define COMPONENT_MENU "component-menu"

void studio_add_component_open(studio *s)
{
    s->components.open = true;
    snprintf(
        s->components.prototype,
        sizeof s->components.prototype,
        "%s",
        selected_prototype(s->scene)->prototype->name
    );
    kit_command_open(s->kit, "Search components");
}

static void add_component(studio *s, const char *prototype, const ecs_component_desc *desc)
{
    if (!studio_editable(s)) {
        return;
    }
    if (app_scene_authoring_add_prototype_component(s->scene, prototype, desc->name) != 0) {
        studio_error(s, "Could not add component", s->scene->prototype_message);
        return;
    }
    char label[128], report[320];
    editor_component_label(desc->name, label, sizeof label);
    snprintf(report, sizeof report, "Added %s to %s", label, prototype);
    studio_report(s, report);
    /* Its panel shows, open, as Unity adds a component. */
    s->tab = tab_of(s, desc);
    snprintf(s->components.added, sizeof s->components.added, "%s", desc->name);
}

/* A component the prototype lacks, as the command surface lists it. */
typedef struct
{
    int order; /* its tab's, the current tab's first */
    char label[128];
    const ecs_component_desc *desc;
} component_choice;

static int by_tab_and_label(const void *a, const void *b)
{
    const component_choice *x = a, *y = b;
    return x->order != y->order ? x->order - y->order : strcmp(x->label, y->label);
}

bool studio_component_palette(studio *s)
{
    if (!s->components.open) {
        return false;
    }
    if (!kit_command_is_open(s->kit)) {
        s->components.open = false;
        return false;
    }
    app_scene_authoring *scene = s->scene;
    scene_prototype_asset *asset = find_prototype(scene, s->components.prototype);
    int current = s->tab >= STUDIO_TAB_OBJECT && s->tab <= STUDIO_TAB_GAMEPLAY
                    ? s->tab - STUDIO_TAB_OBJECT
                    : 0;
    component_choice *choices = NULL;
    for (int i = 0; asset && i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (prototype_has_component_id(asset->prototype, desc->id)) {
            continue;
        }
        component_choice choice = {
            .order = (tab_of(s, desc) - STUDIO_TAB_OBJECT - current + COMPONENT_TAB_COUNT) %
                     COMPONENT_TAB_COUNT,
            .desc = desc,
        };
        editor_component_label(desc->name, choice.label, sizeof choice.label);
        apush(choices, choice);
    }
    if (acount(choices)) {
        qsort(choices, (size_t)acount(choices), sizeof *choices, by_tab_and_label);
    }
    kit_command_item *items = NULL;
    for (int i = 0; i < acount(choices); i++) {
        studio_tab tab = tab_of(s, choices[i].desc);
        apush(
            items,
            ((kit_command_item){ .label = choices[i].label,
                                 .group = tab_label(tab),
                                 .keywords = choices[i].desc->name,
                                 .icon = TAB_ICONS[tab] })
        );
    }
    int chosen = kit_command(s->kit, items, acount(items));
    if (chosen >= 0) {
        s->components.open = false;
        add_component(s, s->components.prototype, choices[chosen].desc);
    }
    afree(items);
    afree(choices);
    return true;
}

/* Destructive edits offer Undo in a toast while they are still the latest
   edit, as Delete does. */
static void remove_component(studio *s, scene_prototype_asset *asset)
{
    const char *name = s->components.menu;
    char label[128], title[320];
    editor_component_label(name, label, sizeof label);
    snprintf(title, sizeof title, "Removed %s from %s", label, asset->prototype->name);
    if (app_scene_authoring_remove_prototype_component(
            s->scene,
            asset->prototype->name,
            name
        ) != 0) {
        studio_error(s, "Could not remove component", s->scene->prototype_message);
        return;
    }
    kit_toast_dismiss(s->kit, s->components.toast);
    s->components.toast = kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){ .title = title, .action = "Undo", .duration = 6 }
    );
    s->components.step = alast(asset->undo);
    studio_record(s, KIT_TOAST_DEFAULT, title);
}

void studio_component_toast(studio *s)
{
    int toast = s->components.toast;
    if (!toast) {
        return;
    }
    scene_prototype_asset *asset = s->scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE
                                     ? selected_prototype(s->scene)
                                     : NULL;
    bool current = asset && acount(asset->undo) &&
                   alast(asset->undo) == s->components.step;
    if (kit_toast_action(s->kit, toast)) {
        s->components.toast = 0;
        if (current && studio_editable(s) && prototype_undo(s->scene) == 0) {
            studio_report(s, "Undo");
        }
        return;
    }
    if (!current) {
        kit_toast_dismiss(s->kit, toast);
        s->components.toast = 0;
    }
}

/* ---------- Keep Changes ---------- */

void studio_keep_changes(studio *s, const ecs_component_desc *only)
{
    scene_actor *actor = selected_actor(s->scene);
    int kept = scene_keep_changes(s->scene, only);
    if (kept < 0) {
        return;
    }
    char title[320];
    kit_toast_dismiss(s->kit, s->keep.toast);
    s->keep.toast = 0;
    if (!kept) {
        snprintf(title, sizeof title, "%s matches the scene", actor->name);
        kit_toast(s->kit, 0, &(kit_toast_desc){ .type = KIT_TOAST_INFO, .title = title });
        return;
    }
    snprintf(
        title,
        sizeof title,
        "Kept %d %s on %s",
        kept,
        kept == 1 ? "value" : "values",
        actor->name
    );
    s->keep.toast = kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){
            .title = title,
            .description = "They apply to the scene when you stop",
            .action = "Undo",
            .duration = 6 }
    );
    s->keep.depth = acount(s->scene->kept);
    studio_record(s, KIT_TOAST_DEFAULT, title);
}

void studio_keep_toast(studio *s)
{
    int toast = s->keep.toast;
    if (!toast) {
        return;
    }
    bool current = acount(s->scene->kept) == s->keep.depth;
    if (kit_toast_action(s->kit, toast)) {
        s->keep.toast = 0;
        if (current) {
            scene_kept_drop(s->scene);
            studio_report(s, "Undo Keep Changes");
        }
        return;
    }
    if (!current) {
        kit_toast_dismiss(s->kit, toast);
        s->keep.toast = 0;
    }
}

/* A component panel's menu: its header's button or a right press there. */
static void component_menu(studio *s, scene_prototype_asset *asset, bool locked)
{
    kit *k = s->kit;
    if (!kit_popup_begin(k, COMPONENT_MENU, "menu")) {
        return;
    }
    char title[128];
    editor_component_label(s->components.menu, title, sizeof title);
    kit_menu_group(k, "group", title);
    kit_menu_item_desc remove = {
        .label = "Remove Component",
        .icon = KIT_ICON_TRASH,
        .checked = -1,
        .disabled = locked,
        .description = while_playing(s, locked, "Stop playing to remove")
    };
    if (kit_menu_item(k, "remove", &remove)) {
        remove_component(s, asset);
    }
    kit_popup_end(k);
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
        int components = acount(prototype->components);
        snprintf(
            lineage,
            sizeof lineage,
            "%d %s \xc2\xb7 %d %s",
            components,
            components == 1 ? "component" : "components",
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
                            desc->id == GAME_COMPONENT_GameOrientation.id ||
                            !strcmp(desc->name, s->components.added);
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
        if (kit_panel_menu(k)) {
            snprintf(s->components.menu, sizeof s->components.menu, "%s", desc->name);
            kit_popup_open_at(k, COMPONENT_MENU, s->in.mouse_x, s->in.mouse_y);
        }
    }
    kit_push(k, "add", NULL, "add-row");
    kit_button_desc add = { .label = "Add Component",
                            .icon = KIT_ICON_ADD,
                            .variant = KIT_SUBTLE,
                            .flags = locked ? KIT_DISABLED : 0 };
    if (kit_button(k, "component", &add)) {
        studio_command_run(s, STUDIO_COMMAND_ADD_COMPONENT);
    }
    kit_tip(k, while_playing(s, locked, "Stop playing to add components"), NULL, NULL);
    kit_pop(k);
    component_menu(s, asset, locked);
}

/* An object the running scene spawned shows as an instance of its prototype;
   its values edit live, and it has no authored state to keep. */
static scene_actor *spawned_actor(app_scene_authoring *scene, scene_actor *out)
{
    ecs_entity entity = selected_scene_entity(scene);
    if (!scene->runtime_selected.slot || !entity.slot) {
        return NULL;
    }
    /* Borrowed for this frame; nothing writes through it. */
    char *name = (char *)runtime_entity_label(scene, entity);
    scene_prototype_asset *asset = find_prototype(scene, name);
    *out = (scene_actor){ .entity = entity,
                          .name = name,
                          .prototype_name = name,
                          .base = asset ? asset->prototype : NULL };
    return out;
}

static studio_tab prototype_default_tab(const studio *s, const scene_prototype_asset *asset)
{
    const game_prototype *prototype = asset->prototype;
    for (int i = 0; i < acount(prototype->components); i++) {
        if (tab_of(s, prototype->components[i].desc) == STUDIO_TAB_OBJECT) {
            return STUDIO_TAB_OBJECT;
        }
    }
    return acount(prototype->components) ? tab_of(s, prototype->components[0].desc)
                                         : STUDIO_TAB_OBJECT;
}

void studio_properties_body(studio *s, int area)
{
    /* Visual designer (workstream H): a UI document's Details. */
    if (studio_designer_active(s)) {
        studio_designer_details(s, area);
        return;
    }
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    app_editor_document_context document = app_scene_authoring_document(scene);
    bool prototype_document = document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE;
    scene_prototype_asset *asset = prototype_document ? selected_prototype(scene)
                                                      : NULL;
    scene_actor spawned;
    scene_actor *actor = prototype_document ? NULL : selected_actor(scene);
    if (!actor && !prototype_document) {
        actor = spawned_actor(scene, &spawned);
    }
    bool locked = !studio_editable(s);
    /* Several selected objects show the components they share. */
    bool multi = actor && editing_selection(s, actor);
    bool available[STUDIO_TAB_COUNT] = { [STUDIO_TAB_SCENE] = asset == NULL };
    if (asset && asset->prototype) {
        available[STUDIO_TAB_OBJECT] = true;
        for (int i = 0; i < acount(asset->prototype->components); i++) {
            available[tab_of(s, asset->prototype->components[i].desc)] = true;
        }
    } else if (actor) {
        available[STUDIO_TAB_OBJECT] = true;
        available[STUDIO_TAB_BEHAVIORS] = !multi;
        for (int i = 0; i < scene->registry.component_count; i++) {
            const ecs_component_desc *desc = scene->registry.components[i];
            if (ecs_component_has(scene->world, actor->entity, desc->id) &&
                (!multi || shared_component(s, desc->id))) {
                available[tab_of(s, desc)] = true;
            }
        }
    }
    /* The Node tab follows the graph selection, as Object follows the
   scene's: a newly selected node shows its details, a newly selected
   object its components. */
    uint64_t node = studio_graph_node(s) ? studio_graph_node(s)
                                         : studio_graph_comment(s);
    bool graph_document = document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR ||
                          document.kind == APP_EDITOR_DOCUMENT_SYSTEM;
    available[STUDIO_TAB_NODE] = graph_document && node != 0;
    if (node && node != s->shown_node) {
        s->tab = STUDIO_TAB_NODE;
    } else if (s->tab == STUDIO_TAB_NODE && scene->selected != s->shown_actor && actor) {
        s->tab = STUDIO_TAB_OBJECT;
    } else if (s->tab == STUDIO_TAB_SCENE && s->shown_actor < 0 && scene->selected >= 0) {
        /* A selection made from none, as a marquee makes one, shows it. */
        s->tab = STUDIO_TAB_OBJECT;
    }
    /* Member follows the Members selection the same way. */
    uint64_t member = studio_member_shown(s);
    available[STUDIO_TAB_MEMBER] = graph_document && member != 0;
    if (member && member != s->shown_member) {
        s->tab = STUDIO_TAB_MEMBER;
    }
    s->shown_member = member;
    s->shown_node = node;
    s->shown_actor = scene->selected;
    if (!available[s->tab]) {
        s->tab = asset ? prototype_default_tab(s, asset)
               : actor ? STUDIO_TAB_OBJECT
                       : STUDIO_TAB_SCENE;
    }
    rml_element *host = studio_ui_panel(s, "properties", "Properties");
    if (host) {
        if (!kit_slot_begin(k, "tabs", host, "#tabs")) {
            assert(0 && "Properties.rml needs its #tabs slot");
            return;
        }
    } else {
        kit_push(k, "properties", NULL, "properties");
        kit_push(k, "tabs", "nav", "prop-tabs");
    }
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
    if (host) {
        kit_slot_end(k);
        if (!kit_slot_begin(k, "scroll", host, "#scroll")) {
            assert(0 && "Properties.rml needs its #scroll slot");
            return;
        }
    } else {
        kit_pop(k);
        kit_push(k, "scroll", NULL, "prop-scroll");
    }
    if (s->tab == STUDIO_TAB_SCENE) {
        scene_panels(s, locked);
    } else if (s->tab == STUDIO_TAB_NODE) {
        studio_node_properties(s);
    } else if (s->tab == STUDIO_TAB_MEMBER) {
        studio_member_properties(s);
    } else if (asset && asset->prototype) {
        prototype_properties(s, asset, locked);
    } else if (!actor) {
        kit_label(k, "empty", "prop-empty", "Select an object to edit its properties");
    } else {
        if (s->tab == STUDIO_TAB_OBJECT && multi) {
            selection_identity(s);
        } else if (s->tab == STUDIO_TAB_OBJECT) {
            identity(s, actor, locked);
        }
        if (s->tab == STUDIO_TAB_BEHAVIORS) {
            behaviors_panel(s, actor, locked);
        } else {
            for (int i = 0; i < scene->registry.component_count; i++) {
                const ecs_component_desc *desc = scene->registry.components[i];
                if (ecs_component_has(scene->world, actor->entity, desc->id) &&
                    tab_of(s, desc) == s->tab &&
                    (!multi || shared_component(s, desc->id))) {
                    /* Values edit the running world while the scene runs. */
                    component_panel(s, actor, desc, locked && !studio_live(s));
                }
            }
        }
    }
    if (host) {
        kit_slot_end(k);
    } else {
        kit_pop(k);
        kit_pop(k);
    }
    field_menu(s, actor, locked && !studio_live(s));
    live_component_menu(s);
}
