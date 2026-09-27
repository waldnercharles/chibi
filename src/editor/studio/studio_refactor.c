/* Rename and safe delete across files: one popover names the symbol anew
 * and lists every asset the rename rewrites, or lists what still uses a
 * symbol being deleted and asks to delete it anyway (JetBrains' Rename and
 * Safe Delete; Blender asks in a popup menu). Applying writes every file or
 * none (editor/workspace/asset_refactor.h); a toast then offers Undo, which
 * puts every file back. */
#include "assets/refactor.h"
#include "editor/studio/studio_graph_internal.h"
#include "editor/workspace/asset_refactor.h"
#include "graph/document/events.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POPOVER "refactor"
#define LISTED 10 /* assets or uses a popover lists before "N more" */

static void close_state(studio_refactor *r)
{
    app_refactor_free(r->plan);
    afree(r->uses);
    *r = (studio_refactor){ .undo = r->undo, .undo_toast = r->undo_toast };
}

/* The symbol with its asset found again, since the index may have been
   rebuilt since the popover opened. */
static reference_symbol resolved(studio *s)
{
    studio_refactor *r = &s->refactor;
    app_scene_authoring_refresh_asset_index(s->scene);
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    reference_symbol symbol = r->symbol;
    symbol.asset = reference_index_find_name(index, r->asset_kind, r->asset);
    symbol.name = symbol.kind == REFERENCE_SYMBOL_EVENT ? r->event : NULL;
    return symbol;
}

static void begin(studio *s, studio_refactor_mode mode, reference_symbol symbol, const char *name)
{
    studio_refactor *r = &s->refactor;
    close_state(r);
    const reference_asset *asset = reference_index_asset_at(
        app_scene_authoring_asset_index(s->scene),
        symbol.asset
    );
    bool event = symbol.kind == REFERENCE_SYMBOL_EVENT;
    r->mode = mode;
    r->symbol = symbol;
    r->asset_kind = event ? REFERENCE_ASSET_EVENT : (int)asset->kind;
    snprintf(
        r->asset,
        sizeof r->asset,
        "%s",
        event ? (symbol.name ? symbol.name : "") : asset->name
    );
    snprintf(r->event, sizeof r->event, "%s", event ? symbol.name : "");
    snprintf(r->title, sizeof r->title, "%s", name);
    snprintf(r->name, sizeof r->name, "%s", name);
    r->opening = true;
    r->focus = true;
}

static void plan(studio *s)
{
    studio_refactor *r = &s->refactor;
    app_refactor_free(r->plan);
    r->plan = NULL;
    r->error[0] = '\0';
    snprintf(r->planned, sizeof r->planned, "%s", r->name);
    app_scene_authoring_plan_rename(
        s->scene,
        resolved(s),
        r->name,
        &r->plan,
        r->error,
        sizeof r->error
    );
}

static void open_rename(studio *s, reference_symbol symbol, const char *name)
{
    begin(s, STUDIO_REFACTOR_RENAME, symbol, name);
}

bool studio_refactor_member_rename(studio *s, reference_symbol symbol, const char *name, const char *new_name)
{
    if (!app_scene_authoring_refactor_reaches(s->scene, symbol)) {
        return false;
    }
    begin(s, STUDIO_REFACTOR_RENAME, symbol, name);
    snprintf(s->refactor.name, sizeof s->refactor.name, "%s", new_name);
    plan(s);
    return true;
}

/* What still uses a symbol, as "Asset › Place". */
static void describe_uses(studio *s, reference_symbol symbol)
{
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    const reference **uses = NULL;
    refactor_blockers(index, symbol, &uses);
    for (int i = 0; i < acount(uses); i++) {
        char place[192];
        refactor_describe(index, uses[i], place, sizeof place);
        studio_refactor_use use;
        snprintf(use.text, sizeof use.text, "%s", place);
        apush(s->refactor.uses, use);
    }
    afree(uses);
}

bool studio_refactor_confirm_delete(
    studio *s,
    reference_symbol symbol,
    const char *name,
    studio_member_kind member,
    uint64_t id
)
{
    app_scene_authoring_refresh_asset_index(s->scene);
    const reference **uses = NULL;
    refactor_blockers(app_scene_authoring_asset_index(s->scene), symbol, &uses);
    bool used = acount(uses) > 0;
    afree(uses);
    if (!used) {
        return false;
    }
    begin(s, STUDIO_REFACTOR_DELETE, symbol, name);
    s->refactor.member = member;
    s->refactor.member_id = id;
    describe_uses(s, symbol);
    return true;
}

/* ---------- Applying ---------- */

static void offer_undo(studio *s, app_refactor *undo, const char *title, int assets)
{
    studio_refactor *r = &s->refactor;
    app_refactor_free(r->undo);
    r->undo = undo;
    char description[64];
    snprintf(description, sizeof description, "%d %s", assets, assets == 1 ? "asset" : "assets");
    kit_toast_dismiss(s->kit, r->undo_toast);
    r->undo_toast = kit_toast(
        s->kit,
        0,
        &(
            kit_toast_desc
        ){ .title = title, .description = description, .action = "Undo", .duration = 6 }
    );
    studio_record(s, KIT_TOAST_DEFAULT, title);
}

static void refused(studio *s, const char *title, const char *error)
{
    kit_toast(s->kit, 0, &(kit_toast_desc){ .type = KIT_TOAST_WARNING, .title = title, .description = error });
}

/* Applies a plan; renaming the open graph asset keeps its graph's camera. */
static int apply_plan(studio *s, const app_refactor *plan, app_refactor **undo, char *error, size_t size)
{
    const char *id = s->scene->graph_document_id;
    char *open = id ? xstrdup(id) : NULL;
    int result = app_scene_authoring_apply_refactor(s->scene, plan, undo, error, size);
    for (int i = 0; result == 0 && open && i < app_refactor_count(plan); i++) {
        app_refactor_asset asset = app_refactor_at(plan, i);
        if (asset.renamed && !strcmp(asset.name, open) && s->scene->graph_document_id &&
            !strcmp(asset.renamed, s->scene->graph_document_id)) {
            sg_keep_graph_view(s);
        }
    }
    free(open);
    return result;
}

static void apply(studio *s, const app_refactor *plan, const char *title)
{
    app_refactor *undo = NULL;
    char error[256] = "";
    if (apply_plan(s, plan, &undo, error, sizeof error) != 0) {
        refused(s, "Nothing changed", error);
        return;
    }
    offer_undo(s, undo, title, app_refactor_count(plan));
}

static void apply_rename(studio *s)
{
    studio_refactor *r = &s->refactor;
    char title[192];
    snprintf(title, sizeof title, "Renamed %s", r->title);
    apply(s, r->plan, title);
    close_state(r);
}

bool studio_asset_refactorable(const reference_asset *asset)
{
    return asset && !asset->unresolved && asset->kind != REFERENCE_ASSET_SCENE &&
           asset->kind != REFERENCE_ASSET_SYSTEM &&
           (asset->kind != REFERENCE_ASSET_UI ||
            studio_asset_nameable(asset, REFERENCE_ASSET_UI));
}

/* Deletes an asset's file, or an event definition's. */
static void delete_asset(studio *s, reference_symbol symbol, const char *name)
{
    app_refactor *plan = NULL;
    char error[256] = "";
    char title[192];
    snprintf(title, sizeof title, "Deleted %s", name);
    if (app_scene_authoring_plan_delete(s->scene, symbol, &plan, error, sizeof error) !=
        0) {
        refused(s, "Can't delete", error);
        return;
    }
    apply(s, plan, title);
    app_refactor_free(plan);
}

void studio_refactor_delete_asset(studio *s, int asset)
{
    const reference_asset *
        found = reference_index_asset_at(app_scene_authoring_asset_index(s->scene), asset);
    if (!studio_asset_refactorable(found)) {
        return;
    }
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
    char name[128];
    snprintf(name, sizeof name, "%s", found->name);
    if (!studio_refactor_confirm_delete(s, symbol, name, STUDIO_MEMBER_NONE, 0)) {
        delete_asset(s, symbol, name);
    }
}

void studio_refactor_rename_asset(studio *s, int asset)
{
    const reference_asset *
        found = reference_index_asset_at(app_scene_authoring_asset_index(s->scene), asset);
    if (studio_asset_refactorable(found)) {
        open_rename(
            s,
            (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset },
            found->name
        );
    }
}

static void confirm_delete(studio *s)
{
    studio_refactor *r = &s->refactor;
    if (r->member) {
        studio_members_delete_now(s, r->member, r->member_id);
    } else {
        char name[128];
        snprintf(name, sizeof name, "%s", r->title);
        delete_asset(s, resolved(s), name);
    }
    close_state(r);
}

/* ---------- Symbols ---------- */

const char *studio_event_renameable(const node *n)
{
    const char *name = n && n->desc ? n->desc->name : "";
    bool event = !strcmp(name, "Event") || !strcmp(name, "Call Event") ||
                 !strcmp(name, "Send Event");
    return event && n->reference_name && n->reference_name[0] &&
                   !graph_event_host_name(n->reference_name)
             ? n->reference_name
             : NULL;
}

void studio_refactor_rename_event(studio *s, const char *event)
{
    if (graph_event_host_name(event)) {
        return;
    }
    app_scene_authoring_refresh_asset_index(s->scene);
    reference_symbol symbol = {
        .kind = REFERENCE_SYMBOL_EVENT,
        .asset = reference_index_find_name(app_scene_authoring_asset_index(s->scene), REFERENCE_ASSET_EVENT, event),
        .name = event,
    };
    open_rename(s, symbol, event);
}

/* ---------- Popover ---------- */

static void row(kit *k, const char *key, uint32_t icon, const char *label, const char *meta)
{
    kit_push(k, key, NULL, "refactor-row");
    kit_icon(k, "icon", "lead", icon);
    kit_label(k, "label", "label", label);
    if (meta) {
        kit_label(k, "meta", "kbd", meta);
    }
    kit_pop(k);
}

static void more_row(kit *k, int more)
{
    char text[32];
    snprintf(text, sizeof text, "%d more", more);
    row(k, "more", KIT_ICON_MORE, text, NULL);
}

static void plan_rows(studio *s)
{
    kit *k = s->kit;
    const app_refactor *plan = s->refactor.plan;
    int count = app_refactor_count(plan);
    for (int i = 0; i < count && i < LISTED; i++) {
        app_refactor_asset asset = app_refactor_at(plan, i);
        char key[16], meta[64];
        snprintf(key, sizeof key, "a%d", i);
        if (asset.renamed) {
            snprintf(meta, sizeof meta, "%s", asset.renamed);
        } else {
            snprintf(meta, sizeof meta, "%d", asset.changes);
        }
        row(k, key, studio_asset_kind_icon(asset.kind), asset.name, meta);
    }
    if (count > LISTED) {
        more_row(k, count - LISTED);
    }
    int mentions = app_refactor_mentions(plan);
    if (mentions) {
        char text[48];
        snprintf(text, sizeof text, "%d %s", mentions, mentions == 1 ? "mention" : "mentions");
        row(k, "mentions", KIT_ICON_WARNING, text, NULL);
    }
}

static bool enter_pressed(const studio *s)
{
    for (int i = 0; i < acount(s->keys); i++) {
        if ((s->keys[i].key == SDLK_RETURN || s->keys[i].key == SDLK_KP_ENTER) &&
            !s->keys[i].repeat) {
            return true;
        }
    }
    return false;
}

static void rename_body(studio *s)
{
    kit *k = s->kit;
    studio_refactor *r = &s->refactor;
    kit_push(k, "field", NULL, "refactor-field");
    bool committed = kit_text_field(k, "name", r->name, sizeof r->name, NULL, 0);
    if (r->focus) {
        kit_focus(k);
        r->focus = false;
    }
    kit_pop(k);
    /* Enter on a new name shows what it rewrites; Enter again applies it. */
    if (committed && strcmp(r->name, r->title)) {
        plan(s);
    }
    bool current = r->plan && !strcmp(r->planned, r->name);
    /* The Enter that planned a member's rename in its own field shows the
       popover; only an Enter pressed while it shows applies. */
    if (!committed && current && r->shown && enter_pressed(s)) {
        apply_rename(s);
        return;
    }
    if (r->error[0]) {
        row(k, "error", KIT_ICON_WARNING, r->error, NULL);
    }
    if (current) {
        plan_rows(s);
    }
    kit_menu_separator(k, "sep");
    kit_menu_item_desc d = { .label = "Rename",
                             .icon = KIT_ICON_EDIT,
                             .checked = -1,
                             .disabled = !current };
    if (kit_menu_item(k, "apply", &d)) {
        apply_rename(s);
    }
}

static void delete_body(studio *s)
{
    kit *k = s->kit;
    studio_refactor *r = &s->refactor;
    int count = acount(r->uses);
    for (int i = 0; i < count && i < LISTED; i++) {
        char key[16];
        snprintf(key, sizeof key, "u%d", i);
        row(k, key, KIT_ICON_REFERENCES, r->uses[i].text, NULL);
    }
    if (count > LISTED) {
        more_row(k, count - LISTED);
    }
    kit_menu_separator(k, "sep");
    kit_menu_item_desc d = { .label = "Delete", .icon = KIT_ICON_TRASH, .checked = -1 };
    if (kit_menu_item(k, "apply", &d)) {
        confirm_delete(s);
    }
}

void studio_refactor_frame(studio *s)
{
    kit *k = s->kit;
    studio_refactor *r = &s->refactor;
    if (r->undo_toast && kit_toast_action(k, r->undo_toast)) {
        r->undo_toast = 0;
        app_refactor *undo = r->undo;
        r->undo = NULL;
        app_refactor *redo = NULL;
        char error[256] = "";
        if (apply_plan(s, undo, &redo, error, sizeof error) == 0) {
            studio_report(s, "Undo");
        } else {
            refused(s, "Can't undo", error);
        }
        app_refactor_free(undo);
        app_refactor_free(redo);
    }
    if (r->opening) {
        kit_popup_open_at(k, POPOVER, s->in.mouse_x, s->in.mouse_y);
        r->opening = false;
    }
    if (!kit_popup_begin(k, POPOVER, "menu refactor")) {
        if (r->mode) {
            close_state(r);
        }
        return;
    }
    kit_menu_group(k, "title", r->title);
    if (r->mode == STUDIO_REFACTOR_RENAME) {
        rename_body(s);
    } else if (r->mode == STUDIO_REFACTOR_DELETE) {
        delete_body(s);
    }
    kit_popup_end(k);
    /* Enter applies without activating an item, which would close it. */
    if (!r->mode) {
        kit_popup_close(k, POPOVER);
    }
    r->shown = r->mode != STUDIO_REFACTOR_CLOSED;
}

void studio_refactor_free(studio *s)
{
    close_state(&s->refactor);
    app_refactor_free(s->refactor.undo);
    s->refactor.undo = NULL;
}
