/* Asset pickers: a field naming an asset shows it as a select and chooses
 * another of its kind in an anchored search dropdown, as Unity's object field
 * and Unreal's asset picker search a project's assets. A choice waits for the
 * field that opened the pick, as the kit returns a control's input to its
 * next call. */
#include "editor/studio/studio_internal.h"
#include "support/array.h"
#include "support/asset_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int by_name(const void *a, const void *b)
{
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

bool studio_asset_nameable(const reference_asset *asset, int kind)
{
    const char *suffix = kind == REFERENCE_ASSET_UI ? UI_MARKUP_EXTENSION
                                                    : ASSET_EXTENSION;
    size_t length = asset->path ? strlen(asset->path) : 0, extension = strlen(suffix);
    return (int)asset->kind == kind && !asset->unresolved &&
           (!asset->path || (length > extension &&
                             !strcmp(asset->path + length - extension, suffix)));
}

const char **studio_asset_names(const reference_index *index, int kind)
{
    const char **names = NULL;
    for (int i = 0; index && i < reference_index_asset_count(index); i++) {
        const reference_asset *asset = reference_index_asset_at(index, i);
        if (studio_asset_nameable(asset, kind)) {
            apush(names, asset->name);
        }
    }
    if (acount(names) > 1) {
        qsort(names, (size_t)acount(names), sizeof *names, by_name);
    }
    return names;
}

void studio_pick_open(studio *s, const char *key, const char *current, const studio_pick_desc *desc)
{
    s->pick.open = true;
    s->pick.chosen = false;
    s->pick.kind = desc->kind;
    s->pick.text = desc->text;
    snprintf(s->pick.key, sizeof s->pick.key, "%s", key);
    snprintf(s->pick.none, sizeof s->pick.none, "%s", desc->none ? desc->none : "");
    snprintf(s->pick.current, sizeof s->pick.current, "%s", current ? current : "");
    char placeholder[96];
    snprintf(placeholder, sizeof placeholder, "Search %s", studio_asset_kind_label(desc->kind));
    if (!strcmp(key, STUDIO_PICK_PREVIEW)) {
        kit_command_open(s->kit, placeholder);
        return;
    }
    kit_dropdown_open(s->kit, "asset-picker", placeholder);
}

static bool any_matches(const char *const *names, const char *query)
{
    for (int i = 0; i < acount(names); i++) {
        if (studio_matches(query, names[i])) {
            return true;
        }
    }
    return false;
}

static void choose(studio *s, const char *value)
{
    s->pick.open = false;
    if (!strcmp(s->pick.key, STUDIO_PICK_PREVIEW)) {
        studio_preview_ui(s, value);
        return;
    }
    snprintf(s->pick.value, sizeof s->pick.value, "%s", value);
    s->pick.chosen = true;
}

bool studio_pick_palette(studio *s)
{
    if (!s->pick.open) {
        return false;
    }
    bool field = strcmp(s->pick.key, STUDIO_PICK_PREVIEW) != 0;
    bool open = field ? kit_dropdown_is_open(s->kit, "asset-picker")
                      : kit_command_is_open(s->kit);
    if (!open) {
        s->pick.open = false;
        return false;
    }
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    const char **names = studio_asset_names(index, s->pick.kind);
    const char *group = studio_asset_kind_label(s->pick.kind);
    uint32_t icon = studio_asset_kind_icon(s->pick.kind);
    kit_command_item *items = NULL;
    if (s->pick.none[0]) {
        apush(items, ((kit_command_item){ .label = s->pick.none, .icon = KIT_ICON_CIRCLE }));
    }
    int first = acount(items);
    for (int i = 0; i < acount(names); i++) {
        bool current = !strcmp(names[i], s->pick.current);
        apush(
            items,
            ((kit_command_item){ .label = names[i],
                                 .group = group,
                                 .meta = current ? "Current" : NULL,
                                 .icon = icon,
                                 .current = current })
        );
    }
    /* A name no asset has yet, as typed, once no asset matches it: an exact
       match would otherwise rank it above the assets. */
    const char *query = field ? kit_dropdown_query(s->kit, "asset-picker")
                              : kit_command_query(s->kit);
    bool typed = s->pick.text && query[0] && !any_matches(names, query);
    if (typed) {
        apush(
            items,
            ((kit_command_item){ .label = query,
                                 .group = "Name",
                                 .icon = KIT_ICON_EDIT,
                                 .description = "No asset has this name yet" })
        );
    }
    int chosen = field ? kit_dropdown(s->kit, "asset-picker", items, acount(items))
                       : kit_command(s->kit, items, acount(items));
    if (chosen >= 0) {
        choose(s, chosen < first ? "" : items[chosen].label);
    }
    afree(items);
    afree(names);
    return true;
}

const char *studio_asset_field(
    studio *s,
    const char *key,
    const char *value,
    const studio_pick_desc *desc,
    unsigned flags
)
{
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    /* Events are names: host, action and ui events have no asset. */
    bool named = value && value[0] && index && desc->kind != REFERENCE_ASSET_EVENT;
    const reference_asset *found = named
                                     ? reference_index_asset_at(
                                           index,
                                           reference_index_find_name(index, desc->kind, value)
                                       )
                                     : NULL;
    bool unresolved = named && (!found || found->unresolved);
    char tip[192] = "";
    if (unresolved) {
        snprintf(tip, sizeof tip, "No %s asset is named %s", studio_asset_kind_name(desc->kind), value);
    }
    kit_picker_desc picker = { .value = value,
                               .placeholder = desc->none ? desc->none : "",
                               .icon = studio_asset_kind_icon(desc->kind),
                               .tip = tip[0] ? tip : NULL,
                               .flags = flags | (unresolved ? KIT_INVALID : 0) };
    if (kit_picker(s->kit, "value", &picker)) {
        studio_pick_open(s, key, value, desc);
    }
    if (!s->pick.chosen || strcmp(s->pick.key, key)) {
        return NULL;
    }
    s->pick.chosen = false;
    return s->pick.value;
}
